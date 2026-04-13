/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
#ifndef CRYPT_H
#define CRYPT_H

#include <stdint.h>
#include <stddef.h>

/* ── AES-256-GCM ── */

int aes256gcm_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                      const uint8_t *pt, size_t pt_len,
                      uint8_t *ct, uint8_t tag[16]);

int aes256gcm_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                      const uint8_t *ct, size_t ct_len,
                      const uint8_t tag[16], uint8_t *pt);

/* ── SHA-256 ── */

typedef struct {
    uint32_t state[8];
    uint64_t bitcount;
    uint8_t  buffer[64];
    uint32_t buflen;
} Sha256Ctx;

void sha256_init(Sha256Ctx *ctx);
void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len);
void sha256_final(Sha256Ctx *ctx, uint8_t out[32]);
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);

/* ── SHA-1 (for TOTP only — not for security hashing) ── */

typedef struct {
    uint32_t state[5];
    uint64_t bitcount;
    uint8_t  buffer[64];
    uint32_t buflen;
} Sha1Ctx;

void sha1_init(Sha1Ctx *ctx);
void sha1_update(Sha1Ctx *ctx, const uint8_t *data, size_t len);
void sha1_final(Sha1Ctx *ctx, uint8_t out[20]);
void sha1(const uint8_t *data, size_t len, uint8_t out[20]);

/* ── HMAC-SHA-1 (for TOTP — RFC 6238) ── */

void sha1_hmac(const uint8_t *key, size_t klen,
               const uint8_t *data, size_t dlen,
               uint8_t out[20]);

/* ── HMAC-SHA-256 ── */

void sha256_hmac(const uint8_t *key, size_t klen,
                 const uint8_t *data, size_t dlen,
                 uint8_t out[32]);

/* ── PBKDF2-SHA-256 ── */

void pbkdf2_sha256(const uint8_t *password, size_t plen,
                   const uint8_t *salt, size_t slen,
                   uint32_t iterations,
                   uint8_t *out, size_t out_len);

/* ── SHA-512 (internal for Ed25519, also public) ── */

typedef struct {
    uint64_t state[8];
    uint64_t bitcount[2];
    uint8_t  buffer[128];
    uint32_t buflen;
} Sha512Ctx;

void sha512_init(Sha512Ctx *ctx);
void sha512_update(Sha512Ctx *ctx, const uint8_t *data, size_t len);
void sha512_final(Sha512Ctx *ctx, uint8_t out[64]);
void sha512(const uint8_t *data, size_t len, uint8_t out[64]);

/* ── Ed25519 — RFC 8032 ── */

void ed25519_keypair(uint8_t pk[32], uint8_t sk[64]);
void ed25519_sign(const uint8_t sk[64], const uint8_t *msg, size_t len, uint8_t sig[64]);
int  ed25519_verify(const uint8_t pk[32], const uint8_t *msg, size_t len, const uint8_t sig[64]);

/* ── Utilities ── */

void sha256_to_hex(const uint8_t digest[32], char *hex);
int  sha256_equal(const uint8_t a[32], const uint8_t b[32]);

/* ── Platform ── */

void     crypt_fill_random(uint8_t *buf, size_t len);
uint64_t crypt_time_ms(void);

#endif /* CRYPT_H */
