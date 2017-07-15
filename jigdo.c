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

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include "jigdo.h"

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


    /* XXX getline(3) may have returned negative due to e.g. a realloc(3) error,
     * but let's be optimistic and assume it just hit EOF */
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


    /* XXX getline(3) may have returned negative due to e.g. a realloc(3) error,
     * but let's be optimistic and assume it just hit EOF */
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

/**
 * @brief Comparator function for qsort(3) and bsearch(3) that operates on
 * jigdoFileInfo records and compares by md5Checksum
 */
static int fileMD5Cmp(const void *a, const void *b)
{
    const jigdoFileInfo *fileA = a, *fileB = b;

    return md5Cmp(&(fileA->md5Sum), &(fileB->md5Sum));;
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

    /* XXX getline(3) may have returned negative due to e.g. a realloc(3) error,
     * but let's be optimistic and assume it just hit EOF */
    if (read < 0) {
        goto done;
    }

    do {
        char *serverMirror = NULL, *serverName;
        bool success = false;

        read = getline(&line, &lineLen, fp);
        trimmed = trimWhitespace(line);


        /* XXX getline(3) may have returned negative due to e.g. a realloc(3)
         * error, but let's be optimistic and assume it just hit EOF */
        if (read < 0) {
            continue;
        }

        serverMirror = getEqualValue(trimmed);
        if (!serverMirror) {
            /* This line contains no server: clean up and continue loop */
            success = true;
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
#if 0 /* This code will be used eventually, just not right now */
static jigdoFileInfo *findFileByMD5(const jigdoData *data,
                                    const md5Checksum key, int *numFound)
{
    jigdoFileInfo keyFile, *found;

    *numFound = 0;

    keyFile.md5Sum = key; // keyFile is a container to make key fit fileMD5Cmp()

    found = bsearch(&keyFile, data->files, data->numFiles,
                    sizeof(data->files[0]), fileMD5Cmp);

    /* The .jigdo file format supports multiple entries with identical MD5 sums:
     * rewind to the first matching file and count the number of matches. */
    if (found) {
        int i;

        for (i = found - data->files; i > 0; i--) {
            if (md5Cmp(&key, &(data->files[i - 1].md5Sum)) != 0) {
                break;
            }
            found--;
        }

        for (*numFound = 1; i + *numFound < data->numFiles; (*numFound)++) {
            if (md5Cmp(&key, &(data->files[i + *numFound].md5Sum)) != 0) {
                break;
            }
        }
    }

    return found;
}
#endif
