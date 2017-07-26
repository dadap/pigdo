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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

#include "libigdo/jigdo.h"
#include "libigdo/fetch.h"
#include "libigdo/util.h"

#include "worker.h"

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
    jigdoData *jigdo;
    templateDescTable *table;
    int ret = 1, fd = -1, i;
    bool resize;
    char *jigdoFile = NULL, *jigdoDir, *templatePath = NULL, *imagePath = NULL;
    int opt;
    char **mirrors = NULL;
    int numMirrors = 0;
    const char *progName = argv[0];
    int numWorkers = defaultNumThreads;
    const char *imageName = "", *templateName = "", *md5Hex = "";
    uint64_t imageSize;

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

    if (!fetch_init()) {
        goto done;
    }

    jigdoFile = strdup(argv[0]);

    if ((jigdo = jigdoReadJigdoFile(jigdoFile))) {
            imageName = jigdoGetImageName(jigdo);
            templateName = jigdoGetTemplateName(jigdo);

            printf("Successfully read jigdo file for '%s'\n", imageName);
            printf("Template filename is: %s\n", templateName);
            printf("Template MD5 sum is: %s\n", jigdoGetTemplateMD5(jigdo));
    } else {
            fprintf(stderr, "Failed to read jigdo file '%s'\n", jigdoFile);
            goto done;
    }

    jigdoDir = dirname(jigdoFile);

    if (!templatePath) {
        if (isURI(templateName) || isAbsolute(templateName)) {
            templatePath = strdup(templateName);
        } else {
            templatePath = dircat(jigdoDir, templateName);
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

    if (!(table = jigdoReadTemplateFile(fp))) {
        fprintf(stderr, "Failed to read the template DESC table.\n");
        goto done;
    }

    for (i = 0; i < numMirrors; i++) {
        if (!addServerMirror(jigdo, mirrors[i])) {
            fprintf(stderr, "Invalid mirror specification '%s'\n", mirrors[i]);
            goto done;
        }
    }

    md5Hex = jigdoGetImageMD5(table);
    imageSize = jigdoGetImageSize(table);

    printf("Image size is: %"PRIu64" bytes\n", imageSize);
    printf("Image md5sum is: %s\n", md5Hex);

    if (!imagePath) {
        if (isURI(jigdoFile)) {
            imagePath = strdup(imageName);
        } else {
            imagePath = dircat(jigdoDir, imageName);
        }
    }

    if (!imagePath) {
        goto done;
    }

    fd = open(imagePath, O_RDWR | O_CREAT, 0644);
    free(imagePath);

    if (fd < 0) {
        fprintf(stderr, "Failed to open image file '%s'\n", imageName);
        goto done;
    }

    if (lseek(fd, 0, SEEK_END) < imageSize) {
#ifdef HAVE_POSIX_FALLOCATE
        resize = (posix_fallocate(fd, 0, imageSize) == 0);
#else
        /* Poor man's fallocate(2); much slower than the real thing */
        resize = (pwrite(fd, "\0", 1, imageSize - 1) == 1);
#endif
        if (!resize) {
            fprintf(stderr, "Failed to allocate disk space for image file\n");
            goto done;
        }
    } else {
        jigdoSetExistingFile(table, true);
    }

    if (!writeDataFromTemplate(fp, fd, table)) {
        goto done;
    }

    if (!pfetch(fd, jigdo, table, numWorkers)) {
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
