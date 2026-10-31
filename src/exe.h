/* The original game program, read at run time: tables the game needs (process tables, surface names,
   cheat codes, strings) are taken from CMR2.exe of the user's copy rather than copied into the source
   tree. On the CD it exists only inside the installer cabinet, which the file layer reads (vfs_cab.h).
   Only the supported build is accepted: version 1.11, 1,228,800 bytes, CRC-32 44c022f4. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EXE_PATH "CMR2.exe"
#define EXE_SIZE 1228800u
#define EXE_CRC32 0x44c022f4u

/* Loads and checks the exe through the file layer. False (err says why) if missing or another build. */
bool exe_init(char *err, size_t errcap);
bool exe_loaded(void);
void exe_free(void);
/* len bytes at virtual address va (image base 0x400000), or NULL if the exe isn't loaded or the range
   isn't initialised data of one section (bytes beyond a section's raw size are zero-fill: NULL). */
const uint8_t *exe_data(uint32_t va, size_t len);
/* Little-endian reads at va; 0 if unavailable. */
uint32_t exe_u32(uint32_t va);
uint16_t exe_u16(uint32_t va);
/* The NUL-terminated string at va, or "" if unavailable. */
const char *exe_str(uint32_t va);

uint32_t crc32(const void *data, size_t len);
