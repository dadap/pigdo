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
#include "jigdo-template.h"

typedef struct _jigdoServer jigdoServer;
typedef struct _jigdoFile jigdoFileInfo;
typedef struct _jigdo jigdoData;

/**
 * @brief Parse data from a @c .jigdo file
 *
 * @param path The path to the file.
 *
 * @return A pointer to a jigdoData record on success; NULL on error
 */
jigdoData *jigdoReadJigdoFile(const char *path);

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
 * @brief Populate @p fd with any local matches for files making up @p jigdo
 *
 * @param fd An open file descriptor to the output file
 * @param jigdo Parsed data for the .jigdo file
 *
 * @return Number of locally matched files, or -1 on error
 */
int jigdoFindLocalFiles(int fd, templateDescTable *table, jigdoData *jigdo);

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

/**
 * @brief Get the name of the jigdo target file
 */
const char *jigdoGetImageName(const jigdoData *jigdo);

/**
 * @brief Get the name of the template file
 */
const char *jigdoGetTemplateName(const jigdoData *jigdo);

/**
 * @brief Get the MD5 checksum of the template file
 */
const char *jigdoGetTemplateMD5(const jigdoData *jigdo);

#endif
