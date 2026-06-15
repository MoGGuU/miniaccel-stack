# miniaccel-stack

MiniAccel 的 Linux 驱动、用户态 Runtime、测试、脚本和设计文档仓库。

## 周一环境

宿主机是 Ubuntu 24.04，Guest 使用 Debian 12 generic cloud image。QEMU EDU
设备的 PCI ID 是 `1234:11e8`。

```bash
# 检查宿主机依赖
./scripts/check-host-deps.sh

# 下载 Guest 基础镜像，创建可写 RootFS 和 cloud-init seed
./scripts/prepare-rootfs.sh

# 后台启动 Guest
./scripts/run-qemu.sh

# 等待 Guest 就绪并检查 EDU
./scripts/verify-guest.sh

# SSH 登录
./scripts/ssh-guest.sh

# 关闭 Guest
./scripts/stop-qemu.sh

# 执行三次冷启动验收
./scripts/verify-reboots.sh
```

Guest 串口日志写入 `build/guest/serial.log`。QEMU monitor socket 位于
`build/guest/monitor.sock`。

## QEMU 源码仓库

同级目录 `../miniaccel-qemu` 是 `qemu/qemu` 的本地克隆，当前分支为
`miniaccel`，暂未修改源码。Git remote 配置如下：

```bash
origin    git@github.com:MoGGuU/qemu.git
upstream  https://github.com/qemu/qemu.git
```
