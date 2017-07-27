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

#ifndef PIGDO_DECOMPRESS_H
#define PIGDO_DECOMPRESS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * @brief Identifiers for the different compression types
 */
typedef enum {
    COMPRESSED_DATA_UNKNOWN = 0, ///< Unknown compression type
    COMPRESSED_DATA_ZLIB,        ///< zlib compression
    COMPRESSED_DATA_BZIP2,       ///< bzip2 compression
    COMPRESSED_DATA_GZIP,        ///< gzip compression
    COMPRESSED_DATA_PLAIN,       ///< Uncompressed data
} compressType;

/**
 * @brief Decompress data from one memory location into another one
 *
 * @param type Compression algorithm for decompression
 * @param in The beginning of the stream to be decompressed
 * @param inBytes Size of the compressed stream, if known.
 * @param out A pointer to the output buffer.
 * @param outBytes A pointer to the size of the output buffer.
 *
 * @return The number of decompressed bytes on success, or -1 on failure.
 */
int decompressMemToMem(compressType type, void *in, ssize_t inBytes,
                       void *out, size_t outBytes);

/**
 * @brief Determine whether the file opened at @p fp is gzip-compressed, and
 *        replace @p fp with a handle to an uncompressed version if so
 *
 * @param fp A pointer to an open <tt>FILE *</a> handle that may or may not be
 *           gzip-compressed.
 *
 * @return @c true on success; @c false if an error occurred. If the file
 *         underlying @p fp was gzip-compressed, @p fp will be closed and
 *         replaced with a new <tt>FILE *</tt> handle to a temporary file
 *         containing the uncompressed contents of the original @p fp. The
 *         temporary file will be deleted once its handle is closed. If the
 *         original file is not compressed, @p fp will be left alone.
 */
bool gunzipFReplace(FILE **fp);
#endif
