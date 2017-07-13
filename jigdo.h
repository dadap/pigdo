#ifndef PIGDO_JIGDO_H
#define PIGDO_JIGDO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t sum[4];
} md5Checksum;

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
    uint64_t size; ///< Uncompressed length of the data block
} templateDataEntry;

/**
 * @brief data parsed from a DESC table entry for a matched file
 */
typedef struct {
    uint64_t size;                   ///< Length of the component file
    uint64_t rsync64SumInitialBlock; ///< rsync64 sum of the inital block; will
                                     ///< be initialized to 0 when entry type is
                                     ///< TEMPLATE_ENTRY_TYPE_FILE_OBSOLETE.
    md5Checksum md5Sum;              ///< MD5 sum of the component file
} templateFileEntry;

/**
 * @brief Data parsed from a DESC table entry
 */
typedef struct {
    templateEntryType type; ///< Type of the entry
    off_t offset;           ///< offset within the image file
    union {
        templateImageInfoEntry imageInfo;
        templateDataEntry data;
        templateFileEntry file;
    } u;                   ///< Entry data; must access via the correct union
                           ///< member based on templateDescEntry::type.
} templateDescEntry;

/**
 * @brief Parse a DESC table from a template file
 *
 * @param fp An open <tt>FILE *</tt> handle to a Jigdo template file.
 * @param table A pointer to an array of @c templateDescEntry records where the
 * parsed data for the DESC table entries will be stored. @p table may be NULL,
 * in which case freadTemplateDesc() will count the number of DESC table entries
 * and store the count in @p count without storing any entry data.
 *
 * @return @c true on success; @c false on error
 */
bool freadTemplateDesc(FILE *fp, templateDescEntry **table, int *count);
#endif
