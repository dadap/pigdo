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

#ifndef PIGDO_WORKER_H
#define PIGDO_WORKER_H

#include <stdbool.h>

#include "libigdo/jigdo.h"
#include "libigdo/jigdo-template.h"

#define defaultNumThreads 16

/*
 * @brief Kick off worker threads to download files to @p fd
 */
bool pfetch(int fd, jigdoData *jigdo, templateDescTable table, int numWorkers);

#endif
