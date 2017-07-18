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
 * @brief Count the number of @c TEMPLATE_ENTRY_TYPE_FILE* entries in @p table
 *
 * @param fileBytes If non-NULL, will store the total size of the files
 */
static int countFiles(templateDescEntry *table, int count, size_t *fileBytes)
{
    int i, numFiles;

    if (fileBytes) {
        *fileBytes = 0;
    }

    for (i = numFiles = 0; i < count; i++) {
        if (table[i].type == TEMPLATE_ENTRY_TYPE_FILE ||
            table[i].type == TEMPLATE_ENTRY_TYPE_FILE_OBSOLETE) {
            numFiles++;
            if (fileBytes) {
                *fileBytes += table[i].u.file.size;
            }
        }
    }

    return numFiles;
}

/**
 * @brief Count the number of completed files in @p table
 *
 * @return number of completed files, or -1 on error
 */
static int countCompletedFiles(templateDescEntry *table, int count,
                               size_t *completedBytes)
{
    int i, numCompleted;

    if (completedBytes) {
        *completedBytes = 0;
    }

    if (pthread_mutex_lock(&tableLock) != 0) {
        return -1;
    }

    for (i = numCompleted = 0; i < count; i++) {
        if((table[i].type == TEMPLATE_ENTRY_TYPE_FILE ||
            table[i].type == TEMPLATE_ENTRY_TYPE_FILE_OBSOLETE) &&
           table[i].status == COMMIT_STATUS_COMPLETE) {
            numCompleted++;
            if (completedBytes) {
                *completedBytes += table[i].u.file.size;
            }
        }
    }

    if (pthread_mutex_unlock(&tableLock) != 0) {
        return -1;
    }

    return numCompleted;
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
    int outFd;                ///< Pointer to the output buffer
} workerArgs;

/**
 * @brief Verify the MD5 checksum of @p chunk at @buf
 */
static bool verifyChunkMD5(const void *buf, const templateDescEntry *chunk)
{
    md5Checksum md5 = md5MemOneShot(buf, chunk->u.file.size);
    return md5Cmp(&md5, &(chunk->u.file.md5Sum)) == 0;
}

/**
 * @brief Worker thread to wrap around fetch()
 */
static void *fetch_worker(void *args)
{
    workerArgs *a = (workerArgs *) args;
    char *uri = md5ToURI(a->jigdo, a->chunk->u.file.md5Sum);

    if (uri) {
        void *out;
        size_t fetched;

        out = mmap(NULL, a->chunk->u.file.size + pagemod(a->chunk->offset),
                   PROT_READ | PROT_WRITE, MAP_SHARED, a->outFd,
                   pagebase(a->chunk->offset));
        if (out == MAP_FAILED) {
            setStatus(a->chunk, COMMIT_STATUS_FATAL_ERROR);
            return NULL;
        }

        setStatus(a->chunk, COMMIT_STATUS_IN_PROGRESS);
        fetched = fetch(uri, out + pagemod(a->chunk->offset),
                        a->chunk->u.file.size);

        if (fetched == a->chunk->u.file.size &&
            verifyChunkMD5(out + pagemod(a->chunk->offset), a->chunk)) {
            msync(out, a->chunk->u.file.size, MS_SYNC);
            munmap(out, a->chunk->u.file.size);
            setStatus(a->chunk, COMMIT_STATUS_COMPLETE);
        } else {
            setStatus(a->chunk, COMMIT_STATUS_ERROR);
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
    int count, ret = 1, fd = -1, i, contiguousComplete, numFiles,
        completedFiles = -1;
    size_t fileBytes;
    char *jigdoFile = NULL, *jigdoDir, *templatePath, *imagePath;
    uint64_t imageLen;
    bool resize;
    static const int numThreads = 16;
    struct { pthread_t tid; workerArgs args; } *workerState = NULL;
    md5Checksum fileChecksum;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s jigdo-file-name\n", argv[0]);
        goto done;
    }

    jigdoFile = strdup(argv[1]);

    if (readJigdoFile(jigdoFile, &jigdo)) {
            printf("Successfully read jigdo file for '%s'\n", jigdo.imageName);
            printf("Template filename is: %s\n", jigdo.templateName);
            printf("Template MD5 sum is: ");
            printMd5Sum(jigdo.templateMD5);
            printf("\n");
    } else {
            fprintf(stderr, "Failed to read jigdo file '%s'\n", argv[1]);
            goto done;
    }

    jigdoDir = dirname(jigdoFile);

    if (isURI(jigdo.templateName) || isAbsolute(jigdo.templateName)) {
        templatePath = strdup(jigdo.templateName);
    } else {
        templatePath = dircat(jigdoDir, jigdo.templateName);
    }

    if (!templatePath) {
        fprintf(stderr, "Failed to build the template path.\n");
        goto done;
    }

    fp = fetchopen(templatePath);
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
    printf("Image size is: %"PRIu64" bytes\n", imageLen);
    printf("Image md5sum is: ");
    printMd5Sum(table[count-1].u.imageInfo.md5Sum);
    printf("\n");

    if (isURI(jigdoFile)) {
        imagePath = strdup(jigdo.imageName);
    } else {
        imagePath = dircat(jigdoDir, jigdo.imageName);
    }

    if (!imagePath) {
        goto done;
    }

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

    if (!writeDataFromTemplate(fp, fd, table, count)) {
        goto done;
    }

    workerState = calloc(numThreads, sizeof(workerState[0]));
    if (!workerState) {
        fprintf(stderr, "Failed to allocate worker thread state\n");
        goto done;
    }

    for (i = 0; i < numThreads; i++) {
        workerState[i].args.jigdo = &jigdo;
        // XXX sharing fd between threads probably kills kittens
        workerState[i].args.outFd = fd;
    }

    if (pthread_mutex_init(&tableLock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize mutex\n");
        goto done;
    }

    if (!fetch_init()) {
        goto done;
    }

    numFiles = countFiles(table, count, &fileBytes);
    contiguousComplete = 0;

    printf("\nNeed to fetch %d files (%zu kBytes total).\n",
           numFiles, fileBytes / 1024);

    /* XXX this will hang if more files error out than there are threads, and
     * do not succeed upon retry. Should implement max retries limit, perhaps
     * after exhaustively searching all mirror possibilities. */
    while (partsRemain(table, count, &contiguousComplete) > 0) {
        for (i = 0; i < numThreads; i++) {
            size_t bytes;
            commitStatus status = COMMIT_STATUS_NOT_STARTED;
            int newCompletedFiles = countCompletedFiles(table, count, &bytes);

            /* Only print the file count if it's changed to avoid excessive
             * spam in case \r doesn't work as intended. */
            if (completedFiles != newCompletedFiles) {
                completedFiles = newCompletedFiles;
                printf("Downloading files... %d of %d files "
                       "(%zu/%zu kB) complete.            \r", completedFiles,
                       numFiles, bytes / 1024, fileBytes / 1024);
                fflush(stdout);
            }

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
        usleep(123456); // No need to keep the CPU spinning in a tight loop
    }

    fetch_cleanup();

    printf("All parts downloaded. Performing final MD5 verification check...");
    fflush(stdout);

    fileChecksum = md5Fd(fd);
    ret = md5Cmp(&fileChecksum, &(table[count-1].u.imageInfo.md5Sum));

    if (ret == 0) {
        printf(" done!\n");
    } else {
        printf(" error!\n");
        printf("Expected: ");
        printMd5Sum(table[count-1].u.imageInfo.md5Sum);
        printf("; got: ");
        printMd5Sum(fileChecksum);
        printf("\n");
        fprintf(stderr, "MD5 checksum verification failed!\n");
    }

    fflush(stdout);
    fflush(stderr);

done:
    /* Clean up */
    if (fp) {
        fclose(fp);
    }

    if (fd >= 0) {
        close(fd);
    }

    free(workerState);
    free(jigdoFile);
    free(table);

    return ret;
}
