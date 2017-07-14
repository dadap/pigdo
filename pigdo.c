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
#include <assert.h>
#include <libgen.h>
#include <string.h>

#include "jigdo.h"

/**
 * @brief print a hexadecimal string representing @p md5 to stdout
 */
static void printMd5Sum(md5Checksum md5)
{
    int i;
    for (i = 0; i < (sizeof(md5) / sizeof(md5.sum[0])); i++) {
        int j;
        for (j = 0; j < sizeof(md5.sum[0]); j++) {
            printf("%x", *((uint8_t*) &md5.sum[i] + j));
        }
    }
}

int main(int argc, const char * const * argv)
{
    FILE *fp = NULL;
    jigdoData jigdo;
    templateDescEntry *table = NULL;
    int count, ret = 1, len;
    char *jigdoFile = NULL, *jigdoDir, *templatePath;

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
    len = strlen(jigdoDir) + strlen(jigdo.templateName) + 2 /* '/' and '\0' */;
    templatePath = calloc(len, 1);
    if (!templatePath) {
        fprintf(stderr, "Failed to allocate memory for the template path.\n");
        goto done;
    }

    // asprintf(3) would be nice, but it's not part of any standard
    if (snprintf(templatePath, len, "%s/%s", jigdoDir, jigdo.templateName) !=
        len - 1) {
        fprintf(stderr, "Unexpected character count returned by snprintf\n");
        free(templatePath);
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

    printf("Image size is: %llu bytes\n", table[count-1].u.imageInfo.size);
    printf("Image md5sum is: ");
    printMd5Sum(table[count-1].u.imageInfo.md5Sum);
    printf("\n");

    ret = 0;

done:
    /* Clean up */
    if (fp) {
        fclose(fp);
    }

    free(jigdoFile);
    free(table);

    return ret;
}
