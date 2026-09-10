# Phase 1: Auth + One Firestore Read/Write

## Step 0 — Firebase Console (no code, do this first)

1. Go to https://console.firebase.google.com → Create Project
2. Enable **Firestore Database** (start in test mode for now — lock down rules later)
3. Enable **Authentication** (turn on Email/Password provider, at minimum)
4. Enable **Storage**
5. Go to Project Settings → Service Accounts → **Generate new private key**
   → this downloads a JSON file. **Never commit this to git.** Save it as `service-account.json` in this project root.
6. Note your **Project ID** (shown in Project Settings → General) — you'll need it.

## Step 1 — Install dependencies (Ubuntu/WSL)

```bash
sudo apt update
sudo apt install -y git gcc g++ cmake libjsoncpp-dev uuid-dev \
    zlib1g-dev libssl-dev postgresql-server-dev-all sqlite3 libsqlite3-dev

# Build Drogon from source (safest way to get a recent version)
git clone https://github.com/drogonframework/drogon
cd drogon
git submodule update --init
mkdir build && cd build
cmake ..
make -j$(nproc)
sudo make install

# jwt-cpp (header-only, needs OpenSSL which you already installed)
git clone https://github.com/Thalhammer/jwt-cpp
sudo cp -r jwt-cpp/include/jwt-cpp /usr/local/include/
sudo cp -r jwt-cpp/include/picojson /usr/local/include/ 2>/dev/null || true

# nlohmann/json (header-only)
sudo apt install -y nlohmann-json3-dev
```

## Step 2 — Configure

```bash
cp .env.example .env
# edit .env: set FIREBASE_PROJECT_ID to your project id
# make sure service-account.json is in this folder
```

## Step 3 — Build & run

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
./phase1_server
```

Then in another terminal:
```bash
curl http://localhost:8080/test-write
curl http://localhost:8080/test-read
```

If `test-write` returns success and `test-read` returns the doc back, **Phase 1 checkpoint is done.**

## What to send me if it breaks
- The exact `cmake` or `make` error output (dependency/build issues are common and fixable)
- The exact JSON response from `/test-write` (Firestore's error messages are usually specific — e.g. permission denied, invalid token, malformed request)
- Confirm: does `service-account.json` actually exist in the project root and did you set `FIREBASE_PROJECT_ID` correctly in `.env`?
