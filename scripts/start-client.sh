#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_docker

N1_IP="$(split_host "${NODE1_ADDR}")"
N1_PORT="$(split_port "${NODE1_ADDR}")"
N2_IP="$(split_host "${NODE2_ADDR}")"
N2_PORT="$(split_port "${NODE2_ADDR}")"
N3_IP="$(split_host "${NODE3_ADDR}")"
N3_PORT="$(split_port "${NODE3_ADDR}")"

echo "Starting terminal client against ${NODE1_ADDR}, ${NODE2_ADDR}, ${NODE3_ADDR}"
docker run --rm -it "${IMAGE_NAME}" \
  -lc "cd /app/build && ./aegis_client ${N1_IP} ${N1_PORT} ${N2_IP} ${N2_PORT} ${N3_IP} ${N3_PORT}"
