# miniaccel-stack

MiniAccel 的 Linux 驱动、用户态运行时、测试、脚本和设计文档仓库。

## 周一环境

宿主机使用 Ubuntu 24.04，客户机使用 Debian 12 通用云镜像。QEMU EDU
设备的 PCI ID 为 `1234:11e8`。

```bash
# 检查宿主机依赖
./scripts/check-host-deps.sh

# 下载客户机基础镜像，创建可写根文件系统和 cloud-init 配置盘
./scripts/prepare-rootfs.sh

# 在后台启动客户机
./scripts/run-qemu.sh

# 等待客户机就绪并检查 EDU 设备
./scripts/verify-guest.sh

# SSH 登录
./scripts/ssh-guest.sh

# 关闭客户机
./scripts/stop-qemu.sh

# 执行三次冷启动验收
./scripts/verify-reboots.sh
```

客户机串口日志写入 `build/guest/serial.log`。QEMU 监控器套接字位于
`build/guest/monitor.sock`。

## QEMU 源码仓库

同级目录 `../miniaccel-qemu` 是 `qemu/qemu` 的本地克隆，当前分支为
`miniaccel`，暂未修改源码。Git 远程仓库配置如下：

```bash
origin    git@github.com:MoGGuU/qemu.git
upstream  https://github.com/qemu/qemu.git
```
