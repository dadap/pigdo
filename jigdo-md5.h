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

#ifndef PIGDO_JIGDO_MD5_H
#define PIGDO_JIGDO_MD5_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t sum[4];
} md5Checksum;

/**
 * @brief decode a base64-encoded md5sum
 *
 * @param in a pointer to the start of a base64 string encoding an md5sum
 * @param out a pointer to where the decoded md5sum will be stored
 *
 * @return true on success; false on invalid base64 string
 */
bool deBase64MD5Sum(const char* in, md5Checksum *out);

/**
 * @brief Compare two MD5 checksum values
 *
 * @param a The first checksum to compare
 * @param b The second checksum to compare
 *
 * @return The return value of the underlying memcmp(3) operation; i.e., zero
 * if the checksums are identical, positive if @p a > @p b, and negative if
 * @p a < @p b.
 */
int md5Cmp(const md5Checksum *a, const md5Checksum *b);


/**
 * @brief print a hexadecimal string representing @p md5 to stdout
 */
void printMd5Sum(md5Checksum md5);

/**
 * @brief compute an MD5 checksum from memory region
 *
 * @param in The starting address to checksum
 * @param len The number of bytes to checksum
 *
 * @return The computer MD5 checksum
 */
md5Checksum md5MemOneShot(const void *in, size_t len);

/**
 * @brief compute an MD5 checksum for a file
 *
 * @param fd An open file descriptor to the file to checksum
 *
 * @return The MD5 checksum. If an error occurred, all bits in the checksum will
 *         be set to 1.
 */
md5Checksum md5Fd(int fd);
#endif
