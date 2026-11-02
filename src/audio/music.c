/* The music (see music.h). The ACM's decompression is the port's MS ADPCM decoder (sound.c). */
#include "audio/music.h"
#include "audio/dsound.h"
#include "sound.h"
#include "vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SEG = 0xfe80, NSEG = 8, BUF = SEG * NSEG };   /* DAT_005a271c */

static Dsb *buf;              /* DAT_005a2854 */
static VfsFile *file;
static AdpInfo info;
static uint64_t block;        /* the next block to decode */
static int16_t *pcm;          /* a decoded block */
static int pcm_frames, pcm_at;
static int write_seg;         /* DAT_005a2714 */
static bool opened;           /* DAT_005a2730 */
static bool paused;           /* DAT_005a2724 */
static bool stopped;          /* DAT_005a2720: stopped by the pause, to play again */
static char name[260];        /* DAT_005a2738 */

/* 0x4a2ac0 */
static void close_all(void)
{
    if (buf) Dsb_Stop(buf), Dsb_Release(buf);
    buf = NULL;
    if (file) vfs_close(file);
    file = NULL;
    free(pcm);
    pcm = NULL;
    opened = false;
    stopped = false;
}

void Music_Open(const char *path)
{
    close_all();
    char p[260];
    snprintf(p, sizeof p, "%s", path);
    for (char *c = p; *c; c++)
        if (*c == '\\') *c = '/';
    file = vfs_open(p);
    if (!file) return;
    uint8_t head[512];
    int64_t n = vfs_read_at(file, 0, head, sizeof head);
    if (n <= 0 || !Adp_Header(head, (size_t)n, vfs_file_size(file), &info) || info.channels != 2) {
        close_all();
        return;
    }
    pcm = malloc((size_t)info.samples_per_block * 2 * sizeof *pcm);
    buf = Dsb_Create(44100, 16, 2, BUF);   /* 0x4a2a20 */
    if (!pcm || !buf) {
        close_all();
        return;
    }
    block = 0, pcm_frames = pcm_at = 0;
    opened = true;
    snprintf(name, sizeof name, "%s", path);
}

void Music_SetVolume(int v)
{
    if (!opened) return;
    Dsb_SetVolume(buf, v == 0 ? -10000 : (v * 5 - 500) * 20 / 4);
}

/* the ACM's output: the next frames of the file, from its start again at its end */
static void fill(uint8_t *dst, uint32_t bytes)
{
    uint8_t *raw = malloc((size_t)info.block_align);
    while (bytes && raw) {
        if (pcm_at >= pcm_frames) {
            uint64_t off = info.data_off + block * (uint64_t)info.block_align;
            if (off >= info.data_off + info.data_size) block = 0, off = info.data_off;
            uint64_t left = info.data_off + info.data_size - off;
            size_t want = left < (uint64_t)info.block_align ? (size_t)left : (size_t)info.block_align;
            int64_t got = vfs_read_at(file, off, raw, want);
            block++;
            pcm_frames = got > 0 ? Adpcm_DecodeBlock(&info, raw, (size_t)got, pcm) : 0;
            pcm_at = 0;
            if (pcm_frames <= 0) {
                memset(dst, 0, bytes);
                break;
            }
        }
        uint32_t n = (uint32_t)(pcm_frames - pcm_at) * 4;
        if (n > bytes) n = bytes & ~3u;
        if (!n) break;
        memcpy(dst, pcm + pcm_at * 2, n);
        pcm_at += (int)(n / 4);
        dst += n, bytes -= n;
    }
    free(raw);
}

/* 0x4a2d90: segments into the buffer from the write segment on */
static void fill_segments(int count)
{
    static uint8_t seg[SEG];
    for (int i = 0; i < count; i++) {
        fill(seg, SEG);
        Dsb_Write(buf, (uint32_t)write_seg * SEG, seg, SEG);
        if (++write_seg == NSEG) write_seg = 0;
    }
}

void Music_Update(void)
{
    if (!opened) return;
    write_seg = 0;
    Dsb_SetPosition(buf, 0);   /* 0x4a2c70: the whole buffer from its start */
    fill_segments(NSEG);
    Dsb_Play(buf, true);
}

/* 0x4a3160 with 0x4a31a0: paused, the buffer stopped when it plays */
void Music_Pause(void)
{
    if (!opened) return;
    paused = true;
    if (buf && Dsb_Playing(buf)) Dsb_Stop(buf), stopped = true;
}

void Music_Resume(void) { if (opened) paused = false; }   /* 0x4a3180 */

/* 0x4a2fe0: unless paused, the buffer played again after a pause, else (0x4a3050 with 0x4a2d30)
   the segments between the write segment and the one playing */
void Music_Poll(void)
{
    if (paused || !opened) return;
    if (stopped) {
        Dsb_Play(buf, true);
        stopped = false;
        return;
    }
    if (!Dsb_Playing(buf)) return;
    int cur = (int)(Dsb_Position(buf) / SEG), n = cur - write_seg;
    int count = n > 0 ? n - 1 : n + 7;
    if (count > 0) fill_segments(count);
}

void Music_Stop(bool keep_name)
{
    close_all();
    if (!keep_name) name[0] = 0;
}
