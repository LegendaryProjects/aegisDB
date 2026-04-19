#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

# shellcheck source=env.sh
source "${SCRIPT_DIR}/env.sh"

require_docker() {
  if ! command -v docker >/dev/null 2>&1; then
    echo "Error: docker is not installed or not in PATH." >&2
    exit 1
  fi
}

split_host() {
  local addr="$1"
  echo "${addr%:*}"
}

split_port() {
  local addr="$1"
  echo "${addr##*:}"
}

node_addr() {
  case "$1" in
    1) echo "${NODE1_ADDR}" ;;
    2) echo "${NODE2_ADDR}" ;;
    3) echo "${NODE3_ADDR}" ;;
    *)
      echo "Error: node id must be 1, 2, or 3." >&2
      return 1
      ;;
  esac
}

node_name() {
  echo "aegis-node$1"
}

node_data_dir() {
  echo "${ROOT_DIR}/data/node$1"
}

node_peers() {
  case "$1" in
    1) echo "${NODE2_ADDR} ${NODE3_ADDR}" ;;
    2) echo "${NODE1_ADDR} ${NODE3_ADDR}" ;;
    3) echo "${NODE1_ADDR} ${NODE2_ADDR}" ;;
    *)
      echo "Error: node id must be 1, 2, or 3." >&2
      return 1
      ;;
  esac
}

ensure_data_dirs() {
  mkdir -p "${ROOT_DIR}/data/node1" "${ROOT_DIR}/data/node2" "${ROOT_DIR}/data/node3"
}
