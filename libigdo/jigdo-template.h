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

typedef struct _templateImageInfo templateImageInfoEntry;
typedef struct _templateData templateDataEntry;
typedef struct _templateFile templateFileEntry;
typedef struct _templateDescTable templateDescTable;

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
 * @brief Flags for different possible states of a part during reassembly
 */
typedef enum {
    COMMIT_STATUS_NOT_STARTED = 0, ///< This part has not been processed yet
    COMMIT_STATUS_ASSIGNED,        ///< Assigned to a worker but not started
    COMMIT_STATUS_IN_PROGRESS,     ///< Actively being processed
    COMMIT_STATUS_COMPLETE,        ///< Successfully completed
    COMMIT_STATUS_ERROR,           ///< Attempted, but an error occurred
    COMMIT_STATUS_FATAL_ERROR,     ///< An error occurred, will not retry
    COMMIT_STATUS_LOCAL_COPY,      ///< Local copy found, but not copied yet
} commitStatus;

/**
 * @brief Parse a DESC table from a @c .template file
 *
 * @param fp An open <tt>FILE *</tt> handle to a Jigdo @c .template file.
 *
 * @return A pointer to a new templateDescTable record on success, NULL on error
 */
templateDescTable *jigdoReadTemplateFile(FILE *fp);

/**
 * @brief Decompress the data stream from the @c .template and write it out
 *
 * @param fp An open <tt>FILE *</tt> handle to a jigdo @c .template file.
 * @param out An open file descriptor to the output file.
 * @param table Table of file parts from the @c .template DESC table
 */
bool writeDataFromTemplate(FILE *fp, int outFd, templateDescTable *table);

/**
 * @brief Get the MD5 checksum of the target file
 */
const char *jigdoGetImageMD5(const templateDescTable *table);

/**
 * @brief Get the size of the target file in bytes
 */
uint64_t jigdoGetImageSize(const templateDescTable *table);

/**
 * @brief set the templateDescTable::existingFile flag
 */
void jigdoSetExistingFile(templateDescTable *table, bool val);

#endif
