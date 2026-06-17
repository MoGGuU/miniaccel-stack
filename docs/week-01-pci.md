# 第 1 周 Day 2 SOP：PCI 枚举与驱动匹配

本文记录 MiniAccel 第 1 周 Day 2 的实际执行过程。本文不是待填写讲义，而是
2026-06-17 在当前机器上真实跑过一遍后的实验记录：每一步都包含命令、实际输出、
结论和验收答案。

最终验收结果：

1. Guest 已枚举 EDU PCI 设备：`00:02.0`，PCI ID 为 `1234:11e8`。
2. BAR0 是 MMIO：地址范围 `0xfea00000..0xfeafffff`，长度 `0x100000`，即 1 MiB。
3. 当前没有绑定 Linux driver：`/sys/bus/pci/devices/0000:00:02.0/driver` 不存在。
4. QEMU 源码证据已定位：ID 在 `edu_class_init()`，BAR0 在 `pci_edu_realize()`。

## 1. 当前上下文

### 1.1 实际环境

在宿主机执行：

```bash
cd ~/Miniaccel/miniaccel-stack
qemu-system-x86_64 --version
qemu-system-x86_64 -device help | rg 'name "edu"'
```

实际输出：

```text
QEMU emulator version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.16)
Copyright (c) 2003-2023 Fabrice Bellard and the QEMU Project developers
name "edu", bus PCI
```

结论：

- 当前使用系统 QEMU，不是自编译 QEMU。
- 系统 QEMU 已包含 EDU PCI 设备。
- Day 2 继续只观察系统 QEMU 的 EDU 设备，不改 QEMU 源码。

### 1.2 已完成前置条件

第 1 周 Day 1 已完成：

- `scripts/run-qemu.sh` 可以启动 Debian Guest。
- `scripts/ssh-guest.sh` 可以登录 Guest。
- `scripts/verify-guest.sh` 可以等待 cloud-init 并检查 EDU 设备。
- `scripts/run-qemu.sh` 的 QEMU 参数包含 `-device edu`。

确认启动脚本中的设备参数：

```bash
rg -n -- '-device edu' scripts/run-qemu.sh
```

应看到：

```text
25:  -device edu \
```

### 1.3 本日不做

- 不修改 QEMU 源码。
- 不写 Linux KMD。
- 不调用 `pci_enable_device()`。
- 不申请或映射 BAR。
- 不读写 MMIO。
- 不处理中断、DMA、字符设备或 ioctl。

今天只回答一个问题：QEMU EDU 是怎样被 Guest Linux 枚举成 PCI 设备的，以及后续
KMD 为什么能通过 `1234:11e8` 匹配到它。

## 2. 启动 Guest 并确认 EDU 存在

### 2.1 启动 QEMU

在宿主机执行：

```bash
cd ~/Miniaccel/miniaccel-stack
./scripts/run-qemu.sh
```

本次实际输出：

```text
QEMU started with PID 9842; SSH port is 2222.
```

如果输出是：

```text
QEMU is already running with PID <pid>.
```

也可以继续后续步骤，说明 Guest 已经启动。

### 2.2 等待 Guest 就绪

执行：

```bash
./scripts/verify-guest.sh
```

本次实际输出：

```text
Waiting for SSH and cloud-init...
Guest is ready. EDU device: 00:02.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
```

验收答案：

| 字段 | 实际值 | 说明 |
|---|---|---|
| BDF | `00:02.0` | bus `00`，device `02`，function `0` |
| Class | `00ff` | unclassified / other |
| Vendor ID | `1234` | QEMU experimental vendor |
| Device ID | `11e8` | EDU device |
| Revision | `10` | QEMU EDU 设置的 revision |

结论：

- EDU 已经被 Guest Linux PCI core 枚举。
- 此时还没有加载我们自己的 KMD，所以这一步只能证明“设备存在”，不能证明“驱动绑定成功”。

## 3. 用 `lspci` 观察 PCI 配置结果

### 3.1 查看 Guest 内所有 PCI 设备

执行：

```bash
./scripts/ssh-guest.sh 'lspci -nn'
```

本次实际输出：

```text
00:00.0 Host bridge [0600]: Intel Corporation 82G33/G31/P35/P31 Express DRAM Controller [8086:29c0]
00:01.0 VGA compatible controller [0300]: Device [1234:1111] (rev 02)
00:02.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
00:03.0 Ethernet controller [0200]: Red Hat, Inc. Virtio network device [1af4:1000]
00:04.0 SCSI storage controller [0100]: Red Hat, Inc. Virtio block device [1af4:1001]
00:05.0 SCSI storage controller [0100]: Red Hat, Inc. Virtio block device [1af4:1001]
00:1f.0 ISA bridge [0601]: Intel Corporation 82801IB (ICH9) LPC Interface Controller [8086:2918] (rev 02)
00:1f.2 SATA controller [0106]: Intel Corporation 82801IR/IO/IH (ICH9R/DO/DH) 6 port SATA Controller [AHCI mode] [8086:2922] (rev 02)
00:1f.3 SMBus [0c05]: Intel Corporation 82801I (ICH9 Family) SMBus Controller [8086:2930] (rev 02)
```

验收答案：

- EDU 的 BDF 是 `00:02.0`。
- EDU 的 class 是 `[00ff]`。
- EDU 的 PCI ID 是 `[1234:11e8]`。
- EDU 的 revision 是 `rev 10`。

### 3.2 查看 EDU 的详细 PCI 信息

执行：

```bash
./scripts/ssh-guest.sh 'sudo lspci -vv -s 00:02.0'
```

本次实际输出：

```text
00:02.0 Unclassified device [00ff]: Device 1234:11e8 (rev 10)
	Subsystem: Red Hat, Inc. Device 1100
	Control: I/O+ Mem+ BusMaster- SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR+ FastB2B- DisINTx-
	Status: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-
	Interrupt: pin A routed to IRQ 11
	Region 0: Memory at fea00000 (32-bit, non-prefetchable) [size=1M]
	Capabilities: [40] MSI: Enable- Count=1/1 Maskable- 64bit+
		Address: 0000000000000000  Data: 0000
```

验收答案：

| 问题 | 答案 |
|---|---|
| BAR0 类型是什么 | `Memory`，即 MMIO |
| BAR0 Guest 物理起始地址是多少 | `0xfea00000` |
| BAR0 大小是多少 | `1M` |
| BAR0 是否 prefetchable | 否，`non-prefetchable` |
| 使用哪个中断脚 | `pin A` |
| 当前路由到哪个 IRQ | `IRQ 11` |
| 是否有 MSI capability | 有，`Capabilities: [40] MSI` |
| MSI 当前是否启用 | 未启用，`Enable-` |

结论：

- QEMU EDU 的 BAR0 在 Guest 中表现为一段 1 MiB 的 MMIO 窗口。
- Day 2 只确认 BAR 资源存在，不访问 BAR 内容；MMIO 读写放到后续 Day 4/Week 2。

## 4. 用 sysfs 交叉验证同一设备

### 4.1 读取 sysfs 属性

执行：

```bash
./scripts/ssh-guest.sh '
cd /sys/bus/pci/devices/0000:00:02.0
pwd
printf "vendor=" && cat vendor
printf "device=" && cat device
printf "class=" && cat class
printf "revision=" && cat revision
echo resource_first_line=$(head -1 resource)
if [ -e driver ]; then readlink driver; else echo driver_link_missing; fi
'
```

本次实际输出：

```text
/sys/bus/pci/devices/0000:00:02.0
vendor=0x1234
device=0x11e8
class=0x00ff00
revision=0x10
resource_first_line=0x00000000fea00000 0x00000000feafffff 0x0000000000040200
driver_link_missing
```

验收答案：

| sysfs 项 | 实际值 | 结论 |
|---|---|---|
| `vendor` | `0x1234` | 与 `lspci` 的 vendor 一致 |
| `device` | `0x11e8` | 与 `lspci` 的 device 一致 |
| `class` | `0x00ff00` | class code 是 `0x00ff`，低 8 位是 programming interface |
| `revision` | `0x10` | 与 `lspci` 的 `rev 10` 一致 |
| `resource` 第一行 start | `0xfea00000` | BAR0 起始地址 |
| `resource` 第一行 end | `0xfeafffff` | BAR0 结束地址 |
| `driver` symlink | 不存在 | 设备已枚举，但尚未绑定 driver |

### 4.2 计算 BAR0 长度

执行：

```bash
./scripts/ssh-guest.sh 'bash -lc '\''
read start end flags _ < /sys/bus/pci/devices/0000:00:02.0/resource
printf "start=0x%x\n" "$((start))"
printf "end=0x%x\n" "$((end))"
printf "flags=0x%x\n" "$((flags))"
printf "length=0x%x (%d bytes)\n" "$((end - start + 1))" "$((end - start + 1))"
'\'''
```

本次实际输出：

```text
start=0xfea00000
end=0xfeafffff
flags=0x40200
length=0x100000 (1048576 bytes)
```

验收答案：

```text
length = end - start + 1
       = 0xfeafffff - 0xfea00000 + 1
       = 0x100000
       = 1048576 bytes
       = 1 MiB
```

结论：

- sysfs 的 `resource` 区间是闭区间，所以必须 `end - start + 1`。
- 计算结果与 `lspci` 的 `[size=1M]` 一致。
- `driver_link_missing` 是本日预期结果，不是失败。

## 5. 阅读 QEMU EDU 官方规格并填答案

阅读：

- <https://www.qemu.org/docs/master/specs/edu.html>

本次记录的规格答案：

| 问题 | 答案 |
|---|---|
| 创建设备的 QEMU 参数 | `-device edu[,dma_mask=mask]` |
| 默认 DMA mask | 28 bits，即 256 MiB |
| PCI ID | `1234:11e8` |
| PCI Region 0 类型 | I/O memory，也就是 MMIO |
| PCI Region 0 大小 | 1 MB |
| Guest 如何和设备通信 | 通过 Region 0 这段 MMIO |
| 偏移 `0x00` 寄存器用途 | 识别寄存器，EDU 文档定义为 `0x010000edu` |

结论：

- QEMU 规格定义“设备需要 1 MiB BAR0”。
- Guest Linux PCI core 负责给 BAR0 分配本次实际地址 `0xfea00000`。
- 所以后续 KMD 绝对不能硬编码 `0xfea00000`，必须从 `struct pci_dev` 的 resource 获取。

## 6. 定位 QEMU EDU 源码证据

### 6.1 确认源码状态

执行：

```bash
cd ~/Miniaccel/miniaccel-qemu
git status --short
git rev-parse --short HEAD
```

本次实际输出：

```text
?? compile_commands.json
2f28d34ea0
```

说明：

- `compile_commands.json` 是已有未跟踪文件，本次未修改 QEMU 源码。
- 本次源码阅读基于提交 `2f28d34ea0`。

### 6.2 定位 QOM type、class init 和 instance init

执行：

```bash
rg -n 'edu_types|DEFINE_TYPES|edu_class_init|edu_instance_init' hw/misc/edu.c
```

本次实际输出：

```text
409:static void edu_instance_init(Object *obj)
418:static void edu_class_init(ObjectClass *class, const void *data)
432:static const TypeInfo edu_types[] = {
437:        .instance_init = edu_instance_init,
438:        .class_init    = edu_class_init,
446:DEFINE_TYPES(edu_types)
```

验收答案：

- `-device edu` 对应的 QEMU 设备类型来自 `edu_types[]`。
- `DEFINE_TYPES(edu_types)` 把该类型注册进 QEMU QOM 类型系统。
- `edu_class_init()` 设置 PCI ID、revision、class 和 realize 回调。
- `edu_instance_init()` 设置实例默认属性，例如 DMA mask。

### 6.3 定位 PCI ID 和 BAR0 源码

执行：

```bash
rg -n 'pci_edu_realize|pci_register_bar|memory_region_init_io|vendor_id|device_id|revision|class_id' hw/misc/edu.c
rg -n 'PCI_VENDOR_ID_QEMU' include/hw/pci hw/misc/edu.c
```

本次实际输出：

```text
369:static void pci_edu_realize(PCIDevice *pdev, Error **errp)
387:    memory_region_init_io(&edu->mmio, OBJECT(edu), &edu_mmio_ops, edu,
389:    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &edu->mmio);
423:    k->realize = pci_edu_realize;
425:    k->vendor_id = PCI_VENDOR_ID_QEMU;
426:    k->device_id = 0x11e8;
427:    k->revision = 0x10;
428:    k->class_id = PCI_CLASS_OTHERS;
hw/misc/edu.c:425:    k->vendor_id = PCI_VENDOR_ID_QEMU;
include/hw/pci/pci.h:60:#define PCI_VENDOR_ID_QEMU               0x1234
```

继续查看关键代码：

```bash
sed -n '369,446p' hw/misc/edu.c
```

本次读到的关键代码：

```text
static void pci_edu_realize(PCIDevice *pdev, Error **errp)
{
    EduState *edu = EDU(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_config_set_interrupt_pin(pci_conf, 1);
    ...
    memory_region_init_io(&edu->mmio, OBJECT(edu), &edu_mmio_ops, edu,
                    "edu-mmio", 1 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &edu->mmio);
}

static void edu_instance_init(Object *obj)
{
    EduState *edu = EDU(obj);

    edu->dma_mask = (1UL << 28) - 1;
    object_property_add_uint64_ptr(obj, "dma_mask",
                                   &edu->dma_mask, OBJ_PROP_FLAG_READWRITE);
}

static void edu_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_edu_realize;
    k->exit = pci_edu_uninit;
    k->vendor_id = PCI_VENDOR_ID_QEMU;
    k->device_id = 0x11e8;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_OTHERS;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}
```

验收答案：

| 问题 | 答案 |
|---|---|
| Vendor ID 在哪里设置 | `edu_class_init()` 中的 `k->vendor_id = PCI_VENDOR_ID_QEMU` |
| Vendor ID 的值在哪里定义 | `include/hw/pci/pci.h` 中 `PCI_VENDOR_ID_QEMU = 0x1234` |
| Device ID 在哪里设置 | `edu_class_init()` 中的 `k->device_id = 0x11e8` |
| Revision 在哪里设置 | `edu_class_init()` 中的 `k->revision = 0x10` |
| Class 在哪里设置 | `edu_class_init()` 中的 `k->class_id = PCI_CLASS_OTHERS` |
| BAR0 MemoryRegion 在哪里创建 | `pci_edu_realize()` 中的 `memory_region_init_io(..., "edu-mmio", 1 * MiB)` |
| BAR0 在哪里注册为 PCI BAR | `pci_edu_realize()` 中的 `pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &edu->mmio)` |
| 默认 DMA mask 在哪里设置 | `edu_instance_init()` 中的 `edu->dma_mask = (1UL << 28) - 1` |

结论：

- Guest 看到的 `1234:11e8` 不是 Linux 猜出来的，而是 QEMU EDU 写入虚拟 PCI
  config space 后，被 Guest PCI core 枚举出来的。
- Guest 看到的 `Region 0: Memory ... [size=1M]` 来自 QEMU 的
  `memory_region_init_io(..., 1 * MiB)` 和 `pci_register_bar(..., BAR0, MEMORY, ...)`。
- Guest 看到的具体地址 `0xfea00000` 不在 QEMU EDU 源码里写死，它是 Guest PCI
  枚举阶段分配出来的。

源码阅读心得单独记录在：

```text
docs/source-reading-notes/qemu-edu-pci.md
```

## 7. Linux PCI driver 绑定模型答案

阅读：

- <https://docs.kernel.org/PCI/pci.html>
- <https://docs.kernel.org/driver-api/driver-model/binding.html>
- <https://docs.kernel.org/driver-api/driver-model/bus.html>

本日只需要形成后续 KMD 的最小答案：

| 问题 | 答案 |
|---|---|
| `struct pci_driver` 中负责匹配的字段 | `id_table` |
| 匹配成功后调用的回调 | `probe` |
| 驱动注销或设备移除时调用的回调 | `remove` |
| `probe` 返回 `0` 的含义 | 驱动接受并成功绑定该设备 |
| `probe` 返回负数的含义 | 驱动拒绝或初始化失败，不绑定该设备 |
| `pci_device_id` 数组如何结束 | 用全 0 哨兵项结束，例如 `{ }` |
| `MODULE_DEVICE_TABLE(pci, ids)` 的用途 | 导出设备 ID 表，供模块自动加载和用户态工具识别匹配关系 |

后续 Day 3/4 的最小 KMD 结构会是：

```c
static const struct pci_device_id miniaccel_ids[] = {
    { PCI_DEVICE(0x1234, 0x11e8) },
    { }
};
MODULE_DEVICE_TABLE(pci, miniaccel_ids);

static struct pci_driver miniaccel_driver = {
    .name = "miniaccel_edu",
    .id_table = miniaccel_ids,
    .probe = miniaccel_probe,
    .remove = miniaccel_remove,
};
```

关键理解：

```text
设备枚举 != 驱动绑定
```

本次实验中：

1. QEMU 启动时创建 EDU 虚拟 PCI 设备。
2. Guest Linux 启动时枚举 PCI bus，创建 `struct pci_dev`。
3. 因为还没有加载匹配的 KMD，所以 `/sys/bus/pci/devices/0000:00:02.0/driver` 不存在。
4. 后续 `insmod miniaccel_edu.ko` 后，KMD 调用 `pci_register_driver()`。
5. PCI core 用 `id_table` 匹配已有 `struct pci_dev`。
6. 匹配 `1234:11e8` 后才调用 `miniaccel_probe()`。

## 8. 本日最终调用链

```text
Host: ./scripts/run-qemu.sh
  -> qemu-system-x86_64 ... -device edu
  -> QEMU QOM 找到 type name "edu"
  -> edu_class_init()
       vendor_id = 0x1234
       device_id = 0x11e8
       revision  = 0x10
       class_id  = PCI_CLASS_OTHERS
  -> edu_instance_init()
       dma_mask = (1UL << 28) - 1
  -> pci_edu_realize()
       memory_region_init_io("edu-mmio", 1 MiB)
       pci_register_bar(BAR0, MEMORY, &edu->mmio)

Guest: Linux PCI core
  -> 扫描 00:02.0
  -> 读取虚拟 PCI config space
  -> 创建 struct pci_dev
  -> 分配 BAR0 地址 0xfea00000..0xfeafffff
  -> 创建 /sys/bus/pci/devices/0000:00:02.0
  -> lspci/sysfs 能看到 1234:11e8

当前状态:
  -> driver symlink 不存在
  -> 尚未调用任何 miniaccel probe()

后续 Day 3/4:
  -> insmod miniaccel_edu.ko
  -> pci_register_driver()
  -> id_table 匹配 1234:11e8
  -> miniaccel_probe()
```

## 9. 踩坑与修正

| 问题 | 本次现象 | 原因 | 修正 |
|---|---|---|---|
| 把枚举和绑定混为一谈 | `lspci` 能看到 EDU，但 sysfs 没有 `driver` | PCI core 已枚举设备，但没有 KMD 绑定 | 把“看到设备”和“probe 被调用”分成两天验证 |
| 把 I/O memory 看成 I/O port | QEMU 文档写 I/O memory，容易误解成 x86 port I/O | 这里指 PCI BAR memory space，也就是 MMIO | 以 `lspci` 的 `Region 0: Memory` 为准 |
| BAR 长度少算 1 | `end - start` 会得到 `0xfffff` | sysfs resource 是闭区间 | 使用 `end - start + 1`，得到 `0x100000` |
| 使用 `awk strtonum()` 解析十六进制 | Guest 中 `awk` 报 `function strtonum never defined` | Debian Guest 默认 awk 不一定是 gawk | 改用 `bash` 算术展开解析 `0x...` |
| 硬编码 BAR 地址 | 本次地址是 `0xfea00000`，但换机器可能变化 | BAR 起始地址由 Guest PCI core 分配 | 后续 KMD 必须通过 `pci_resource_start()` 获取 |
| 从 `edu.c` 第一行硬读 | 容易陷入 DMA/IRQ/线程细节 | Day 2 只关心 PCI 枚举和 BAR | 先抓 `edu_types`、`class_init`、`realize`、`pci_register_bar` |

## 10. 本日产物

- `docs/week-01-pci.md`：本文件，记录 Day 2 实际执行过程和验收答案。
- `docs/source-reading-notes/qemu-edu-pci.md`：QEMU EDU PCI 设备源码阅读笔记。
- 关键实测证据：
  - `lspci`: `00:02.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)`
  - BAR0: `Memory at fea00000 (32-bit, non-prefetchable) [size=1M]`
  - sysfs: `vendor=0x1234`，`device=0x11e8`，`revision=0x10`
  - resource: `0xfea00000 0xfeafffff`，长度 `0x100000`
  - driver: `driver_link_missing`

## 11. 下一步

下一份 SOP 进入 Week 01 Day 3：创建最小 out-of-tree kernel module。Day 3 的验收不再
停留在 `lspci`，而是必须实际看到：

1. `insmod miniaccel_edu.ko` 成功。
2. `dmesg` 打印 `probe()` 被调用。
3. `/sys/bus/pci/devices/0000:00:02.0/driver` 指向新驱动。
4. `rmmod` 后 `remove()` 被调用，driver symlink 消失。
