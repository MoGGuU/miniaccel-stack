# 第 2 周 Day 1-5 SOP：BAR0 映射与 MMIO 读取

本文记录 MiniAccel 第 2 周当前已经实际完成的 PCI 资源和 MMIO 路径：
观察 BAR0、启用设备、配置 DMA mask、申请 BAR、映射 MMIO，并读取 QEMU
EDU 的 identification 和 status 寄存器。

最终验收结果：

1. Guest 能直接通过 9p 访问宿主机源码，并在共享目录生成
   `miniaccel_drv.ko`。
2. 驱动能打印 BAR0 的 start、end、length 和 flags。
3. `pci_request_regions()` 成功后，`/proc/iomem` 显示 BAR0 由
   `miniaccel` 占用。
4. `pci_iomap()` 成功，`ioread32()` 读到 EDU identification
   `0x010000ed`，status 为 idle。
5. `remove()` 能按逆序释放映射、BAR region、PCI device 和私有结构。
6. 单次 bind/unbind 和 100 次 reload 测试通过。

当前未完全验收：

- `make W=1` 仍报告 `miniaccel_write32()` 未使用。
- 尚未人工注入每个 probe 失败点。
- 尚未编写 Week 2 专项资源测试脚本。

## 1. 当前上下文

### 1.0 实际执行记录

执行日期：2026-06-23

| 项目 | 实际值 |
|---|---|
| 宿主机工作目录 | `~/Miniaccel/miniaccel-stack` |
| Guest kernel | `6.1.0-49-amd64` |
| Guest headers | `/lib/modules/6.1.0-49-amd64/build` |
| 共享源码目录 | `/mnt/miniaccel` |
| 共享方式 | QEMU virtio-9p |
| EDU BDF | `00:02.0` |
| EDU PCI ID | `1234:11e8` |
| BAR0 | `0xfea00000..0xfeafffff`，1 MiB MMIO |

本次是否完整跑通：

- Day 1-Day 5 的正常路径已跑通。
- Day 6 只验证了正常卸载和 reload，未完成人工失败注入。
- Day 7 专项脚本和封版尚未执行。

### 1.1 已完成前置条件

- Week 1 的 probe/remove 和私有结构生命周期已经完成。
- QEMU 启动参数包含 `-device edu`。
- Guest generic kernel 包含 `9p`、`9pnet`、`9pnet_virtio`。
- 宿主仓库自动挂载到 Guest 的 `/mnt/miniaccel`。

### 1.2 本周当前不做

- 不实现字符设备和 ioctl。
- 不处理中断。
- 不分配 DMA buffer。
- 不启动 EDU factorial 或 DMA 任务。
- 不访问 `0x80` 之后的 64 位 DMA register。

## 2. 目标资源链

```text
probe()
  -> kzalloc(struct miniaccel_dev)
  -> pci_enable_device()
  -> dma_set_mask_and_coherent()
  -> 读取 BAR0 start/end/len/flags
  -> pci_request_regions()
  -> pci_set_master()
  -> pci_iomap(BAR0)
  -> miniaccel_read32(IDENT)
  -> miniaccel_read32(STATUS)

remove()
  -> pci_iounmap()
  -> pci_release_regions()
  -> pci_disable_device()
  -> pci_set_drvdata(NULL)
  -> kfree()
```

失败回滚遵循同一条链的逆序：

```text
IDENT 失败
  -> iounmap
  -> release regions
  -> disable device
  -> clear drvdata
  -> free

iomap 失败
  -> release regions
  -> disable device
  -> clear drvdata
  -> free

request regions 或 DMA mask 失败
  -> disable device
  -> clear drvdata
  -> free
```

## 3. 9p 共享开发目录

### 3.1 QEMU 暴露宿主目录

`scripts/run-qemu.sh` 使用：

```bash
-fsdev "local,id=miniaccel_fs,path=$ROOT_DIR,security_model=mapped-xattr" \
-device "virtio-9p-pci,fsdev=miniaccel_fs,mount_tag=miniaccel" \
```

Guest 的 `/etc/fstab` 包含：

```text
miniaccel /mnt/miniaccel 9p trans=virtio,version=9p2000.L,rw,nofail,x-systemd.automount 0 0
```

实际观察：

```bash
mount | grep /mnt/miniaccel
```

```text
miniaccel on /mnt/miniaccel type 9p (rw,relatime,sync,dirsync,access=client,trans=virtio,x-systemd.automount)
```

验收：

- 宿主机修改源码后，Guest 立即从 `/mnt/miniaccel` 看到最新内容。
- 不再需要每次通过 `scp` 同步驱动源码。
- 编译命令仍由 Guest 执行，因此模块匹配 Guest kernel。

## 4. Day 1：确认 BAR0 信息

### 4.1 用 lspci 和 sysfs 观察

执行：

```bash
lspci -vv -s 00:02.0
head -1 /sys/bus/pci/devices/0000:00:02.0/resource
```

实际输出摘录：

```text
00:02.0 Unclassified device [00ff]: Device 1234:11e8 (rev 10)
	Region 0: Memory at fea00000 (32-bit, non-prefetchable) [size=1M]

0x00000000fea00000 0x00000000feafffff 0x0000000000040200
```

验收答案：

| 字段 | 实际值 |
|---|---|
| BAR index | BAR0 |
| 类型 | MMIO / `IORESOURCE_MEM` |
| start | `0xfea00000` |
| end | `0xfeafffff` |
| length | `0x100000`，即 1 MiB |
| prefetchable | 否 |

### 4.2 驱动读取并打印 resource

驱动使用：

```c
bar0_start = pci_resource_start(pdev, 0);
mdev->bar0_len = pci_resource_len(pdev, 0);
bar0_flags = pci_resource_flags(pdev, 0);
bar0_end = pci_resource_end(pdev, 0);
```

实际 dmesg：

```text
miniaccel_drv 0000:00:02.0: BAR0: start=0x00000000fea00000 end=0x00000000feafffff len=0x0000000000100000 flags=0x40200
```

驱动还会检查：

```c
if (!(bar0_flags & IORESOURCE_MEM))
	return -ENODEV;

if (!mdev->bar0_len)
	return -ENODEV;
```

验收：

- `lspci`、sysfs 和 KMD 打印的地址及长度一致。
- 驱动不会把 I/O port BAR 当成 MMIO 使用。
- 长度为零时不会继续 request/iomap。

## 5. Day 2：启用设备和 DMA mask

驱动顺序：

```c
ret = pci_enable_device(pdev);
if (ret)
	goto err_free;

ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
if (ret)
	goto err_disable;

pci_set_master(pdev);
```

本次使用 28-bit DMA mask，是为了和 QEMU EDU 默认 DMA 地址限制保持一致。
当前阶段没有真正提交 DMA transaction。

验收：

- `pci_enable_device()` 成功后才继续访问 PCI resource。
- DMA mask 失败会执行 `pci_disable_device()` 和私有结构释放。
- `pci_set_master()` 在 request regions 成功后执行。
- 模块卸载后能够再次加载。

与原计划的差异：

- 原计划要求先尝试 64-bit、失败后降级 32-bit。
- 当前实现选择 EDU 的 28-bit mask；后续迁移到自定义 MiniAccel 设备时应重新
  设计 DMA addressing capability。

## 6. Day 3：申请 BAR region

驱动使用：

```c
ret = pci_request_regions(pdev, "miniaccel");
if (ret)
	goto err_disable;
```

申请成功后执行：

```c
pci_set_master(pdev);
```

加载模块时观察：

```bash
grep -i miniaccel /proc/iomem
```

实际输出：

```text
fea00000-feafffff : miniaccel
```

验收：

- BAR0 region 被当前驱动占用。
- request 失败时不会错误调用 `pci_release_regions()`。
- `remove()` 中会调用 `pci_release_regions()`。

## 7. Day 4：映射 BAR0 和封装 MMIO

私有结构保存：

```c
struct miniaccel_dev {
	struct pci_dev *pdev;
	void __iomem *regs;
	resource_size_t bar0_len;
};
```

映射：

```c
mdev->regs = pci_iomap(pdev, 0, 0);
if (!mdev->regs) {
	ret = -ENOMEM;
	goto err_request_region;
}
```

32 位读 helper：

```c
static u32 miniaccel_read32(struct miniaccel_dev *mdev, u32 offset)
{
	if (offset > mdev->bar0_len - sizeof(u32))
		return ~0U;

	return ioread32(mdev->regs + offset);
}
```

32 位写 helper：

```c
static void miniaccel_write32(struct miniaccel_dev *mdev,
			      u32 offset, u32 value)
{
	iowrite32(value, mdev->regs + offset);
}
```

验收：

- BAR0 映射结果非空。
- MMIO 访问统一使用 `ioread32()` / `iowrite32()`。
- 没有把 `__iomem` 指针当普通内存直接解引用。

当前待修正：

- 边界检查应先判断 `bar0_len < sizeof(u32)`，避免无符号减法下溢。
- `miniaccel_write32()` 也应加入对齐和边界检查。
- `miniaccel_write32()` 当前未被调用，`W=1` 会报告 unused warning。
- 后续可将字段名 `regs` 统一为更明确的 `bar0`。

## 8. Day 5：读取 EDU 寄存器

当前读取：

```c
version = miniaccel_read32(mdev, EDU_REG_IDENT);
status = miniaccel_read32(mdev, EDU_REG_STATUS);
```

实际 dmesg：

```text
miniaccel_drv 0000:00:02.0: EDU identifier: 0x010000ed
miniaccel_drv 0000:00:02.0: status is idle
miniaccel_drv 0000:00:02.0: probe ok: regs=00000000b566009a version=0x010000ed
```

IDENT 检查：

```c
if ((version & 0xfff) != 0xedu) {
	ret = -ENODEV;
	goto err_ioremap;
}
```

验收：

- identification 稳定读到 `0x010000ed`。
- 低 12 位为 `0x0ed`，与 QEMU EDU 定义一致。
- status 本次读到 idle。
- ID 不匹配会进入完整 MMIO 资源回滚路径。

## 9. 编译与运行验证

### 9.1 在共享目录编译

Guest 内执行：

```bash
cd /mnt/miniaccel/driver/char
make clean
make W=1
```

实际输出摘录：

```text
CC [M]  /mnt/miniaccel/driver/char/miniaccel_drv.o
warning: 'miniaccel_write32' defined but not used
MODPOST /mnt/miniaccel/driver/char/Module.symvers
LD [M]  /mnt/miniaccel/driver/char/miniaccel_drv.ko
Skipping BTF generation ... due to unavailability of vmlinux
```

验收：

- `.ko` 成功生成。
- 仍有一个需要清理的 C warning，当前不能写成“W=1 无 warning”。
- 9p 构建偶尔提示 clock skew，是宿主与 Guest 文件时间存在亚秒偏差；
  本次产物仍正常生成并成功加载。

### 9.2 加载和卸载

执行：

```bash
cd /mnt/miniaccel/driver/char
insmod miniaccel_drv.ko
lspci -nnk -s 00:02.0
dmesg | grep miniaccel_drv
rmmod miniaccel_drv
```

实际输出摘录：

```text
Kernel driver in use: miniaccel_drv
miniaccel_drv 0000:00:02.0: probe called for 1234:11e8
miniaccel_drv 0000:00:02.0: BAR0: start=0x00000000fea00000 end=0x00000000feafffff len=0x0000000000100000 flags=0x40200
miniaccel_drv 0000:00:02.0: EDU identifier: 0x010000ed
miniaccel_drv 0000:00:02.0: status is idle
miniaccel_drv 0000:00:02.0: remove called for 1234:11e8
```

### 9.3 生命周期回归

执行：

```bash
cd /mnt/miniaccel
./tests/probe/test_bind_unbind.sh
./tests/probe/test_reload_100.sh
```

实际输出：

```text
bind-unbind-ok
reload-100-ok
no-recent-oops-warning-panic
```

验收：

- 正常申请和释放路径可以重复执行。
- 没有观察到 resource busy、Oops、BUG、WARNING、Call Trace 或 panic。

## 10. 踩坑与修正

| 问题 | 现象 | 原因 | 修正 |
|---|---|---|---|
| cloud kernel 无法挂载 9p | `unknown filesystem type '9p'` | `6.1.0-49-cloud-amd64` 没有安装 9p 模块 | 安装并启动 `6.1.0-49-amd64` generic kernel |
| 仍在 Guest 旧目录看源码 | `~/miniaccel-stack` 内容不是最新版本 | 这是以前 scp 的本地副本 | 使用 `/mnt/miniaccel`，或把旧目录替换为软链接 |
| request regions 失败后 release | 失败路径可能释放未成功申请的资源 | label 按失败位置命名，跳转层级混乱 | request 失败直接跳到 disable；仅成功 request 后的失败才 release |
| remove 中先 free 再访问 regs | 存在 use-after-free | 没按资源申请逆序释放 | 先 iounmap/release/disable，最后 kfree |
| 用户态整数头文件 | 内核构建找不到 `stdint.h` / `cstddef` | 内核不使用用户态/C++ 标准头文件 | 使用内核类型 `u32` / `u64` |
| 32 位边界检查减法下溢 | BAR 长度小于 4 时判断失效 | `resource_size_t` 是无符号类型 | 先检查 `bar0_len < sizeof(u32)` |
| 非交互 SSH 裸 dmesg 失败 | `Operation not permitted` | 自动 root 只对带 TTY 的交互 shell 生效 | 交互登录直接使用；脚本中继续使用 `sudo dmesg` |

## 11. 当前产物

- `driver/char/miniaccel_drv.c`
- `driver/char/Makefile`
- `scripts/run-qemu.sh` 中的 9p 设备参数
- Guest generic kernel 和自动 9p mount
- `docs/week-02-mmio.md`

## 12. 下一步

1. 清理 `miniaccel_write32()` 未使用 warning。
2. 完成 read/write helper 的长度和对齐检查。
3. 用显式失败注入逐条验证 Day 6 回滚路径。
4. 新增 `tests/probe/test_pci_resources.sh`。
5. 完成 Day 7 后再将 Week 2 标记为完整封版。
