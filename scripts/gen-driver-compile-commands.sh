#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

DRIVER_DIR="${DRIVER_DIR:-$ROOT_DIR/driver/char}"
GUEST_ROOT_DIR="${GUEST_ROOT_DIR:-/home/miniaccel/miniaccel-stack-index}"
GUEST_DRIVER_DIR="${GUEST_DRIVER_DIR:-$GUEST_ROOT_DIR/driver/char}"
INDEX_DIR="$ROOT_DIR/build/driver-index"
HEADER_ROOT="$ROOT_DIR/build/kernel-headers"
GUEST_CC="$INDEX_DIR/compile_commands.guest.json"
OUT_CC="${OUT_CC:-$ROOT_DIR/compile_commands.json}"
OLD_DRIVER_CC="$DRIVER_DIR/compile_commands.json"
HOST_CC="${HOST_CC:-$(command -v gcc || command -v cc || true)}"

if [[ -z "$HOST_CC" ]]; then
  HOST_CC="/usr/bin/cc"
fi

case "$GUEST_DRIVER_DIR" in
  *"'"*)
    echo "GUEST_DRIVER_DIR must not contain single quotes: $GUEST_DRIVER_DIR" >&2
    exit 2
    ;;
esac

case "$GUEST_ROOT_DIR" in
  *"'"*)
    echo "GUEST_ROOT_DIR must not contain single quotes: $GUEST_ROOT_DIR" >&2
    exit 2
    ;;
esac

SSH=(ssh -F "$SCRIPT_DIR/ssh-config" -i "$ROOT_DIR/.ssh/id_ed25519" miniaccel)

if [[ ! -d "$DRIVER_DIR" ]]; then
  echo "Driver directory not found: $DRIVER_DIR" >&2
  exit 1
fi

if [[ ! -f "$DRIVER_DIR/Makefile" ]]; then
  echo "Driver Makefile not found: $DRIVER_DIR/Makefile" >&2
  exit 1
fi

mkdir -p "$INDEX_DIR" "$HEADER_ROOT"

echo "Checking guest Kbuild tooling..."
"${SSH[@]}" 'set -e; command -v bear >/dev/null; test -d /lib/modules/$(uname -r)/build'

echo "Syncing driver sources to guest: $GUEST_DRIVER_DIR"
"${SSH[@]}" "rm -rf '$GUEST_ROOT_DIR' && mkdir -p '$GUEST_ROOT_DIR'"
tar -C "$ROOT_DIR" \
  --exclude='compile_commands.json' \
  --exclude='driver/char/.tmp_versions' \
  --exclude='driver/char/.*.cmd' \
  --exclude='driver/char/*.ko' \
  --exclude='driver/char/*.mod' \
  --exclude='driver/char/*.mod.c' \
  --exclude='driver/char/*.o' \
  --exclude='driver/char/Module.symvers' \
  --exclude='driver/char/modules.order' \
  -cf - driver/char include/uapi | "${SSH[@]}" "tar -C '$GUEST_ROOT_DIR' -xf -"

echo "Capturing guest Kbuild command line with bear..."
"${SSH[@]}" "set -e; cd '$GUEST_DRIVER_DIR'; rm -f compile_commands.json; bear -- make clean all >/tmp/miniaccel-driver-index.log 2>&1 || { cat /tmp/miniaccel-driver-index.log >&2; exit 1; }"
"${SSH[@]}" "cat '$GUEST_DRIVER_DIR/compile_commands.json'" > "$GUEST_CC"

mapfile -t GUEST_HEADER_DIRS < <(
  python3 - "$GUEST_CC" <<'PY'
import json
import re
import sys

with open(sys.argv[1], encoding="utf-8") as f:
    data = json.load(f)

blob = json.dumps(data)
seen = set()
for match in re.findall(r"/usr/src/linux-headers-[^/\"'\s,]+", blob):
    if match not in seen:
        seen.add(match)
        print(match)
PY
)

for guest_header in "${GUEST_HEADER_DIRS[@]}"; do
  header_base="$(basename "$guest_header")"
  header_parent="$(dirname "$guest_header")"
  header_dest="$HEADER_ROOT/$header_base"

  if [[ ! -d "$header_dest" || "${REFRESH_HEADERS:-0}" == "1" ]]; then
    echo "Copying guest kernel headers: $guest_header"
    rm -rf "$header_dest"
    "${SSH[@]}" "tar -C '$header_parent' -cf - '$header_base'" | tar -C "$HEADER_ROOT" -xf -
  else
    echo "Using cached kernel headers: $header_dest"
  fi
done

HOST_DRIVER_DIR="$(cd "$DRIVER_DIR" && pwd)"

python3 - "$GUEST_CC" "$OUT_CC" "$ROOT_DIR" "$GUEST_DRIVER_DIR" "$HOST_DRIVER_DIR" "$HEADER_ROOT" "$HOST_CC" "${GUEST_HEADER_DIRS[@]}" <<'PY'
import glob
import json
import os
import sys

in_path, out_path, root_dir, guest_driver, host_driver, header_root, host_cc = sys.argv[1:8]
guest_headers = sys.argv[8:]
header_map = {
    path: os.path.join(header_root, os.path.basename(path))
    for path in guest_headers
}

def rewrite_string(value):
    value = value.replace(guest_driver.rstrip("/"), host_driver.rstrip("/"))
    for guest_path, host_path in sorted(header_map.items(), key=lambda item: len(item[0]), reverse=True):
        value = value.replace(guest_path, host_path)
    return value

def rewrite(value):
    if isinstance(value, str):
        return rewrite_string(value)
    if isinstance(value, list):
        return [rewrite(item) for item in value]
    if isinstance(value, dict):
        return {key: rewrite(item) for key, item in value.items()}
    return value

with open(in_path, encoding="utf-8") as f:
    entries = json.load(f)

rewritten = []
for entry in entries:
    item = rewrite(entry)
    file_path = item.get("file", "")
    if file_path.endswith(".mod.c"):
        continue
    args = item.get("arguments")
    if isinstance(args, list) and args:
        args[0] = host_cc
    rewritten.append(item)

seen_files = {entry.get("file") for entry in rewritten}
for source in sorted(glob.glob(os.path.join(root_dir, "tools", "*.c"))):
    if source in seen_files:
        continue
    output = os.path.join(root_dir, "build", "tools", os.path.splitext(os.path.basename(source))[0] + ".o")
    rewritten.append({
        "arguments": [
            host_cc,
            "-Wall",
            "-Wextra",
            "-std=gnu11",
            "-I" + os.path.join(root_dir, "include", "uapi"),
            "-c",
            source,
            "-o",
            output,
        ],
        "directory": root_dir,
        "file": source,
        "output": output,
    })

os.makedirs(os.path.dirname(out_path), exist_ok=True)
with open(out_path, "w", encoding="utf-8") as f:
    json.dump(rewritten, f, indent=2)
    f.write("\n")
PY

if [[ "$OUT_CC" != "$OLD_DRIVER_CC" ]]; then
  rm -f "$OLD_DRIVER_CC"
fi

echo "Wrote: $OUT_CC"
echo "Kernel headers cache: $HEADER_ROOT"
