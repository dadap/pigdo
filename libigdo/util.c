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
#include <string.h>
#include <unistd.h>

#include "util.h"

char *dircat(const char *dir, const char *file)
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

off_t pagemod(off_t offset)
{
    return offset % getpagesize();
}

off_t pagebase(off_t offset)
{
    return offset - pagemod(offset);
}

bool isAbsolute(const char *path)
{
    return path[0] == '/';
}
