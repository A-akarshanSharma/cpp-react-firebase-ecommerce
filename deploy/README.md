# Private preview deployment

This package targets **one Ubuntu 24.04 x86-64 server**, Docker Engine with Compose v2, a DNS hostname and a separate demo Firebase project. It uses Caddy 2.11.4 for HTTPS, serves the React build and proxies `/api` to the C++ backend. The backend is always running for order expiry. Only ports 80/443 are published; the backend runs as UID 10001 without Linux capabilities. Uploaded images and HTTPS state use persistent Docker volumes.

This is a restricted demo, not an acceptance of real orders. The frontend displays a demo notice. No payments are collected. The database is still writable: use sample records and tester accounts, not a live shop project.

## Information to supply

- Server public IP and SSH access; choose a server near the Firestore region. A small 2-vCPU/4-GB machine is a starting point for building/testing, not a measured customer capacity guarantee.
- A hostname, e.g. `preview.your-domain.com`, with an A record pointing directly to the server. Configure AAAA only if IPv6 actually works. Do not put a CDN/reverse proxy in front of this package: its access rules use the actual connecting IP.
- Your email for certificate notices, and each tester's public IP or narrow CIDR. Mobile/VPN IP changes require updating the allowlist. Never open access with `0.0.0.0/0` or `::/0`.
- Demo Firebase web configuration and that project's service-account file. Put the JSON file on the server; never paste it into chat or commit it.

## Prepare the release

Commit the required source, frontend, tests, `deploy/`, `.dockerignore`, workflow and index files. Many of these may currently be untracked; inspect `git status` and the staged diff. Do not blindly stage credentials. The Docker build context is an allowlist that excludes `.env`, credentials, build outputs and uploads.

The `Store verification` workflow now builds both preview images and checks routing/runtime startup in addition to the application tests. Require its `required-checks` job and wait for a hosted pass before relying on the images. Local Docker was unavailable when this package was prepared, so an actual image build remains required.

## Configure the server

Install Docker Engine and Compose using the [official Ubuntu instructions](https://docs.docker.com/engine/install/ubuntu/). Open TCP 80/443 for HTTPS issuance/traffic; restrict SSH appropriately. Do not open 8080. From the repository root:

```sh
cp deploy/.env.example deploy/.env
chmod 600 deploy/.env
mkdir -p deploy/secrets
chmod 700 deploy/secrets
```

Fill `deploy/.env` with the real preview values. Copy the demo project's service account to `deploy/secrets/service-account.json`, then make it readable by the backend's UID only:

```sh
sudo chown 10001:10001 deploy/secrets/service-account.json
sudo chmod 400 deploy/secrets/service-account.json
sudo python3 deploy/check-preview.py
```

The preflight checks required fields, tester address ranges and matching Firebase project IDs without printing secrets. It does not inspect your cloud resources. Compose local secrets use a read-only bind mount, so the host UID/file permissions above matter.

## Firebase prerequisites

1. Enable email/password sign-in in the demo project; review its authorized domains and add the preview hostname.
2. Deploy the three indexes in [firestore.indexes.json](../firestore.indexes.json) and wait until ready; see [pagination activation](../docs/backend-pagination.md). Keep the automatic indexes used by expiry/history. Preserve unrelated existing indexes.
3. Review Firestore rules: browser clients should not directly read/write the store collections. This application uses the server/service account for commerce data. Service-account IAM must permit the required Firestore and authentication account-lookup operations; do not grant public database access to fix errors.
4. Register/login a tester to provision a customer profile, then bootstrap one intended test administrator in the demo database. Confirm the UID belongs to the intended Firebase account. Future roles can be managed through the admin screen.
5. Add sample products and at least one active shipping/pickup method. Checkout intentionally closes when no active methods exist.

## Build and start

```sh
sudo docker compose --env-file deploy/.env -f deploy/compose.yaml config --quiet
sudo docker compose --env-file deploy/.env -f deploy/compose.yaml build --pull
sudo docker compose --env-file deploy/.env -f deploy/compose.yaml up -d
sudo docker compose --env-file deploy/.env -f deploy/compose.yaml ps
sudo docker compose --env-file deploy/.env -f deploy/compose.yaml logs --tail=100
```

The first build compiles pinned Drogon/jwt-cpp versions and runs backend unit tests. It installs `libpng-dev` inside the build image, removing the old reliance on temporary local headers. Frontend Firebase configuration is compiled into the public bundle; changing it requires rebuilding `web`.

Caddy obtains HTTPS certificates automatically when DNS and ports are correct. Certificate validation is handled by Caddy; the tester restriction applies to application requests. The backend health check is process liveness, not proof that Firebase/indexes work. Restart policies recover exited containers but do not automatically restart an unhealthy still-running process.

The private Docker subnet is `172.30.90.0/24`. If it conflicts with your server's existing networks, change the subnet, both service addresses and `TRUSTED_PROXY_IP` together before startup. Only Caddy at `172.30.90.2` may supply the overwritten `X-Real-IP` header. Direct callers cannot spoof that header to bypass limits; ordinary local development leaves proxy trust disabled.

## Check the deployed preview

From an allowed tester IP:

- Visit `/`, `/shop` and a product; refresh `/checkout` and `/admin/shipping` directly.
- Confirm the demo notice and HTTPS without certificate warnings.
- Register/login; check a customer cannot access admin functions.
- Upload an image, create a test order, inspect order history, cancel it and verify stock returns.
- Check notifications, inventory and audit pages load (missing indexes will cause errors).
- Confirm a separate uninvited network receives 403 for both `/` and `/api/health`.
- Restart the services and confirm images remain available. Test the four-hour expiry separately or inspect the existing local expiry tests; do not shorten the real setting just for a demo.

Keep `/api` responses uncached. There is no extra proxy cache; the application's public catalog cache remains in charge.

## Updates and stopping

Rebuild and run `up -d` using the commands above. To apply allowlist changes, run `up -d --force-recreate web`. Record the last working commit/image before updating; roll back by rebuilding the previous release. Coordinate frontend/backend releases because pagination changed API contracts.

`docker compose ... down` stops the preview and retains volumes. **Do not use `down -v`, volume prune or remove `uploads` if you want to keep product images.** Firebase does not store these local image files. No extra backup service is introduced by this package.

## Verification scope

Local checks cover the application build/tests, environment-only startup, proxy trust/spoof rejection, actual Caddy access restrictions, bearer-token forwarding, media routing and SPA fallback. Docker image/runtime execution and public DNS/TLS remain server/CI checks until a Docker-capable machine and hostname are provided. No hosting was purchased, cloud data modified, deployment published or capacity/load test run.


Verified locally on September 25, 2026: fresh native build, all six CTest suites, six existing HTTP scenarios, both environment/proxy HTTP scenarios, Caddy 2.11.4 routing and private-access tests, Compose 2.40.3 configuration validation, preflight validation cases, frontend build/unit/format checks and three focused browser journeys. The Caddy binary was checked against its published SHA-512 checksum. Docker images and hosted CI have not been executed here; those remain required checks on a Docker-capable machine. The browser/emulator suites were not rerun in full, and no user-capacity test was run.
