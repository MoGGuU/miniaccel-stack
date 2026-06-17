# 第 1 周 Day 3-5 SOP：KMD probe/remove 与绑定测试

本文记录 MiniAccel 第 1 周 Day 3、Day 4、Day 5 的实际执行过程：创建
out-of-tree Linux kernel module，注册最小 PCI driver，匹配 QEMU EDU
设备 `1234:11e8`，并把手工加载/卸载验证固化成脚本。

最终验收结果：

1. Guest 内能编译生成 `driver/char/miniaccel_drv.ko`。
2. `insmod` 后 PCI core 调用 `miniaccel_probe()`，`lspci -nnk` 显示
   `Kernel driver in use: miniaccel_drv`。
3. `rmmod` 后调用 `miniaccel_remove()`，模块能卸载干净。
4. `tests/probe/test_bind_unbind.sh` 通过。
5. `tests/probe/test_reload_100.sh` 完成 100 次 reload，没有看到近期
   `Oops`、`BUG`、`WARNING`、`Call Trace` 或 `panic`。

## 1. 当前上下文

### 1.0 实际执行记录

执行日期：2026-06-17

宿主机与 Guest：

| 项目 | 实际值 |
|---|---|
| 宿主机工作目录 | `~/Miniaccel/miniaccel-stack` |
| 宿主机 kernel | `6.6.87.2-microsoft-standard-WSL2` |
| Guest kernel | `6.1.0-49-cloud-amd64` |
| Guest headers | `/lib/modules/6.1.0-49-cloud-amd64/build` 存在 |
| EDU BDF | `00:02.0` |
| EDU PCI ID | `1234:11e8` |

本次是否完整跑通：是。Day 3、Day 4、Day 5 都在 Guest 内实际执行过。

未执行或阻塞项：

- 宿主机是 WSL2 kernel，`/lib/modules/$(uname -r)/build` 不存在，所以不在宿主机直接编译 KMD。
- 本周仍不访问 BAR0，不读写 MMIO，不处理中断、DMA、字符设备或 ioctl。

### 1.1 已完成内容

- Day 1 已有 QEMU Guest 启动脚本。
- Day 2 已确认 Guest Linux 能枚举 EDU 设备 `00:02.0 [1234:11e8]`。
- `scripts/run-qemu.sh` 已包含 `-device edu`。
- Guest 内已有 kernel headers、`make`、`gcc`、`pciutils`。

### 1.2 本文不做

- 不调用 `pci_enable_device()`。
- 不 request BAR。
- 不 ioremap。
- 不读取 EDU MMIO register。
- 不创建设备节点。
- 不实现 ioctl。

## 2. 目标调用链

```text
Host: scripts/run-qemu.sh
  -> QEMU 启动 EDU PCI device
  -> Guest Linux PCI core 枚举 1234:11e8
  -> Guest 编译 miniaccel_drv.ko
  -> insmod miniaccel_drv.ko
  -> pci_register_driver(&miniaccel_driver)
  -> pci_device_id 匹配 1234:11e8
  -> miniaccel_probe()
  -> lspci -nnk 显示 Kernel driver in use: miniaccel_drv
  -> rmmod miniaccel_drv
  -> miniaccel_remove()
```

## 3. Day 3：创建最小 out-of-tree KMD

### 3.1 实现文件

本次新增或使用的驱动文件：

```text
driver/char/miniaccel_drv.c
driver/char/Makefile
scripts/build-driver.sh
```

`Makefile` 使用 `obj-m := miniaccel_drv.o`，因此最终模块名是：

```text
miniaccel_drv.ko
```

注意：模块名不是 `miniaccel.ko`。后续 `insmod`、`rmmod`、`lspci -nnk`
和测试脚本都统一使用 `miniaccel_drv`。

### 3.2 在 Guest 内确认 kernel headers

从宿主机执行：

```bash
cd ~/Miniaccel/miniaccel-stack
./scripts/ssh-guest.sh 'set -eu; echo uname=$(uname -r); if [ -d /lib/modules/$(uname -r)/build ]; then echo build-dir-ok; else echo build-dir-missing; fi'
```

实际输出：

```text
uname=6.1.0-49-cloud-amd64
build-dir-ok
```

验收：

- Guest 的运行 kernel 是 `6.1.0-49-cloud-amd64`。
- 对应 headers 目录存在，可以编译 out-of-tree KMD。

### 3.3 同步代码到 Guest 并编译

本次验证使用临时目录 `~/miniaccel-stack-review`，不依赖宿主机直接编译：

```bash
cd ~/Miniaccel/miniaccel-stack
./scripts/ssh-guest.sh 'rm -rf ~/miniaccel-stack-review && mkdir -p ~/miniaccel-stack-review'
scp -F scripts/ssh-config -i .ssh/id_ed25519 -r driver scripts tests miniaccel:~/miniaccel-stack-review/
./scripts/ssh-guest.sh 'cd ~/miniaccel-stack-review && ./scripts/build-driver.sh clean >/dev/null 2>&1 || true && ./scripts/build-driver.sh W=1'
```

实际输出摘录：

```text
make: Entering directory '/home/miniaccel/miniaccel-stack-review/driver/char'
make -C /lib/modules/6.1.0-49-cloud-amd64/build M=/home/miniaccel/miniaccel-stack-review/driver/char modules
make[1]: Entering directory '/usr/src/linux-headers-6.1.0-49-cloud-amd64'
  CC [M]  /home/miniaccel/miniaccel-stack-review/driver/char/miniaccel_drv.o
  MODPOST /home/miniaccel/miniaccel-stack-review/driver/char/Module.symvers
  CC [M]  /home/miniaccel/miniaccel-stack-review/driver/char/miniaccel_drv.mod.o
  LD [M]  /home/miniaccel/miniaccel-stack-review/driver/char/miniaccel_drv.ko
  BTF [M] /home/miniaccel/miniaccel-stack-review/driver/char/miniaccel_drv.ko
Skipping BTF generation for /home/miniaccel/miniaccel-stack-review/driver/char/miniaccel_drv.ko due to unavailability of vmlinux
make[1]: Leaving directory '/usr/src/linux-headers-6.1.0-49-cloud-amd64'
make: Leaving directory '/home/miniaccel/miniaccel-stack-review/driver/char'
```

验收：

- `miniaccel_drv.o` 编译成功。
- `miniaccel_drv.ko` 链接成功。
- `W=1` 没有 C 代码 warning。
- `Skipping BTF generation ... due to unavailability of vmlinux` 是当前 Guest
  缺少 `vmlinux` 时的普通提示，不影响模块加载验证。

## 4. Day 4：实现 PCI driver 匹配

### 4.1 驱动关键结构

驱动匹配表：

```c
static const struct pci_device_id miniaccel_id_table[] = {
	{ PCI_DEVICE(0x1234, 0x11e8) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, miniaccel_id_table);
```

PCI driver 注册：

```c
static struct pci_driver miniaccel_driver = {
	.name		= DRV_NAME,
	.id_table	= miniaccel_id_table,
	.probe		= miniaccel_probe,
	.remove		= miniaccel_remove,
};
```

设备日志使用 `dev_info(&pdev->dev, ...)`：

```c
dev_info(&pdev->dev, "probe called for %04x:%04x\n",
	 pdev->vendor, pdev->device);
```

这样 dmesg 会自动带上设备前缀，例如：

```text
miniaccel_drv 0000:00:02.0: probe called for 1234:11e8
```

模块入口和出口没有具体 `struct device`，所以继续使用 `pr_info()`：

```c
pr_info("%s: module_init\n", DRV_NAME);
pr_info("%s: module_exit\n", DRV_NAME);
```

### 4.2 手工加载并观察绑定

从宿主机执行：

```bash
cd ~/Miniaccel/miniaccel-stack
./scripts/ssh-guest.sh '
set -eu
cd ~/miniaccel-stack-review
if lsmod | awk "{print \$1}" | grep -qx miniaccel_drv; then sudo -n rmmod miniaccel_drv; fi
sudo -n insmod driver/char/miniaccel_drv.ko
lspci -nnk -s 00:02.0
sudo -n dmesg | grep -E "miniaccel_drv.*(module_init|probe called)" | tail -4
sudo -n rmmod miniaccel_drv
sudo -n dmesg | grep -E "miniaccel_drv.*(remove called|module_exit)" | tail -4
'
```

实际输出：

```text
00:02.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
	Subsystem: Red Hat, Inc. Device [1af4:1100]
	Kernel driver in use: miniaccel_drv
[ 6291.378404] miniaccel_drv: module_init
[ 6291.379224] miniaccel_drv 0000:00:02.0: probe called for 1234:11e8
[ 6318.337853] miniaccel_drv: module_init
[ 6318.338452] miniaccel_drv 0000:00:02.0: probe called for 1234:11e8
[ 6291.505999] miniaccel_drv 0000:00:02.0: remove called for 1234:11e8
[ 6291.506594] miniaccel_drv: module_exit
[ 6318.766561] miniaccel_drv 0000:00:02.0: remove called for 1234:11e8
[ 6318.768633] miniaccel_drv: module_exit
```

验收：

- `insmod` 后 `lspci -nnk -s 00:02.0` 显示 `Kernel driver in use: miniaccel_drv`。
- `probe()` 被调用，日志包含 `probe called for 1234:11e8`。
- `rmmod` 后 `remove()` 被调用，日志包含 `remove called for 1234:11e8`。
- `module_init` 和 `module_exit` 都出现，模块生命周期闭环。

### 4.3 卸载状态确认

执行：

```bash
./scripts/ssh-guest.sh 'lsmod | awk "{print \$1}" | grep -qx miniaccel_drv && echo still-loaded || echo unloaded'
```

实际输出：

```text
unloaded
```

验收：

- 测试结束后模块没有残留加载。

## 5. Day 5：自动化绑定/解绑测试

### 5.1 单次绑定/解绑测试

测试脚本：

```text
tests/probe/test_bind_unbind.sh
```

脚本检查点：

- 确认 `driver/char/miniaccel_drv.ko` 存在。
- 清理已加载的旧模块。
- `insmod miniaccel_drv.ko`。
- 用 `lspci -nnk -s 00:02.0` 检查 `Kernel driver in use: miniaccel_drv`。
- `rmmod miniaccel_drv`。
- 确认 `lsmod` 中不再存在 `miniaccel_drv`。
- 检查 dmesg 里出现过 probe/remove 关键日志。

执行：

```bash
cd ~/Miniaccel/miniaccel-stack
./scripts/ssh-guest.sh 'cd ~/miniaccel-stack-review && ./tests/probe/test_bind_unbind.sh'
```

实际输出：

```text
bind-unbind-ok
```

验收：

- 单次绑定/解绑自动化测试通过。

### 5.2 100 次 reload 测试

测试脚本：

```text
tests/probe/test_reload_100.sh
```

脚本检查点：

- 默认执行 100 次 `insmod` / `rmmod`。
- 结束后检查最近 300 行 dmesg，不能出现 `Oops`、`BUG:`、`WARNING:`、
  `Call Trace` 或 `panic`。

执行：

```bash
cd ~/Miniaccel/miniaccel-stack
./scripts/ssh-guest.sh 'cd ~/miniaccel-stack-review && ./tests/probe/test_reload_100.sh'
```

实际输出：

```text
reload-100-ok
no-recent-oops-warning-panic
```

验收：

- 100 次 reload 完成。
- 近期内核日志没有发现明显异常。

## 6. 踩坑与修正

| 问题 | 现象 | 原因 | 修正 |
|---|---|---|---|
| `dev_info()` 直接替换 `pr_info()` | 编译会缺少参数 | `dev_info()` 第一个参数必须是 `struct device *` | 在 `probe/remove` 中使用 `dev_info(&pdev->dev, ...)`；`module_init/module_exit` 保持 `pr_info()` |
| `Makefile` 使用 `M=$(PWD)` | 通过 `make -C driver/char` 调用时，kernel build 查找错误目录 | `PWD` 是调用者 shell 的目录，不一定是 Makefile 所在目录 | 改为 `M=$(CURDIR)` |
| 模块名写法不统一 | 文档命令可能写成 `miniaccel.ko` / `rmmod miniaccel` | `obj-m := miniaccel_drv.o` 决定模块名是 `miniaccel_drv` | 命令、脚本、验收统一使用 `miniaccel_drv` |
| out-of-tree 模块 taint | dmesg 出现 signature/key missing 或 taints kernel | 未签名外部模块的正常提示 | 当前阶段接受；只把它和 oops/warning 区分开 |
| BTF 跳过 | 构建输出 `Skipping BTF generation ...` | Guest headers 环境没有可用 `vmlinux` | 当前不影响 `.ko` 生成和加载 |

## 7. 本日产物

- `driver/char/miniaccel_drv.c`
- `driver/char/Makefile`
- `scripts/build-driver.sh`
- `tests/probe/test_bind_unbind.sh`
- `tests/probe/test_reload_100.sh`
- `docs/week-01-probe.md`

## 8. 下一步

下一份 SOP 进入 Week 02：BAR0/MMIO。建议顺序：

1. 给 `probe()` 添加设备私有结构 `struct miniaccel_dev`。
2. 使用 `pci_set_drvdata()` / `pci_get_drvdata()` 管理生命周期。
3. 调用 `pcim_enable_device()` 或等价 managed PCI enable 流程。
4. request BAR0 并映射 MMIO。
5. 只读 EDU 的安全寄存器，先验证 `ioread32()` 路径。
