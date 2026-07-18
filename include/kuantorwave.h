/*
 * kuantorwave.h — public C API of the KuantorWave audio conversion core.
 *
 * This header is the single contract between the compiled core
 * (.so / .dll / .dylib) and every consumer, including the Python
 * wrapper in Kuantor/kuantorwave_cli (bound via ctypes, see
 * docs/decisions/0001-python-binding-mechanism.md).
 *
 * ABI rules:
 *   - C ABI only: no C++ types, no structs passed by value across the boundary.
 *   - Every function returns kw_result; output values go through out-parameters.
 *   - Buffers created by the core are released with kw_pcm_free(), never free().
 */

#ifndef KUANTORWAVE_H
#define KUANTORWAVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(KW_STATIC)
#  define KW_API /* static linking: no import/export decoration */
#elif defined(_WIN32)
#  if defined(KW_BUILD_DLL)
#    define KW_API __declspec(dllexport)
#  else
#    define KW_API __declspec(dllimport)
#  endif
#else
#  define KW_API __attribute__((visibility("default")))
#endif

/* Incremented on every breaking change to this header. */
#define KW_ABI_VERSION 1

/* ------------------------------------------------------------------ */
/* Result codes                                                        */
/* ------------------------------------------------------------------ */

typedef enum kw_result {
    KW_OK                     = 0,
    KW_ERR_INVALID_ARG        = 1,  /* NULL pointer, bad enum value, bad rate */
    KW_ERR_IO                 = 2,  /* file missing, unreadable, unwritable   */
    KW_ERR_UNSUPPORTED_FORMAT = 3,  /* detection succeeded, codec unsupported */
    KW_ERR_UNKNOWN_FORMAT     = 4,  /* input matches no known signature       */
    KW_ERR_CORRUPTED          = 5,  /* stream damaged mid-decode              */
    KW_ERR_DECODE             = 6,  /* decoder failure other than corruption  */
    KW_ERR_ENCODE             = 7,  /* encoder failure or bad parameter combo */
    KW_ERR_RESAMPLE           = 8,
    KW_ERR_NOMEM              = 9,
    KW_ERR_ENGINE_NOT_LOADED  = 10  /* API used before kw_load_engine()       */
} kw_result;

/* ------------------------------------------------------------------ */
/* Formats and codecs                                                  */
/* ------------------------------------------------------------------ */

/* Detected container/codec of an input file. */
typedef enum kw_format {
    KW_FORMAT_UNKNOWN = 0,
    KW_FORMAT_MP3     = 1,
    KW_FORMAT_M4A_AAC = 2,   /* AAC in an MP4/M4A container */
    KW_FORMAT_WAV     = 3
} kw_format;

/* Target codec for encoding. */
typedef enum kw_codec {
    KW_CODEC_MP3 = 1,
    KW_CODEC_AAC = 2         /* written into an M4A container */
} kw_codec;

/* ------------------------------------------------------------------ */
/* PCM buffer — the unified intermediate format                        */
/* ------------------------------------------------------------------ */

typedef enum kw_sample_format {
    KW_SAMPLE_S16 = 1,       /* int16_t, native endianness */
    KW_SAMPLE_F32 = 2        /* float, [-1.0, 1.0]         */
} kw_sample_format;

/*
 * Interleaved PCM audio. `data` holds frame_count * channels samples.
 * Instances created by the core own their data; release with kw_pcm_free().
 */
typedef struct kw_pcm_buffer {
    kw_sample_format sample_format;
    int32_t  sample_rate_hz;
    int32_t  channels;
    int64_t  frame_count;    /* samples per channel */
    void    *data;
} kw_pcm_buffer;

/* ------------------------------------------------------------------ */
/* Metadata                                                            */
/* ------------------------------------------------------------------ */

/* A field the source file does not provide is set to -1 (KW_META_UNKNOWN). */
#define KW_META_UNKNOWN (-1)

typedef struct kw_metadata {
    kw_format format;
    int64_t   duration_ms;
    int32_t   bitrate_kbps;
    int32_t   sample_rate_hz;
    int32_t   channels;
} kw_metadata;

/* ------------------------------------------------------------------ */
/* Engine lifecycle                                                    */
/* ------------------------------------------------------------------ */

/* Initialise codec tables and global state. Idempotent. Must be called
 * before any other API function. */
KW_API kw_result kw_load_engine(void);

/* Release global state. Outstanding kw_pcm_buffer objects stay valid. */
KW_API void kw_unload_engine(void);

/* Human-readable library version, e.g. "0.1.0". Static storage. */
KW_API const char *kw_version(void);

/* Must equal KW_ABI_VERSION of the header the caller compiled against. */
KW_API int32_t kw_abi_version(void);

/* ------------------------------------------------------------------ */
/* Core operations                                                     */
/* ------------------------------------------------------------------ */

/* Identify the format of a file from headers/magic bytes. Never decodes. */
KW_API kw_result kw_detect_format(const char *input_path, kw_format *out_format);

/* Decode a whole file into a newly allocated PCM buffer.
 * On success *out_pcm is owned by the caller (release with kw_pcm_free). */
KW_API kw_result kw_decode(const char *input_path, kw_pcm_buffer **out_pcm);

/* Encode a PCM buffer to a file.
 * bitrate_kbps: target bitrate; 0 selects the codec default.
 * sample_rate_hz: target rate; 0 keeps the buffer's rate. A differing
 * rate resamples internally. */
KW_API kw_result kw_encode(const kw_pcm_buffer *pcm,
                           const char *output_path,
                           kw_codec    codec,
                           int32_t     bitrate_kbps,
                           int32_t     sample_rate_hz);

/* decode + (resample) + encode in one call. Parameter semantics match
 * kw_encode. */
KW_API kw_result kw_convert(const char *input_path,
                            const char *output_path,
                            kw_codec    codec,
                            int32_t     bitrate_kbps,
                            int32_t     sample_rate_hz);

/* Read duration/bitrate/rate/channels from headers without a full decode.
 * Unavailable fields are KW_META_UNKNOWN. */
KW_API kw_result kw_get_metadata(const char *input_path, kw_metadata *out_meta);

/* Sample-rate conversion. Same rate returns a plain copy. */
KW_API kw_result kw_resample(const kw_pcm_buffer *in,
                             int32_t              target_rate_hz,
                             kw_pcm_buffer      **out_pcm);

/* ------------------------------------------------------------------ */
/* PCM buffer lifecycle                                                */
/* ------------------------------------------------------------------ */

/* Allocate a zero-filled buffer owned by the caller. */
KW_API kw_result kw_pcm_alloc(kw_sample_format  sample_format,
                              int32_t           sample_rate_hz,
                              int32_t           channels,
                              int64_t           frame_count,
                              kw_pcm_buffer   **out_pcm);

/* Release a buffer created by the core. NULL is a no-op. */
KW_API void kw_pcm_free(kw_pcm_buffer *pcm);

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */

/* Static English description of a result code. Never NULL. */
KW_API const char *kw_error_message(kw_result code);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* KUANTORWAVE_H */
