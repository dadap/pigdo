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

#ifndef LIBIGDO_JIGDO_TEMPLATE_PRIVATE_H
#define LIBIGDO_JIGDO_TEMPLATE_PRIVATE_H

#include "jigdo-template.h"
#include "jigdo-md5-private.h"

/**
 * @brief data parsed from the final "image info" record in the DESC table
 */
struct _templateImageInfo {
    uint64_t size;               ///< Length of the image file
    md5Checksum md5Sum;          ///< MD5 sum of the image file
    uint32_t rsync64SumBlockLen; ///< Length of the initial block of each file
                                 ///< over which the rolling rsync64 sum was
                                 ///< computed during .jigdo generation, when
                                 ///< applicable. Initialized to 0 when using
                                 ///< TEMPLATE_ENTRY_TYPE_IMAGE_INFO_OBSOLETE.
    /// md5Sum in string format
    char md5String[MD5SUM_STRING_LENGTH];
};

/**
 * @brief data parsed from a DESC table entry for unmatched data
 */
struct _templateData {
    uint64_t size;         ///< Uncompressed length of the data block
    off_t offset;          ///< Offset within reassembled file
};

/**
 * @brief data parsed from a DESC table entry for a matched file
 */
struct _templateFile {
    uint64_t size;                   ///< Length of the component file
    off_t offset;                    ///< Offset within reassembled file
    uint64_t rsync64SumInitialBlock; ///< rsync64 sum of the inital block; will
                                     ///< be initialized to 0 when entry type is
                                     ///< TEMPLATE_ENTRY_TYPE_FILE_OBSOLETE.
    md5Checksum md5Sum;              ///< MD5 sum of the component file
    commitStatus status;             ///< Status of restoring this file
};

/**
 * @brief Data parsed from a DESC table entry
 */
struct _templateDescTable {
    templateImageInfoEntry imageInfo; ///< Image summary information
    templateDataEntry *dataBlocks;    ///< Non-file data in the .template stream
    int numDataBlocks;                ///< Count of non-file data blocks
    templateFileEntry *files;         ///< Files to reassemble
    int numFiles;                     ///< Count of files
    bool existingFile;                ///< Set if output file already exists
};

#endif
