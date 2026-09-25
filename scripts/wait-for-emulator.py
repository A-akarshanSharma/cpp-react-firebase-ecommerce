"""Wait only for the disposable loopback Firestore emulator used by CI."""
import socket
import time

until = time.monotonic() + 60
while True:
    try:
        with socket.create_connection(('127.0.0.1', 8189), timeout=1):
            break
    except OSError:
        if time.monotonic() >= until:
            raise RuntimeError('Firestore emulator did not start on 127.0.0.1:8189')
        time.sleep(.2)
