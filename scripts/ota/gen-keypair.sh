#!/usr/bin/env bash
set -euo pipefail

out_dir="${1:-keys}"
mkdir -p "$out_dir"

priv="$out_dir/swupdate_signing.key"
pub="$out_dir/public.pem"

if [[ -f "$priv" || -f "$pub" ]]; then
  echo "Refusing to overwrite existing keys in: $out_dir" >&2
  echo "Move them aside or choose a different output dir." >&2
  exit 2
fi

openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:3072 -out "$priv"
openssl pkey -in "$priv" -pubout -out "$pub"

echo "Generated:"
echo "  private: $priv"
echo "  public : $pub"
echo
echo "Copy the public key into the image overlay at:"
echo "  aa_wireless_dongle/board/common/rootfs_overlay/etc/swupdate/public.pem"

