/*
 * kw_pcm.c — the PCM buffer subsystem (issue #7).
 *
 * The unified raw-audio pivot format: every decoder produces one of these,
 * every encoder consumes one. Interleaved samples, caller-owned storage.
 * Contains no codec logic.
 */

#include "kuantorwave.h"
#include "kw_pcm_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Size arithmetic with overflow guards                                */
/* ------------------------------------------------------------------ */

/*
 * Compute frames * channels * bps into *out, rejecting any overflow.
 * Returns KW_ERR_INVALID_ARG for negative inputs, KW_ERR_NOMEM if the
 * product does not fit in size_t. A zero dimension yields *out = 0.
 */
static kw_result kw_pcm_checked_size(int64_t frames, int32_t channels,
                                     size_t bps, size_t *out)
{
    uint64_t f, c, b, samples, bytes;

    *out = 0;
    if (frames < 0 || channels < 0)
        return KW_ERR_INVALID_ARG;

    f = (uint64_t)frames;
    c = (uint64_t)channels;
    b = (uint64_t)bps;

    if (f != 0 && c != 0) {
        samples = f * c;
        if (samples / f != c)          /* multiplication overflowed */
            return KW_ERR_NOMEM;
    } else {
        samples = 0;
    }

    if (samples != 0 && b != 0) {
        bytes = samples * b;
        if (bytes / samples != b)      /* multiplication overflowed */
            return KW_ERR_NOMEM;
    } else {
        bytes = 0;
    }

#if SIZE_MAX < 0xFFFFFFFFFFFFFFFFull
    if (bytes > (uint64_t)SIZE_MAX)    /* only reachable on 32-bit size_t */
        return KW_ERR_NOMEM;
#endif

    *out = (size_t)bytes;
    return KW_OK;
}

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

size_t kw_pcm_bytes_per_sample(kw_sample_format fmt)
{
    switch (fmt) {
        case KW_SAMPLE_S16: return sizeof(int16_t);
        case KW_SAMPLE_F32: return sizeof(float);
        default:            return 0;
    }
}

int64_t kw_pcm_sample_count(const kw_pcm_buffer *pcm)
{
    if (pcm == NULL)
        return 0;
    return pcm->frame_count * (int64_t)pcm->channels;
}

size_t kw_pcm_data_size(const kw_pcm_buffer *pcm)
{
    size_t bytes = 0;

    if (pcm == NULL)
        return 0;
    (void)kw_pcm_checked_size(pcm->frame_count, pcm->channels,
                              kw_pcm_bytes_per_sample(pcm->sample_format),
                              &bytes);
    return bytes;
}

kw_result kw_pcm_resize(kw_pcm_buffer *pcm, int64_t new_frame_count)
{
    size_t new_bytes, old_bytes;
    size_t bps;
    void  *p;

    if (pcm == NULL || new_frame_count < 0)
        return KW_ERR_INVALID_ARG;

    bps = kw_pcm_bytes_per_sample(pcm->sample_format);
    if (bps == 0)
        return KW_ERR_INVALID_ARG;

    if (kw_pcm_checked_size(new_frame_count, pcm->channels, bps, &new_bytes) != KW_OK)
        return KW_ERR_NOMEM;

    old_bytes = kw_pcm_data_size(pcm);

    if (new_bytes == 0) {
        free(pcm->data);
        pcm->data = NULL;
        pcm->frame_count = 0;
        return KW_OK;
    }

    p = realloc(pcm->data, new_bytes);
    if (p == NULL)
        return KW_ERR_NOMEM;            /* pcm->data left intact on failure */

    if (new_bytes > old_bytes)
        memset((unsigned char *)p + old_bytes, 0, new_bytes - old_bytes);

    pcm->data = p;
    pcm->frame_count = new_frame_count;
    return KW_OK;
}

/* ------------------------------------------------------------------ */
/* Public API (include/kuantorwave.h)                                  */
/* ------------------------------------------------------------------ */

KW_API kw_result kw_pcm_alloc(kw_sample_format  sample_format,
                              int32_t           sample_rate_hz,
                              int32_t           channels,
                              int64_t           frame_count,
                              kw_pcm_buffer   **out_pcm)
{
    kw_pcm_buffer *pcm;
    size_t         bps, total_bytes;

    if (out_pcm == NULL)
        return KW_ERR_INVALID_ARG;
    *out_pcm = NULL;

    bps = kw_pcm_bytes_per_sample(sample_format);
    if (bps == 0)                                   /* unrecognised format */
        return KW_ERR_INVALID_ARG;
    if (sample_rate_hz <= 0 || channels <= 0 || frame_count < 0)
        return KW_ERR_INVALID_ARG;

    if (kw_pcm_checked_size(frame_count, channels, bps, &total_bytes) != KW_OK)
        return KW_ERR_NOMEM;

    pcm = (kw_pcm_buffer *)calloc(1, sizeof *pcm);
    if (pcm == NULL)
        return KW_ERR_NOMEM;

    if (total_bytes > 0) {
        pcm->data = calloc(1, total_bytes);         /* zero-filled */
        if (pcm->data == NULL) {
            free(pcm);
            return KW_ERR_NOMEM;
        }
    }

    pcm->sample_format  = sample_format;
    pcm->sample_rate_hz = sample_rate_hz;
    pcm->channels       = channels;
    pcm->frame_count    = frame_count;

    *out_pcm = pcm;
    return KW_OK;
}

KW_API void kw_pcm_free(kw_pcm_buffer *pcm)
{
    if (pcm == NULL)
        return;
    free(pcm->data);
    free(pcm);
}
