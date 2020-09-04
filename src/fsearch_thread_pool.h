/*
   FSearch - A fast file search utility
   Copyright © 2016 Christian Boxdörfer

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
   */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <glib.h>

typedef struct _FsearchThreadPool FsearchThreadPool;
typedef GThreadFunc ThreadFunc;

typedef enum _FsearchThreadStatus {
    THREAD_IDLE,
    THREAD_BUSY,
    THREAD_FINISHED
} FsearchThreadStatus;

FsearchThreadPool *
fsearch_thread_pool_init (void);

void
fsearch_thread_pool_free (FsearchThreadPool *pool);

GList *
fsearch_thread_pool_get_threads (FsearchThreadPool *pool);

uint32_t
fsearch_thread_pool_get_num_threads (FsearchThreadPool *pool);

bool
fsearch_thread_pool_push_data (FsearchThreadPool *pool,
                               GList *thread,
                               ThreadFunc thread_func,
                               gpointer thread_data);

bool
fsearch_thread_pool_wait_for_thread (FsearchThreadPool *pool, GList *thread);

bool
fsearch_thread_pool_task_is_busy (FsearchThreadPool *pool, GList *thread);

bool
fsearch_thread_pool_task_is_idle (FsearchThreadPool *pool, GList *thread);

gpointer
fsearch_thread_pool_get_data (FsearchThreadPool *pool, GList *thread);

bool
fsearch_thread_pool_set_task_finished (FsearchThreadPool *pool, GList *thread);
