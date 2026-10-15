#!/usr/bin/env python3
# Copyright (c) 2026 aMule Team
# SPDX-License-Identifier: GPL-2.0-or-later
"""Malformed six-byte headers must close the peer, not crash the daemon."""
from pathlib import Path
import socket
import struct
import sys
import tempfile

from AllSearchIntegrationTest import free_ports, stop_daemon
from KnownHistoryIntegrationTest import start
from ServerTrustIntegrationTest import configure


def run(binary):
    with tempfile.TemporaryDirectory(prefix='amule-packet-receive-') as directory:
        root = Path(directory)
        for name in ('Incoming', 'Temp'):
            (root / name).mkdir()
        ports = free_ports(3, udp_indices=(2,), peer_index=1)
        configure(root, ports, filter_lan=False)
        proc, ec = start(binary, root, ports)
        try:
            for length in (0, 0x7ffffff0, 0xffffffff):
                with socket.create_connection(('127.0.0.1', ports[1]), timeout=10) as peer:
                    peer.settimeout(10)
                    # Send only the header: rejection must precede any payload read.
                    peer.sendall(struct.pack('<BIB', 0xe3, length, 0x01))
                    try:
                        assert peer.recv(1) == b'', f'header {length:#x} was not rejected'
                    except ConnectionResetError:
                        pass
                assert proc.poll() is None, f'header {length:#x} killed the daemon'
            # A fresh connection proves the listener still works after all three failures.
            with socket.create_connection(('127.0.0.1', ports[1]), timeout=10):
                pass
        finally:
            ec.sock.close()
            stop_daemon(proc)


if __name__ == '__main__':
    run(str(Path(sys.argv[1]).resolve()))
