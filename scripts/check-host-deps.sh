#!/usr/bin/env bash
set -euo pipefail

commands=(
  qemu-system-x86_64 qemu-img cloud-localds
  git cmake gdb gcc make ninja pkg-config
  curl ssh ssh-keygen
)

missing=()
for command in "${commands[@]}"; do
  if ! command -v "$command" >/dev/null 2>&1; then
    missing+=("$command")
  fi
done

if ((${#missing[@]})); then
  printf 'Missing commands: %s\n' "${missing[*]}" >&2
  cat >&2 <<'EOF'
Install the Ubuntu packages with:
sudo apt-get update
sudo apt-get install -y qemu-system-x86 qemu-utils cloud-image-utils \
  build-essential ninja-build pkg-config libglib2.0-dev libpixman-1-dev \
  libaio-dev libcap-ng-dev libattr1-dev libslirp-dev python3-venv \
  python3-sphinx flex bison libelf-dev libssl-dev bc dwarves cpio rsync \
  git cmake gdb
EOF
  exit 1
fi

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
CUSTOM_QEMU="$ROOT_DIR/../miniaccel-qemu/build/qemu-system-x86_64"

if [[ -x "$CUSTOM_QEMU" ]]; then
  "$CUSTOM_QEMU" -device help | grep -q 'name "miniaccel"' || {
    echo 'Custom QEMU build does not provide the miniaccel device.' >&2
    exit 1
  }
  echo "Host dependencies and QEMU miniaccel device are available."
else
  qemu-system-x86_64 -device help | grep -q 'name "edu"' || {
    echo 'System QEMU build does not provide the EDU fallback device.' >&2
    exit 1
  }
  echo "Host dependencies and QEMU EDU fallback device are available."
fi
