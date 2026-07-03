# MiniAccel BAR0 Register Spec

本文记录 QEMU MiniAccel 设备当前 BAR0 寄存器规格。该规格对应 QEMU
`-device miniaccel`，PCI ID 为 `1afe:acc1`。

## 1. 总览

- BAR：BAR0
- BAR size：4 KiB
- 访问宽度：32-bit
- 访问要求：4-byte aligned
- endian：native endian
- 当前命令：NOP
- 中断：MSI，1 vector

## 2. Register Map

| Offset | Name | Direction | Reset/Value | 说明 |
|---:|---|---|---|---|
| `0x000` | `MAGIC` | RO | `0x4d414343` | MiniAccel identity，ASCII 近似为 `MACC` |
| `0x004` | `VERSION` | RO | `0x00010000` | 当前寄存器接口版本 |
| `0x008` | `CAPS` | RO | `0x00000003` | BAR capability：bit0=Nop，bit1=IRQ |
| `0x010` | `STATUS` | RO | `0x00000000` | bit0=`BUSY` |
| `0x020` | `IRQ_STATUS` | RO | `0x00000000` | bit0=`DONE` |
| `0x024` | `IRQ_ACK` | WO | - | W1C，写 1 清对应 `IRQ_STATUS` bit |
| `0x030` | `DOORBELL` | WO | - | 写入触发一次命令提交 |
| `0x034` | `SUBMIT_SEQNO` | RW | `0x00000000` | KMD 写入提交序号 |
| `0x038` | `COMPLETED_SEQNO` | RO | `0x00000000` | QEMU worker 完成后写入 |
| `0x040` | `CMD_OPCODE` | RW | `0x00000000` | `0 = NOP` |
| `0x044` | `CMD_FLAGS` | RW | `0x00000000` | bit0=`REQUEST_IRQ` |

## 3. Bit 定义

### 3.1 `CAPS`

| Bit | Name | 说明 |
|---:|---|---|
| 0 | `NOP` | 支持 NOP 命令 |
| 1 | `IRQ` | 支持完成中断 |

注意：BAR0 `CAPS=0x3` 是硬件 capability；`MINIACCEL_IOCTL_QUERY`
当前返回的是 UAPI capability，值为 `MINIACCEL_CAP_RUN_SYNC=0x1`。两者不是同一个命名空间。

### 3.2 `STATUS`

| Bit | Name | 说明 |
|---:|---|---|
| 0 | `BUSY` | QEMU worker 正在处理一条命令 |

### 3.3 `IRQ_STATUS`

| Bit | Name | 说明 |
|---:|---|---|
| 0 | `DONE` | 当前命令完成 |

### 3.4 `CMD_OPCODE`

| Value | Name | 说明 |
|---:|---|---|
| `0` | `NOP` | 空操作，worker sleep 1ms 后完成 |

### 3.5 `CMD_FLAGS`

| Bit | Name | 说明 |
|---:|---|---|
| 0 | `REQUEST_IRQ` | 完成后 raise MSI |

## 4. NOP 提交流程

```text
KMD:
  read STATUS
  if BUSY: return -EBUSY
  submit_seqno++
  write SUBMIT_SEQNO
  write CMD_OPCODE = NOP
  write CMD_FLAGS = REQUEST_IRQ
  write DOORBELL = 1
  wait_for_completion_timeout(cmd_done)

QEMU:
  DOORBELL write
  set STATUS.BUSY
  signal worker
  worker copies SUBMIT_SEQNO/CMD_OPCODE/CMD_FLAGS
  worker executes NOP
  write COMPLETED_SEQNO
  clear STATUS.BUSY
  set IRQ_STATUS.DONE
  msi_notify(vector 0)

KMD ISR:
  read IRQ_STATUS
  if 0: return IRQ_NONE
  write IRQ_ACK = IRQ_STATUS
  done_seqno = submit_seqno
  in_flight = false
  complete(cmd_done)
  wake_up_interruptible(event_wq)
```

## 5. 错误处理规则

| 场景 | 行为 |
|---|---|
| 非 32-bit 访问 | QEMU `LOG_GUEST_ERROR`，返回 `~0U` 或忽略写 |
| 非 4-byte aligned offset | QEMU `LOG_GUEST_ERROR` |
| 未知 offset | QEMU `LOG_GUEST_ERROR` |
| `DOORBELL` 时设备 busy | QEMU 记录 guest error，KMD 正常应先读 `STATUS.BUSY` 避免提交 |
| KMD wait timeout | KMD 清 `in_flight/expected_irq_status`，返回 `-ETIMEDOUT` |
| timeout 后迟到 IRQ | ISR ACK，但因为没有 matching `expected_irq_status`，不会完成旧命令 |

## 6. 当前限制

- `SUBMIT_SEQNO` 和 `COMPLETED_SEQNO` 在 BAR0 中是 32-bit；KMD 内部使用 `u64`，写寄存器时截断为 `u32`。
- 当前只支持单 outstanding command。
- 当前没有 doorbell queue，没有 DMA descriptor ring。
- 当前 `DOORBELL` 不解析写入值，只把写操作作为触发事件。
