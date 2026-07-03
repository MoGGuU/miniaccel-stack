// SPDX-License-Identifier: GPL-2.0
#include <linux/cdev.h>
#include <linux/completion.h>
#include <linux/device/class.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/idr.h>
#include <linux/iopoll.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <miniaccel_uapi.h>

#define DRV_NAME "miniaccel_drv"

#define MINIACCEL_MAX_DEVICES 256
#define EDU_FACTORIAL_MAX_INPUT		12
#define MINIACCEL_MAX_TIMEOUT_MS	10000
#define MINIACCEL_POLL_INTERVAL_US	1000

#define EDU_REG_IDENT		0x00 /* RO: 版本号 0x010000edu */
#define EDU_REG_LIVENESS	0x04 /* RW: 读回 ~val */
#define EDU_REG_FACTORIAL	0x08 /* RW: 阶乘 */
#define EDU_REG_STATUS		0x20 /* RW: 状态 */
#define EDU_REG_IRQ_STATUS	0x24 /* RO: 中断状态 */
#define EDU_REG_RAISE_IRQ	0x60 /* WO: 触发中断 */
#define EDU_REG_ACK_IRQ		0x64 /* WO: 确认中断 */
#define EDU_REG_DMA_SRC		0x80
#define EDU_REG_DMA_DST		0x88
#define EDU_REG_DMA_CNT		0x90
#define EDU_REG_DMA_CMD		0x98

#define EDU_IDENT_MASK		0xfff
#define EDU_IDENT_VALUE		0x0ed
#define EDU_STATUS_COMPUTING	BIT(0)
#define EDU_LIVENESS_TEST	0x12345678

#define EDU_IRQ_FACTORIAL	BIT(0)
#define EDU_STATUS_IRQFACT	BIT(7)

static dev_t miniaccel_base_devt;
static struct class *miniaccel_class;
static DEFINE_IDA(miniaccel_minor_ida);
static bool force_timeout;

module_param(force_timeout, bool, 0644);
MODULE_PARM_DESC(force_timeout, "Force RUN_SYNC timeout for IRQ wait tests");

struct miniaccel_dev {
	struct pci_dev *pdev;
	void __iomem *regs;
	resource_size_t bar0_len;

	struct cdev cdev;
	dev_t devt;
	struct device *chardev;
	int minor;

	u32 device_id;
	u32 version;

	/* Serializes RUN_SYNC MMIO submissions for this device. */
	struct mutex submit_lock;

	int irq;
	struct completion cmd_done;
	wait_queue_head_t event_wq;
	/* Protects IRQ state shared with process context. */
	spinlock_t irq_lock;

	u32 irq_status;
	u64 irq_count;
	u64 submit_seqno;
	u64 done_seqno;
	bool in_flight;

};

struct miniaccel_file {
	struct miniaccel_dev *mdev;
	u64 seen_seqno;
};

/* ---------- 1. PCI ID 表 ---------- */
/* 列出本驱动匹配的 vendor/device。pci_device_id 数组必须以全零项结尾。 */
static const struct pci_device_id miniaccel_id_table[] = {
	{ PCI_DEVICE(0x1234, 0x11e8) }, /* QEMU EDU */
	{ 0, } /* sentinel */
};
MODULE_DEVICE_TABLE(pci, miniaccel_id_table);

static int miniaccel_write32(struct miniaccel_dev *mdev, u32 offset, u32 value);
static int miniaccel_read32(struct miniaccel_dev *mdev, u32 offset, u32 *value);

static long miniaccel_ioctl_query(struct miniaccel_dev *mdev, void __user *argp)
{
	struct miniaccel_query query = {};

	query.device_id = mdev->device_id;
	query.version = mdev->version;
	query.capabilities = MINIACCEL_CAP_RUN_SYNC;

	if (copy_to_user(argp, &query, sizeof(query)))
		return -EFAULT;

	return 0;
}

static long miniaccel_ioctl_run_sync(struct miniaccel_dev *mdev,
				     void __user *argp)
{
	struct miniaccel_run_sync run;
	unsigned long flags;
	unsigned long timeout_jiffies;
	u32 status;
	int ret;

	if (copy_from_user(&run, argp, sizeof(run)))
		return -EFAULT;

	if (run.reserved)
		return -EINVAL;

	if (!run.timeout_ms || run.timeout_ms > MINIACCEL_MAX_TIMEOUT_MS)
		return -EINVAL;

	if (run.input > EDU_FACTORIAL_MAX_INPUT)
		return -EINVAL;

	timeout_jiffies = msecs_to_jiffies(run.timeout_ms);

	mutex_lock(&mdev->submit_lock);

	ret = miniaccel_read32(mdev, EDU_REG_STATUS, &status);
	if (ret)
		goto unlock;

	if (status & EDU_STATUS_COMPUTING) {
		ret = -EBUSY;
		goto unlock;
	}

	spin_lock_irqsave(&mdev->irq_lock, flags);
	if (mdev->in_flight) {
		spin_unlock_irqrestore(&mdev->irq_lock, flags);
		ret = -EBUSY;
		goto unlock;
	}
	reinit_completion(&mdev->cmd_done);
	mdev->submit_seqno++;
	mdev->in_flight = true;
	spin_unlock_irqrestore(&mdev->irq_lock, flags);

	if (force_timeout) {
		ret = -ETIMEDOUT;
		goto clear_in_flight;
	}

	ret = miniaccel_write32(mdev, EDU_REG_STATUS, EDU_STATUS_IRQFACT);
	if (ret)
		goto clear_in_flight;

	ret = miniaccel_write32(mdev, EDU_REG_FACTORIAL, run.input);
	if (ret)
		goto clear_in_flight;

	if (!wait_for_completion_timeout(&mdev->cmd_done, timeout_jiffies)) {
		ret = -ETIMEDOUT;
		goto clear_in_flight;
	}

	ret = miniaccel_read32(mdev, EDU_REG_FACTORIAL, &run.output);
	if (ret)
		goto unlock;

	mutex_unlock(&mdev->submit_lock);

	if (copy_to_user(argp, &run, sizeof(run)))
		return -EFAULT;

	return 0;

clear_in_flight:
	spin_lock_irqsave(&mdev->irq_lock, flags);
	if (mdev->in_flight)
		mdev->in_flight = false;
	spin_unlock_irqrestore(&mdev->irq_lock, flags);

unlock:
	mutex_unlock(&mdev->submit_lock);
	return ret;
}

static int miniaccel_open(struct inode *inode, struct file *file)
{
	struct miniaccel_file *mfile;
	struct miniaccel_dev *mdev;
	unsigned long flags;

	mdev = container_of(inode->i_cdev, struct miniaccel_dev, cdev);

	mfile = kzalloc(sizeof(*mfile), GFP_KERNEL);
	if (!mfile)
		return -ENOMEM;
	mfile->mdev = mdev;

	spin_lock_irqsave(&mdev->irq_lock, flags);
	mfile->seen_seqno = mdev->done_seqno;
	spin_unlock_irqrestore(&mdev->irq_lock, flags);

	file->private_data = mfile;

	return 0;
}

static int miniaccel_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;
	return 0;
}

static long miniaccel_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	struct miniaccel_file *mfile = file->private_data;
	struct miniaccel_dev *mdev;
	void __user *argp = (void __user *)arg;

	if (!mfile || !mfile->mdev)
		return -ENODEV;

	mdev = mfile->mdev;

	switch (cmd) {
	case MINIACCEL_IOCTL_QUERY:
		return miniaccel_ioctl_query(mdev, argp);
	case MINIACCEL_IOCTL_RUN_SYNC:
		return miniaccel_ioctl_run_sync(mdev, argp);
	default:
		return -ENOTTY;
	}
}

static __poll_t miniaccel_poll(struct file *file, poll_table *wait)
{
	struct miniaccel_file *mfile = file->private_data;
	struct miniaccel_dev *mdev;
	unsigned long flags;
	__poll_t mask = 0;

	if (!mfile || !mfile->mdev)
		return EPOLLERR;

	mdev = mfile->mdev;

	poll_wait(file, &mdev->event_wq, wait);

	spin_lock_irqsave(&mdev->irq_lock, flags);
	if (mdev->done_seqno > mfile->seen_seqno) {
		mfile->seen_seqno = mdev->done_seqno;
		mask = EPOLLIN | EPOLLRDNORM;
	}
	spin_unlock_irqrestore(&mdev->irq_lock, flags);

	return mask;
}

static const struct file_operations miniaccel_ops = {
	.owner		= THIS_MODULE,
	.open		= miniaccel_open,
	.release	= miniaccel_release,
	.unlocked_ioctl	= miniaccel_ioctl,
	.poll		= miniaccel_poll,
};

static int miniaccel_validate_mmio32(struct miniaccel_dev *mdev, u32 offset)
{
	if (!mdev->regs)
		return -ENODEV;

	if (!IS_ALIGNED(offset, sizeof(u32)))
		return -EINVAL;

	if (mdev->bar0_len < sizeof(u32) ||
	    offset > mdev->bar0_len - sizeof(u32))
		return -ERANGE;

	return 0;
}

static int miniaccel_read32(struct miniaccel_dev *mdev, u32 offset, u32 *value)
{
	int ret;

	ret = miniaccel_validate_mmio32(mdev, offset);
	if (ret)
		return ret;

	*value = ioread32(mdev->regs + offset);
	return 0;
}

static int miniaccel_write32(struct miniaccel_dev *mdev, u32 offset, u32 value)
{
	int ret;

	ret = miniaccel_validate_mmio32(mdev, offset);
	if (ret)
		return ret;

	iowrite32(value, mdev->regs + offset);
	return 0;
}

static irqreturn_t miniaccel_irq_handler(int irq, void *dev_id)
{
	struct miniaccel_dev *mdev = dev_id;
	bool cmd_done = false;
	unsigned long flags;
	u32 status;
	int ret;

	ret = miniaccel_read32(mdev, EDU_REG_IRQ_STATUS, &status);
	if (ret || !status)
		return IRQ_NONE;

	ret = miniaccel_write32(mdev, EDU_REG_ACK_IRQ, status);
	if (ret)
		return IRQ_HANDLED;

	spin_lock_irqsave(&mdev->irq_lock, flags);
	mdev->irq_status = status;
	mdev->irq_count++;
	if ((status & EDU_IRQ_FACTORIAL) && mdev->in_flight) {
		mdev->done_seqno = mdev->submit_seqno;
		mdev->in_flight = false;
		cmd_done = true;
	}
	spin_unlock_irqrestore(&mdev->irq_lock, flags);

	if (cmd_done) {
		complete(&mdev->cmd_done);
		wake_up_interruptible(&mdev->event_wq);
	}

	return IRQ_HANDLED;
}

/* ---------- 2. probe: 设备匹配成功后调用 ---------- */
/* 返回 0 = 成功绑定; 负数 = 失败, core 会继续找别的驱动 */
static int miniaccel_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int ret;
	int nr_vecs;
	struct miniaccel_dev *mdev;
	resource_size_t bar0_start;
	resource_size_t bar0_end;
	unsigned long bar0_flags;
	u32 version;
	u32 status;
	u32 liveness;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	mdev->pdev = pdev;
	mdev->device_id = pdev->device;
	dev_info(&pdev->dev, "probe called for %04x:%04x\n", pdev->vendor,
		 pdev->device);

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable device: %d\n", ret);
		goto err_free;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
	if (ret) {
		dev_err(&pdev->dev, "failed to set DMA mask: %d\n", ret);
		goto err_disable_device;
	}

	bar0_start = pci_resource_start(pdev, 0);
	mdev->bar0_len = pci_resource_len(pdev, 0);
	bar0_flags = pci_resource_flags(pdev, 0);
	bar0_end = pci_resource_end(pdev, 0);

	dev_info(&pdev->dev, "BAR0: start=%pa end=%pa len=%pa flags=0x%lx\n",
		 &bar0_start, &bar0_end, &mdev->bar0_len, bar0_flags);

	if (!(bar0_flags & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "BAR0 is not MMIO\n");
		ret = -ENODEV;
		goto err_disable_device;
	}

	if (!mdev->bar0_len) {
		dev_err(&pdev->dev, "BAR0 has zero length\n");
		ret = -ENODEV;
		goto err_disable_device;
	}

	ret = pci_request_regions(pdev, "miniaccel");
	if (ret) {
		dev_err(&pdev->dev, "failed to request regions: %d\n", ret);
		goto err_disable_device;
	}

	pci_set_master(pdev);
	mdev->regs = pci_iomap(pdev, 0, 0);
	if (!mdev->regs) {
		dev_err(&pdev->dev, "failed to iomap BAR0\n");
		ret = -ENOMEM;
		goto err_release_regions;
	}

	ret = miniaccel_read32(mdev, EDU_REG_IDENT, &version);
	if (ret)
		goto err_iounmap;

	dev_info(&pdev->dev, "EDU identifier: 0x%08x\n", version);
	if ((version & EDU_IDENT_MASK) != EDU_IDENT_VALUE) {
		dev_err(&pdev->dev,
			"EDU identifier mismatch (got 0x%03x, expected 0x%03x)\n",
			version & EDU_IDENT_MASK, EDU_IDENT_VALUE);
		ret = -ENODEV;
		goto err_iounmap;
	}

	mdev->version = version;

	ret = miniaccel_read32(mdev, EDU_REG_STATUS, &status);
	if (ret)
		goto err_iounmap;

	dev_info(&pdev->dev, "status is %s\n",
		 status & EDU_STATUS_COMPUTING ? "computing" : "idle");

	ret = miniaccel_write32(mdev, EDU_REG_LIVENESS, EDU_LIVENESS_TEST);
	if (ret)
		goto err_iounmap;

	ret = miniaccel_read32(mdev, EDU_REG_LIVENESS, &liveness);
	if (ret)
		goto err_iounmap;

	if (liveness != ~EDU_LIVENESS_TEST) {
		dev_err(&pdev->dev,
			"liveness mismatch: got 0x%08x, expected 0x%08x\n",
			liveness, ~EDU_LIVENESS_TEST);
		ret = -EIO;
		goto err_iounmap;
	}

	mdev->irq = -1;
	init_completion(&mdev->cmd_done);
	init_waitqueue_head(&mdev->event_wq);
	spin_lock_init(&mdev->irq_lock);
	mutex_init(&mdev->submit_lock);

	nr_vecs = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX | PCI_IRQ_MSI);
	if (nr_vecs < 0) {
		ret = nr_vecs;
		dev_err(&pdev->dev, "failed to allocate IRQ vectors: %d\n", ret);
		goto err_iounmap;
	}
	mdev->irq = pci_irq_vector(pdev, 0);
	if (mdev->irq < 0) {
		ret = mdev->irq;
		dev_err(&pdev->dev, "failed to get IRQ vector 0: %d\n", ret);
		goto err_free_irq_vectors;
	}
	ret = request_irq(mdev->irq, miniaccel_irq_handler, 0, DRV_NAME, mdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ %d: %d\n", mdev->irq, ret);
		goto err_free_irq_vectors;
	}
	dev_info(&pdev->dev, "allocated %d IRQ vector(s), linux irq=%d\n",
		 nr_vecs, mdev->irq);

	ret = ida_alloc_range(&miniaccel_minor_ida, 0,
			      MINIACCEL_MAX_DEVICES - 1, GFP_KERNEL);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to allocate minor: %d\n", ret);
		goto err_free_irq;
	}

	mdev->minor = ret;
	mdev->devt = MKDEV(MAJOR(miniaccel_base_devt),
			   MINOR(miniaccel_base_devt) + mdev->minor);
	cdev_init(&mdev->cdev, &miniaccel_ops);
	mdev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&mdev->cdev, mdev->devt, 1);
	if (ret) {
		dev_err(&pdev->dev, "failed to add cdev: %d\n", ret);
		goto err_free_minor;
	}
	mdev->chardev = device_create(miniaccel_class, &pdev->dev, mdev->devt,
				      mdev, "miniaccel%d", mdev->minor);

	if (IS_ERR(mdev->chardev)) {
		ret = PTR_ERR(mdev->chardev);
		dev_err(&pdev->dev, "failed to create device: %d\n", ret);
		goto err_cdev_del;
	}

	pci_set_drvdata(pdev, mdev);
	dev_info(&pdev->dev,
		 "probe ok: regs=%p version=0x%08x liveness=0x%08x\n",
		 mdev->regs, version, liveness);

	return 0;

err_cdev_del:
	cdev_del(&mdev->cdev);
err_free_minor:
	ida_free(&miniaccel_minor_ida, mdev->minor);
err_free_irq:
	free_irq(mdev->irq, mdev);
err_free_irq_vectors:
	pci_free_irq_vectors(pdev);
err_iounmap:
	pci_iounmap(pdev, mdev->regs);
err_release_regions:
	pci_release_regions(pdev);
err_disable_device:
	pci_disable_device(pdev);
err_free:
	kfree(mdev);
	return ret;
}

/* ---------- 3. remove: rmmod 或设备热拔时调用 ---------- */
static void miniaccel_remove(struct pci_dev *pdev)
{
	struct miniaccel_dev *mdev = pci_get_drvdata(pdev);

	pci_set_drvdata(pdev, NULL);

	device_destroy(miniaccel_class, mdev->devt);
	cdev_del(&mdev->cdev);
	ida_free(&miniaccel_minor_ida, mdev->minor);
	free_irq(mdev->irq, mdev);
	pci_free_irq_vectors(pdev);
	if (mdev->regs)
		pci_iounmap(pdev, mdev->regs);

	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(mdev);

	dev_info(&pdev->dev, "device removed\n");
}

/* ---------- 4. pci_driver 结构 ---------- */
/* 把 probe/remove/id_table 装到一起, 给 pci_register_driver 用. */
static struct pci_driver miniaccel_driver = {
	.name = DRV_NAME,
	.id_table = miniaccel_id_table,
	.probe = miniaccel_probe,
	.remove = miniaccel_remove,
};

/* ---------- 5. 模块入口 / 出口 ---------- */
static int __init miniaccel_drv_init(void)
{
	int ret;

	pr_info("%s: module_init\n", DRV_NAME);

	ret = alloc_chrdev_region(&miniaccel_base_devt, 0,
				  MINIACCEL_MAX_DEVICES, "miniaccel");
	if (ret) {
		pr_err("%s: failed to allocate character device numbers: %d\n",
		       DRV_NAME, ret);
		goto err_alloc;
	}

	miniaccel_class = class_create(THIS_MODULE, "miniaccel");
	if (IS_ERR(miniaccel_class)) {
		ret = PTR_ERR(miniaccel_class);
		pr_err("%s: failed to create device class: %d\n", DRV_NAME,
		       ret);
		goto err_class;
	}
	ret = pci_register_driver(&miniaccel_driver);
	if (ret) {
		pr_err("%s: failed to register PCI driver: %d\n", DRV_NAME,
		       ret);
		goto err_pci_register;
	}

	return ret;

err_pci_register:
	class_destroy(miniaccel_class);
err_class:
	unregister_chrdev_region(miniaccel_base_devt, MINIACCEL_MAX_DEVICES);
err_alloc:
	return ret;
}

static void __exit miniaccel_drv_exit(void)
{
	pci_unregister_driver(&miniaccel_driver);

	class_destroy(miniaccel_class);
	unregister_chrdev_region(miniaccel_base_devt, MINIACCEL_MAX_DEVICES);
	ida_destroy(&miniaccel_minor_ida);

	pr_info("%s: module_exit\n", DRV_NAME);
}

module_init(miniaccel_drv_init);
module_exit(miniaccel_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MiniAccel");
MODULE_DESCRIPTION("Minimal PCI driver skeleton for QEMU EDU (1234:11e8)");
MODULE_VERSION("0.1");
