/* The original's C runtime parts (see crt.h). */
#include "crt.h"

static uint32_t holdrand = 1;   /* the runtime's initial state */

void crt_srand(uint32_t seed) { holdrand = seed; }
uint32_t crt_seed(void) { return holdrand; }

int crt_rand(void)
{
    holdrand = holdrand * 214013u + 2531011u;
    return (int)(holdrand >> 16 & 0x7fff);
}

/* qsort (msvcrt.dll's, imported at 0x4c667c): a quicksort on the middle element with a stack of
   ranges, and ranges of up to eight elements sorted by repeatedly moving the greatest (the first of
   equals found) to the end. Equal elements end in its order, which the game's sorts of tied records
   show. */
static void swap_elems(uint8_t *a, uint8_t *b, size_t w)
{
    if (a == b) return;
    while (w--) {
        uint8_t t = *a;
        *a++ = *b, *b++ = t;
    }
}

static void short_sort(uint8_t *lo, uint8_t *hi, size_t w, int (*cmp)(const void *, const void *))
{
    while (hi > lo) {
        uint8_t *max = lo;
        for (uint8_t *p = lo + w; p <= hi; p += w)
            if (cmp(p, max) > 0) max = p;
        swap_elems(max, hi, w);
        hi -= w;
    }
}

void crt_qsort(void *base, size_t n, size_t w, int (*cmp)(const void *, const void *))
{
    if (n < 2 || !w) return;
    uint8_t *lostk[30], *histk[30];
    int sp = 0;
    uint8_t *lo = base, *hi = (uint8_t *)base + w * (n - 1);
    for (;;) {
        size_t size = (size_t)(hi - lo) / w + 1;
        if (size <= 8) short_sort(lo, hi, w, cmp);
        else {
            swap_elems(lo + size / 2 * w, lo, w);
            uint8_t *loguy = lo, *higuy = hi + w;
            for (;;) {
                do loguy += w; while (loguy <= hi && cmp(loguy, lo) <= 0);
                do higuy -= w; while (higuy > lo && cmp(higuy, lo) >= 0);
                if (higuy < loguy) break;
                swap_elems(loguy, higuy, w);
            }
            swap_elems(lo, higuy, w);
            /* the smaller side first, the larger kept for later */
            if (higuy - 1 - lo >= hi - loguy) {
                if (lo + w < higuy) lostk[sp] = lo, histk[sp] = higuy - w, sp++;
                if (loguy < hi) { lo = loguy; continue; }
            } else {
                if (loguy < hi) lostk[sp] = loguy, histk[sp] = hi, sp++;
                if (lo + w < higuy) { hi = higuy - w; continue; }
            }
        }
        if (--sp < 0) return;
        lo = lostk[sp], hi = histk[sp];
    }
}
