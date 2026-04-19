#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $(basename "$0") <node_id: 1|2|3>" >&2
  exit 1
fi

NODE_ID="$1"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_docker

ADDR="$(node_addr "${NODE_ID}")"
PORT="$(split_port "${ADDR}")"
PEERS="$(node_peers "${NODE_ID}")"
CONTAINER_NAME="$(node_name "${NODE_ID}")"
DATA_DIR="$(node_data_dir "${NODE_ID}")"

mkdir -p "${DATA_DIR}"

echo "Starting ${CONTAINER_NAME} on ${ADDR}"
docker run --rm -it --name "${CONTAINER_NAME}" \
  -p "${PORT}:${PORT}" \
  -v "${DATA_DIR}:/var/lib/aegis" \
  "${IMAGE_NAME}" \
  -lc "cd /var/lib/aegis && /app/build/aegis_server ${NODE_ID} 0.0.0.0:${PORT} ${PEERS}"
