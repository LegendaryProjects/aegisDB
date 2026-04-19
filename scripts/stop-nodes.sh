#!/usr/bin/env bash
set -euo pipefail

if ! command -v docker >/dev/null 2>&1; then
  echo "Error: docker is not installed or not in PATH." >&2
  exit 1
fi

docker rm -f aegis-node1 aegis-node2 aegis-node3 >/dev/null 2>&1 || true
echo "Stopped containers: aegis-node1, aegis-node2, aegis-node3"
