/* C adaptation of LZokay's MIT-licensed decompressor, using bounded integer
 * offsets instead of forming out-of-range pointers. Upstream db2df1fcbebc2ed
 * https://github.com/AxioDL/lzokay . No liblzo/minilzo implementation used.
 *
 * The MIT License
 * Copyright (c) 2018 Jack Andersen
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "inktag_lzo1x.h"

static bool extended(const uint8_t *src, size_t n, size_t *i, size_t *count) {
    uint8_t next;
    do {
        if (*i == n || *count > SIZE_MAX - 255u) return false;
        next = src[(*i)++];
        *count += next ? next : 255u;
    } while (!next);
    return true;
}

/* Compile the decode-only entry as its original direct function, including
 * its original copy expressions. The validator profile shares this parser,
 * while allowing a NULL sink; it must not change decode-only compiler IR. */
#if INKTAG_LZO1X_VALIDATE_ENABLED
#define INKTAG_LZO_LITERAL { if (dst) dst[o] = src[i]; ++o; ++i; }
#define INKTAG_LZO_MATCH { if (dst) dst[o] = dst[o - distance]; ++o; }
#if defined(__GNUC__) || defined(__clang__)
static inline __attribute__((always_inline))
#else
static inline
#endif
bool lzo1x_process(const uint8_t *src, size_t n,
                    uint8_t *dst, size_t capacity, size_t *written)
#else
#define INKTAG_LZO_LITERAL dst[o++] = src[i++];
#define INKTAG_LZO_MATCH { dst[o] = dst[o - distance]; ++o; }
bool inktag_lzo1x_decode(const uint8_t *src, size_t n,
                        uint8_t *dst, size_t capacity, size_t *written)
#endif
{
    size_t i = 0u, o = 0u, state = 0u, count, distance, literals, word;
    uint8_t inst;
    if (written) *written = 0u;
#if INKTAG_LZO1X_VALIDATE_ENABLED
    if (!src || !written || n < 3u) return false;
#else
    if (!src || !dst || !written || n < 3u) return false;
#endif
    if (src[0] >= 18u) {
        count = (size_t)src[i++] - 17u;
        if (count > n - i || count > capacity) return false;
        while (count--) INKTAG_LZO_LITERAL
        state = o >= 4u ? 4u : o;
    }
    for (;;) {
        if (i == n) return false;
        inst = src[i++];
        if (inst >= 64u) {
            if (i == n) return false;
            distance = ((size_t)src[i++] << 3u) + ((inst >> 2u) & 7u) + 1u;
            count = (inst >> 5u) + 1u;
            literals = inst & 3u;
        } else if (inst >= 16u) {
            count = (inst & (inst >= 32u ? 31u : 7u)) + 2u;
            if (count == 2u) {
                count += inst >= 32u ? 31u : 7u;
                if (!extended(src, n, &i, &count)) return false;
            }
            if (n - i < 2u) return false;
            word = src[i] | ((size_t)src[i + 1u] << 8u); i += 2u;
            literals = word & 3u;
            if (inst >= 32u) distance = (word >> 2u) + 1u;
            else {
                distance = ((size_t)(inst & 8u) << 11u) + (word >> 2u);
                if (!distance) {
                    if (count != 3u || literals || i != n) return false;
                    *written = o;
                    return true;
                }
                distance += 16384u;
            }
        } else if (!state) {
            count = inst + 3u;
            if (!inst) {
                count += 15u;
                if (!extended(src, n, &i, &count)) return false;
            }
            if (count > n - i || count > capacity - o) return false;
            while (count--) INKTAG_LZO_LITERAL
            state = 4u;
            continue;
        } else {
            if (i == n) return false;
            distance = (inst >> 2u) + ((size_t)src[i++] << 2u) +
                       (state == 4u ? 2049u : 1u);
            count = state == 4u ? 3u : 2u;
            literals = inst & 3u;
        }
        if (distance > o || count > capacity - o ||
            literals > capacity - o - count || literals > n - i) return false;
        while (count--) INKTAG_LZO_MATCH
        state = literals;
        while (literals--) INKTAG_LZO_LITERAL
    }
}

#undef INKTAG_LZO_LITERAL
#undef INKTAG_LZO_MATCH

#if INKTAG_LZO1X_VALIDATE_ENABLED
bool inktag_lzo1x_decode(const uint8_t *src, size_t n,
                        uint8_t *dst, size_t capacity, size_t *written) {
    if (!dst) { if (written) *written=0u; return false; }
    return lzo1x_process(src,n,dst,capacity,written);
}

bool inktag_lzo1x_validate(const uint8_t *src, size_t n, size_t decoded_length) {
    size_t written=0u;
    return lzo1x_process(src,n,NULL,decoded_length,&written) && written==decoded_length;
}
#endif
