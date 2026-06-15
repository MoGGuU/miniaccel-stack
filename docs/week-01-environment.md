# 第 1 周环境搭建 SOP：从零启动 QEMU EDU 客户机

本文记录 MiniAccel 第 1 周周一环境的完整搭建过程，包括方案选择、依赖安装、
GitHub 仓库准备、客户机根文件系统制作、QEMU 启动参数、脚本设计、验收和故障
排查。

完成本文后，应得到以下两个本地仓库：

```text
~/Miniaccel/
├── miniaccel-qemu/       # qemu/qemu 的 Fork，本周暂不修改源码
└── miniaccel-stack/      # 驱动、运行时、测试、脚本和文档
```

最终验收目标：

1. 能够启动带 QEMU EDU PCI 设备的 Debian 客户机。
2. 能够通过 SSH 登录客户机。
3. 客户机执行 `lspci -nn` 能看到 EDU 设备 `1234:11e8`。
4. 连续三次冷启动均能得到相同结果。

## 1. 当前方案与关键决策

### 1.1 当前是否裁剪或修改了 QEMU

**没有。**

当前环境使用 Ubuntu 24.04 软件包提供的完整 QEMU：

```text
QEMU emulator version 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.16)
```

`scripts/run-qemu.sh` 默认调用 PATH 中的 `qemu-system-x86_64`，也就是
`/usr/bin/qemu-system-x86_64`。这个版本已经包含 EDU 设备，因此周一阶段无需
修改、裁剪或编译 QEMU 源码。

同级目录中的 `miniaccel-qemu` 是 `qemu/qemu` 的源码 Fork，当前仅创建了
`miniaccel` 分支，源码工作树保持干净。它将在后续把 EDU 改造成 MiniAccel 时
使用。

可以用以下命令确认：

```bash
qemu-system-x86_64 --version
qemu-system-x86_64 -device help | grep 'name "edu"'

git -C ../miniaccel-qemu status
git -C ../miniaccel-qemu branch -vv
```

### 1.2 为什么先使用系统 QEMU

- 周一目标是验证 PCI 枚举和 EDU 设备，而不是修改虚拟硬件。
- 系统 QEMU 已经包含 EDU，可以减少首次环境搭建的变量。
- 先建立稳定基线，后续自行编译 QEMU 后可以对比行为是否发生变化。
- 使用 `QEMU_SYSTEM_X86_64` 环境变量即可切换到自编译版本，无需修改启动脚本。

例如，后续完成源码构建后可以这样启动：

```bash
QEMU_SYSTEM_X86_64=../miniaccel-qemu/build/qemu-system-x86_64 \
  ./scripts/run-qemu.sh
```

### 1.3 客户机方案

客户机使用 Debian 12 通用云镜像，而不是从零安装发行版：

- 基础镜像：`debian-12-genericcloud-amd64.qcow2`
- 可写磁盘：基于基础镜像创建的 12 GiB qcow2 增量磁盘
- 初始化：cloud-init
- 登录：SSH 密钥
- 网络：QEMU 用户模式网络
- 虚拟 CPU：TCG

使用 TCG 是为了确保在 WSL2 中即使没有嵌套 KVM，也能稳定运行。其速度低于
KVM，但足够完成驱动学习和 EDU 验证。

## 2. 从零安装宿主机依赖

本文假设宿主机为 Ubuntu 24.04 或 WSL2 Ubuntu 24.04。

安装 QEMU、Git、CMake、GDB、Kernel 构建依赖和客户机镜像工具：

```bash
sudo apt-get update
sudo apt-get install -y \
  qemu-system-x86 qemu-utils cloud-image-utils \
  build-essential ninja-build pkg-config \
  libglib2.0-dev libpixman-1-dev libaio-dev libcap-ng-dev \
  libattr1-dev libslirp-dev python3-venv python3-sphinx \
  flex bison libelf-dev libssl-dev bc dwarves cpio rsync \
  git cmake gdb
```

主要软件用途：

| 软件或依赖 | 用途 |
|---|---|
| `qemu-system-x86` | 提供 `qemu-system-x86_64` 和 EDU 设备 |
| `qemu-utils` | 提供 `qemu-img`，创建 qcow2 增量磁盘 |
| `cloud-image-utils` | 提供 `cloud-localds`，生成 cloud-init 配置盘 |
| `build-essential`、`ninja-build` | 编译 Linux Kernel 和 QEMU |
| `libglib2.0-dev`、`libpixman-1-dev` 等 | 后续构建 QEMU 的依赖 |
| `flex`、`bison`、`libelf-dev` 等 | 后续构建 Linux Kernel 的依赖 |
| `gdb` | 调试用户程序、Kernel 和 QEMU |
| `git` | 管理源码和提交 |
| `cmake` | 后续构建用户态运行时 |

检查基础命令和 EDU 设备是否存在：

```bash
qemu-system-x86_64 --version
qemu-system-x86_64 -device help | grep 'name "edu"'
git --version
cmake --version
gdb --version
```

仓库中提供了自动检查脚本：

```bash
cd ~/Miniaccel/miniaccel-stack
./scripts/check-host-deps.sh
```

## 3. 从零准备 Git 仓库

### 3.1 Fork QEMU 并创建开发分支

使用 GitHub CLI 登录：

```bash
gh auth login
gh auth status
```

创建 QEMU Fork，然后克隆并创建 `miniaccel` 分支：

```bash
mkdir -p ~/Miniaccel
cd ~/Miniaccel

gh repo fork qemu/qemu --clone=false
git clone --filter=blob:none https://github.com/qemu/qemu.git miniaccel-qemu
cd miniaccel-qemu
git switch -c miniaccel
```

配置远程仓库：

```bash
git remote rename origin upstream
git remote add origin git@github.com:MoGGuU/qemu.git
git push -u origin miniaccel
```

当前实际配置：

```text
origin    git@github.com:MoGGuU/qemu.git
upstream  https://github.com/qemu/qemu.git
```

注意：周一阶段不要修改 QEMU 源码。`git status` 应保持干净。

### 3.2 创建 miniaccel-stack

从零创建时可执行：

```bash
cd ~/Miniaccel
mkdir -p miniaccel-stack/{driver,include/uapi,runtime,tests,scripts,docs,build}
cd miniaccel-stack
git init -b main
```

当前远程仓库为：

```text
git@github.com:MoGGuU/miniaccel-stack.git
```

## 4. miniaccel-stack 目录结构

```text
miniaccel-stack/
├── .ssh/                       # 自动生成的客户机 SSH 密钥，不提交
├── build/
│   └── guest/                  # 客户机镜像、日志、PID 和监控器套接字
├── docs/
│   └── week-01-environment.md  # 本文
├── driver/                     # 后续 Linux Kernel 驱动
├── include/uapi/               # 后续用户态接口头文件
├── runtime/                    # 后续用户态运行时
├── scripts/
│   ├── check-host-deps.sh
│   ├── prepare-rootfs.sh
│   ├── run-qemu.sh
│   ├── ssh-config
│   ├── ssh-guest.sh
│   ├── stop-qemu.sh
│   ├── verify-guest.sh
│   └── verify-reboots.sh
└── tests/                      # 后续测试
```

`.gitignore` 忽略以下本地生成内容：

```gitignore
/build/*
!/build/.gitkeep
/.ssh/
```

客户机镜像和私钥不能提交到 Git。

## 5. 脚本编写原则

所有 Shell 脚本开头均使用：

```bash
#!/usr/bin/env bash
set -euo pipefail
```

含义：

- `-e`：命令失败后立即退出。
- `-u`：使用未定义变量时立即失败。
- `-o pipefail`：管道中任意命令失败，整个管道都视为失败。

脚本通过自身路径计算仓库根目录，因此可以从任意工作目录调用：

```bash
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
```

## 6. 各脚本的实现

### 6.1 `check-host-deps.sh`：检查宿主机依赖

脚本维护所需命令列表，逐项使用 `command -v` 检查：

```bash
commands=(
  qemu-system-x86_64 qemu-img cloud-localds
  git cmake gdb gcc make ninja pkg-config
  curl ssh ssh-keygen
)
```

除了检查命令，还验证当前 QEMU 是否编译了 EDU 设备：

```bash
qemu-system-x86_64 -device help | grep -q 'name "edu"'
```

执行：

```bash
./scripts/check-host-deps.sh
```

成功输出：

```text
Host dependencies and QEMU EDU device are available.
```

### 6.2 `prepare-rootfs.sh`：准备客户机 RootFS

此脚本依次完成：

1. 检查宿主机依赖。
2. 创建 `build/guest` 和 `.ssh` 目录。
3. 生成仅供客户机使用的 Ed25519 SSH 密钥。
4. 下载 Debian 12 通用云镜像。
5. 创建 12 GiB qcow2 增量磁盘。
6. 生成 cloud-init 的 `user-data` 和 `meta-data`。
7. 使用 `cloud-localds` 生成 `seed.img`。

生成 SSH 密钥：

```bash
ssh-keygen -q -t ed25519 -N '' -C miniaccel-guest \
  -f .ssh/id_ed25519
```

下载基础镜像时先写入 `.part` 临时文件，下载成功后再重命名，避免中断下载留下
看似完整的损坏镜像：

```bash
curl -fL --retry 3 --output "$BASE_IMAGE.part" "$IMAGE_URL"
mv "$BASE_IMAGE.part" "$BASE_IMAGE"
```

创建增量磁盘：

```bash
qemu-img create -f qcow2 -F qcow2 \
  -b debian-12-genericcloud-amd64.qcow2 rootfs.qcow2 12G
```

其中：

- 基础镜像保持只读和干净。
- 客户机产生的修改只写入 `rootfs.qcow2`。
- `12G` 是虚拟容量，不会立即占用 12 GiB 宿主机空间。

cloud-init 配置完成以下工作：

- 将主机名设置为 `miniaccel-guest`。
- 创建 `miniaccel` 用户。
- 注入 SSH 公钥。
- 允许该用户无密码执行 `sudo`。
- 禁止 SSH 密码登录。
- 安装 `pciutils` 和 `openssh-server`。
- 启用 SSH 服务。

执行：

```bash
./scripts/prepare-rootfs.sh
```

主要生成文件：

```text
.ssh/id_ed25519
.ssh/id_ed25519.pub
build/guest/debian-12-genericcloud-amd64.qcow2
build/guest/rootfs.qcow2
build/guest/user-data
build/guest/meta-data
build/guest/seed.img
```

该脚本具有幂等性：基础镜像、RootFS 和 SSH 密钥已经存在时不会重复创建。

### 6.3 `run-qemu.sh`：稳定启动 EDU 客户机

核心启动命令为：

```bash
qemu-system-x86_64 \
  -name miniaccel-guest \
  -machine q35,accel=tcg \
  -cpu max \
  -smp 2 \
  -m 1024 \
  -device edu \
  -drive file=rootfs.qcow2,if=virtio,format=qcow2 \
  -drive file=seed.img,if=virtio,format=raw,readonly=on \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2222-:22 \
  -device virtio-net-pci,netdev=net0 \
  -display none \
  -serial file:serial.log \
  -monitor unix:monitor.sock,server=on,wait=off \
  -pidfile qemu.pid \
  -daemonize
```

参数说明：

| 参数 | 作用 |
|---|---|
| `-machine q35,accel=tcg` | 使用现代 PCIe 风格的 q35 机器和 TCG |
| `-cpu max` | 为 TCG 客户机提供较完整的 x86 CPU 特性 |
| `-smp 2` | 分配 2 个虚拟 CPU |
| `-m 1024` | 分配 1 GiB 内存 |
| `-device edu` | 挂载本周学习的 EDU PCI 设备 |
| `if=virtio` | 使用 virtio 块设备连接磁盘 |
| `readonly=on` | cloud-init 配置盘只读 |
| `hostfwd=...2222-:22` | 将宿主机 `127.0.0.1:2222` 转发至客户机 SSH |
| `-display none` | 不打开图形窗口 |
| `-serial file:...` | 将串口输出保存到日志 |
| `-monitor unix:...` | 创建 QEMU 监控器 Unix 套接字 |
| `-pidfile` | 保存 QEMU 进程号 |
| `-daemonize` | 在后台运行 |

启动前，脚本会验证 `rootfs.qcow2` 和 `seed.img` 是否存在，并检查已有 PID 是否
仍在运行，避免重复启动两个实例争用 SSH 端口。

执行：

```bash
./scripts/run-qemu.sh
```

可以通过环境变量切换 QEMU 可执行文件：

```bash
# 使用自行编译的 QEMU
QEMU_SYSTEM_X86_64=../miniaccel-qemu/build/qemu-system-x86_64 \
  ./scripts/run-qemu.sh
```

`run-qemu.sh` 也支持使用 `SSH_PORT` 修改转发端口，但 `ssh-config` 当前固定使用
`2222`。如果需要更换端口，必须同时修改 `scripts/ssh-config`。

### 6.4 `ssh-config` 与 `ssh-guest.sh`：登录客户机

`ssh-config` 定义固定的 SSH 别名和连接参数：

```sshconfig
Host miniaccel
    HostName 127.0.0.1
    Port 2222
    User miniaccel
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
    LogLevel ERROR
```

开发客户机经常重建，主机密钥可能变化，因此该学习环境关闭了 known-host
校验。不要把这个配置用于生产服务器。

`ssh-guest.sh` 自动选择仓库中的私钥，并将额外参数传给 SSH：

```bash
# 交互登录
./scripts/ssh-guest.sh

# 非交互执行一条命令
./scripts/ssh-guest.sh 'lspci -nn | grep 1234:11e8'
```

SSH 登录后看到 Debian 欢迎信息属于正常现象。

### 6.5 `verify-guest.sh`：验证客户机和 EDU

脚本最多等待 10 分钟，每 5 秒尝试一次 SSH，并在客户机中等待 cloud-init
完成：

```bash
cloud-init status --wait
```

客户机就绪后，脚本检查 EDU 的 PCI ID：

```bash
lspci -nn | grep -i '1234:11e8'
```

执行：

```bash
./scripts/verify-guest.sh
```

成功输出类似：

```text
Guest is ready. EDU device: 00:02.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
```

### 6.6 `stop-qemu.sh`：正常关闭客户机

脚本先通过 SSH 在客户机内执行：

```bash
sudo poweroff
```

随后最多等待 30 秒，确认 QEMU 进程退出。使用客户机内部关机可以让文件系统
正常卸载，避免直接杀死 QEMU 导致 RootFS 损坏。

执行：

```bash
./scripts/stop-qemu.sh
```

### 6.7 `verify-reboots.sh`：三次冷启动验收

脚本循环执行三次：

```text
run-qemu.sh -> verify-guest.sh -> stop-qemu.sh
```

执行：

```bash
./scripts/verify-reboots.sh
```

最终应输出：

```text
Three cold boots completed successfully.
```

## 7. 标准操作流程

### 7.1 首次搭建

```bash
cd ~/Miniaccel/miniaccel-stack

./scripts/check-host-deps.sh
./scripts/prepare-rootfs.sh
./scripts/run-qemu.sh
./scripts/verify-guest.sh
./scripts/ssh-guest.sh
```

登录客户机后执行：

```bash
whoami
hostname
uname -a
lspci -nn | grep 1234:11e8
sudo lspci -vv -s 00:02.0
```

预期关键信息：

```text
miniaccel
miniaccel-guest
00:02.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
```

退出并关闭客户机：

```bash
exit
./scripts/stop-qemu.sh
```

### 7.2 三次冷启动验收

```bash
./scripts/verify-reboots.sh
```

### 7.3 日常使用

```bash
cd ~/Miniaccel/miniaccel-stack
./scripts/run-qemu.sh
./scripts/verify-guest.sh
./scripts/ssh-guest.sh
```

工作完成后：

```bash
exit
./scripts/stop-qemu.sh
```

## 8. 如何确认 EDU 设备

EDU 设备的 PCI Vendor ID 和 Device ID 为：

```text
1234:11e8
```

基础检查：

```bash
lspci -nn | grep 1234:11e8
```

查看详细配置、BAR 和能力：

```bash
sudo lspci -vv -s 00:02.0
sudo lspci -xxx -s 00:02.0
```

周一阶段只要求确认设备被 PCI 子系统枚举。此时还没有编写 MiniAccel Linux
驱动，所以设备显示为 `Unclassified device` 是正常现象。

## 9. 故障排查

### 9.1 提示 RootFS 或 seed.img 不存在

错误类似：

```text
Missing .../rootfs.qcow2; run scripts/prepare-rootfs.sh first.
```

解决：

```bash
./scripts/prepare-rootfs.sh
```

### 9.2 提示 QEMU 已经运行

解决：

```bash
./scripts/stop-qemu.sh
./scripts/run-qemu.sh
```

确认进程：

```bash
cat build/guest/qemu.pid
ps -fp "$(cat build/guest/qemu.pid)"
```

### 9.3 SSH 无法连接

先等待自动验证：

```bash
./scripts/verify-guest.sh
```

检查端口和日志：

```bash
ss -lnt | grep 2222
tail -n 200 build/guest/serial.log
```

首次启动会运行 cloud-init 并安装软件，通常比后续启动慢。

### 9.4 客户机中找不到 EDU

确认宿主机 QEMU 支持 EDU：

```bash
qemu-system-x86_64 -device help | grep 'name "edu"'
```

确认启动脚本包含：

```text
-device edu
```

确认当前 QEMU 进程的命令行：

```bash
tr '\0' ' ' </proc/"$(cat build/guest/qemu.pid)"/cmdline
```

### 9.5 查看 QEMU 串口日志

```bash
tail -f build/guest/serial.log
```

### 9.6 连接 QEMU Monitor

安装 `socat`：

```bash
sudo apt-get install -y socat
```

连接：

```bash
socat -,echo=0,icanon=0 unix-connect:build/guest/monitor.sock
```

进入监控器后可执行：

```text
info pci
info qtree
info status
quit
```

## 10. 重建客户机

如果需要丢弃客户机中的全部修改并重新初始化，只删除增量磁盘和 seed 配置，
不要删除基础镜像：

```bash
./scripts/stop-qemu.sh
rm -f build/guest/rootfs.qcow2
rm -f build/guest/seed.img
rm -f build/guest/user-data
rm -f build/guest/meta-data
./scripts/prepare-rootfs.sh
```

如需同时生成新的客户机 SSH 密钥：

```bash
rm -rf .ssh
./scripts/prepare-rootfs.sh
```

## 11. 后续切换到自行编译的 QEMU

周一环境没有编译 QEMU。后续需要修改 EDU 或实现 MiniAccel 时，再在
`miniaccel-qemu` 中构建：

```bash
cd ~/Miniaccel/miniaccel-qemu
mkdir -p build
cd build
../configure --target-list=x86_64-softmmu --enable-debug
ninja
```

这里的 `--target-list=x86_64-softmmu` 会只构建 x86_64 系统模拟器，可以减少
构建时间。它属于**构建目标裁剪**，不是当前周一环境使用的 QEMU。

使用自编译 QEMU 启动现有客户机：

```bash
cd ~/Miniaccel/miniaccel-stack
QEMU_SYSTEM_X86_64=../miniaccel-qemu/build/qemu-system-x86_64 \
  ./scripts/run-qemu.sh
./scripts/verify-guest.sh
```

这样可以复用同一份 RootFS 和验收脚本，对比系统 QEMU 与修改后 QEMU 的行为。

## 12. 当前实际验收结果

当前环境已经完成三次冷启动验收。每次均成功：

- 启动 Debian 12 客户机。
- 通过 SSH 登录 `miniaccel` 用户。
- 等待 cloud-init 完成。
- 使用 `lspci -nn` 发现 EDU 设备。
- 正常关闭客户机。

EDU 枚举结果：

```text
00:02.0 Unclassified device [00ff]: Device [1234:11e8] (rev 10)
```
