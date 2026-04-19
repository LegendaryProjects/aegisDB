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

for cfg_addr in "${NODE1_ADDR}" "${NODE2_ADDR}" "${NODE3_ADDR}"; do
  cfg_host="$(split_host "${cfg_addr}")"
  if [[ "${cfg_host}" == "localhost" || "${cfg_host}" == "127.0.0.1" ]]; then
    echo "Error: NODE*_ADDR uses ${cfg_host}, which breaks container-to-container Raft peer communication." >&2
    echo "Set NODE*_ADDR in scripts/env.sh to host.docker.internal:<port> (single laptop on Docker Desktop)" >&2
    echo "or to each laptop's real LAN IP:<port> (multi-laptop demo)." >&2
    exit 1
  fi
done

ADDR="$(node_addr "${NODE_ID}")"
PORT="$(split_port "${ADDR}")"
PEERS="$(node_peers "${NODE_ID}")"
CONTAINER_NAME="$(node_name "${NODE_ID}")"
DATA_DIR="$(node_data_dir "${NODE_ID}")"

mkdir -p "${DATA_DIR}"

cleanup() {
  docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true
}

# Closing this terminal should also stop/remove the node container.
trap cleanup EXIT INT TERM HUP

# Avoid stale-name conflicts from previously interrupted sessions.
docker rm -f "${CONTAINER_NAME}" >/dev/null 2>&1 || true

echo "Starting ${CONTAINER_NAME} on ${ADDR}"
docker run -d --name "${CONTAINER_NAME}" \
  --label aegis.managed=true \
  --label aegis.role=node \
  --label "aegis.node_id=${NODE_ID}" \
  --user "$(id -u):$(id -g)" \
  -p "${PORT}:${PORT}" \
  -v "${DATA_DIR}:/var/lib/aegis" \
  "${IMAGE_NAME}" \
  -lc "cd /var/lib/aegis && /app/build/aegis_server ${NODE_ID} 0.0.0.0:${PORT} ${PEERS}" >/dev/null

echo "${CONTAINER_NAME} is running. Streaming logs (close terminal or Ctrl+C to stop this node)."
docker logs -f "${CONTAINER_NAME}"
