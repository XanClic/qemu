/*
 * block_copy API
 *
 * Copyright (C) 2013 Proxmox Server Solutions
 * Copyright (c) 2019 Virtuozzo International GmbH.
 *
 * Authors:
 *  Dietmar Maurer (dietmar@proxmox.com)
 *  Vladimir Sementsov-Ogievskiy <vsementsov@virtuozzo.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef BLOCK_COPY_H
#define BLOCK_COPY_H

#include "block/block.h"
#include "qemu/co-shared-resource.h"

typedef void (*ProgressBytesCallbackFunc)(int64_t bytes, void *opaque);
typedef void (*BlockCopyAsyncCallbackFunc)(void *opaque);
typedef struct BlockCopyState BlockCopyState;
typedef struct BlockCopyCallState BlockCopyCallState;

BlockCopyState *block_copy_state_new(BdrvChild *source, BdrvChild *target,
                                     int64_t cluster_size, bool use_copy_range,
                                     BdrvRequestFlags write_flags,
                                     Error **errp);

void block_copy_set_progress_callback(
        BlockCopyState *s,
        ProgressBytesCallbackFunc progress_bytes_callback,
        void *progress_opaque);

void block_copy_set_progress_meter(BlockCopyState *s, ProgressMeter *pm);

void block_copy_state_free(BlockCopyState *s);

int64_t block_copy_reset_unallocated(BlockCopyState *s,
                                     int64_t offset, int64_t *count);

int coroutine_fn block_copy(BlockCopyState *s, int64_t offset, int64_t bytes,
                            bool ignore_ratelimit, bool *error_is_read);

/*
 * Run block-copy in a coroutine, create corresponding BlockCopyCallState
 * object and return pointer to it. Never returns NULL.
 *
 * Caller is responsible to call block_copy_call_free() to free
 * BlockCopyCallState object.
 *
 * @max_workers means maximum of parallel coroutines to execute sub-requests,
 * must be > 0.
 *
 * @max_chunk means maximum length for one IO operation. Zero means unlimited.
 */
BlockCopyCallState *block_copy_async(BlockCopyState *s,
                                     int64_t offset, int64_t bytes,
                                     int max_workers, int64_t max_chunk,
                                     BlockCopyAsyncCallbackFunc cb,
                                     void *cb_opaque);

/*
 * Free finished BlockCopyCallState. Trying to free running
 * block-copy will crash.
 */
void block_copy_call_free(BlockCopyCallState *call_state);

/*
 * Note, that block-copy call is marked finished prior to calling
 * the callback.
 */
bool block_copy_call_finished(BlockCopyCallState *call_state);
bool block_copy_call_succeeded(BlockCopyCallState *call_state);
bool block_copy_call_failed(BlockCopyCallState *call_state);
int block_copy_call_status(BlockCopyCallState *call_state, bool *error_is_read);

void block_copy_set_speed(BlockCopyState *s, uint64_t speed);
void block_copy_kick(BlockCopyCallState *call_state);

BdrvDirtyBitmap *block_copy_dirty_bitmap(BlockCopyState *s);
void block_copy_set_skip_unallocated(BlockCopyState *s, bool skip);

#endif /* BLOCK_COPY_H */
