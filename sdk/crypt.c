/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */
/*
 * crypt.c — AES-256-GCM + SHA-256 + HMAC + PBKDF2 + Platform
 *
 * Pure C. No OpenSSL. No dependencies.
 * AES-256-GCM: NIST FIPS 197 + SP 800-38D.
 * SHA-256: FIPS 180-4. HMAC: RFC 2104. PBKDF2: RFC 2898.
 */

#include "crypt.h"
#include <string.h>

/* ================================================================
 * PLATFORM — Random + Time
 * ================================================================ */

#ifdef _WIN32
#include <windows.h>
/* RtlGenRandom (SystemFunction036) — always available */
BOOLEAN NTAPI SystemFunction036(PVOID, ULONG);

void crypt_fill_random(uint8_t *buf, size_t len) {
    SystemFunction036(buf, (ULONG)len);
}

uint64_t crypt_time_ms(void) {
    return GetTickCount64();
}
#else
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

void crypt_fill_random(uint8_t *buf, size_t len) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) { read(fd, buf, len); close(fd); }
}

uint64_t crypt_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#endif

/* ================================================================
 * SHA-256 — FIPS 180-4
 * ================================================================ */

static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z)  (((x)&(y)) ^ (~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y)) ^ ((x)&(z)) ^ ((y)&(z)))
#define EP0(x) (ROTR(x,2) ^ ROTR(x,13) ^ ROTR(x,22))
#define EP1(x) (ROTR(x,6) ^ ROTR(x,11) ^ ROTR(x,25))
#define SIG0(x) (ROTR(x,7) ^ ROTR(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTR(x,17) ^ ROTR(x,19) ^ ((x) >> 10))

static inline uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) |
           ((uint32_t)p[2]<<8)  | (uint32_t)p[3];
}

static inline void be32_store(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8);  p[3]=(uint8_t)v;
}

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64], a, b, c, d, e, f, g, h, t1, t2;

    for (int i = 0; i < 16; i++) W[i] = be32(block + i * 4);
    for (int i = 16; i < 64; i++)
        W[i] = SIG1(W[i-2]) + W[i-7] + SIG0(W[i-15]) + W[i-16];

    a=state[0]; b=state[1]; c=state[2]; d=state[3];
    e=state[4]; f=state[5]; g=state[6]; h=state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e,f,g) + K256[i] + W[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }

    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

void sha256_init(Sha256Ctx *ctx) {
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85;
    ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c;
    ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
    ctx->bitcount = 0; ctx->buflen = 0;
}

void sha256_update(Sha256Ctx *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buflen++] = data[i];
        if (ctx->buflen == 64) {
            sha256_transform(ctx->state, ctx->buffer);
            ctx->bitcount += 512;
            ctx->buflen = 0;
        }
    }
}

void sha256_final(Sha256Ctx *ctx, uint8_t out[32]) {
    uint64_t total = ctx->bitcount + (uint64_t)ctx->buflen * 8;
    ctx->buffer[ctx->buflen++] = 0x80;
    if (ctx->buflen > 56) {
        while (ctx->buflen < 64) ctx->buffer[ctx->buflen++] = 0;
        sha256_transform(ctx->state, ctx->buffer);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 56) ctx->buffer[ctx->buflen++] = 0;
    for (int i = 7; i >= 0; i--)
        ctx->buffer[56 + (7 - i)] = (uint8_t)(total >> (i * 8));
    sha256_transform(ctx->state, ctx->buffer);
    for (int i = 0; i < 8; i++) be32_store(out + i * 4, ctx->state[i]);
}

void sha256(const uint8_t *data, size_t len, uint8_t out[32]) {
    Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

/* ── HMAC-SHA-256 (RFC 2104) ── */

void sha256_hmac(const uint8_t *key, size_t klen,
                 const uint8_t *data, size_t dlen,
                 uint8_t out[32]) {
    uint8_t k_pad[64], inner[32], key_hash[32];
    Sha256Ctx ctx;

    if (klen > 64) { sha256(key, klen, key_hash); key = key_hash; klen = 32; }

    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < klen; i++) k_pad[i] ^= key[i];
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, data, dlen);
    sha256_final(&ctx, inner);

    memset(k_pad, 0x5c, 64);
    for (size_t i = 0; i < klen; i++) k_pad[i] ^= key[i];
    sha256_init(&ctx);
    sha256_update(&ctx, k_pad, 64);
    sha256_update(&ctx, inner, 32);
    sha256_final(&ctx, out);
}

/* ── SHA-1 — FIPS 180-1 (for TOTP only) ── */

#define ROTL1(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t W[80], a, b, c, d, e, t;
    for (int i = 0; i < 16; i++) W[i] = be32(block + i * 4);
    for (int i = 16; i < 80; i++)
        W[i] = ROTL1(W[i-3] ^ W[i-8] ^ W[i-14] ^ W[i-16], 1);

    a=state[0]; b=state[1]; c=state[2]; d=state[3]; e=state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if      (i < 20) { f = (b&c) | (~b&d);     k = 0x5A827999; }
        else if (i < 40) { f = b^c^d;               k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b&c) | (b&d) | (c&d); k = 0x8F1BBCDC; }
        else              { f = b^c^d;               k = 0xCA62C1D6; }
        t = ROTL1(a, 5) + f + e + k + W[i];
        e=d; d=c; c=ROTL1(b, 30); b=a; a=t;
    }
    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e;
}

void sha1_init(Sha1Ctx *ctx) {
    ctx->state[0]=0x67452301; ctx->state[1]=0xEFCDAB89;
    ctx->state[2]=0x98BADCFE; ctx->state[3]=0x10325476;
    ctx->state[4]=0xC3D2E1F0;
    ctx->bitcount = 0; ctx->buflen = 0;
}

void sha1_update(Sha1Ctx *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buflen++] = data[i];
        if (ctx->buflen == 64) {
            sha1_transform(ctx->state, ctx->buffer);
            ctx->bitcount += 512;
            ctx->buflen = 0;
        }
    }
}

void sha1_final(Sha1Ctx *ctx, uint8_t out[20]) {
    uint64_t total = ctx->bitcount + (uint64_t)ctx->buflen * 8;
    ctx->buffer[ctx->buflen++] = 0x80;
    if (ctx->buflen > 56) {
        while (ctx->buflen < 64) ctx->buffer[ctx->buflen++] = 0;
        sha1_transform(ctx->state, ctx->buffer);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 56) ctx->buffer[ctx->buflen++] = 0;
    for (int i = 7; i >= 0; i--)
        ctx->buffer[56 + (7 - i)] = (uint8_t)(total >> (i * 8));
    sha1_transform(ctx->state, ctx->buffer);
    for (int i = 0; i < 5; i++) be32_store(out + i * 4, ctx->state[i]);
}

void sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
    Sha1Ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, out);
}

/* ── HMAC-SHA-1 (RFC 2104) ── */

void sha1_hmac(const uint8_t *key, size_t klen,
               const uint8_t *data, size_t dlen,
               uint8_t out[20]) {
    uint8_t k_pad[64], inner[20], key_hash[20];
    Sha1Ctx ctx;

    if (klen > 64) { sha1(key, klen, key_hash); key = key_hash; klen = 20; }

    memset(k_pad, 0x36, 64);
    for (size_t i = 0; i < klen; i++) k_pad[i] ^= key[i];
    sha1_init(&ctx);
    sha1_update(&ctx, k_pad, 64);
    sha1_update(&ctx, data, dlen);
    sha1_final(&ctx, inner);

    memset(k_pad, 0x5c, 64);
    for (size_t i = 0; i < klen; i++) k_pad[i] ^= key[i];
    sha1_init(&ctx);
    sha1_update(&ctx, k_pad, 64);
    sha1_update(&ctx, inner, 20);
    sha1_final(&ctx, out);
}

/* ── PBKDF2-HMAC-SHA-256 (RFC 2898) ── */

void pbkdf2_sha256(const uint8_t *password, size_t plen,
                   const uint8_t *salt, size_t slen,
                   uint32_t iterations,
                   uint8_t *out, size_t out_len) {
    uint8_t U[32], T[32], salt_block[132];
    uint32_t block = 1;
    size_t copied = 0;

    if (slen > 128) slen = 128;

    while (copied < out_len) {
        memcpy(salt_block, salt, slen);
        salt_block[slen]   = (uint8_t)(block >> 24);
        salt_block[slen+1] = (uint8_t)(block >> 16);
        salt_block[slen+2] = (uint8_t)(block >> 8);
        salt_block[slen+3] = (uint8_t)(block);

        sha256_hmac(password, plen, salt_block, slen + 4, U);
        memcpy(T, U, 32);

        for (uint32_t i = 1; i < iterations; i++) {
            sha256_hmac(password, plen, U, 32, U);
            for (int j = 0; j < 32; j++) T[j] ^= U[j];
        }

        size_t rem = out_len - copied;
        size_t n = rem < 32 ? rem : 32;
        memcpy(out + copied, T, n);
        copied += n;
        block++;
    }
}

/* ================================================================
 * SHA-512 — FIPS 180-4 (required internally by Ed25519)
 * ================================================================ */

static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
    0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
    0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
    0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
    0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
    0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
    0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
    0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
    0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
    0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
    0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
    0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
    0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
    0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

#define ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define CH64(x,y,z)  (((x)&(y)) ^ (~(x)&(z)))
#define MAJ64(x,y,z) (((x)&(y)) ^ ((x)&(z)) ^ ((y)&(z)))
#define EP064(x) (ROTR64(x,28) ^ ROTR64(x,34) ^ ROTR64(x,39))
#define EP164(x) (ROTR64(x,14) ^ ROTR64(x,18) ^ ROTR64(x,41))
#define SIG064(x) (ROTR64(x,1) ^ ROTR64(x,8) ^ ((x) >> 7))
#define SIG164(x) (ROTR64(x,19) ^ ROTR64(x,61) ^ ((x) >> 6))

static inline uint64_t be64(const uint8_t *p) {
    return ((uint64_t)p[0]<<56) | ((uint64_t)p[1]<<48) |
           ((uint64_t)p[2]<<40) | ((uint64_t)p[3]<<32) |
           ((uint64_t)p[4]<<24) | ((uint64_t)p[5]<<16) |
           ((uint64_t)p[6]<<8)  | (uint64_t)p[7];
}

static inline void be64_store(uint8_t *p, uint64_t v) {
    p[0]=(uint8_t)(v>>56); p[1]=(uint8_t)(v>>48);
    p[2]=(uint8_t)(v>>40); p[3]=(uint8_t)(v>>32);
    p[4]=(uint8_t)(v>>24); p[5]=(uint8_t)(v>>16);
    p[6]=(uint8_t)(v>>8);  p[7]=(uint8_t)v;
}

static void sha512_transform(uint64_t state[8], const uint8_t block[128]) {
    uint64_t W[80], a, b, c, d, e, f, g, h, t1, t2;

    for (int i = 0; i < 16; i++) W[i] = be64(block + i * 8);
    for (int i = 16; i < 80; i++)
        W[i] = SIG164(W[i-2]) + W[i-7] + SIG064(W[i-15]) + W[i-16];

    a=state[0]; b=state[1]; c=state[2]; d=state[3];
    e=state[4]; f=state[5]; g=state[6]; h=state[7];

    for (int i = 0; i < 80; i++) {
        t1 = h + EP164(e) + CH64(e,f,g) + K512[i] + W[i];
        t2 = EP064(a) + MAJ64(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }

    state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d;
    state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
}

void sha512_init(Sha512Ctx *ctx) {
    ctx->state[0]=0x6a09e667f3bcc908ULL; ctx->state[1]=0xbb67ae8584caa73bULL;
    ctx->state[2]=0x3c6ef372fe94f82bULL; ctx->state[3]=0xa54ff53a5f1d36f1ULL;
    ctx->state[4]=0x510e527fade682d1ULL; ctx->state[5]=0x9b05688c2b3e6c1fULL;
    ctx->state[6]=0x1f83d9abfb41bd6bULL; ctx->state[7]=0x5be0cd19137e2179ULL;
    ctx->bitcount[0] = ctx->bitcount[1] = 0;
    ctx->buflen = 0;
}

void sha512_update(Sha512Ctx *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buflen++] = data[i];
        if (ctx->buflen == 128) {
            sha512_transform(ctx->state, ctx->buffer);
            ctx->bitcount[0] += 1024;
            if (ctx->bitcount[0] < 1024) ctx->bitcount[1]++;
            ctx->buflen = 0;
        }
    }
}

void sha512_final(Sha512Ctx *ctx, uint8_t out[64]) {
    uint64_t total_lo = ctx->bitcount[0] + (uint64_t)ctx->buflen * 8;
    uint64_t total_hi = ctx->bitcount[1];
    if (total_lo < ctx->bitcount[0]) total_hi++;

    ctx->buffer[ctx->buflen++] = 0x80;
    if (ctx->buflen > 112) {
        while (ctx->buflen < 128) ctx->buffer[ctx->buflen++] = 0;
        sha512_transform(ctx->state, ctx->buffer);
        ctx->buflen = 0;
    }
    while (ctx->buflen < 112) ctx->buffer[ctx->buflen++] = 0;
    be64_store(ctx->buffer + 112, total_hi);
    be64_store(ctx->buffer + 120, total_lo);
    sha512_transform(ctx->state, ctx->buffer);
    for (int i = 0; i < 8; i++) be64_store(out + i * 8, ctx->state[i]);
}

void sha512(const uint8_t *data, size_t len, uint8_t out[64]) {
    Sha512Ctx ctx;
    sha512_init(&ctx);
    sha512_update(&ctx, data, len);
    sha512_final(&ctx, out);
}

/* ── Utilities ── */

static const char hex_lut[] = "0123456789abcdef";

void sha256_to_hex(const uint8_t digest[32], char *hex) {
    for (int i = 0; i < 32; i++) {
        hex[i*2]   = hex_lut[(digest[i] >> 4) & 0x0f];
        hex[i*2+1] = hex_lut[digest[i] & 0x0f];
    }
    hex[64] = '\0';
}

int sha256_equal(const uint8_t a[32], const uint8_t b[32]) {
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

/* ================================================================
 * AES-256 — FIPS 197
 * ================================================================ */

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const uint8_t rcon[10] = {
    0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

typedef struct { uint32_t rk[60]; } AesKey;

static inline uint32_t get_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
           ((uint32_t)p[2]<<8)|(uint32_t)p[3];
}

static inline void put_u32_be(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}

static uint32_t sub_word(uint32_t w) {
    return ((uint32_t)sbox[(w>>24)&0xff]<<24) |
           ((uint32_t)sbox[(w>>16)&0xff]<<16) |
           ((uint32_t)sbox[(w>>8)&0xff]<<8)   |
           (uint32_t)sbox[w&0xff];
}

static void aes256_expand_key(const uint8_t key[32], AesKey *ek) {
    for (int i = 0; i < 8; i++) ek->rk[i] = get_u32_be(key + 4*i);
    for (int i = 8; i < 60; i++) {
        uint32_t t = ek->rk[i-1];
        if (i % 8 == 0)
            t = sub_word((t<<8)|(t>>24)) ^ ((uint32_t)rcon[i/8-1]<<24);
        else if (i % 8 == 4)
            t = sub_word(t);
        ek->rk[i] = ek->rk[i-8] ^ t;
    }
}

static inline uint8_t xtime(uint8_t x) {
    return (uint8_t)((x<<1) ^ (((x>>7)&1) * 0x1b));
}

static void aes256_encrypt_block(const AesKey *ek,
                                 const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16];
    memcpy(s, in, 16);

    for (int i = 0; i < 4; i++) {
        s[4*i]^=(uint8_t)(ek->rk[i]>>24); s[4*i+1]^=(uint8_t)(ek->rk[i]>>16);
        s[4*i+2]^=(uint8_t)(ek->rk[i]>>8); s[4*i+3]^=(uint8_t)(ek->rk[i]);
    }

    for (int round = 1; round <= 14; round++) {
        uint8_t t[16];
        for (int i = 0; i < 16; i++) t[i] = sbox[s[i]];
        s[0]=t[0]; s[1]=t[5]; s[2]=t[10]; s[3]=t[15];
        s[4]=t[4]; s[5]=t[9]; s[6]=t[14]; s[7]=t[3];
        s[8]=t[8]; s[9]=t[13]; s[10]=t[2]; s[11]=t[7];
        s[12]=t[12]; s[13]=t[1]; s[14]=t[6]; s[15]=t[11];

        if (round < 14) {
            for (int c = 0; c < 4; c++) {
                uint8_t a0=s[4*c], a1=s[4*c+1], a2=s[4*c+2], a3=s[4*c+3];
                uint8_t x0=xtime(a0), x1=xtime(a1), x2=xtime(a2), x3=xtime(a3);
                s[4*c]=x0^x1^a1^a2^a3; s[4*c+1]=a0^x1^x2^a2^a3;
                s[4*c+2]=a0^a1^x2^x3^a3; s[4*c+3]=x0^a0^a1^a2^x3;
            }
        }

        for (int i = 0; i < 4; i++) {
            s[4*i]^=(uint8_t)(ek->rk[round*4+i]>>24);
            s[4*i+1]^=(uint8_t)(ek->rk[round*4+i]>>16);
            s[4*i+2]^=(uint8_t)(ek->rk[round*4+i]>>8);
            s[4*i+3]^=(uint8_t)(ek->rk[round*4+i]);
        }
    }
    memcpy(out, s, 16);
}

/* ================================================================
 * GCM — Galois/Counter Mode (SP 800-38D)
 * ================================================================ */

typedef struct { uint64_t hi, lo; } Block128;

static Block128 b128_from(const uint8_t b[16]) {
    Block128 r;
    r.hi = ((uint64_t)b[0]<<56)|((uint64_t)b[1]<<48)|((uint64_t)b[2]<<40)|
           ((uint64_t)b[3]<<32)|((uint64_t)b[4]<<24)|((uint64_t)b[5]<<16)|
           ((uint64_t)b[6]<<8)|(uint64_t)b[7];
    r.lo = ((uint64_t)b[8]<<56)|((uint64_t)b[9]<<48)|((uint64_t)b[10]<<40)|
           ((uint64_t)b[11]<<32)|((uint64_t)b[12]<<24)|((uint64_t)b[13]<<16)|
           ((uint64_t)b[14]<<8)|(uint64_t)b[15];
    return r;
}

static void b128_to(Block128 b, uint8_t out[16]) {
    for (int i = 0; i < 8; i++) out[i] = (uint8_t)(b.hi >> (56-i*8));
    for (int i = 0; i < 8; i++) out[i+8] = (uint8_t)(b.lo >> (56-i*8));
}

static Block128 gf_mult(Block128 X, Block128 Y) {
    Block128 Z = {0, 0}, V = X;
    for (int i = 0; i < 128; i++) {
        uint64_t mask = (i < 64)
            ? (Y.hi & ((uint64_t)1 << (63 - i)))
            : (Y.lo & ((uint64_t)1 << (127 - i)));
        if (mask) { Z.hi ^= V.hi; Z.lo ^= V.lo; }
        int lsb = V.lo & 1;
        V.lo = (V.lo >> 1) | ((V.hi & 1) << 63);
        V.hi >>= 1;
        if (lsb) V.hi ^= (uint64_t)0xe1 << 56;
    }
    return Z;
}

static Block128 ghash(Block128 H, const uint8_t *data, size_t len, Block128 X) {
    uint8_t block[16];
    size_t full = len / 16;
    for (size_t i = 0; i < full; i++) {
        Block128 d = b128_from(data + i * 16);
        X.hi ^= d.hi; X.lo ^= d.lo;
        X = gf_mult(X, H);
    }
    size_t rem = len % 16;
    if (rem > 0) {
        memset(block, 0, 16);
        memcpy(block, data + full * 16, rem);
        Block128 d = b128_from(block);
        X.hi ^= d.hi; X.lo ^= d.lo;
        X = gf_mult(X, H);
    }
    return X;
}

static void inc32(uint8_t ctr[16]) {
    uint32_t c = ((uint32_t)ctr[12]<<24)|((uint32_t)ctr[13]<<16)|
                 ((uint32_t)ctr[14]<<8)|(uint32_t)ctr[15];
    c++;
    ctr[12]=(uint8_t)(c>>24); ctr[13]=(uint8_t)(c>>16);
    ctr[14]=(uint8_t)(c>>8); ctr[15]=(uint8_t)c;
}

static void gctr(const AesKey *ek, uint8_t *icb,
                 const uint8_t *in, size_t len, uint8_t *out) {
    uint8_t enc[16], ctr[16];
    memcpy(ctr, icb, 16);
    size_t full = len / 16;
    for (size_t i = 0; i < full; i++) {
        aes256_encrypt_block(ek, ctr, enc);
        for (int j = 0; j < 16; j++) out[i*16+j] = in[i*16+j] ^ enc[j];
        inc32(ctr);
    }
    size_t rem = len % 16;
    if (rem > 0) {
        aes256_encrypt_block(ek, ctr, enc);
        for (size_t j = 0; j < rem; j++)
            out[full*16+j] = in[full*16+j] ^ enc[j];
    }
}

/* ── GCM core (with AAD support, used internally by vault) ── */

static int gcm_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                       const uint8_t *pt, size_t pt_len,
                       const uint8_t *aad, size_t aad_len,
                       uint8_t *ct, uint8_t tag[16]) {
    AesKey ek;
    uint8_t H_bytes[16], J0[16], icb[16], len_block[16];

    aes256_expand_key(key, &ek);
    memset(H_bytes, 0, 16);
    aes256_encrypt_block(&ek, H_bytes, H_bytes);
    Block128 H = b128_from(H_bytes);

    memcpy(J0, nonce, 12);
    J0[12]=0; J0[13]=0; J0[14]=0; J0[15]=1;

    memcpy(icb, J0, 16);
    inc32(icb);
    gctr(&ek, icb, pt, pt_len, ct);

    Block128 zero = {0, 0};
    Block128 S = ghash(H, aad, aad_len, zero);
    S = ghash(H, ct, pt_len, S);

    uint64_t ab = (uint64_t)aad_len * 8, cb = (uint64_t)pt_len * 8;
    for (int i = 0; i < 8; i++) len_block[i] = (uint8_t)(ab >> (56-i*8));
    for (int i = 0; i < 8; i++) len_block[i+8] = (uint8_t)(cb >> (56-i*8));
    Block128 lb = b128_from(len_block);
    S.hi ^= lb.hi; S.lo ^= lb.lo;
    S = gf_mult(S, H);

    uint8_t S_bytes[16], T[16];
    b128_to(S, S_bytes);
    gctr(&ek, J0, S_bytes, 16, T);
    memcpy(tag, T, 16);
    return 0;
}

static int gcm_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                       const uint8_t *ct, size_t ct_len,
                       const uint8_t *aad, size_t aad_len,
                       const uint8_t tag[16], uint8_t *pt) {
    AesKey ek;
    uint8_t H_bytes[16], J0[16], icb[16], len_block[16];

    aes256_expand_key(key, &ek);
    memset(H_bytes, 0, 16);
    aes256_encrypt_block(&ek, H_bytes, H_bytes);
    Block128 H = b128_from(H_bytes);

    memcpy(J0, nonce, 12);
    J0[12]=0; J0[13]=0; J0[14]=0; J0[15]=1;

    Block128 zero = {0, 0};
    Block128 S = ghash(H, aad, aad_len, zero);
    S = ghash(H, ct, ct_len, S);

    uint64_t ab = (uint64_t)aad_len * 8, cb = (uint64_t)ct_len * 8;
    for (int i = 0; i < 8; i++) len_block[i] = (uint8_t)(ab >> (56-i*8));
    for (int i = 0; i < 8; i++) len_block[i+8] = (uint8_t)(cb >> (56-i*8));
    Block128 lb = b128_from(len_block);
    S.hi ^= lb.hi; S.lo ^= lb.lo;
    S = gf_mult(S, H);

    uint8_t S_bytes[16], computed[16];
    b128_to(S, S_bytes);
    gctr(&ek, J0, S_bytes, 16, computed);

    uint8_t diff = 0;
    for (int i = 0; i < 16; i++) diff |= computed[i] ^ tag[i];
    if (diff != 0) return -1;

    memcpy(icb, J0, 16);
    inc32(icb);
    gctr(&ek, icb, ct, ct_len, pt);
    return 0;
}

/* ── Public a-d-d interface ── */

int aes256gcm_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                      const uint8_t *pt, size_t pt_len,
                      uint8_t *ct, uint8_t tag[16]) {
    return gcm_encrypt(key, nonce, pt, pt_len, NULL, 0, ct, tag);
}

int aes256gcm_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                      const uint8_t *ct, size_t ct_len,
                      const uint8_t tag[16], uint8_t *pt) {
    return gcm_decrypt(key, nonce, ct, ct_len, NULL, 0, tag, pt);
}

/* ================================================================
 * Ed25519 — RFC 8032
 *
 * Field: GF(p) where p = 2^255 - 19
 * Curve: -x^2 + y^2 = 1 + d*x^2*y^2  (twisted Edwards)
 * d = -121665/121666 mod p
 * Base point B: y = 4/5 mod p, x positive
 * Order L = 2^252 + 27742317777372353535851937790883648493
 *
 * Field element: 10 limbs of alternating 26/25 bits (radix 2^25.5).
 * Matches SUPERCOP ref10 representation. No __uint128_t needed.
 * ================================================================ */

typedef int32_t fe[10];

static uint64_t load_3(const uint8_t *in) {
    return (uint64_t)in[0] | ((uint64_t)in[1] << 8) | ((uint64_t)in[2] << 16);
}

static uint64_t load_4(const uint8_t *in) {
    return (uint64_t)in[0] | ((uint64_t)in[1] << 8) |
           ((uint64_t)in[2] << 16) | ((uint64_t)in[3] << 24);
}

static void fe_0(fe h) { for (int i = 0; i < 10; i++) h[i] = 0; }
static void fe_1(fe h) { h[0] = 1; for (int i = 1; i < 10; i++) h[i] = 0; }
static void fe_copy(fe h, const fe f) { for (int i = 0; i < 10; i++) h[i] = f[i]; }

static void fe_frombytes(fe h, const uint8_t *s) {
    int64_t h0 = (int64_t)load_4(s);
    int64_t h1 = (int64_t)load_3(s + 4) << 6;
    int64_t h2 = (int64_t)load_3(s + 7) << 5;
    int64_t h3 = (int64_t)load_3(s + 10) << 3;
    int64_t h4 = (int64_t)load_3(s + 13) << 2;
    int64_t h5 = (int64_t)load_4(s + 16);
    int64_t h6 = (int64_t)load_3(s + 20) << 7;
    int64_t h7 = (int64_t)load_3(s + 23) << 5;
    int64_t h8 = (int64_t)load_3(s + 26) << 4;
    int64_t h9 = (int64_t)(load_3(s + 29) & 8388607) << 2;

    int64_t carry9 = (h9 + ((int64_t)1 << 24)) >> 25; h0 += carry9 * 19; h9 -= carry9 << 25;
    int64_t carry1 = (h1 + ((int64_t)1 << 24)) >> 25; h2 += carry1; h1 -= carry1 << 25;
    int64_t carry3 = (h3 + ((int64_t)1 << 24)) >> 25; h4 += carry3; h3 -= carry3 << 25;
    int64_t carry5 = (h5 + ((int64_t)1 << 24)) >> 25; h6 += carry5; h5 -= carry5 << 25;
    int64_t carry7 = (h7 + ((int64_t)1 << 24)) >> 25; h8 += carry7; h7 -= carry7 << 25;

    int64_t carry0 = (h0 + ((int64_t)1 << 25)) >> 26; h1 += carry0; h0 -= carry0 << 26;
    int64_t carry2 = (h2 + ((int64_t)1 << 25)) >> 26; h3 += carry2; h2 -= carry2 << 26;
    int64_t carry4 = (h4 + ((int64_t)1 << 25)) >> 26; h5 += carry4; h4 -= carry4 << 26;
    int64_t carry6 = (h6 + ((int64_t)1 << 25)) >> 26; h7 += carry6; h6 -= carry6 << 26;
    int64_t carry8 = (h8 + ((int64_t)1 << 25)) >> 26; h9 += carry8; h8 -= carry8 << 26;

    h[0]=(int32_t)h0; h[1]=(int32_t)h1; h[2]=(int32_t)h2; h[3]=(int32_t)h3; h[4]=(int32_t)h4;
    h[5]=(int32_t)h5; h[6]=(int32_t)h6; h[7]=(int32_t)h7; h[8]=(int32_t)h8; h[9]=(int32_t)h9;
}

/* Fully reduce mod p and store as 32 LE bytes (ref10 algorithm) */
static void fe_tobytes(uint8_t *s, const fe h_in) {
    int32_t h0=h_in[0], h1=h_in[1], h2=h_in[2], h3=h_in[3], h4=h_in[4];
    int32_t h5=h_in[5], h6=h_in[6], h7=h_in[7], h8=h_in[8], h9=h_in[9];
    int32_t q;

    /* Compute q = floor(h/p): propagate carry to determine if h >= p */
    q = (19 * h9 + (((int32_t)1) << 24)) >> 25;
    q = (h0 + q) >> 26; q = (h1 + q) >> 25; q = (h2 + q) >> 26;
    q = (h3 + q) >> 25; q = (h4 + q) >> 26; q = (h5 + q) >> 25;
    q = (h6 + q) >> 26; q = (h7 + q) >> 25; q = (h8 + q) >> 26;
    q = (h9 + q) >> 25;

    /* Subtract q*p by adding 19*q (since p = 2^255 - 19) */
    h0 += 19 * q;

    /* Carry-propagate to canonical form */
    { int32_t c; c=h0>>26; h1+=c; h0-=c<<26; c=h1>>25; h2+=c; h1-=c<<25;
      c=h2>>26; h3+=c; h2-=c<<26; c=h3>>25; h4+=c; h3-=c<<25;
      c=h4>>26; h5+=c; h4-=c<<26; c=h5>>25; h6+=c; h5-=c<<25;
      c=h6>>26; h7+=c; h6-=c<<26; c=h7>>25; h8+=c; h7-=c<<25;
      c=h8>>26; h9+=c; h8-=c<<26; c=h9>>25; h9-=c<<25; }

    /* Pack 10 limbs (26/25/26/25/.../25 bits) into 32 LE bytes */
    s[0]  = (uint8_t)(h0); s[1]  = (uint8_t)(h0 >> 8); s[2]  = (uint8_t)(h0 >> 16);
    s[3]  = (uint8_t)((h0 >> 24) | (h1 << 2)); s[4]  = (uint8_t)(h1 >> 6);
    s[5]  = (uint8_t)(h1 >> 14); s[6]  = (uint8_t)((h1 >> 22) | (h2 << 3));
    s[7]  = (uint8_t)(h2 >> 5); s[8]  = (uint8_t)(h2 >> 13);
    s[9]  = (uint8_t)((h2 >> 21) | (h3 << 5)); s[10] = (uint8_t)(h3 >> 3);
    s[11] = (uint8_t)(h3 >> 11); s[12] = (uint8_t)((h3 >> 19) | (h4 << 6));
    s[13] = (uint8_t)(h4 >> 2); s[14] = (uint8_t)(h4 >> 10); s[15] = (uint8_t)(h4 >> 18);
    s[16] = (uint8_t)(h5); s[17] = (uint8_t)(h5 >> 8); s[18] = (uint8_t)(h5 >> 16);
    s[19] = (uint8_t)((h5 >> 24) | (h6 << 1)); s[20] = (uint8_t)(h6 >> 7);
    s[21] = (uint8_t)(h6 >> 15); s[22] = (uint8_t)((h6 >> 23) | (h7 << 3));
    s[23] = (uint8_t)(h7 >> 5); s[24] = (uint8_t)(h7 >> 13);
    s[25] = (uint8_t)((h7 >> 21) | (h8 << 4)); s[26] = (uint8_t)(h8 >> 4);
    s[27] = (uint8_t)(h8 >> 12); s[28] = (uint8_t)((h8 >> 20) | (h9 << 6));
    s[29] = (uint8_t)(h9 >> 2); s[30] = (uint8_t)(h9 >> 10); s[31] = (uint8_t)(h9 >> 18);
}

static void fe_add(fe h, const fe f, const fe g) {
    for (int i = 0; i < 10; i++) h[i] = f[i] + g[i];
}

static void fe_sub(fe h, const fe f, const fe g) {
    for (int i = 0; i < 10; i++) h[i] = f[i] - g[i];
}

static void fe_neg(fe h, const fe f) {
    for (int i = 0; i < 10; i++) h[i] = -f[i];
}

/* fe_mul: h = f * g — ref10 10x10 schoolbook with 12-carry chain */
static void fe_mul(fe h, const fe f, const fe g) {
    int32_t f0=f[0],f1=f[1],f2=f[2],f3=f[3],f4=f[4];
    int32_t f5=f[5],f6=f[6],f7=f[7],f8=f[8],f9=f[9];
    int32_t g0=g[0],g1=g[1],g2=g[2],g3=g[3],g4=g[4];
    int32_t g5=g[5],g6=g[6],g7=g[7],g8=g[8],g9=g[9];
    int32_t g1_19=19*g1,g2_19=19*g2,g3_19=19*g3,g4_19=19*g4;
    int32_t g5_19=19*g5,g6_19=19*g6,g7_19=19*g7,g8_19=19*g8,g9_19=19*g9;
    int32_t f1_2=2*f1,f3_2=2*f3,f5_2=2*f5,f7_2=2*f7,f9_2=2*f9;

    int64_t h0 = f0*(int64_t)g0   + f1_2*(int64_t)g9_19 + f2*(int64_t)g8_19
               + f3_2*(int64_t)g7_19 + f4*(int64_t)g6_19   + f5_2*(int64_t)g5_19
               + f6*(int64_t)g4_19   + f7_2*(int64_t)g3_19 + f8*(int64_t)g2_19
               + f9_2*(int64_t)g1_19;
    int64_t h1 = f0*(int64_t)g1   + f1*(int64_t)g0     + f2*(int64_t)g9_19
               + f3*(int64_t)g8_19   + f4*(int64_t)g7_19   + f5*(int64_t)g6_19
               + f6*(int64_t)g5_19   + f7*(int64_t)g4_19   + f8*(int64_t)g3_19
               + f9*(int64_t)g2_19;
    int64_t h2 = f0*(int64_t)g2   + f1_2*(int64_t)g1   + f2*(int64_t)g0
               + f3_2*(int64_t)g9_19 + f4*(int64_t)g8_19   + f5_2*(int64_t)g7_19
               + f6*(int64_t)g6_19   + f7_2*(int64_t)g5_19 + f8*(int64_t)g4_19
               + f9_2*(int64_t)g3_19;
    int64_t h3 = f0*(int64_t)g3   + f1*(int64_t)g2     + f2*(int64_t)g1
               + f3*(int64_t)g0     + f4*(int64_t)g9_19   + f5*(int64_t)g8_19
               + f6*(int64_t)g7_19   + f7*(int64_t)g6_19   + f8*(int64_t)g5_19
               + f9*(int64_t)g4_19;
    int64_t h4 = f0*(int64_t)g4   + f1_2*(int64_t)g3   + f2*(int64_t)g2
               + f3_2*(int64_t)g1   + f4*(int64_t)g0     + f5_2*(int64_t)g9_19
               + f6*(int64_t)g8_19   + f7_2*(int64_t)g7_19 + f8*(int64_t)g6_19
               + f9_2*(int64_t)g5_19;
    int64_t h5 = f0*(int64_t)g5   + f1*(int64_t)g4     + f2*(int64_t)g3
               + f3*(int64_t)g2     + f4*(int64_t)g1     + f5*(int64_t)g0
               + f6*(int64_t)g9_19   + f7*(int64_t)g8_19   + f8*(int64_t)g7_19
               + f9*(int64_t)g6_19;
    int64_t h6 = f0*(int64_t)g6   + f1_2*(int64_t)g5   + f2*(int64_t)g4
               + f3_2*(int64_t)g3   + f4*(int64_t)g2     + f5_2*(int64_t)g1
               + f6*(int64_t)g0     + f7_2*(int64_t)g9_19 + f8*(int64_t)g8_19
               + f9_2*(int64_t)g7_19;
    int64_t h7 = f0*(int64_t)g7   + f1*(int64_t)g6     + f2*(int64_t)g5
               + f3*(int64_t)g4     + f4*(int64_t)g3     + f5*(int64_t)g2
               + f6*(int64_t)g1     + f7*(int64_t)g0     + f8*(int64_t)g9_19
               + f9*(int64_t)g8_19;
    int64_t h8 = f0*(int64_t)g8   + f1_2*(int64_t)g7   + f2*(int64_t)g6
               + f3_2*(int64_t)g5   + f4*(int64_t)g4     + f5_2*(int64_t)g3
               + f6*(int64_t)g2     + f7_2*(int64_t)g1   + f8*(int64_t)g0
               + f9_2*(int64_t)g9_19;
    int64_t h9 = f0*(int64_t)g9   + f1*(int64_t)g8     + f2*(int64_t)g7
               + f3*(int64_t)g6     + f4*(int64_t)g5     + f5*(int64_t)g4
               + f6*(int64_t)g3     + f7*(int64_t)g2     + f8*(int64_t)g1
               + f9*(int64_t)g0;

    /* ref10 carry chain: pairs (0,4),(1,5),(2,6),(3,7),(4,8), then 9->0, 0->1 */
    int64_t c;
    c = (h0 + ((int64_t)1<<25)) >> 26; h1 += c; h0 -= c << 26;
    c = (h4 + ((int64_t)1<<25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h1 + ((int64_t)1<<24)) >> 25; h2 += c; h1 -= c << 25;
    c = (h5 + ((int64_t)1<<24)) >> 25; h6 += c; h5 -= c << 25;
    c = (h2 + ((int64_t)1<<25)) >> 26; h3 += c; h2 -= c << 26;
    c = (h6 + ((int64_t)1<<25)) >> 26; h7 += c; h6 -= c << 26;
    c = (h3 + ((int64_t)1<<24)) >> 25; h4 += c; h3 -= c << 25;
    c = (h7 + ((int64_t)1<<24)) >> 25; h8 += c; h7 -= c << 25;
    c = (h4 + ((int64_t)1<<25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h8 + ((int64_t)1<<25)) >> 26; h9 += c; h8 -= c << 26;
    c = (h9 + ((int64_t)1<<24)) >> 25; h0 += c * 19; h9 -= c << 25;
    c = (h0 + ((int64_t)1<<25)) >> 26; h1 += c; h0 -= c << 26;

    h[0]=(int32_t)h0; h[1]=(int32_t)h1; h[2]=(int32_t)h2; h[3]=(int32_t)h3; h[4]=(int32_t)h4;
    h[5]=(int32_t)h5; h[6]=(int32_t)h6; h[7]=(int32_t)h7; h[8]=(int32_t)h8; h[9]=(int32_t)h9;
}

/* fe_sq: h = f*f — ref10 optimized squaring exploiting symmetry */
static void fe_sq(fe h, const fe f) {
    int32_t f0=f[0],f1=f[1],f2=f[2],f3=f[3],f4=f[4];
    int32_t f5=f[5],f6=f[6],f7=f[7],f8=f[8],f9=f[9];
    int32_t f0_2=2*f0,f1_2=2*f1,f2_2=2*f2,f3_2=2*f3,f4_2=2*f4;
    int32_t f5_2=2*f5,f6_2=2*f6,f7_2=2*f7;
    int32_t f5_38=38*f5,f6_19=19*f6,f7_38=38*f7,f8_19=19*f8,f9_38=38*f9;

    int64_t h0 = f0*(int64_t)f0   + f1_2*(int64_t)f9_38 + f2_2*(int64_t)f8_19
               + f3_2*(int64_t)f7_38 + f4_2*(int64_t)f6_19 + f5*(int64_t)f5_38;
    int64_t h1 = f0_2*(int64_t)f1 + f2*(int64_t)f9_38   + f3_2*(int64_t)f8_19
               + f4*(int64_t)f7_38   + f5_2*(int64_t)f6_19;
    int64_t h2 = f0_2*(int64_t)f2 + f1_2*(int64_t)f1    + f3_2*(int64_t)f9_38
               + f4_2*(int64_t)f8_19 + f5_2*(int64_t)f7_38 + f6*(int64_t)f6_19;
    int64_t h3 = f0_2*(int64_t)f3 + f1_2*(int64_t)f2    + f4*(int64_t)f9_38
               + f5_2*(int64_t)f8_19 + f6*(int64_t)f7_38;
    int64_t h4 = f0_2*(int64_t)f4 + f1_2*(int64_t)f3_2  + f2*(int64_t)f2
               + f5_2*(int64_t)f9_38 + f6_2*(int64_t)f8_19 + f7*(int64_t)f7_38;
    int64_t h5 = f0_2*(int64_t)f5 + f1_2*(int64_t)f4    + f2_2*(int64_t)f3
               + f6*(int64_t)f9_38   + f7_2*(int64_t)f8_19;
    int64_t h6 = f0_2*(int64_t)f6 + f1_2*(int64_t)f5_2  + f2_2*(int64_t)f4
               + f3_2*(int64_t)f3    + f7_2*(int64_t)f9_38 + f8*(int64_t)f8_19;
    int64_t h7 = f0_2*(int64_t)f7 + f1_2*(int64_t)f6    + f2_2*(int64_t)f5
               + f3_2*(int64_t)f4    + f8*(int64_t)f9_38;
    int64_t h8 = f0_2*(int64_t)f8 + f1_2*(int64_t)f7_2  + f2_2*(int64_t)f6
               + f3_2*(int64_t)f5_2  + f4*(int64_t)f4     + f9*(int64_t)f9_38;
    int64_t h9 = f0_2*(int64_t)f9 + f1_2*(int64_t)f8    + f2_2*(int64_t)f7
               + f3_2*(int64_t)f6    + f4_2*(int64_t)f5;

    int64_t c;
    c = (h0 + ((int64_t)1<<25)) >> 26; h1 += c; h0 -= c << 26;
    c = (h4 + ((int64_t)1<<25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h1 + ((int64_t)1<<24)) >> 25; h2 += c; h1 -= c << 25;
    c = (h5 + ((int64_t)1<<24)) >> 25; h6 += c; h5 -= c << 25;
    c = (h2 + ((int64_t)1<<25)) >> 26; h3 += c; h2 -= c << 26;
    c = (h6 + ((int64_t)1<<25)) >> 26; h7 += c; h6 -= c << 26;
    c = (h3 + ((int64_t)1<<24)) >> 25; h4 += c; h3 -= c << 25;
    c = (h7 + ((int64_t)1<<24)) >> 25; h8 += c; h7 -= c << 25;
    c = (h4 + ((int64_t)1<<25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h8 + ((int64_t)1<<25)) >> 26; h9 += c; h8 -= c << 26;
    c = (h9 + ((int64_t)1<<24)) >> 25; h0 += c * 19; h9 -= c << 25;
    c = (h0 + ((int64_t)1<<25)) >> 26; h1 += c; h0 -= c << 26;

    h[0]=(int32_t)h0; h[1]=(int32_t)h1; h[2]=(int32_t)h2; h[3]=(int32_t)h3; h[4]=(int32_t)h4;
    h[5]=(int32_t)h5; h[6]=(int32_t)h6; h[7]=(int32_t)h7; h[8]=(int32_t)h8; h[9]=(int32_t)h9;
}

/* fe_sq2: h = 2*f*f — ref10 square-and-double in one pass (avoids excess limb growth) */
static void fe_sq2(fe h, const fe f) {
    int32_t f0=f[0],f1=f[1],f2=f[2],f3=f[3],f4=f[4];
    int32_t f5=f[5],f6=f[6],f7=f[7],f8=f[8],f9=f[9];
    int32_t f0_2=2*f0,f1_2=2*f1,f2_2=2*f2,f3_2=2*f3,f4_2=2*f4;
    int32_t f5_2=2*f5,f6_2=2*f6,f7_2=2*f7;
    int32_t f5_38=38*f5,f6_19=19*f6,f7_38=38*f7,f8_19=19*f8,f9_38=38*f9;

    int64_t h0 = f0*(int64_t)f0   + f1_2*(int64_t)f9_38 + f2_2*(int64_t)f8_19
               + f3_2*(int64_t)f7_38 + f4_2*(int64_t)f6_19 + f5*(int64_t)f5_38;
    int64_t h1 = f0_2*(int64_t)f1 + f2*(int64_t)f9_38   + f3_2*(int64_t)f8_19
               + f4*(int64_t)f7_38   + f5_2*(int64_t)f6_19;
    int64_t h2 = f0_2*(int64_t)f2 + f1_2*(int64_t)f1    + f3_2*(int64_t)f9_38
               + f4_2*(int64_t)f8_19 + f5_2*(int64_t)f7_38 + f6*(int64_t)f6_19;
    int64_t h3 = f0_2*(int64_t)f3 + f1_2*(int64_t)f2    + f4*(int64_t)f9_38
               + f5_2*(int64_t)f8_19 + f6*(int64_t)f7_38;
    int64_t h4 = f0_2*(int64_t)f4 + f1_2*(int64_t)f3_2  + f2*(int64_t)f2
               + f5_2*(int64_t)f9_38 + f6_2*(int64_t)f8_19 + f7*(int64_t)f7_38;
    int64_t h5 = f0_2*(int64_t)f5 + f1_2*(int64_t)f4    + f2_2*(int64_t)f3
               + f6*(int64_t)f9_38   + f7_2*(int64_t)f8_19;
    int64_t h6 = f0_2*(int64_t)f6 + f1_2*(int64_t)f5_2  + f2_2*(int64_t)f4
               + f3_2*(int64_t)f3    + f7_2*(int64_t)f9_38 + f8*(int64_t)f8_19;
    int64_t h7 = f0_2*(int64_t)f7 + f1_2*(int64_t)f6    + f2_2*(int64_t)f5
               + f3_2*(int64_t)f4    + f8*(int64_t)f9_38;
    int64_t h8 = f0_2*(int64_t)f8 + f1_2*(int64_t)f7_2  + f2_2*(int64_t)f6
               + f3_2*(int64_t)f5_2  + f4*(int64_t)f4     + f9*(int64_t)f9_38;
    int64_t h9 = f0_2*(int64_t)f9 + f1_2*(int64_t)f8    + f2_2*(int64_t)f7
               + f3_2*(int64_t)f6    + f4_2*(int64_t)f5;

    /* Double all terms before carry chain — keeps output normalized */
    h0 += h0; h1 += h1; h2 += h2; h3 += h3; h4 += h4;
    h5 += h5; h6 += h6; h7 += h7; h8 += h8; h9 += h9;

    int64_t c;
    c = (h0 + ((int64_t)1<<25)) >> 26; h1 += c; h0 -= c << 26;
    c = (h4 + ((int64_t)1<<25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h1 + ((int64_t)1<<24)) >> 25; h2 += c; h1 -= c << 25;
    c = (h5 + ((int64_t)1<<24)) >> 25; h6 += c; h5 -= c << 25;
    c = (h2 + ((int64_t)1<<25)) >> 26; h3 += c; h2 -= c << 26;
    c = (h6 + ((int64_t)1<<25)) >> 26; h7 += c; h6 -= c << 26;
    c = (h3 + ((int64_t)1<<24)) >> 25; h4 += c; h3 -= c << 25;
    c = (h7 + ((int64_t)1<<24)) >> 25; h8 += c; h7 -= c << 25;
    c = (h4 + ((int64_t)1<<25)) >> 26; h5 += c; h4 -= c << 26;
    c = (h8 + ((int64_t)1<<25)) >> 26; h9 += c; h8 -= c << 26;
    c = (h9 + ((int64_t)1<<24)) >> 25; h0 += c * 19; h9 -= c << 25;
    c = (h0 + ((int64_t)1<<25)) >> 26; h1 += c; h0 -= c << 26;

    h[0]=(int32_t)h0; h[1]=(int32_t)h1; h[2]=(int32_t)h2; h[3]=(int32_t)h3; h[4]=(int32_t)h4;
    h[5]=(int32_t)h5; h[6]=(int32_t)h6; h[7]=(int32_t)h7; h[8]=(int32_t)h8; h[9]=(int32_t)h9;
}

/* Invert: r = a^(p-2) mod p via ref10 addition chain */
static void fe_invert(fe r, const fe z) {
    fe t0, t1, t2, t3;
    int i;
    fe_sq(t0, z);                                                  /* t0 = z^2 */
    fe_sq(t1, t0); fe_sq(t1, t1);                                 /* t1 = z^8 */
    fe_mul(t1, z, t1);                                             /* t1 = z^9 */
    fe_mul(t0, t0, t1);                                            /* t0 = z^11 */
    fe_sq(t2, t0);                                                 /* t2 = z^22 */
    fe_mul(t1, t1, t2);                                            /* t1 = z^31 = z^(2^5-1) */
    fe_sq(t2, t1); for (i=1;i<5;i++) fe_sq(t2, t2);               /* t2 = z^(2^10-32) */
    fe_mul(t1, t2, t1);                                            /* t1 = z^(2^10-1) */
    fe_sq(t2, t1); for (i=1;i<10;i++) fe_sq(t2, t2);              /* t2 = z^(2^20-1024) */
    fe_mul(t2, t2, t1);                                            /* t2 = z^(2^20-1) */
    fe_sq(t3, t2); for (i=1;i<20;i++) fe_sq(t3, t3);              /* t3 = z^(2^40-2^20) */
    fe_mul(t2, t3, t2);                                            /* t2 = z^(2^40-1) */
    fe_sq(t2, t2); for (i=1;i<10;i++) fe_sq(t2, t2);              /* t2 = z^(2^50-1024) */
    fe_mul(t1, t2, t1);                                            /* t1 = z^(2^50-1) */
    fe_sq(t2, t1); for (i=1;i<50;i++) fe_sq(t2, t2);              /* t2 = z^(2^100-2^50) */
    fe_mul(t2, t2, t1);                                            /* t2 = z^(2^100-1) */
    fe_sq(t3, t2); for (i=1;i<100;i++) fe_sq(t3, t3);             /* t3 = z^(2^200-2^100) */
    fe_mul(t2, t3, t2);                                            /* t2 = z^(2^200-1) */
    fe_sq(t2, t2); for (i=1;i<50;i++) fe_sq(t2, t2);              /* t2 = z^(2^250-2^50) */
    fe_mul(t1, t2, t1);                                            /* t1 = z^(2^250-1) */
    fe_sq(t1, t1); for (i=1;i<5;i++) fe_sq(t1, t1);               /* t1 = z^(2^255-32) */
    fe_mul(r, t1, t0);                                             /* r = z^(2^255-21) = z^(p-2) */
}

/* a^((p-5)/8) — for square root in point decompression */
static void fe_pow2523(fe r, const fe z) {
    fe t0, t1, t2;
    int i;
    fe_sq(t0, z);                                                  /* t0 = z^2 */
    fe_sq(t1, t0); fe_sq(t1, t1);                                 /* t1 = z^8 */
    fe_mul(t1, z, t1);                                             /* t1 = z^9 */
    fe_mul(t0, t0, t1);                                            /* t0 = z^11 */
    fe_sq(t0, t0);                                                 /* t0 = z^22 */
    fe_mul(t0, t1, t0);                                            /* t0 = z^31 = z^(2^5-1) */
    fe_sq(t1, t0); for (i=1;i<5;i++) fe_sq(t1, t1);               /* t1 = z^(2^10-32) */
    fe_mul(t0, t1, t0);                                            /* t0 = z^(2^10-1) */
    fe_sq(t1, t0); for (i=1;i<10;i++) fe_sq(t1, t1);              /* t1 = z^(2^20-1024) */
    fe_mul(t1, t1, t0);                                            /* t1 = z^(2^20-1) */
    fe_sq(t2, t1); for (i=1;i<20;i++) fe_sq(t2, t2);              /* t2 = z^(2^40-2^20) */
    fe_mul(t1, t2, t1);                                            /* t1 = z^(2^40-1) */
    fe_sq(t1, t1); for (i=1;i<10;i++) fe_sq(t1, t1);              /* t1 = z^(2^50-1024) */
    fe_mul(t0, t1, t0);                                            /* t0 = z^(2^50-1) */
    fe_sq(t1, t0); for (i=1;i<50;i++) fe_sq(t1, t1);              /* t1 = z^(2^100-2^50) */
    fe_mul(t1, t1, t0);                                            /* t1 = z^(2^100-1) */
    fe_sq(t2, t1); for (i=1;i<100;i++) fe_sq(t2, t2);             /* t2 = z^(2^200-2^100) */
    fe_mul(t1, t2, t1);                                            /* t1 = z^(2^200-1) */
    fe_sq(t1, t1); for (i=1;i<50;i++) fe_sq(t1, t1);              /* t1 = z^(2^250-2^50) */
    fe_mul(t0, t1, t0);                                            /* t0 = z^(2^250-1) */
    fe_sq(t0, t0); fe_sq(t0, t0);                                 /* t0 = z^(2^252-4) */
    fe_mul(r, t0, z);                                              /* r = z^(2^252-3) = z^((p-5)/8) */
}

static int fe_isnonzero(const fe a) {
    uint8_t s[32]; fe_tobytes(s, a);
    uint8_t d = 0;
    for (int i = 0; i < 32; i++) d |= s[i];
    return d != 0;
}

static int fe_isnegative(const fe a) {
    uint8_t s[32]; fe_tobytes(s, a);
    return s[0] & 1;
}

/* ================================================================
 * Ed25519 group element — extended coordinates (X:Y:Z:T)
 * x = X/Z, y = Y/Z, x*y = T/Z
 * ================================================================ */

typedef struct { fe X, Y, Z, T; } ge;

/* Curve constants as fe (from ref10 — no runtime conversion needed) */
static const fe FE_D = {
    -10913610, 13857413, -15372611, 6949391, 114729,
    -8787816, -6275908, -3247719, -18696448, -12055116
};
static const fe FE_D2 = {
    -21827239, -5839606, -30745221, 13898782, 229458,
    15978800, -12551817, -6495438, 29715968, 9444199
};
static const fe FE_SQRTM1 = {
    -32595792, -7943725, 9377950, 3500415, 12389472,
    -272473, -25146209, -2005654, 326686, 11406482
};

/* Base point B compressed: y = 4/5 mod p, sign(x) = 1 */
static const uint8_t B_COMPRESSED[32] = {
    0x58, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66,
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66
};

static void ge_zero(ge *r) {
    fe_0(r->X); fe_1(r->Y); fe_1(r->Z); fe_0(r->T);
}

/* Decompress 32 bytes -> group element. Returns 0 on success, -1 on failure. */
static int ge_frombytes(ge *r, const uint8_t s[32]) {
    fe u, v, v3, vxx, check;

    /* y = decode LE, clear top bit */
    uint8_t y_bytes[32];
    memcpy(y_bytes, s, 32);
    int x_sign = (y_bytes[31] >> 7) & 1;
    y_bytes[31] &= 0x7f;
    fe_frombytes(r->Y, y_bytes);

    /* u = y^2 - 1, v = d*y^2 + 1 */
    fe t, y2;
    fe_sq(y2, r->Y);
    fe_1(t);
    fe_sub(u, y2, t);                 /* u = y^2 - 1 */
    fe_mul(v, y2, FE_D);
    fe_add(v, v, t);                   /* v = d*y^2 + 1 */

    /* x = sqrt(u/v) */
    fe_sq(v3, v); fe_mul(v3, v3, v);       /* v3 = v^3 */
    fe_sq(r->X, v3); fe_mul(r->X, r->X, v); /* X = v^7 */
    fe_mul(r->X, r->X, u);                /* X = u*v^7 */
    fe_pow2523(r->X, r->X);               /* X = (u*v^7)^((p-5)/8) */
    fe_mul(r->X, r->X, v3);               /* X *= v^3 */
    fe_mul(r->X, r->X, u);                /* X *= u */

    /* verify: vxx = v * x^2 */
    fe_sq(vxx, r->X);
    fe_mul(vxx, vxx, v);
    fe_sub(check, vxx, u);
    if (fe_isnonzero(check)) {
        fe_add(check, vxx, u);
        if (fe_isnonzero(check)) return -1; /* not on curve */
        fe_mul(r->X, r->X, FE_SQRTM1);
    }

    /* fix sign */
    if (fe_isnegative(r->X) != x_sign) fe_neg(r->X, r->X);

    fe_1(r->Z);
    fe_mul(r->T, r->X, r->Y);
    return 0;
}

/* Compress group element to 32 bytes */
static void ge_tobytes(uint8_t out[32], const ge *p) {
    fe recip, x, y;
    fe_invert(recip, p->Z);
    fe_mul(x, p->X, recip);
    fe_mul(y, p->Y, recip);
    fe_tobytes(out, y);
    out[31] ^= (uint8_t)(fe_isnegative(x) << 7);
}

/* Point doubling: extended -> extended */
static void ge_double(ge *r, const ge *p) {
    fe a, b, c, d_val, e, f, g, h_val;
    fe_sq(a, p->X);
    fe_sq(b, p->Y);
    fe_sq2(c, p->Z);                        /* c = 2*Z^2 (normalized) */
    fe_neg(d_val, a);                       /* d = -X^2 (twisted Edwards: a=-1) */
    fe_add(e, p->X, p->Y); fe_sq(e, e);
    fe_sub(e, e, a); fe_sub(e, e, b);      /* e = (X+Y)^2 - X^2 - Y^2 = 2XY */
    fe_add(g, d_val, b);                    /* g = -X^2+Y^2 */
    fe_sub(f, g, c);                        /* f = g - c */
    fe_sub(h_val, d_val, b);               /* h = -X^2-Y^2 */
    fe_mul(r->X, e, f);
    fe_mul(r->Y, g, h_val);
    fe_mul(r->T, e, h_val);
    fe_mul(r->Z, f, g);
}

/* Point addition: extended + extended -> extended */
static void ge_add(ge *r, const ge *p, const ge *q) {
    fe a, b, c, d_val, e, f, g, h_val;

    fe_sub(a, p->Y, p->X);
    fe_sub(h_val, q->Y, q->X);
    fe_mul(a, a, h_val);                   /* a = (Y1-X1)(Y2-X2) */
    fe_add(b, p->Y, p->X);
    fe_add(h_val, q->Y, q->X);
    fe_mul(b, b, h_val);                   /* b = (Y1+X1)(Y2+X2) */
    fe_mul(c, p->T, q->T);
    fe_mul(c, c, FE_D2);                   /* c = T1*T2*2d */
    fe_mul(d_val, p->Z, q->Z);
    fe_add(d_val, d_val, d_val);           /* d = 2*Z1*Z2 */
    fe_sub(e, b, a);                        /* e = b - a */
    fe_sub(f, d_val, c);                    /* f = d - c */
    fe_add(g, d_val, c);                    /* g = d + c */
    fe_add(h_val, b, a);                   /* h = b + a */
    fe_mul(r->X, e, f);
    fe_mul(r->Y, g, h_val);
    fe_mul(r->T, e, h_val);
    fe_mul(r->Z, f, g);
}

/* Scalar multiplication: r = scalar * p (double-and-add, constant-ish time) */
static void ge_scalarmult(ge *r, const uint8_t scalar[32], const ge *p) {
    ge acc, tmp;
    ge_zero(&acc);
    ge pp = *p;

    for (int i = 0; i < 256; i++) {
        int bit = (scalar[i >> 3] >> (i & 7)) & 1;
        if (bit) { ge_add(&tmp, &acc, &pp); acc = tmp; }
        ge_double(&tmp, &pp); pp = tmp;
    }
    *r = acc;
}

/* Fixed-base scalar multiply: r = scalar * B */
static void ge_scalarmult_base(ge *r, const uint8_t scalar[32]) {
    ge B;
    ge_frombytes(&B, B_COMPRESSED);
    ge_scalarmult(r, scalar, &B);
}

/* ================================================================
 * Scalar arithmetic mod L
 * L = 2^252 + 27742317777372353535851937790883648493
 * Represented as 32 bytes little-endian.
 * ================================================================ */

static const uint8_t L_BYTES[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10
};

/* Reduce 64-byte scalar mod L into 32 bytes.
 * Uses Barrett-like reduction with schoolbook multiply.
 * Input: 64 bytes (512-bit LE). Output: 32 bytes (256-bit LE). */
static void sc_reduce(uint8_t out[32], const uint8_t s[64]) {
    int64_t t[24];
    t[0]  = (int64_t)(2097151 & (((uint32_t)s[0]) | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16)));
    t[1]  = (int64_t)(2097151 & (((uint32_t)s[2] >> 5) | ((uint32_t)s[3] << 3) | ((uint32_t)s[4] << 11) | ((uint32_t)s[5] << 19)));
    t[2]  = (int64_t)(2097151 & (((uint32_t)s[5] >> 2) | ((uint32_t)s[6] << 6) | ((uint32_t)s[7] << 14)));
    t[3]  = (int64_t)(2097151 & (((uint32_t)s[7] >> 7) | ((uint32_t)s[8] << 1) | ((uint32_t)s[9] << 9) | ((uint32_t)s[10] << 17)));
    t[4]  = (int64_t)(2097151 & (((uint32_t)s[10] >> 4) | ((uint32_t)s[11] << 4) | ((uint32_t)s[12] << 12) | ((uint32_t)s[13] << 20)));
    t[5]  = (int64_t)(2097151 & (((uint32_t)s[13] >> 1) | ((uint32_t)s[14] << 7) | ((uint32_t)s[15] << 15)));
    t[6]  = (int64_t)(2097151 & (((uint32_t)s[15] >> 6) | ((uint32_t)s[16] << 2) | ((uint32_t)s[17] << 10) | ((uint32_t)s[18] << 18)));
    t[7]  = (int64_t)(2097151 & (((uint32_t)s[18] >> 3) | ((uint32_t)s[19] << 5) | ((uint32_t)s[20] << 13)));
    t[8]  = (int64_t)(2097151 & (((uint32_t)s[21]) | ((uint32_t)s[22] << 8) | ((uint32_t)s[23] << 16)));
    t[9]  = (int64_t)(2097151 & (((uint32_t)s[23] >> 5) | ((uint32_t)s[24] << 3) | ((uint32_t)s[25] << 11) | ((uint32_t)s[26] << 19)));
    t[10] = (int64_t)(2097151 & (((uint32_t)s[26] >> 2) | ((uint32_t)s[27] << 6) | ((uint32_t)s[28] << 14)));
    t[11] = (int64_t)(2097151 & (((uint32_t)s[28] >> 7) | ((uint32_t)s[29] << 1) | ((uint32_t)s[30] << 9) | ((uint32_t)s[31] << 17)));
    t[12] = (int64_t)(2097151 & (((uint32_t)s[31] >> 4) | ((uint32_t)s[32] << 4) | ((uint32_t)s[33] << 12) | ((uint32_t)s[34] << 20)));
    t[13] = (int64_t)(2097151 & (((uint32_t)s[34] >> 1) | ((uint32_t)s[35] << 7) | ((uint32_t)s[36] << 15)));
    t[14] = (int64_t)(2097151 & (((uint32_t)s[36] >> 6) | ((uint32_t)s[37] << 2) | ((uint32_t)s[38] << 10) | ((uint32_t)s[39] << 18)));
    t[15] = (int64_t)(2097151 & (((uint32_t)s[39] >> 3) | ((uint32_t)s[40] << 5) | ((uint32_t)s[41] << 13)));
    t[16] = (int64_t)(2097151 & (((uint32_t)s[42]) | ((uint32_t)s[43] << 8) | ((uint32_t)s[44] << 16)));
    t[17] = (int64_t)(2097151 & (((uint32_t)s[44] >> 5) | ((uint32_t)s[45] << 3) | ((uint32_t)s[46] << 11) | ((uint32_t)s[47] << 19)));
    t[18] = (int64_t)(2097151 & (((uint32_t)s[47] >> 2) | ((uint32_t)s[48] << 6) | ((uint32_t)s[49] << 14)));
    t[19] = (int64_t)(2097151 & (((uint32_t)s[49] >> 7) | ((uint32_t)s[50] << 1) | ((uint32_t)s[51] << 9) | ((uint32_t)s[52] << 17)));
    t[20] = (int64_t)(2097151 & (((uint32_t)s[52] >> 4) | ((uint32_t)s[53] << 4) | ((uint32_t)s[54] << 12) | ((uint32_t)s[55] << 20)));
    t[21] = (int64_t)(2097151 & (((uint32_t)s[55] >> 1) | ((uint32_t)s[56] << 7) | ((uint32_t)s[57] << 15)));
    t[22] = (int64_t)(2097151 & (((uint32_t)s[57] >> 6) | ((uint32_t)s[58] << 2) | ((uint32_t)s[59] << 10) | ((uint32_t)s[60] << 18)));
    t[23] = (int64_t)(((uint32_t)s[60] >> 3) | ((uint32_t)s[61] << 5) | ((uint32_t)s[62] << 13) | ((uint32_t)s[63] << 21));

    /* Two-pass reduction following ref10 to avoid int64_t overflow.
     * L = 2^252 + L_low, and the constants {666643, 470296, 654183, -997805,
     * 136657, -683901} are the negated signed 21-bit limbs of L_low. */
    int64_t carry;

    /* Pass 1: fold t[23]..t[18] into t[6]..t[17] */
    t[11] += t[23] * 666643; t[12] += t[23] * 470296; t[13] += t[23] * 654183;
    t[14] -= t[23] * 997805; t[15] += t[23] * 136657; t[16] -= t[23] * 683901; t[23] = 0;
    t[10] += t[22] * 666643; t[11] += t[22] * 470296; t[12] += t[22] * 654183;
    t[13] -= t[22] * 997805; t[14] += t[22] * 136657; t[15] -= t[22] * 683901; t[22] = 0;
    t[9]  += t[21] * 666643; t[10] += t[21] * 470296; t[11] += t[21] * 654183;
    t[12] -= t[21] * 997805; t[13] += t[21] * 136657; t[14] -= t[21] * 683901; t[21] = 0;
    t[8]  += t[20] * 666643; t[9]  += t[20] * 470296; t[10] += t[20] * 654183;
    t[11] -= t[20] * 997805; t[12] += t[20] * 136657; t[13] -= t[20] * 683901; t[20] = 0;
    t[7]  += t[19] * 666643; t[8]  += t[19] * 470296; t[9]  += t[19] * 654183;
    t[10] -= t[19] * 997805; t[11] += t[19] * 136657; t[12] -= t[19] * 683901; t[19] = 0;
    t[6]  += t[18] * 666643; t[7]  += t[18] * 470296; t[8]  += t[18] * 654183;
    t[9]  -= t[18] * 997805; t[10] += t[18] * 136657; t[11] -= t[18] * 683901; t[18] = 0;

    /* Intermediate two-wave carry on t[6]..t[17]: even then odd */
    carry = (t[6]  + (1 << 20)) >> 21; t[7]  += carry; t[6]  -= carry << 21;
    carry = (t[8]  + (1 << 20)) >> 21; t[9]  += carry; t[8]  -= carry << 21;
    carry = (t[10] + (1 << 20)) >> 21; t[11] += carry; t[10] -= carry << 21;
    carry = (t[12] + (1 << 20)) >> 21; t[13] += carry; t[12] -= carry << 21;
    carry = (t[14] + (1 << 20)) >> 21; t[15] += carry; t[14] -= carry << 21;
    carry = (t[16] + (1 << 20)) >> 21; t[17] += carry; t[16] -= carry << 21;
    carry = (t[7]  + (1 << 20)) >> 21; t[8]  += carry; t[7]  -= carry << 21;
    carry = (t[9]  + (1 << 20)) >> 21; t[10] += carry; t[9]  -= carry << 21;
    carry = (t[11] + (1 << 20)) >> 21; t[12] += carry; t[11] -= carry << 21;
    carry = (t[13] + (1 << 20)) >> 21; t[14] += carry; t[13] -= carry << 21;
    carry = (t[15] + (1 << 20)) >> 21; t[16] += carry; t[15] -= carry << 21;

    /* Pass 2: fold t[17]..t[12] into t[0]..t[11] */
    t[5] += t[17] * 666643; t[6]  += t[17] * 470296; t[7]  += t[17] * 654183;
    t[8] -= t[17] * 997805; t[9]  += t[17] * 136657; t[10] -= t[17] * 683901; t[17] = 0;
    t[4] += t[16] * 666643; t[5]  += t[16] * 470296; t[6]  += t[16] * 654183;
    t[7] -= t[16] * 997805; t[8]  += t[16] * 136657; t[9]  -= t[16] * 683901; t[16] = 0;
    t[3] += t[15] * 666643; t[4]  += t[15] * 470296; t[5]  += t[15] * 654183;
    t[6] -= t[15] * 997805; t[7]  += t[15] * 136657; t[8]  -= t[15] * 683901; t[15] = 0;
    t[2] += t[14] * 666643; t[3]  += t[14] * 470296; t[4]  += t[14] * 654183;
    t[5] -= t[14] * 997805; t[6]  += t[14] * 136657; t[7]  -= t[14] * 683901; t[14] = 0;
    t[1] += t[13] * 666643; t[2]  += t[13] * 470296; t[3]  += t[13] * 654183;
    t[4] -= t[13] * 997805; t[5]  += t[13] * 136657; t[6]  -= t[13] * 683901; t[13] = 0;
    t[0] += t[12] * 666643; t[1]  += t[12] * 470296; t[2]  += t[12] * 654183;
    t[3] -= t[12] * 997805; t[4]  += t[12] * 136657; t[5]  -= t[12] * 683901; t[12] = 0;

    /* Two-wave carry on t[0]..t[11]: even then odd */
    carry = (t[0]  + (1 << 20)) >> 21; t[1]  += carry; t[0]  -= carry << 21;
    carry = (t[2]  + (1 << 20)) >> 21; t[3]  += carry; t[2]  -= carry << 21;
    carry = (t[4]  + (1 << 20)) >> 21; t[5]  += carry; t[4]  -= carry << 21;
    carry = (t[6]  + (1 << 20)) >> 21; t[7]  += carry; t[6]  -= carry << 21;
    carry = (t[8]  + (1 << 20)) >> 21; t[9]  += carry; t[8]  -= carry << 21;
    carry = (t[10] + (1 << 20)) >> 21; t[11] += carry; t[10] -= carry << 21;
    carry = (t[1]  + (1 << 20)) >> 21; t[2]  += carry; t[1]  -= carry << 21;
    carry = (t[3]  + (1 << 20)) >> 21; t[4]  += carry; t[3]  -= carry << 21;
    carry = (t[5]  + (1 << 20)) >> 21; t[6]  += carry; t[5]  -= carry << 21;
    carry = (t[7]  + (1 << 20)) >> 21; t[8]  += carry; t[7]  -= carry << 21;
    carry = (t[9]  + (1 << 20)) >> 21; t[10] += carry; t[9]  -= carry << 21;
    carry = (t[11] + (1 << 20)) >> 21; t[12] += carry; t[11] -= carry << 21;

    /* Reduce t[12] carry into t[0]..t[5] */
    t[0] += t[12] * 666643; t[1] += t[12] * 470296; t[2] += t[12] * 654183;
    t[3] -= t[12] * 997805; t[4] += t[12] * 136657; t[5] -= t[12] * 683901; t[12] = 0;

    /* Sequential carry (simple truncation, not rounded) */
    carry = t[0]  >> 21; t[1]  += carry; t[0]  -= carry << 21;
    carry = t[1]  >> 21; t[2]  += carry; t[1]  -= carry << 21;
    carry = t[2]  >> 21; t[3]  += carry; t[2]  -= carry << 21;
    carry = t[3]  >> 21; t[4]  += carry; t[3]  -= carry << 21;
    carry = t[4]  >> 21; t[5]  += carry; t[4]  -= carry << 21;
    carry = t[5]  >> 21; t[6]  += carry; t[5]  -= carry << 21;
    carry = t[6]  >> 21; t[7]  += carry; t[6]  -= carry << 21;
    carry = t[7]  >> 21; t[8]  += carry; t[7]  -= carry << 21;
    carry = t[8]  >> 21; t[9]  += carry; t[8]  -= carry << 21;
    carry = t[9]  >> 21; t[10] += carry; t[9]  -= carry << 21;
    carry = t[10] >> 21; t[11] += carry; t[10] -= carry << 21;
    carry = t[11] >> 21; t[12] += carry; t[11] -= carry << 21;

    /* Reduce t[12] again (edge case from carry11) */
    t[0] += t[12] * 666643; t[1] += t[12] * 470296; t[2] += t[12] * 654183;
    t[3] -= t[12] * 997805; t[4] += t[12] * 136657; t[5] -= t[12] * 683901; t[12] = 0;

    /* Final sequential carry (simple truncation) */
    carry = t[0]  >> 21; t[1]  += carry; t[0]  -= carry << 21;
    carry = t[1]  >> 21; t[2]  += carry; t[1]  -= carry << 21;
    carry = t[2]  >> 21; t[3]  += carry; t[2]  -= carry << 21;
    carry = t[3]  >> 21; t[4]  += carry; t[3]  -= carry << 21;
    carry = t[4]  >> 21; t[5]  += carry; t[4]  -= carry << 21;
    carry = t[5]  >> 21; t[6]  += carry; t[5]  -= carry << 21;
    carry = t[6]  >> 21; t[7]  += carry; t[6]  -= carry << 21;
    carry = t[7]  >> 21; t[8]  += carry; t[7]  -= carry << 21;
    carry = t[8]  >> 21; t[9]  += carry; t[8]  -= carry << 21;
    carry = t[9]  >> 21; t[10] += carry; t[9]  -= carry << 21;
    carry = t[10] >> 21; t[11] += carry; t[10] -= carry << 21;

    /* Pack 12 x 21-bit limbs into 32 bytes LE */
    out[0]  = (uint8_t)(t[0]);
    out[1]  = (uint8_t)(t[0] >> 8);
    out[2]  = (uint8_t)((t[0] >> 16) | (t[1] << 5));
    out[3]  = (uint8_t)(t[1] >> 3);
    out[4]  = (uint8_t)(t[1] >> 11);
    out[5]  = (uint8_t)((t[1] >> 19) | (t[2] << 2));
    out[6]  = (uint8_t)(t[2] >> 6);
    out[7]  = (uint8_t)((t[2] >> 14) | (t[3] << 7));
    out[8]  = (uint8_t)(t[3] >> 1);
    out[9]  = (uint8_t)(t[3] >> 9);
    out[10] = (uint8_t)((t[3] >> 17) | (t[4] << 4));
    out[11] = (uint8_t)(t[4] >> 4);
    out[12] = (uint8_t)(t[4] >> 12);
    out[13] = (uint8_t)((t[4] >> 20) | (t[5] << 1));
    out[14] = (uint8_t)(t[5] >> 7);
    out[15] = (uint8_t)((t[5] >> 15) | (t[6] << 6));
    out[16] = (uint8_t)(t[6] >> 2);
    out[17] = (uint8_t)(t[6] >> 10);
    out[18] = (uint8_t)((t[6] >> 18) | (t[7] << 3));
    out[19] = (uint8_t)(t[7] >> 5);
    out[20] = (uint8_t)(t[7] >> 13);
    out[21] = (uint8_t)(t[8]);
    out[22] = (uint8_t)(t[8] >> 8);
    out[23] = (uint8_t)((t[8] >> 16) | (t[9] << 5));
    out[24] = (uint8_t)(t[9] >> 3);
    out[25] = (uint8_t)(t[9] >> 11);
    out[26] = (uint8_t)((t[9] >> 19) | (t[10] << 2));
    out[27] = (uint8_t)(t[10] >> 6);
    out[28] = (uint8_t)((t[10] >> 14) | (t[11] << 7));
    out[29] = (uint8_t)(t[11] >> 1);
    out[30] = (uint8_t)(t[11] >> 9);
    out[31] = (uint8_t)(t[11] >> 17);
}

/* sc_muladd: out = (a * b + c) mod L — all 32-byte LE scalars */
static void sc_muladd(uint8_t out[32], const uint8_t a[32], const uint8_t b[32], const uint8_t c[32]) {
    /* Load a, b, c into 21-bit limbs */
    int64_t a0  = 2097151 & (((uint32_t)a[0]) | ((uint32_t)a[1]<<8) | ((uint32_t)a[2]<<16));
    int64_t a1  = 2097151 & (((uint32_t)a[2]>>5) | ((uint32_t)a[3]<<3) | ((uint32_t)a[4]<<11) | ((uint32_t)a[5]<<19));
    int64_t a2  = 2097151 & (((uint32_t)a[5]>>2) | ((uint32_t)a[6]<<6) | ((uint32_t)a[7]<<14));
    int64_t a3  = 2097151 & (((uint32_t)a[7]>>7) | ((uint32_t)a[8]<<1) | ((uint32_t)a[9]<<9) | ((uint32_t)a[10]<<17));
    int64_t a4  = 2097151 & (((uint32_t)a[10]>>4) | ((uint32_t)a[11]<<4) | ((uint32_t)a[12]<<12) | ((uint32_t)a[13]<<20));
    int64_t a5  = 2097151 & (((uint32_t)a[13]>>1) | ((uint32_t)a[14]<<7) | ((uint32_t)a[15]<<15));
    int64_t a6  = 2097151 & (((uint32_t)a[15]>>6) | ((uint32_t)a[16]<<2) | ((uint32_t)a[17]<<10) | ((uint32_t)a[18]<<18));
    int64_t a7  = 2097151 & (((uint32_t)a[18]>>3) | ((uint32_t)a[19]<<5) | ((uint32_t)a[20]<<13));
    int64_t a8  = 2097151 & (((uint32_t)a[21]) | ((uint32_t)a[22]<<8) | ((uint32_t)a[23]<<16));
    int64_t a9  = 2097151 & (((uint32_t)a[23]>>5) | ((uint32_t)a[24]<<3) | ((uint32_t)a[25]<<11) | ((uint32_t)a[26]<<19));
    int64_t a10 = 2097151 & (((uint32_t)a[26]>>2) | ((uint32_t)a[27]<<6) | ((uint32_t)a[28]<<14));
    int64_t a11 = ((uint32_t)a[28]>>7) | ((uint32_t)a[29]<<1) | ((uint32_t)a[30]<<9) | ((uint32_t)a[31]<<17);

    int64_t b0  = 2097151 & (((uint32_t)b[0]) | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16));
    int64_t b1  = 2097151 & (((uint32_t)b[2]>>5) | ((uint32_t)b[3]<<3) | ((uint32_t)b[4]<<11) | ((uint32_t)b[5]<<19));
    int64_t b2  = 2097151 & (((uint32_t)b[5]>>2) | ((uint32_t)b[6]<<6) | ((uint32_t)b[7]<<14));
    int64_t b3  = 2097151 & (((uint32_t)b[7]>>7) | ((uint32_t)b[8]<<1) | ((uint32_t)b[9]<<9) | ((uint32_t)b[10]<<17));
    int64_t b4  = 2097151 & (((uint32_t)b[10]>>4) | ((uint32_t)b[11]<<4) | ((uint32_t)b[12]<<12) | ((uint32_t)b[13]<<20));
    int64_t b5  = 2097151 & (((uint32_t)b[13]>>1) | ((uint32_t)b[14]<<7) | ((uint32_t)b[15]<<15));
    int64_t b6  = 2097151 & (((uint32_t)b[15]>>6) | ((uint32_t)b[16]<<2) | ((uint32_t)b[17]<<10) | ((uint32_t)b[18]<<18));
    int64_t b7  = 2097151 & (((uint32_t)b[18]>>3) | ((uint32_t)b[19]<<5) | ((uint32_t)b[20]<<13));
    int64_t b8  = 2097151 & (((uint32_t)b[21]) | ((uint32_t)b[22]<<8) | ((uint32_t)b[23]<<16));
    int64_t b9  = 2097151 & (((uint32_t)b[23]>>5) | ((uint32_t)b[24]<<3) | ((uint32_t)b[25]<<11) | ((uint32_t)b[26]<<19));
    int64_t b10 = 2097151 & (((uint32_t)b[26]>>2) | ((uint32_t)b[27]<<6) | ((uint32_t)b[28]<<14));
    int64_t b11 = ((uint32_t)b[28]>>7) | ((uint32_t)b[29]<<1) | ((uint32_t)b[30]<<9) | ((uint32_t)b[31]<<17);

    int64_t c0  = 2097151 & (((uint32_t)c[0]) | ((uint32_t)c[1]<<8) | ((uint32_t)c[2]<<16));
    int64_t c1  = 2097151 & (((uint32_t)c[2]>>5) | ((uint32_t)c[3]<<3) | ((uint32_t)c[4]<<11) | ((uint32_t)c[5]<<19));
    int64_t c2  = 2097151 & (((uint32_t)c[5]>>2) | ((uint32_t)c[6]<<6) | ((uint32_t)c[7]<<14));
    int64_t c3  = 2097151 & (((uint32_t)c[7]>>7) | ((uint32_t)c[8]<<1) | ((uint32_t)c[9]<<9) | ((uint32_t)c[10]<<17));
    int64_t c4  = 2097151 & (((uint32_t)c[10]>>4) | ((uint32_t)c[11]<<4) | ((uint32_t)c[12]<<12) | ((uint32_t)c[13]<<20));
    int64_t c5  = 2097151 & (((uint32_t)c[13]>>1) | ((uint32_t)c[14]<<7) | ((uint32_t)c[15]<<15));
    int64_t c6  = 2097151 & (((uint32_t)c[15]>>6) | ((uint32_t)c[16]<<2) | ((uint32_t)c[17]<<10) | ((uint32_t)c[18]<<18));
    int64_t c7  = 2097151 & (((uint32_t)c[18]>>3) | ((uint32_t)c[19]<<5) | ((uint32_t)c[20]<<13));
    int64_t c8  = 2097151 & (((uint32_t)c[21]) | ((uint32_t)c[22]<<8) | ((uint32_t)c[23]<<16));
    int64_t c9  = 2097151 & (((uint32_t)c[23]>>5) | ((uint32_t)c[24]<<3) | ((uint32_t)c[25]<<11) | ((uint32_t)c[26]<<19));
    int64_t c10 = 2097151 & (((uint32_t)c[26]>>2) | ((uint32_t)c[27]<<6) | ((uint32_t)c[28]<<14));
    int64_t c11 = ((uint32_t)c[28]>>7) | ((uint32_t)c[29]<<1) | ((uint32_t)c[30]<<9) | ((uint32_t)c[31]<<17);

    /* Schoolbook multiply a*b + c into 24 limbs */
    int64_t s0  = c0 + a0*b0;
    int64_t s1  = c1 + a0*b1 + a1*b0;
    int64_t s2  = c2 + a0*b2 + a1*b1 + a2*b0;
    int64_t s3  = c3 + a0*b3 + a1*b2 + a2*b1 + a3*b0;
    int64_t s4  = c4 + a0*b4 + a1*b3 + a2*b2 + a3*b1 + a4*b0;
    int64_t s5  = c5 + a0*b5 + a1*b4 + a2*b3 + a3*b2 + a4*b1 + a5*b0;
    int64_t s6  = c6 + a0*b6 + a1*b5 + a2*b4 + a3*b3 + a4*b2 + a5*b1 + a6*b0;
    int64_t s7  = c7 + a0*b7 + a1*b6 + a2*b5 + a3*b4 + a4*b3 + a5*b2 + a6*b1 + a7*b0;
    int64_t s8  = c8 + a0*b8 + a1*b7 + a2*b6 + a3*b5 + a4*b4 + a5*b3 + a6*b2 + a7*b1 + a8*b0;
    int64_t s9  = c9 + a0*b9 + a1*b8 + a2*b7 + a3*b6 + a4*b5 + a5*b4 + a6*b3 + a7*b2 + a8*b1 + a9*b0;
    int64_t s10 = c10 + a0*b10 + a1*b9 + a2*b8 + a3*b7 + a4*b6 + a5*b5 + a6*b4 + a7*b3 + a8*b2 + a9*b1 + a10*b0;
    int64_t s11 = c11 + a0*b11 + a1*b10 + a2*b9 + a3*b8 + a4*b7 + a5*b6 + a6*b5 + a7*b4 + a8*b3 + a9*b2 + a10*b1 + a11*b0;
    int64_t s12 = a1*b11 + a2*b10 + a3*b9 + a4*b8 + a5*b7 + a6*b6 + a7*b5 + a8*b4 + a9*b3 + a10*b2 + a11*b1;
    int64_t s13 = a2*b11 + a3*b10 + a4*b9 + a5*b8 + a6*b7 + a7*b6 + a8*b5 + a9*b4 + a10*b3 + a11*b2;
    int64_t s14 = a3*b11 + a4*b10 + a5*b9 + a6*b8 + a7*b7 + a8*b6 + a9*b5 + a10*b4 + a11*b3;
    int64_t s15 = a4*b11 + a5*b10 + a6*b9 + a7*b8 + a8*b7 + a9*b6 + a10*b5 + a11*b4;
    int64_t s16 = a5*b11 + a6*b10 + a7*b9 + a8*b8 + a9*b7 + a10*b6 + a11*b5;
    int64_t s17 = a6*b11 + a7*b10 + a8*b9 + a9*b8 + a10*b7 + a11*b6;
    int64_t s18 = a7*b11 + a8*b10 + a9*b9 + a10*b8 + a11*b7;
    int64_t s19 = a8*b11 + a9*b10 + a10*b9 + a11*b8;
    int64_t s20 = a9*b11 + a10*b10 + a11*b9;
    int64_t s21 = a10*b11 + a11*b10;
    int64_t s22 = a11*b11;
    int64_t s23 = 0;
    int64_t carry;

    /* Initial carry chain on full 24-limb product: even then odd */
    carry = (s0 + (1<<20)) >> 21; s1 += carry; s0 -= carry << 21;
    carry = (s2 + (1<<20)) >> 21; s3 += carry; s2 -= carry << 21;
    carry = (s4 + (1<<20)) >> 21; s5 += carry; s4 -= carry << 21;
    carry = (s6 + (1<<20)) >> 21; s7 += carry; s6 -= carry << 21;
    carry = (s8 + (1<<20)) >> 21; s9 += carry; s8 -= carry << 21;
    carry = (s10 + (1<<20)) >> 21; s11 += carry; s10 -= carry << 21;
    carry = (s12 + (1<<20)) >> 21; s13 += carry; s12 -= carry << 21;
    carry = (s14 + (1<<20)) >> 21; s15 += carry; s14 -= carry << 21;
    carry = (s16 + (1<<20)) >> 21; s17 += carry; s16 -= carry << 21;
    carry = (s18 + (1<<20)) >> 21; s19 += carry; s18 -= carry << 21;
    carry = (s20 + (1<<20)) >> 21; s21 += carry; s20 -= carry << 21;
    carry = (s22 + (1<<20)) >> 21; s23 += carry; s22 -= carry << 21;
    carry = (s1 + (1<<20)) >> 21; s2 += carry; s1 -= carry << 21;
    carry = (s3 + (1<<20)) >> 21; s4 += carry; s3 -= carry << 21;
    carry = (s5 + (1<<20)) >> 21; s6 += carry; s5 -= carry << 21;
    carry = (s7 + (1<<20)) >> 21; s8 += carry; s7 -= carry << 21;
    carry = (s9 + (1<<20)) >> 21; s10 += carry; s9 -= carry << 21;
    carry = (s11 + (1<<20)) >> 21; s12 += carry; s11 -= carry << 21;
    carry = (s13 + (1<<20)) >> 21; s14 += carry; s13 -= carry << 21;
    carry = (s15 + (1<<20)) >> 21; s16 += carry; s15 -= carry << 21;
    carry = (s17 + (1<<20)) >> 21; s18 += carry; s17 -= carry << 21;
    carry = (s19 + (1<<20)) >> 21; s20 += carry; s19 -= carry << 21;
    carry = (s21 + (1<<20)) >> 21; s22 += carry; s21 -= carry << 21;

    /* Pass 1: fold s23..s18 into s6..s17 */
    s11 += s23 * 666643; s12 += s23 * 470296; s13 += s23 * 654183;
    s14 -= s23 * 997805; s15 += s23 * 136657; s16 -= s23 * 683901; s23 = 0;
    s10 += s22 * 666643; s11 += s22 * 470296; s12 += s22 * 654183;
    s13 -= s22 * 997805; s14 += s22 * 136657; s15 -= s22 * 683901; s22 = 0;
    s9  += s21 * 666643; s10 += s21 * 470296; s11 += s21 * 654183;
    s12 -= s21 * 997805; s13 += s21 * 136657; s14 -= s21 * 683901; s21 = 0;
    s8  += s20 * 666643; s9  += s20 * 470296; s10 += s20 * 654183;
    s11 -= s20 * 997805; s12 += s20 * 136657; s13 -= s20 * 683901; s20 = 0;
    s7  += s19 * 666643; s8  += s19 * 470296; s9  += s19 * 654183;
    s10 -= s19 * 997805; s11 += s19 * 136657; s12 -= s19 * 683901; s19 = 0;
    s6  += s18 * 666643; s7  += s18 * 470296; s8  += s18 * 654183;
    s9  -= s18 * 997805; s10 += s18 * 136657; s11 -= s18 * 683901; s18 = 0;

    /* Mid carry on s6..s17: even then odd */
    carry = (s6 + (1<<20)) >> 21; s7 += carry; s6 -= carry << 21;
    carry = (s8 + (1<<20)) >> 21; s9 += carry; s8 -= carry << 21;
    carry = (s10 + (1<<20)) >> 21; s11 += carry; s10 -= carry << 21;
    carry = (s12 + (1<<20)) >> 21; s13 += carry; s12 -= carry << 21;
    carry = (s14 + (1<<20)) >> 21; s15 += carry; s14 -= carry << 21;
    carry = (s16 + (1<<20)) >> 21; s17 += carry; s16 -= carry << 21;
    carry = (s7 + (1<<20)) >> 21; s8 += carry; s7 -= carry << 21;
    carry = (s9 + (1<<20)) >> 21; s10 += carry; s9 -= carry << 21;
    carry = (s11 + (1<<20)) >> 21; s12 += carry; s11 -= carry << 21;
    carry = (s13 + (1<<20)) >> 21; s14 += carry; s13 -= carry << 21;
    carry = (s15 + (1<<20)) >> 21; s16 += carry; s15 -= carry << 21;

    /* Pass 2: fold s17..s12 into s0..s11 */
    s5 += s17 * 666643; s6  += s17 * 470296; s7  += s17 * 654183;
    s8 -= s17 * 997805; s9  += s17 * 136657; s10 -= s17 * 683901; s17 = 0;
    s4 += s16 * 666643; s5  += s16 * 470296; s6  += s16 * 654183;
    s7 -= s16 * 997805; s8  += s16 * 136657; s9  -= s16 * 683901; s16 = 0;
    s3 += s15 * 666643; s4  += s15 * 470296; s5  += s15 * 654183;
    s6 -= s15 * 997805; s7  += s15 * 136657; s8  -= s15 * 683901; s15 = 0;
    s2 += s14 * 666643; s3  += s14 * 470296; s4  += s14 * 654183;
    s5 -= s14 * 997805; s6  += s14 * 136657; s7  -= s14 * 683901; s14 = 0;
    s1 += s13 * 666643; s2  += s13 * 470296; s3  += s13 * 654183;
    s4 -= s13 * 997805; s5  += s13 * 136657; s6  -= s13 * 683901; s13 = 0;
    s0 += s12 * 666643; s1  += s12 * 470296; s2  += s12 * 654183;
    s3 -= s12 * 997805; s4  += s12 * 136657; s5  -= s12 * 683901; s12 = 0;

    /* Carry on s0..s11: even then odd */
    carry = (s0 + (1<<20)) >> 21; s1 += carry; s0 -= carry << 21;
    carry = (s2 + (1<<20)) >> 21; s3 += carry; s2 -= carry << 21;
    carry = (s4 + (1<<20)) >> 21; s5 += carry; s4 -= carry << 21;
    carry = (s6 + (1<<20)) >> 21; s7 += carry; s6 -= carry << 21;
    carry = (s8 + (1<<20)) >> 21; s9 += carry; s8 -= carry << 21;
    carry = (s10 + (1<<20)) >> 21; s11 += carry; s10 -= carry << 21;
    carry = (s1 + (1<<20)) >> 21; s2 += carry; s1 -= carry << 21;
    carry = (s3 + (1<<20)) >> 21; s4 += carry; s3 -= carry << 21;
    carry = (s5 + (1<<20)) >> 21; s6 += carry; s5 -= carry << 21;
    carry = (s7 + (1<<20)) >> 21; s8 += carry; s7 -= carry << 21;
    carry = (s9 + (1<<20)) >> 21; s10 += carry; s9 -= carry << 21;
    carry = (s11 + (1<<20)) >> 21; s12 += carry; s11 -= carry << 21;

    /* Reduce s12 into s0..s5 */
    s0 += s12 * 666643; s1 += s12 * 470296; s2 += s12 * 654183;
    s3 -= s12 * 997805; s4 += s12 * 136657; s5 -= s12 * 683901; s12 = 0;

    /* Sequential carry (simple truncation) */
    carry = s0 >> 21; s1 += carry; s0 -= carry << 21;
    carry = s1 >> 21; s2 += carry; s1 -= carry << 21;
    carry = s2 >> 21; s3 += carry; s2 -= carry << 21;
    carry = s3 >> 21; s4 += carry; s3 -= carry << 21;
    carry = s4 >> 21; s5 += carry; s4 -= carry << 21;
    carry = s5 >> 21; s6 += carry; s5 -= carry << 21;
    carry = s6 >> 21; s7 += carry; s6 -= carry << 21;
    carry = s7 >> 21; s8 += carry; s7 -= carry << 21;
    carry = s8 >> 21; s9 += carry; s8 -= carry << 21;
    carry = s9 >> 21; s10 += carry; s9 -= carry << 21;
    carry = s10 >> 21; s11 += carry; s10 -= carry << 21;
    carry = s11 >> 21; s12 += carry; s11 -= carry << 21;

    /* Reduce s12 again */
    s0 += s12 * 666643; s1 += s12 * 470296; s2 += s12 * 654183;
    s3 -= s12 * 997805; s4 += s12 * 136657; s5 -= s12 * 683901; s12 = 0;

    /* Final sequential carry */
    carry = s0 >> 21; s1 += carry; s0 -= carry << 21;
    carry = s1 >> 21; s2 += carry; s1 -= carry << 21;
    carry = s2 >> 21; s3 += carry; s2 -= carry << 21;
    carry = s3 >> 21; s4 += carry; s3 -= carry << 21;
    carry = s4 >> 21; s5 += carry; s4 -= carry << 21;
    carry = s5 >> 21; s6 += carry; s5 -= carry << 21;
    carry = s6 >> 21; s7 += carry; s6 -= carry << 21;
    carry = s7 >> 21; s8 += carry; s7 -= carry << 21;
    carry = s8 >> 21; s9 += carry; s8 -= carry << 21;
    carry = s9 >> 21; s10 += carry; s9 -= carry << 21;
    carry = s10 >> 21; s11 += carry; s10 -= carry << 21;

    /* Pack 12 x 21-bit limbs into 32 bytes LE */
    out[0]  = (uint8_t)(s0);
    out[1]  = (uint8_t)(s0 >> 8);
    out[2]  = (uint8_t)((s0 >> 16) | (s1 << 5));
    out[3]  = (uint8_t)(s1 >> 3);
    out[4]  = (uint8_t)(s1 >> 11);
    out[5]  = (uint8_t)((s1 >> 19) | (s2 << 2));
    out[6]  = (uint8_t)(s2 >> 6);
    out[7]  = (uint8_t)((s2 >> 14) | (s3 << 7));
    out[8]  = (uint8_t)(s3 >> 1);
    out[9]  = (uint8_t)(s3 >> 9);
    out[10] = (uint8_t)((s3 >> 17) | (s4 << 4));
    out[11] = (uint8_t)(s4 >> 4);
    out[12] = (uint8_t)(s4 >> 12);
    out[13] = (uint8_t)((s4 >> 20) | (s5 << 1));
    out[14] = (uint8_t)(s5 >> 7);
    out[15] = (uint8_t)((s5 >> 15) | (s6 << 6));
    out[16] = (uint8_t)(s6 >> 2);
    out[17] = (uint8_t)(s6 >> 10);
    out[18] = (uint8_t)((s6 >> 18) | (s7 << 3));
    out[19] = (uint8_t)(s7 >> 5);
    out[20] = (uint8_t)(s7 >> 13);
    out[21] = (uint8_t)(s8);
    out[22] = (uint8_t)(s8 >> 8);
    out[23] = (uint8_t)((s8 >> 16) | (s9 << 5));
    out[24] = (uint8_t)(s9 >> 3);
    out[25] = (uint8_t)(s9 >> 11);
    out[26] = (uint8_t)((s9 >> 19) | (s10 << 2));
    out[27] = (uint8_t)(s10 >> 6);
    out[28] = (uint8_t)((s10 >> 14) | (s11 << 7));
    out[29] = (uint8_t)(s11 >> 1);
    out[30] = (uint8_t)(s11 >> 9);
    out[31] = (uint8_t)(s11 >> 17);
}

/* ================================================================
 * Ed25519 public API — RFC 8032
 * ================================================================ */

void ed25519_keypair(uint8_t pk[32], uint8_t sk[64]) {
    uint8_t seed[32], h[64];
    crypt_fill_random(seed, 32);

    sha512(seed, 32, h);
    h[0] &= 248;       /* clamp */
    h[31] &= 127;
    h[31] |= 64;

    ge A;
    ge_scalarmult_base(&A, h);
    ge_tobytes(pk, &A);

    memcpy(sk, seed, 32);     /* sk = seed || pk */
    memcpy(sk + 32, pk, 32);
}

void ed25519_sign(const uint8_t sk[64], const uint8_t *msg, size_t len,
                  uint8_t sig[64]) {
    uint8_t h[64], nonce[64], hram[64];
    Sha512Ctx ctx;

    /* expand secret key */
    sha512(sk, 32, h);
    h[0] &= 248;
    h[31] &= 127;
    h[31] |= 64;

    /* r = SHA-512(h[32..63] || msg) mod L */
    sha512_init(&ctx);
    sha512_update(&ctx, h + 32, 32);
    sha512_update(&ctx, msg, len);
    sha512_final(&ctx, nonce);

    uint8_t r_reduced[32];
    sc_reduce(r_reduced, nonce);

    /* R = r * B */
    ge R;
    ge_scalarmult_base(&R, r_reduced);
    ge_tobytes(sig, &R);            /* first 32 bytes of sig = R */

    /* S = r + SHA-512(R || pk || msg) * a mod L */
    sha512_init(&ctx);
    sha512_update(&ctx, sig, 32);           /* R */
    sha512_update(&ctx, sk + 32, 32);       /* pk */
    sha512_update(&ctx, msg, len);
    sha512_final(&ctx, hram);

    uint8_t hram_reduced[32];
    sc_reduce(hram_reduced, hram);

    sc_muladd(sig + 32, hram_reduced, h, r_reduced);   /* S = hram*a + r */
}

int ed25519_verify(const uint8_t pk[32], const uint8_t *msg, size_t len,
                   const uint8_t sig[64]) {
    ge A;
    uint8_t hram_hash[64], hram_reduced[32];
    Sha512Ctx ctx;

    /* decode public key */
    if (ge_frombytes(&A, pk) != 0) return -1;

    /* negate A for verification: we check [S]B - [H]A == R */
    fe_neg(A.X, A.X);
    fe_neg(A.T, A.T);

    /* H = SHA-512(R || pk || msg) */
    sha512_init(&ctx);
    sha512_update(&ctx, sig, 32);       /* R */
    sha512_update(&ctx, pk, 32);
    sha512_update(&ctx, msg, len);
    sha512_final(&ctx, hram_hash);

    sc_reduce(hram_reduced, hram_hash);

    /* R_check = [S]B + [H](-A) = [S]B - [H]A */
    ge sB, hA, sum;
    ge_scalarmult_base(&sB, sig + 32);
    ge_scalarmult(&hA, hram_reduced, &A);
    ge_add(&sum, &sB, &hA);

    uint8_t R_computed[32];
    ge_tobytes(R_computed, &sum);

    /* constant-time compare R */
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= R_computed[i] ^ sig[i];
    return diff == 0 ? 0 : -1;
}
