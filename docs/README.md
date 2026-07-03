# MiniAccel Docs

本文档目录只放 MiniAccel 当前实现过程相关记录。

## 周 SOP

周 SOP 直接放在 `docs/` 下，命名为 `week-XX-topic.md`。这类文档写每天如何落地：
目标、上下文、命令、验证、踩坑和产物。

- [第 1 周 Day 1：环境搭建](week-01-environment.md)
- [第 1 周 Day 2：PCI 枚举与驱动匹配](week-01-pci.md)
- [第 1 周 Day 3-5：KMD probe/remove 与绑定测试](week-01-probe.md)
- [第 2 周 Day 1-5：BAR0 映射与 MMIO 读取](week-02-mmio.md)
- [第 3 周 Day 1-7：字符设备、UAPI 与 ioctl](week-03-char-ioctl.md)
- [第 4 周：MSI 中断、completion 与 poll](week-04-interrupt-and-completion.md)
- [第 5 周：QEMU MiniAccel 虚拟 PCIe 设备](week-05-qemu-miniaccel.md)

## 接口文档

- [MiniAccel UAPI](week-03-uapi.md)
- [MiniAccel BAR0 Register Spec](register-spec.md)

## 源码阅读笔记

源码阅读笔记放在 `docs/source-reading-notes/` 下。这类文档写阅读目标、源码证据、
调用链图、理解心得和后续问题，不承担每天实现 SOP 的职责。

- [QEMU EDU PCI 设备](source-reading-notes/qemu-edu-pci.md)
- [从 QEMU EDU 到 MiniAccel](source-reading-notes/qemu-edu-to-miniaccel.md)
