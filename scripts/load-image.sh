#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_docker

IN_FILE="${1:-${ROOT_DIR}/aegisdb-image.tar}"
if [[ ! -f "${IN_FILE}" ]]; then
  echo "Error: image archive not found: ${IN_FILE}" >&2
  exit 1
fi

docker load -i "${IN_FILE}"

echo "Loaded image from: ${IN_FILE}"
