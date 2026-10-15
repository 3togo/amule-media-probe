#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise server-list failover through real local HTTP downloads and amuled."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import struct
import sys
import tempfile
import threading
import time

from AllSearchIntegrationTest import free_ports, stop_daemon
from DaemonConfig import write_amule_conf
from KnownHistoryIntegrationTest import start

# A minimal, valid server.met containing one public address, no tags.
SERVER_MET = b'\xe0' + struct.pack('<I', 1) + bytes([203, 0, 113, 7]) + struct.pack('<HI', 4661, 0)


def run(binary, root, primary_response, fallback_response, enabled=True):
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            status, body = primary_response if self.path == '/primary' else fallback_response
            self.send_response(status)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    proc = ec = None
    try:
        root.mkdir()
        (root / 'Incoming').mkdir()
        (root / 'Temp').mkdir()
        ports = free_ports(3, udp_indices=(2,), peer_index=1)
        base_url = f'http://127.0.0.1:{server.server_port}'
        write_amule_conf(root, ports[0], Port=ports[1], UDPPort=ports[2], ConnectToED2K=1,
                         Ed2kServersUrl=base_url + '/primary',
                         Ed2kServersFallbackUrl=base_url + '/fallback' if enabled else '')
        proc, ec = start(binary, root, ports)
        expected_success = primary_response == (200, SERVER_MET) or (
            enabled and fallback_response == (200, SERVER_MET))
        expected_requests = ['/primary']
        if enabled and primary_response != (200, SERVER_MET):
            expected_requests.append('/fallback')
        deadline = time.monotonic() + 20
        while True:
            log = (root / 'logfile').read_text(errors='replace')
            done = ('Finished downloading the server list' in log if expected_success else
                    f'Failed to download server.met from {base_url + expected_requests[-1]}' in log)
            if done:
                break
            if proc.poll() is not None or time.monotonic() > deadline:
                raise AssertionError(f'download did not complete: {requests}\n{log}')
            time.sleep(0.05)
        # Allow any erroneous repeated retries to be dispatched before asserting.
        time.sleep(0.3)
        assert requests == expected_requests, requests
        if expected_success:
            data = (root / 'server.met').read_bytes()
            assert int.from_bytes(data[1:5], 'little') == 1, data
    finally:
        if ec:
            ec.sock.close()
        if proc:
            stop_daemon(proc)
        server.shutdown()
        server.server_close()
        worker.join(timeout=5)


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='amule-server-fallback-') as temp:
        root = Path(temp)
        run(binary, root / 'primary-success', (200, SERVER_MET), (200, SERVER_MET))
        run(binary, root / 'http-failure', (503, b'unavailable'), (200, SERVER_MET))
        run(binary, root / 'invalid-primary', (200, b'not a server list'), (200, SERVER_MET))
        run(binary, root / 'both-fail', (503, b'unavailable'), (503, b'unavailable'))
        run(binary, root / 'invalid-backup', (503, b'unavailable'), (200, b'invalid'))
        run(binary, root / 'disabled-backup', (503, b'unavailable'), (200, SERVER_MET), enabled=False)
    print('server-list fallback scenarios passed')


if __name__ == '__main__':
    main()
