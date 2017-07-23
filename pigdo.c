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
#include <getopt.h>
#include <signal.h>

#include "jigdo.h"
#include "jigdo-template.h"
#include "jigdo-md5.h"
#include "fetch.h"
#include "util.h"
#include "config.h"

static pthread_mutex_t tableLock;  ///< @brief Lock on DESC table management
static pthread_mutex_t workerLock; ///< @brief Lock on worker state
static bool lockInit = false;

/**
 * @brief Determine whether any parts still need to be fetched
 *
 * @return 0 if all parts are complete, positive if parts still need to be
 *           fetched, and negative if an unrecoverable error occurred.
 */
static int partsRemain(templateFileEntry *files, int count, int *beginComplete)
{
    int i;
    bool ret = 0;

    if (pthread_mutex_lock(&tableLock) != 0) {
        /* Something horrible has happened; break out of the loop in main() */
        return -1;
    }

    for (i = *beginComplete; i < count - 1; i++) {
        bool breakLoop = false;

        switch(files[i].status) {
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
 * @brief Count the number of completed files in @p files
 *
 * @return number of completed files, or -1 on error
 */
static int countCompletedFiles(templateFileEntry *files, int count,
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
        if(files[i].status == COMMIT_STATUS_COMPLETE) {
            numCompleted++;
            if (completedBytes) {
                *completedBytes += files[i].size;
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
static commitStatus getStatus(templateFileEntry *chunk)
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
static bool isWaitingFileNoMutex(templateFileEntry *chunk)
{
    return (chunk->status == COMMIT_STATUS_NOT_STARTED ||
            chunk->status == COMMIT_STATUS_ERROR ||
            chunk->status == COMMIT_STATUS_LOCAL_COPY);
}

/**
 * @brief Scan @p files for the next unfetched chunk
 */
static templateFileEntry *selectChunk(templateFileEntry *files, int count)
{
    int i;

    if (pthread_mutex_lock(&tableLock) != 0) {
        return NULL;
    }

    /* Searching for the next available file and assigning it should happen
     * atomically, so don't release tableLock until assigned. */
    for (i = 0; i < count && !isWaitingFileNoMutex(files + i); i++);

    files[i].status = COMMIT_STATUS_ASSIGNED;

    if (pthread_mutex_unlock(&tableLock) != 0) {
        return NULL;
    }

    if (i == count) {
        return NULL; // Not an error; we've just reached the end.
    }

    return files + i;
}

/**
 * @brief Assign @p status to @p chunk
 */
static void setStatus(templateFileEntry *chunk, commitStatus status)
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
    templateFileEntry *chunk; ///< Pointer to the chunk this worker will work on
    int outFd;                ///< Pointer to the output buffer
    ssize_t fetchedBytes;     ///< Bytes fetched so far
    char *uri;                ///< URI being fetched
} workerArgs;

/**
 * @brief Verify the MD5 checksum of @p chunk at @buf
 */
static bool verifyChunkMD5(const void *buf, const templateFileEntry *chunk)
{
    md5Checksum md5 = md5MemOneShot(buf, chunk->size);
    return md5Cmp(&md5, &(chunk->md5Sum)) == 0;
}

/**
 * @brief Worker thread to wrap around fetch()
 */
static void *fetch_worker(void *args)
{
    workerArgs *a = (workerArgs *) args;
    a->uri = md5ToURI(a->jigdo, a->chunk->md5Sum);

    if (a->uri) {
        void *out;
        size_t fetched;

        out = mmap(NULL, a->chunk->size + pagemod(a->chunk->offset),
                   PROT_READ | PROT_WRITE, MAP_SHARED, a->outFd,
                   pagebase(a->chunk->offset));
        if (out == MAP_FAILED) {
            setStatus(a->chunk, COMMIT_STATUS_FATAL_ERROR);
            goto done;
        }

        setStatus(a->chunk, COMMIT_STATUS_IN_PROGRESS);
        fetched = fetch(a->uri, out + pagemod(a->chunk->offset),
                        a->chunk->size, &(a->fetchedBytes));

        if (fetched == a->chunk->size &&
            verifyChunkMD5(out + pagemod(a->chunk->offset), a->chunk)) {
            msync(out, a->chunk->size, MS_SYNC);
            munmap(out, a->chunk->size);
            setStatus(a->chunk, COMMIT_STATUS_COMPLETE);
        } else {
            setStatus(a->chunk, COMMIT_STATUS_ERROR);
        }
    } else {
        setStatus(a->chunk, COMMIT_STATUS_FATAL_ERROR);
    }

done:
    free(a->uri);

    return NULL;
}

/*
 * @brief Sum up the total size of all file parts combined
 *
 * @param table Table containing files with sizes to sum
 * @param incomplete If non-NULL, total size of incomplete files is stored here
 *
 * @return Sum of the sizes of all files in @p table
 */
static size_t fileSizeTotal(const templateDescTable *table,
                            size_t *incomplete)
{
    int i;
    size_t ret = 0;

    if (incomplete) {
        *incomplete  = 0;
    }

    for (i = 0; i < table->numFiles; i++) {
        ret += table->files[i].size;
        if (incomplete && table->files[i].status != COMMIT_STATUS_COMPLETE) {
            *incomplete += table->files[i].size;
        }
    }

    return ret;
}

/*
 * @brief Comparator function to allow sorting files in reverse size order
 */
static int fileRevSizeCmp(const void *a, const void *b)
{
    const templateFileEntry *fileA = (templateFileEntry *) a;
    const templateFileEntry *fileB = (templateFileEntry *) b;

    return fileB->size - fileA->size;
}

/*
 * @brief Scan a partially downloaded file and mark valid files as complete
 *
 * @param fd An open file descriptor to the file to scan
 * @param table Table with templateFileEntry records to verify and mark complete
 *
 * @return Number of verified files, or -1 on error
 */
static int verifyPartial(int fd, templateDescTable *table)
{
    int i, complete = 0;

    if (!table->existingFile) {
        return 0;
    }

    printf("Verifying partially downloaded file:\n");

    for (i = 0; i < table->numFiles; i++) {
        void *map;

        // Ignore files that were found locally
        if (table->files[i].status == COMMIT_STATUS_LOCAL_COPY) {
            continue;
        }

        map = mmap(NULL, table->files[i].size + pagemod(table->files[i].offset),
                   PROT_READ, MAP_SHARED, fd, pagebase(table->files[i].offset));
        if (map == MAP_FAILED) {
            return -1;
        }

        if (verifyChunkMD5(map + pagemod(table->files[i].offset),
                           table->files + i)) {
            table->files[i].status = COMMIT_STATUS_COMPLETE;
            complete++;
        }

        if (munmap(map, table->files[i].size + pagemod(table->files[i].offset))
            != 0) {
            return -1;
        }

        printf("\r%d out of %d files OK", complete, table->numFiles);
        fflush(stdout);
    }

    printf("\n");

    return complete;
}

/**
 * @brief Populate @p fd with any local matches for files making up @p jigdo
 *
 * @param fd An open file descriptor to the output file
 * @param jigdo Parsed data for the .jigdo file
 *
 * @return Number of locally matched files, or -1 on error
 */
static int findLocalFiles(int fd, templateDescTable *table, jigdoData *jigdo)
{
    int count, i, n;

    for (count = i = 0; i < table->numFiles; i++) {
        jigdoFileInfo *file = findFileByMD5(jigdo, table->files[i].md5Sum, &n);
        int localDirIndex;

        if (!file) {
            return -1;
        }

        localDirIndex = findLocalCopy(file);

        if (localDirIndex >= 0) {
            file->localMatch = localDirIndex;
            count++;
            table->files[i].status = COMMIT_STATUS_LOCAL_COPY;
        }
    }

    return count;
}

#define defaultNumThreads 16

static struct { pthread_t tid; workerArgs args; } *workerState = NULL;
static int numWorkers = defaultNumThreads;

static void printProgress(int sig)
{
    if (!lockInit) {
        return;
    }

    pthread_mutex_lock(&workerLock);

    if (workerState) {
        int i;

        for (i = 0; i < numWorkers; i++) {
            if (!workerState[i].args.chunk) {
                continue;
            }

            printf("%s: %zi/%"PRIu64" bytes\n", workerState[i].args.uri,
                   workerState[i].args.fetchedBytes,
                   workerState[i].args.chunk->size);
        }
    }

    pthread_mutex_unlock(&workerLock);
}

/*
 * @brief Kick off worker threads to download files to @p fd
 */
static bool pfetch(int fd, jigdoData jigdo, templateDescTable table)
{
    bool ret = false;
    int i, contiguousComplete, completedFiles = 0, localFiles = 0;
    size_t fileBytes, fileIncompleteBytes;
    md5Checksum fileChecksum;

    if (pthread_mutex_init(&workerLock, NULL) == 0 &&
        pthread_mutex_init(&tableLock, NULL) == 0) {
        lockInit = true;
    } else {
        fprintf(stderr, "Failed to initialize mutex\n");
        goto done;
    }

    pthread_mutex_lock(&workerLock);

    if (workerState != NULL) {
        goto done;
    }

    workerState = calloc(numWorkers, sizeof(workerState[0]));
    if (!workerState) {
        fprintf(stderr, "Failed to allocate worker thread state\n");
        goto done;
    }

    for (i = 0; i < numWorkers; i++) {
        workerState[i].args.jigdo = &jigdo;
        // XXX sharing fd between threads probably kills kittens
        workerState[i].args.outFd = fd;
    }

    pthread_mutex_unlock(&workerLock);

    localFiles = findLocalFiles(fd, &table, &jigdo);
    if (localFiles > 0) {
        printf("%d files were found locally and do not need to be fetched.\n",
               localFiles);
    }

    completedFiles = verifyPartial(fd, &table);
    if (completedFiles < 0) {
        goto done;
    }

    contiguousComplete = 0;
    fileBytes = fileSizeTotal(&table, &fileIncompleteBytes);

    printf("\nNeed to fetch %d files (%zu kBytes total).\n",
           table.numFiles - completedFiles - localFiles,
           fileIncompleteBytes / 1024);

    /* XXX this will hang if more files error out than there are threads, and
     * do not succeed upon retry. Should implement max retries limit, perhaps
     * after exhaustively searching all mirror possibilities. */
    while (partsRemain(table.files, table.numFiles, &contiguousComplete) > 0) {
        pthread_mutex_lock(&workerLock);

        for (i = 0; i < numWorkers; i++) {
            size_t bytes;
            commitStatus status = COMMIT_STATUS_NOT_STARTED;
            int newCompletedFiles = countCompletedFiles(table.files,
                                                        table.numFiles,
                                                        &bytes);

            /* Only print the file count if it's changed to avoid excessive
             * spam in case \r doesn't work as intended. */
            if (completedFiles != newCompletedFiles) {
                completedFiles = newCompletedFiles;
                printf("\r%d of %d files (%zu/%zu kB) done",
                       completedFiles, table.numFiles, bytes / 1024,
                       fileBytes / 1024);
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

                workerState[i].args.chunk = selectChunk(table.files,
                                                        table.numFiles);

                if (!workerState[i].args.chunk) {
                    break;
                }

                if (pthread_create(&(workerState[i].tid), NULL, fetch_worker,
                                   &(workerState[i].args)) != 0) {
                    goto done;
                }
            }

            pthread_mutex_unlock(&workerLock);
        }
        usleep(12345); // No need to keep the CPU spinning in a tight loop
    }

    printf("\rAll parts assembled. Performing final MD5 verification check...");
    fflush(stdout);

    fileChecksum = md5Fd(fd);
    ret = md5Cmp(&fileChecksum, &(table.imageInfo.md5Sum)) == 0;

    if (ret) {
        printf(" done!\n");
    } else {
        printf(" error!\n");
        printf("Expected: ");
        printMd5Sum(table.imageInfo.md5Sum);
        printf("; got: ");
        printMd5Sum(fileChecksum);
        printf("\n");
        fprintf(stderr, "MD5 checksum verification failed!\n");
    }

    fflush(stdout);
    fflush(stderr);

done:

    if (lockInit) {
        lockInit = false;

        pthread_mutex_lock(&workerLock);
        free(workerState);
        workerState = NULL;
        pthread_mutex_unlock(&workerLock);

        pthread_mutex_destroy(&workerLock);
        pthread_mutex_destroy(&tableLock);
    }

    return ret;
}

/*
 * @brief print a usage message and exit
 */
void usage(const char *progName)
{
    fprintf(stderr,
            "Usage: %s jigdofile \\\n    "
            "[-o output] [-t template] [-j threads] [-m mirror=path ...]\n\n"
            "jigdofile:       location of the .jigdo file\n\n"
            "-o | --output:   location where output file will be written\n"
            "                 default: use filename specified in the .jigdo\n"
            "                 file, and save in same directory as the .jigdo\n"
            "                 file, or in the current directory if the .jigdo\n"
            "                 file was fetched remotely\n\n"
            "-t | --template: location of the .template file\n"
            "                 default: use filename specified in the .jigdo\n"
            "                 file, resolved relative to the location of the\n"
            "                 .jigdo file\n\n"
            "-j | --threads:  number of simultaneous download threads\n"
            "                 default: %d\n\n"
            "-m | --mirror:   map a mirror name to a URI in 'mirror=path'\n"
            "                 format, where 'mirror' is the name of a mirror\n"
            "                 as specified in the .jigdo file, and 'path' is\n"
            "                 a remote URI or local path where file paths in\n"
            "                 the .jigdo file will be mapped\n",
            progName, defaultNumThreads);
    exit(1);
}

int main(int argc, char * const * argv)
{
    FILE *fp = NULL;
    jigdoData jigdo;
    templateDescTable table;
    int ret = 1, fd = -1, i;
    bool resize;
    char *jigdoFile = NULL, *jigdoDir, *templatePath = NULL, *imagePath = NULL;
    int opt;
    char **mirrors = NULL;
    int numMirrors = 0;
    const char *progName = argv[0];

    static struct option opts[] = {
        {"mirror",      required_argument, NULL, 'm'},
        {"output",      required_argument, NULL, 'o'},
        {"template",    required_argument, NULL, 't'},
        {"threads",     required_argument, NULL, 'j'},
        {NULL,          0,                 NULL,  0 }
    };

    while ((opt = getopt_long(argc, argv, "m:o:t:j:", opts, NULL)) != -1) {
        switch(opt) {
            case 'm':
                mirrors = realloc(mirrors, (numMirrors + 1) * sizeof(char *));
                mirrors[numMirrors++] = strdup(optarg);
                break;
            case 'o':
                imagePath = strdup(optarg);
                break;
            case 't':
                templatePath = strdup(optarg);
                break;
            case 'j':
                if (sscanf(optarg, "%d", &numWorkers) != 1 || numWorkers < 0 ) {
                    usage(progName);
                }
                break;
            default:
                usage(progName);
        }
    }
    argc -= optind;
    argv += optind;

    if (argc < 1) {
        usage(progName);
    }

    if (signal(SIGUSR1, printProgress) == SIG_ERR) {
        goto done;
    }

    if (!fetch_init()) {
        goto done;
    }

    jigdoFile = strdup(argv[0]);

    if (readJigdoFile(jigdoFile, &jigdo)) {
            printf("Successfully read jigdo file for '%s'\n", jigdo.imageName);
            printf("Template filename is: %s\n", jigdo.templateName);
            printf("Template MD5 sum is: ");
            printMd5Sum(jigdo.templateMD5);
            printf("\n");
    } else {
            fprintf(stderr, "Failed to read jigdo file '%s'\n", jigdoFile);
            goto done;
    }

    jigdoDir = dirname(jigdoFile);

    if (!templatePath) {
        if (isURI(jigdo.templateName) || isAbsolute(jigdo.templateName)) {
            templatePath = strdup(jigdo.templateName);
        } else {
            templatePath = dircat(jigdoDir, jigdo.templateName);
        }
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

    if (!freadTemplateDesc(fp, &table)) {
        fprintf(stderr, "Failed to read the template DESC table.\n");
        goto done;
    }

    for (i = 0; i < numMirrors; i++) {
        if (!addServerMirror(&jigdo, mirrors[i])) {
            fprintf(stderr, "Invalid mirror specification '%s'\n", mirrors[i]);
            goto done;
        }
    }

    /* Download the largest files first, to maximize the parallelism */
    qsort(table.files, table.numFiles, sizeof(table.files[0]), fileRevSizeCmp);

    printf("Image size is: %"PRIu64" bytes\n", table.imageInfo.size);
    printf("Image md5sum is: ");
    printMd5Sum(table.imageInfo.md5Sum);
    printf("\n");

    if (!imagePath) {
        if (isURI(jigdoFile)) {
            imagePath = strdup(jigdo.imageName);
        } else {
            imagePath = dircat(jigdoDir, jigdo.imageName);
        }
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

    if (lseek(fd, 0, SEEK_END) < table.imageInfo.size) {
#ifdef HAVE_POSIX_FALLOCATE
        resize = (posix_fallocate(fd, 0, table.imageInfo.size) == 0);
#else
        /* Poor man's fallocate(2); much slower than the real thing */
        resize = (pwrite(fd, "\0", 1, table.imageInfo.size - 1) == 1);
#endif
        if (!resize) {
            fprintf(stderr, "Failed to allocate disk space for image file\n");
            goto done;
        }
    } else {
        table.existingFile = true;
    }

    if (!writeDataFromTemplate(fp, fd, &table)) {
        goto done;
    }

    if (!pfetch(fd, jigdo, table)) {
        goto done;
    }

    ret = 0;

done:
    if (ret != 0) {
        fprintf(stderr, "Reconstruction failed!\n");
    }

    /* Clean up */
    if (fp) {
        fclose(fp);
    }

    if (fd >= 0) {
        close(fd);
    }

    if (mirrors) {
        for (i = 0; i < numMirrors; i++) {
            free(mirrors[i]);
        }
        free(mirrors);
    }

    fetch_cleanup();
    free(jigdoFile);

    return ret;
}
