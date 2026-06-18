#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#define DRV_NAME "miniaccel_drv"

struct miniaccel_dev {
	struct pci_dev *pdev;
};

/* ---------- 1. PCI ID 表 ---------- */
/* 列出本驱动匹配的 vendor/device。pci_device_id 数组必须以全零项结尾。 */
static const struct pci_device_id miniaccel_id_table[] = {
	{ PCI_DEVICE(0x1234, 0x11e8) },	/* QEMU EDU */
	{ 0, }				/* sentinel */
};
MODULE_DEVICE_TABLE(pci, miniaccel_id_table);

/* ---------- 2. probe: 设备匹配成功后调用 ---------- */
/* 返回 0 = 成功绑定; 负数 = 失败, core 会继续找别的驱动 */
static int miniaccel_probe(struct pci_dev *pdev,
			   const struct pci_device_id *id)
{
	struct miniaccel_dev *mdev;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev) {
		dev_err(&pdev->dev, "failed to allocate private data\n");
		return -ENOMEM;
	}

	mdev->pdev = pdev;
	pci_set_drvdata(pdev, mdev);

	dev_info(&pdev->dev, "probe called for %04x:%04x\n",
		 pdev->vendor, pdev->device);

	return 0;
}

/* ---------- 3. remove: rmmod 或设备热拔时调用 ---------- */
static void miniaccel_remove(struct pci_dev *pdev)
{
	struct miniaccel_dev *mdev = pci_get_drvdata(pdev);

	pci_set_drvdata(pdev, NULL);
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
