"""Local synthetic startup/proxy checks; never read the project's credentials."""
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

binary = str(pathlib.Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="store-deployment-") as folder:
    root = pathlib.Path(folder)
    secret = root / "synthetic.json"
    secret.write_text(json.dumps({"client_email": "test@example.invalid", "private_key": "synthetic"}))
    for trusted in ["127.0.0.1", "192.0.2.254"]:
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        env = {**os.environ, "APP_ENV_FILE": "/dev/null", "FIREBASE_PROJECT_ID": "demo-preview-test",
               "SERVICE_ACCOUNT_PATH": str(secret), "SERVER_HOST": "127.0.0.1", "SERVER_PORT": str(port),
               "ALLOWED_ORIGIN": "https://preview.example.test", "TRUSTED_PROXY_IP": trusted,
               "UPLOAD_DIRECTORY": str(root / "uploads")}
        server = subprocess.Popen([binary], cwd=root, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        def status(ip=None, extra=None):
            headers = {**(extra or {})}
            if ip is not None:
                headers["X-Real-IP"] = ip
            req = urllib.request.Request(f"http://127.0.0.1:{port}/me", headers=headers)
            try:
                with urllib.request.urlopen(req, timeout=5) as result:
                    return result.status
            except urllib.error.HTTPError as error:
                return error.code
        try:
            deadline = time.monotonic() + 10
            while True:
                if server.poll() is not None:
                    raise RuntimeError("Environment-configured server exited")
                try:
                    with socket.create_connection(("127.0.0.1", port), timeout=.1):
                        break
                except OSError:
                    if time.monotonic() > deadline:
                        raise RuntimeError("Server startup timed out")
                    time.sleep(.05)
            if trusted == "127.0.0.1":
                assert status() == 400
                assert status("198.51.100.1, 192.0.2.1") == 400
                for _ in range(130):
                    if status("198.51.100.1") == 429:
                        break
                else:
                    raise AssertionError("Trusted client was not limited")
                assert status("198.51.100.2") == 401, "Separate customers share one limit"
                assert status("198.51.100.1", {"X-Forwarded-For": "203.0.113.9"}) == 429
            else:
                for i in range(130):
                    if status(f"198.51.100.{i % 250 + 1}") == 429:
                        break
                else:
                    raise AssertionError("Untrusted X-Real-IP bypasses rate limits")
            print("PASS deployment: environment startup and", "trusted proxy isolation" if trusted == "127.0.0.1" else "untrusted spoof rejection")
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
