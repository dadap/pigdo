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

/**
 * @brief Correlation of a Jigdo @c [Server] with local and remote sources
 */
typedef struct {
    char *name;       ///< Name of the server as it appears in the .jigdo file
    char **mirrors;   ///< List of remote mirror URIs associated with the server
    int numMirrors;   ///< Number of remote mirrors
    char **localDirs; ///< List of local paths containing files from the server
    int numLocalDirs; ///< Number of local directories
} server;

/**
 * @brief Data on an individual file listed in a .jigdo file
 */
typedef struct {
    md5Checksum md5Sum; ///< MD5 sum of the file
    char *path;         ///< Path relative to @c [Server] root
    server *server;     ///< pointer to @c server struct associated with file
    int localMatch;     ///< Index into server::localDirs where match was found;
                        ///< negative index indicates no match found.
} jigdoFileInfo;

/**
 * @brief Data parsed from a @c .jigdo file
 */
typedef struct {
    /* [Jigdo] section */
    char *version;           ///< Jigdo file format version
    char *generator;         ///< Program used to generate @c .jigdo file

    /* [Image] section */
    char *imageName;         ///< Name of reconstructed image file
    char *templateName;      ///< Name of jigdo @c .template file
    md5Checksum templateMD5; ///< MD5 sum of @c .template file
    /* TODO store ShortInfo and Info? */

    /* [Parts] section */
    jigdoFileInfo *files;    ///< Files contained in the image
    int numFiles;            ///< Count of jigdoData::files elements

    /* [Servers] section */
    server *servers;         ///< Servers where jigdoData::files can be found
    int numServers;          ///< Count of jigdoData::servers elements
} jigdoData;

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
#endif
