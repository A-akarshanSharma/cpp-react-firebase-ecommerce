"""Exercise the real Caddy config locally, with a synthetic upstream and static app."""
import http.server
import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

binary = str(pathlib.Path(sys.argv[1]).resolve())
repository = pathlib.Path(__file__).resolve().parents[2]
class Upstream(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        self.wfile.write(json.dumps({"path": self.path, "realIp": self.headers.get("X-Real-IP"),
                                    "authorization": self.headers.get("Authorization")}).encode())
    def log_message(self, *args):
        pass

upstream = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Upstream)
thread = threading.Thread(target=upstream.serve_forever, daemon=True)
thread.start()
try:
    with tempfile.TemporaryDirectory(prefix="preview-proxy-") as folder:
        root = pathlib.Path(folder)
        (root / "index.html").write_text("<html>Preview app shell</html>")
        (root / "asset.js").write_text("console.log('preview')")
        config = (repository / "deploy/Caddyfile").read_text().replace("/srv", str(root)).replace(
            "backend:8080", f"127.0.0.1:{upstream.server_port}")
        # Validation/test changes only the local address, document root and upstream.
        config = config.replace("email {$ACME_EMAIL}", "email {$ACME_EMAIL}\n    admin off\n    auto_https off")
        (root / "Caddyfile").write_text(config)
        for allowed in ["127.0.0.1/32", "192.0.2.42/32"]:
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                port = sock.getsockname()[1]
            env = {**os.environ, "SITE_HOST": f"http://127.0.0.1:{port}", "ACME_EMAIL": "test@example.test",
                   "PREVIEW_ALLOWED_IPS": allowed, "XDG_DATA_HOME": str(root / "data"),
                   "XDG_CONFIG_HOME": str(root / "config")}
            with (root / "caddy.log").open("w") as log:
                server = subprocess.Popen([binary, "run", "--config", str(root / "Caddyfile"), "--adapter", "caddyfile"],
                                          env=env, stdout=log, stderr=log)
                def request(path, headers=None):
                    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", headers=headers or {})
                    try:
                        with urllib.request.urlopen(req, timeout=5) as response:
                            return response.status, response.headers, response.read()
                    except urllib.error.HTTPError as error:
                        return error.code, error.headers, error.read()
                try:
                    deadline = time.monotonic() + 10
                    while True:
                        if server.poll() is not None:
                            raise RuntimeError((root / "caddy.log").read_text())
                        try:
                            with socket.create_connection(("127.0.0.1", port), timeout=.1):
                                break
                        except OSError:
                            if time.monotonic() > deadline:
                                raise RuntimeError("Caddy startup timed out")
                            time.sleep(.05)
                    if allowed == "127.0.0.1/32":
                        for path in ["/", "/checkout", "/orders/order-123", "/admin/shipping"]:
                            status, headers, body = request(path)
                            assert status == 200 and b"Preview app shell" in body
                            assert headers["Cache-Control"] == "no-cache"
                            assert "noindex" in headers["X-Robots-Tag"]
                        assert request("/asset.js")[2] == b"console.log('preview')"
                        status, _, body = request("/api/me?example=1", {
                            "Authorization": "Bearer synthetic", "X-Real-IP": "198.51.100.9",
                            "X-Forwarded-For": "198.51.100.10"})
                        assert status == 200
                        assert json.loads(body) == {"path": "/me?example=1", "realIp": "127.0.0.1", "authorization": "Bearer synthetic"}
                        assert json.loads(request("/api/media/image.png")[2])["path"] == "/media/image.png"
                        print("PASS preview: SPA refresh, assets, API/media routing, bearer tokens and overwritten client identity")
                    else:
                        for path in ["/", "/checkout", "/asset.js", "/api/me", "/api/media/image.png"]:
                            assert request(path, {"X-Real-IP": "192.0.2.42", "X-Forwarded-For": "192.0.2.42"})[0] == 403
                        print("PASS preview: both site and API reject uninvited peers and spoofed headers")
                finally:
                    server.terminate()
                    try:
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait()
finally:
    upstream.shutdown()
    upstream.server_close()
