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

/**
 * @brief Lock on DESC table entry management
 */
static pthread_mutex_t tableLock;

/**
 * @brief Concatenate a directory and file name, with a '/' in between
 *
 * @param dir The name of the directory
 * @param dir The name of the file
 *
 * @return @c dir/file on success, or NULL on error
 */
static char *dircat(const char *dir, const char *file)
{
    char *ret;
    int len = strlen(dir) + strlen(file) + 2 /* '/' and '\0' */;

    ret = calloc(len, 1);

    if (ret) {
        // asprintf(3) would be nice, but it's not part of any standard
        if (snprintf(ret, len, "%s/%s", dir, file) != len - 1) {
            free(ret);
            ret = NULL;
        }
    }

    return ret;
}

static bool partsRemain(templateDescEntry *table, int count)
{
    int i;
    bool ret = false;

    if (pthread_mutex_lock(&tableLock) != 0) {
        /* Something horrible has happened; break out of the loop in main() */
        return false;
    }

    for (i = 0; i < count; i++) {
        if (table[i].status != COMMIT_STATUS_COMPLETE) {
            ret = true;
            break;
        }
    }

    if (pthread_mutex_unlock(&tableLock) != 0) {
        ret = false;
    }

    return ret;
}

typedef struct {
    jigdoData *jigdo;
    templateDescEntry *chunk;
} workerArgs;

int main(int argc, const char * const * argv)
{
    FILE *fp = NULL;
    jigdoData jigdo;
    templateDescEntry *table = NULL;
    int count, ret = 1, fd = -1, i;
    char *jigdoFile = NULL, *jigdoDir, *templatePath, *imagePath;
    uint8_t *image = MAP_FAILED;
    size_t imageLen = 0;
    bool resize;
    static const int numThreads = 4;
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
    /* Poor man's fallocate(2) - much slower than the real thing */
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

    workerState = calloc(numThreads, sizeof(workerState));
    if (!workerState) {
        fprintf(stderr, "Failed to allocate worker thread state\n");
        goto done;
    }

    for (i = 0; i < numThreads; i++) {
        workerState[i].args.jigdo = &jigdo;
    }

    if (pthread_mutex_init(&tableLock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize mutex\n");
        goto done;
    }

    /* TODO Don't actually iterate the loop until rebuilding is implemented */
    while (0 && partsRemain(table, count)) {

    }

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
