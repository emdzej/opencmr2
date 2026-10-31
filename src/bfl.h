/* The game's file loading and its archives (docs/formats/bfl.md): File_Load reads a file through the gzip
   and CMPR layers, an archive slot holds a loaded .bfl, Bfl_Find* look members up in its trailing
   directory. Paths are the game's ("Game\\BigFiles\\Common.bfl"), resolved by the file layer (vfs.h). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An archive slot (16 bytes in the original: data, size, loaded, unused). */
typedef struct {
    uint8_t *data;
    uint32_t size;
    uint32_t loaded;
} BflSlot;

/* File_Load 0x4aa220: the payload of a CMPR file, else the file as it is (malloc'd; free()). quiet is the
   original's second argument: 0 shows the "insert the CD" dialog on a missing file, which the port
   reduces to a log line. NULL if missing. The size goes to File_LastSize. */
uint8_t *File_Load(const char *path, int quiet);
uint32_t File_LastSize(void);   /* FUN_004aa590 */

bool Bfl_LoadInto(const char *path, BflSlot *slot);   /* 0x4a9c30 */
bool Bfl_Mount(BflSlot *slot, const char *path);      /* 0x4a9d70 */
void Bfl_Unmount(BflSlot *slot);

/* Bfl_Find 0x4a9c70: the member called name (compared lower-cased, whole name), or NULL. *size (may be
   NULL) is written for every entry visited, so after a miss it holds the last entry's size, as in the
   original. */
const uint8_t *Bfl_Find(const BflSlot *slot, const char *name, uint32_t *size);
/* Bfl_FindFile 0x4a9df0: Bfl_Find on Path_Basename(path); *found = 1 on a hit; *size = 0 on a miss.
   skip (the original's fifth argument) forces a miss. */
const uint8_t *Bfl_FindFile(const BflSlot *slot, const char *path, uint8_t *found, uint32_t *size, bool skip);
/* The member lookup half of Bfl_FindTexture 0x4a9e60: the file name of path without its extension, as
   <name>.dds, then <name>.tga. NULL if neither (the original then loads the texture from disk). */
const uint8_t *Bfl_FindTextureMember(const BflSlot *slot, const char *path, uint32_t *size);

/* Path_Basename 0x4a9da0 (dst at least 64 bytes, as the original's buffers). */
void Path_Basename(const char *path, char *dst, size_t cap);

/* Calls fn for each directory entry of an archive payload, in order; false if the directory is malformed
   (tests and tools; the game only looks names up). */
typedef void (*BflEntryFn)(const char *name, uint32_t off, uint32_t size, void *user);
bool Bfl_Walk(const uint8_t *data, uint32_t size, BflEntryFn fn, void *user);
