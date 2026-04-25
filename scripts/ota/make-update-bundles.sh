#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
usage: make-update-bundles.sh --board <raspberrypi4|raspberrypi5|...> --signing-key <privkey> [--output-root <buildroot_output_dir>] [--images-dir <dir>]

Builds a signed update bundle next to the generated sdcard image:
  - update.swu

Assumes Buildroot output is in:
  buildroot/output/<board>   (or --output-root override)
EOF
}

board=""
signing_key=""
output_root=""
images_dir=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --board) board="$2"; shift 2;;
    --signing-key) signing_key="$2"; shift 2;;
    --output-root) output_root="$2"; shift 2;;
    --images-dir) images_dir="$2"; shift 2;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2;;
  esac
done

if [[ -z "$board" || -z "$signing_key" ]]; then
  usage
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
output_root="${output_root:-$repo_root/buildroot/output/$board}"
images_dir="${images_dir:-$output_root/images}"

rootfs_image=""
if [[ -f "$images_dir/rootfs.squashfs" ]]; then
  rootfs_image="$images_dir/rootfs.squashfs"
else
  echo "Could not find rootfs image in: $images_dir" >&2
  echo "Expected rootfs.squashfs (BR2_TARGET_ROOTFS_SQUASHFS)" >&2
  exit 3
fi

out="$images_dir/update.swu"

"$repo_root/scripts/ota/make-swu.sh" \
  --rootfs-image "$rootfs_image" \
  --signing-key "$signing_key" \
  --out "$out"

echo
echo "Bundles are in: $images_dir"
echo "  - $(basename "$out")"
echo
echo "Upload the bundle to your HTTP server, and trigger via MQTT:"
echo "  ota https://host/update.swu"

