#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

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
    int count, ret = 1;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s jigdo-file-name\n", argv[0]);
        goto done;
    }

    fp = fopen(argv[1], "r"); // TODO support loading .jigdo from URI
    if (!fp) {
        fprintf(stderr, "Unable to open '%s' for reading\n", argv[1]);
        goto done;
    }

    if (freadJigdoFile(fp, &jigdo)) {
        printf("Successfully read jigdo file for '%s'\n", jigdo.imageName);
    } else {
        fprintf(stderr, "Failed to read jigdo file '%s'\n", argv[1]);
        goto done;
    }

    fclose(fp);
    fp = fopen(jigdo.templateName, "r"); // TODO resolve path relative to .jigdo
    if (!fp) {
        fprintf(stderr, "Unable to open '%s' for reading\n",
                jigdo.templateName);
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

    if (table) {
        free(table);
    }

    return ret;
}
