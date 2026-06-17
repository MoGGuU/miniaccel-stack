# 源码阅读笔记：QEMU EDU PCI 设备

本文记录对 QEMU EDU 设备 PCI 创建路径的阅读结论。重点不是复述 `edu.c`，
而是回答：EDU 是如何被 QEMU 创建成 PCI 设备的，Guest Linux 为什么能看到
`1234:11e8`，以及这条路径对 MiniAccel 后续虚拟设备实现有什么启发。

## 1. 阅读目标

本次只回答：

1. `-device edu` 如何对应到 QEMU 中的 EDU 类型？
2. EDU 的 Vendor ID、Device ID、Revision、Class 在哪里设置？
3. BAR0 的 MMIO 区域在哪里创建、大小是多少、如何注册到 PCI BAR？
4. Host QEMU、Guest Linux、后续 KMD probe 三者之间的边界是什么？

本次不展开：

- EDU 的 DMA 实现。
- EDU 的 factorial 计算逻辑。
- EDU 的 IRQ/MSI 细节。
- Linux KMD 的 `pci_enable_device()`、BAR 映射和 MMIO 访问。

这些内容后续会分别进入 Week 02、Week 04、Week 05。

## 2. 证据索引

| 问题 | 证据位置 | 结论 |
|---|---|---|
| EDU 类型名是什么 | `hw/misc/edu.c:edu_types[]` | QEMU 注册了名为 `edu` 的 PCI device type |
| `-device edu` 后进入哪里 | `hw/misc/edu.c:pci_edu_realize()` | QEMU 创建具体设备实例时进入 realize 回调 |
| Vendor ID 来源 | `include/hw/pci/pci.h:PCI_VENDOR_ID_QEMU` | Vendor ID 是 `0x1234` |
| Device ID 来源 | `hw/misc/edu.c:edu_class_init()` | Device ID 是 `0x11e8` |
| Revision 来源 | `hw/misc/edu.c:edu_class_init()` | Revision 是 `0x10` |
| Class 来源 | `hw/misc/edu.c:edu_class_init()` | Class 是 `PCI_CLASS_OTHERS`，Guest 显示 `[00ff]` |
| BAR0 创建 | `hw/misc/edu.c:pci_edu_realize()` | `memory_region_init_io(..., 1 * MiB)` 创建 1 MiB MMIO |
| BAR0 注册 | `hw/misc/edu.c:pci_edu_realize()` | `pci_register_bar(..., 0, PCI_BASE_ADDRESS_SPACE_MEMORY, ...)` 注册为 BAR0 |
| Guest 观察结果 | `lspci -nn -vv -s 00:02.0` | Linux 看到 `1234:11e8` 和 1 MiB Memory BAR |
| sysfs 观察结果 | `/sys/bus/pci/devices/0000:00:02.0/*` | `vendor/device/resource` 与 QEMU 定义一致 |

## 3. 核心路径图

```text
Host userspace
┌──────────────────────────────────────────────────────────────┐
│ scripts/run-qemu.sh                                           │
│   -> qemu-system-x86_64 ... -device edu                       │
└───────────────────────┬──────────────────────────────────────┘
                        │
                        v
Host QEMU process
┌──────────────────────────────────────────────────────────────┐
│ QOM type registry                                             │
│   -> 找到 type name: "edu"                                    │
│   -> 创建 EDU object                                          │
│   -> edu_instance_init()                                      │
│   -> pci_edu_realize()                                        │
│        -> 设置/继承 PCI config space                           │
│        -> memory_region_init_io("edu-mmio", 1 MiB)             │
│        -> pci_register_bar(BAR0, MEMORY, &edu->mmio)           │
└───────────────────────┬──────────────────────────────────────┘
                        │ 暴露虚拟 PCI config space + BAR window
                        v
Guest Linux kernel
┌──────────────────────────────────────────────────────────────┐
│ PCI core 扫描 bus/device/function                             │
│   -> 读取 config space                                        │
│   -> 看到 vendor/device: 1234:11e8                            │
│   -> 为设备创建 struct pci_dev                                │
│   -> 注册到 /sys/bus/pci/devices/0000:00:02.0                 │
│   -> lspci/sysfs 可以观察到设备                               │
└───────────────────────┬──────────────────────────────────────┘
                        │ 后续加载 KMD
                        v
Guest Linux KMD
┌──────────────────────────────────────────────────────────────┐
│ insmod miniaccel_edu.ko                                       │
│   -> pci_register_driver()                                    │
│   -> pci_device_id 匹配 1234:11e8                             │
│   -> probe()                                                  │
└──────────────────────────────────────────────────────────────┘
```

我读完这条路径后最大的收获是：QEMU 和 Guest Linux 不是共享一个对象。QEMU
里有 `PCIDevice`，Guest Linux 里有 `struct pci_dev`。它们通过虚拟 PCI 总线
协议连接，而不是同一个内存里的结构体。

## 4. 关键源码解读

### 4.1 `edu_types[]` 和 `DEFINE_TYPES`

位置：

```text
miniaccel-qemu/hw/misc/edu.c:edu_types[]
miniaccel-qemu/hw/misc/edu.c:DEFINE_TYPES(edu_types)
```

作用：

- 把 EDU 作为一个 QOM type 注册到 QEMU。
- 让命令行 `-device edu` 能找到对应设备类型。
- 指定 parent type 是 PCI device。

我读到的关键点：

- `-device edu` 不是直接调用某个 C 函数，而是通过 QOM type registry 找到
  名为 `edu` 的设备类型。
- 这对 MiniAccel 的启发是：后续要实现 `-device miniaccel`，第一步不是写
  driver，而是在 QEMU 中注册一个新的 QOM PCI device type。

### 4.2 `edu_class_init()`

位置：

```text
miniaccel-qemu/hw/misc/edu.c:edu_class_init()
```

作用：

- 设置 PCI class 层面的默认属性。
- 设置 `vendor_id`、`device_id`、`revision`、`class_id`。
- 设置 realize/unrealize 等回调。

关键结论：

```text
Vendor ID = PCI_VENDOR_ID_QEMU = 0x1234
Device ID = 0x11e8
Revision  = 0x10
Class     = PCI_CLASS_OTHERS
```

我读到的关键点：

- `edu_class_init()` 不是 Guest Linux KMD 的 `probe()`，它运行在 Host QEMU
  进程里，用来定义 QEMU 设备类型的属性。
- Guest 里的 `lspci` 看到 `1234:11e8`，根源是 QEMU 在这里把这些字段写进
  虚拟 PCI config space。
- MiniAccel 后续可以模仿这里设置自己的实验 PCI ID，例如 `1afe:acc1`。

### 4.3 `edu_instance_init()`

位置：

```text
miniaccel-qemu/hw/misc/edu.c:edu_instance_init()
```

作用：

- 初始化具体 EDU 实例的默认属性。
- EDU 中会设置默认 DMA mask 等实例级配置。

我读到的关键点：

- class init 更像“设备类型的共同定义”，instance init 更像“每个设备实例的默认状态”。
- 这能避免把后续 MiniAccel 的全局类型信息和单设备运行状态混在一起。

### 4.4 `pci_edu_realize()`

位置：

```text
miniaccel-qemu/hw/misc/edu.c:pci_edu_realize()
```

作用：

- 设备实例真正被具现化。
- 初始化 MMIO MemoryRegion。
- 把 MemoryRegion 注册为 BAR0。
- 初始化 IRQ、DMA、线程等运行资源。

和 PCI/BAR 相关的关键路径：

```text
memory_region_init_io(&edu->mmio, ..., "edu-mmio", 1 * MiB)
  -> 创建 1 MiB MMIO 区域

pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &edu->mmio)
  -> 把该 MMIO 区域挂到 PCI BAR0
```

我读到的关键点：

- QEMU 只定义 BAR0 的大小和类型，不定义 Guest 最终看到的 BAR 起始地址。
- Guest 中看到的 `0xfea00000` 这类地址是 PCI 枚举阶段分配的。
- MiniAccel 后续要做 BAR0 register map，也是在 QEMU `realize()` 中创建
  MemoryRegion，再用 `pci_register_bar()` 暴露出去。

## 5. Guest 观测和源码的对应关系

| Guest 观察 | QEMU 源码来源 | 我的理解 |
|---|---|---|
| `Device [1234:11e8]` | `vendor_id` / `device_id` | PCI config space 中的 ID 字段由 QEMU 提供 |
| `(rev 10)` | `revision = 0x10` | revision 也是 config space 字段 |
| `[00ff]` | `PCI_CLASS_OTHERS` | EDU 被归为 unclassified/other |
| `Region 0: Memory ... [size=1M]` | `memory_region_init_io(..., 1 * MiB)` | BAR0 大小由 QEMU MemoryRegion 决定 |
| `non-prefetchable` | BAR 注册为普通 memory space | 寄存器访问有副作用，不能被 CPU/桥随意预取 |
| `/sys/.../resource` 第一行 | Linux PCI resource | Linux 枚举后记录的 BAR 地址区间 |
| 没有 `driver` symlink | 暂未加载匹配 KMD | 枚举设备不等于绑定驱动 |

## 6. 通信/枚举链路图

这张图是后续写 MiniAccel QEMU 设备和 Linux KMD 时最重要的分层图：

```text
┌────────────────────────────────────────────────────────────────────┐
│ Host: shell                                                        │
│                                                                    │
│ ./scripts/run-qemu.sh                                              │
│   QEMU_SYSTEM_X86_64=/usr/bin/qemu-system-x86_64                   │
│   -device edu                                                      │
└───────────────────────────────┬────────────────────────────────────┘
                                │ process argv
                                v
┌────────────────────────────────────────────────────────────────────┐
│ Host: QEMU process                                                 │
│                                                                    │
│ QOM device registry                                                │
│   "edu"                                                            │
│     -> edu_class_init()                                            │
│          vendor_id = 0x1234                                        │
│          device_id = 0x11e8                                        │
│          class_id  = PCI_CLASS_OTHERS                              │
│     -> edu_instance_init()                                         │
│     -> pci_edu_realize()                                           │
│          memory_region_init_io("edu-mmio", 1 MiB)                  │
│          pci_register_bar(BAR0, MEMORY, edu-mmio)                  │
│                                                                    │
│ QEMU exposes virtual PCI config space and BAR0 to the guest.        │
└───────────────────────────────┬────────────────────────────────────┘
                                │ virtual PCI bus
                                v
┌────────────────────────────────────────────────────────────────────┐
│ Guest: Linux PCI core                                              │
│                                                                    │
│ scan bus 0                                                         │
│   -> read config space of 00:02.0                                  │
│   -> vendor/device = 1234:11e8                                     │
│   -> BAR0 requires 1 MiB MMIO                                      │
│   -> allocate BAR0 address window                                  │
│   -> create struct pci_dev                                         │
│   -> expose sysfs node                                             │
│        /sys/bus/pci/devices/0000:00:02.0                           │
│                                                                    │
│ At this point lspci works even without a driver.                    │
└───────────────────────────────┬────────────────────────────────────┘
                                │ later: insmod KMD
                                v
┌────────────────────────────────────────────────────────────────────┐
│ Guest: miniaccel_edu.ko                                            │
│                                                                    │
│ module_init                                                        │
│   -> pci_register_driver(&miniaccel_pci_driver)                    │
│   -> PCI core compares pci_device_id with existing pci_dev          │
│   -> match 1234:11e8                                                │
│   -> miniaccel_probe(pdev, id)                                     │
│                                                                    │
│ probe() is a driver binding event, not a device enumeration event.  │
└────────────────────────────────────────────────────────────────────┘
```

## 7. 和 MiniAccel 的关系

| QEMU EDU 机制 | MiniAccel 后续对应 | 启发 |
|---|---|---|
| `-device edu` | `-device miniaccel` | 先注册 QOM type，才能被命令行创建 |
| `edu_class_init()` 设置 ID | `miniaccel_class_init()` 设置实验 ID | Vendor/Device ID 是 KMD 匹配入口 |
| `memory_region_init_io()` | MiniAccel BAR0 register file | 用 MMIO ops 实现寄存器读写 |
| `pci_register_bar()` | MiniAccel BAR0 | BAR 暴露的是 MemoryRegion，不是普通 malloc 内存 |
| Guest `struct pci_dev` | KMD `probe()` 参数 | Guest driver 只看到 PCI 设备抽象，不直接碰 QEMU 对象 |
| EDU spec | MiniAccel register spec | 写设备前先写 register map |

## 8. 我的理解

- QEMU EDU 是学习 PCI 设备的好模板，因为它把 PCI config、BAR、MMIO、IRQ、DMA
  都放在一个相对小的文件里。
- `lspci` 能看到设备，只说明 Linux PCI core 已经完成枚举；它不说明我们已经
  写了驱动。
- BAR0 的具体地址不是 QEMU 源码写死的，而是 Guest PCI 枚举阶段分配的，所以
  KMD 里永远不能硬编码 `0xfea00000` 之类地址。
- QEMU 里的 `PCIDevice` 和 Guest Linux 的 `struct pci_dev` 是两边世界里的对象，
  中间靠虚拟 PCI 协议通信。这一点想清楚后，后面做 MiniAccel 就不会把 host
  侧设备状态和 guest 侧 driver 状态混在一起。
- 读 QEMU 源码不要从第 1 行线性读，应该先抓 type/class/realize/BAR 这几个锚点。

## 9. 后续问题

- EDU 的 MMIO read/write callback 如何根据 offset 分发寄存器？
- EDU 如何触发 MSI/MSI-X，Guest ISR 如何 ACK？
- EDU 的 DMA helper 如何访问 Guest memory？
- MiniAccel 的 BAR0 register map 应该如何裁剪得比 EDU 更像 NPU/GPU command
  processor？
