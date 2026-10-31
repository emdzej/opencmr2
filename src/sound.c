/* Sound data (see sound.h). */
#include "sound.h"
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

bool Snd_Header(const uint8_t *f, size_t size, SndInfo *out)
{
    if (!f || size < 0x2c || memcmp(f + 8, "WAVE", 4)) return false;
    out->channels = rd16(f + 0x16);
    out->rate = (int)rd32(f + 0x18);
    out->bits = rd16(f + 0x22);
    out->size = rd32(f + 0x28);
    if (out->size > size - 0x2c) out->size = (uint32_t)(size - 0x2c);
    out->data = f + 0x2c;
    return true;
}

bool Adp_Header(const uint8_t *h, size_t n, uint64_t file_size, AdpInfo *a)
{
    memset(a, 0, sizeof *a);
    if (n < 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
    bool fmt = false;
    for (size_t p = 12; p + 8 <= n;) {
        uint32_t len = rd32(h + p + 4);
        const uint8_t *c = h + p + 8;
        if (!memcmp(h + p, "fmt ", 4)) {
            if (len < 20 || p + 8 + len > n || rd16(c) != 2) return false;
            a->channels = rd16(c + 2);
            a->rate = (int)rd32(c + 4);
            a->block_align = rd16(c + 12);
            a->samples_per_block = rd16(c + 18);
            a->ncoef = len >= 22 ? rd16(c + 20) : 0;
            if (a->ncoef > 32 || 22u + 4u * a->ncoef > len) return false;
            for (int i = 0; i < a->ncoef; i++) {
                a->coef[i][0] = (int16_t)rd16(c + 22 + 4 * i);
                a->coef[i][1] = (int16_t)rd16(c + 24 + 4 * i);
            }
            fmt = true;
        } else if (!memcmp(h + p, "fact", 4) && len >= 4 && p + 12 <= n) {
            a->frames = rd32(c);
        } else if (!memcmp(h + p, "data", 4)) {
            a->data_off = p + 8;
            a->data_size = len;
            if (a->data_off + a->data_size > file_size) a->data_size = file_size - a->data_off;
            break;
        }
        p += 8 + len + (len & 1);
    }
    return fmt && a->data_off && (a->channels == 1 || a->channels == 2) && a->block_align > 7 * a->channels &&
           a->samples_per_block >= 2 && a->ncoef > 0;
}

/* Microsoft ADPCM, as the ACM codec decodes it: per channel a predictor index, the step (delta) and two
   history samples, the older first in the output; then 4-bit codes, high nibble first, channels
   alternating. The adaptation table is the codec's published one. The prediction divides by 256
   (truncating, as Wine's msadp32 does); whether Msadp32.acm shifts instead (a 1-LSB difference on
   negative values) is not checked. */
int Adpcm_DecodeBlock(const AdpInfo *a, const uint8_t *b, size_t n, int16_t *out)
{
    static const int adapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
    int ch = a->channels;
    if (n < (size_t)7 * ch) return 0;
    int c1[2], c2[2], delta[2], s1[2], s2[2];
    for (int c = 0; c < ch; c++) {
        int pi = b[c] < a->ncoef ? b[c] : 0;
        c1[c] = a->coef[pi][0];
        c2[c] = a->coef[pi][1];
        delta[c] = (int16_t)rd16(b + ch + 2 * c);
        s1[c] = (int16_t)rd16(b + 3 * ch + 2 * c);
        s2[c] = (int16_t)rd16(b + 5 * ch + 2 * c);
    }
    for (int c = 0; c < ch; c++) {
        out[c] = (int16_t)s2[c];
        out[ch + c] = (int16_t)s1[c];
    }
    int frames = 2, max = a->samples_per_block;
    size_t p = (size_t)7 * ch;
    int16_t *o = out + 2 * ch;
    int k = 0;
    while (frames < max && p < n) {
        for (int half = 0; half < 2 && frames < max; half++) {
            int c = k % ch;
            int code = half ? b[p] & 15 : b[p] >> 4;
            int pred = (s1[c] * c1[c] + s2[c] * c2[c]) / 256 + (code >= 8 ? code - 16 : code) * delta[c];
            if (pred > 32767) pred = 32767;
            if (pred < -32768) pred = -32768;
            s2[c] = s1[c];
            s1[c] = pred;
            *o++ = (int16_t)pred;
            delta[c] = adapt[code] * delta[c] / 256;
            if (delta[c] < 16) delta[c] = 16;
            if (++k % ch == 0) frames++;
        }
        p++;
    }
    return frames > max ? max : frames;
}
