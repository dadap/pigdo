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
#include <stdlib.h>
#include <sys/mman.h>

#include "jigdo-template.h"
#include "decompress.h"
#include "util.h"

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

/**
 * @brief Seek @p fp to the byte following the next CRLF line terminator
 *
 * The @c .template file format explicitly uses CRLF: one CRLF after the file
 * identifier line, and two CRLFs following the comment line.
 *
 * @return The number of bytes skipped, or -1 if EOF was reached before a CRLF
 */
static int nextCRLF(FILE *fp)
{
    int c = 0, oldc, count = -1;

    while (true) {
        do {
            oldc = c;
            c = fgetc(fp);
            count++;
        } while (c != '\n' && c != EOF);

        if (c == EOF) {
            return -1;
        }

        if (oldc == '\r') {
            return count;
        }
    }
}

/**
 * @brief Validate the header of the .template file
 *
 * This will leave @p fp seeked to the beginning of the first data block.
 *
 * @param fp An open <tt>FILE *</tt> handle to the @c .template file
 *
 * @return @c true on success; @c false on error
 */
static bool validateTemplateFile(FILE *fp)
{
    int i;

    // pigdo only supports v1.x Jigdo files
    static const char headerV1[] = "JigsawDownload template 1.";
    char headerVerify[sizeof(headerV1)];

    if (fseek(fp, 0, SEEK_SET) != 0) {
        return false;
    }

    if (fread(headerVerify, sizeof(headerVerify), 1, fp) != 1) {
        return false;
    }
    headerVerify[sizeof(headerVerify)-1] = '\0';

    if (strcmp(headerV1, headerVerify) != 0) {
        return false;
    }

    // Ignore the rest of the version line and the comment section.
    for (i = 0; i < 3; i++) {
        if (nextCRLF(fp) < 0) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Decompress a chunk of the @c .template file data stream
 *
 * @param fp An open <tt>FILE *</tt> handle to a Jigdo @c .template file.
 *           This should have been seeked to the beginning of a compressed data
 *           chunk by the caller ahead of time.
 * @param out The base address of where the decompressed output will be written
 * @param avail Available capacity of the output buffer
 *
 * @return The number of decompressed bytes on success, -1 on failure, or 0 when
 *         no further compressed data chunks remain.
 */
static ssize_t decompressDataPart(FILE *fp, void *out, size_t avail)
{
    static const int headerLen = 4;
    char header[headerLen + 1];
    templateU48 len6B;
    ssize_t inBytes;
    size_t outBytes;
    void *in;
    int ret;
    compressType type = COMPRESSED_DATA_UNKNOWN;

    header[headerLen] = '\0';
    if (fread(&header, headerLen, 1, fp) != 1) {
        return -1;
    }

    if (strcmp(header, "DATA") == 0) {
        type = COMPRESSED_DATA_ZLIB;
    } else if (strcmp(header, "BZIP") == 0) {
        type = COMPRESSED_DATA_BZIP2;
    } else if (strcmp(header, "DESC") == 0) {
        /* The DESC table follows the end of the data stream */
        return 0;
    } else {
        return -1;
    }

    if (fread(&len6B, sizeof(len6B), 1, fp) != 1) {
        return -1;
    }
    inBytes = templateU48ToU64(len6B) - 16; /* 4B header, 2 x 6B sizes */

    if (fread(&len6B, sizeof(len6B), 1, fp) != 1) {
        return -1;
    }
    outBytes = templateU48ToU64(len6B);

    if (outBytes > avail) {
        return -1;
    }

    in = malloc(inBytes);
    if (fread(in, inBytes, 1, fp) != 1) {
        free(in);
        return -1;
    }
    ret = decompressMemToMem(type, in, inBytes, out, outBytes);
    free(in);

    assert(ret == outBytes || ret == -1);
    assert(outBytes != 0); // XXX It should probably be valid for outBytes to be
                           // 0, but that would confuse the API of returning 0
                           // when hitting the DESC table.

    return ret;
}

bool writeDataFromTemplate(FILE *fp, int outFd, templateDescEntry *table,
                           int count)
{
    int i;
    size_t totalDecompressedSize = 0, doneSize = 0, copiedSize = 0;
    ssize_t partSize;
    void *decompressed = NULL;
    bool ret = false;

    if (!validateTemplateFile(fp)) {
        goto done;
    }

    /* Determine the sum of the sizes of the data parts and allocate enough
     * memory to store the decompressed data. */
    for (i = 0; i < count; i++) {
        if (table[i].type == TEMPLATE_ENTRY_TYPE_DATA) {
            totalDecompressedSize += table[i].u.data.size;
        }
    }

    if (totalDecompressedSize > table[count-1].u.imageInfo.size) {
        goto done;
    }

    decompressed = malloc(totalDecompressedSize);
    if (!decompressed) {
        goto done;
    }

    /* Decompress the stream of data parts from the template file */
    do {
        partSize = decompressDataPart(fp, decompressed + doneSize,
                                      totalDecompressedSize - doneSize);
        if (partSize < 0) {
            goto done;
        }
        doneSize += partSize;
    } while (partSize > 0);

    assert(doneSize == totalDecompressedSize);

    /* Find all of the data part entries from the DESC table and copy the
     * corresponding bytes into the file mapping. */
    for (i = 0; i < count; i++) {
        void *out;

        if (table[i].type != TEMPLATE_ENTRY_TYPE_DATA) {
            continue;
        }

        /* Bounds checking in case of a corrupted or malicious .template */
        if (copiedSize + table[i].u.data.size > totalDecompressedSize) {
            goto done;
        }

        out = mmap(NULL, table[i].u.data.size + pagemod(table[i].offset),
                   PROT_READ | PROT_WRITE, MAP_SHARED, outFd,
                   pagebase(table[i].offset));
        if (out == MAP_FAILED) {
            goto done;
        }
        memcpy(out + pagemod(table[i].offset), decompressed + copiedSize,
               table[i].u.data.size);
        copiedSize += table[i].u.data.size;
        msync(out, table[i].u.data.size, MS_ASYNC);
        munmap(out, table[i].u.data.size);

        table[i].status = COMMIT_STATUS_COMPLETE;
    }

    ret = true;

done:
    free(decompressed);

    return ret;
}
