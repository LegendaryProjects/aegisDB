#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

require_docker

if [[ -z "${REGISTRY_IMAGE}" ]]; then
  echo "Error: REGISTRY_IMAGE is empty in scripts/env.sh" >&2
  echo "Set REGISTRY_IMAGE (for example ghcr.io/<user>/aegisdb:latest) and retry." >&2
  exit 1
fi

docker tag "${IMAGE_NAME}" "${REGISTRY_IMAGE}"
docker push "${REGISTRY_IMAGE}"

echo "Pushed image: ${REGISTRY_IMAGE}"
