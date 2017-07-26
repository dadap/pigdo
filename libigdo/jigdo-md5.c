/*
 * pigdo: parallel implementation of jigsaw download
 * Copyright (c) 2017 Daniel Dadap
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "jigdo-md5.h"
#include "jigdo-md5-private.h"
#include "md5.h"

/**
 * @brief tests whether a base64 symbol has been flagged as valid in the table
 */
static bool b64ValidSymbol(uint8_t symbol)
{
    return (symbol & (1 << 7)) != 1 << 7;
}

/**
 * @brief extracts the value of a base64 symbol from its entry in the table
 */
static uint8_t b64Symbol(uint8_t symbol)
{
    return symbol & 63;
}

/**
 * @brief Extracts a 3-byte integer value from four base64 characters
 *
 * @param in A base64 string
 *
 * @return A signed 32-bit integer, the lower three bytes of which contain
 * the result of decoding the first four bytes of the base64 string, or
 * a negative value if an invalid symbol was encountered.
 *
 * @note This is not a general-purpose base64 implementation, and is only
 * suitable for decoding jigdo md5sums encoded in either jigdo or standard
 * base64. Since jigdo base64 does not pad to a multiple of 3 bytes, there
 * is a hardcoded shift when reaching the end of the string.
 */
static int32_t base64To3ByteIntVal(const char *in)
{
    int32_t val, i;

    /* Any symbols not explicitly assigned in table[] will be zeroed out;
     * set the high bit when assigning a symbol as a flag that it is valid
     * Most of the lookup table will be unassigned characters, which is a
     * minor waste of space, but allows lookups in constant time. */
    #define b64(key, val) [key] = (1 << 7) | val,

    /* jigdo base64 uses '-' and '_' instead of '+' and '/', respectively:
     * this difference is non-conflicting, so populate the table with entries
     * for both variants just in case a jigdo file uses real base64 instead
     * of the jigdo base64 variant. */
    static const uint8_t table[256] = {
        b64('+',62) b64('-',62) b64('/',63) b64('0',52) b64('1',53) b64('2',54)
        b64('3',55) b64('4',56) b64('5',57) b64('6',58) b64('7',59) b64('8',60)
        b64('9',61) b64('A', 0) b64('B', 1) b64('C', 2) b64('D', 3) b64('E', 4)
        b64('F', 5) b64('G', 6) b64('H', 7) b64('I', 8) b64('J', 9) b64('K',10)
        b64('L',11) b64('M',12) b64('N',13) b64('O',14) b64('P',15) b64('Q',16)
        b64('R',17) b64('S',18) b64('T',19) b64('U',20) b64('V',21) b64('W',22)
        b64('X',23) b64('Y',24) b64('Z',25) b64('_',63) b64('a',26) b64('b',27)
        b64('c',28) b64('d',29) b64('e',30) b64('f',31) b64('g',32) b64('h',33)
        b64('i',34) b64('j',35) b64('k',36) b64('l',37) b64('m',38) b64('n',39)
        b64('o',40) b64('p',41) b64('q',42) b64('r',43) b64('s',44) b64('t',45)
        b64('u',46) b64('v',47) b64('w',48) b64('x',49) b64('y',50) b64('z',51)
    };

    #undef b64

    for (i = val = 0; i < 4; i++) {
        if (in[i] == '\0' || in[i] == '=') {
            /* End of base64 string: jigdo base64 is unpadded, and we know
             * exactly how long the unbase64-ed data is supposed to be, so
             * shift the partially decoded result to where deBase64MD5Sum
             * will expect it to be. MD5 sums are 16 bytes long, and base64
             * uses 4 symbols to encode 3 bytes: 24 symbols would encode 18
             * bytes, but we only need 22 for 16, which leaves a deficit of
             * 2 symbols, corresponding to 3 nybbles or 12 bits. Theoretically
             * we'd never encounter the == padding that a real base64 encoding
             * of a 16 byte value would have, since the caller would have split
             * a key/value pair on '=' anyway, but handle it just in case. */
            val <<= 12;
            break;
        }

        if (!b64ValidSymbol(in[i])) {
            return -1;
        }

        val *= 64;
        val += b64Symbol(table[(uint8_t) in[i]]);
    }

    return val;
}

/**
 * @brief Set byte number @p byte to @p newval in @p md5
 */
static void md5SetByte(md5Checksum *md5, int byte, uint8_t newval)
{
    int word = byte / 4;
    int subbyte = byte % 4;
    uint8_t *dest = ((uint8_t *) &(md5->sum[word])) + subbyte;

    assert(byte >= 0 && byte < 16);

    *dest = newval;
}

/**
 * @brief extract byte number @p byte from @p word
 */
static uint8_t getByteFromWord(int32_t word, int byte)
{
    int shift_distance = 8 * (2 - byte);
    uint32_t mask = 0xff << shift_distance;

    assert(byte >= 0 && byte < 3);

    return (word & mask) >> shift_distance;
}

bool deBase64MD5Sum(const char* in, md5Checksum *out)
{
    int i, byte;
    static const int md5base64characters = 22;

    assert(strlen(in) == md5base64characters);

    for (i = byte = 0; i < md5base64characters; i += 4) {
        int32_t j, decoded = base64To3ByteIntVal(in + i);

        if (decoded < 0) {
            return false;
        }

        for (j = 0; j < 3 && byte < 16; j++, byte++) {
            md5SetByte(out, byte, getByteFromWord(decoded, j));
        }
    }

    return true;
}

int md5Cmp(const md5Checksum *a, const md5Checksum *b)
{
    return memcmp(a, b, sizeof(*a));
}

void md5SumToString(md5Checksum md5, char *out)
{
    int i;
    for (i = 0; i < (sizeof(md5) / sizeof(md5.sum[0])); i++) {
        int j;
        for (j = 0; j < sizeof(md5.sum[0]); j++) {
            sprintf(out, "%02x", *((uint8_t*) &md5.sum[i] + j));
            out += 2;
        }
    }
    out[0] = '\0';
}

md5Checksum md5MemOneShot(const void *in, size_t len)
{
    struct MD5Context ctx;
    md5Checksum ret;

    MD5Init(&ctx);
    MD5Update(&ctx, in, len);
    MD5Final(&ret, &ctx);

    return ret;
}

md5Checksum md5Fd(int fd)
{
    md5Checksum ret;
    struct MD5Context ctx;
    struct stat st;
    off_t pos;
    int windowSize = getpagesize() * 1024;

    if (fstat(fd, &st) != 0) {
        goto fail;
    }

    MD5Init(&ctx);

    for (pos = 0; pos < st.st_size; pos += windowSize) {
        void *buf;
        size_t toRead = st.st_size - pos;

        if (toRead > windowSize) {
            toRead = windowSize;
        }

        buf = mmap(NULL, toRead, PROT_READ, MAP_PRIVATE, fd, pos);
        if (buf == MAP_FAILED) {
            goto fail;
        }

        MD5Update(&ctx, buf, toRead);

        munmap(buf, toRead);
    }

    MD5Final(&ret, &ctx);
    return ret;

fail:
    memset(&ret, 0xff, sizeof(ret));
    return ret;
}

md5Checksum md5Path(const char *path)
{
    md5Checksum ret;
    int fd = open(path, O_RDONLY);

    if (fd >= 0) {
        ret = md5Fd(fd);
        if (close(fd) < 0) {
            memset(&ret, 0xff, sizeof(ret));
        }
        return ret;
    } else {
        memset(&ret, 0xff, sizeof(ret));
        return ret;
    }
}
