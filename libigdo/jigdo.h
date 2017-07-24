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

#ifndef PIGDO_JIGDO_H
#define PIGDO_JIGDO_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "jigdo-md5.h"

typedef struct _jigdoServer jigdoServer;
typedef struct _jigdoFile jigdoFileInfo;
typedef struct _jigdo jigdoData;

/**
 * @brief Parse data from a @c .jigdo file
 *
 * @param path The path to the file.
 * @param data A pointer to a jigdoData record where parsed data will be stored.
 *
 * @return @c true on success; @c false on error
 */
bool readJigdoFile(const char *path, jigdoData *data);

/**
 * @brief Free heap-allocated members of a jigdoData record
 *
 * @note TODO not implemented yet; leaks ahoy!
 */
void freeJigdoData(jigdoData *data);

/**
 * @brief Get a URI where the file identified by @p md5 can be fetched
 *
 * @param data Parsed data from the @c .jigdo file
 * @param md5 The checksum to match
 *
 * @return A URI for fetching a file matching @p md5, or NULL if not found or
 *         an error occurred.
 */
char *md5ToURI(jigdoData *data, md5Checksum md5);

/**
 * @brief Append @p mirror to the mirrors list in the server named @serverName
 *
 * @param data jigdoData struct where mirror will be added
 * @param mirror The server and its base path, in Server=URI format
 *
 * @return true on success; false if an error occurred
 */
bool addServerMirror(jigdoData *data, char *servermirror);

/**
 * @brief Search for @p file in local directories
 *
 * @return Index (in file::server) to first local directory containing a match,
 *         or -1 if no match found
 */
int findLocalCopy(const jigdoFileInfo *file);

/**
 * @brief Locate a jigdoFileInfo record in @p data based on @key
 *
 * @param data The jigdoData record containing the jigdoFileInfo array to search
 * @param key The MD5 checksum to match within @data
 * @param numFound returns the count of matching records to the caller
 *
 * @return The address to the first jigdoFileInfo record in @p data that matches
 * the MD5 checksum in @p key
 */
jigdoFileInfo *findFileByMD5(const jigdoData *data, md5Checksum key,
                             int *numFound);
#endif
