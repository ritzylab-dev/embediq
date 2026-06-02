#!/usr/bin/env bash
# gen_tls_test_certs.sh — generate self-signed TLS test certificates
#
# Generates: CA, server cert (signed by CA), client cert (signed by CA).
# Output: /tmp/embediq_tls_test/{ca,server,client}.{crt,key}
# Safe to re-run — skips generation if files already exist and are valid.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail
OUTDIR="/tmp/embediq_tls_test"
mkdir -p "$OUTDIR"

# Skip if all certs already exist and are not expired
if [ -f "$OUTDIR/ca.crt" ] && \
   [ -f "$OUTDIR/server.crt" ] && \
   [ -f "$OUTDIR/client.crt" ]; then
    if openssl x509 -checkend 86400 -noout -in "$OUTDIR/ca.crt" 2>/dev/null; then
        echo "TLS test certs already valid — skipping generation."
        exit 0
    fi
fi

echo "Generating TLS test certificates in $OUTDIR ..."

# CA key + self-signed cert (10-year validity for test stability)
openssl genrsa -out "$OUTDIR/ca.key" 2048 2>/dev/null
openssl req -new -x509 -days 3650 -key "$OUTDIR/ca.key" \
    -out "$OUTDIR/ca.crt" \
    -subj "/CN=EmbedIQ-Test-CA" 2>/dev/null

# Server key + cert signed by CA
openssl genrsa -out "$OUTDIR/server.key" 2048 2>/dev/null
openssl req -new -key "$OUTDIR/server.key" \
    -out "$OUTDIR/server.csr" \
    -subj "/CN=localhost" 2>/dev/null
openssl x509 -req -days 3650 \
    -in "$OUTDIR/server.csr" \
    -CA "$OUTDIR/ca.crt" \
    -CAkey "$OUTDIR/ca.key" \
    -CAcreateserial \
    -out "$OUTDIR/server.crt" 2>/dev/null

# Client key + cert signed by CA (for mutual TLS tests)
openssl genrsa -out "$OUTDIR/client.key" 2048 2>/dev/null
openssl req -new -key "$OUTDIR/client.key" \
    -out "$OUTDIR/client.csr" \
    -subj "/CN=embediq-test-client" 2>/dev/null
openssl x509 -req -days 3650 \
    -in "$OUTDIR/client.csr" \
    -CA "$OUTDIR/ca.crt" \
    -CAkey "$OUTDIR/ca.key" \
    -CAcreateserial \
    -out "$OUTDIR/client.crt" 2>/dev/null

echo "Done. Certs written to $OUTDIR/"
