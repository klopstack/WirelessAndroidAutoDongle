#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
usage: docker-make-update-bundles.sh --board <raspberrypi4|raspberrypi5|...> --signing-key <path_in_repo>

Runs the update-bundle generation inside the repo's docker build image to
ensure required tools (openssl, cpio) are present.

Notes:
- <path_in_repo> must be a path inside the repo (it will be available in /app/).
EOF
}

board=""
signing_key=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --board) board="$2"; shift 2;;
    --signing-key) signing_key="$2"; shift 2;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown arg: $1" >&2; usage; exit 2;;
  esac
done

if [[ -z "$board" || -z "$signing_key" ]]; then
  usage
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
rel_key="${signing_key#"$repo_root/"}"
if [[ "$rel_key" = "$signing_key" && ! "$signing_key" =~ ^[^/].* ]]; then
  : # already relative
fi

cd "$repo_root"

docker compose run --rm bash -lc \
  "cd /app && ./scripts/ota/make-update-bundles.sh --board '$board' --signing-key '/app/$rel_key'"

