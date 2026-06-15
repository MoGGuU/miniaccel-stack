#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
GUEST_DIR="$ROOT_DIR/build/guest"
SSH_DIR="$ROOT_DIR/.ssh"
BASE_IMAGE="$GUEST_DIR/debian-12-genericcloud-amd64.qcow2"
ROOTFS="$GUEST_DIR/rootfs.qcow2"
SEED_IMAGE="$GUEST_DIR/seed.img"
IMAGE_URL="${IMAGE_URL:-https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2}"

"$SCRIPT_DIR/check-host-deps.sh"
mkdir -p "$GUEST_DIR" "$SSH_DIR"

if [[ ! -f "$SSH_DIR/id_ed25519" ]]; then
  ssh-keygen -q -t ed25519 -N '' -C miniaccel-guest -f "$SSH_DIR/id_ed25519"
fi

if [[ ! -f "$BASE_IMAGE" ]]; then
  echo "Downloading Debian 12 cloud image..."
  curl -fL --retry 3 --output "$BASE_IMAGE.part" "$IMAGE_URL"
  mv "$BASE_IMAGE.part" "$BASE_IMAGE"
fi

if [[ ! -f "$ROOTFS" ]]; then
  qemu-img create -f qcow2 -F qcow2 -b "$BASE_IMAGE" "$ROOTFS" 12G
fi

PUBLIC_KEY="$(<"$SSH_DIR/id_ed25519.pub")"
cat >"$GUEST_DIR/user-data" <<EOF
#cloud-config
hostname: miniaccel-guest
manage_etc_hosts: true
users:
  - name: miniaccel
    groups: [adm, sudo]
    shell: /bin/bash
    sudo: ALL=(ALL) NOPASSWD:ALL
    ssh_authorized_keys:
      - $PUBLIC_KEY
ssh_pwauth: false
package_update: true
packages:
  - pciutils
  - openssh-server
runcmd:
  - [systemctl, enable, --now, ssh]
EOF

cat >"$GUEST_DIR/meta-data" <<'EOF'
instance-id: miniaccel-guest-v1
local-hostname: miniaccel-guest
EOF

cloud-localds "$SEED_IMAGE" "$GUEST_DIR/user-data" "$GUEST_DIR/meta-data"
echo "Guest RootFS is ready at $ROOTFS"

