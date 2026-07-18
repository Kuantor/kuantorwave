/*
 * kw_detect.c — format detection from headers/magic bytes (issue #6).
 *
 * Detection is stateless and reads at most KW_DETECT_SCAN_BYTES from the
 * file; it never decodes audio. It therefore works without kw_load_engine(),
 * though callers should still follow the documented lifecycle.
 */

#include "kuantorwave.h"

#include <stdio.h>
#include <string.h>

/* Enough to hold an ID3v2 header plus the first audio frame in typical
 * files; tags larger than this are trusted on the strength of the ID3
 * signature alone. */
#define KW_DETECT_SCAN_BYTES 16384

/* True if b points at a plausible MPEG-1/2 Layer III frame header. */
static int kw_is_mp3_frame_sync(const uint8_t *b)
{
    if (b[0] != 0xFF || (b[1] & 0xE0) != 0xE0)
        return 0;
    if (((b[1] >> 3) & 0x03) == 0x01) /* reserved MPEG version */
        return 0;
    if (((b[1] >> 1) & 0x03) != 0x01) /* not Layer III */
        return 0;
    if ((b[2] >> 4) == 0x0F)          /* invalid bitrate index */
        return 0;
    if (((b[2] >> 2) & 0x03) == 0x03) /* reserved sample rate */
        return 0;
    return 1;
}

/* True if b points at a well-formed ID3v2 tag header (10 bytes). */
static int kw_is_id3v2_header(const uint8_t *b)
{
    if (memcmp(b, "ID3", 3) != 0)
        return 0;
    if (b[3] == 0xFF || b[4] == 0xFF) /* version bytes may not be 0xFF */
        return 0;
    /* size is syncsafe: high bit of each byte must be clear */
    if ((b[6] | b[7] | b[8] | b[9]) & 0x80)
        return 0;
    return 1;
}

static uint32_t kw_id3v2_tag_size(const uint8_t *b)
{
    return ((uint32_t)b[6] << 21) | ((uint32_t)b[7] << 14) |
           ((uint32_t)b[8] << 7)  |  (uint32_t)b[9];
}

KW_API kw_result kw_detect_format(const char *input_path, kw_format *out_format)
{
    uint8_t buf[KW_DETECT_SCAN_BYTES]; /* 16 KiB stack use is fine on our targets */
    size_t n;
    FILE *f;

    if (input_path == NULL || out_format == NULL)
        return KW_ERR_INVALID_ARG;
    *out_format = KW_FORMAT_UNKNOWN;

    /* TODO(#5 follow-up): route through a UTF-8 -> UTF-16 helper on Windows
     * once the engine module lands; plain fopen limits paths to the ANSI
     * code page there. */
    f = fopen(input_path, "rb");
    if (f == NULL)
        return KW_ERR_IO;
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);

    /* WAV: "RIFF" .... "WAVE" */
    if (n >= 12 && memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WAVE", 4) == 0) {
        *out_format = KW_FORMAT_WAV;
        return KW_OK;
    }

    /* MP4/M4A: any file whose first box is "ftyp" */
    if (n >= 12 && memcmp(buf + 4, "ftyp", 4) == 0) {
        *out_format = KW_FORMAT_M4A_AAC;
        return KW_OK;
    }

    /* MP3 with a leading ID3v2 tag */
    if (n >= 10 && kw_is_id3v2_header(buf)) {
        size_t audio_off = 10 + (size_t)kw_id3v2_tag_size(buf);
        if (audio_off + 4 <= n) {
            /* Frame expected inside our window: allow padding by scanning
             * forward a little for the sync word. */
            size_t i;
            size_t limit = (n >= 4) ? n - 4 : 0;
            for (i = audio_off; i <= limit; i++) {
                if (kw_is_mp3_frame_sync(buf + i)) {
                    *out_format = KW_FORMAT_MP3;
                    return KW_OK;
                }
            }
            return KW_ERR_UNKNOWN_FORMAT;
        }
        /* Tag extends past the scan window: trust the ID3 signature. */
        *out_format = KW_FORMAT_MP3;
        return KW_OK;
    }

    /* Bare MP3: frame sync at offset 0 */
    if (n >= 4 && kw_is_mp3_frame_sync(buf)) {
        *out_format = KW_FORMAT_MP3;
        return KW_OK;
    }

    return KW_ERR_UNKNOWN_FORMAT;
}
