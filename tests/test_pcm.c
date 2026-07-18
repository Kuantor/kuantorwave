/* Unit tests for the PCM buffer subsystem (issue #7).
 *
 * Exercises allocation, property accessors, data ownership, resize, and
 * argument validation. The repeated allocate/free loop plus the resize
 * churn give AddressSanitizer/LeakSanitizer (the CI sanitizer job) a chance
 * to prove there are no leaks or overflows. Exits non-zero on any failure.
 */

#include "kuantorwave.h"
#include "kw_pcm_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(what, cond)                                                  \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            fprintf(stderr, "FAIL %s (line %d)\n", (what), __LINE__);      \
        }                                                                  \
    } while (0)

#define CHECK_EQ(what, got, want)                                          \
    do {                                                                   \
        g_checks++;                                                        \
        if ((long long)(got) != (long long)(want)) {                       \
            g_failures++;                                                  \
            fprintf(stderr, "FAIL %s: got %lld, want %lld (line %d)\n",     \
                    (what), (long long)(got), (long long)(want), __LINE__); \
        }                                                                  \
    } while (0)

static void test_bytes_per_sample(void)
{
    CHECK_EQ("bps s16", kw_pcm_bytes_per_sample(KW_SAMPLE_S16), 2);
    CHECK_EQ("bps f32", kw_pcm_bytes_per_sample(KW_SAMPLE_F32), 4);
    CHECK_EQ("bps bad", kw_pcm_bytes_per_sample((kw_sample_format)0), 0);
    CHECK_EQ("bps bad2", kw_pcm_bytes_per_sample((kw_sample_format)99), 0);
}

static void test_alloc_basic(void)
{
    kw_pcm_buffer *pcm = NULL;
    const int16_t *samples;
    int64_t i, n;

    CHECK_EQ("alloc ok", kw_pcm_alloc(KW_SAMPLE_S16, 44100, 2, 1000, &pcm), KW_OK);
    CHECK("alloc non-null", pcm != NULL);
    if (pcm == NULL)
        return;

    CHECK_EQ("format", pcm->sample_format, KW_SAMPLE_S16);
    CHECK_EQ("rate", pcm->sample_rate_hz, 44100);
    CHECK_EQ("channels", pcm->channels, 2);
    CHECK_EQ("frames", pcm->frame_count, 1000);
    CHECK("data non-null", pcm->data != NULL);

    CHECK_EQ("sample_count", kw_pcm_sample_count(pcm), 2000);
    CHECK_EQ("data_size", kw_pcm_data_size(pcm), 2000 * 2);

    /* freshly allocated storage must be zero-filled */
    samples = (const int16_t *)pcm->data;
    n = kw_pcm_sample_count(pcm);
    for (i = 0; i < n; i++) {
        if (samples[i] != 0) {
            CHECK("zero-filled", 0);
            break;
        }
    }
    kw_pcm_free(pcm);
}

static void test_ownership(void)
{
    /* The caller owns the storage and may read back what it writes. */
    kw_pcm_buffer *pcm = NULL;
    float *s;
    int64_t i, n;
    int ok = 1;

    CHECK_EQ("alloc f32", kw_pcm_alloc(KW_SAMPLE_F32, 48000, 1, 256, &pcm), KW_OK);
    if (pcm == NULL)
        return;
    s = (float *)pcm->data;
    n = kw_pcm_sample_count(pcm);
    for (i = 0; i < n; i++)
        s[i] = (float)i * 0.5f;
    for (i = 0; i < n; i++) {
        if (s[i] != (float)i * 0.5f) { ok = 0; break; }
    }
    CHECK("readback", ok);
    kw_pcm_free(pcm);
}

static void test_zero_frames(void)
{
    /* A zero-length buffer is valid; its data pointer is NULL. */
    kw_pcm_buffer *pcm = NULL;
    CHECK_EQ("alloc zero", kw_pcm_alloc(KW_SAMPLE_S16, 44100, 2, 0, &pcm), KW_OK);
    if (pcm == NULL)
        return;
    CHECK("zero data null", pcm->data == NULL);
    CHECK_EQ("zero size", kw_pcm_data_size(pcm), 0);
    kw_pcm_free(pcm);
}

static void test_invalid_args(void)
{
    kw_pcm_buffer  sentinel;              /* a valid non-NULL address to poison with */
    kw_pcm_buffer *pcm = &sentinel;       /* out-param must be reset to NULL on error */

    CHECK_EQ("null out", kw_pcm_alloc(KW_SAMPLE_S16, 44100, 2, 10, NULL), KW_ERR_INVALID_ARG);

    CHECK_EQ("bad format", kw_pcm_alloc((kw_sample_format)0, 44100, 2, 10, &pcm), KW_ERR_INVALID_ARG);
    CHECK("out reset 1", pcm == NULL);

    pcm = &sentinel;
    CHECK_EQ("bad rate", kw_pcm_alloc(KW_SAMPLE_S16, 0, 2, 10, &pcm), KW_ERR_INVALID_ARG);
    CHECK("out reset 2", pcm == NULL);

    pcm = &sentinel;
    CHECK_EQ("bad channels", kw_pcm_alloc(KW_SAMPLE_S16, 44100, 0, 10, &pcm), KW_ERR_INVALID_ARG);
    CHECK("out reset 3", pcm == NULL);

    pcm = &sentinel;
    CHECK_EQ("neg frames", kw_pcm_alloc(KW_SAMPLE_S16, 44100, 2, -1, &pcm), KW_ERR_INVALID_ARG);
    CHECK("out reset 4", pcm == NULL);
}

static void test_overflow_rejected(void)
{
    /* frame_count * channels * bps must not wrap; expect a clean NOMEM,
     * not a small allocation that later overruns. */
    kw_pcm_buffer  sentinel;
    kw_pcm_buffer *pcm = &sentinel;
    kw_result r = kw_pcm_alloc(KW_SAMPLE_F32, 44100, 2, INT64_MAX, &pcm);
    CHECK("overflow rejected", r == KW_ERR_NOMEM || r == KW_ERR_INVALID_ARG);
    CHECK("overflow out null", pcm == NULL);
}

static void test_resize(void)
{
    kw_pcm_buffer *pcm = NULL;
    int16_t *s;
    int64_t i;
    int preserved = 1, zeroed = 1;

    CHECK_EQ("alloc for resize", kw_pcm_alloc(KW_SAMPLE_S16, 44100, 2, 100, &pcm), KW_OK);
    if (pcm == NULL)
        return;

    s = (int16_t *)pcm->data;
    for (i = 0; i < kw_pcm_sample_count(pcm); i++)
        s[i] = (int16_t)(i + 1);

    /* grow: existing samples preserved, new tail zero-filled */
    CHECK_EQ("resize grow", kw_pcm_resize(pcm, 300), KW_OK);
    CHECK_EQ("grow frames", pcm->frame_count, 300);
    CHECK_EQ("grow size", kw_pcm_data_size(pcm), 300 * 2 * 2);
    s = (int16_t *)pcm->data;
    for (i = 0; i < 200; i++)
        if (s[i] != (int16_t)(i + 1)) { preserved = 0; break; }
    CHECK("grow preserves", preserved);
    for (i = 200; i < 600; i++)
        if (s[i] != 0) { zeroed = 0; break; }
    CHECK("grow zero-fills", zeroed);

    /* shrink: first samples still intact */
    CHECK_EQ("resize shrink", kw_pcm_resize(pcm, 50), KW_OK);
    CHECK_EQ("shrink frames", pcm->frame_count, 50);
    s = (int16_t *)pcm->data;
    preserved = 1;
    for (i = 0; i < 100; i++)
        if (s[i] != (int16_t)(i + 1)) { preserved = 0; break; }
    CHECK("shrink preserves", preserved);

    /* resize to zero frees storage */
    CHECK_EQ("resize zero", kw_pcm_resize(pcm, 0), KW_OK);
    CHECK("resize zero data null", pcm->data == NULL);
    CHECK_EQ("resize zero frames", pcm->frame_count, 0);

    /* grow from empty */
    CHECK_EQ("regrow", kw_pcm_resize(pcm, 10), KW_OK);
    CHECK("regrow data", pcm->data != NULL);

    kw_pcm_free(pcm);

    CHECK_EQ("resize null", kw_pcm_resize(NULL, 10), KW_ERR_INVALID_ARG);
}

static void test_free_null(void)
{
    kw_pcm_free(NULL); /* must be a no-op, not a crash */
    CHECK("free null ok", 1);
}

static void test_alloc_free_churn(void)
{
    /* Repeated allocate / resize / free — a leak here shows up under
     * LeakSanitizer in the CI sanitizer job. */
    int iter;
    for (iter = 0; iter < 2000; iter++) {
        kw_pcm_buffer *pcm = NULL;
        if (kw_pcm_alloc(KW_SAMPLE_S16, 44100, 2, 64, &pcm) != KW_OK || pcm == NULL) {
            CHECK("churn alloc", 0);
            return;
        }
        (void)kw_pcm_resize(pcm, 128);
        (void)kw_pcm_resize(pcm, 32);
        kw_pcm_free(pcm);
    }
    CHECK("churn done", 1);
}

int main(void)
{
    test_bytes_per_sample();
    test_alloc_basic();
    test_ownership();
    test_zero_frames();
    test_invalid_args();
    test_overflow_rejected();
    test_resize();
    test_free_null();
    test_alloc_free_churn();

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
