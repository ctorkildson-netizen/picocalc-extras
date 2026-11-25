/* mbedtls_config.h -- just what libssh2 needs from Mbed TLS 3.6 on the
 * PicoCalc (RP2350): key exchange (ECDH P-256/384/521, DH groups), host keys
 * and user keys (RSA, ECDSA), AES-CTR/GCM ciphers, SHA-1/2 MACs; random
 * numbers from the RP2350's TRNG (the SDK's pico_mbedtls supplies
 * mbedtls_hardware_poll()), SHA-256 in hardware. No TLS, no X.509, no files. */
#ifndef PICOCALC_MBEDTLS_CONFIG_H
#define PICOCALC_MBEDTLS_CONFIG_H

/* the platform */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_SHA256_ALT

/* numbers and randomness */
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C
#define MBEDTLS_HMAC_DRBG_C		/* deterministic ECDSA */
#define MBEDTLS_ECDSA_DETERMINISTIC

/* hashes and MACs */
#define MBEDTLS_MD_C
#define MBEDTLS_MD5_C			/* host key fingerprints (MD5 kind) */
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA224_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C
#define MBEDTLS_RIPEMD160_C

/* ciphers */
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_GCM_C

/* public keys */
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_PKCS1_V21
#define MBEDTLS_GENPRIME
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_ECP_DP_SECP521R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C		/* user keys, later (PEM) */
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_OID_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_CIPHER_PADDING_PKCS7	/* encrypted PEM keys */
#define MBEDTLS_PKCS5_C
#define MBEDTLS_PKCS12_C
#define MBEDTLS_DES_C			/* (old encrypted PEM keys) */
#define MBEDTLS_FS_IO			/* (libssh2 calls it for key files; with no file system behind stdio it just fails, and keys come from memory) */

#endif
