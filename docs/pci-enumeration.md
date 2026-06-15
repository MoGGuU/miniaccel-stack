# 第 1 周周二 SOP：理解 PCI 枚举与驱动匹配

这份 SOP 用于引导你亲手完成周二任务。不要一次做完所有步骤。每完成一步，先
确认现象、记录结论，再进入下一步。

本日最终目标：

```text
QEMU 启动 EDU
  -> Linux 枚举出 EDU
  -> 你编写的 PCI 驱动匹配 EDU
  -> Linux 调用 probe()
  -> 卸载模块时调用 remove()
```

本日不做：

- 不修改 QEMU 源码。
- 不调用 `pci_enable_device()`。
- 不申请或映射 BAR。
- 不读写 MMIO。
- 不处理中断和 DMA。

## 学习规则

1. 先观察，再阅读，再编码。
2. 每次只增加一个概念。
3. 不直接复制完整驱动答案。
4. 遇到错误时先保存完整错误信息，不要立即随机修改。
5. 每完成一个阶段，使用 `git diff` 检查自己做了什么。

## 第 0 步：确认起点

在宿主机执行：

```bash
cd ~/Miniaccel/miniaccel-stack
git status
./scripts/run-qemu.sh
./scripts/verify-guest.sh
```

如果 QEMU 已经运行，`run-qemu.sh` 会提示已有进程，此时直接运行
`verify-guest.sh`。

验收：

- `git status` 没有未提交修改。
- `verify-guest.sh` 能看到 `1234:11e8`。

完成后记录：

```text
EDU 的 PCI 地址：
EDU 的 Vendor ID：
EDU 的 Device ID：
```

## 第 1 步：从 Linux 观察 EDU

登录 Guest：

```bash
./scripts/ssh-guest.sh
```

在 Guest 中依次执行：

```bash
lspci -nn
lspci -nn -s 00:02.0
sudo lspci -vv -s 00:02.0
```

重点观察：

- `00:02.0` 表示什么？
- `[1234:11e8]` 表示什么？
- `Region 0` 的类型是什么？
- `Region 0` 的大小是多少？
- 当前是否存在 `Kernel driver in use`？

继续观察 sysfs：

```bash
cd /sys/bus/pci/devices/0000:00:02.0
pwd
cat vendor
cat device
cat class
cat resource
ls -l driver
```

如果 `ls -l driver` 提示文件不存在，这是正常现象，表示当前没有驱动绑定 EDU。

思考题：

1. `lspci` 是否创造了 EDU 设备？
2. 没有驱动绑定时，Linux 为什么仍然能显示 EDU？
3. `resource` 第一行的起始地址、结束地址和长度有什么关系？

验收：

- 能解释 PCI 地址 `domain:bus:device.function`。
- 能从 sysfs 找到 Vendor ID 和 Device ID。
- 能判断当前 EDU 是否绑定驱动。

完成此步后退出 Guest：

```bash
exit
```

## 第 2 步：阅读 QEMU EDU 官方文档

阅读：

- <https://www.qemu.org/docs/master/specs/edu.html>

本次只精读以下章节：

1. `Command line switches`
2. `PCI specs`
3. `MMIO area spec` 的开头和寄存器 `0x00`

不要在本步深入 IRQ 和 DMA。

阅读时填写：

```text
创建设备的 QEMU 参数：
默认 DMA mask：
PCI ID：
PCI Region 0 类型：
PCI Region 0 大小：
偏移 0x00 寄存器用途：
```

思考题：

1. QEMU 文档中的 PCI Region 0 与 `lspci` 中的 Region 0 是否对应？
2. QEMU 文档只规定 BAR 大小，为什么 Guest 中还会出现具体起始地址？

验收：

- 能指出 EDU 的 ID 和 BAR0 规格。
- 能区分“设备规定 BAR 大小”和“系统分配 BAR 地址”。

## 第 3 步：阅读 QEMU `hw/misc/edu.c`

进入 QEMU 源码仓库：

```bash
cd ~/Miniaccel/miniaccel-qemu
git status
```

确认当前不修改源码。

先使用搜索定位，不要从文件第一行开始逐行阅读：

```bash
rg -n 'edu_types|DEFINE_TYPES|edu_class_init|edu_instance_init' hw/misc/edu.c
rg -n 'pci_edu_realize|pci_register_bar|memory_region_init_io' hw/misc/edu.c
rg -n 'vendor_id|device_id|revision|class_id' hw/misc/edu.c
rg -n 'CONFIG_EDU|edu.c' hw/misc
```

然后阅读每个搜索结果附近约 20 至 40 行：

```bash
sed -n '<起始行>,<结束行>p' hw/misc/edu.c
```

阅读顺序：

1. `edu_types[]`
2. `DEFINE_TYPES(edu_types)`
3. `edu_class_init()`
4. `edu_instance_init()`
5. `pci_edu_realize()`

填写下表：

| 问题 | 你的答案 |
|---|---|
| QEMU 中 EDU 类型的名字是什么？ | |
| EDU 的父类型是什么？ | |
| 哪个函数设置 Vendor ID 和 Device ID？ | |
| 哪个函数为具体 EDU 实例设置默认 DMA mask？ | |
| 哪个函数创建 MMIO MemoryRegion？ | |
| 哪个函数把 MemoryRegion 注册为 BAR0？ | |
| BAR0 大小在哪里确定？ | |

关键区分：

```text
QEMU edu_class_init() != Linux 驱动 probe()
```

思考题：

1. `edu_class_init()` 是注册设备类型，还是初始化某个具体 EDU 实例？
2. `-device edu` 创建实例时，为什么最终会进入 `pci_edu_realize()`？
3. QEMU 中的 `PCIDevice` 与 Guest Linux 中的 `struct pci_dev` 是同一个对象吗？

验收：

- 能指出设置 PCI ID 的源码位置。
- 能指出注册 BAR0 的源码位置。
- 能口头解释类型注册与实例创建的区别。

## 第 4 步：画出 QEMU 设备创建路径

不要复制现成图。根据前面阅读结果，在本文末尾或你的学习笔记中补全：

```text
scripts/run-qemu.sh
  -> QEMU 参数：__________
  -> QEMU 找到类型：__________
  -> 实例初始化函数：__________
  -> realize 回调：__________
  -> 创建 MMIO：__________
  -> 注册 BAR0：__________
  -> 向 Guest 暴露 PCI ID：__________
```

画图时必须区分：

- 宿主机 QEMU 进程中的操作。
- Guest Linux 内核中的操作。

验收：

- 路径中包含 `-device edu`、类型、实例、realize 和 BAR0。
- 没有把 QEMU 函数写成 Linux 内核函数。

## 第 5 步：阅读 Linux PCI Driver 文档

按顺序阅读：

1. <https://docs.kernel.org/PCI/pci.html>
2. <https://docs.kernel.org/driver-api/driver-model/binding.html>
3. <https://docs.kernel.org/driver-api/driver-model/bus.html>

第一篇只精读：

- `Structure of PCI drivers`
- `pci_register_driver() call`
- `Device Initialization Steps`
- `PCI device shutdown`

阅读时回答：

```text
struct pci_driver 中负责匹配的字段：
匹配成功后调用的回调：
驱动注销或设备移除时调用的回调：
probe 返回 0 的含义：
probe 返回负数的含义：
pci_device_id 数组如何表示结束：
MODULE_DEVICE_TABLE 的用途：
```

从 Driver Binding 文档中理解两个触发匹配的时机：

1. 新设备注册时，遍历已有驱动。
2. 新驱动注册时，遍历已有设备。

当前实验属于第二种：EDU 已经被 Linux 枚举，之后你才加载模块。

验收：

- 能解释“枚举设备”与“绑定驱动”是两个阶段。
- 能解释为什么加载模块后才调用 `probe()`。
- 能解释为什么卸载模块会调用 `remove()`。

## 第 6 步：画出 Linux 枚举和匹配路径

补全以下路径：

```text
Linux 启动
  -> 扫描 PCI ____________
  -> 读取 EDU 的 ____________ 空间
  -> 发现 ID：____________
  -> 创建内核对象：____________
  -> 注册到 sysfs：____________

加载你的模块
  -> module_init
  -> 调用：____________
  -> PCI 总线比较设备 ID 与：____________
  -> 匹配成功
  -> 调用：____________
  -> 返回 0 后建立绑定
```

将第 4 步和第 6 步的图连接起来，形成最终路径：

```text
QEMU 创建设备 -> Linux 枚举设备 -> 驱动注册 -> ID 匹配 -> probe()
```

## 第 7 步：准备 Guest 内核模块构建环境

这一步只准备工具，不编写驱动。

启动并登录 Guest：

```bash
cd ~/Miniaccel/miniaccel-stack
./scripts/verify-guest.sh
./scripts/ssh-guest.sh
```

在 Guest 中检查：

```bash
uname -r
ls -l /lib/modules/$(uname -r)/build
gcc --version
make --version
```

如果缺失依赖，在 Guest 中执行：

```bash
sudo apt-get update
sudo apt-get install -y build-essential linux-headers-$(uname -r)
```

再次检查：

```bash
test -e /lib/modules/$(uname -r)/build && echo ready
```

验收：

- `ready` 被打印。
- 能解释为什么 Kernel Module 必须使用匹配当前 Guest 内核的 headers。

## 第 8 步：亲手创建最小 Out-of-tree Module

回到宿主机的仓库：

```bash
exit
cd ~/Miniaccel/miniaccel-stack
```

你需要亲手创建：

```text
driver/Makefile
driver/miniaccel_edu.c
```

先创建一个与 PCI 无关、只打印加载和卸载日志的最小模块。此时不要写
`pci_driver`。

你需要自行查找并使用：

- `module_init`
- `module_exit`
- `pr_info`
- `MODULE_LICENSE`

编写后不要立刻增加 PCI 代码。先将源码复制到 Guest，在 Guest 中构建、加载和
卸载，确认基础模块工作。

建议手动复制：

```bash
scp -F scripts/ssh-config -i .ssh/id_ed25519 \
  driver/Makefile driver/miniaccel_edu.c miniaccel:~/miniaccel-driver/
```

注意：首次复制前需要在 Guest 创建目录：

```bash
mkdir -p ~/miniaccel-driver
```

在 Guest 中构建：

```bash
cd ~/miniaccel-driver
make
sudo insmod miniaccel_edu.ko
sudo dmesg | tail -n 20
sudo rmmod miniaccel_edu
sudo dmesg | tail -n 20
```

验收：

- `.ko` 成功生成。
- 加载日志出现。
- 卸载日志出现。

完成后先停下，不要继续写 PCI 匹配。

## 第 9 步：增加 `pci_device_id`

基础模块通过后，再加入 EDU ID 表。

你需要自行查找：

- `struct pci_device_id`
- `PCI_DEVICE`
- `MODULE_DEVICE_TABLE`

要求：

- 只匹配 Vendor ID `0x1234` 和 Device ID `0x11e8`。
- ID 数组必须有结束项。
- 暂时不要注册 `pci_driver`。

构建后使用以下命令观察模块 alias：

```bash
/sbin/modinfo miniaccel_edu.ko | grep alias
```

思考题：

1. alias 中如何表示 Vendor ID 和 Device ID？
2. 为什么只有 ID 表还不会触发 `probe()`？

验收：

- alias 中出现 `1234` 和 `11E8`。
- 能解释 `MODULE_DEVICE_TABLE` 不等于注册驱动。

## 第 10 步：增加 `pci_driver` 和 probe/remove

最后加入：

- 一个只打印设备信息并返回 0 的 `probe()`。
- 一个只打印日志的 `remove()`。
- `struct pci_driver`。
- 在模块初始化函数中调用驱动注册 API。
- 在模块退出函数中调用对应注销 API。

本步需要你从 Linux PCI 文档中确定：

```text
注册 API：
注销 API：
probe 参数类型：
remove 参数类型：
struct pci_driver 必填字段：
```

限制：

- `probe()` 中不要启用设备。
- 不要映射 BAR。
- 不要访问寄存器。
- 不要申请 IRQ。

建议 `probe()` 至少打印：

- PCI 地址。
- Vendor ID。
- Device ID。
- BAR0 起始地址。
- BAR0 长度。

你需要自行查找用于获取 BAR 信息的 PCI API，避免直接写死地址。

## 第 11 步：验证 probe/remove

重新复制并构建模块，然后执行：

```bash
sudo dmesg -C
sudo insmod miniaccel_edu.ko
sudo dmesg
```

检查绑定关系：

```bash
lsmod | grep miniaccel_edu
readlink /sys/bus/pci/devices/0000:00:02.0/driver
ls -l /sys/bus/pci/drivers/miniaccel_edu
```

卸载：

```bash
sudo rmmod miniaccel_edu
sudo dmesg
ls -l /sys/bus/pci/devices/0000:00:02.0/driver
```

你可能看到以下提示：

```text
loading out-of-tree module taints kernel
module verification failed: signature and/or required key missing
```

在当前开发环境中，这表示模块来自内核源码树之外且没有签名，不代表
`probe/remove` 失败。

验收：

- 加载模块时调用 `probe()`。
- `probe()` 返回 0 后 sysfs 出现 driver 链接。
- 卸载模块时调用 `remove()`。
- 卸载后 driver 链接消失。

## 第 12 步：错误实验

完成正常路径后，只做一个错误实验：

将 Device ID 临时改成一个不匹配 EDU 的值，重新构建并加载。

观察：

- 模块能否加载？
- `probe()` 是否调用？
- sysfs 是否建立绑定？

完成观察后恢复正确 ID。

这个实验用于证明：

```text
模块成功加载 != 驱动成功匹配设备
```

## 第 13 步：提交前检查

在宿主机执行：

```bash
cd ~/Miniaccel/miniaccel-stack
git status
git diff
```

确保没有提交：

- `.ko`
- `.o`
- `.cmd`
- `Module.symvers`
- `modules.order`
- Guest RootFS
- SSH 私钥

需要提交：

```text
docs/pci-enumeration.md
driver/Makefile
driver/miniaccel_edu.c
```

建议将学习过程拆成多个提交，而不是一次提交所有内容：

```text
docs: document PCI enumeration path
feat: add minimal loadable kernel module
feat: register EDU PCI probe driver
```

## 向老师反馈时提供什么

每完成一步，将以下信息发给老师：

```text
当前步骤：
执行的命令：
完整输出或错误：
你的理解：
你不确定的问题：
```

老师会检查你的理解和结果，再引导下一步，而不是直接提供完整实现。
