# 第 3 周 Day 1-7 SOP：字符设备、UAPI 与 ioctl

本文记录 MiniAccel 第 3 周的完整实现过程：从 `/dev/miniaccel0` 字符设备、
UAPI 结构体、`QUERY` ioctl，到同步 `RUN_SYNC` polling、错误路径测试和并发
验证。

最终验收目标：

1. Guest 中加载驱动后出现 `/dev/miniaccel0`，卸载后节点消失。
2. `miniaccel-info` 能通过 `MINIACCEL_IOCTL_QUERY` 输出设备 ID、版本和能力位。
3. `miniaccel-run-sync` 能通过 `MINIACCEL_IOCTL_RUN_SYNC` 返回 EDU factorial 结果。
4. 非法 ioctl、NULL 用户指针、非法参数都有确定错误码。
5. 多线程、多进程同步提交稳定，打开 fd 时模块不能被卸载。

## 1. 当前上下文

### 1.0 实际执行记录

执行日期：2026-06-24。

宿主机/Guest/工具版本：

- Guest：QEMU EDU PCI 设备 `00:02.0 [1234:11e8]`。
- Guest kernel headers：`6.1.0-49-amd64`。
- 编译器：Guest 中使用 `gcc -Wall -Wextra -Werror` 编译用户态工具和测试。

本次是否完整跑通：Day 1-7 的主路径和自动化测试已跑通。

未执行或阻塞项：

- 没有做真实硬件长时间 busy 的超时注入；`RUN_SYNC` 中已使用
  `read_poll_timeout()` 做 bounded polling，超时会返回 `-ETIMEDOUT`，但本轮
  EDU factorial 正常完成太快，没有把真实 `-ETIMEDOUT` 当作已验证事实记录。

### 1.1 已完成内容

- `include/uapi/miniaccel_uapi.h`：定义 `QUERY` / `RUN_SYNC` ABI。
- `driver/char/miniaccel_drv.c`：注册字符设备，实现 `open/release/ioctl`。
- `tools/miniaccel-info.c`：查询设备信息。
- `tools/miniaccel-run-sync.c`：同步提交 factorial 命令。
- `tests/ioctl/*`：覆盖 query、run_sync、非法参数、并发和卸载路径。

### 1.2 本周不做

- 不实现 DMA。
- 不实现 interrupt completion。
- 不支持多 in-flight 命令；当前每个设备使用一个 `submit_lock` 串行化
  `RUN_SYNC`。

## 2. 目标调用链

```text
miniaccel-run-sync
  -> open /dev/miniaccel0
  -> ioctl(MINIACCEL_IOCTL_RUN_SYNC)
  -> copy_from_user(struct miniaccel_run_sync)
  -> mutex_lock(submit_lock)
  -> iowrite32(input, EDU_REG_FACTORIAL)
  -> read_poll_timeout(ioread32, EDU_REG_STATUS.COMPUTING 清零)
  -> ioread32(EDU_REG_FACTORIAL)
  -> mutex_unlock(submit_lock)
  -> copy_to_user(struct miniaccel_run_sync)
```

## 3. 实现步骤

### 3.1 Day 1：定义 UAPI

操作：

```c
struct miniaccel_query {
	__u32 device_id;
	__u32 version;
	__u64 capabilities;
	__u64 reserved[2];
};

struct miniaccel_run_sync {
	__u32 input;
	__u32 output;
	__u32 timeout_ms;
	__u32 reserved;
};
```

验收：

- 所有 UAPI 字段使用 `__u32` / `__u64`。
- 不使用 `long`、`size_t` 或裸用户指针。
- `struct miniaccel_query` 实测 32 bytes。
- `struct miniaccel_run_sync` 实测 16 bytes。

### 3.2 Day 2：注册字符设备

实现分工：

- `module_init()`：
  - `alloc_chrdev_region()`
  - `class_create()`
  - `pci_register_driver()`
- `probe()`：
  - `ida_alloc_range()`
  - `cdev_add()`
  - `device_create()`
- `remove()`：
  - `device_destroy()`
  - `cdev_del()`
  - `ida_free()`
- `module_exit()`：
  - `pci_unregister_driver()`
  - `class_destroy()`
  - `unregister_chrdev_region()`
  - `ida_destroy()`

实际验证：

```text
crw------- 1 root root 245, 0 ... /dev/miniaccel0
245:0
devnode-lifecycle-ok
```

### 3.3 Day 3：实现 QUERY

核心逻辑：

```text
open()
  -> file->private_data = mdev

ioctl(MINIACCEL_IOCTL_QUERY)
  -> struct miniaccel_query query = {}
  -> query.device_id = pdev->device
  -> query.version = EDU_REG_IDENT
  -> query.capabilities = MINIACCEL_CAP_RUN_SYNC
  -> copy_to_user()
```

实际输出：

```text
device_id=0x11e8 version=0x010000ed capabilities=0x0000000000000001
query-ioctl-ok
```

### 3.4 Day 4：实现 RUN_SYNC polling

`RUN_SYNC` 是 `_IOWR`，所以方向是双向：

```text
copy_from_user()
  -> 校验 reserved、timeout_ms、input
  -> mutex_lock()
  -> 写 EDU_REG_FACTORIAL
  -> bounded polling EDU_REG_STATUS
  -> 读 EDU_REG_FACTORIAL
  -> mutex_unlock()
  -> copy_to_user()
```

当前约束：

| 条件 | 返回值 |
|---|---|
| `reserved != 0` | `-EINVAL` |
| `timeout_ms == 0` | `-EINVAL` |
| `timeout_ms > 10000` | `-EINVAL` |
| `input > 12` | `-EINVAL` |
| 用户地址不可读/写 | `-EFAULT` |
| 轮询超时 | `-ETIMEDOUT` |

`input` 限制为 `0..12`，因为 `12! = 479001600` 仍可放入 `u32`，
`13!` 会溢出。

### 3.5 Day 5：用户态工具和错误测试

用户态工具：

```bash
sudo ./tools/miniaccel-info
sudo ./tools/miniaccel-run-sync 5
```

实际输出：

```text
device_id=0x11e8 version=0x010000ed capabilities=0x0000000000000001
input=5 output=120 timeout_ms=1000
```

负面测试覆盖：

```text
invalid-ioctl-enotty-ok
query-null-efault-ok
run-sync-null-efault-ok
run-sync-reserved-einval-ok
run-sync-timeout-zero-einval-ok
run-sync-timeout-too-large-einval-ok
run-sync-input-too-large-einval-ok
invalid-args-ok
```

### 3.6 Day 6：并发和卸载路径

并发策略：

- 每个 `miniaccel_dev` 有一个 `submit_lock`。
- 多线程/多进程可以同时 `open()` 和提交 ioctl。
- 真正访问 EDU MMIO 的 `RUN_SYNC` 会被 mutex 串行化。

实际输出：

```text
concurrent-run-sync-ok threads=8 loops=50 total=400
multi-process-run-sync-ok count=20
open-fd-rmmod-blocked-ok
concurrent-run-sync-sh-ok
```

验收结论：

- 8 个线程共 400 次 `RUN_SYNC` 全部返回正确 factorial 结果。
- 20 个进程同时运行 `miniaccel-run-sync` 成功。
- 持有 `/dev/miniaccel0` fd 时，`rmmod miniaccel_drv` 被阻止。
- 最近 dmesg 中没有 `Oops`、`BUG`、`WARNING`、`Call Trace` 或 `panic`。

### 3.7 Day 7：封版记录

本周形成两个层次的文档：

- `docs/week-03-char-ioctl.md`：完整实现 SOP 和实际验收记录。
- `docs/week-03-uapi.md`：UAPI 字段、ioctl 编号和错误码参考。

## 4. 自动化验证

执行命令：

```bash
make -C driver/char W=1
./tests/ioctl/test_query.sh
./tests/ioctl/test_run_sync.sh
./tests/ioctl/test_invalid_args.sh
./tests/ioctl/test_concurrent_run_sync.sh
```

实际输出：

```text
Waiting for SSH and cloud-init...
Guest is ready. EDU device: 00:02.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
make: Entering directory '/mnt/miniaccel/driver/char'
make -C /lib/modules/6.1.0-49-amd64/build M=/mnt/miniaccel/driver/char modules
make[1]: Entering directory '/usr/src/linux-headers-6.1.0-49-amd64'
  MODPOST /mnt/miniaccel/driver/char/Module.symvers
make[1]: Leaving directory '/usr/src/linux-headers-6.1.0-49-amd64'
make: Leaving directory '/mnt/miniaccel/driver/char'
device_id=0x11e8 version=0x010000ed capabilities=0x0000000000000001
query-ioctl-ok
input=0 output=1 timeout_ms=1000
input=5 output=120 timeout_ms=1000
input=12 output=479001600 timeout_ms=1000
run-sync-ioctl-ok
invalid-ioctl-enotty-ok
query-null-efault-ok
run-sync-null-efault-ok
run-sync-reserved-einval-ok
run-sync-timeout-zero-einval-ok
run-sync-timeout-too-large-einval-ok
run-sync-input-too-large-einval-ok
invalid-args-ok
concurrent-run-sync-ok threads=8 loops=50 total=400
multi-process-run-sync-ok count=20
open-fd-rmmod-blocked-ok
concurrent-run-sync-sh-ok
```

验收答案：

- `QUERY`：返回 `device_id=0x11e8`、`version=0x010000ed`。
- `RUN_SYNC`：`0!`、`5!`、`12!` 分别返回 `1`、`120`、
  `479001600`。
- 错误路径：`ENOTTY`、`EFAULT`、`EINVAL` 路径均已覆盖。
- 并发路径：多线程、多进程同步提交均通过。
- 卸载路径：打开 fd 时模块卸载被阻止。

## 5. 踩坑与修正

| 问题 | 现象 | 原因 | 修正 |
|---|---|---|---|
| `cdev_add()` 的 count 误写为 0 | 字符设备注册语义不正确 | `count` 表示这个 cdev 覆盖多少个 devt | 改为 `cdev_add(&mdev->cdev, mdev->devt, 1)` |
| `device_create()` 参数容易传错 | 设备节点创建失败或类型不匹配 | 第三个参数需要 `dev_t`，不是 `dev_t *` | 使用 `device_create(miniaccel_class, &pdev->dev, mdev->devt, mdev, ...)` |
| 多设备 minor 分配不能都用 0 | 多设备时 devt 冲突 | 次设备号必须唯一 | 使用全局 `IDA` 分配 minor，remove 时 `ida_free()` |
| `ida_destroy()` 生命周期容易误解 | 担心拔掉第一个设备会销毁 IDA | `module_exit()` 是整个模块退出，不是单设备 remove | 每设备 remove 只调用 `ida_free()`，模块退出才 `ida_destroy()` |
| 测试脚本提前 cleanup | 刚编译到 `/tmp` 的测试二进制被删 | cleanup 同时做卸载和删临时文件 | 拆成 `unload_module()` 和 `cleanup()` |

## 6. 本周产物

- `include/uapi/miniaccel_uapi.h`
- `driver/char/miniaccel_drv.c`
- `tools/miniaccel-info.c`
- `tools/miniaccel-run-sync.c`
- `tests/ioctl/test_query.sh`
- `tests/ioctl/test_run_sync.sh`
- `tests/ioctl/test_invalid_args.c`
- `tests/ioctl/test_invalid_args.sh`
- `tests/ioctl/test_concurrent_run_sync.c`
- `tests/ioctl/test_concurrent_run_sync.sh`
- `docs/week-03-uapi.md`
- `docs/week-03-char-ioctl.md`

## 7. 下一步

下一周可以进入 DMA 或 interrupt completion。建议先保持同步 ioctl 作为基线，
再把 `RUN_SYNC` 的内部执行路径替换成 DMA buffer 或中断完成，避免一次改动同时改变
UAPI、调度模型和设备执行模型。
