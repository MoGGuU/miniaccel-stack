# 第 1 周：环境搭建

## 稳定的客户机配置

- 机器类型：QEMU `q35`
- 加速器：TCG，因此无需嵌套 KVM，也可以在 WSL2 中运行
- 客户机：Debian 12 通用云镜像，配合 12 GiB 的 qcow2 增量磁盘
- 学习设备：QEMU EDU，PCI ID 为 `1234:11e8`
- 网络：QEMU 用户模式网络，将宿主机 TCP 端口 `2222` 转发至客户机 SSH 端口 `22`
- 登录方式：使用 SSH 密钥登录 `miniaccel` 用户

## 验收检查

`scripts/verify-reboots.sh` 会执行三次冷启动。每次启动都会等待 cloud-init 和
SSH 就绪，然后验证 `lspci -nn` 的输出中包含 `1234:11e8`。

## 调试

客户机启动失败时，查看 `build/guest/serial.log`。使用以下命令连接 QEMU
监控器：

```bash
socat -,echo=0,icanon=0 unix-connect:build/guest/monitor.sock
```
