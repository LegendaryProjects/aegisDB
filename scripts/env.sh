#!/usr/bin/env bash

# Central demo/deployment configuration.
# Edit these addresses once; all scripts reuse them.

IMAGE_NAME="${IMAGE_NAME:-aegisdb:latest}"

# Optional registry image for push/pull scripts, e.g. ghcr.io/<user>/aegisdb:latest
REGISTRY_IMAGE="${REGISTRY_IMAGE:-}"

# Node addresses (ip:port)
NODE1_ADDR="${NODE1_ADDR:-192.168.1.10:50051}"
NODE2_ADDR="${NODE2_ADDR:-192.168.1.11:50052}"
NODE3_ADDR="${NODE3_ADDR:-192.168.1.12:50053}"
