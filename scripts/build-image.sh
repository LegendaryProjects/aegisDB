#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_docker

echo "Resetting demo runtime state (containers + node data)..."

# Stop/remove well-known node containers.
docker rm -f aegis-node1 aegis-node2 aegis-node3 >/dev/null 2>&1 || true

# Also remove any stray containers started from this image tag (e.g. orphaned client runs).
mapfile -t IMAGE_CONTAINERS < <(docker ps -aq --filter "ancestor=${IMAGE_NAME}")
if [[ ${#IMAGE_CONTAINERS[@]} -gt 0 ]]; then
	docker rm -f "${IMAGE_CONTAINERS[@]}" >/dev/null 2>&1 || true
fi

HELPER_IMAGE="${IMAGE_NAME}"
if ! docker image inspect "${HELPER_IMAGE}" >/dev/null 2>&1; then
	HELPER_IMAGE="ubuntu:22.04"
fi

HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

if ! rm -rf "$(node_data_dir 1)" "$(node_data_dir 2)" "$(node_data_dir 3)" 2>/dev/null; then
	echo "Detected root-owned node data. Cleaning via helper container..."
	docker run --rm --entrypoint /bin/bash \
		-v "${ROOT_DIR}/data:/data" \
		"${HELPER_IMAGE}" \
		-lc "rm -rf /data/node1 /data/node2 /data/node3 && chown -R ${HOST_UID}:${HOST_GID} /data"
fi

if ! ensure_data_dirs; then
	echo "Fixing data directory ownership and retrying..."
	docker run --rm --entrypoint /bin/bash \
		-v "${ROOT_DIR}/data:/data" \
		"${HELPER_IMAGE}" \
		-lc "chown -R ${HOST_UID}:${HOST_GID} /data"
	ensure_data_dirs
fi

echo "Runtime reset complete."

echo "Building Docker image: ${IMAGE_NAME}"
docker build -t "${IMAGE_NAME}" "${ROOT_DIR}"

echo "Build complete: ${IMAGE_NAME}"
