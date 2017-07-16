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
 * @param uri The Uniform Resource Indicator of the data to fetch
 * @param out Memory location where the data should be written
 * @param outBytes Maximum amount of data to write
 *
 * @return Amount of data written, or -1 on error
 */
ssize_t fetch(const char *uri, void *out, size_t outBytes);

#endif
