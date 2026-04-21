#!/usr/bin/env bash

# Central demo/deployment configuration.
# Edit these addresses once; all scripts reuse them.

IMAGE_NAME="${IMAGE_NAME:-aegisdb:latest}"

# Optional registry image for push/pull scripts, e.g. ghcr.io/<user>/aegisdb:latest
REGISTRY_IMAGE="${REGISTRY_IMAGE:-}"

# Node addresses (ip:port)
# Single laptop on Docker Desktop: host.docker.internal:<port>
# Multi-laptop demo: replace with each laptop's real LAN IP:<port>
NODE1_ADDR="${NODE1_ADDR:-100.78.101.127:50051}"
NODE2_ADDR="${NODE2_ADDR:-100.116.88.75:50052}"
NODE3_ADDR="${NODE3_ADDR:-100.119.37.82:50053}"

#NODE1_ADDR="${NODE1_ADDR:-host.docker.internal:50051}"
#NODE2_ADDR="${NODE2_ADDR:-host.docker.internal:50052}"
#NODE3_ADDR="${NODE3_ADDR:-host.docker.internal:50053}"
