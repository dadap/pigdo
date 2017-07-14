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

#include <string.h>
#include <assert.h>

#include "jigdo-template.h"

/*@
 * @brief Container for the 6-byte little endian ints used in the @c .template
 */
typedef struct {
    uint8_t bytes[6];
} templateU48;

/**
 * @brief Convert binary little endian value read from a @c .template file
 *
 * @note This could probably be micro-optimized away on little endian machines,
 * but it's already fast enough to not really matter.
 */
static uint64_t readLittleEndianValue(void *base, int bytes)
{
    int i;
    uint64_t ret;

    assert(bytes <= 8);

    for (ret = i = 0; i < bytes; i++)
    {
        ret += (uint64_t)(((uint8_t *) base)[i]) << i * 8;
    }

    return ret;
}

/**
 * @brief Extract the value from a 6-byte integer read from a @c .template file
 */
static uint64_t templateU48ToU64(templateU48 i)
{
    return readLittleEndianValue(i.bytes, sizeof(i.bytes));
}

bool freadTemplateDesc(FILE *fp, templateDescEntry **table, int *count)
{
    static const char descHeader[] = { 'D', 'E', 'S', 'C' };
    char descHeaderVerify[sizeof(descHeader)];
    templateU48 sizeRead;
    uint64_t size;
    off_t offset = 0;

    /* The last six bytes of the .template are the size of the DESC table */

    if (fseek(fp, -1 * sizeof(sizeRead), SEEK_END) != 0) {
        return false;
    }

    if (fread(&sizeRead, sizeof(sizeRead), 1, fp) != 1) {
        return false;
    }

    size = templateU48ToU64(sizeRead);

    /* Seek to the beginning of the DESC table and perform some validation */

    if (fseek(fp, -1 * size, SEEK_END) != 0) {
        return false;
    }

    if (fread(&descHeaderVerify, sizeof(descHeaderVerify), 1, fp) != 1) {
        return false;
    }

    if (memcmp(descHeader, descHeaderVerify, sizeof(descHeader)) != 0) {
        return false;
    }

    if (fread(&sizeRead, sizeof(sizeRead), 1, fp) != 1) {
        return false;
    }

    if (size != templateU48ToU64(sizeRead)) {
        return false;
    }

    /* From now on, we will use size as a running countdown to EOF */

    size -= sizeof(descHeader) + sizeof(sizeRead);

    for (*count = 0; size > sizeof(sizeRead); (*count)++) {
        char type;
        uint64_t entrySize;

        /* For all DESC table entry types, the first byte is the entry type,
         * and the next six bytes are the size. */

        if(fread(&type, sizeof(type), 1, fp) != 1) {
            return false;
        }
        size -= sizeof(type);

        if(fread(&sizeRead, sizeof(sizeRead), 1, fp) != 1) {
            return false;
        }

        size -= sizeof(sizeRead);
        entrySize = templateU48ToU64(sizeRead);

        /* If we're not just counting entries, store the type-agnostic values */

        if (table) {
            memset(*table + *count, 0, sizeof(*table[*count]));

            (*table)[*count].type = type;
            (*table)[*count].offset = offset;
            offset += entrySize;
        }

        /* Parse and optionally store the type-specific values for this entry.
         * The size data is pretty much type-agnostic, but it is handled in the
         * type-specific section to be polite and avoid abusing the union. */

        switch (type) {
            md5Checksum md5Sum;
            uint32_t blockLen = 0;
            uint64_t rsync64Sum = 0;

            case TEMPLATE_ENTRY_TYPE_IMAGE_INFO_OBSOLETE:
            case TEMPLATE_ENTRY_TYPE_IMAGE_INFO:

                if (fread(&md5Sum, sizeof(md5Sum), 1, fp) != 1) {
                    return false;
                }
                size -= sizeof(md5Sum);

                if (type == TEMPLATE_ENTRY_TYPE_IMAGE_INFO) {
                    if (fread(&blockLen, sizeof(blockLen), 1, fp) != 1) {
                        return false;
                    }
                    size -= sizeof(blockLen);
                }

                if (table) {
                    assert(offset == 2 * entrySize);

                    (*table)[*count].u.imageInfo.size = entrySize;
                    (*table)[*count].u.imageInfo.md5Sum = md5Sum;
                    (*table)[*count].u.imageInfo.rsync64SumBlockLen =
                        readLittleEndianValue(&blockLen, sizeof(blockLen));
                }

                break;

            case TEMPLATE_ENTRY_TYPE_DATA:

                if (table) {
                    (*table)[*count].u.data.size = entrySize;
                }

                break;

            case TEMPLATE_ENTRY_TYPE_FILE_OBSOLETE:
            case TEMPLATE_ENTRY_TYPE_FILE:

                if (type == TEMPLATE_ENTRY_TYPE_FILE) {
                    if (fread(&rsync64Sum, sizeof(rsync64Sum), 1, fp) != 1) {
                        return false;
                    }
                    size -= sizeof(rsync64Sum);
                }

                if (fread(&md5Sum, sizeof(md5Sum), 1, fp) != 1) {
                    return false;
                }
                size -= sizeof(md5Sum);

                if (table) {
                    (*table)[*count].u.file.size = entrySize;
                    (*table)[*count].u.file.rsync64SumInitialBlock =
                        readLittleEndianValue(&rsync64Sum, sizeof(rsync64Sum));
                    (*table)[*count].u.file.md5Sum = md5Sum;
                }

               break;

            /* Invalid type ID */

            default:

                return false;
        }

    }

    return true;
}

