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

#ifndef PIGDO_FETCH_H
#define PIGDO_FETCH_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/**
 * @brief Identifiers for URI types
 *
 * @note This currently only distinguishes file:// URIs apart from all other
 *       schemes, for the purposes of optimizing operations on local files
 *       identified via a file:// URI
 */
typedef enum {
    URI_TYPE_NONE = 0,   ///< Not a URI: defined to 0 so isURI()'s return value
                         ///< can be treated as true/false as a shortcut, if
                         ///< distinction between URI schemes is not important
    URI_TYPE_FILE,       ///< file:// URI
    URI_TYPE_OTHER = 255 ///< URI without its own specific type. Defined to a
                         ///< high value to allow additional URI types to be
                         ///< added while keeping ABI backwards-compatible.
} uriType;

/**
 * @brief Initialize libcurl before fetching
 *
 * @return @c true on success; @c false on failure
 *
 * @note This function is not thread-safe and MUST NOT be called when other
 *       threads are running
 */
bool fetch_init(void);

/**
 * @brief Release libcurl resources
 *
 * @note This function is not thread-safe and MUST NOT be called when other
 *       threads are running
 */
void fetch_cleanup(void);

/**
 * @brief Fetch data into memory
 *
 * @param uri The Uniform Resource Identifier of the data to fetch
 * @param out Memory location where the data should be written
 * @param outBytes Maximum amount of data to write
 * @param fetchedBytes This will be updated with bytes fetched so far
 *
 * @return Amount of data written, or -1 on error
 */
ssize_t fetch(const char *uri, void *out, size_t outBytes,
              ssize_t *fetchedBytes);

/**
 * @brief Open a file for read, fetching it from a remote location if necessary
 *
 * @param path The path to the target file. May be on the filesystem or a URI.
 *
 * @return An open <tt>FILE *</tt> handle to the file, or NULL on failure. If
 *         @p path is a URI, the returned handle will be to a temporary file
 *         which the contents of @p path were fetched to, which will be deleted
 *         once the handle is closed with fclose(3).
 */
FILE *fetchopen(const char *path);

/**
 * @brief Determine whether a path is a URI
 *
 * @return The uriType of the path if it is a URI, or URI_TYPE_NONE if not
 */
uriType isURI(const char *path);
#endif
