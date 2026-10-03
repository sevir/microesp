/* Minimal mbedTLS configuration for the host tests (same algorithms the firmware uses). */
#pragma once
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_HKDF_C
