/* The front end's frame, process and start-up (see fe.h), ported from 0x4d0780-0x4d2380, the start-up
   0x4d15e0 and the state functions of table 0x523bc8. */
#include "fe/fe.h"
#include "fe/fe_res.h"
#include "fe/pages.h"
#include "fe/fe_page.h"
#include "fe/movie.h"
#include "game/gstate.h"
#include "game/unlocks.h"
#include "world/tod.h"
#include "audio/snd.h"
#include "audio/music.h"
#include "exe.h"
#include "font.h"
#include "game/input.h"
#include "game/ctlcfg.h"
#include "game/mem.h"
#include "game/paths.h"
#include "game/proc.h"
#include "game/profile.h"
#include "game/race.h"
#include "game/play.h"
#include "game/weather.h"
#include "game/timer.h"
#include "pending.h"
#include "platform.h"
#include "render/d3d7.h"
#include "render/display.h"
#include "render/mesh.h"
#include "render/rstate.h"
#include "render/twod.h"
#include "render/view.h"
#include "text.h"
#include "game/saves.h"
#include "game/service.h"
#include "world/frame.h"
#include "world/obj.h"
#include "world/relight.h"
#include "fe/fe_net.h"
#include "net/session.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void Fe_MenuMusic(void);
static ProcGroup group;          /* DAT_00817da0 */
static ProcSlot slot;            /* DAT_00817d98 */
static bool started;             /* DAT_00817eb0 */
static bool raced;               /* the port: a race ran since the front end left */
static uint32_t now;             /* DAT_00817fe4 */
static uint32_t since_menu;      /* DAT_00817ff4: the time on the menus (state 6) */
static Menu *page, *next_page;   /* DAT_00818abc, DAT_00818ac0 */
static int player;               /* DAT_00818ac8 */
static Obj scene, camera;        /* DAT_00817fc8, DAT_00817fc4 (0x4d0840) */
static Tex *title_tex, *bink_tex, *copyright[4];   /* DAT_00817fd0, DAT_00817ebc, DAT_00817fd4 */
static int ncopyright;
static bool to_game = true;      /* DAT_00523d68: on to the game (the start-up sets it) */
static bool to_stage, to_race;   /* DAT_008180f9, DAT_008180fa: the demo's ways out */
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

/* 0x4d21e0: the resolution's archives: FrontEnd\Res640.bfl, and the language's
   CountrySpecific\<region>\FERes640<E or P>.bfl (Res1024 and FERes1024 for a 1,024 mode) */
void Fe_MountRes(void)
{
    bool hi = Disp_HiRes();
    char p[260];
    Bfl_Unmount(&res);
    Path_Format(p, sizeof p, hi ? 0x523e44 : 0x523e24, Path_FrontEnd());   /* 0x4a9700: no compressed textures */
    Bfl_Mount(&res, p);
    Bfl_Unmount(&res_lang);
    uint32_t lang = gm32(0x52ea54);
    int c = lang <= 2 ? 'E' : 'P';
    Path_Format(p, sizeof p, hi ? 0x523df8 : 0x523dd0, Path_Country(), exe_str(exe_u32(0x516124 + 4 * lang)), c);
    Bfl_Mount(&res_lang, p);
}

/* 0x4d2380: FrontEnd\Common.bfl, the resolution archives, the text archives of the territory's
   languages (Europe: English, French, German, Spanish, Italian) */
static void mount_all(void)
{
    char p[260];
    Path_Format(p, sizeof p, 0x51a0f0, Path_FrontEnd());
    Bfl_Mount(&common, p);
    Fe_MountRes();
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
        /* the device on the settings' mode (0x405c00-0x405c60; 0x4a78a0); the port's --param
           mode=<w>x<h>x<bits> sets them first */
        char md[32];
        int mw, mh, mb;
        if (plat_param("mode", md, sizeof md) && sscanf(md, "%dx%dx%d", &mw, &mh, &mb) == 3)
            gw32(0x52afc4, (uint32_t)mw), gw32(0x52afc8, (uint32_t)mh), gw32(0x52afcc, (uint32_t)mb);
        Disp_Open((int)gm32(0x52afc4), (int)gm32(0x52afc8), (int)gm32(0x52afcc));
        Sprite_Init();
        Tri_Init();
    }
    mount_all();
    int w, h;
    screen(&w, &h);
    ncopyright = Disp_HiRes() ? 4 : 3;
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
    D3d_SetWide(false);   /* (the port's wide modes: the menus pillarboxed) */
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
    to_game = true;
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
        Unlocks_ClearNew();   /* 0x406580 */
        /* the input devices (0x49eaf0(2)); GameInfo.rcf (0x4ea5e0); the network leaderboards
           (0x40e3c0, 0x40e3f0) are not kept by the port yet; Controller.rcf (0x40bf70) */
        Input_Init();
        bool loaded = GameInfo_Load();
        gw32(0x52afb4, gm32(0x52afb4) & ~0x80000u);
        Ctl_Load();
        Input_SetMenuMode(true);
        gw32(0x81777c, 0);
        Net_Init();   /* 0x4aaa40 */
        if (!fe_init(true)) return;
        Render_SetClearColour(0, 0, 0);
        Champs_Scan();   /* 0x4f4ef0: the saved championships */
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
    /* back from the game */
    uint32_t mw = gm32(0x52afb4);
    if ((mw >> 3 & 0x7f) == 4) Fe_ClearGuests();
    gw8(0x52ea59, 0);
    if (!gm8(0x52ea58)) {
        if (mw >> 19 & 1) {
            uint32_t m = mw >> 3 & 0x7f;
            if (m == 10 || m == 12) gw8(0x52ea59, 1);   /* 0x406810: the network's waiting page */
        }
        if (Net_IsHost()) Net_SetJoinable(true);
        gw8(0x818ce4, 0);
        Fe_NetSendMe();   /* 0x4ec2b0 */
    } else {
        gw8(0x52ea58, 0);
        gw32(0x52afb4, gm32(0x52afb4) & ~0x80000u);
    }
    if (gm8(0x52ea52)) {
        Fe_DemoRestore();   /* 0x4ead10 */
        gw8(0x52ea52, 0);
    }
    Unlocks_ClearNew();   /* 0x406580 */
    GameInfo_Save();   /* 0x4ea840 (the leaderboards 0x40e470 not kept yet) */
    Ctl_Save();        /* 0x40bff0 */
    Input_SetMenuMode(true);
    if (!fe_init(false)) return;
    if (gm8(0x52ea50)) Render_SetClearColour(0, 0, 0);
    else Render_SetClearColour(0x9c, 0xb4, 0xac);
    Fe_LoadSounds();
    if (!Fe_LoadFonts() || !Fe_LoadText()) return;
    Fe_LoadCredits();
    Fe_ResetPanels();
    Pages_Build(false, false);
    Fe_LoadDots();
    Fe_MenuSounds();
    Fe_MenuMusic();
    /* 0x4eb470 saves the changed profiles */
    if (!(gm32(0x52afb4) >> 19 & 1) && !gm8(0x52ea50))
        for (int i = 0; i < 0x10; i++) Player_DropProfile(i);   /* 0x4ebec0 */
    memset(gm(0x52ebc0), 0, 0x148 * 4);
    for (int i = 0; i < 4; i++) memset(gm(0x52f0e0 + 7 * (uint32_t)i), 0, 7);
    Fe_Stamp();
    if (gm8(0x52ea50) == 1) {
        gw8(0x52ea50, 0);
        Proc_Event(&group, 0, 6, 2);
    } else
        Proc_Event(&group, 0, 0, 2);
    Fe_LoadDmd();
}

/* 0x4d1b40 (states 1 and 2): five seconds */
static void st_wait5(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    now = Time_Get();
    Input_PollAll();
    Ctl_AxisPresses();   /* 0x40bad0 */
    Ctl_Merge(0, Input_Record(0));
    if (now - Fe_Stamped() > 5000) {
        Fe_Stamp();
        Proc_Event(&group, 0, 0, 2);
    }
}

/* 0x50fdf0 with flags 2 as a frame's step: the movie opened the first time (one that can't be opened
   is over at once), then a frame a call until its end or select */
static bool movie_step(const char *path, int track)
{
    if (!Movie_Playing() && !Movie_Open(path, track)) return false;
    if (Movie_Step()) return true;
    Movie_Close();
    return false;
}

/* 0x4d1ba0, 0x4d1c30: FrontEnd\Videos\cm.bik and Intro.bik on black, then the stamp and on */
static void st_movie_file(uint32_t fmt)
{
    char p[260];
    snprintf(p, sizeof p, exe_str(fmt), Path_Videos());
    if (movie_step(p, 0)) return;
    Fe_Stamp();
    Proc_Event(&group, 0, 0, 2);
}
static void st_cm(ProcGroup *g, uint32_t s) { (void)g, (void)s; st_movie_file(0x523da4); }
static void st_intro(ProcGroup *g, uint32_t s) { (void)g, (void)s; st_movie_file(0x523db0); }

/* the movie's frame on black (BinkBufferBlit: the original draws nothing else meanwhile) */
static void dr_movie(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    Render_SetClearColour(0, 0, 0);
    begin();
    Movie_Draw();
    draw_scene();
}

/* the menus' music: sounds\music\select1.adp at the options' music volume (0x52afb8 bits 0-6) */
static void Fe_MenuMusic(void)
{
    char p[260];
    Path_Format(p, sizeof p, 0x523d70, Path_Music());
    Music_Open(p);
    Music_SetVolume((int)(gm32(0x52afb8) & 0x7f));
    Music_Update();
}

/* 0x4d1a90 (state 5): the title for five seconds or until select, then the menu music */
static void st_title(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    now = Time_Get();
    Input_PollAll();
    Ctl_AxisPresses();
    InputRecord *r = Input_Record(0);
    Ctl_Merge(0, r);
    if (!(r->pressed & 0x10) && now - Fe_Stamped() < 0x1389) return;
    Fe_Stamp();
    Fe_MenuMusic();
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
    Ctl_AxisPresses();
    Music_Poll();   /* 0x4a2fe0 */
    /* not on the controller pages' configuration and analogue set-up, which read the devices */
    if (page != &pg_8288c0 && page != &pg_828aa0) Ctl_Merge(player, r);
    uint32_t b = r->pressed;
    if (player == 1) b |= Input_Record(Ctl_Device(0))->pressed & 0x20;
    Menu *to = Menu_Input(page, b);
    if (to) {
        gw32(0x8189b8, Fe_Now());
        next_page = to;
    }
}

uint32_t Fe_MenuTime(void) { return since_menu; }

/* 0x4d1cc0 (state 7): going on to the game at the first stage of a championship or single rally, the
   music off and the loading screen for four seconds (select ends them), then the rally's movie
   (CountrySpecific\<region>\<FIN, GRE, ...>.bik, on the language's sound track); otherwise on at once */
static bool rally_movie;   /* DAT_00817fec (0x4d20f0) */
static void st_rally_movie(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    rally_movie = false;
    static const uint32_t codes[8] = {0x519280, 0x51927c, 0x519278, 0x519274, 0x519270, 0x51926c, 0x519268, 0x519264};
    if (to_game && (Gs_Mode() == 0 || Gs_Mode() == 1) && Tod_Stage() == 0) {
        rally_movie = true;
        if (Movie_Playing()) {
            if (movie_step("", 0)) return;
            Proc_Event(&group, 0, 0, 2);
            return;
        }
        Music_Stop(false);   /* 0x4a2b50 */
        uint32_t now = Time_Get();
        Input_PollAll();
        Ctl_AxisPresses();   /* 0x40bad0 */
        InputRecord *r = Input_Record(0);
        Ctl_Merge(0, r);
        uint32_t d = now - Fe_Stamped();
        if (d > 4000) {
            char p[260];
            snprintf(p, sizeof p, exe_str(0x523dc0), Path_CountryCd(), exe_str(exe_u32(0x516124 + 4 * gm32(0x52ea54))),
                     exe_str(codes[Tod_Rally() & 7]));
            if (movie_step(p, (int)(gm32(0x52afb4) & 7))) return;   /* the track: 0x4057f0 */
        }
        if (!(r->pressed & 0x10) && d < 4001) return;
    }
    Proc_Event(&group, 0, 0, 2);
}

/* 0x4d1e10 (state 8): three pauses of five seconds */
static void st_pause(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    Input_PollAll();
    Ctl_AxisPresses();
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

/* 0x4d1e90 (state 9): out of the front end: the championship's tables, the options' mode byte, the
   weather and the cars' set-ups, the selection kept in the mode word, the front end's memory and
   textures freed, then the stage set-up (mode 2) or the race (mode 3) */
static void st_leave(ProcGroup *g, uint32_t s)
{
    (void)g, (void)s;
    uint32_t m = gm32(0x52afb4) >> 3 & 0x7f;
    gw8(0x52ea59, 0);
    gw8(0x817574, (uint8_t)(gm32(0x52e890) >> 0x11 & 1));   /* 0x4d0590 */
    if (m != 4) Fe_ChampTables();   /* 0x4cf060 */
    Music_Stop(false);   /* 0x4a2b50 */
    Fe_OptionsMode();   /* 0x4eabc0 */
    if (!to_stage) {
        if (m < 8 || m > 12) Weather_Choose();   /* 0x40dc30 */
        Fe_CarSetups();     /* 0x406820 */
        gw8(0x52f2a8, 0xff);   /* 0x4068d0 */
        Unlocks_ClearNew();   /* 0x406580 */
        if (!gm8(0x52ea52)) {
            if (to_game || to_stage || to_race) {
                uint32_t sel = gm32(0x52f2ac);   /* 0x4ea9c0: rally and stage into bits 23-30 */
                gw32(0x52afb4, (((sel >> 5 & 0xf) << 4 | (sel & 0xf)) << 0x17) | (gm32(0x52afb4) & 0x807fffffu));
            }
            /* GameInfo.rcf and the profiles (0x4ea840, 0x4eb3e0); the leaderboards and Controller.rcf
               (0x40e470, 0x40bff0) not kept yet */
            GameInfo_Save();
            Profiles_Save();
        }
    }
    Fe_Teardown();   /* 0x49c0f0 to the front end's mark, 0x4a5be0, 0x4a5d10 */
    if (m != 4) Fe_AgeProfiles();   /* 0x4ec1a0 */
    Proc_Event(&group, 0, 0, 2);
    if (!to_game) {
        if (to_stage) Game_SetMode(2);
        else if (to_race) Game_SetMode(3);
        else gw8(0x663db8, 1);   /* 0x4a9a40: the quit */
        return;
    }
    if (m == 0 || m == 1) {
        Fe_Stamp();
        Game_SetMode(2);
        return;
    }
    Game_SetMode(m == 2 || m == 3 || m == 8 || m == 9 || m == 10 ? 2 : 3);
    gw32(0x5320a4, Timer_Centis());   /* 0x40af20 */
}

/* 0x4d2070: leave the menus: on to the game (event 0, the rally's movie first), or the demo's way */
void Fe_Leave(bool game, bool stage, bool race)
{
    to_game = game, to_stage = stage, to_race = race;
    Proc_Event(&group, 0, game || stage || race ? 0 : 7, 2);
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
/* 0x4d0d30: a word of the loading screen in font 2 at (x, y), white fading to black from 2.5 s to 4 s
   after the stamp; a 2-pixel bar after it unless the last; the x for the next */
static int loading_word(int x, int y, const char *s, bool last)
{
    int w, h;
    screen(&w, &h);
    int t = (int)(Time_Get() - Fe_Stamped());
    uint8_t c = 0xff;
    if (t >= 0x9c5) c = t < 0xfa1 ? (uint8_t)(0xff - (int)lrintf((float)(t - 0x9c4) * (1.0f / 1500.0f) * 255.0f)) : 0;   /* (0x513ec8, 0x511324) */
    uint8_t rgba[4] = {c, c, c, 0xff};
    Font_Print(2, s, x, y, rgba, 0x11);
    if (last) return x;
    x += w * 10 / 0x280 + Font_TextWidth(2, s);
    int16_t r[4] = {(int16_t)x, (int16_t)(h * 200 / 0x1e0), 2, (int16_t)(h * 0x3c / 0x1e0)};
    Rect_Fill(r, rgba, 1);
    return x + w * 10 / 0x280;
}

/* 0x4d0ba0 (state 7's draw) while the rally's movie is due: "Loading" and the rally's name on black
   (the name in lower case, 0x4aa090), the scene; the movie's frame while it plays */
static void dr_rally_movie(ProcGroup *g, uint32_t s)
{
    if (Movie_Playing()) {
        dr_movie(g, s);
        return;
    }
    if (!rally_movie) return;
    int w, h;
    screen(&w, &h);
    Obj_SetPosition(&camera, (const int32_t[3]){0, 0, -0x140000});
    Obj_SetAngles(&camera, (const uint16_t[3]){0, 0, 0});
    Render_SetClearColour(0, 0, 0);
    begin();
    int x = loading_word(w * 0x1e / 0x280, h / 2, Text_Get(0x1bd), false);
    char b[200];
    snprintf(b, sizeof b, "%s", Text_Get((int)(Tod_Rally() & 0xff) + 0x27));   /* 0x516180 */
    Text_Lower(b);
    loading_word(x, h / 2, b, true);
    draw_scene();
}

static const ProcState states[10] = {
    {st_startup, NULL}, {st_wait5, dr_copyright}, {st_wait5, dr_bink}, {st_cm, dr_movie}, {st_intro, dr_movie},
    {st_title, dr_title}, {st_menus, dr_menus}, {st_rally_movie, dr_rally_movie}, {st_pause, NULL}, {st_leave, NULL},
};

int Game_Frame(void)
{
    uint32_t m = gm32(0x52ea4c);
    if (m == 0 && raced) {
        Play_End();
        raced = false;
    }
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
        /* after a stage the race's own state is freed first (the port's Play_End); the original's
           results come before this (0x41e8d0) */
        if (raced) {
            Play_End();
            raced = false;
        }
        return World_Frame();   /* 0x4ff450 */
    }
    if (m == 3) {
        /* the arcade modes come straight here (0x4d0c60): the port's stage and car set-up (which the
           original does in the race's states 3 and 4) first */
        /* the results went on to the next stage without the front end (0x41e670): the race
           freed and begun again */
        if (raced && Race_Exited()) {
            Play_End();
            raced = false;
            return 0;
        }
        if (!raced) {
            raced = Play_Begin();
            if (!raced) Game_SetMode(0);
            return 0;
        }
        return Race_Frame();
    }
    return 0;
}

void Fe_SetPage(Menu *m) { page = m; }
const char *g_fe_difficulty[3];

void Fe_SetMode(uint32_t m)
{
    gw32(0x52afb4, (gm32(0x52afb4) & 0xfffffc07u) | (m & 0x7f) << 3);
    if (m > 7) gw8(0x52e920, (uint8_t)(gm32(0x52afb4) >> 3 & 0x7f));
}

void Fe_SetPlayer(int n)
{
    uint32_t mw = gm32(0x52afb4);
    player = n >= 1 && n <= 3 && !(mw >> 17 & 1) && (mw >> 3 & 0x7f) != 4 ? n : 0;
}
