# 第 5 周 SOP：QEMU MiniAccel 虚拟 PCIe 设备

本文记录 MiniAccel 第 5 周把主线设备从 QEMU EDU 切换到自定义
`-device miniaccel` 的实现过程，包括 QEMU 设备、BAR0 寄存器、KMD probe、
doorbell worker、MSI completion interrupt、默认启动脚本和回归测试。

最终验收目标：

1. QEMU `-device help` 能看到 `miniaccel`。
2. Guest `lspci` 能看到 `1afe:acc1`。
3. KMD 能 probe MiniAccel，并读取 BAR0 `MAGIC/VERSION/CAPS`。
4. 用户态 `RUN_SYNC --opcode nop` 能触发 doorbell、QEMU worker 和 MSI。
5. KMD ISR 能 ACK 中断、`complete()` 唤醒 ioctl，并唤醒 `poll()`。
6. 默认 `scripts/run-qemu.sh` 使用 `-device miniaccel`，保留 EDU fallback。

## 1. 当前上下文

### 1.0 实际执行记录

执行日期：2026-07-03。

宿主机/Guest/工具版本：

- 宿主机工作目录：`/home/mogu/Miniaccel/miniaccel-stack`
- QEMU 工作目录：`/home/mogu/Miniaccel/miniaccel-qemu`
- Guest kernel headers：`6.1.0-49-amd64`
- QEMU binary：`../miniaccel-qemu/build/qemu-system-x86_64`

本次是否完整跑通：

- QEMU 编译通过。
- Guest 默认启动为 MiniAccel `1afe:acc1`。
- KMD 编译通过，并产出 `miniaccel_drv.ko` 与 `miniaccel_edu_drv.ko` 两个模块。
- probe、QUERY、RUN_SYNC、IRQ wait、poll、timeout cleanup、非法参数、多进程提交均已验证。
- EDU fallback 已用 `MINIACCEL_QEMU_DEVICE=edu` 重启验证。

未执行或阻塞项：

- 未创建 git tag，未 push。当前按本轮要求只完成实现和文档。
- Guest 内 `make -C driver/char W=1` 第一次出现过共享目录 clock skew 提示，不是编译错误；重启后重新编译通过。

### 1.1 已完成内容

- Week 04 已有 MSI、`completion`、`poll()` 和 timeout 清理模型。
- `miniaccel-qemu/hw/misc/miniaccel.c` 新增自定义 PCI 设备。
- `miniaccel-stack` 主线 KMD `miniaccel_drv.ko` 只支持 MiniAccel `1afe:acc1`。
- EDU fallback 已迁移到独立模块 `miniaccel_edu_drv.ko`，只支持 `1234:11e8`。
- 用户态 `tools/miniaccel-run-sync.c` 支持 `--opcode nop`。
- 测试脚本已根据 PCI ID 自动选择 `miniaccel_drv` 或 `miniaccel_edu_drv`。

### 1.2 本周不做

- 不实现 DMA。
- 不实现真实计算 opcode；当前 MiniAccel 只实现 NOP。
- 不删除 EDU 兼容路径，但 EDU 不再和 MiniAccel 主线驱动混在同一个 `.c` 文件里。

## 2. 目标调用链

```text
scripts/run-qemu.sh
  -> ../miniaccel-qemu/build/qemu-system-x86_64 ... -device miniaccel
  -> QEMU QOM type "miniaccel"
  -> pci_miniaccel_realize()
  -> 4 KiB BAR0 + MSI capability
  -> Guest PCI core 枚举 1afe:acc1
  -> miniaccel_drv probe()
  -> read MAGIC/VERSION/CAPS
  -> /dev/miniaccel0
  -> miniaccel-run-sync --opcode nop
  -> ioctl(MINIACCEL_IOCTL_RUN_SYNC)
  -> write SUBMIT_SEQNO/CMD_OPCODE/CMD_FLAGS/DOORBELL
  -> QEMU worker 执行 NOP
  -> QEMU msi_notify()
  -> miniaccel_irq_handler()
  -> read IRQ_STATUS / write IRQ_ACK
  -> complete(&cmd_done)
  -> wake_up_interruptible(&event_wq)
  -> ioctl 返回 completed seqno
```

## 3. 实现步骤

### 3.1 从 EDU 提取 MiniAccel 设备模板

保留的 EDU 结构：

- QOM type 注册。
- `PCIDevice` 派生状态结构。
- `realize` / `exit` 生命周期。
- `memory_region_init_io()` + `pci_register_bar()`。
- MMIO read/write 分发。
- MSI capability。
- worker thread + mutex/cond。

删除或不继承的 EDU 内容：

- factorial 寄存器和计算逻辑。
- EDU DMA 逻辑。
- EDU vendor/device ID。
- EDU 1 MiB BAR0；MiniAccel 改为 4 KiB BAR0。

产物：

- [source-reading-notes/qemu-edu-to-miniaccel.md](source-reading-notes/qemu-edu-to-miniaccel.md)
- [register-spec.md](register-spec.md)

### 3.2 新增 QEMU MiniAccel PCI 设备

关键文件：

- `miniaccel-qemu/hw/misc/miniaccel.c`
- `miniaccel-qemu/hw/misc/meson.build`
- `miniaccel-qemu/hw/misc/Kconfig`

关键行为：

- QOM type：`miniaccel`
- Vendor ID：`0x1afe`
- Device ID：`0xacc1`
- Revision：`0x01`
- PCI class：`PCI_CLASS_OTHERS`
- BAR0：4 KiB MMIO
- MSI：1 vector

验证命令：

```bash
cd /home/mogu/Miniaccel/miniaccel-qemu
ninja -C build qemu-system-x86_64
build/qemu-system-x86_64 -device help | rg -i 'miniaccel|edu'
build/qemu-system-x86_64 -accel help
```

实际输出：

```text
[3/3] Linking target qemu-system-x86_64
name "edu", bus PCI
name "miniaccel", bus PCI
Accelerators supported in QEMU binary:
tcg
mshv
nitro
kvm
```

验收：

- QEMU 编译通过。
- `-device help` 能看到 `miniaccel`。
- `-accel help` 能看到 `tcg`，默认脚本可以继续用 `-machine q35,accel=tcg`。

### 3.3 实现 BAR0 基础寄存器和 KMD probe

QEMU 侧实现：

- `MAGIC = 0x4d414343`
- `VERSION = 0x00010000`
- `CAPS = BIT(0) | BIT(1)`，分别表示 NOP 和 IRQ capability。
- 所有 MMIO 访问要求 4-byte aligned。
- 未知 offset 用 `qemu_log_mask(LOG_GUEST_ERROR, ...)` 记录。

KMD 侧实现：

- `miniaccel_drv.c` 的 `pci_device_id` 只匹配 `1afe:acc1`。
- `miniaccel_edu_drv.c` 的 `pci_device_id` 只匹配 EDU `1234:11e8`。
- MiniAccel probe 读取 `MAGIC/VERSION/CAPS/STATUS`。
- `QUERY` 对外仍返回 UAPI capability `MINIACCEL_CAP_RUN_SYNC`。

验证命令：

```bash
cd /home/mogu/Miniaccel/miniaccel-stack
./scripts/run-qemu.sh
./scripts/verify-guest.sh
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/probe/test_pci_resources.sh'
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/ioctl/test_query.sh'
```

实际输出：

```text
Guest is ready. MiniAccel/EDU device: 00:02.0 Unclassified device [00ff]: Device [1afe:acc1] (rev 01)
pci-resources-ok start=0xfebd1000 end=0xfebd1fff len=0x1000 flags=0x40200
device_id=0xacc1 version=0x00010000 capabilities=0x0000000000000001
query-ioctl-ok
```

验收：

- Guest 枚举到 `1afe:acc1`。
- BAR0 是 4 KiB。
- KMD probe 成功。
- `miniaccel-info` 能看到新 device id 和 version。

### 3.4 实现 doorbell 和 worker

QEMU state 增加：

- `QemuThread worker`
- `QemuMutex lock`
- `QemuCond cond`
- `pending`
- `stopping`
- `status`
- `irq_status`
- `submit_seqno`
- `completed_seqno`
- `cmd_opcode`
- `cmd_flags`

doorbell 路径：

```text
KMD write SUBMIT_SEQNO
  -> KMD write CMD_OPCODE = NOP
  -> KMD write CMD_FLAGS = IRQ
  -> KMD write DOORBELL
  -> QEMU 设置 pending 和 STATUS_BUSY
  -> QEMU worker 被 cond signal 唤醒
  -> worker sleep 1ms 模拟执行
  -> worker 写 COMPLETED_SEQNO
  -> worker 清 STATUS_BUSY
```

用户态验证：

```bash
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/ioctl/test_run_sync.sh'
```

实际输出：

```text
device_id=0xacc1 version=0x00010000 capabilities=0x0000000000000001
opcode=nop seqno=1 timeout_ms=1000
opcode=nop seqno=2 timeout_ms=1000
opcode=nop seqno=3 timeout_ms=1000
run-sync-nop-ok repeat=3
run-sync-ioctl-ok
```

验收：

- `RUN_SYNC --opcode nop --repeat 3` 返回 seqno 1/2/3。
- seqno 来自 QEMU `COMPLETED_SEQNO`，不是用户态自己生成。

### 3.5 接入 completion interrupt

QEMU 侧：

- `msi_init(pdev, 0, 1, true, false, errp)` 初始化 1 个 MSI vector。
- worker 完成后设置 `IRQ_STATUS.DONE`。
- MSI 开启时调用 `msi_notify(&s->pdev, 0)`。
- `IRQ_ACK` 用 write-one-to-clear 语义清除 `irq_status`。

KMD 侧：

- `pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX | PCI_IRQ_MSI)` 申请 vector。
- `pci_irq_vector(pdev, 0)` 转成 Linux IRQ 号。
- `request_irq(mdev->irq, miniaccel_irq_handler, 0, DRV_NAME, mdev)` 注册 ISR。
- ISR 读取 `MINIACCEL_REG_IRQ_STATUS`。
- ISR 写 `MINIACCEL_REG_IRQ_ACK`。
- ISR 在 `irq_lock` 内更新 `irq_count/done_seqno/in_flight`。
- ISR 在锁外调用 `complete()` 和 `wake_up_interruptible()`。

验证命令：

```bash
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/ioctl/test_irq_wait.sh'
```

实际输出：

```text
normal-irq-ok before=0 after=1
poll-wakeup-ok
poll-no-stale-event-ok
forced-timeout-ok
timeout-cleanup-ok
invalid-args-ok
invalid-ioctl-ok
concurrent-run-sync-c-skip-ok device=miniaccel
multi-process-run-sync-ok count=20
open-fd-rmmod-blocked-ok
dmesg-clean-ok
unload-ok
irq-wait-sh-ok
```

验收：

- IRQ 计数从 0 增加到 1。
- `poll()` 能被完成事件唤醒。
- 强制 timeout 后，下一次 `RUN_SYNC` 仍能成功，说明 `in_flight` 被清理。
- 多进程提交没有破坏 `submit_lock` 串行语义。

### 3.6 默认切换到 MiniAccel

修改：

- `scripts/run-qemu.sh` 默认优先使用 `../miniaccel-qemu/build/qemu-system-x86_64`。
- `scripts/run-qemu.sh` 默认 `MINIACCEL_QEMU_DEVICE=miniaccel`。
- `scripts/check-host-deps.sh` 优先检查自定义 QEMU 是否支持 `miniaccel`。
- `scripts/verify-guest.sh` 接受 `1afe:acc1` 或 EDU fallback。
- `scripts/detect-driver-module.sh` 根据 `lspci` 自动选择 KMD：
  `1afe:acc1 -> miniaccel_drv`，`1234:11e8 -> miniaccel_edu_drv`。
- ioctl/probe 测试根据当前 PCI ID 自动加载对应 `.ko`。

验证命令：

```bash
./scripts/check-host-deps.sh
./scripts/run-qemu.sh
./scripts/verify-guest.sh
```

实际输出：

```text
Host dependencies and QEMU miniaccel device are available.
QEMU started with PID 694845; SSH port is 2222.
Guest is ready. MiniAccel/EDU device: 00:02.0 Unclassified device [00ff]: Device [1afe:acc1] (rev 01)
```

验收：

- 默认启动路径已经切到 MiniAccel。
- EDU 没有删除，必要时可以用 `MINIACCEL_QEMU_DEVICE=edu` 回退；回退时加载
  `miniaccel_edu_drv.ko`，不会再走主线 `miniaccel_drv.ko`。

## 4. 自动化验证

Guest 内重新编译 KMD：

```bash
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && make -C driver/char W=1'
```

实际输出：

```text
make: Entering directory '/mnt/miniaccel/driver/char'
make -C /lib/modules/6.1.0-49-amd64/build M=/mnt/miniaccel/driver/char modules
make[1]: Entering directory '/usr/src/linux-headers-6.1.0-49-amd64'
  CC [M]  /mnt/miniaccel/driver/char/miniaccel_drv.o
  CC [M]  /mnt/miniaccel/driver/char/miniaccel_edu_drv.o
  LD [M]  /mnt/miniaccel/driver/char/miniaccel_drv.ko
  LD [M]  /mnt/miniaccel/driver/char/miniaccel_edu_drv.ko
make[1]: Leaving directory '/usr/src/linux-headers-6.1.0-49-amd64'
make: Leaving directory '/mnt/miniaccel/driver/char'
```

完整测试命令：

```bash
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/probe/test_pci_resources.sh'
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/ioctl/test_query.sh'
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/ioctl/test_run_sync.sh'
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/ioctl/test_irq_wait.sh'
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/ioctl/test_concurrent_run_sync.sh && ./tests/ioctl/test_invalid_args.sh'
```

实际输出摘要：

```text
pci-resources-ok start=0xfebd1000 end=0xfebd1fff len=0x1000 flags=0x40200
query-ioctl-ok
run-sync-ioctl-ok
irq-wait-sh-ok
concurrent-run-sync-sh-ok
invalid-args-ok
```

验收答案：

- `lspci`：看到 `1afe:acc1`。
- BAR0：`febd1000-febd1fff`，长度 `0x1000`。
- MSI：`lspci -vv` 显示 `MSI: Enable+ Count=1/1`。
- `QUERY`：`device_id=0xacc1 version=0x00010000`。
- `RUN_SYNC`：NOP seqno 递增。
- IRQ/poll：`normal-irq-ok` 和 `poll-wakeup-ok`。
- timeout cleanup：`forced-timeout-ok` 后仍有 `timeout-cleanup-ok`。

EDU fallback 验证：

```bash
MINIACCEL_QEMU_DEVICE=edu ./scripts/run-qemu.sh
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./scripts/detect-driver-module.sh'
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/ioctl/test_run_sync.sh'
./scripts/ssh-guest.sh 'cd /mnt/miniaccel && ./tests/ioctl/test_irq_wait.sh'
```

实际输出摘要：

```text
Guest is ready. MiniAccel/EDU device: 00:02.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
miniaccel_edu_drv
device_id=0x11e8 version=0x010000ed capabilities=0x0000000000000001
input=0 output=1 timeout_ms=1000
input=5 output=120 timeout_ms=1000
input=12 output=479001600 timeout_ms=1000
run-sync-ioctl-ok
irq-wait-sh-ok
```

## 5. 取证输出

`lspci -vv -d 1afe:acc1`：

```text
00:02.0 Unclassified device [00ff]: Device 1afe:acc1 (rev 01)
        Interrupt: pin A routed to IRQ 36
        Region 0: Memory at febd1000 (32-bit, non-prefetchable) [size=4K]
        Capabilities: [40] MSI: Enable+ Count=1/1 Maskable- 64bit+
        Kernel driver in use: miniaccel_drv
```

`miniaccel-info`：

```text
device_id=0xacc1 version=0x00010000 capabilities=0x0000000000000001
```

`dmesg`：

```text
miniaccel_drv 0000:00:02.0: probe called for 1afe:acc1
miniaccel_drv 0000:00:02.0: BAR0: start=0x00000000febd1000 end=0x00000000febd1fff len=0x0000000000001000 flags=0x40200
miniaccel_drv 0000:00:02.0: MiniAccel identity: magic=0x4d414343 version=0x00010000 caps=0x00000003 status=0x00000000
miniaccel_drv 0000:00:02.0: allocated 1 IRQ vector(s), linux irq=36
miniaccel_drv 0000:00:02.0: probe ok: MiniAccel regs=... version=0x00010000 caps=0x00000003
```

## 6. 踩坑与修正

| 问题 | 现象 | 原因 | 修正 |
|---|---|---|---|
| 本地 QEMU 不支持 TCG | `scripts/run-qemu.sh` 报 `invalid accelerator tcg` | 原 build 只有 `mshv/nitro/kvm` | 重新 `./configure --target-list=x86_64-softmmu --enable-debug --enable-tcg` 并重编 |
| `berkeley-testfloat-3` 子项目不完整 | `configure` 报缺少 `meson.build` | 子项目目录只有 `.git`，没有 packagefiles 内容 | fetch 指定 revision 后复制 QEMU `subprojects/packagefiles/berkeley-testfloat-3/` |
| `miniaccel-info` 权限不足 | 普通用户打开 `/dev/miniaccel0` 报 permission denied | udev 权限未放开 | 文档取证命令用 `sudo /tmp/miniaccel-info-doc` |
| QEMU IRQ 状态读写细节 | `miniaccel_raise_irq()` 解锁后再次读 `s->irq_status` | 测试能过，但多线程语义不够干净 | 在锁内保存 `irq_status` 局部变量后再判断 |

## 7. 本周产物

stack 仓库：

- `driver/char/miniaccel_drv.c`
- `driver/char/miniaccel_edu_drv.c`
- `driver/char/Makefile`
- `tools/miniaccel-run-sync.c`
- `scripts/run-qemu.sh`
- `scripts/check-host-deps.sh`
- `scripts/verify-guest.sh`
- `scripts/detect-driver-module.sh`
- `scripts/check-driver-style.sh`
- `scripts/format-driver.sh`
- `tests/probe/test_pci_resources.sh`
- `tests/ioctl/test_query.sh`
- `tests/ioctl/test_run_sync.sh`
- `tests/ioctl/test_irq_wait.sh`
- `tests/ioctl/test_concurrent_run_sync.sh`
- `docs/week-05-qemu-miniaccel.md`
- `docs/register-spec.md`
- `docs/source-reading-notes/qemu-edu-to-miniaccel.md`

QEMU 仓库：

- `hw/misc/miniaccel.c`
- `hw/misc/meson.build`
- `hw/misc/Kconfig`

## 8. 下一步

下一周可以进入 MiniAccel DMA 或更真实的 command queue。进入前建议先把 Week 05
拆成两个仓库的提交，并在确认后再创建 tag。
