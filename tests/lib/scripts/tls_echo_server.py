#!/usr/bin/env python3
"""
tls_echo_server.py — minimal TLS echo server for EmbedIQ TLS tests.

Accepts one TLS connection, echoes every received byte back, closes
after the client disconnects or after IDLE_TIMEOUT_S seconds.

Usage: python3 tls_echo_server.py <port> <server_cert> <server_key> <ca_cert>
       python3 tls_echo_server.py <port> <server_cert> <server_key> <ca_cert> --mutual

@author  Ritesh Anand
@company embediq.com | ritzylab.com
SPDX-License-Identifier: Apache-2.0
"""
import ssl
import socket
import sys

IDLE_TIMEOUT_S = 10


def main():
    args = sys.argv[1:]
    if len(args) < 4:
        print("Usage: tls_echo_server.py <port> <cert> <key> <ca> [--mutual]")
        sys.exit(1)

    port        = int(args[0])
    server_cert = args[1]
    server_key  = args[2]
    ca_cert     = args[3]
    mutual      = "--mutual" in args

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(certfile=server_cert, keyfile=server_key)
    ctx.load_verify_locations(cafile=ca_cert)
    if mutual:
        ctx.verify_mode = ssl.CERT_REQUIRED
    else:
        ctx.verify_mode = ssl.CERT_NONE

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as raw:
        raw.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        raw.settimeout(IDLE_TIMEOUT_S)
        raw.bind(("127.0.0.1", port))
        raw.listen(1)
        try:
            conn, _ = raw.accept()
            with ctx.wrap_socket(conn, server_side=True) as tls:
                tls.settimeout(IDLE_TIMEOUT_S)
                while True:
                    try:
                        data = tls.recv(4096)
                        if not data:
                            break
                        tls.sendall(data)
                    except (ssl.SSLError, socket.timeout):
                        break
        except socket.timeout:
            pass  # no connection — normal exit


if __name__ == "__main__":
    main()
