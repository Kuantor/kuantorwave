/* Unit tests for kw_detect_format (issue #6).
 *
 * Fixtures are generated on the fly so no binary files live in the repo.
 * Run via ctest; exits non-zero on any failure.
 */

#include "kuantorwave.h"

#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK_EQ(what, got, want)                                          \
    do {                                                                   \
        g_checks++;                                                        \
        if ((int)(got) != (int)(want)) {                                   \
            g_failures++;                                                  \
            fprintf(stderr, "FAIL %s: got %d, want %d (line %d)\n",        \
                    (what), (int)(got), (int)(want), __LINE__);            \
        }                                                                  \
    } while (0)

static int write_fixture(const char *path, const unsigned char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* Build a minimal fixture in buf and return its length. */

static size_t make_wav(unsigned char *buf)
{
    memcpy(buf, "RIFF", 4);
    buf[4] = 36; buf[5] = 0; buf[6] = 0; buf[7] = 0;
    memcpy(buf + 8, "WAVE", 4);
    memcpy(buf + 12, "fmt ", 4);
    memset(buf + 16, 0, 20);
    return 36;
}

static size_t make_m4a(unsigned char *buf)
{
    memset(buf, 0, 32);
    buf[3] = 32; /* box size 32, big-endian */
    memcpy(buf + 4, "ftyp", 4);
    memcpy(buf + 8, "M4A ", 4);
    return 32;
}

/* 0xFF 0xFB: MPEG-1 Layer III; 0x90: bitrate idx 9, 44.1 kHz. */
static size_t make_bare_mp3(unsigned char *buf)
{
    buf[0] = 0xFF; buf[1] = 0xFB; buf[2] = 0x90; buf[3] = 0x64;
    memset(buf + 4, 0, 60);
    return 64;
}

/* ID3v2.4 header with a small tag, padding, then a valid frame sync. */
static size_t make_id3_mp3(unsigned char *buf)
{
    size_t tag_size = 32;
    memcpy(buf, "ID3", 3);
    buf[3] = 4; buf[4] = 0; buf[5] = 0;              /* v2.4, no flags   */
    buf[6] = 0; buf[7] = 0; buf[8] = 0; buf[9] = 32; /* syncsafe size 32 */
    memset(buf + 10, 0, tag_size);                   /* empty tag body   */
    buf[10 + tag_size + 0] = 0xFF;
    buf[10 + tag_size + 1] = 0xFB;
    buf[10 + tag_size + 2] = 0x90;
    buf[10 + tag_size + 3] = 0x64;
    return 10 + tag_size + 4;
}

/* ID3 header whose declared tag size is far larger than the scan window. */
static size_t make_id3_huge_tag(unsigned char *buf)
{
    memcpy(buf, "ID3", 3);
    buf[3] = 4; buf[4] = 0; buf[5] = 0;
    buf[6] = 0x01; buf[7] = 0; buf[8] = 0; buf[9] = 0; /* ~2 MiB syncsafe */
    memset(buf + 10, 0, 100);
    return 110;
}

static size_t make_garbage(unsigned char *buf)
{
    size_t i;
    for (i = 0; i < 256; i++)
        buf[i] = (unsigned char)(0x37 + i * 11); /* arbitrary, no magic */
    return 256;
}

static void expect_format(const char *path, kw_format want)
{
    kw_format got = KW_FORMAT_UNKNOWN;
    CHECK_EQ(path, kw_detect_format(path, &got), KW_OK);
    CHECK_EQ(path, got, want);
}

static void expect_error(const char *path, kw_result want)
{
    kw_format got = KW_FORMAT_MP3; /* poison: must be reset to UNKNOWN */
    CHECK_EQ(path, kw_detect_format(path, &got), want);
    CHECK_EQ(path, got, KW_FORMAT_UNKNOWN);
}

int main(void)
{
    unsigned char buf[512];
    kw_format fmt;

    /* argument validation */
    CHECK_EQ("null path", kw_detect_format(NULL, &fmt), KW_ERR_INVALID_ARG);
    CHECK_EQ("null out", kw_detect_format("x.wav", NULL), KW_ERR_INVALID_ARG);

    /* missing file */
    CHECK_EQ("missing file",
             kw_detect_format("kw_no_such_file.bin", &fmt), KW_ERR_IO);

    /* happy paths */
    write_fixture("fx_wav.bin", buf, make_wav(buf));
    expect_format("fx_wav.bin", KW_FORMAT_WAV);

    write_fixture("fx_m4a.bin", buf, make_m4a(buf));
    expect_format("fx_m4a.bin", KW_FORMAT_M4A_AAC);

    write_fixture("fx_mp3.bin", buf, make_bare_mp3(buf));
    expect_format("fx_mp3.bin", KW_FORMAT_MP3);

    write_fixture("fx_id3.bin", buf, make_id3_mp3(buf));
    expect_format("fx_id3.bin", KW_FORMAT_MP3);

    write_fixture("fx_id3huge.bin", buf, make_id3_huge_tag(buf));
    expect_format("fx_id3huge.bin", KW_FORMAT_MP3);

    /* detection ignores the file extension */
    write_fixture("fx_lying.mp3", buf, make_wav(buf));
    expect_format("fx_lying.mp3", KW_FORMAT_WAV);

    /* unknown / degenerate inputs */
    write_fixture("fx_garbage.bin", buf, make_garbage(buf));
    expect_error("fx_garbage.bin", KW_ERR_UNKNOWN_FORMAT);

    write_fixture("fx_empty.bin", buf, 0);
    expect_error("fx_empty.bin", KW_ERR_UNKNOWN_FORMAT);

    write_fixture("fx_tiny.bin", (const unsigned char *)"RI", 2);
    expect_error("fx_tiny.bin", KW_ERR_UNKNOWN_FORMAT);

    /* truncated WAV: RIFF present but no WAVE marker */
    write_fixture("fx_riffonly.bin", (const unsigned char *)"RIFF\x10\x00\x00\x00JUNK", 12);
    expect_error("fx_riffonly.bin", KW_ERR_UNKNOWN_FORMAT);

    /* ID3 header followed by garbage instead of an audio frame */
    {
        size_t len = make_id3_mp3(buf);
        buf[10 + 32] = 0x00; /* destroy the frame sync */
        write_fixture("fx_id3_noframe.bin", buf, len);
        expect_error("fx_id3_noframe.bin", KW_ERR_UNKNOWN_FORMAT);
    }

    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
