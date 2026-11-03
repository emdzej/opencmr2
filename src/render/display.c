/* The display adapter: the modes and the device on one of them (0x4a78a0, 0x4a8da0-0x4a8f60).
   The original asks DirectDraw for the device's modes and keeps those its callback 0x4a8da0 accepts. The
   port's adapter offers the 4:3 and 5:4 modes of a card of the time in 16 and 32 bits and, the port's
   addition, today's 16:9 and 16:10 ones (the game drawn on a 4:3 screen in their middle, the race's
   view across the whole: D3d_Open, D3d_SetWide), and the same callback's rules choose among them. */
#include "render/display.h"
#include "game/mem.h"
#include "render/d3d7.h"
#include "render/dlight.h"
#include "render/rstate.h"

enum { MODES = 0x6631f8, NMODES = 0x663b28, CURRENT = 0x663b2c };

/* the adapter's modes, in the order a driver of the time lists them: by size, then by depth */
static const int sizes[][2] = {{320, 240}, {512, 384}, {640, 480}, {800, 600}, {1024, 768}, {1152, 864},
                               {1280, 720}, {1280, 800}, {1280, 960}, {1280, 1024}, {1366, 768}, {1440, 900},
                               {1600, 900}, {1600, 1200}, {1680, 1050}, {1920, 1080}, {1920, 1200},
                               {1920, 1440}, {2048, 1536}, {2560, 1440}};
static const int depths[] = {8, 16, 32};
/* the video memory the callback measures the modes against (DDSCAPS_VIDEOMEMORY less the textures'
   share, 0x4bdd00): a 64 MB card's */
enum { VIDEO_MEMORY = 64 << 20 };

/* 0x4a8da0: a mode kept when at least 640 x 480, 16 or 32 bits (32 only on a hardware device, which the
   port's is; any depth windowed, which the port's display is) and three buffers of it fit the memory */
static void enumerate(void)
{
    gw32(NMODES, 0);
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++)
        for (unsigned j = 0; j < sizeof depths / sizeof *depths; j++) {
            int w = sizes[i][0], h = sizes[i][1], b = depths[j];
            if (w < 0x280 || h < 0x1e0 || (b != 16 && b != 32)) continue;
            if ((unsigned)(b / 8 * h * w * 3) >= (unsigned)VIDEO_MEMORY) continue;
            uint32_t n = gm32(NMODES), a = MODES + 12 * n;
            gw32(a, (uint32_t)w), gw32(a + 4, (uint32_t)h), gw32(a + 8, (uint32_t)b);
            gw32(NMODES, n + 1);
        }
}

int Disp_Modes(void) { return (int)gm32(NMODES); }
int Disp_CurrentMode(void) { return (int)gm32(CURRENT); }

void Disp_Mode(int i, int *width, int *height, int *bpp)
{
    uint32_t a = MODES + 12 * (uint32_t)i;
    *width = (int)gm32(a), *height = (int)gm32(a + 4), *bpp = (int)gm32(a + 8);
}

bool Disp_HasMode(int width, int height, int bpp)
{
    for (int i = 0; i < Disp_Modes(); i++) {
        int w, h, b;
        Disp_Mode(i, &w, &h, &b);
        if (w == width && h == height && b == bpp) return true;
    }
    return false;
}

/* 0x4a8ec0: the current mode the one matching (the last of them; unchanged when none does) */
static void select_mode(int width, int height, int bpp)
{
    for (int i = 0; i < Disp_Modes(); i++) {
        int w, h, b;
        Disp_Mode(i, &w, &h, &b);
        if (w == width && h == height && b == bpp) gw32(CURRENT, (uint32_t)i);
    }
}

bool Disp_Open(int width, int height, int bpp)
{
    enumerate();
    /* not listed: 640 x 480 at the desktop's depth (the port's desktop: 32 bits) */
    if (!Disp_HasMode(width, height, bpp)) width = 0x280, height = 0x1e0, bpp = 32;
    select_mode(width, height, bpp);
    if (!D3d_Open(width, height, bpp)) return false;
    Render_InitStates();
    Dlight_Restore();   /* 0x4b2e50 */
    return true;
}

/* the port's textures go to 2,048 and more (GLES 3 has at least 2,048) */
bool Caps_TexWidth(int n) { return n <= 0x800; }
bool Caps_TexHeight(int n) { return n <= 0x800; }

/* (the port: on a wide mode the width of its 4:3 game screen, what the menus are laid out on) */
bool Disp_HiRes(void) { return D3d_State()->width > 0x3ff && Caps_TexWidth(0x400) && Caps_TexHeight(0x400); }
