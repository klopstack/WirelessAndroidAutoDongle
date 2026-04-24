#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
usage: make-swu.sh --rootfs-image <path> --signing-key <privkey.pem> --out <update.swu>

Creates a signed SWUpdate bundle (.swu) that can write the given rootfs image
to either A/B slot. The device selects which slot to apply at install time
via `swupdate -e stable,rootfsA` or `swupdate -e stable,rootfsB`.

Signing uses a simple RSA signature:
  openssl dgst -sha256 -sign <privkey> sw-description > sw-description.sig

The resulting .swu is a cpio archive where sw-description is the first entry,
and sw-description.sig is the second entry.
EOF
}

rootfs_image=""
signing_key=""
out_swu=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rootfs-image) rootfs_image="$2"; shift 2;;
    --signing-key) signing_key="$2"; shift 2;;
    --out) out_swu="$2"; shift 2;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2;;
  esac
done

if [[ -z "$rootfs_image" || -z "$signing_key" || -z "$out_swu" ]]; then
  usage
  exit 2
fi

if [[ ! -f "$rootfs_image" ]]; then
  echo "Missing rootfs image: $rootfs_image" >&2
  exit 3
fi
if [[ ! -f "$signing_key" ]]; then
  echo "Missing signing key: $signing_key" >&2
  exit 3
fi

tmp="$(mktemp -d)"
cleanup() { rm -rf "$tmp"; }
trap cleanup EXIT

cp -f "$rootfs_image" "$tmp/rootfs.ext4"

cat >"$tmp/sw-description" <<EOF
software =
{
    version = "1.0";
    description = "AAWG rootfs update";

    stable = {
        rootfsA = {
            images: (
                {
                    filename = "rootfs.ext4";
                    device = "/dev/mmcblk0p2";
                    type = "raw";
                }
            );
        };
        rootfsB = {
            images: (
                {
                    filename = "rootfs.ext4";
                    device = "/dev/mmcblk0p3";
                    type = "raw";
                }
            );
        };
    };
}
EOF

openssl dgst -sha256 -sign "$signing_key" -out "$tmp/sw-description.sig" "$tmp/sw-description"

# .swu format: cpio archive, sw-description MUST be first, signature should follow.
(
  cd "$tmp"
  : > "$out_swu"
  printf '%s\n' "sw-description" "sw-description.sig" "rootfs.ext4" \
    | cpio -ov -H crc > "$out_swu"
)

echo "Created: $out_swu"

