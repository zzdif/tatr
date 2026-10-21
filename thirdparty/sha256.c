/*
 * SHA-256 implementation derived from RFC 6234, section 8.2.2.
 * https://www.rfc-editor.org/rfc/rfc6234#section-8.2.2
 *
 * Copyright (c) 2011 IETF Trust and the persons identified as
 * authors of the code.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * - Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and
 *   the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 *
 * - Neither the name of Internet Society, IETF or IETF Trust, nor
 *   the names of specific contributors, may be used to endorse or
 *   promote products derived from this software without specific
 *   prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "sha256.h"

#include <stdint.h>
#include <string.h>

static const uint32_t scheduler_sha256_k[64] = {
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
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t scheduler_sha256_rotr(uint32_t value, unsigned int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

static void scheduler_sha256_block(uint32_t hash[8], const unsigned char block[64])
{
    uint32_t words[64];
    for (unsigned int i = 0; i < 16; ++i) {
        unsigned int j = i * 4;
        words[i] = ((uint32_t) block[j] << 24) |
                   ((uint32_t) block[j + 1] << 16) |
                   ((uint32_t) block[j + 2] << 8) |
                   block[j + 3];
    }
    for (unsigned int i = 16; i < 64; ++i) {
        uint32_t s0 = scheduler_sha256_rotr(words[i - 15], 7) ^
                      scheduler_sha256_rotr(words[i - 15], 18) ^
                      (words[i - 15] >> 3);
        uint32_t s1 = scheduler_sha256_rotr(words[i - 2], 17) ^
                      scheduler_sha256_rotr(words[i - 2], 19) ^
                      (words[i - 2] >> 10);
        words[i] = s1 + words[i - 7] + s0 + words[i - 16];
    }

    uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
    uint32_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];
    for (unsigned int i = 0; i < 64; ++i) {
        uint32_t s1 = scheduler_sha256_rotr(e, 6) ^
                      scheduler_sha256_rotr(e, 11) ^
                      scheduler_sha256_rotr(e, 25);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + choice + scheduler_sha256_k[i] + words[i];
        uint32_t s0 = scheduler_sha256_rotr(a, 2) ^
                      scheduler_sha256_rotr(a, 13) ^
                      scheduler_sha256_rotr(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
}

void scheduler_sha256(const void *data, size_t length, char out[72])
{
    static const char hex[] = "0123456789abcdef";
    uint32_t hash[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    const unsigned char *input = data;
    size_t original_length = length;
    unsigned char block[64] = {0};

    while (length >= sizeof(block)) {
        scheduler_sha256_block(hash, input);
        input += sizeof(block);
        length -= sizeof(block);
    }

    if (length) memcpy(block, input, length);
    block[length] = 0x80;
    if (length >= 56) {
        scheduler_sha256_block(hash, block);
        memset(block, 0, sizeof(block));
    }

    uint64_t bit_length = (uint64_t) original_length * UINT64_C(8);
    for (unsigned int i = 0; i < 8; ++i) {
        block[63 - i] = (unsigned char) (bit_length >> (i * 8));
    }
    scheduler_sha256_block(hash, block);

    memcpy(out, "sha256:", 7);
    for (unsigned int i = 0; i < 8; ++i) {
        for (unsigned int j = 0; j < 8; ++j) {
            out[7 + i * 8 + j] = hex[(hash[i] >> (28 - j * 4)) & 0x0f];
        }
    }
    out[71] = '\0';
}
