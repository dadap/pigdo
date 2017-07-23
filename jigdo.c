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

#define _XOPEN_SOURCE 700 // for getline(3) and realpath(3)

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <unistd.h>
#include <sys/param.h>
#include <limits.h>
#include <stdlib.h>

#include "jigdo.h"
#include "util.h"
#include "decompress.h"
#include "fetch.h"

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
 *
 * @return A newly heap-allocated buffer containing a copy of the value, or
 * NULL if @p line is not a key/value pair, or the value is empty.
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
 * on the contents of @p line, or else run on a disposable copy.
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
 * @brief Search @p for a server named @p name, creating a new server if needed
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

bool addServerMirror(jigdoData *data, char *servermirror)
{
    int i;
    server *s;
    char *c, *serverName, *mirror;

    // XXX nuke args like --try-last until quoting support is added
    c = strchr(servermirror, ' ');
    if (c) *c = 0;

    mirror = trimWhitespace(getEqualValue(servermirror));
    if (!mirror || !mirror[0]) {
        return false;
    }

    serverName = trimWhitespace(getKey(servermirror, '='));
    if (!serverName || !serverName[0]) {
        return false;
    }

    s = getServer(data, serverName);

    if (!s) {
        return false;
    }

    switch (isURI(mirror)) {
        const char *prefix = "";
        char *path;
        size_t len;

        // file:// URI or local directory
        case URI_TYPE_NONE:
            // XXX Ensure the local path is formatted in the form of a file://
            // URI, so that libcurl can treat it just like any other URI. It
            // might be useful to perform the transformation the other way,
            // i.e., remove "file://" from the beginning of the path if it's
            // there, and take a non-libcurl path, to make it possible for pigdo
            // to be built without libcurl support for those who only wish to
            // use it for assembly from local mirrors.
            prefix = "file://";
            // fall through
        case URI_TYPE_FILE:

            len = PATH_MAX + strlen(prefix);

            path = calloc(len + 1, 1);
            if (!path) {
                return false;
            }

            strncat(path, prefix, len);
            if (realpath(mirror, path + strlen(prefix)) == NULL) {
                free(path);
                return false;
            }

            i = s->numLocalDirs++;
            s->localDirs = realloc(s->localDirs,
                                   sizeof(s->localDirs[i]) * s->numLocalDirs);
            if (!s->localDirs) {
                free(path);
                return false;
            }
            s->localDirs[i] = path;

            return true;

        // non-local URI
        default:
            i = s->numMirrors++;
            s->mirrors = realloc(s->mirrors,
                                 sizeof(s->mirrors[i]) * s->numMirrors);
            if (!s->mirrors) {
                return false;
            }
            s->mirrors[i] = strdup(mirror);

            return s->mirrors[i] != NULL;
    }
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
        bool success = false;

        read = getline(&line, &lineLen, fp);
        trimmed = trimWhitespace(line);


        /* XXX getline(3) may have returned negative due to e.g. a realloc(3)
         * error, but let's be optimistic and assume it just hit EOF */
        if (read < 0) {
            continue;
        }

        trimmed = trimWhitespace(line);

        if (!addServerMirror(data, trimmed)) {
            goto mirrorDone;
        }

        success = true;

mirrorDone:
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

bool readJigdoFile(const char *path, jigdoData *data)
{
    int ret = false;
    FILE *fp;

    fp = fetchopen(path);
    if (!fp) {
        goto done;
    }

    if (!gunzipFReplace(&fp)) {
        goto done;
    }

    memset(data, 0, sizeof(*data));

    if (!freadJigdoFileJigdoSection(fp, data)) {
        goto done;
    }

    if (!freadJigdoFileImageSection(fp, data)) {
        goto done;
    }

    if (!freadJigdoFilePartsSections(fp, data)) {
        goto done;
    }

    if (!freadJigdoFileServersSection(fp, data)) {
        goto done;
    }

    ret = true;

done:
    if (fp) {
        fclose(fp);
    }

    return ret;
}

jigdoFileInfo *findFileByMD5(const jigdoData *data, md5Checksum key,
                             int *numFound)
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

int findLocalCopy(const jigdoFileInfo *file)
{
    int i;

    for (i = 0; i < file->server->numLocalDirs; i++) {
        char *fileURI = dircat(file->server->localDirs[i], file->path);
        char *path = fileURI + strlen("file://"); // prepended to localDirs[i]
        bool found = false;

        if (access(path, F_OK) == 0) {
            md5Checksum md5 = md5Path(path);
            found = (md5Cmp(&md5, &(file->md5Sum)) == 0);
        }

        free(fileURI);
        if (found) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief Choose a mirror where @p file can be found
 *
 * @return The name of a mirror where @p file should be available
 */
static char *selectMirror(const jigdoFileInfo *file)
{
    if (file->localMatch >= 0) {
        return file->server->localDirs[file->localMatch];
    }

    // TODO should probably keep track of mirror performance and prioritize
    // faster ones, and also honor things like --try-last
    return file->server->mirrors[rand() % file->server->numMirrors];
}

char *md5ToURI(jigdoData *data, md5Checksum md5)
{
    char *mirror;
    int numFound;
    jigdoFileInfo *fileInfo = findFileByMD5(data, md5, &numFound);

    if (numFound == 0) {
        return NULL;
    }

    mirror = selectMirror(fileInfo);
    if (!mirror) {
        return NULL;
    }

    return dircat(mirror, fileInfo->path);
}
