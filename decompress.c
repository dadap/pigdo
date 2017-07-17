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

#include <string.h>
#include <zlib.h>
#include <bzlib.h>
#include <stdbool.h>

#include "decompress.h"

/**
 * @brief Decompress a bzip2 stream
 *
 * API is the same as decompressMemToMem(), but without the type argument.
 */
static int bunzip2MemToMem(void *in, ssize_t inBytes, void **out,
                           size_t *outBytes, bool resize)
{
    unsigned int written = *outBytes;

    /* Only one-shot decompression supported for now */
    if (inBytes < 0 || resize) {
        return -1;
    }

    if (BZ2_bzBuffToBuffDecompress(*out, &written, in, inBytes, 0, 0) ==
        BZ_OK) {
        return written;
    }

    return -1;
}

/**
 * @brief Decompress a zlib stream
 *
 * API is the same as decompressMemToMem(), but without the type argument.
 * The silly name is partially because "inflate" was already taken
 */
static int infl8(void *in, ssize_t inBytes, void **out, size_t *outBytes,
                 bool resize)
{
    bool success = false;
    z_stream z;

    /* Only one-shot decompression supported for now */
    if (inBytes < 0 || resize) {
        return -1;
    }

    memset(&z, 0, sizeof(z));

    z.next_in = in;
    z.avail_in = inBytes;
    z.next_out = *out;
    z.avail_out = *outBytes;

    if (inflateInit(&z) != Z_OK) {
        goto done;
    }

    if (inflate(&z, Z_FINISH) != Z_STREAM_END) {
        goto done;
    }

    success = true;

done:

    if (inflateEnd(&z) != Z_OK) {
        success = false;
    }

    return success ? z.total_out : -1;
}

int decompressMemToMem(compressType type, void *in, ssize_t inBytes,
                        void **out, size_t *outBytes, bool resize)
{
    switch (type) {
        case COMPRESSED_DATA_ZLIB:
            return infl8(in, inBytes, out, outBytes, resize);
            break;

        case COMPRESSED_DATA_BZIP2:
            return bunzip2MemToMem(in, inBytes, out, outBytes, resize);

        case COMPRESSED_DATA_GZIP:    // Not implemented, try gunzopen() instead
        case COMPRESSED_DATA_UNKNOWN:
        default:
            return -1;
    }
}

/**
 * @brief Detects whether the file at @p path is gzip-compressed.
 *
 * @return COMPRESSED_DATA_GZIP if the file is gzip-compressed,
 *         COMPRESSED_DATA_PLAIN if the file is not gzip-compressed, or
 *         COMPRESSED_DATA_UNKNOWN if an error occurred.
 */
static compressType isgzip(const char *path)
{
    compressType ret = COMPRESSED_DATA_UNKNOWN;

    gzFile gz = gzopen(path, "r");
    if (gz) {
        ret = gzdirect(gz) ? COMPRESSED_DATA_PLAIN : COMPRESSED_DATA_GZIP;
        if (gzclose(gz) != Z_OK) {
            ret = COMPRESSED_DATA_UNKNOWN;
        }
    }

    return ret;
}

/**
 * @brief Decompress the gzipped file at @p path and copy its contents to @p fp
 *
 * @return @c true on success; @c false on failure
 */
static bool gunzipToFile(const char *path, FILE *fp)
{
    gzFile gz = gzopen(path, "r");
    if (gz) {
        uint8_t buf[65536];
        ssize_t readLen;

        do {
            readLen = gzread(gz, buf, sizeof(buf));
            if (readLen >= 0) {
                // This should preserve the value of readLen on success, or
                // reset it to -1 on failure.
                readLen = fwrite(buf, 1, readLen, fp);
            }
        } while (readLen == sizeof(buf));

        if (gzclose(gz) != Z_OK || readLen < 0) {
            return false;
        }

        return true;
    }

    return false;
}

FILE *gunzopen(const char *path)
{
    switch (isgzip(path)) {
        case COMPRESSED_DATA_GZIP: {
            FILE *fp = tmpfile();

            if (fp) {
                if (gunzipToFile(path, fp)) {
                    return fp;
                } else {
                    fclose(fp);
                }
            }

            return NULL;
        }

        case COMPRESSED_DATA_PLAIN:
            return fopen(path, "r");

        default:
            return NULL;
    }
}
