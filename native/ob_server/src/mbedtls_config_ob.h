// SPDX-License-Identifier: AGPL-3.0-or-later
// Additions to mbedTLS 2.28's default configuration for the PS2's 2003-era TLS clients (the game's login browser):
// SSL 3.0 alongside TLS 1.0, and the SSLv2-format first greeting old clients send. The fan server's setup builds
// OpenSSL 1.0.2 with SSL 3 enabled for the same clients (research/2026-10-27-lobby-protocol.md; plan P4 result).
// 3DES suites need nothing here: MBEDTLS_REMOVE_3DES_CIPHERSUITES only drops them from the default list, and ob_server
// sets its list explicitly (report entry: "mbedTLS 2.28 ... a suite set explicitly still negotiates").
#define MBEDTLS_SSL_PROTO_SSL3
#define MBEDTLS_SSL_SRV_SUPPORT_SSLV2_CLIENT_HELLO
