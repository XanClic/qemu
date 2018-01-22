/*
 * Block node draining tests
 *
 * Copyright (c) 2017 Kevin Wolf <kwolf@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "block/block.h"
#include "block/blockjob_int.h"
#include "sysemu/block-backend.h"
#include "qapi/error.h"

typedef struct BDRVTestState {
    int drain_count;
} BDRVTestState;

static void coroutine_fn bdrv_test_co_drain_begin(BlockDriverState *bs)
{
    BDRVTestState *s = bs->opaque;
    s->drain_count++;
}

static void coroutine_fn bdrv_test_co_drain_end(BlockDriverState *bs)
{
    BDRVTestState *s = bs->opaque;
    s->drain_count--;
}

static void bdrv_test_close(BlockDriverState *bs)
{
    BDRVTestState *s = bs->opaque;
    g_assert_cmpint(s->drain_count, >, 0);
}

static int coroutine_fn bdrv_test_co_preadv(BlockDriverState *bs,
                                            uint64_t offset, uint64_t bytes,
                                            QEMUIOVector *qiov, int flags)
{
    /* We want this request to stay until the polling loop in drain waits for
     * it to complete. We need to sleep a while as bdrv_drain_invoke() comes
     * first and polls its result, too, but it shouldn't accidentally complete
     * this request yet. */
    qemu_co_sleep_ns(QEMU_CLOCK_REALTIME, 100000);

    return 0;
}

static BlockDriver bdrv_test = {
    .format_name            = "test",
    .instance_size          = sizeof(BDRVTestState),

    .bdrv_close             = bdrv_test_close,
    .bdrv_co_preadv         = bdrv_test_co_preadv,

    .bdrv_co_drain_begin    = bdrv_test_co_drain_begin,
    .bdrv_co_drain_end      = bdrv_test_co_drain_end,

    .bdrv_child_perm        = bdrv_format_default_perms,
};

static void aio_ret_cb(void *opaque, int ret)
{
    int *aio_ret = opaque;
    *aio_ret = ret;
}

typedef struct CallInCoroutineData {
    void (*entry)(void);
    bool done;
} CallInCoroutineData;

static coroutine_fn void call_in_coroutine_entry(void *opaque)
{
    CallInCoroutineData *data = opaque;

    data->entry();
    data->done = true;
}

static void call_in_coroutine(void (*entry)(void))
{
    Coroutine *co;
    CallInCoroutineData data = {
        .entry  = entry,
        .done   = false,
    };

    co = qemu_coroutine_create(call_in_coroutine_entry, &data);
    qemu_coroutine_enter(co);
    while (!data.done) {
        aio_poll(qemu_get_aio_context(), true);
    }
}

enum drain_type {
    BDRV_DRAIN_ALL,
    BDRV_DRAIN,
    BDRV_SUBTREE_DRAIN,
    DRAIN_TYPE_MAX,
};

static void do_drain_begin(enum drain_type drain_type, BlockDriverState *bs)
{
    switch (drain_type) {
    case BDRV_DRAIN_ALL:        bdrv_drain_all_begin(); break;
    case BDRV_DRAIN:            bdrv_drained_begin(bs); break;
    case BDRV_SUBTREE_DRAIN:    bdrv_subtree_drained_begin(bs); break;
    default:                    g_assert_not_reached();
    }
}

static void do_drain_end(enum drain_type drain_type, BlockDriverState *bs)
{
    switch (drain_type) {
    case BDRV_DRAIN_ALL:        bdrv_drain_all_end(); break;
    case BDRV_DRAIN:            bdrv_drained_end(bs); break;
    case BDRV_SUBTREE_DRAIN:    bdrv_subtree_drained_end(bs); break;
    default:                    g_assert_not_reached();
    }
}

static void test_drv_cb_common(enum drain_type drain_type, bool recursive)
{
    BlockBackend *blk;
    BlockDriverState *bs, *backing;
    BDRVTestState *s, *backing_s;
    BlockAIOCB *acb;
    int aio_ret;

    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base = NULL,
        .iov_len = 0,
    };
    qemu_iovec_init_external(&qiov, &iov, 1);

    blk = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "test-node", BDRV_O_RDWR,
                              &error_abort);
    s = bs->opaque;
    blk_insert_bs(blk, bs, &error_abort);

    backing = bdrv_new_open_driver(&bdrv_test, "backing", 0, &error_abort);
    backing_s = backing->opaque;
    bdrv_set_backing_hd(bs, backing, &error_abort);

    /* Simple bdrv_drain_all_begin/end pair, check that CBs are called */
    g_assert_cmpint(s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    do_drain_begin(drain_type, bs);

    g_assert_cmpint(s->drain_count, ==, 1);
    g_assert_cmpint(backing_s->drain_count, ==, !!recursive);

    do_drain_end(drain_type, bs);

    g_assert_cmpint(s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    /* Now do the same while a request is pending */
    aio_ret = -EINPROGRESS;
    acb = blk_aio_preadv(blk, 0, &qiov, 0, aio_ret_cb, &aio_ret);
    g_assert(acb != NULL);
    g_assert_cmpint(aio_ret, ==, -EINPROGRESS);

    g_assert_cmpint(s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    do_drain_begin(drain_type, bs);

    g_assert_cmpint(aio_ret, ==, 0);
    g_assert_cmpint(s->drain_count, ==, 1);
    g_assert_cmpint(backing_s->drain_count, ==, !!recursive);

    do_drain_end(drain_type, bs);

    g_assert_cmpint(s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    bdrv_unref(backing);
    bdrv_unref(bs);
    blk_unref(blk);
}

static void test_drv_cb_drain_all(void)
{
    test_drv_cb_common(BDRV_DRAIN_ALL, true);
}

static void test_drv_cb_drain(void)
{
    test_drv_cb_common(BDRV_DRAIN, false);
}

static void test_drv_cb_drain_subtree(void)
{
    test_drv_cb_common(BDRV_SUBTREE_DRAIN, true);
}

static void test_drv_cb_co_drain(void)
{
    call_in_coroutine(test_drv_cb_drain);
}

static void test_drv_cb_co_drain_subtree(void)
{
    call_in_coroutine(test_drv_cb_drain_subtree);
}

static void test_quiesce_common(enum drain_type drain_type, bool recursive)
{
    BlockBackend *blk;
    BlockDriverState *bs, *backing;

    blk = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "test-node", BDRV_O_RDWR,
                              &error_abort);
    blk_insert_bs(blk, bs, &error_abort);

    backing = bdrv_new_open_driver(&bdrv_test, "backing", 0, &error_abort);
    bdrv_set_backing_hd(bs, backing, &error_abort);

    g_assert_cmpint(bs->quiesce_counter, ==, 0);
    g_assert_cmpint(backing->quiesce_counter, ==, 0);

    do_drain_begin(drain_type, bs);

    g_assert_cmpint(bs->quiesce_counter, ==, 1);
    g_assert_cmpint(backing->quiesce_counter, ==, !!recursive);

    do_drain_end(drain_type, bs);

    g_assert_cmpint(bs->quiesce_counter, ==, 0);
    g_assert_cmpint(backing->quiesce_counter, ==, 0);

    bdrv_unref(backing);
    bdrv_unref(bs);
    blk_unref(blk);
}

static void test_quiesce_drain_all(void)
{
    // XXX drain_all doesn't quiesce
    //test_quiesce_common(BDRV_DRAIN_ALL, true);
}

static void test_quiesce_drain(void)
{
    test_quiesce_common(BDRV_DRAIN, false);
}

static void test_quiesce_drain_subtree(void)
{
    test_quiesce_common(BDRV_SUBTREE_DRAIN, true);
}

static void test_quiesce_co_drain(void)
{
    call_in_coroutine(test_quiesce_drain);
}

static void test_quiesce_co_drain_subtree(void)
{
    call_in_coroutine(test_quiesce_drain_subtree);
}

static void test_nested(void)
{
    BlockBackend *blk;
    BlockDriverState *bs, *backing;
    BDRVTestState *s, *backing_s;
    enum drain_type outer, inner;

    blk = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    bs = bdrv_new_open_driver(&bdrv_test, "test-node", BDRV_O_RDWR,
                              &error_abort);
    s = bs->opaque;
    blk_insert_bs(blk, bs, &error_abort);

    backing = bdrv_new_open_driver(&bdrv_test, "backing", 0, &error_abort);
    backing_s = backing->opaque;
    bdrv_set_backing_hd(bs, backing, &error_abort);

    for (outer = 0; outer < DRAIN_TYPE_MAX; outer++) {
        for (inner = 0; inner < DRAIN_TYPE_MAX; inner++) {
            /* XXX bdrv_drain_all() doesn't increase the quiesce_counter */
            int bs_quiesce      = (outer != BDRV_DRAIN_ALL) +
                                  (inner != BDRV_DRAIN_ALL);
            int backing_quiesce = (outer == BDRV_SUBTREE_DRAIN) +
                                  (inner == BDRV_SUBTREE_DRAIN);
            int backing_cb_cnt  = (outer != BDRV_DRAIN) +
                                  (inner != BDRV_DRAIN);

            g_assert_cmpint(bs->quiesce_counter, ==, 0);
            g_assert_cmpint(backing->quiesce_counter, ==, 0);
            g_assert_cmpint(s->drain_count, ==, 0);
            g_assert_cmpint(backing_s->drain_count, ==, 0);

            do_drain_begin(outer, bs);
            do_drain_begin(inner, bs);

            g_assert_cmpint(bs->quiesce_counter, ==, bs_quiesce);
            g_assert_cmpint(backing->quiesce_counter, ==, backing_quiesce);
            g_assert_cmpint(s->drain_count, ==, 2);
            g_assert_cmpint(backing_s->drain_count, ==, backing_cb_cnt);

            do_drain_end(inner, bs);
            do_drain_end(outer, bs);

            g_assert_cmpint(bs->quiesce_counter, ==, 0);
            g_assert_cmpint(backing->quiesce_counter, ==, 0);
            g_assert_cmpint(s->drain_count, ==, 0);
            g_assert_cmpint(backing_s->drain_count, ==, 0);
        }
    }

    bdrv_unref(backing);
    bdrv_unref(bs);
    blk_unref(blk);
}

static void test_multiparent(void)
{
    BlockBackend *blk_a, *blk_b;
    BlockDriverState *bs_a, *bs_b, *backing;
    BDRVTestState *a_s, *b_s, *backing_s;

    blk_a = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    bs_a = bdrv_new_open_driver(&bdrv_test, "test-node-a", BDRV_O_RDWR,
                                &error_abort);
    a_s = bs_a->opaque;
    blk_insert_bs(blk_a, bs_a, &error_abort);

    blk_b = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    bs_b = bdrv_new_open_driver(&bdrv_test, "test-node-b", BDRV_O_RDWR,
                                &error_abort);
    b_s = bs_b->opaque;
    blk_insert_bs(blk_b, bs_b, &error_abort);

    backing = bdrv_new_open_driver(&bdrv_test, "backing", 0, &error_abort);
    backing_s = backing->opaque;
    bdrv_set_backing_hd(bs_a, backing, &error_abort);
    bdrv_set_backing_hd(bs_b, backing, &error_abort);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 0);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 0);
    g_assert_cmpint(backing->quiesce_counter, ==, 0);
    g_assert_cmpint(a_s->drain_count, ==, 0);
    g_assert_cmpint(b_s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    do_drain_begin(BDRV_SUBTREE_DRAIN, bs_a);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 1);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 1);
    g_assert_cmpint(backing->quiesce_counter, ==, 1);
    g_assert_cmpint(a_s->drain_count, ==, 1);
    g_assert_cmpint(b_s->drain_count, ==, 1);
    g_assert_cmpint(backing_s->drain_count, ==, 1);

    do_drain_begin(BDRV_SUBTREE_DRAIN, bs_b);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 2);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 2);
    g_assert_cmpint(backing->quiesce_counter, ==, 2);
    g_assert_cmpint(a_s->drain_count, ==, 2);
    g_assert_cmpint(b_s->drain_count, ==, 2);
    g_assert_cmpint(backing_s->drain_count, ==, 2);

    do_drain_end(BDRV_SUBTREE_DRAIN, bs_b);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 1);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 1);
    g_assert_cmpint(backing->quiesce_counter, ==, 1);
    g_assert_cmpint(a_s->drain_count, ==, 1);
    g_assert_cmpint(b_s->drain_count, ==, 1);
    g_assert_cmpint(backing_s->drain_count, ==, 1);

    do_drain_end(BDRV_SUBTREE_DRAIN, bs_a);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 0);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 0);
    g_assert_cmpint(backing->quiesce_counter, ==, 0);
    g_assert_cmpint(a_s->drain_count, ==, 0);
    g_assert_cmpint(b_s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    bdrv_unref(backing);
    bdrv_unref(bs_a);
    bdrv_unref(bs_b);
    blk_unref(blk_a);
    blk_unref(blk_b);
}

static void test_graph_change(void)
{
    BlockBackend *blk_a, *blk_b;
    BlockDriverState *bs_a, *bs_b, *backing;
    BDRVTestState *a_s, *b_s, *backing_s;

    blk_a = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    bs_a = bdrv_new_open_driver(&bdrv_test, "test-node-a", BDRV_O_RDWR,
                                &error_abort);
    a_s = bs_a->opaque;
    blk_insert_bs(blk_a, bs_a, &error_abort);

    blk_b = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    bs_b = bdrv_new_open_driver(&bdrv_test, "test-node-b", BDRV_O_RDWR,
                                &error_abort);
    b_s = bs_b->opaque;
    blk_insert_bs(blk_b, bs_b, &error_abort);

    backing = bdrv_new_open_driver(&bdrv_test, "backing", 0, &error_abort);
    backing_s = backing->opaque;
    bdrv_set_backing_hd(bs_a, backing, &error_abort);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 0);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 0);
    g_assert_cmpint(backing->quiesce_counter, ==, 0);
    g_assert_cmpint(a_s->drain_count, ==, 0);
    g_assert_cmpint(b_s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    do_drain_begin(BDRV_SUBTREE_DRAIN, bs_a);
    do_drain_begin(BDRV_SUBTREE_DRAIN, bs_a);
    do_drain_begin(BDRV_SUBTREE_DRAIN, bs_a);
    do_drain_begin(BDRV_SUBTREE_DRAIN, bs_b);
    do_drain_begin(BDRV_SUBTREE_DRAIN, bs_b);

    bdrv_set_backing_hd(bs_b, backing, &error_abort);
    g_assert_cmpint(bs_a->quiesce_counter, ==, 5);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 5);
    g_assert_cmpint(backing->quiesce_counter, ==, 5);
    g_assert_cmpint(a_s->drain_count, ==, 5);
    g_assert_cmpint(b_s->drain_count, ==, 5);
    g_assert_cmpint(backing_s->drain_count, ==, 5);

    bdrv_set_backing_hd(bs_b, NULL, &error_abort);
    g_assert_cmpint(bs_a->quiesce_counter, ==, 3);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 2);
    g_assert_cmpint(backing->quiesce_counter, ==, 3);
    g_assert_cmpint(a_s->drain_count, ==, 3);
    g_assert_cmpint(b_s->drain_count, ==, 2);
    g_assert_cmpint(backing_s->drain_count, ==, 3);

    bdrv_set_backing_hd(bs_b, backing, &error_abort);
    g_assert_cmpint(bs_a->quiesce_counter, ==, 5);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 5);
    g_assert_cmpint(backing->quiesce_counter, ==, 5);
    g_assert_cmpint(a_s->drain_count, ==, 5);
    g_assert_cmpint(b_s->drain_count, ==, 5);
    g_assert_cmpint(backing_s->drain_count, ==, 5);

    do_drain_end(BDRV_SUBTREE_DRAIN, bs_b);
    do_drain_end(BDRV_SUBTREE_DRAIN, bs_b);
    do_drain_end(BDRV_SUBTREE_DRAIN, bs_a);
    do_drain_end(BDRV_SUBTREE_DRAIN, bs_a);
    do_drain_end(BDRV_SUBTREE_DRAIN, bs_a);

    g_assert_cmpint(bs_a->quiesce_counter, ==, 0);
    g_assert_cmpint(bs_b->quiesce_counter, ==, 0);
    g_assert_cmpint(backing->quiesce_counter, ==, 0);
    g_assert_cmpint(a_s->drain_count, ==, 0);
    g_assert_cmpint(b_s->drain_count, ==, 0);
    g_assert_cmpint(backing_s->drain_count, ==, 0);

    bdrv_unref(backing);
    bdrv_unref(bs_a);
    bdrv_unref(bs_b);
    blk_unref(blk_a);
    blk_unref(blk_b);
}


typedef struct TestBlockJob {
    BlockJob common;
    bool should_complete;
} TestBlockJob;

static void test_job_completed(BlockJob *job, void *opaque)
{
    block_job_completed(job, 0);
}

static void coroutine_fn test_job_start(void *opaque)
{
    TestBlockJob *s = opaque;

    while (!s->should_complete) {
        block_job_sleep_ns(&s->common, 100000);
    }

    block_job_defer_to_main_loop(&s->common, test_job_completed, NULL);
}

static void test_job_complete(BlockJob *job, Error **errp)
{
    TestBlockJob *s = container_of(job, TestBlockJob, common);
    s->should_complete = true;
}

BlockJobDriver test_job_driver = {
    .instance_size  = sizeof(TestBlockJob),
    .start          = test_job_start,
    .complete       = test_job_complete,
};

static void test_blockjob_common(enum drain_type drain_type)
{
    BlockBackend *blk_src, *blk_target;
    BlockDriverState *src, *target;
    BlockJob *job;
    int ret;

    src = bdrv_new_open_driver(&bdrv_test, "source", BDRV_O_RDWR,
                               &error_abort);
    blk_src = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    blk_insert_bs(blk_src, src, &error_abort);

    target = bdrv_new_open_driver(&bdrv_test, "target", BDRV_O_RDWR,
                                  &error_abort);
    blk_target = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    blk_insert_bs(blk_target, target, &error_abort);

    job = block_job_create("job0", &test_job_driver, src, 0, BLK_PERM_ALL, 0,
                           0, NULL, NULL, &error_abort);
    block_job_add_bdrv(job, "target", target, 0, BLK_PERM_ALL, &error_abort);
    block_job_start(job);

    g_assert_cmpint(job->pause_count, ==, 0);
    g_assert_false(job->paused);
    g_assert_false(job->busy); /* We're in block_job_sleep_ns() */

    do_drain_begin(drain_type, src);

    if (drain_type == BDRV_DRAIN_ALL) {
        /* bdrv_drain_all() drains both src and target */
        g_assert_cmpint(job->pause_count, ==, 2);
    } else {
        g_assert_cmpint(job->pause_count, ==, 1);
    }
    /* XXX We don't wait until the job is actually paused. Is this okay? */
    /* g_assert_true(job->paused); */
    g_assert_false(job->busy); /* The job is paused */

    do_drain_end(drain_type, src);

    g_assert_cmpint(job->pause_count, ==, 0);
    g_assert_false(job->paused);
    g_assert_false(job->busy); /* We're in block_job_sleep_ns() */

    do_drain_begin(drain_type, target);

    if (drain_type == BDRV_DRAIN_ALL) {
        /* bdrv_drain_all() drains both src and target */
        g_assert_cmpint(job->pause_count, ==, 2);
    } else {
        g_assert_cmpint(job->pause_count, ==, 1);
    }
    /* XXX We don't wait until the job is actually paused. Is this okay? */
    /* g_assert_true(job->paused); */
    g_assert_false(job->busy); /* The job is paused */

    do_drain_end(drain_type, target);

    g_assert_cmpint(job->pause_count, ==, 0);
    g_assert_false(job->paused);
    g_assert_false(job->busy); /* We're in block_job_sleep_ns() */

    ret = block_job_complete_sync(job, &error_abort);
    g_assert_cmpint(ret, ==, 0);

    blk_unref(blk_src);
    blk_unref(blk_target);
    bdrv_unref(src);
    bdrv_unref(target);
}

static void test_blockjob_drain_all(void)
{
    test_blockjob_common(BDRV_DRAIN_ALL);
}

static void test_blockjob_drain(void)
{
    test_blockjob_common(BDRV_DRAIN);
}

static void test_blockjob_drain_subtree(void)
{
    test_blockjob_common(BDRV_SUBTREE_DRAIN);
}


typedef struct BDRVTestTopState {
    BdrvChild *wait_child;
} BDRVTestTopState;

static void bdrv_test_top_close(BlockDriverState *bs)
{
    BdrvChild *c, *next_c;
    QLIST_FOREACH_SAFE(c, &bs->children, next, next_c) {
        bdrv_unref_child(bs, c);
    }
}

static int coroutine_fn bdrv_test_top_co_preadv(BlockDriverState *bs,
                                                uint64_t offset, uint64_t bytes,
                                                QEMUIOVector *qiov, int flags)
{
    BDRVTestTopState *tts = bs->opaque;
    return bdrv_co_preadv(tts->wait_child, offset, bytes, qiov, flags);
}

static BlockDriver bdrv_test_top_driver = {
    .format_name            = "test_top_driver",
    .instance_size          = sizeof(BDRVTestTopState),

    .bdrv_close             = bdrv_test_top_close,
    .bdrv_co_preadv         = bdrv_test_top_co_preadv,

    .bdrv_child_perm        = bdrv_format_default_perms,
};

typedef struct TestCoDeleteByDrainData {
    BlockBackend *blk;
    bool detach_instead_of_delete;
    bool done;
} TestCoDeleteByDrainData;

static void coroutine_fn test_co_delete_by_drain(void *opaque)
{
    TestCoDeleteByDrainData *dbdd = opaque;
    BlockBackend *blk = dbdd->blk;
    BlockDriverState *bs = blk_bs(blk);
    BDRVTestTopState *tts = bs->opaque;
    void *buffer = g_malloc(65536);
    QEMUIOVector qiov;
    struct iovec iov = {
        .iov_base = buffer,
        .iov_len  = 65536,
    };

    qemu_iovec_init_external(&qiov, &iov, 1);

    /* Pretend some internal write operation from parent to child.
     * Important: We have to read from the child, not from the parent!
     * Draining works by first propagating it all up the tree to the
     * root and then waiting for drainage from root to the leaves
     * (protocol nodes).  If we have a request waiting on the root,
     * everything will be drained before we go back down the tree, but
     * we do not want that.  We want to be in the middle of draining
     * when this following requests returns. */
    bdrv_co_preadv(tts->wait_child, 0, 65536, &qiov, 0);
    /* The drain is running concurrently, so it must have its own
     * reference to @bs */
    g_assert_cmpint(bs->refcnt, ==, 2);

    if (!dbdd->detach_instead_of_delete) {
        blk_unref(blk);
    } else {
        BdrvChild *c, *next_c;
        QLIST_FOREACH_SAFE(c, &bs->children, next, next_c) {
            bdrv_unref_child(bs, c);
        }
    }

    dbdd->done = true;
}

/**
 * Test what happens when some BDS has some children, you drain one of
 * them and this results in the BDS being deleted.
 *
 * If @detach_instead_of_delete is set, the BDS is not going to be
 * deleted but will only detach all of its children.
 */
static void do_test_delete_by_drain(bool detach_instead_of_delete)
{
    BlockBackend *blk;
    BlockDriverState *bs, *child_bs, *null_bs;
    BDRVTestTopState *tts;
    TestCoDeleteByDrainData dbdd;
    Coroutine *co;

    bs = bdrv_new_open_driver(&bdrv_test_top_driver, "top", BDRV_O_RDWR,
                              &error_abort);
    bs->total_sectors = 65536 >> BDRV_SECTOR_BITS;
    tts = bs->opaque;

    null_bs = bdrv_open("null-co://", NULL, NULL, BDRV_O_RDWR | BDRV_O_PROTOCOL,
                        &error_abort);
    bdrv_attach_child(bs, null_bs, "null-child", &child_file, &error_abort);

    /* This child will be the one to pass to requests through to, and
     * it will stall until a drain occurs */
    child_bs = bdrv_new_open_driver(&bdrv_test, "child", BDRV_O_RDWR,
                                    &error_abort);
    child_bs->total_sectors = 65536 >> BDRV_SECTOR_BITS;
    /* Takes our reference to child_bs */
    tts->wait_child = bdrv_attach_child(bs, child_bs, "wait-child", &child_file,
                                        &error_abort);

    /* This child is just there to be deleted
     * (for detach_instead_of_delete == true) */
    null_bs = bdrv_open("null-co://", NULL, NULL, BDRV_O_RDWR | BDRV_O_PROTOCOL,
                        &error_abort);
    bdrv_attach_child(bs, null_bs, "null-child", &child_file, &error_abort);

    blk = blk_new(BLK_PERM_ALL, BLK_PERM_ALL);
    blk_insert_bs(blk, bs, &error_abort);

    /* Referenced by blk now */
    bdrv_unref(bs);

    g_assert_cmpint(bs->refcnt, ==, 1);
    g_assert_cmpint(child_bs->refcnt, ==, 1);
    g_assert_cmpint(null_bs->refcnt, ==, 1);


    dbdd = (TestCoDeleteByDrainData){
        .blk = blk,
        .detach_instead_of_delete = detach_instead_of_delete,
        .done = false,
    };
    co = qemu_coroutine_create(test_co_delete_by_drain, &dbdd);
    qemu_coroutine_enter(co);

    /* Drain the child while the read operation is still pending.
     * This should result in the operation finishing and
     * test_co_delete_by_drain() resuming.  Thus, @bs will be deleted
     * and the coroutine will exit while this drain operation is still
     * in progress. */
    bdrv_ref(child_bs);
    bdrv_drain(child_bs);
    bdrv_unref(child_bs);

    while (!dbdd.done) {
        aio_poll(qemu_get_aio_context(), true);
    }

    if (detach_instead_of_delete) {
        /* Here, the reference has not passed over to the coroutine,
         * so we have to delete the BB ourselves */
        blk_unref(blk);
    }
}


static void test_delete_by_drain(void)
{
    do_test_delete_by_drain(false);
    do_test_delete_by_drain(true);
}


int main(int argc, char **argv)
{
    bdrv_init();
    qemu_init_main_loop(&error_abort);

    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/bdrv-drain/driver-cb/drain_all", test_drv_cb_drain_all);
    g_test_add_func("/bdrv-drain/driver-cb/drain", test_drv_cb_drain);
    g_test_add_func("/bdrv-drain/driver-cb/drain_subtree",
                    test_drv_cb_drain_subtree);

    // XXX bdrv_drain_all() doesn't work in coroutine context
    g_test_add_func("/bdrv-drain/driver-cb/co/drain", test_drv_cb_co_drain);
    g_test_add_func("/bdrv-drain/driver-cb/co/drain_subtree",
                    test_drv_cb_co_drain_subtree);


    g_test_add_func("/bdrv-drain/quiesce/drain_all", test_quiesce_drain_all);
    g_test_add_func("/bdrv-drain/quiesce/drain", test_quiesce_drain);
    g_test_add_func("/bdrv-drain/quiesce/drain_subtree",
                    test_quiesce_drain_subtree);

    // XXX bdrv_drain_all() doesn't work in coroutine context
    g_test_add_func("/bdrv-drain/quiesce/co/drain", test_quiesce_co_drain);
    g_test_add_func("/bdrv-drain/quiesce/co/drain_subtree",
                    test_quiesce_co_drain_subtree);

    g_test_add_func("/bdrv-drain/nested", test_nested);
    g_test_add_func("/bdrv-drain/multiparent", test_multiparent);
    g_test_add_func("/bdrv-drain/graph-change", test_graph_change);

    g_test_add_func("/bdrv-drain/blockjob/drain_all", test_blockjob_drain_all);
    g_test_add_func("/bdrv-drain/blockjob/drain", test_blockjob_drain);
    g_test_add_func("/bdrv-drain/blockjob/drain_subtree",
                    test_blockjob_drain_subtree);

    g_test_add_func("/bdrv-drain/deletion", test_delete_by_drain);

    return g_test_run();
}
