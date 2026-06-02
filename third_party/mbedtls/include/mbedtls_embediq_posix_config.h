/*
 * mbedtls_embediq_posix_config.h — EmbedIQ POSIX mbedTLS configuration
 *
 * Enables the minimal feature set required for TLS 1.2 client operation
 * on POSIX/Linux: certificate verification, ECDHE + RSA key exchange,
 * AES-GCM cipher, SHA-256 hash, /dev/urandom entropy.
 *
 * D-LIB-3: this file lives in third_party/mbedtls/include/ alongside
 * the vendored source. It is the ONLY EmbedIQ-authored file in this
 * directory — do not add any other files here.
 *
 * Phase 3 note: FreeRTOS/ESP32 requires a separate config with
 * hardware entropy and constrained memory settings.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MBEDTLS_EMBEDIQ_POSIX_CONFIG_H
#define MBEDTLS_EMBEDIQ_POSIX_CONFIG_H

/* ---------------------------------------------------------------------------
 * Platform: POSIX (Linux / macOS)
 * ------------------------------------------------------------------------- */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_HAVE_TIME
#define MBEDTLS_HAVE_TIME_DATE

/* ---------------------------------------------------------------------------
 * Entropy: /dev/urandom (standard POSIX path — no hardware RNG needed)
 * ------------------------------------------------------------------------- */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
/* MBEDTLS_NO_PLATFORM_ENTROPY is intentionally NOT defined — use /dev/urandom */

/* ---------------------------------------------------------------------------
 * Network: BSD sockets (POSIX)
 * ------------------------------------------------------------------------- */
#define MBEDTLS_NET_C

/* ---------------------------------------------------------------------------
 * TLS protocol: client only (no server needed in EmbedIQ Phase 2)
 * ------------------------------------------------------------------------- */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2

/* ---------------------------------------------------------------------------
 * Certificate handling: X.509 parse + verify
 * ------------------------------------------------------------------------- */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C

/* ---------------------------------------------------------------------------
 * Key handling: RSA + EC private key parse (for mutual TLS)
 * ------------------------------------------------------------------------- */
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21

/* ---------------------------------------------------------------------------
 * Key exchange: ECDHE (preferred) + RSA (fallback)
 * ------------------------------------------------------------------------- */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED

/* ---------------------------------------------------------------------------
 * Cipher suites: AES-128-GCM and AES-256-GCM
 * ------------------------------------------------------------------------- */
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_PADDING_PKCS7

/* ---------------------------------------------------------------------------
 * Hash / MAC: SHA-256 (required for TLS 1.2) + SHA-512 (for cert chains)
 * ------------------------------------------------------------------------- */
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_MD_C

/* ---------------------------------------------------------------------------
 * Bignum: required for RSA and ECC
 * ------------------------------------------------------------------------- */
#define MBEDTLS_BIGNUM_C

/* ---------------------------------------------------------------------------
 * OID: required for certificate parsing
 * ------------------------------------------------------------------------- */
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C

/* ---------------------------------------------------------------------------
 * Error strings (useful for debugging — can be disabled in release builds)
 * ------------------------------------------------------------------------- */
#define MBEDTLS_ERROR_C

/* ---------------------------------------------------------------------------
 * Debugging (disabled in EmbedIQ builds — use Observatory instead)
 * ------------------------------------------------------------------------- */
/* MBEDTLS_DEBUG_C intentionally NOT defined */

/* ---------------------------------------------------------------------------
 * Platform abstraction
 * ------------------------------------------------------------------------- */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY

#endif /* MBEDTLS_EMBEDIQ_POSIX_CONFIG_H */
