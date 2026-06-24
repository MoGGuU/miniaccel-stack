# 第 3 周 UAPI 参考：MiniAccel ioctl ABI

本文定义 MiniAccel 内核驱动与用户态程序之间的初始 ABI。公共头文件位于
`include/uapi/miniaccel_uapi.h`，内核驱动和用户态工具必须使用同一份定义。

当前状态：

- UAPI 结构体和 ioctl 编号已经定义。
- 字符设备 `/dev/miniaccel0` 已创建。
- `QUERY` ioctl 已实现，`tools/miniaccel-info.c` 已可输出设备信息。
- `RUN_SYNC` ioctl 已实现，`tools/miniaccel-run-sync.c` 已可同步提交 EDU
  factorial 命令。

## 1. ABI 约束

- 固定宽度字段统一使用 `__u32` 和 `__u64`。
- UAPI 不使用 `long`、`size_t` 或用户态指针，避免 32 位和 64 位 ABI 不一致。
- 所有输入方向的 `reserved` 字段必须为 0；驱动收到非 0 值时返回 `-EINVAL`。
- 驱动返回结构体前必须将整个结构体清零，避免 reserved 字段泄露内核数据。
- 已发布的字段顺序、宽度和 ioctl 编号不应随意修改。

实际验证的结构体尺寸：

| 结构体 | 大小 |
|---|---:|
| `struct miniaccel_query` | 32 bytes |
| `struct miniaccel_run_sync` | 16 bytes |

## 2. Capability

```c
#define MINIACCEL_CAP_RUN_SYNC (1ULL << 0)
```

| Bit | 名称 | 含义 |
|---:|---|---|
| 0 | `MINIACCEL_CAP_RUN_SYNC` | 支持同步提交和 polling 等待完成 |
| 1-63 | 保留 | 当前必须返回 0 |

当前 `RUN_SYNC` 已实现，`QUERY` 会宣告 `MINIACCEL_CAP_RUN_SYNC`。

## 3. QUERY

定义：

```c
struct miniaccel_query {
	__u32 device_id;
	__u32 version;
	__u64 capabilities;
	__u64 reserved[2];
};

#define MINIACCEL_IOCTL_QUERY \
	_IOR(MINIACCEL_IOCTL_BASE, 0x00, struct miniaccel_query)
```

方向为内核到用户态。用户态传入一个可写的
`struct miniaccel_query`，驱动返回设备信息：

| 字段 | 方向 | 含义 |
|---|---|---|
| `device_id` | 输出 | PCI Device ID，EDU 当前为 `0x11e8` |
| `version` | 输出 | BAR0 identification 寄存器值，当前为 `0x010000ed` |
| `capabilities` | 输出 | 驱动当前支持的能力位 |
| `reserved[2]` | 输出 | 必须返回 0 |

预期错误码：

| 错误码 | 条件 |
|---|---|
| `-EFAULT` | 用户态输出地址不可写 |
| `-ENOTTY` | ioctl 编号或 magic 不受支持 |

## 4. RUN_SYNC

定义：

```c
struct miniaccel_run_sync {
	__u32 input;
	__u32 output;
	__u32 timeout_ms;
	__u32 reserved;
};

#define MINIACCEL_IOCTL_RUN_SYNC \
	_IOWR(MINIACCEL_IOCTL_BASE, 0x01, struct miniaccel_run_sync)
```

方向为双向：

| 字段 | 方向 | 含义 |
|---|---|---|
| `input` | 输入 | EDU factorial 输入，初始实现限制为 `0..12` |
| `output` | 输出 | factorial 计算结果 |
| `timeout_ms` | 输入 | polling 最长等待时间，必须大于 0 |
| `reserved` | 输入 | 必须为 0 |

预期执行链：

```text
copy_from_user()
  -> 校验 input、timeout_ms 和 reserved
  -> 获取设备 submit mutex
  -> 写 EDU_REG_FACTORIAL
  -> polling EDU_REG_STATUS.COMPUTING
  -> 读取 EDU_REG_FACTORIAL
  -> 释放 mutex
  -> copy_to_user()
```

预期错误码：

| 错误码 | 条件 |
|---|---|
| `-EINVAL` | `reserved != 0`、`timeout_ms == 0` 或 input 越界 |
| `-EFAULT` | 用户地址无法读取或写入 |
| `-ETIMEDOUT` | 设备在超时前没有完成 |
| `-ENODEV` | 设备已经不可用 |
| `-ENOTTY` | ioctl 编号或 magic 不受支持 |

## 5. ioctl 编码

```c
#define MINIACCEL_IOCTL_BASE 'M'
```

当前在 x86-64 Guest 中实际得到：

| ioctl | 编码 |
|---|---:|
| `MINIACCEL_IOCTL_QUERY` | `0x80204d00` |
| `MINIACCEL_IOCTL_RUN_SYNC` | `0xc0104d01` |

用户态不应硬编码这些数值，必须 include
`miniaccel_uapi.h` 并使用对应宏。

## 6. 双端包含验证

用户态验证：

```bash
gcc -Wall -Wextra -Werror \
  -Iinclude/uapi \
  tools/miniaccel-info.c \
  -o tools/miniaccel-info
```

内核侧已在 Guest 的 Linux `6.1.0-49-amd64` headers 下验证该头文件能够和
`miniaccel_drv.c` 一起编译。

## 7. QUERY / RUN_SYNC 验收

执行环境：QEMU Guest，Linux headers `6.1.0-49-amd64`。

编译内核模块和用户态工具：

```bash
make -C driver/char W=1
gcc -Wall -Wextra -Werror -Iinclude/uapi \
  tools/miniaccel-info.c -o tools/miniaccel-info
gcc -Wall -Wextra -Werror -Iinclude/uapi \
  tools/miniaccel-run-sync.c -o tools/miniaccel-run-sync
```

重要输出：

```text
CC [M]  /mnt/miniaccel/driver/char/miniaccel_drv.o
LD [M]  /mnt/miniaccel/driver/char/miniaccel_drv.ko
Skipping BTF generation ... due to unavailability of vmlinux
```

加载模块并查询设备：

```bash
sudo insmod driver/char/miniaccel_drv.ko
sudo ./tools/miniaccel-info
```

实际输出：

```text
device_id=0x11e8 version=0x010000ed capabilities=0x0000000000000001
```

结论：

- `MINIACCEL_IOCTL_QUERY` 能返回 PCI Device ID `0x11e8`。
- `version` 来自 EDU identification register，当前为 `0x010000ed`。
- 当前 `RUN_SYNC` 已实现后，`capabilities` 返回
  `0x0000000000000001`。

同步提交：

```bash
sudo ./tools/miniaccel-run-sync 5
sudo ./tools/miniaccel-run-sync 12 1000
```

实际输出：

```text
input=5 output=120 timeout_ms=1000
input=12 output=479001600 timeout_ms=1000
```

负面路径验证：

```text
invalid-ioctl-enotty-ok
query-null-efault-ok
run-sync-null-efault-ok
run-sync-reserved-einval-ok
run-sync-timeout-zero-einval-ok
run-sync-timeout-too-large-einval-ok
run-sync-input-too-large-einval-ok
```

结论：

- 未知 ioctl 返回 `ENOTTY`。
- `MINIACCEL_IOCTL_QUERY` 传入 NULL 用户指针返回 `EFAULT`。
- `MINIACCEL_IOCTL_RUN_SYNC` 传入 NULL 用户指针返回 `EFAULT`。
- `RUN_SYNC` 的 reserved、timeout 和 input 非法值返回 `EINVAL`。
