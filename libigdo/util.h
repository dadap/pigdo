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

#ifndef PIGDO_UTIL_H
#define PIGDO_UTIL_H

#include <stdbool.h>

/**
 * @brief Concatenate a directory and file name, with a '/' in between
 *
 * @param dir The name of the directory
 * @param dir The name of the file
 *
 * @return @c dir/file on success, or NULL on error
 */
char *dircat(const char *dir, const char *file);

/**
 * @brief get the offset of @p offset within its page
 */
off_t pagemod(off_t offset);

/**
 * @brief get a page-aligned starting address for @p offset
 */
off_t pagebase(off_t offset);

/**
 * @brief Determine whether a path is absolute
 */
bool isAbsolute(const char *path);
#endif
