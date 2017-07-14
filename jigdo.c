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

#define _POSIX_C_SOURCE 200809L // for getline(3)

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>

#include "jigdo.h"

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
 * @brief Trim leading and trailing whitespace from @p s
 *
 * @note if @p s is a heap-allocated string, the return value may point to an
 * address within that string, so the original malloced string must be tracked
 * separately, and the return value of trimWhitespace() should not be freed.
 * Similarly, the return value of trimWhitespace() should not be used after
 * a string passed as @p s has been freed.
 */
static char *trimWhitespace(char *s)
{
    int i;

    if (s == NULL) {
        /* Give the caller something that can be dereferenced */
        return "";
    }

    for (i = strlen(s) - 1; i >= 0 && isspace(s[i]); i--) {
        s[i] = '\0';
    }

    for (; *s && isspace(*s); s++);

    return s;
}

/**
 * @brief Determine whether @p line contains a <tt>key = value</tt> pair with
 * @p keyname as the key.
 */
static bool isEqualKey(char *line, const char *keyName)
{
    if (strncmp(line, keyName, strlen(keyName)) != 0) {
        return false;
    }

    line += strlen(keyName);

    if (*line && strchr(line, '=') && (isspace(*line) || *line == '=')) {
        return true;
    }

    return false;
}

/**
 * @brief Isolate the value portion of a key/value pair
 *
 * @note FIXME This function should be updated to handle the quoting rules
 * described in the jigdo-file(1) manual page.
 *
 * @param line A string containing a key/value pair. May be modified.
 * @param delim The character that splits the key/value pair.
 @
 * @return A newly heap-allocated buffer containing a copy of the value, or
 * NULL if @line is not a key/value pair, or the value is empty.
 */
static char *getValue(char *line, char delim)
{
    char *c = strchr(line, delim);

    if (c && (++c)[0]) {
        return strdup(trimWhitespace(c));
    }

    return NULL;
}

/**
 * @brief Wrapper around getValue() for <tt>key = value</tt> pairs
 *
 * key/value pairs using '=' as the delimiter are the most common key/value
 * pairs in .jigdo files, so treat them as a special case.
 */
static char *getEqualValue(char *line)
{
    return getValue(line, '=');
}

/**
 * @brief Isolate the key portion of a key/value pair
 *
 * @param line A string containing a key/value pair. May be modified.
 * @param delim The character that splits the key/value pair.
 *
 * @return A pointer to the key, or NULL if @line is not a key/value pair.
 *
 * @note This function *will* replace any '=' character with '\0', and as such
 * must either be run *after* getEqualValue() or any other function that depends
 * on the contents of the @line, or else run on a disposable copy.
 */
static char *getKey(char *line, char delim)
{
    char *c = strchr(line, delim);

    if (c) {
        c[0] = '\0';
        return trimWhitespace(line);
    }

    return NULL;
}

/**
 * @brief locate the @c [Jigdo] section and parse its data into @p data
 */
static bool freadJigdoFileJigdoSection(FILE *fp, jigdoData *data)
{
    static const char *versionKey = "Version";
    static const char *generatorKey = "Generator";
    static const char *oneDotX = "1.";

    bool ret = false;
    char *line = NULL, *trimmed;
    size_t lineLen = 81;
    ssize_t read;

    if (fseek(fp, 0, SEEK_SET) != 0) {
        goto done;
    }

    line = malloc(lineLen);

    do {
        read = getline(&line, &lineLen, fp);
        trimmed = trimWhitespace(line);
    } while (read >= 0 && strcmp(trimmed, "[Jigdo]") != 0);

    if (read < 0) {
        goto done;
    }

    do {
        read = getline(&line, &lineLen, fp);
        trimmed = trimWhitespace(line);

        if (isEqualKey(trimmed, versionKey)) {
            data->version = getEqualValue(trimmed);
        }

        if (isEqualKey(trimmed, generatorKey)) {
            data->generator = getEqualValue(trimmed);
        }
    } while (read >= 0 && line[0] != '[');

    /* Only support format 1.x: a major version bump would signal a potentially
     * incompatible file format change. */
    if (data->version == NULL ||
        strncmp(data->version, oneDotX, strlen(oneDotX)) != 0) {
        goto done;
    }

    ret = true;

done:
    free(line);

    if (!ret) {
        free(data->version);
        data->version = NULL;
        free(data->generator);
        data->generator = NULL;
    }

    return ret;
}

/**
 * @brief tests whether a base64 symbol has been flagged as valid in the table
 */
static bool b64ValidSymbol(uint8_t symbol)
{
    return (symbol & (1 << 7)) != 1 << 7;
}

/**
 * @brief extracts the value of a base64 symbol from its entry in the table
 */
static uint8_t b64Symbol(uint8_t symbol)
{
    return symbol & 63;
}

/**
 * @brief Extracts a 3-byte integer value from four base64 characters
 *
 * @param in A base64 string
 *
 * @return A signed 32-bit integer, the lower three bytes of which contain
 * the result of decoding the first four bytes of the base64 string, or
 * a negative value if an invalid symbol was encountered.
 *
 * @note This is not a general-purpose base64 implementation, and is only
 * suitable for decoding jigdo md5sums encoded in either jigdo or standard
 * base64. Since jigdo base64 does not pad to a multiple of 3 bytes, there
 * is a hardcoded shift when reaching the end of the string.
 */
static int32_t base64To3ByteIntVal(const char *in)
{
    int32_t val, i;

    /* Any symbols not explicitly assigned in table[] will be zeroed out;
     * set the high bit when assigning a symbol as a flag that it is valid
     * Most of the lookup table will be unassigned characters, which is a
     * minor waste of space, but allows lookups in constant time. */
    #define b64(key, val) [key] = (1 << 7) | val,

    /* jigdo base64 uses '-' and '_' instead of '+' and '/', respectively:
     * this difference is non-conflicting, so populate the table with entries
     * for both variants just in case a jigdo file uses real base64 instead
     * of the jigdo base64 variant. */
    static const uint8_t table[256] = {
        b64('+',62) b64('-',62) b64('/',63) b64('0',52) b64('1',53) b64('2',54)
        b64('3',55) b64('4',56) b64('5',57) b64('6',58) b64('7',59) b64('8',60)
        b64('9',61) b64('A', 0) b64('B', 1) b64('C', 2) b64('D', 3) b64('E', 4)
        b64('F', 5) b64('G', 6) b64('H', 7) b64('I', 8) b64('J', 9) b64('K',10)
        b64('L',11) b64('M',12) b64('N',13) b64('O',14) b64('P',15) b64('Q',16)
        b64('R',17) b64('S',18) b64('T',19) b64('U',20) b64('V',21) b64('W',22)
        b64('X',23) b64('Y',24) b64('Z',25) b64('_',63) b64('a',26) b64('b',27)
        b64('c',28) b64('d',29) b64('e',30) b64('f',31) b64('g',32) b64('h',33)
        b64('i',34) b64('j',35) b64('k',36) b64('l',37) b64('m',38) b64('n',39)
        b64('o',40) b64('p',41) b64('q',42) b64('r',43) b64('s',44) b64('t',45)
        b64('u',46) b64('v',47) b64('w',48) b64('x',49) b64('y',50) b64('z',51)
    };

    #undef b64

    for (i = val = 0; i < 4; i++) {
        if (in[i] == '\0' || in[i] == '=') {
            /* End of base64 string: jigdo base64 is unpadded, and we know
             * exactly how long the unbase64-ed data is supposed to be, so
             * shift the partially decoded result to where deBase64MD5Sum
             * will expect it to be. MD5 sums are 16 bytes long, and base64
             * uses 4 symbols to encode 3 bytes: 24 symbols would encode 18
             * bytes, but we only need 22 for 16, which leaves a deficit of
             * 2 symbols, corresponding to 3 nybbles or 12 bits. Theoretically
             * we'd never encounter the == padding that a real base64 encoding
             * of a 16 byte value would have, since the caller would have split
             * a key/value pair on '=' anyway, but handle it just in case. */
            val <<= 12;
            break;
        }

        if (!b64ValidSymbol(in[i])) {
            return -1;
        }

        val *= 64;
        val += b64Symbol(table[in[i]]);
    }

    return val;
}

/**
 * @brief Set byte number @p byte to @p newval in @p md5
 */
static void md5SetByte(md5Checksum *md5, int byte, uint8_t newval)
{
    int word = byte / 4;
    int subbyte = byte % 4;
    uint8_t *dest = ((uint8_t *) &(md5->sum[word])) + subbyte;

    assert(byte >= 0 && byte < 16);

    *dest = newval;
}

/**
 * @brief extract byte number @p byte from @p word
 */
static uint8_t getByteFromWord(int32_t word, int byte)
{
    int shift_distance = 8 * (2 - byte);
    uint32_t mask = 0xff << shift_distance;

    assert(byte >= 0 && byte < 3);

    return (word & mask) >> shift_distance;
}

/**
 * @brief decode a base64-encoded md5sum
 *
 * @param in a pointer to the start of a base64 string encoding an md5sum
 * @param out a pointer to where the decoded md5sum will be stored
 *
 * @return true on success; false on invalid base64 string
 */
static bool deBase64MD5Sum(const char* in, md5Checksum *out)
{
    int i, byte;
    static const int md5base64characters = 22;

    assert(strlen(in) == md5base64characters);

    for (i = byte = 0; i < md5base64characters; i += 4) {
        int32_t j, decoded = base64To3ByteIntVal(in + i);

        if (decoded < 0) {
            return false;
        }

        for (j = 0; j < 3 && byte < 16; j++, byte++) {
            md5SetByte(out, byte, getByteFromWord(decoded, j));
        }
    }

    return true;
}

/**
 * @brief Locate the @c [Image] section and parse its data into @p data
 */
static bool freadJigdoFileImageSection(FILE *fp, jigdoData *data)
{
    static const char *imageNameKey = "Filename";
    static const char *templateNameKey = "Template";
    static const char *templateMD5Key = "Template-MD5Sum";

    bool ret = false;
    char *line = NULL, *trimmed;
    size_t lineLen = 81;
    ssize_t read;

    if (fseek(fp, 0, SEEK_SET) != 0) {
        goto done;
    }

    line = malloc(lineLen);

    do {
        read = getline(&line, &lineLen, fp);
        trimmed = trimWhitespace(line);
    } while (read >= 0 && strcmp(trimmed, "[Image]") != 0);

    if (read < 0) {
        goto done;
    }

    do {
        read = getline(&line, &lineLen, fp);
        trimmed = trimWhitespace(line);

        if (isEqualKey(trimmed, imageNameKey)) {
            data->imageName = getEqualValue(trimmed);
        }

        if (isEqualKey(trimmed, templateNameKey)) {
            data->templateName = getEqualValue(trimmed);
        }

        if (isEqualKey(trimmed, templateMD5Key)) {
            if (!deBase64MD5Sum(getEqualValue(trimmed), &(data->templateMD5))) {
                goto done;
            }
        }
    } while (read >= 0 && line[0] != '[');

    if (data->imageName && data->templateName) {
        ret = true;
    }

done:
    free(line);

    if (!ret) {
        free(data->imageName);
        data->imageName = NULL;
        free(data->templateName);
        data->templateName = NULL;
    }

    return ret;
}

/**
 * @brief Advance @p fp line by line until a @c [Parts] section header is found
 *
 * @return true if a @c [Parts] section was found; false on error or if EOF is
 * reached without finding a @c [Parts] section.
 */
static bool findPartsSection(FILE *fp)
{
    char *line = NULL, *trimmed;
    size_t lineLen = 81;
    ssize_t read;
    bool found;

    do {
        read = getline(&line, &lineLen, fp);
        trimmed = trimWhitespace(line);
        found = strcmp(trimmed, "[Parts]") == 0;
    } while (read >= 0 && !found);

    free(line);

    return found;
}

/**
 * @brief Search @p for a server named @name, creating a new server if needed
 *
 * @return A pointer to the existing server if found, or a newly created one
 * if not found, or NULL if an error occurred.
 */
static server *getServer(jigdoData *data, const char *name)
{
    int i;

    for (i = 0; i < data->numServers; i++) {
        if (strcmp(data->servers[i].name, name) == 0) {
            return data->servers + i;
        }
    }

    /* No server with that name found; create a new one */
    data->numServers++;
    data->servers = realloc(data->servers,
                            sizeof(data->servers[i]) * data->numServers);

    if (data->servers == NULL) {
        return NULL;
    }

    memset(data->servers + i, 0, sizeof(data->servers[i]));
    data->servers[i].name = strdup(name);

    return data->servers + i;
}

static int fileMD5Cmp(const void *a, const void *b)
{
    const jigdoFileInfo *fileA = a, *fileB = b;

    return memcmp(&(fileA->md5Sum.sum), &(fileB->md5Sum.sum),
                  sizeof(fileA->md5Sum.sum));
}

/**
 * @brief Locate any @c [Parts] sections and parse them into @p data
 */
static bool freadJigdoFilePartsSections(FILE *fp, jigdoData *data)
{
    char *line = NULL, *trimmed;
    size_t lineLen = 81;
    ssize_t read;
    bool ret = false;

    if (fseek(fp, 0, SEEK_SET) != 0) {
        goto done;
    }

    /* A .jigdo file may have zero, one, or many [Parts] sections */
    while (findPartsSection(fp)) {
        do {
            char *file = NULL;

            read = getline(&line, &lineLen, fp);
            trimmed = trimWhitespace(line);
            if (strncmp(trimmed, "[", 1) == 0) {
                /* Rewind to the previous line */
                if (fseek(fp, -1 * read, SEEK_CUR) != 0) {
                    goto done;
                }

                break;
            }

            if (strlen(trimmed) == 0) {
                continue;
            }

            /* FIXME handle comment lines that may contain '=' characters */

            file = getEqualValue(line);
            if (file) {
                int i = data->numFiles++;
                bool fileOK = false;
                char *md5, *path, *server;

                data->files = realloc(data->files,
                                      sizeof(data->files[0]) * data->numFiles);
                if (data->files == NULL) {
                    goto fileDone;
                }

                memset(data->files + i, 0, sizeof(data->files[i]));

                md5 = getKey(line, '=');

                if (!md5 || !deBase64MD5Sum(trimWhitespace(md5),
                                            &(data->files[i].md5Sum))) {
                    goto done;
                }

                /* FIXME handle direct URIs as file location */
                path = strdup(getValue(file, ':'));
                if (!path) {
                    goto fileDone;
                }
                data->files[i].path = path;

                server = trimWhitespace(getKey(file, ':'));
                data->files[i].server = getServer(data, server);

                if (!data->files[i].server) {
                    goto fileDone;
                }

                /* No search for local matches performed yet, so none found */
                data->files[i].localMatch = -1;

                fileOK = true;

fileDone:
                free(file);

                if (!fileOK) {
                    goto done;
                }
            }
        } while (read >= 0);
    }

    /* Sort by MD5 to make it easier to find files from MD5 sums in .template */
    qsort(data->files, data->numFiles, sizeof(data->files[0]), fileMD5Cmp);

    ret = true;

done:
    free(line);

    return ret;
}

/**
 * @brief Append @p mirror to the mirrors list in the server named @serverName
 *
 * @note This will probably be exported as part of this file's external API to
 * support adding additional mirrors via command line or config file options.
 */
static bool addServerMirror(jigdoData *data, const char *serverName,
                            const char *mirror)
{
    int i;
    server *s = getServer(data, serverName);

    if (!s) {
        return false;
    }

    i = s->numMirrors++;
    s->mirrors = realloc(s->mirrors, sizeof(s->mirrors[i]) * s->numMirrors);
    if (!s->mirrors) {
        return false;
    }
    s->mirrors[i] = strdup(mirror);

    return s->mirrors[i] != NULL;
}

/**
 * @brief Parse the @c [Servers] section out of @p fp into @p data
 */
static bool freadJigdoFileServersSection(FILE *fp, jigdoData *data)
{
    bool ret = false;
    char *line = NULL, *trimmed;
    size_t lineLen = 81;
    ssize_t read;

    if (fseek(fp, 0, SEEK_SET) != 0) {
        goto done;
    }

    line = malloc(lineLen);

    do {
        read = getline(&line, &lineLen, fp);
        trimmed = trimWhitespace(line);
    } while (read >= 0 && strcmp(trimmed, "[Servers]") != 0);

    if (read < 0) {
        goto done;
    }

    do {
        char *serverMirror = NULL, *serverName;
        bool success = false;

        read = getline(&line, &lineLen, fp);
        trimmed = trimWhitespace(line);

        if (strlen(trimmed) == 0) {
            continue;
        }

        serverMirror = getEqualValue(trimmed);
        if (!serverMirror) {
            goto mirrorDone;
        }
        trimmed = trimWhitespace(serverMirror);

        serverName = trimWhitespace(getKey(line, '='));
        if (!addServerMirror(data, serverName, trimmed)) {
            free(serverMirror);
            goto mirrorDone;
        }

        success = true;
mirrorDone:
        free(serverMirror);

        if (!success) {
            goto done;
        }
    } while (read >= 0 && line[0] != '[');

    if (data->imageName && data->templateName) {
        ret = true;
    }

done:
    free(line);

    if (!ret) {
        free(data->imageName);
        data->imageName = NULL;
        free(data->templateName);
        data->templateName = NULL;
    }

    return ret;
}

bool freadJigdoFile(FILE *fp, jigdoData *data)
{
    memset(data, 0, sizeof(*data));

    if (!freadJigdoFileJigdoSection(fp, data)) {
        return false;
    }

    if (!freadJigdoFileImageSection(fp, data)) {
        return false;
    }

    if (!freadJigdoFilePartsSections(fp, data)) {
        return false;
    }

    if (!freadJigdoFileServersSection(fp, data)) {
        return false;
    }

    return true;
}
