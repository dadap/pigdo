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

#include <string.h>
#include <zlib.h>
#include <bzlib.h>
#include <stdbool.h>
#include <unistd.h>

#include "decompress.h"

/**
 * @brief Decompress a bzip2 stream
 *
 * API is the same as decompressMemToMem(), but without the type argument.
 */
static int bunzip2MemToMem(void *in, ssize_t inBytes, void *out,
                           size_t outBytes)
{
#if defined HAVE_LIBBZ2
    unsigned int written = outBytes;

    if (BZ2_bzBuffToBuffDecompress(out, &written, in, inBytes, 0, 0) == BZ_OK) {
        return written;
    }
#endif

    return -1;
}

/**
 * @brief Decompress a zlib stream
 *
 * API is the same as decompressMemToMem(), but without the type argument.
 * The silly name is partially because "inflate" was already taken
 */
static int infl8(void *in, ssize_t inBytes, void *out, size_t outBytes)
{
#if defined HAVE_LIBZ
    bool success = false;
    z_stream z;

    memset(&z, 0, sizeof(z));

    z.next_in = in;
    z.avail_in = inBytes;
    z.next_out = out;
    z.avail_out = outBytes;

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
#else
    return -1;
#endif
}

int decompressMemToMem(compressType type, void *in, ssize_t inBytes,
                       void *out, size_t outBytes)
{
    switch (type) {
        case COMPRESSED_DATA_ZLIB:
            return infl8(in, inBytes, out, outBytes);

        case COMPRESSED_DATA_BZIP2:
            return bunzip2MemToMem(in, inBytes, out, outBytes);

        case COMPRESSED_DATA_GZIP:    // Not implemented, try gunzopen() instead
        case COMPRESSED_DATA_UNKNOWN:
        default:
            return -1;
    }
}

#if defined HAVE_LIBZ
/**
 * @brief Decompress a gzipped file opened on @p in and write it to @out
 *
 * @return @c true on success; @c false on failure
 */
static bool gunzipToFile(FILE *in, FILE *out)
{
    int fd = dup(fileno(in)); // Otherwise gzclose() would close in as well.
    gzFile gz;

    if (fd < 0) {
        return false;
    }

    gz = gzdopen(fd, "r");

    if (gz) {
        uint8_t buf[65536];
        ssize_t readLen;

        do {
            readLen = gzread(gz, buf, sizeof(buf));

            if (readLen >= 0) {
                // This should preserve the value of readLen on success, or
                // reset it to -1 on failure.
                readLen = fwrite(buf, 1, readLen, out);
            }
        } while (readLen == sizeof(buf));

        if (gzclose(gz) != Z_OK || readLen < 0) {
            return false;
        }

        return true;
    }

    close(fd);

    return false;
}
#endif

bool gunzipFReplace(FILE **fp)
{
#if defined HAVE_LIBZ
    int fd = dup(fileno(*fp)); // Otherwise gzclose() would close *fp as well
    gzFile gz;
    bool isGz;

    if (fseek(*fp, 0, SEEK_SET) != 0) {
        return false;
    }

    if (fd < 0) {
        return false;
    }

    gz = gzdopen(fd, "r");

    if (gz == NULL) {
        close(fd);
        return false;
    }

    isGz = !gzdirect(gz);
    if (gzclose(gz) != Z_OK) {
        return false;
    }

    if (isGz) {
        FILE *fpOut;

        if (fseek(*fp, 0, SEEK_SET) != 0) {
            return false;
        }

        fpOut = tmpfile();

        if (fpOut) {
            if (gunzipToFile(*fp, fpOut)) {
                fclose(*fp);
                *fp = fpOut;

                return true;
            }

            fclose(fpOut);
        }
        return false;
    } else {
        /* Not compressed; do nothing */
        return true;
    }
#else
    return false;
#endif
}
