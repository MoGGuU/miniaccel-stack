# Week 01: Environment

## Stable Guest configuration

- Machine: QEMU `q35`
- Accelerator: TCG, so it also works inside WSL2 without nested KVM
- Guest: Debian 12 generic cloud image with a 12 GiB qcow2 overlay
- Device under study: QEMU EDU, PCI ID `1234:11e8`
- Network: QEMU user networking, host TCP `2222` forwarded to Guest SSH `22`
- Login: SSH key for user `miniaccel`

## Acceptance checks

`scripts/verify-reboots.sh` performs three cold boots. Each boot waits for cloud-init
and SSH, then verifies that `lspci -nn` contains `1234:11e8`.

## Debugging

Read `build/guest/serial.log` when the Guest fails to boot. Connect to the monitor
with:

```bash
socat -,echo=0,icanon=0 unix-connect:build/guest/monitor.sock
```
