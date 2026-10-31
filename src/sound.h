/* Sound data (docs/formats/media.md): PCM WAV effects from the archives and the MS ADPCM music files.
   The original plays effects through DirectSound buffers and converts the music with the ACM; the port
   decodes both itself and mixes (step 7). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int channels, rate, bits;
    const uint8_t *data;
    uint32_t size;      /* bytes */
} SndInfo;

/* The header reads of Snd_Load 0x4a1f50: "WAVE" at +8, then fixed offsets of the canonical 44-byte
   header (channels +0x16, rate +0x18, bits +0x22, data size +0x28, data at +0x2c); the chunks are not
   walked. size bounds the data (the original trusts the header). */
bool Snd_Header(const uint8_t *file, size_t size, SndInfo *out);

typedef struct {
    int channels, rate, block_align, samples_per_block;
    int ncoef;
    int16_t coef[32][2];
    uint64_t data_off, data_size;   /* the data chunk, in the file */
    uint32_t frames;                /* from the fact chunk (sample frames), 0 if none */
} AdpInfo;

/* RIFF WAVE with format tag 2 (Microsoft ADPCM), chunks walked as the mmio reader of Music_Open
   0x4a28d0 does (FUN_004bd520). head is the start of the file (a few hundred bytes are enough). */
bool Adp_Header(const uint8_t *head, size_t n, uint64_t file_size, AdpInfo *out);
/* One block into interleaved 16-bit samples (out has room for samples_per_block * channels); returns
   the sample frames decoded (a short last block gives fewer). */
int Adpcm_DecodeBlock(const AdpInfo *a, const uint8_t *block, size_t n, int16_t *out);
