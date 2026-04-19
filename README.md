# AegisDB Multi-Laptop Demo Setup

This demo runs 3 Raft nodes on 3 different laptops (same network) with terminal-only commands.

## 1. Prerequisites
Install Docker and Docker Compose plugin on each laptop.

## 2. One-time configuration
Edit addresses once in `scripts/env.sh`:

```bash
NODE1_ADDR=192.168.1.10:50051
NODE2_ADDR=192.168.1.11:50052
NODE3_ADDR=192.168.1.12:50053
```

You can also set:

```bash
IMAGE_NAME=aegisdb:latest
REGISTRY_IMAGE=ghcr.io/<user>/aegisdb:latest
```

## 3. Build and distribute image

Option A: Build on every laptop.

```bash
./scripts/build-image.sh
```

Option B: Build once and push to registry.

```bash
./scripts/build-image.sh
./scripts/push-image.sh
```

Then on other laptops:

```bash
./scripts/pull-image.sh
```

Option C: Build once and copy via tar.

```bash
./scripts/build-image.sh
./scripts/save-image.sh
# copy aegisdb-image.tar to other laptop
./scripts/load-image.sh
```

## 4. Start servers (short commands)
Run one terminal on each laptop.

Laptop 1:

```bash
./scripts/start-node1.sh
```

Laptop 2:

```bash
./scripts/start-node2.sh
```

Laptop 3:

```bash
./scripts/start-node3.sh
```

Stop all running node containers:

```bash
./scripts/stop-nodes.sh
```

## 5. Run client (from any laptop or cloud VM)

```bash
./scripts/start-client.sh
```

## 6. Demo commands
Inside client terminal:

```sql
CREATE TABLE users COLUMNS id* name
INSERT INTO users VALUES (1, Alice)
SELECT FROM users
SHOW TABLES
```

## 7. Optional local simulation (single laptop)

```bash
docker compose up --build -d node1 node2 node3
docker compose run --rm --service-ports client
docker compose down
```

## Notes
- Keep ports 50051, 50052, 50053 open between machines.
- Do not use 127.0.0.1 for multi-laptop server addresses.
- Current transport is insecure gRPC; for internet demos use private networks/VPN.
