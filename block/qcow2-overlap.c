/*
 * QCOW2 runtime metadata overlap detection
 *
 * Copyright (c) 2015 Max Reitz <mreitz@redhat.com>
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

#include "block/block_int.h"
#include "qemu-common.h"
#include "qemu/range.h"
#include "qcow2.h"

/* Number of clusters which are covered by each metadata window;
 * note that this may not exceed 2^16 as long as
 * Qcow2MetadataFragment::relative_start is a uint16_t */
#define WINDOW_SIZE 4096

/* Describes a fragment of or a whole metadata range; does not necessarily
 * describe the whole range because it needs to be split on window boundaries */
typedef struct Qcow2MetadataFragment {
    /* Bitmask of QCow2MetadataOverlap values */
    uint8_t types;
    uint8_t nb_clusters_minus_one;
    /* Number of clusters between the start of the window and this range */
    uint16_t relative_start;
} QEMU_PACKED Qcow2MetadataFragment;

typedef struct Qcow2MetadataWindow {
    /* This should normally not be NULL. However, it is possible that this list
     * would require more space than the bitmap, in which case this must be NULL
     * as long as @bitmap is not NULL.
     * Note that therefore, the size of this array in bytes may never exceed
     * WINDOW_SIZE. If that condition would arise during generation of this
     * array (from the bitmap), this field must be set to NULL and @bitmap must
     * continue to point to a valid bitmap. */
    Qcow2MetadataFragment *fragments;
    int nb_fragments, fragments_array_size;

    /* If not NULL, this is an expanded version of the "RLE" version given by
     * the fragments array; there are WINDOW_SIZE entries */
    uint8_t *bitmap;
    bool bitmap_modified;

    /* Time of last access */
    unsigned age;
} Qcow2MetadataWindow;

struct Qcow2MetadataList {
    Qcow2MetadataWindow *windows;
    uint64_t nb_windows;

    unsigned current_age;

    /* Index into the windows array */
    int *cached_windows;
    size_t nb_cached_windows;
};

/**
 * Destroys the cached window bitmap. If it has been modified, the fragment list
 * will be rebuilt accordingly.
 */
static void destroy_window_bitmap(Qcow2MetadataList *mdl,
                                  Qcow2MetadataWindow *window)
{
    Qcow2MetadataFragment *new_fragments;

    if (!window->bitmap) {
        return;
    }

    if (window->bitmap_modified) {
        int bitmap_i, fragment_i = 0;
        QCow2MetadataOverlap current_types = 0;
        int current_nb_clusters = 0;

        /* Rebuild the fragment list; the case bitmap_i == WINDOW_SIZE is for
         * entering the last fragment at the bitmap end */

        for (bitmap_i = 0; bitmap_i <= WINDOW_SIZE; bitmap_i++) {
            /* Qcow2MetadataFragment::nb_clusters_minus_one is a uint8_t, so
             * current_nb_clusters may not exceed 256 */
            if (bitmap_i < WINDOW_SIZE &&
                current_types == window->bitmap[bitmap_i] &&
                current_nb_clusters < 256)
            {
                current_nb_clusters++;
            } else {
                if (current_types && current_nb_clusters) {
                    if (fragment_i >= window->fragments_array_size) {
                        window->fragments_array_size =
                            3 * window->fragments_array_size / 2 + 1;
                        if (sizeof(Qcow2MetadataFragment)
                            * window->fragments_array_size >= WINDOW_SIZE)
                        {
                            /* There is no reason to build this fragment list
                             * over keeping the bitmap. Abort. */
                            goto fail;
                        }

                        new_fragments =
                            g_try_renew(Qcow2MetadataFragment,
                                        window->fragments,
                                        window->fragments_array_size);
                        if (!new_fragments) {
                            goto fail;
                        }
                        window->fragments = new_fragments;
                    }

                    assert(fragment_i < window->fragments_array_size);
                    window->fragments[fragment_i++] = (Qcow2MetadataFragment){
                        .types                 = current_types,
                        .nb_clusters_minus_one = current_nb_clusters - 1,
                        .relative_start        = bitmap_i - current_nb_clusters,
                    };
                }

                current_nb_clusters = 1;
                if (bitmap_i < WINDOW_SIZE) {
                    current_types = window->bitmap[bitmap_i];
                }
            }
        }

        window->nb_fragments = fragment_i;
    }

    /* Shrink window->fragments if it is possible and makes sense */
    if (window->nb_fragments < window->fragments_array_size / 2) {
        new_fragments = g_try_renew(Qcow2MetadataFragment, window->fragments,
                                    window->nb_fragments);
        if (new_fragments) {
            window->fragments = new_fragments;
            window->fragments_array_size = window->nb_fragments;
        }
    }

    g_free(window->bitmap);
    window->bitmap = NULL;

    return;

fail:
    g_free(window->fragments);
    window->fragments = NULL;
    window->nb_fragments = 0;
    window->fragments_array_size = 0;
}

/**
 * Creates a bitmap from the fragment list.
 */
static void build_window_bitmap(Qcow2MetadataList *mdl,
                                Qcow2MetadataWindow *window)
{
    int cache_i, oldest_cache_i = -1, i;
    unsigned oldest_cache_age = 0;

    for (cache_i = 0; cache_i < mdl->nb_cached_windows; cache_i++) {
        unsigned age;

        if (mdl->cached_windows[cache_i] < 0) {
            break;
        }

        age = mdl->current_age - mdl->windows[mdl->cached_windows[cache_i]].age;
        if (age > oldest_cache_age) {
            oldest_cache_age = age;
            oldest_cache_i = cache_i;
        }
    }

    if (cache_i >= mdl->nb_cached_windows) {
        destroy_window_bitmap(mdl,
            &mdl->windows[mdl->cached_windows[oldest_cache_i]]);
        cache_i = oldest_cache_i;
    }

    assert(cache_i >= 0);
    mdl->cached_windows[cache_i] = window - mdl->windows;

    window->age = mdl->current_age++;

    /* Maybe there already is a bitmap because it was more space-efficient than
     * the range list representation */
    if (window->bitmap) {
        return;
    }

    window->bitmap = g_new0(uint8_t, WINDOW_SIZE);

    for (i = 0; i < window->nb_fragments; i++) {
        Qcow2MetadataFragment *fragment = &window->fragments[i];

        memset(&window->bitmap[fragment->relative_start], fragment->types,
               fragment->nb_clusters_minus_one + 1);
    }

    window->bitmap_modified = false;
}

/**
 * Enters a new range into the metadata list.
 */
void qcow2_metadata_list_enter(BlockDriverState *bs, uint64_t offset,
                               int nb_clusters, QCow2MetadataOverlap types)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t start_cluster = offset >> s->cluster_bits;
    uint64_t end_cluster = start_cluster + nb_clusters;
    uint64_t current_cluster = start_cluster;

    types &= s->overlap_check;
    if (!types) {
        return;
    }

    if (offset_into_cluster(s, offset)) {
        /* Do not enter apparently broken metadata ranges */
        return;
    }

    while (current_cluster < end_cluster) {
        int bitmap_i;
        int bitmap_i_start = current_cluster % WINDOW_SIZE;
        int bitmap_i_end = MIN(WINDOW_SIZE,
                               end_cluster - current_cluster + bitmap_i_start);
        uint64_t window_i = current_cluster / WINDOW_SIZE;
        Qcow2MetadataWindow *window;

        if (window_i >= s->metadata_list->nb_windows) {
            /* This should not be happening too often, so it is fine to resize
             * the array to exactly the required size */
            Qcow2MetadataWindow *new_windows;

            new_windows = g_try_renew(Qcow2MetadataWindow,
                                      s->metadata_list->windows,
                                      window_i + 1);
            if (!new_windows) {
                return;
            }

            memset(new_windows + s->metadata_list->nb_windows, 0,
                   (window_i + 1 - s->metadata_list->nb_windows) *
                   sizeof(Qcow2MetadataWindow));

            s->metadata_list->windows = new_windows;
            s->metadata_list->nb_windows = window_i + 1;
        }

        window = &s->metadata_list->windows[window_i];
        if (!window->bitmap) {
            build_window_bitmap(s->metadata_list, window);
        }

        for (bitmap_i = bitmap_i_start; bitmap_i < bitmap_i_end; bitmap_i++) {
            window->bitmap[bitmap_i] |= types;
        }

        window->age = s->metadata_list->current_age++;
        window->bitmap_modified = true;

        /* Go to the next window */
        current_cluster += WINDOW_SIZE - bitmap_i_start;
    }
}

/**
 * Removes a range of the given types from the metadata list.
 */
void qcow2_metadata_list_remove(BlockDriverState *bs, uint64_t offset,
                                int nb_clusters, QCow2MetadataOverlap types)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t start_cluster = offset >> s->cluster_bits;
    uint64_t end_cluster = start_cluster + nb_clusters;
    uint64_t current_cluster = start_cluster;

    types &= s->overlap_check;
    if (!types) {
        return;
    }

    if (offset_into_cluster(s, offset)) {
        /* Try to remove even broken metadata ranges */
        end_cluster++;
    }

    while (current_cluster < end_cluster) {
        int bitmap_i;
        int bitmap_i_start = current_cluster % WINDOW_SIZE;
        int bitmap_i_end = MIN(WINDOW_SIZE,
                               end_cluster - current_cluster + bitmap_i_start);
        uint64_t window_i = current_cluster / WINDOW_SIZE;
        Qcow2MetadataWindow *window;

        /* If the list is too small, there is no metadata structure here;
         * because window_i will only grow, we can abort here */
        if (window_i >= s->metadata_list->nb_windows) {
            return;
        }

        window = &s->metadata_list->windows[window_i];
        if (!window->bitmap) {
            build_window_bitmap(s->metadata_list, window);
        }

        for (bitmap_i = bitmap_i_start; bitmap_i < bitmap_i_end; bitmap_i++) {
            window->bitmap[bitmap_i] &= ~types;
        }

        window->age = s->metadata_list->current_age++;
        window->bitmap_modified = true;

        /* Go to the next window */
        current_cluster += WINDOW_SIZE - bitmap_i_start;
    }
}

static int single_check_metadata_overlap(Qcow2MetadataList *mdl, int ign,
                                         uint64_t cluster)
{
    uint64_t window_i = cluster / WINDOW_SIZE;
    int bitmap_i = cluster % WINDOW_SIZE;
    Qcow2MetadataWindow *window;

    if (window_i >= mdl->nb_windows) {
        return 0;
    }
    window = &mdl->windows[window_i];

    if (!window->bitmap) {
        build_window_bitmap(mdl, window);
    }

    window->age = mdl->current_age++;

    return window->bitmap[bitmap_i] & ~ign;
}

int qcow2_check_metadata_overlap(BlockDriverState *bs, int ign, int64_t offset,
                                 int64_t size)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t start_cluster = offset >> s->cluster_bits;
    uint64_t end_cluster = DIV_ROUND_UP(offset + size, s->cluster_size);
    uint64_t current_cluster;
    int ret = 0;

    if (!s->metadata_list) {
        return 0;
    }

    for (current_cluster = start_cluster; current_cluster < end_cluster;
         current_cluster++)
    {
        ret |= single_check_metadata_overlap(s->metadata_list, ign,
                                             current_cluster);
    }

    return ret;
}

int qcow2_create_empty_metadata_list(BlockDriverState *bs, size_t cache_size,
                                     Error **errp)
{
    BDRVQcowState *s = bs->opaque;
    int ret;
    size_t cache_entries, i;

    s->metadata_list = g_new0(Qcow2MetadataList, 1);
    s->metadata_list->nb_windows =
        DIV_ROUND_UP(bdrv_nb_sectors(bs->file),
                     (uint64_t)s->cluster_sectors * WINDOW_SIZE);
    s->metadata_list->windows = g_try_new0(Qcow2MetadataWindow,
                                           s->metadata_list->nb_windows);
    if (s->metadata_list->nb_windows && !s->metadata_list->windows) {
        error_setg(errp, "Could not allocate metadata overlap check windows");
        ret = -ENOMEM;
        goto fail;
    }

    /* Do not count the size of s->metadata_list->cached_windows, because the
     * size per element is negligible when compared to WINDOW_SIZE, and also
     * because the user is more likely to specify multiples of WINDOW_SIZE than
     * WINDOW_SIZE + sizeof(*s->metadata_list->cached_windows). */
    cache_entries = cache_size / WINDOW_SIZE;
    if (!cache_entries) {
        cache_entries = 1;
    }

    s->metadata_list->nb_cached_windows = cache_entries;
    s->metadata_list->cached_windows = g_try_new(int, cache_entries);
    if (!s->metadata_list->cached_windows) {
        error_setg(errp, "Could not allocate metadata overlap cache pointers");
        ret = -ENOMEM;
        goto fail;
    }
    for (i = 0; i < s->metadata_list->nb_cached_windows; i++) {
        s->metadata_list->cached_windows[i] = -1;
    }

    return 0;

fail:
    qcow2_metadata_list_destroy(bs);
    return ret;
}

void qcow2_metadata_list_destroy(BlockDriverState *bs)
{
    BDRVQcowState *s = bs->opaque;
    uint64_t i;

    if (s->metadata_list && s->metadata_list->windows) {
        for (i = 0; i < s->metadata_list->nb_windows; i++) {
            Qcow2MetadataWindow *window = &s->metadata_list->windows[i];

            g_free(window->bitmap);
            g_free(window->fragments);
        }

        g_free(s->metadata_list->cached_windows);
        g_free(s->metadata_list->windows);
    }

    g_free(s->metadata_list);
    s->metadata_list = NULL;
}
