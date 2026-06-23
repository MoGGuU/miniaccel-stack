// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#define DRV_NAME "miniaccel_drv"

#define EDU_REG_IDENT		0x00	/* RO: 版本号 0x010000edu */
#define EDU_REG_LIVENESS	0x04	/* RW: 读回 ~val */
#define EDU_REG_FACTORIAL	0x08	/* RW: 阶乘 */
#define EDU_REG_STATUS		0x20	/* RW: 状态 */
#define EDU_REG_IRQ_STATUS	0x24	/* RO: 中断状态 */
#define EDU_REG_RAISE_IRQ	0x60	/* WO: 触发中断 */
#define EDU_REG_ACK_IRQ		0x64	/* WO: 确认中断 */
#define EDU_REG_DMA_SRC		0x80
#define EDU_REG_DMA_DST		0x88
#define EDU_REG_DMA_CNT		0x90
#define EDU_REG_DMA_CMD		0x98

#define EDU_IDENT_MASK		0xfff
#define EDU_IDENT_VALUE		0x0ed
#define EDU_STATUS_COMPUTING	BIT(0)
#define EDU_LIVENESS_TEST	0x12345678

struct miniaccel_dev {
	struct pci_dev *pdev;
	void __iomem *regs;
	resource_size_t bar0_len;
};

/* ---------- 1. PCI ID 表 ---------- */
/* 列出本驱动匹配的 vendor/device。pci_device_id 数组必须以全零项结尾。 */
static const struct pci_device_id miniaccel_id_table[] = {
	{ PCI_DEVICE(0x1234, 0x11e8) },	/* QEMU EDU */
	{ 0, }				/* sentinel */
};
MODULE_DEVICE_TABLE(pci, miniaccel_id_table);

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

static int miniaccel_write32(struct miniaccel_dev *mdev,
			     u32 offset, u32 value)
{
	int ret;

	ret = miniaccel_validate_mmio32(mdev, offset);
	if (ret)
		return ret;

	iowrite32(value, mdev->regs + offset);
	return 0;
}

/* ---------- 2. probe: 设备匹配成功后调用 ---------- */
/* 返回 0 = 成功绑定; 负数 = 失败, core 会继续找别的驱动 */
static int miniaccel_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	int ret;
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

	dev_info(&pdev->dev, "probe called for %04x:%04x\n",
		 pdev->vendor, pdev->device);

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

	dev_info(&pdev->dev,
		 "BAR0: start=%pa end=%pa len=%pa flags=0x%lx\n",
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

	pci_set_drvdata(pdev, mdev);
	dev_info(&pdev->dev,
		 "probe ok: regs=%p version=0x%08x liveness=0x%08x\n",
		 mdev->regs, version, liveness);
	return 0;

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

	if (mdev->regs)
		pci_iounmap(pdev, mdev->regs);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	kfree(mdev);
	dev_info(&pdev->dev, "remove called for %04x:%04x\n",
		 pdev->vendor, pdev->device);
}

/* ---------- 4. pci_driver 结构 ---------- */
/* 把 probe/remove/id_table 装到一起, 给 pci_register_driver 用. */
static struct pci_driver miniaccel_driver = {
	.name		= DRV_NAME,
	.id_table	= miniaccel_id_table,
	.probe		= miniaccel_probe,
	.remove		= miniaccel_remove,
};

/* ---------- 5. 模块入口 / 出口 ---------- */
static int __init miniaccel_drv_init(void)
{
	int ret;

	pr_info("%s: module_init\n", DRV_NAME);

	ret = pci_register_driver(&miniaccel_driver);
	if (ret)
		pr_err("%s: failed to register PCI driver: %d\n",
		       DRV_NAME, ret);

	return ret;
}

static void __exit miniaccel_drv_exit(void)
{
	pci_unregister_driver(&miniaccel_driver);
	pr_info("%s: module_exit\n", DRV_NAME);
}

module_init(miniaccel_drv_init);
module_exit(miniaccel_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MiniAccel");
MODULE_DESCRIPTION("Minimal PCI driver skeleton for QEMU EDU (1234:11e8)");
MODULE_VERSION("0.1");
