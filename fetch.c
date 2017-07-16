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

#include <string.h>
#include <curl/curl.h>

#include "fetch.h"

static bool initialized = false;

/**
 * @brief Arguments for the custom @c CURLOPT_WRITEFUNCTION callback
 */
typedef struct {
    void *base;      ///< Start of the output buffer
    size_t written;  ///< Bytes written so far
    size_t capacity; ///< Total capacity of the output buffer
} memInfo;

static size_t fetchToMem(void *in, size_t size, size_t nmemb, void *private)
{
    memInfo *info = (memInfo *) private;
    size_t wanted = size * nmemb;

    if (wanted + info->written > info->capacity) {
        return 0;
    }

    memcpy(info->base + info->written, in, wanted);
    info->written += wanted;

    return wanted;
}

bool fetch_init(void)
{
    if (!initialized && curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
        initialized = true;
    }

    return initialized;
}

void fetch_cleanup(void)
{
    if (initialized) {
        curl_global_cleanup();
    }

    initialized = false;
}

ssize_t fetch(const char *uri, void *out, size_t outBytes)
{
    memInfo info;
    ssize_t ret = -1;
    CURL *curl = NULL;

    if (!initialized) {
        goto done;
    }

    curl = curl_easy_init();
    if (!curl) {
        goto done;
    }

    if (curl_easy_setopt(curl, CURLOPT_URL, uri) != CURLE_OK) {
        goto done;
    }

    if (curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fetchToMem) != CURLE_OK) {
        goto done;
    }

    /* In case of HTTP 30x redirects */
    if (curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1) != CURLE_OK) {
        goto done;
    }

    info.base = out;
    info.written = 0;
    info.capacity = outBytes;

    if (curl_easy_setopt(curl, CURLOPT_WRITEDATA, &info) != CURLE_OK) {
        goto done;
    }

    if (curl_easy_perform(curl) != CURLE_OK) {
        goto done;
    }

    ret = info.written;

done:
    if (curl) {
        curl_easy_cleanup(curl);
    }

    return ret;
}
