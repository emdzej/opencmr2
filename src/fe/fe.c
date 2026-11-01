/* The front end's frame, process and start-up (see fe.h), ported from 0x4d0780-0x4d2380, the start-up
   0x4d15e0 and the state functions of table 0x523bc8. */
#include "fe/fe.h"
#include "fe/fe_res.h"
#include "fe/pages.h"
#include "fe/fe_page.h"
#include "audio/snd.h"
#include "exe.h"
#include "font.h"
#include "game/input.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/proc.h"
#include "game/profile.h"
#include "game/race.h"
#include "game/timer.h"
#include "pending.h"
#include "render/d3d7.h"
#include "render/mesh.h"
#include "render/rstate.h"
#include "render/twod.h"
#include "render/view.h"
#include "text.h"
#include "world/frame.h"
#include "world/obj.h"
#include "world/relight.h"
#include <stdio.h>
#include <string.h>

static ProcGroup group;          /* DAT_00817da0 */
static ProcSlot slot;            /* DAT_00817d98 */
static bool started;             /* DAT_00817eb0 */
static uint32_t now;             /* DAT_00817fe4 */
static uint32_t since_menu;      /* DAT_00817ff4: the time on the menus (state 6) */
static Menu *page, *next_page;   /* DAT_00818abc, DAT_00818ac0 */
static int player;               /* DAT_00818ac8 */
static Obj scene, camera;        /* DAT_00817fc8, DAT_00817fc4 (0x4d0840) */
static Tex *title_tex, *bink_tex, *copyright[4];   /* DAT_00817fd0, DAT_00817ebc, DAT_00817fd4 */
static int ncopyright;
static bool first_run;           /* DAT_00523d68 */
static bool to_stage;            /* DAT_008180f9 (DAT_008180fa: to the race) */
static uint32_t pause_count = (uint32_t)-1;   /* DAT_00523d6c */
static uint32_t pause_start;     /* DAT_00817ff0 */
static BflSlot res, res_lang, common, texts[5];   /* 0x818250, 0x818260, 0x818240, 0x818200 */

BflSlot *Fe_Res(void) { return &res; }
BflSlot *Fe_ResLang(void) { return &res_lang; }
BflSlot *Fe_Common(void) { return &common; }
BflSlot *Fe_Text(int lang) { return &texts[lang]; }
uint32_t Fe_Now(void) { return now; }
void Fe_Stamp(void) { gw32(0x52af90, Time_Get()); }
uint32_t Fe_Stamped(void) { return gm32(0x52af90); }
Menu *Fe_Page(void) { return page; }
void Fe_SetNextPage(Menu *m) { next_page = m; }
void Fe_Event(uint32_t event, uint32_t prio) { Proc_Event(&group, 0, event, prio); }
int Fe_Player(void) { return player; }
void Game_SetMode(uint32_t m) { gw32(0x52ea4c, m); }

Tex *Fe_Texture(const BflSlot *arc, uint32_t fmt, const char *arg)
{
    char p[260];
    Path_Format(p, sizeof p, fmt, arg);
    return Bfl_FindTexture(arc, p, NULL, false, 0);
}

static void screen(int *w, int *h) { D3d_ScreenSize(w, h); }

/* 0x4d21e0: the 640 (or 1,024) resolution archives: FrontEnd\Res640.bfl, and the language's
   CountrySpecific\<region>\FERes640<E or P>.bfl */
static void mount_res(void)
{
    int w, h;
    screen(&w, &h);
    if (w > 0x3ff) PENDING_NOTE(0x4b7560, "0x4d21e0 (the 1,024 archives with the texture memory)");
    char p[260];
    Bfl_Unmount(&res);
    Path_Format(p, sizeof p, 0x523e24, Path_FrontEnd());   /* 0x4a9700: no compressed textures */
    Bfl_Mount(&res, p);
    Bfl_Unmount(&res_lang);
    uint32_t lang = gm32(0x52ea54);
    int c = lang <= 2 ? 'E' : 'P';
    Path_Format(p, sizeof p, 0x523dd0, Path_Country(), exe_str(exe_u32(0x516124 + 4 * lang)), c);
    Bfl_Mount(&res_lang, p);
}

/* 0x4d2380: FrontEnd\Common.bfl, the resolution archives, the text archives of the territory's
   languages (Europe: English, French, German, Spanish, Italian) */
static void mount_all(void)
{
    char p[260];
    Path_Format(p, sizeof p, 0x51a0f0, Path_FrontEnd());
    Bfl_Mount(&common, p);
    mount_res();
    const char *region = exe_str(exe_u32(0x516124 + 4 * gm32(0x52ea54)));
    static const uint32_t europe[5] = {0x523ec8, 0x523ea0, 0x523e78, 0x523e8c, 0x523e64};
    static const int europe_slot[5] = {0, 1, 4, 2, 3};   /* 0x818200, 0x81820c, 0x818230, 0x818218, 0x818224 */
    static const uint32_t america[3] = {0x523eb4, 0x523ea0, 0x523e8c};
    switch (gm32(0x52ea54)) {
    case 0:
        for (int i = 0; i < 5; i++) {
            Path_Format(p, sizeof p, europe[i], Path_Country(), region);
            Bfl_Mount(&texts[europe_slot[i]], p);
        }
        break;
    case 1:
        for (int i = 0; i < 3; i++) {
            Path_Format(p, sizeof p, america[i], Path_Country(), region);
            Bfl_Mount(&texts[i], p);
        }
        break;
    default:
        Path_Format(p, sizeof p, gm32(0x52ea54) == 2 ? 0x523ec8 : 0x523edc, Path_Country(), region);
        Bfl_Mount(&texts[0], p);
        break;
    }
}

/* 0x4d0840: the scene (an empty node) and the camera 20 back from it */
static void make_scene(void)
{
    memset(&scene, 0, sizeof scene);
    memset(&camera, 0, sizeof camera);
    scene.kind = camera.kind = 3;
    scene.mask = camera.mask = 0xff;
    Mat_Identity(&scene.local);
    Mat_Identity(&camera.local);
    camera.local.m[14] = -0x140000;
    scene.dirty = 1;
    Proj_Set(0x25645, 0x4326e, 0xfa0000, 0x10000);
}

/* Game_Init 0x4d08d0 (the front end's part): the process memory, the archives, the scene, the cull,
   the title, copyright and Bink textures. The display, the tables and the 2D queues are the app's. */
static bool game_init(bool first)
{
    if (first) {
        Sprite_Init();
        Tri_Init();
    }
    mount_all();
    int w, h;
    screen(&w, &h);
    ncopyright = 3;   /* 4 at 1,024 with the texture memory */
    make_scene();
    Render_SetDefaultCull(3);
    static const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    Light_SetLow(white, false);
    title_tex = Fe_Texture(&res, 0x523cb8, Path_Root());
    char p[260];
    for (int i = 0; i < ncopyright; i++) {
        Path_Format(p, sizeof p, 0x523c90, Path_Root(), i + 1);
        copyright[i] = Bfl_FindTexture(&res_lang, p, NULL, false, 0);
    }
    bink_tex = Fe_Texture(&res, 0x523c78, Path_Root());
    return true;
}

/* 0x4d0a50 */
static bool fe_init(bool first)
{
    if (!game_init(first)) return false;
    Font_SetLayer(1);
    Fe_LoadTextures();   /* 0x4e2e50 */
    return true;
}

/* a sprite of the whole texture, centred on its middle */
static void sprite(Tex *t, int16_t x, int16_t y, const uint8_t rgba[4])
{
    Rect16 src = {0, 0, t->width, t->height}, dst = {x, y, t->width, t->height};
    int32_t c[3] = {t->width / 2, t->height / 2, 0};
    Sprite_Add(&src, &dst, t, 1, 0, c, NULL, rgba, 8);
}

/* the end of each draw: the frame, the scene and the 2D queues (0x49de40, the flip, is the app's) */
static void draw_scene(void)
{
    int w, h;
    screen(&w, &h);
    int16_t view[4] = {0, 0, (int16_t)w, (int16_t)h};
    Obj_Frame(&scene, &camera);
    World_Render(&camera, view, 0, true);
}

static void begin(void)
{
    Proj_Set(0x25645, 0x4326e, 0xfa0000, 0x10000);
    Render_ClearTarget();
    Render_ClearZ(false);
}

/* 0x4d15e0: the start-up. The first time: the options, the input, the archives, textures, sounds,
   fonts, text and the menus, then the boot screens (event 1); back from a race: the menus again
   (event 0) or the rally's movie (6). */
static void st_startup(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    /* srand(time()) is the app's fixed seed; the Sku_Type of the registry is EUROPE: territory 0 */
    gw32(0x52ea54, 0);
    Fe_LanguageCount();   /* 0x4f4b40 */
    first_run = true;
    to_stage = false;
    bool first = gm8(0x516120) != 0;
    gw8(0x8180fc, first);
    if (first) {
        gw8(0x531768, 0);
        gw8(0x52ea59, 0);
        gw8(0x52ea58, 0);
        gw32(0x52afb4, gm32(0x52afb4) & ~0x80000u);
        Paths_Init();
        Options_Defaults();
        for (int i = 0; i < 0x16; i++) gw32(0x52e898 + 4 * (uint32_t)i, 0x03030303);   /* 0x406560 */
        PENDING_NOTE(0x406580, "the unlocks' new marks");
        /* 0x49eaf0(2): the input devices; GameInfo.rcf (0x4ea5e0), the leaderboards (0x40e3c0,
           0x40e3f0) and Controller.rcf (0x40bf70) are not saved by the port yet */
        bool loaded = false;
        gw32(0x52afb4, gm32(0x52afb4) & ~0x80000u);
        Input_SetMenuMode(true);
        gw32(0x81777c, 0);
        if (!fe_init(true)) return;
        Render_SetClearColour(0, 0, 0);
        /* the saved games (0x4f4ef0): none yet */
        Render_SetClearColour(0x9c, 0xb4, 0xac);
        Fe_LoadSounds();   /* 0x4f3b50 */
        if (!Fe_LoadFonts() || !Fe_LoadText()) return;   /* 0x4f4d20, 0x4f4b90 */
        Fe_LoadCredits();  /* 0x4f4910 */
        /* 0x40d5c0: the selection's defaults */
        gw32(0x52f2ac, (gm32(0x52f2ac) & 0xfff3c000u) | 0x30000);
        gw32(0x52f2b4, (gm32(0x52f2b4) & 0xfff802ccu) | 0x802cc);
        gw32(0x52f2b0, gm32(0x52f2b0) & ~7u);
        Profiles_Init();
        Fe_ResetPanels();  /* 0x4ef150 */
        Pages_Build(!loaded, true);   /* 0x4e9f70 */
        Fe_LoadDots();     /* 0x4f3f60 */
        Fe_MenuSounds();   /* 0x4f3bb0 */
        gw8(0x516120, 0);
        memset(gm(0x52ebc0), 0, 0x148 * 4);   /* 0x408290 */
        for (int i = 0; i < 4; i++) memset(gm(0x52f0e0 + 7 * (uint32_t)i), 0, 7);   /* 0x40df30 */
        Proc_Event(&group, 0, 1, 2);
        Fe_Stamp();
        Fe_LoadDmd();      /* 0x4d5ca0 */
        return;
    }
    PENDING_NOTE(0x4d15e0, "the front end's start-up after a race");
}

/* 0x4d1b40 (states 1 and 2): five seconds */
static void st_wait5(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    now = Time_Get();
    Input_PollAll();
    if (now - Fe_Stamped() > 5000) {
        Fe_Stamp();
        Proc_Event(&group, 0, 0, 2);
    }
}

/* 0x4d1ba0, 0x4d1c30: FrontEnd\Videos\cm.bik and Intro.bik (0x50fdf0 plays a movie to its end or a
   key). The port has no Bink decoder: the movies end at once. */
static void st_movie(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    Render_SetClearColour(0, 0, 0);
    Render_ClearTarget();
    PENDING_NOTE(0x50fdf0, "the Bink movies (cm.bik, Intro.bik, the rallies')");
    Fe_Stamp();
    Proc_Event(&group, 0, 0, 2);
}

/* 0x4d1a90 (state 5): the title for five seconds or until select, then the menu music */
static void st_title(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    now = Time_Get();
    Input_PollAll();
    InputRecord *r = Input_Record(0);
    if (!(r->pressed & 0x10) && now - Fe_Stamped() < 0x1389) return;
    Fe_Stamp();
    PENDING_NOTE(0x4a28d0, "the music (sounds\\music\\select1.adp)");
    Proc_Event(&group, 0, 0, 2);
}

/* 0x4d1c90 (state 6): the time on the menus, then 0x4ea510 */
static void st_menus(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    now = Time_Get();
    since_menu = now - Fe_Stamped();
    /* 0x4ea510 */
    InputRecord *r = Input_Record(player);
    page = next_page;   /* 0x4ea470 */
    Input_PollAll();
    uint32_t b = r->pressed;
    if (player == 1) b |= Input_Record(0)->pressed & 0x20;
    Menu *to = Menu_Input(page, b);
    if (to) {
        gw32(0x8189b8, Fe_Now());
        next_page = to;
    }
}

uint32_t Fe_MenuTime(void) { return since_menu; }

/* 0x4d1cc0 (state 7): a rally's movie at the first stage of a championship or single rally */
static void st_rally_movie(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    PENDING_NOTE(0x4d1cc0, "the rally's movie");
    Proc_Event(&group, 0, 0, 2);
}

/* 0x4d1e10 (state 8): three pauses of five seconds */
static void st_pause(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    Input_PollAll();
    if (pause_count == (uint32_t)-1) {
        pause_start = Timer_Centis();
        pause_count = 0;
    }
    if (Timer_Centis() - pause_start > 500) {
        if ((int)pause_count < 2) {
            pause_start = Timer_Centis();
            pause_count++;
            return;
        }
        Proc_Event(&group, 0, 0, 2);
    }
}

/* 0x4d1e90 (state 9): out of the front end */
static void st_leave(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    PENDING_NOTE(0x4d1e90, "leaving the front end");
    Proc_Event(&group, 0, 0, 2);
}

/* 0x4d1080 (state 1): the title texture and the copyright pieces below it */
static void dr_copyright(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    static const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    int w, h;
    screen(&w, &h);
    begin();
    if (title_tex) sprite(title_tex, (int16_t)(w * 0x2b / 0x280), (int16_t)(h * 0xca / 0x1e0), white);
    int16_t x = (int16_t)(w * 0x2b / 0x280), y = (int16_t)(h * 0x104 / 0x1e0);
    for (int i = 0; i < ncopyright; i++)
        if (copyright[i]) {
            sprite(copyright[i], x, y, white);
            x = (int16_t)(x + copyright[i]->width);
        }
    draw_scene();
}

/* 0x4d1370 (state 2): the Bink logo on white, the credit line under it */
static void dr_bink(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    static const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff}, black[4] = {0, 0, 0, 0xff};
    int w, h;
    screen(&w, &h);
    Render_SetClearColour(0xff, 0xff, 0xff);
    begin();
    if (bink_tex)
        sprite(bink_tex, (int16_t)(w * 0x140 / 0x280 - bink_tex->width / 2), (int16_t)(h * 0xf0 / 0x1e0 - bink_tex->height / 2),
               white);
    char b[200];
    snprintf(b, sizeof b, "%s", exe_str(gm32(0x52ea54) == 3 ? 0x523d1c : 0x523cd8));
    Font_Print(0, b, w * 0x140 / 0x280, h * 0x15e / 0x1e0, black, 10);
    draw_scene();
}

/* 0x4d0ea0 (state 5): the title on the menus' colour */
static void dr_title(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    static const uint8_t white[4] = {0xff, 0xff, 0xff, 0xff};
    int w, h;
    screen(&w, &h);
    Render_SetClearColour(0x9c, 0xb4, 0xac);
    begin();
    if (title_tex) sprite(title_tex, (int16_t)(w * 0x2b / 0x280), (int16_t)(h * 0xca / 0x1e0), white);
    draw_scene();
}

/* 0x4d0a80 (state 6): the page's draw, the scene */
static void dr_menus(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    Obj_SetPosition(&camera, (const int32_t[3]){0, 0, -0x140000});
    begin();
    Menu_Draw(page);   /* 0x4ea5b0 */
    draw_scene();
}

/* 0x4d0ba0 (state 7): "Loading" and the rally on black */
static void dr_rally_movie(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    PENDING_NOTE(0x4d0ba0, "the rally movie's screen");
}

static const ProcState states[10] = {
    {st_startup, NULL}, {st_wait5, dr_copyright}, {st_wait5, dr_bink}, {st_movie, NULL}, {st_movie, NULL},
    {st_title, dr_title}, {st_menus, dr_menus}, {st_rally_movie, dr_rally_movie}, {st_pause, NULL}, {st_leave, NULL},
};

int Game_Frame(void)
{
    uint32_t m = gm32(0x52ea4c);
    if (m == 0) {
        if (started) {
            Proc_UpdateAll(&group);
            Proc_DrawAll(&group);
            Proc_Advance(&group);
            return 0;
        }
        Proc_InitSlot(&slot, 0, 0xff);
        Proc_Init(&group, 1, &slot, states, exe_data(0x523c18, 16 * 4));
        started = true;
        return 0;
    }
    if (m == 2) {
        PENDING_NOTE(0x4ff450, "World_Frame (the stage set-up screens)");
        return 0;
    }
    if (m == 3) return Race_Frame();
    return 0;
}

void Fe_SetPage(Menu *m) { page = m; }
static int scroll_prev, scroll_cur;   /* DAT_00525398, DAT_0052539c */
void Fe_SetScrollPos(int i) { scroll_prev = scroll_cur = i; }
const char *g_fe_difficulty[3];
