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

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

#include "jigdo.h"
#include "jigdo-template.h"
#include "jigdo-md5.h"
#include "fetch.h"
#include "util.h"

static pthread_mutex_t tableLock; ///< @brief Lock on DESC table management
static pthread_mutex_t printLock; ///< @brief Lock on stdout/stderr

/**
 * @brief Determine whether any parts still need to be fetched
 *
 * @return 0 if all parts are complete, positive if parts still need to be
 *           fetched, and negative if an unrecoverable error occurred.
 */
static int partsRemain(templateDescEntry *table, int count, int *beginComplete)
{
    int i;
    bool ret = 0;

    if (pthread_mutex_lock(&tableLock) != 0) {
        /* Something horrible has happened; break out of the loop in main() */
        return -1;
    }

    for (i = *beginComplete; i < count - 1; i++) {
        bool breakLoop = false;

        switch(table[i].status) {
            case COMMIT_STATUS_FATAL_ERROR:
                ret = -1;
                breakLoop = true;
            break;

            case COMMIT_STATUS_COMPLETE:
                *beginComplete = i;
            break;

            default:
                ret = 1;
                breakLoop = true;
            break;
        }

        if (breakLoop) {
            break;
        }
    }

    if (pthread_mutex_unlock(&tableLock) != 0) {
        ret = -1;
    }

    return ret;
}

/**
 * @brief Retrieve the commitStatus of @p chunk
 */
static commitStatus getStatus(templateDescEntry *chunk)
{
    commitStatus status;

    if (pthread_mutex_lock(&tableLock) != 0) {
        chunk->status = COMMIT_STATUS_FATAL_ERROR;
        return COMMIT_STATUS_FATAL_ERROR;
    }

    status = chunk->status;

    if (pthread_mutex_unlock(&tableLock) != 0) {
        chunk->status = COMMIT_STATUS_FATAL_ERROR;
    }

    return status;
}

/**
 * @brief Determine whether @p chunk is eligible to be assigned to a worker
 *
 * @note The caller must take the tableLock mutex before calling in.
 */
static bool isWaitingFileNoMutex(templateDescEntry *chunk)
{
    return ((chunk->type == TEMPLATE_ENTRY_TYPE_FILE ||
             chunk->type == TEMPLATE_ENTRY_TYPE_FILE_OBSOLETE) &&
            (chunk->status == COMMIT_STATUS_NOT_STARTED ||
             chunk->status == COMMIT_STATUS_ERROR));
}

/**
 * @brief Scan @p table for the next unfetched chunk
 */
static templateDescEntry *selectChunk(templateDescEntry *table, int count)
{
    int i;

    if (pthread_mutex_lock(&tableLock) != 0) {
        return NULL;
    }

    /* Searching for the next available file and assigning it should happen
     * atomically, so don't release tableLock until assigned. */
    for (i = 0; i < count - 1 && !isWaitingFileNoMutex(table + i); i++);

    table[i].status = COMMIT_STATUS_ASSIGNED;

    if (pthread_mutex_unlock(&tableLock) != 0) {
        return NULL;
    }

    if (i == count) {
        return NULL; // Not an error; we've just reached the end.
    }

    return table + i;
}

/**
 * @brief Assign @p status to @p chunk
 */
static void setStatus(templateDescEntry *chunk, commitStatus status)
{
    if (pthread_mutex_lock(&tableLock) != 0) {
        chunk->status = COMMIT_STATUS_FATAL_ERROR;
        return;
    }

    chunk->status = status;

    if (pthread_mutex_unlock(&tableLock) != 0) {
        chunk->status = COMMIT_STATUS_FATAL_ERROR;
    }
}

/**
 * @brief Arguments for the worker thread
 */
typedef struct {
    jigdoData *jigdo;         ///< Pointer to the parsed jigdo data
    templateDescEntry *chunk; ///< Pointer to the chunk this worker will work on
    void *out;                ///< Pointer to the output buffer
} workerArgs;

/**
 * @brief Worker thread to wrap around fetch()
 */
static void *fetch_worker(void *args)
{
    workerArgs *a = (workerArgs *) args;
    char *uri = md5ToURI(a->jigdo, a->chunk->u.file.md5Sum);

    if (uri) {
        size_t fetched;
        if (pthread_mutex_lock(&printLock) != 0) {
            setStatus(a->chunk, COMMIT_STATUS_FATAL_ERROR);
        }
        printf("Downloading %s...\n", uri);
        fflush(stdout);
        if (pthread_mutex_unlock(&printLock) != 0) {
            setStatus(a->chunk, COMMIT_STATUS_FATAL_ERROR);
        }

        setStatus(a->chunk, COMMIT_STATUS_IN_PROGRESS);
        fetched = fetch(uri, a->out + a->chunk->offset, a->chunk->u.file.size);


        if (pthread_mutex_lock(&printLock) != 0) {
            setStatus(a->chunk, COMMIT_STATUS_FATAL_ERROR);
        }
        if (fetched == a->chunk->u.file.size) {
            setStatus(a->chunk, COMMIT_STATUS_COMPLETE);
            printf("%s done!\n", uri);
            fflush(stdout);
        } else {
            fprintf(stderr, "%s error!\n", uri);
            setStatus(a->chunk, COMMIT_STATUS_ERROR);
            fflush(stderr);
        }
        if (pthread_mutex_unlock(&printLock) != 0) {
            setStatus(a->chunk, COMMIT_STATUS_FATAL_ERROR);
        }
    } else {
        setStatus(a->chunk, COMMIT_STATUS_FATAL_ERROR);
    }

    return NULL;
}

int main(int argc, const char * const * argv)
{
    FILE *fp = NULL;
    jigdoData jigdo;
    templateDescEntry *table = NULL;
    int count, ret = 1, fd = -1, i, contiguousComplete;
    char *jigdoFile = NULL, *jigdoDir, *templatePath, *imagePath;
    uint8_t *image = MAP_FAILED;
    size_t imageLen = 0;
    bool resize;
    static const int numThreads = 16;
    struct { pthread_t tid; workerArgs args; } *workerState = NULL;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s jigdo-file-name\n", argv[0]);
        goto done;
    }

    jigdoFile = strdup(argv[1]);

    // TODO support loading .jigdo file from URI and gzip compressed files
    fp = fopen(jigdoFile, "r");
    if (!fp) {
        fprintf(stderr, "Unable to open '%s' for reading\n", jigdoFile);
        goto done;
    }

    if (freadJigdoFile(fp, &jigdo)) {
            printf("Successfully read jigdo file for '%s'\n", jigdo.imageName);
            printf("Template filename is: %s\n", jigdo.templateName);
            printf("Template MD5 sum is: ");
            printMd5Sum(jigdo.templateMD5);
            printf("\n");
    } else {
            fprintf(stderr, "Failed to read jigdo file '%s'\n", argv[1]);
            goto done;
    }

    fclose(fp);

    jigdoDir = dirname(jigdoFile);
    templatePath = dircat(jigdoDir, jigdo.templateName);
    if (!templatePath) {
        fprintf(stderr, "Failed to build the template path.\n");
        goto done;
    }

    fp = fopen(templatePath, "r");
    free(templatePath);

    if (!fp) {
        fprintf(stderr, "Unable to open '%s' for reading\n", templatePath);
        goto done;
    }

    if (freadTemplateDesc(fp, NULL, &count)) {
        printf("Template DESC table contains %d entries\n", count);
    } else {
        fprintf(stderr, "Failed to enumerate template DESC table entries.\n");
        goto done;
    }

    /* Redundant calloc(3) is redundant. */
    table = calloc(count, sizeof(*table));
    if (!table) {
        fprintf(stderr, "Failed to allocate memory for template DESC table.\n");
        goto done;
    }

    if (!freadTemplateDesc(fp, &table, &count)) {
        fprintf(stderr, "Failed to read the template DESC table.\n");
        goto done;
    }

    assert(table[count-1].type == TEMPLATE_ENTRY_TYPE_IMAGE_INFO ||
           table[count-1].type == TEMPLATE_ENTRY_TYPE_IMAGE_INFO_OBSOLETE);

    imageLen = table[count-1].u.imageInfo.size;
    printf("Image size is: %"PRIu64" bytes\n", table[count-1].u.imageInfo.size);
    printf("Image md5sum is: ");
    printMd5Sum(table[count-1].u.imageInfo.md5Sum);
    printf("\n");

    imagePath = dircat(jigdoDir, jigdo.imageName);
    fd = open(imagePath, O_RDWR | O_CREAT, 0644);
    free(imagePath);
    if (fd < 0) {
        fprintf(stderr, "Failed to open image file '%s'\n", jigdo.imageName);
        goto done;
    }

#if __APPLE__
    /* Poor man's fallocate(2) for Mac OS X - much slower than the real thing */
    resize = (pwrite(fd, "\0", 1, imageLen-1) == 1);
#else
    resize = (posix_fallocate(fd, 0, imageLen) == 0);
#endif
    if (!resize) {
        fprintf(stderr, "Failed to allocate disk space for image file\n");
        goto done;
    }

    image = mmap(NULL, imageLen, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (image == MAP_FAILED) {
        fprintf(stderr, "Failed to map image file into memory\n");
        goto done;
    }

    if (!writeDataFromTemplate(fp, image, imageLen, table, count)) {
        goto done;
    }

    workerState = calloc(numThreads, sizeof(workerState[0]));
    if (!workerState) {
        fprintf(stderr, "Failed to allocate worker thread state\n");
        goto done;
    }

    for (i = 0; i < numThreads; i++) {
        workerState[i].args.jigdo = &jigdo;
        workerState[i].args.out = image;
    }

    if (pthread_mutex_init(&tableLock, NULL) != 0 ||
        pthread_mutex_init(&printLock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize mutex\n");
        goto done;
    }

    if (!fetch_init()) {
        goto done;
    }

    contiguousComplete = 0;

    /* XXX this will hang if more files error out than there are threads, and
     * do not succeed upon retry. Should implement max retries limit, perhaps
     * after exhaustively searching all mirror possibilities. */
    while (partsRemain(table, count, &contiguousComplete) > 0) {
        for (i = 0; i < numThreads; i++) {
            commitStatus status = COMMIT_STATUS_NOT_STARTED;

            if (workerState[i].args.chunk) {
                status = getStatus(workerState[i].args.chunk);
            }

            if (workerState[i].args.chunk == NULL ||
                status == COMMIT_STATUS_COMPLETE ||
                status == COMMIT_STATUS_ERROR) {

                if (workerState[i].args.chunk) {
                    if (pthread_join(workerState[i].tid, NULL) != 0) {
                        goto done;
                    }
                }

                workerState[i].args.chunk = selectChunk(table, count);

                if (!workerState[i].args.chunk) {
                    break;
                }

                if (pthread_create(&(workerState[i].tid), NULL, fetch_worker,
                                   &(workerState[i].args)) != 0) {
                    goto done;
                }
            }
        }
    }

    fetch_cleanup();

    msync(image, imageLen, MS_SYNC);

    ret = 0;

done:
    /* Clean up */
    if (fp) {
        fclose(fp);
    }

    if (image != MAP_FAILED) {
        munmap(image, imageLen);
    }

    if (fd >= 0) {
        close(fd);
    }

    free(workerState);
    free(jigdoFile);
    free(table);

    return ret;
}
