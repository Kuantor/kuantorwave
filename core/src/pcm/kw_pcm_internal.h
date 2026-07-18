/*
 * kw_pcm_internal.h — internal helpers for the PCM buffer subsystem (issue #7).
 *
 * The public PCM API (kw_pcm_alloc / kw_pcm_free, plus the kw_pcm_buffer type)
 * lives in include/kuantorwave.h. This header adds the internal helpers that
 * other core modules — decoders, encoders, the resampler — use to inspect and
 * grow buffers. Per docs/architecture.md, any core module may include this
 * header; it is never part of the public shared-library ABI.
 *
 * No codec logic belongs here: this is the pivot format and nothing else.
 */

#ifndef KW_PCM_INTERNAL_H
#define KW_PCM_INTERNAL_H

#include "kuantorwave.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bytes occupied by a single sample of the given format; 0 if unrecognised. */
size_t kw_pcm_bytes_per_sample(kw_sample_format fmt);

/* Total number of samples in the buffer: frame_count * channels.
 * Returns 0 for a NULL buffer. */
int64_t kw_pcm_sample_count(const kw_pcm_buffer *pcm);

/* Size of the data block in bytes: sample_count * bytes_per_sample.
 * Returns 0 for a NULL buffer or an unrecognised format. */
size_t kw_pcm_data_size(const kw_pcm_buffer *pcm);

/*
 * Grow or shrink a buffer's sample storage to new_frame_count frames,
 * preserving as many existing samples as fit and zero-filling any growth.
 * Format, sample rate, and channel count are unchanged.
 *
 * On failure the buffer is left untouched (strong exception guarantee).
 * Decoders that do not know the output length up front append with this.
 */
kw_result kw_pcm_resize(kw_pcm_buffer *pcm, int64_t new_frame_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KW_PCM_INTERNAL_H */
