# Full-Stack E-Commerce Platform

A full-stack e-commerce web application built with **C++**, **Drogon**, **React**, and **Firebase**.

> **Status:** Working storefront/backend; preparing a private demo deployment.

## Start here: private preview

Follow [deploy/README.md](deploy/README.md) for the single-server Docker Compose package: HTTPS, restricted tester access, persistent images, environment configuration and verification. The preview uses sample data and collects no payments. Hosting/domain/Firebase preview configuration must be supplied before publishing.

## Tech Stack

- **Frontend:** React, Vite, JavaScript, HTML, CSS
- **Backend:** C++, Drogon, REST APIs, JWT, CMake
- **Cloud / Database:** Firebase Authentication, Firestore; product images currently use local persistent disk
- **Testing / Tools:** Git, GitHub, CTest, Python HTTP smoke tests, Ubuntu/WSL2

## Current Work

- C++ REST backend using Drogon
- Firebase project and Firestore integration
- Firebase Authentication setup
- JWT and environment-based configuration
- Secure service-account handling
- CMake build workflow
- Backend tests and HTTP smoke tests
- React/Vite storefront, customer checkout and admin screens

## Implemented Features

- Product listing and product details
- User authentication
- Shopping cart
- Checkout and order history
- Inventory management
- Admin dashboard
- Product and order management

## Project Structure

```text
frontend/          React/Vite frontend
src/               C++ backend source
tests/backend/     Backend tests
docs/              Setup and implementation notes
CMakeLists.txt      Backend build configuration
.env.example        Environment template
```

## Backend Setup

```bash
# Install native build prerequisites first (Ubuntu):
sudo apt-get install -y cmake g++ libjsoncpp-dev uuid-dev zlib1g-dev libssl-dev libpng-dev nlohmann-json3-dev
# Install Drogon v1.9.13 and jwt-cpp v0.7.1 as shown in deploy/backend.Dockerfile.
cp .env.example .env
mkdir -p build && cd build
cmake ..
make -j$(nproc)
./phase1_server
```

Set `FIREBASE_PROJECT_ID` in `.env` and keep `service-account.json` in the project root.

**Never commit:**

```text
service-account.json
.env
frontend/.env
```

## Frontend Setup

```bash
# Use Node 24 (frontend/.nvmrc).
cd frontend
npm ci --ignore-scripts
cp .env.example .env
npm run dev
```

Add the Firebase web configuration values to `frontend/.env`.

## Testing

```bash
ctest --test-dir build --output-on-failure
python3 tests/backend/http_smoke.py build/phase1_server
```

The HTTP smoke tests use synthetic credentials and do not access live Firestore data.

## Architecture

```text
React Client
     |
     v
  REST API
     |
     v
Drogon Backend
     |
     v
Firebase / Firestore
```

The frontend communicates with the C++ backend through REST APIs, while server-side Firebase credentials remain private.

## Security

- Service-account credentials excluded from Git
- Environment files excluded from Git
- Public test Firestore endpoints removed
- Backend-controlled Firestore access
- Token-based authentication architecture

## Status

Product, cart, checkout, inventory and admin workflows are implemented. Payments are deferred. Public launch requires additional operational/customer-policy work; the deployment package targets a private preview.

**Author:** Aakarshan Sharma
