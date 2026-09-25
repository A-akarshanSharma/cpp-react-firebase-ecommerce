"""Exercise the real router with synthetic configuration; never load project secrets."""
import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

binary = pathlib.Path(sys.argv[1]).resolve()
fixture = pathlib.Path(__file__).resolve().parents[2] / "frontend/tests/fixtures/product.png"
with tempfile.TemporaryDirectory(prefix="studio-http-smoke-") as temporary:
    root = pathlib.Path(temporary)
    (root / "run").mkdir()
    (root / "images").mkdir()
    name = "a" * 64 + ".png"
    (root / "images" / name).write_bytes(fixture.read_bytes())
    # Deliberately unusable credentials: protected requests below have no token,
    # and public media serving performs no auth-provider or Firestore requests.
    (root / "synthetic.json").write_text(json.dumps({
        "client_email": "test@example.invalid", "private_key": "not-a-signing-key"
    }))
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    (root / ".env").write_text(
        f"FIREBASE_PROJECT_ID=demo-http-smoke\nSERVER_HOST=127.0.0.1\nSERVER_PORT={port}\n"
        "SERVICE_ACCOUNT_PATH=../synthetic.json\nUPLOAD_DIRECTORY=../images\n"
        "ALLOWED_ORIGIN=http://localhost:5173\n"
    )
    # Ignore any deployment configuration inherited from the invoking shell.
    isolated_env = {k: v for k, v in os.environ.items() if k not in {
        "APP_ENV_FILE", "FIREBASE_PROJECT_ID", "SERVICE_ACCOUNT_PATH", "SERVER_HOST", "SERVER_PORT",
        "UPLOAD_DIRECTORY", "ALLOWED_ORIGIN", "TRUSTED_PROXY_IP"}}
    server = subprocess.Popen([str(binary)], cwd=root / "run", env=isolated_env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = f"http://127.0.0.1:{port}"
    def request(path, method="GET", body=None, headers=None):
        req = urllib.request.Request(base + path, data=body, method=method, headers={
            "Origin": "http://localhost:5173", **(headers or {})
        })
        try:
            with urllib.request.urlopen(req, timeout=5) as response:
                return response.status, response.headers, response.read()
        except urllib.error.HTTPError as error:
            return error.code, error.headers, error.read()
    try:
        deadline = time.monotonic() + 10
        while True:
            if server.poll() is not None:
                raise RuntimeError("Isolated server exited before startup")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=.1):
                    break
            except OSError:
                if time.monotonic() > deadline:
                    raise RuntimeError("Isolated server startup timed out")
                time.sleep(.05)
        protected = [
            ("GET", "/cart"), ("POST", "/orders"), ("GET", "/orders/order-1"),
            ("POST", "/orders/order-1/cancel"), ("PUT", "/admin/orders/order-1/status"),
            ("GET", "/admin/inventory"), ("GET", "/admin/shipping"),
            ("PUT", "/admin/shipping"), ("PUT", "/admin/orders/order-1/shipment"),
            ("GET", "/notifications"), ("POST", "/notifications/n/read"),
            ("GET", "/admin/audit-logs"), ("POST", "/admin/products/p/variants"),
            ("POST", "/admin/uploads"), ("PUT", "/admin/users/u/role"),
            ("POST", "/admin/products/p/stock"),
        ]
        for method, path in protected:
            status, headers, body = request(path, method, b"{}" if method != "GET" else None)
            assert status == 401, (method, path, status)
            assert json.loads(body)["code"] == "UNAUTHENTICATED", path
            assert headers["Access-Control-Allow-Origin"] == "http://localhost:5173", path
        print(f"PASS HTTP: {len(protected)} protected routes reject unauthenticated requests")
        status, headers, body = request("/media/" + name)
        assert status == 200 and body == fixture.read_bytes()
        assert headers.get_content_type() == "image/png"
        assert headers["X-Content-Type-Options"] == "nosniff"
        for path in ["/media/../.env", "/media/not-a-hash.png", "/media/" + "b" * 64 + ".png"]:
            assert request(path)[0] == 404, path
        print("PASS HTTP: PNG serving and invalid/missing file paths")
        status, headers, _ = request("/admin/uploads", "OPTIONS", headers={
            "Origin": "http://localhost:5173", "Access-Control-Request-Method": "POST",
            "Access-Control-Request-Headers": "Authorization,Content-Type"
        })
        assert status == 200 and headers["Access-Control-Allow-Origin"] == "http://localhost:5173"
        assert "Idempotency-Key" in headers["Access-Control-Allow-Headers"]
        print("PASS HTTP: configured CORS preflight retains checkout retry header")
        status, headers, _ = request("/route-that-does-not-exist")
        assert status == 404, status
        assert headers["Access-Control-Allow-Origin"] == "http://localhost:5173", dict(headers)
        print("PASS HTTP: router 404 responses retain configured CORS headers")
        for path in ["/test-write", "/test-read"]:
            for method in ["GET", "HEAD", "POST", "PUT", "DELETE"]:
                status, headers, _ = request(path, method)
                assert status == 404, (method, path, status)
                assert headers["Access-Control-Allow-Origin"] == "http://localhost:5173", path
        print("PASS HTTP: removed debug endpoints return 404 without authentication")
        assert request("/health")[0] == 200
        blocked = False
        for i in range(150):
            status, headers, body = request("/me", headers={"X-Forwarded-For": f"192.0.2.{i % 250}"})
            if status == 429:
                assert json.loads(body)["code"] == "RATE_LIMITED"
                assert int(headers["Retry-After"]) > 0
                assert headers["Access-Control-Allow-Origin"] == "http://localhost:5173"
                blocked = True
                break
        assert blocked, "IP rate limit missing or bypassed by forwarded headers"
        print("PASS HTTP: IP limits, Retry-After, CORS and spoofed forwarded-header rejection")
        print("6 compiled-server HTTP smoke scenarios passed")
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
