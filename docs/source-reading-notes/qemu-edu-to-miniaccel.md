# 源码阅读笔记：从 QEMU EDU 到 MiniAccel

本文记录从 QEMU EDU 设备抽取 MiniAccel 虚拟 PCI 设备模板的阅读结论。重点是：
哪些 EDU 结构值得保留，哪些内容应该删掉，以及它们如何落到当前
`hw/misc/miniaccel.c`。

## 1. 阅读目标

本次只回答：

1. `-device edu` 这种 QEMU 设备如何变成 `-device miniaccel`？
2. 一个 PCI MMIO 设备最小需要哪些 QEMU 结构？
3. BAR0 read/write、worker 和 MSI 如何映射到 MiniAccel？
4. Guest KMD 为什么只需要看 PCI ID 和 BAR0 register map？

不展开：

- EDU DMA 细节。
- QEMU PCI core 的完整实现。
- Linux PCI core 的枚举细节。

## 2. 证据索引

| 问题 | 证据位置 | 结论 |
|---|---|---|
| QOM type 名称 | `miniaccel-qemu/hw/misc/miniaccel.c:TYPE_PCI_MINIACCEL_DEVICE` | type name 是 `miniaccel`，对应命令行 `-device miniaccel` |
| PCI ID | `miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_class_init()` | vendor/device 为 `1afe:acc1` |
| BAR0 | `miniaccel-qemu/hw/misc/miniaccel.c:pci_miniaccel_realize()` | `memory_region_init_io()` 创建 4 KiB MMIO，`pci_register_bar()` 注册 BAR0 |
| MSI | `miniaccel-qemu/hw/misc/miniaccel.c:pci_miniaccel_realize()` | `msi_init()` 初始化 1 个 vector |
| MMIO read/write | `miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_mmio_read/write()` | BAR0 register map 在这里实现 |
| Worker | `miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_worker_thread()` | doorbell signal 后处理 NOP 并完成 seqno |
| IRQ raise | `miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_raise_irq()` | 设置 `irq_status` 后调用 `msi_notify()` |
| KMD 匹配 | `miniaccel-stack/driver/char/miniaccel_drv.c:miniaccel_id_table` | 主线 driver 只匹配 `1afe:acc1` |
| EDU fallback 匹配 | `miniaccel-stack/driver/char/miniaccel_edu_drv.c:miniaccel_id_table` | EDU fallback driver 只匹配 `1234:11e8` |

## 3. 核心路径图

```text
Host command line
  qemu-system-x86_64 -device miniaccel
        |
        v
QEMU QOM
  type name "miniaccel"
        |
        v
pci_miniaccel_realize()
  msi_init()
  qemu_thread_create(worker)
  memory_region_init_io("miniaccel-mmio", 4 KiB)
  pci_register_bar(BAR0)
        |
        v
Guest PCI core
  sees vendor/device 1afe:acc1
        |
        v
miniaccel_drv
  pci_device_id match
  probe()
  pci_iomap(BAR0)
  read MAGIC/VERSION/CAPS
```

EDU fallback 现在走单独模块：

```text
Guest PCI core sees 1234:11e8
        |
        v
miniaccel_edu_drv
  pci_device_id match
  probe()
  pci_iomap(BAR0)
  read EDU IDENT/LIVENESS
```

## 4. 关键源码解读

### 4.1 QOM type 和 class init

位置：

```text
miniaccel-qemu/hw/misc/miniaccel.c:TYPE_PCI_MINIACCEL_DEVICE
miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_class_init()
miniaccel-qemu/hw/misc/miniaccel.c:DEFINE_TYPES(miniaccel_types)
```

作用：

- 把 MiniAccel 注册为 QEMU PCI device type。
- 让 `-device miniaccel` 能找到这个设备。
- 设置虚拟 PCI config space 里 Guest 会读到的 vendor/device/revision/class。

我读到的关键点：

- QEMU 里写的是 Host 进程内的设备模型。
- Guest Linux 看到的是这个模型暴露出来的 PCI config space，不是直接看到 QEMU C 结构体。
- 所以 KMD 不需要知道 QOM，只需要匹配 `1afe:acc1` 并访问 BAR0。

### 4.2 BAR0 MMIO

位置：

```text
miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_mmio_ops
miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_mmio_read()
miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_mmio_write()
miniaccel-qemu/hw/misc/miniaccel.c:pci_miniaccel_realize()
```

作用：

- `memory_region_init_io()` 把 read/write callback 绑定到一段虚拟 MMIO 空间。
- `pci_register_bar()` 把这段空间挂到 PCI BAR0。
- Guest KMD `ioread32/iowrite32` 最后会落到这些 callback。

我读到的关键点：

- BAR0 是 Host QEMU 模拟出来的窗口；Guest 看到的是 PCI resource。
- register map 必须稳定，否则 KMD 和 QEMU 会“各说各话”。
- 本周把寄存器规格独立写到 [../register-spec.md](../register-spec.md)，避免只靠代码记忆。

### 4.3 Doorbell 和 worker

位置：

```text
miniaccel-qemu/hw/misc/miniaccel.c:MINIACCEL_REG_DOORBELL
miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_worker_thread()
```

作用：

- KMD 写 `DOORBELL` 时，QEMU 设置 `pending=true` 和 `STATUS.BUSY`。
- worker thread 被 `qemu_cond_signal()` 唤醒。
- worker 拷贝 seqno/opcode/flags，执行 NOP，更新 completed seqno。

我读到的关键点：

- doorbell 是“提交发生了”的边沿事件，不是队列本身。
- 当前模型只允许一个 outstanding command，因此 KMD 的 `submit_lock` 和 QEMU 的 `BUSY` 是匹配的。
- 后续如果做 command queue，doorbell 后面应该变成 descriptor/ring 的消费入口。

### 4.4 MSI completion interrupt

位置：

```text
miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_raise_irq()
miniaccel-qemu/hw/misc/miniaccel.c:miniaccel_lower_irq()
miniaccel-stack/driver/char/miniaccel_drv.c:miniaccel_irq_handler()
```

作用：

- QEMU worker 完成命令后设置 `IRQ_STATUS.DONE`。
- QEMU 调用 `msi_notify()` 通知 Guest。
- KMD ISR 读取 `IRQ_STATUS`，写 `IRQ_ACK`，推进 `done_seqno`，唤醒 completion 和 poll wait queue。

我读到的关键点：

- MSI 本质是设备向 Guest 发送一次中断消息；KMD 仍然要读设备自己的 `IRQ_STATUS` 判断原因。
- `IRQ_ACK` 是设备寄存器协议的一部分，不是 Linux IRQ core 自动替你做的。
- `complete()` 不是下半部，它只是唤醒睡在 `wait_for_completion_timeout()` 的进程上下文。

## 5. 和 MiniAccel 的关系

| EDU/参考对象 | MiniAccel 当前对应 | 后续实现启发 |
|---|---|---|
| QOM PCI device type | `TYPE_PCI_MINIACCEL_DEVICE "miniaccel"` | 后续可继续加设备属性 |
| EDU BAR0 | MiniAccel 4 KiB BAR0 | 小而稳定的 register ABI 更适合教学 |
| EDU factorial | NOP command | 后续替换成真实 command processor |
| EDU IRQ status/ack | `IRQ_STATUS/IRQ_ACK` | 中断原因必须在设备寄存器里表达 |
| EDU worker | MiniAccel worker | 后续可扩展成 command queue worker |
| 原混合 KMD | `miniaccel_drv.c` + `miniaccel_edu_drv.c` | 不同硬件协议分文件，避免 probe/run_sync/ISR 混在一起 |

## 6. 通信/调用链图

```text
userspace:
  miniaccel-run-sync --opcode nop
    |
    v
KMD:
  miniaccel_ioctl_run_sync_miniaccel()
  write SUBMIT_SEQNO/CMD_OPCODE/CMD_FLAGS/DOORBELL
  wait_for_completion_timeout()
    |
    v
QEMU MMIO:
  miniaccel_mmio_write(DOORBELL)
  cond_signal(worker)
    |
    v
QEMU worker:
  complete NOP
  write completed_seqno
  set IRQ_STATUS.DONE
  msi_notify()
    |
    v
KMD ISR:
  read IRQ_STATUS
  write IRQ_ACK
  complete(cmd_done)
  wake_up_interruptible(event_wq)
    |
    v
userspace:
  ioctl returns seqno
```

## 7. 我的理解

- QEMU 设备模型和 Linux KMD 的边界是 PCI config space、BAR、IRQ，不是 C 结构体共享。
- `MAGIC/VERSION/CAPS` 是 KMD 进入 MiniAccel 协议前的握手。
- `DOORBELL` 是提交入口，`IRQ_STATUS/IRQ_ACK` 是完成出口。
- `submit_lock`、QEMU `BUSY`、`in_flight` 三者共同保证当前只有一个命令在路上。
- Week 05 的价值不是 NOP 本身，而是把“自己定义硬件协议并由 KMD 驱动它”这条闭环跑通。

## 8. 后续问题

- MiniAccel 是否需要把 `CAPS` 同步暴露到 UAPI `QUERY` 的扩展字段？
- `SUBMIT_SEQNO/COMPLETED_SEQNO` 是否应该升级为 64-bit register pair？
- 下一步做 DMA 时，doorbell 应该指向单命令寄存器还是 descriptor ring？
