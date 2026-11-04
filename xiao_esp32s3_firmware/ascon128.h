/**
 * @file ascon128.h
 * @brief NIST ASCON-128 Authenticated Encryption with Associated Data (AEAD)
 * Bit-for-bit parity with dashboard web client (ascon.js) and reference C99 test bench.
 * Part of: Secure Wearable Health Monitoring System (ASCON-128 + ESP32-S3)
 */

#ifndef ASCON128_H
#define ASCON128_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

// Master 128-bit key matching test bench and web telemetry engine
static const uint8_t ASCON128_DEFAULT_KEY[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

#define ROTR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static inline uint64_t ascon_load64_be(const uint8_t *b) {
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
           ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
           ((uint64_t)b[6] << 8)  | ((uint64_t)b[7]);
}

static inline void ascon_store64_be(uint8_t *b, uint64_t x) {
    b[0] = (uint8_t)(x >> 56);
    b[1] = (uint8_t)(x >> 48);
    b[2] = (uint8_t)(x >> 40);
    b[3] = (uint8_t)(x >> 32);
    b[4] = (uint8_t)(x >> 24);
    b[5] = (uint8_t)(x >> 16);
    b[6] = (uint8_t)(x >> 8);
    b[7] = (uint8_t)(x);
}

static const uint8_t ASCON_ROUND_CONSTANTS[12] = {
    0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87, 0x78, 0x69, 0x5a, 0x4b
};

/**
 * @brief 320-bit Ascon Core Permutation (5 x 64-bit words)
 */
static inline void ascon_permutation(uint64_t s[5], int rounds) {
    for (int i = 12 - rounds; i < 12; i++) {
        // 1. Constant addition
        s[2] ^= ASCON_ROUND_CONSTANTS[i];

        // 2. Non-linear Substitution Layer (5-bit S-box)
        s[0] ^= s[4]; s[4] ^= s[3]; s[2] ^= s[1];
        uint64_t t0 = s[0] ^ (~s[1] & s[2]);
        uint64_t t1 = s[1] ^ (~s[2] & s[3]);
        uint64_t t2 = s[2] ^ (~s[3] & s[4]);
        uint64_t t3 = s[3] ^ (~s[4] & s[0]);
        uint64_t t4 = s[4] ^ (~s[0] & s[1]);
        t1 ^= t0; t0 ^= t4; t3 ^= t2; t2 = ~t2;

        // 3. Linear Diffusion Layer
        s[0] = t0 ^ ROTR64(t0, 19) ^ ROTR64(t0, 28);
        s[1] = t1 ^ ROTR64(t1, 61) ^ ROTR64(t1, 39);
        s[2] = t2 ^ ROTR64(t2,  1) ^ ROTR64(t2,  6);
        s[3] = t3 ^ ROTR64(t3, 10) ^ ROTR64(t3, 17);
        s[4] = t4 ^ ROTR64(t4,  7) ^ ROTR64(t4, 41);
    }
}

/**
 * @brief ASCON-128 AEAD Encrypt
 * 
 * @param key       16-byte secret master key
 * @param nonce     16-byte fresh counter-based nonce
 * @param ad        Associated Data pointer (e.g. device ID, firmware string)
 * @param ad_len    Length of Associated Data in bytes
 * @param pt        Plaintext input payload pointer
 * @param pt_len    Length of plaintext payload in bytes
 * @param ct        Destination buffer for ciphertext (at least pt_len bytes)
 * @param tag       Destination buffer for 16-byte authentication tag
 */
static inline void ascon_128_encrypt(
    const uint8_t *key,
    const uint8_t *nonce,
    const uint8_t *ad,
    size_t ad_len,
    const uint8_t *pt,
    size_t pt_len,
    uint8_t *ct,
    uint8_t *tag
) {
    uint64_t s[5];
    uint64_t k0 = ascon_load64_be(key);
    uint64_t k1 = ascon_load64_be(key + 8);
    uint64_t n0 = ascon_load64_be(nonce);
    uint64_t n1 = ascon_load64_be(nonce + 8);

    // Initialization: IV for Ascon-128 = 0x80400c0600000000
    s[0] = 0x80400c0600000000ULL;
    s[1] = k0;
    s[2] = k1;
    s[3] = n0;
    s[4] = n1;
    ascon_permutation(s, 12);
    s[3] ^= k0;
    s[4] ^= k1;

    // Associated Data Processing
    if (ad_len > 0) {
        size_t ad_idx = 0;
        while (ad_len - ad_idx >= 8) {
            s[0] ^= ascon_load64_be(ad + ad_idx);
            ascon_permutation(s, 6);
            ad_idx += 8;
        }
        size_t rem = ad_len - ad_idx;
        uint8_t pad[8] = {0};
        memcpy(pad, ad + ad_idx, rem);
        pad[rem] = 0x80;
        s[0] ^= ascon_load64_be(pad);
        ascon_permutation(s, 6);
    }
    s[4] ^= 1; // Domain separator

    // Plaintext Processing & Ciphertext Generation
    size_t pt_idx = 0;
    while (pt_len - pt_idx >= 8) {
        s[0] ^= ascon_load64_be(pt + pt_idx);
        ascon_store64_be(ct + pt_idx, s[0]);
        ascon_permutation(s, 6);
        pt_idx += 8;
    }
    size_t rem = pt_len - pt_idx;
    uint8_t pad[8] = {0};
    memcpy(pad, pt + pt_idx, rem);
    pad[rem] = 0x80;
    s[0] ^= ascon_load64_be(pad);
    uint8_t last_block[8];
    ascon_store64_be(last_block, s[0]);
    memcpy(ct + pt_idx, last_block, rem);

    // Finalization & 16-byte Authentication Tag
    s[1] ^= k0;
    s[2] ^= k1;
    ascon_permutation(s, 12);
    s[3] ^= k0;
    s[4] ^= k1;
    ascon_store64_be(tag, s[3]);
    ascon_store64_be(tag + 8, s[4]);
}

/**
 * @brief Convert binary buffer to lower-case hexadecimal string.
 */
static inline void bytes_to_hex(const uint8_t *src, size_t len, char *dest) {
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        dest[i * 2]     = hex_chars[(src[i] >> 4) & 0x0F];
        dest[i * 2 + 1] = hex_chars[src[i] & 0x0F];
    }
    dest[len * 2] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* ASCON128_H */
