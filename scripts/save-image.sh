#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_docker

OUT_FILE="${1:-${ROOT_DIR}/aegisdb-image.tar}"
docker save -o "${OUT_FILE}" "${IMAGE_NAME}"

echo "Saved image to: ${OUT_FILE}"
