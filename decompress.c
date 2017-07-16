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

        case COMPRESSED_DATA_BZIP2:   // TODO Not implemented yet
        case COMPRESSED_DATA_GZIP:    // TODO Not implemented yet
        case COMPRESSED_DATA_UNKNOWN:
        default:
            return -1;
    }
}
