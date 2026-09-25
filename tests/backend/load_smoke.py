"""Exercise production request dispatch under overload using a loopback-only test harness."""
import concurrent.futures
import pathlib
import socket
import subprocess
import sys
import time
import urllib.request
import urllib.error

with socket.socket() as sock:
    sock.bind(('127.0.0.1', 0))
    port = sock.getsockname()[1]
process = subprocess.Popen([str(pathlib.Path(sys.argv[1]).resolve()), str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
def request(path):
    try:
        with urllib.request.urlopen(f'http://127.0.0.1:{port}{path}', timeout=5) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.read()
try:
    until = time.monotonic() + 10
    while True:
        try:
            request('/health')
            break
        except OSError:
            if time.monotonic() > until: raise
            time.sleep(.02)
    with concurrent.futures.ThreadPoolExecutor(max_workers=24) as executor:
        futures = [executor.submit(request, '/slow') for _ in range(24)]
        until = time.monotonic() + 3
        while request('/health')[1] != b'2':
            if time.monotonic() > until: raise AssertionError('Workers did not saturate')
            time.sleep(.01)
        begin = time.monotonic()
        assert request('/health')[0] == 200
        assert time.monotonic() - begin < .5, 'Slow work blocked the HTTP event loop'
        results = [f.result()[0] for f in futures]
        assert 200 in results and 503 in results, results
        assert all(r in (200, 503) for r in results), results
    assert request('/slow')[0] == 200, 'Server did not recover after overload'
    print('PASS HTTP load: slow work off event loop, bounded overload rejection and recovery')
finally:
    process.terminate()
    try: process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
