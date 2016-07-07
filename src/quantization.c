/* Goxel 3D voxels editor
 *
 * copyright (c) 2016 Guillaume Chereau <guillaume@noctua-software.com>
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.

 * Goxel is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.

 * You should have received a copy of the GNU General Public License along with
 * goxel.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "goxel.h"

typedef struct {
    uvec4b_t c;
    uint32_t n;
} value_t;

static UT_icd value_icd = {sizeof(value_t), NULL, NULL, NULL};

typedef struct {
    UT_array *values;
} bucket_t;

static void bucket_add(bucket_t *b, uvec4b_t c, int n, bool check)
{
    assert(b->values);
    int size = utarray_len(b->values);
    value_t v, *values = (value_t*)utarray_front(b->values);
    int i;

    assert(n);
    if (check) {
        for (i = 0; i < size; i++) {
            if (uvec4b_equal(values[i].c, c)) {
                values[i].n += n;
                return;
            }
        }
    }
    v.c = c;
    v.n = n;
    utarray_push_back(b->values, &v);
}

static int g_k; // Used in the sorting algo.
                // qsort_r is not portable!
static int value_cmp(const void *a_, const void *b_)
{
    int k = g_k;
    const value_t *a = a_;
    const value_t *b = b_;
    return sign(a->c.v[k] - b->c.v[k]);
}

// Split a bucket into two new buckets.
static void bucket_split(const bucket_t *bucket, bucket_t *a, bucket_t *b)
{
    int size = utarray_len(bucket->values);
    value_t *values = (value_t*)utarray_front(bucket->values);
    int i, j, k, nb = 0;
    uvec4b_t min_c = uvec4b(255, 255, 255, 255);
    uvec4b_t max_c = uvec4b(0, 0, 0, 0);
    // Find the channel with the max range
    for (i = 0; i < size; i++) {
        nb += values[i].n;
        for (k = 0; k < 4; k++) {
            min_c.v[k] = min(min_c.v[k], values[i].c.v[k]);
            max_c.v[k] = max(max_c.v[k], values[i].c.v[k]);
        }
    }
    k = 0;
    for (i = 0; i < 4; i++)
        if (max_c.v[i] - min_c.v[i] > max_c.v[k] - min_c.v[k])
            k = i;
    // Sort the values by color.
    g_k = k;
    qsort(values, size, sizeof(*values), value_cmp);
    // Now take the bottom half into the first buket, and the top half into
    // the second one.
    utarray_new(a->values, &value_icd);
    utarray_new(b->values, &value_icd);
    for (i = 0, j = 0; i < size; i++) {
        if (j < nb / 2)
            bucket_add(a, values[i].c, min(values[i].n, nb / 2 - j), false);
        j += values[i].n;
        if (j > nb / 2)
            bucket_add(b, values[i].c, min(values[i].n, j - nb / 2), false);
    }
}

static uvec4b_t bucket_average_color(const bucket_t *b)
{
    int size = utarray_len(b->values);
    value_t *values = (value_t*)utarray_front(b->values);
    int s[4] = {}, n = 0, i, k;
    if (size == 0) return uvec4b_zero;
    for (i = 0; i < size; i++) {
        assert(values[i].n);
        n += values[i].n;
        for (k = 0; k < 4; k++)
            s[k] += values[i].n * values[i].c.v[k];
    }
    return uvec4b(s[0] / n, s[1] / n, s[2] / n, s[3] / n);
}

static int bucket_cmp(const void *a_, const void *b_)
{
    const bucket_t *a = (void*)a_;
    const bucket_t *b = (void*)b_;
    int na = a->values ? utarray_len(a->values) : -1;
    int nb = b->values ? utarray_len(b->values) : -1;
    return sign(nb - na);
}

// Generate an optimal palette whith a fixed number of colors from a mesh.
// This is based on https://en.wikipedia.org/wiki/Median_cut.
void quantization_gen_palette(const mesh_t *mesh, int nb, uvec4b_t *palette)
{
    block_t *block;
    uvec4b_t v;
    int x, y, z, i;
    bucket_t *buckets, b;

    buckets = calloc(nb, sizeof(*buckets));

    // Fill the initial bucket.
    utarray_new(buckets[0].values, &value_icd);
    MESH_ITER_VOXELS(mesh, block, x, y, z, v) {
        if (v.a < 127) continue;
        v.a = 255;
        bucket_add(&buckets[0], v, 1, true);
    }

    // Split until we get nb buckets.  I do it a bit stupidly, by sorting
    // the buckets at every iterations!  I should use a stack!
    while (!buckets[nb - 1].values) {
        assert(!buckets[nb - 1].values);
        b = buckets[0];
        memset(&buckets[0], 0, sizeof(buckets[0]));
        bucket_split(&b, &buckets[0], &buckets[nb - 1]);
        utarray_free(b.values);
        qsort(buckets, nb, sizeof(*buckets), bucket_cmp);
    }

    // Fill the palette colors and cleanup.
    for (i = 0; i < nb; i++) {
        assert(buckets[i].values);
        palette[i] = bucket_average_color(&buckets[i]);
        utarray_free(buckets[i].values);
    }
    free(buckets);
}
