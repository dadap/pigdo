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

#ifndef LIBIGDO_JIGDO_PRIVATE_H
#define LIBIGDO_JIGDO_PRIVATE_H

#include "jigdo-md5-private.h"

/**
 * @brief Correlation of a Jigdo @c [Server] with local and remote sources
 */
struct _jigdoServer {
    char *name;       ///< Name of the server as it appears in the .jigdo file
    char **mirrors;   ///< List of remote mirror URIs associated with the server
    int numMirrors;   ///< Number of remote mirrors
    char **localDirs; ///< List of local paths containing files from the server
    int numLocalDirs; ///< Number of local directories
};

/**
 * @brief Data on an individual file listed in a .jigdo file
 */
struct _jigdoFile{
    md5Checksum md5Sum;  ///< MD5 sum of the file
    char *path;          ///< Path relative to @c [Server] root
    jigdoServer *server; ///< pointer to @c server struct associated with file
    int localMatch;      ///< Index into server::localDirs where match found;
                         ///< negative index indicates no match found.
};

/**
 * @brief Data parsed from a @c .jigdo file
 */
struct _jigdo {
    /* [Jigdo] section */
    char *version;           ///< Jigdo file format version
    char *generator;         ///< Program used to generate @c .jigdo file

    /* [Image] section */
    char *imageName;         ///< Name of reconstructed image file
    char *templateName;      ///< Name of jigdo @c .template file
    md5Checksum templateMD5; ///< MD5 sum of @c .template file
    char templateMD5String[MD5SUM_STRING_LENGTH];
    /* TODO store ShortInfo and Info? */

    /* [Parts] section */
    jigdoFileInfo *files;    ///< Files contained in the image
    int numFiles;            ///< Count of jigdoData::files elements

    /* [Servers] section */
    jigdoServer *servers;    ///< Servers where jigdoData::files can be found
    int numServers;          ///< Count of jigdoData::servers elements
};

#endif
