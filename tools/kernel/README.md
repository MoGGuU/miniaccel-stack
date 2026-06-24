# Linux Kernel Style Tools

This directory vendors the Linux kernel style checker and its data files:

- `scripts/checkpatch.pl`
- `scripts/spelling.txt`
- `scripts/const_structs.checkpatch`

The repository root `.clang-format` is also copied from the upstream Linux
kernel tree.

Run the style checks from the repository root:

```bash
./scripts/check-driver-style.sh
```

Format the driver with the kernel clang-format configuration:

```bash
./scripts/format-driver.sh
```

Both scripts accept explicit file paths as additional arguments.

Upstream source:

```text
https://github.com/torvalds/linux
```
