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

#ifndef PIGDO_JIGDO_TEMPLATE_H
#define PIGDO_JIGDO_TEMPLATE_H

#include <stdio.h>
#include <stdint.h>

#include "jigdo-md5.h"

/**
 * @brief IDs of the various types of template records.
 */
typedef enum {
    TEMPLATE_ENTRY_TYPE_IMAGE_INFO_OBSOLETE = 1, ///< old image info, no rsync64
    TEMPLATE_ENTRY_TYPE_DATA = 2,                ///< data not from any file
    TEMPLATE_ENTRY_TYPE_FILE_OBSOLETE = 3,       ///< old file info, no rsync64
    TEMPLATE_ENTRY_TYPE_IMAGE_INFO = 5,          ///< info about the image file
    TEMPLATE_ENTRY_TYPE_FILE = 6,                ///< info about a matched file
} templateEntryType;

/**
 * @brief data parsed from the final "image info" record in the DESC table
 */
typedef struct {
    uint64_t size;               ///< Length of the image file
    md5Checksum md5Sum;          ///< MD5 sum of the image file
    uint32_t rsync64SumBlockLen; ///< Length of the initial block of each file
                                 ///< over which the rolling rsync64 sum was
                                 ///< computed during .jigdo generation, when
                                 ///< applicable. Initialized to 0 when using
                                 ///< TEMPLATE_ENTRY_TYPE_IMAGE_INFO_OBSOLETE.
} templateImageInfoEntry;

/**
 * @brief data parsed from a DESC table entry for unmatched data
 */
typedef struct {
    uint64_t size;         ///< Uncompressed length of the data block
    off_t offset;          ///< Offset within reassembled file
} templateDataEntry;

/**
 * @brief Flags for different possible states of a part during reassembly
 */
typedef enum {
    COMMIT_STATUS_NOT_STARTED = 0, ///< This part has not been processed yet
    COMMIT_STATUS_ASSIGNED,        ///< Assigned to a worker but not started
    COMMIT_STATUS_IN_PROGRESS,     ///< Actively being processed
    COMMIT_STATUS_COMPLETE,        ///< Successfully completed
    COMMIT_STATUS_ERROR,           ///< Attempted, but an error occurred
    COMMIT_STATUS_FATAL_ERROR,     ///< An error occurred, will not retry
} commitStatus;

/**
 * @brief data parsed from a DESC table entry for a matched file
 */
typedef struct {
    uint64_t size;                   ///< Length of the component file
    off_t offset;                    ///< Offset within reassembled file
    uint64_t rsync64SumInitialBlock; ///< rsync64 sum of the inital block; will
                                     ///< be initialized to 0 when entry type is
                                     ///< TEMPLATE_ENTRY_TYPE_FILE_OBSOLETE.
    md5Checksum md5Sum;              ///< MD5 sum of the component file
    commitStatus status;             ///< Status of restoring this file
} templateFileEntry;

/**
 * @brief Data parsed from a DESC table entry
 */
typedef struct {
    templateImageInfoEntry imageInfo; ///< Image summary information
    templateDataEntry *dataBlocks;    ///< Non-file data in the .template stream
    int numDataBlocks;                ///< Count of non-file data blocks
    templateFileEntry *files;         ///< Files to reassemble
    int numFiles;                     ///< Count of files
    bool existingFile;                ///< Set if output file already exists
} templateDescTable;

/**
 * @brief Parse a DESC table from a @c .template file
 *
 * @param fp An open <tt>FILE *</tt> handle to a Jigdo @c .template file.
 * @param table A pointer to a @c templateDescTable record where the parsed data
 *              will be stored.
 *
 * @return @c true on success; @c false on error
 */
bool freadTemplateDesc(FILE *fp, templateDescTable *table);

/**
 * @brief Decompress the data stream from the @c .template and write it out
 *
 * @param fp An open <tt>FILE *</tt> handle to a jigdo @c .template file.
 * @param out An open file descriptor to the output file.
 * @param table Table of file parts from the @c .template DESC table
 */
bool writeDataFromTemplate(FILE *fp, int outFd, templateDescTable *table);

#endif
