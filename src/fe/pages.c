/* The front end's pages (see pages.h): the constructors 0x4f5520-0x4fa780, each a page's title, items
   and callbacks, and Pages_Build 0x4e9f70, which runs them. */
#include "fe/pages.h"
#include "fe/fe.h"
#include "fe/fe_page.h"
#include "game/mem.h"
#include "text.h"
#include "pending.h"

Menu pg_81b158, pg_81b338, pg_81b518, pg_81bab8, pg_81bc98, pg_81be78, pg_81c058, pg_81c418, pg_81c5f8, pg_81c7d8, pg_81cb98, pg_81cd78, pg_81cf58, pg_81d138, pg_81d318, pg_81d4f8, pg_81d6d8, pg_81d8b8, pg_81dc78, pg_81de58, pg_81e218, pg_81e3f8, pg_81e5d8, pg_81e998, pg_81eb78, pg_81ed58, pg_81ef38, pg_81f118, pg_81f2f8, pg_81f4d8, pg_81f6b8, pg_81fa78, pg_81fc58, pg_81fe38, pg_820018, pg_8201f8, pg_8203d8, pg_8205b8, pg_820798, pg_820978, pg_820b58, pg_820d38, pg_8210f8, pg_8212d8, pg_8214b8, pg_821878, pg_821c38, pg_821e18, pg_8221d8, pg_8223b8, pg_822598, pg_822958, pg_822d18, pg_822ef8, pg_8230d8, pg_8232b8, pg_823498, pg_823678, pg_823858, pg_823a38, pg_823c18, pg_823df8, pg_823fd8, pg_8241b8, pg_824498, pg_824678, pg_824858, pg_824a38, pg_824c18, pg_824df8, pg_824fd8, pg_8251b8, pg_826140, pg_826420, pg_826600, pg_8267e0, pg_8269c0, pg_826ba0, pg_826d80, pg_826f60, pg_827140, pg_827320, pg_827500, pg_8276e0, pg_8278c0, pg_827aa0, pg_827c80, pg_827e60, pg_828040, pg_828220, pg_828500, pg_8286e0, pg_8288c0, pg_828aa0, pg_828c80, pg_828e60, pg_829140;

/* 0x4f5520 */
static void build_4f5520(void)
{
    Menu_Init(&pg_8205b8, NULL, 1, &pg_81d6d8, false, NULL, 1, 0, 0);
    Menu_AddBack(&pg_8205b8, NULL, 0x1b, NULL, 0);
    Menu_SetCallbacks(&pg_8205b8, NULL, NULL, fe_4e2b40, NULL);
    Menu_Reset(&pg_8205b8);
}

/* 0x4f5580 */
static void build_4f5580(void)
{
    Menu_Init(&pg_8221d8, NULL, 1, NULL, false, NULL, 1, 0, 0);
    /* the territory's languages: Europe's five, else English and French and (0x405800) Spanish */
    Menu_AddPage(&pg_8221d8, NULL, 6, &pg_81d6d8, NULL, 0);
    Menu_AddPage(&pg_8221d8, NULL, 7, &pg_81d6d8, NULL, 1);
    if (gm32(0x52ea54) == 0) {
        Menu_AddPage(&pg_8221d8, NULL, 8, &pg_81d6d8, NULL, 2);
        Menu_AddPage(&pg_8221d8, NULL, 9, &pg_81d6d8, NULL, 3);
        Menu_AddPage(&pg_8221d8, NULL, 10, &pg_81d6d8, NULL, 4);
    } else
        Menu_AddPage(&pg_8221d8, NULL, 8, &pg_81d6d8, NULL, 2);
    Menu_SetCallbacks(&pg_8221d8, fe_4f3400, fe_4f39e0, fe_4e2da0, fe_4ef270);
    Menu_Reset(&pg_8221d8);
}

/* 0x4f5670 */
static void build_4f5670(void)
{

    Menu_Init(&pg_81d6d8, NULL, 0, &pg_823df8, false, NULL, 1, 1, 0);
    Menu_AddPage(&pg_81d6d8, NULL, 0x4f, &pg_8212d8, NULL, 0);
    Menu_AddPage(&pg_81d6d8, NULL, 0x50, &pg_823a38, NULL, 1);
    Menu_AddPage(&pg_81d6d8, NULL, 0x51, &pg_8267e0, NULL, 2);   /* 0x4fa2d0 */
    Menu_AddPage(&pg_81d6d8, NULL, 0x52, &pg_8241b8, fe_4ec930, 3);
    Menu_AddPage(&pg_81d6d8, NULL, 0x53, &pg_81f118, NULL, 4);
    if (Fe_CreditsShown()) Menu_AddPage(&pg_81d6d8, NULL, 0x54, &pg_822958, NULL, 5);   /* 0x4eaa30 */
    Menu_AddPage(&pg_81d6d8, NULL, 0x56, &pg_823df8, NULL, 6);
    Menu_SetCallbacks(&pg_81d6d8, fe_4ef300, fe_4ef420, fe_4d4ba0, fe_4f25d0);
    Menu_Reset(&pg_81d6d8);
}

/* 0x4f5770 */
static void build_4f5770(void)
{

    Menu_Init(&pg_8212d8, NULL, 0xb, &pg_81d6d8, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_8212d8, NULL, 0x102, &pg_8203d8, fe_4f0c40, -1);
    Menu_AddAction(&pg_8212d8, NULL, 0xe6, fe_4f2750, -1);
    for (int i = 0; i < 4; i++)
        Menu_AddAction(&pg_8212d8, NULL, -1, fe_4f27d0, -1);
    Menu_SetCallbacks(&pg_8212d8, fe_4f2620, fe_4f3ae0, fe_4d9a40, fe_4f07e0);
    Menu_Reset(&pg_8212d8);
}

/* 0x4f5810 */
static void build_4f5810(void)
{
    Menu_Init(&pg_81fe38, NULL, 0x1a, &pg_8212d8, false, NULL, 1, 0, 1);
    Menu_SetCallbacks(&pg_81fe38, NULL, NULL, fe_4d43e0, NULL);
    Menu_Reset(&pg_81fe38);
}

/* 0x4f5850 */
static void build_4f5850(void)
{
    Menu_Init(&pg_81c058, NULL, 0x59, &pg_8212d8, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_81c058, NULL, 0x5a, &pg_823678, NULL, -1);
    Menu_AddPage(&pg_81c058, NULL, 0x82, &pg_820d38, NULL, -1);
    Menu_AddPage(&pg_81c058, NULL, 0x84, &pg_821878, NULL, -1);
    Menu_SetCallbacks(&pg_81c058, fe_4f3ac0, NULL, fe_4d4c40, NULL);
    Menu_Reset(&pg_81c058);
}

/* 0x4f58e0 */
static void build_4f58e0(void)
{
    Menu_Init(&pg_823678, NULL, 0x5a, &pg_81c058, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_823678, NULL, -1, NULL, -1);
    Menu_SetCallbacks(&pg_823678, NULL, NULL, fe_4d4cf0, NULL);
    Menu_Reset(&pg_823678);
}

/* 0x4f5940 */
static void build_4f5940(void)
{
    Menu_Init(&pg_821878, NULL, 0x84, &pg_81c058, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_821878, NULL, 0x82, NULL, 0);
    Menu_SetCallbacks(&pg_821878, NULL, NULL, fe_4d50a0, NULL);
    Menu_Reset(&pg_821878);
}

/* 0x4f59a0 */
static void build_4f59a0(void)
{
    Menu_Init(&pg_823a38, NULL, 0x58, &pg_81d6d8, false, NULL, 1, 2, 1);
    Menu_AddPage(&pg_823a38, NULL, 0xc, &pg_81dc78, NULL, 0);
    Menu_AddPage(&pg_823a38, NULL, 0xd, &pg_824498, NULL, 0);
    Menu_AddPage(&pg_823a38, NULL, 0xf, &pg_823fd8, NULL, 0);
    Menu_AddPage(&pg_823a38, NULL, 0x10, &pg_824678, NULL, 0);
    Menu_AddPage(&pg_823a38, NULL, 0x11, &pg_81e5d8, NULL, 0);
    Menu_SetCallbacks(&pg_823a38, fe_4ef5f0, fe_4f3ae0, fe_4e1fb0, fe_4ef600);
    Menu_Reset(&pg_823a38);
}

/* 0x4f5a60 */
static void build_4f5a60(void)
{
    Menu_Init(&pg_81dc78, NULL, 0xda, &pg_823a38, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_81dc78, NULL, 0x95, &pg_81d4f8, NULL, 0);
    Menu_AddPage(&pg_81dc78, NULL, 0x8a, &pg_820018, NULL, -1);
    Menu_SetCallbacks(&pg_81dc78, NULL, NULL, fe_4e2040, NULL);
    Menu_Reset(&pg_81dc78);
}

/* 0x4f5ae0 */
static void build_4f5ae0(void)
{
    Menu_Init(&pg_820018, NULL, 0xda, &pg_81dc78, false, NULL, 1, 0, 0);
    Menu_AddList(&pg_820018, NULL, -1, 1, 0, false, fe_4f3a00, -1);
    Menu_SetCallbacks(&pg_820018, fe_4ef4c0, fe_4ef4e0, fe_4e20e0, NULL);
    Menu_Reset(&pg_820018);
}

/* 0x4f5b50 */
static void build_4f5b50(void)
{
    Menu_Init(&pg_8214b8, NULL, 0, NULL, false, NULL, 1, 0, 1);
    Menu_SetCallbacks(&pg_8214b8, NULL, fe_4ef590, NULL, NULL);
    Menu_Reset(&pg_8214b8);
}

/* 0x4f5b90 */
static void build_4f5b90(void)
{
    Menu_Init(&pg_822598, NULL, 0x3c, &pg_8241b8, false, NULL, 1, 0, 0);
    Menu_AddList(&pg_822598, NULL, -1, 10, 0, true, fe_4ecf80, 0);
    Menu_SetCallbacks(&pg_822598, fe_4ec9a0, NULL, fe_4dc7b0, fe_4ecfa0);
    Menu_Reset(&pg_822598);
}

/* 0x4f5c00 */
static void build_4f5c00(void)
{
    Menu_Init(&pg_824c18, NULL, 0x35, &pg_822598, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_824c18, NULL, 0x1ec, fe_4ecd60, 0);
    Menu_AddAction(&pg_824c18, NULL, 0x3d, fe_4ecea0, 0);
    Menu_AddBack(&pg_824c18, NULL, 0x1b, NULL, 0);
    Menu_SetCallbacks(&pg_824c18, fe_4eca60, fe_4ecaf0, fe_4dc930, fe_4ecfa0);
    Menu_Reset(&pg_824c18);
}

/* 0x4f5c90 */
static void build_4f5c90(void)
{
    Menu_Init(&pg_821c38, NULL, 0x3d, &pg_824c18, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_821c38, NULL, -1, NULL, 0);
    Menu_AddList(&pg_821c38, NULL, -1, 5, 0, true, NULL, 1);
    Menu_AddList(&pg_821c38, NULL, -1, 0x7a, 0xb, true, NULL, 2);
    Menu_AddList(&pg_821c38, NULL, -1, 8, 6, true, NULL, 3);
    Menu_AddAction(&pg_821c38, NULL, -1, NULL, 4);
    Menu_AddPage(&pg_821c38, NULL, -1, &pg_81bab8, NULL, 5);
    Menu_AddAction(&pg_821c38, NULL, 0x67, fe_4ed340, 6);
    Menu_AddBack(&pg_821c38, NULL, 0x1b, NULL, 7);
    Menu_SetCallbacks(&pg_821c38, fe_4ecfd0, fe_4ed100, fe_4dce00, fe_4ed500);
    Menu_Reset(&pg_821c38);
}

/* 0x4f5d90 */
static void build_4f5d90(void)
{
    Menu_Init(&pg_81e218, NULL, 0x3e, &pg_824c18, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_81e218, NULL, -1, NULL, 0);
    Menu_AddList(&pg_81e218, NULL, -1, 0x16, 0, true, NULL, 1);
    Menu_AddList(&pg_81e218, NULL, -1, 2, 0, true, NULL, 2);
    Menu_AddList(&pg_81e218, NULL, -1, 2, 0, true, fe_4ed610, 3);
    Menu_AddBack(&pg_81e218, NULL, 0x1b, NULL, 4);
    Menu_SetCallbacks(&pg_81e218, fe_4ed530, fe_4ed840, fe_4dd4b0, fe_4edb30);
    Menu_Reset(&pg_81e218);
}

/* 0x4f5e50 */
static void build_4f5e50(void)
{
    Menu_Init(&pg_824df8, NULL, -1, NULL, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_824df8, NULL, -1, &pg_81e218, NULL, -1);
    Menu_SetCallbacks(&pg_824df8, NULL, fe_4edb60, fe_4de1d0, NULL);
    Menu_Reset(&pg_824df8);
}

/* 0x4f5eb0 */
static void build_4f5eb0(void)
{

    Menu_Init(&pg_81f118, NULL, 0x13, &pg_81d6d8, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_81f118, NULL, 0x14, &pg_81ef38, NULL, 0);
    Menu_AddPage(&pg_81f118, NULL, 0x15, &pg_81fa78, NULL, 0);
    Menu_AddPage(&pg_81f118, NULL, 0x16, &pg_823c18, NULL, 0);
    Menu_AddPage(&pg_81f118, NULL, 0x18, &pg_828c80, NULL, 0);   /* 0x4fa4f0 */
    if (gm32(0x52ea54) != 3 && gm32(0x52ea54) != 2) Menu_AddPage(&pg_81f118, NULL, 0x19, &pg_8221d8, NULL, 0);
    Menu_AddPage(&pg_81f118, NULL, 0x148, &pg_81be78, NULL, 1000);
    Menu_AddBack(&pg_81f118, NULL, 0x67, NULL, -1);
    Menu_SetCallbacks(&pg_81f118, fe_4f29b0, fe_4f3ae0, fe_4e2590, NULL);
    Menu_Reset(&pg_81f118);
    pg_81f118.items[6].flags |= MIF_ENABLED | MIF_SELECTABLE;
}

/* 0x4f5fc0 */
static void build_4f5fc0(void)
{

    Menu_Init(&pg_81be78, NULL, 0x148, &pg_81f118, false, NULL, 1, 0, 1);
    for (int i = 0; i < 8; i++)
        Menu_AddList(&pg_81be78, NULL, i + 0x149, 2, 0, false, fe_4f3a50, -1);
    Menu_SetCallbacks(&pg_81be78, fe_4f28c0, NULL, fe_4ded80, fe_4f2970);
    Menu_Reset(&pg_81be78);
}

/* 0x4f6040 */
static void build_4f6040(void)
{

    Menu_Init(&pg_81ef38, NULL, 0x14, &pg_81f118, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81ef38, NULL, 0x91, 2, 0, false, NULL, 0);
    Menu_AddList(&pg_81ef38, NULL, 0x93, 2, 0, false, NULL, 2);
    Menu_AddList(&pg_81ef38, NULL, 0x96, 2, 0, false, NULL, 3);
    Menu_AddList(&pg_81ef38, NULL, 0x151, 3, 0, false, NULL, 4);
    Menu_AddPage(&pg_81ef38, NULL, 0x8e, &pg_8210f8, NULL, -1);
    Menu_AddPage(&pg_81ef38, NULL, 0x67, pg_81ef38.parent, fe_4f2d20, -1);
    Menu_SetCallbacks(&pg_81ef38, fe_4f2c40, NULL, fe_4df410, NULL);
    Menu_Reset(&pg_81ef38);
}

/* 0x4f6130 */
static void build_4f6130(void)
{
    Menu_Init(&pg_8210f8, NULL, 0x8e, &pg_81ef38, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_8210f8, NULL, -1, &pg_81de58, NULL, 0);
    Menu_AddList(&pg_8210f8, NULL, -1, 2, 0, false, NULL, 1);
    Menu_AddPage(&pg_8210f8, NULL, -1, &pg_81c7d8, NULL, 2);
    Menu_AddPage(&pg_8210f8, NULL, -1, &pg_81cb98, NULL, 3);
    Menu_AddList(&pg_8210f8, NULL, -1, 3, 0, false, NULL, 4);
    Menu_AddList(&pg_8210f8, NULL, -1, 10, 0, false, NULL, 5);
    Menu_AddPage(&pg_8210f8, NULL, 0x1ac, &pg_81cd78, NULL, 6);
    Menu_AddBack(&pg_8210f8, NULL, 0x67, fe_4f1e40, 0xe);
    Menu_SetCallbacks(&pg_8210f8, fe_4f1c00, NULL, fe_4dfe20, fe_4f1f70);
    Menu_Reset(&pg_8210f8);
}

/* 0x4f6240 */
static void build_4f6240(void)
{
    Menu_Init(&pg_81cd78, NULL, 0x1b4, &pg_8210f8, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81cd78, NULL, 0x1b8, 3, 0, false, NULL, 8);
    Menu_AddList(&pg_81cd78, NULL, 0x1b7, 2, 0, false, NULL, 9);
    Menu_AddList(&pg_81cd78, NULL, 0x1b6, 2, 0, false, NULL, 10);
    Menu_AddBack(&pg_81cd78, NULL, 0x67, fe_4f1db0, 0xe);
    Menu_SetCallbacks(&pg_81cd78, fe_4f1d00, fe_4f1d60, fe_4e0770, NULL);
    Menu_Reset(&pg_81cd78);
}

/* 0x4f6300 */
static void build_4f6300(void)
{
    Menu_Init(&pg_81de58, NULL, 0x1b1, &pg_8210f8, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_81de58, NULL, 0x133, fe_4f2050, 0);
    Menu_AddAction(&pg_81de58, NULL, 0x1ae, fe_4f2050, 0);
    Menu_AddAction(&pg_81de58, NULL, 0x1af, fe_4f2050, 0);
    Menu_AddAction(&pg_81de58, NULL, 0x1b0, fe_4f2050, 0);
    Menu_SetCallbacks(&pg_81de58, fe_4f1fa0, NULL, fe_4e1890, NULL);
    Menu_Reset(&pg_81de58);
}

/* 0x4f63b0 */
static void build_4f63b0(void)
{

    Menu_Init(&pg_81c7d8, NULL, 0x14, &pg_8210f8, false, NULL, 1, 0, 1);
    for (uint32_t i = 0; i < Fe_DisplayModes(); i++)   /* 0x4a8be0 */
        Menu_AddAction(&pg_81c7d8, NULL, -1, fe_4f2210, 0);
    Menu_SetCallbacks(&pg_81c7d8, fe_4f21c0, NULL, fe_4e1920, NULL);
    Menu_Reset(&pg_81c7d8);
}

/* 0x4f6420 */
static void build_4f6420(void)
{
    Menu_Init(&pg_81cb98, NULL, 0x14, &pg_8210f8, false, NULL, 1, 0, 0);
    Menu_AddList(&pg_81cb98, NULL, -1, 2, 0, false, fe_4f2430, 0);
    Menu_SetCallbacks(&pg_81cb98, fe_4f2360, fe_4f23d0, fe_4e1d70, NULL);
    Menu_Reset(&pg_81cb98);
}

/* 0x4f6490 */
static void build_4f6490(void)
{

    Menu_Init(&pg_81fa78, NULL, 0x15, &pg_81f118, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81fa78, NULL, 0x5e, 0xb, 10, false, NULL, 0);
    Menu_AddList(&pg_81fa78, NULL, 0x5f, 0xb, 10, false, NULL, 1);
    Menu_AddList(&pg_81fa78, NULL, 0x60, 0xb, 10, false, NULL, 2);
    Menu_AddPage(&pg_81fa78, NULL, 0x67, pg_81fa78.parent, NULL, 4);
    Menu_SetCallbacks(&pg_81fa78, fe_4f2840, fe_4f2b00, fe_4e2610, fe_4f2b70);
    Menu_Reset(&pg_81fa78);
}

/* 0x4f6540 */
static void build_4f6540(void)
{
    Menu_Init(&pg_823c18, NULL, 0x16, &pg_81f118, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_823c18, NULL, 0x162, &pg_820b58, NULL, -1);
    Menu_AddPage(&pg_823c18, NULL, 0x163, &pg_81c418, NULL, -1);
    Menu_AddPage(&pg_823c18, NULL, 0x164, &pg_8223b8, NULL, -1);
    Menu_AddPage(&pg_823c18, NULL, 0x165, &pg_8232b8, NULL, -1);
    Menu_AddPage(&pg_823c18, NULL, 0x166, &pg_81c5f8, NULL, -1);
    Menu_SetCallbacks(&pg_823c18, fe_4f3a70, NULL, fe_4e3230, NULL);
    Menu_Reset(&pg_823c18);
}

/* 0x4f6610 */
static void build_4f6610(void)
{
    Menu_Init(&pg_820d38, NULL, 0x16, &pg_81c058, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_820d38, NULL, 0x162, &pg_823498, NULL, -1);
    Menu_AddPage(&pg_820d38, NULL, 0x163, &pg_8201f8, NULL, -1);
    Menu_AddPage(&pg_820d38, NULL, 0x164, &pg_822ef8, NULL, -1);
    Menu_AddPage(&pg_820d38, NULL, 0x165, &pg_81b518, NULL, -1);
    Menu_AddPage(&pg_820d38, NULL, 0x166, &pg_81f6b8, NULL, -1);
    Menu_SetCallbacks(&pg_820d38, fe_4f3a70, NULL, fe_4e3230, NULL);
    Menu_Reset(&pg_820d38);
}

/* 0x4f66e0 */
static void build_4f66e0(void)
{

    Menu_Init(&pg_820b58, NULL, 0x162, &pg_823c18, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_820b58, NULL, -1, fe_4f3a90, -1);
    Menu_SetCallbacks(&pg_820b58, NULL, NULL, fe_4e3340, NULL);
    Menu_Reset(&pg_820b58);
    pg_820b58.items[0].flags |= MIF_SELECT_STEPS;
}

/* 0x4f6740 */
static void build_4f6740(void)
{

    Menu_Init(&pg_81c418, NULL, 0x163, &pg_823c18, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_81c418, NULL, 0x27, fe_4f3a90, -1);
    Menu_AddAction(&pg_81c418, NULL, 0x28, fe_4f3a90, -1);
    Menu_AddAction(&pg_81c418, NULL, 0x29, fe_4f3a90, -1);
    Menu_AddAction(&pg_81c418, NULL, 0x2a, fe_4f3a90, -1);
    Menu_AddAction(&pg_81c418, NULL, 0x2b, fe_4f3a90, -1);
    Menu_AddAction(&pg_81c418, NULL, 0x2c, fe_4f3a90, -1);
    Menu_AddAction(&pg_81c418, NULL, 0x2d, fe_4f3a90, -1);
    Menu_AddAction(&pg_81c418, NULL, 0x2e, fe_4f3a90, -1);
    Menu_SetCallbacks(&pg_81c418, fe_4f2d90, fe_4f3970, fe_4e4130, NULL);
    Menu_Reset(&pg_81c418);
}

/* 0x4f6830 */
static void build_4f6830(void)
{
    Menu_Init(&pg_8223b8, NULL, 0x164, &pg_823c18, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_8223b8, NULL, 0x27, NULL, -1);
    Menu_AddAction(&pg_8223b8, NULL, 0x28, NULL, -1);
    Menu_AddAction(&pg_8223b8, NULL, 0x29, NULL, -1);
    Menu_AddAction(&pg_8223b8, NULL, 0x2a, NULL, -1);
    Menu_AddAction(&pg_8223b8, NULL, 0x2b, NULL, -1);
    Menu_AddAction(&pg_8223b8, NULL, 0x2c, NULL, -1);
    Menu_AddAction(&pg_8223b8, NULL, 0x2d, NULL, -1);
    Menu_AddAction(&pg_8223b8, NULL, 0x2e, NULL, -1);
    Menu_SetCallbacks(&pg_8223b8, fe_4f2e70, fe_4f3980, fe_4e4fc0, NULL);
    Menu_Reset(&pg_8223b8);
}

/* 0x4f6910 */
static void build_4f6910(void)
{
    Menu_Init(&pg_8232b8, NULL, 0x165, &pg_823c18, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_8232b8, NULL, 0xe9, fe_4f3a90, -1);
    Menu_AddAction(&pg_8232b8, NULL, 0xe8, fe_4f3a90, -1);
    Menu_SetCallbacks(&pg_8232b8, fe_4f2f40, fe_4f3990, fe_4e5c90, NULL);
    Menu_Reset(&pg_8232b8);
}

/* 0x4f6990 */
static void build_4f6990(void)
{
    Menu_Init(&pg_81c5f8, NULL, 0x166, &pg_823c18, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_81c5f8, NULL, -1, NULL, -1);
    Menu_SetCallbacks(&pg_81c5f8, NULL, NULL, fe_4e6a80, NULL);
    Menu_Reset(&pg_81c5f8);
}

/* 0x4f69f0 */
static void build_4f69f0(void)
{

    Menu_Init(&pg_823498, NULL, 0x162, &pg_820d38, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_823498, NULL, -1, fe_4f3a90, -1);
    Menu_SetCallbacks(&pg_823498, NULL, NULL, fe_4e3a80, NULL);
    Menu_Reset(&pg_823498);
    pg_823498.items[0].flags |= MIF_SELECT_STEPS;
}

/* 0x4f6a50 */
static void build_4f6a50(void)
{
    Menu_Init(&pg_8201f8, NULL, 0x163, &pg_820d38, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_8201f8, NULL, 0x27, fe_4f3a90, -1);
    Menu_AddAction(&pg_8201f8, NULL, 0x28, fe_4f3a90, -1);
    Menu_AddAction(&pg_8201f8, NULL, 0x29, fe_4f3a90, -1);
    Menu_AddAction(&pg_8201f8, NULL, 0x2a, fe_4f3a90, -1);
    Menu_AddAction(&pg_8201f8, NULL, 0x2b, fe_4f3a90, -1);
    Menu_AddAction(&pg_8201f8, NULL, 0x2c, fe_4f3a90, -1);
    Menu_AddAction(&pg_8201f8, NULL, 0x2d, fe_4f3a90, -1);
    Menu_AddAction(&pg_8201f8, NULL, 0x2e, fe_4f3a90, -1);
    Menu_SetCallbacks(&pg_8201f8, fe_4f2d90, fe_4f3970, fe_4e48b0, NULL);
    Menu_Reset(&pg_8201f8);
}

/* 0x4f6b40 */
static void build_4f6b40(void)
{

    Menu_Init(&pg_822ef8, NULL, 0x164, &pg_820d38, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_822ef8, NULL, 0x27, NULL, -1);
    Menu_AddAction(&pg_822ef8, NULL, 0x28, NULL, -1);
    Menu_AddAction(&pg_822ef8, NULL, 0x29, NULL, -1);
    Menu_AddAction(&pg_822ef8, NULL, 0x2a, NULL, -1);
    Menu_AddAction(&pg_822ef8, NULL, 0x2b, NULL, -1);
    Menu_AddAction(&pg_822ef8, NULL, 0x2c, NULL, -1);
    Menu_AddAction(&pg_822ef8, NULL, 0x2d, NULL, -1);
    Menu_AddAction(&pg_822ef8, NULL, 0x2e, NULL, -1);
    Menu_SetCallbacks(&pg_822ef8, fe_4f2e70, fe_4f3980, fe_4e5630, NULL);
    Menu_Reset(&pg_822ef8);
    pg_822ef8.items[0].flags |= MIF_SELECT_STEPS;
    pg_822ef8.items[1].flags |= MIF_SELECT_STEPS;
    pg_822ef8.items[2].flags |= MIF_SELECT_STEPS;
    pg_822ef8.items[3].flags |= MIF_SELECT_STEPS;
    pg_822ef8.items[4].flags |= MIF_SELECT_STEPS;
    pg_822ef8.items[5].flags |= MIF_SELECT_STEPS;
    pg_822ef8.items[6].flags |= MIF_SELECT_STEPS;
    pg_822ef8.items[7].flags |= MIF_SELECT_STEPS;
}

/* 0x4f6c90 */
static void build_4f6c90(void)
{
    Menu_Init(&pg_81b518, NULL, 0x165, &pg_820d38, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_81b518, NULL, 0xe9, fe_4f3a90, -1);
    Menu_AddAction(&pg_81b518, NULL, 0xe8, fe_4f3a90, -1);
    Menu_SetCallbacks(&pg_81b518, fe_4f2f40, fe_4f3990, fe_4e63d0, NULL);
    Menu_Reset(&pg_81b518);
}

/* 0x4f6d10 */
static void build_4f6d10(void)
{
    Menu_Init(&pg_81f6b8, NULL, 0x166, &pg_820d38, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_81f6b8, NULL, -1, NULL, -1);
    Menu_SetCallbacks(&pg_81f6b8, NULL, NULL, fe_4e7120, NULL);
    Menu_Reset(&pg_81f6b8);
}

/* 0x4f6d70 */
static void build_4f6d70(void)
{
    Menu_Init(&pg_822958, NULL, 0x34, &pg_81d6d8, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_822958, NULL, -1, NULL, 0);
    Menu_SetCallbacks(&pg_822958, fe_4f1bd0, NULL, fe_4dec30, NULL);
    Menu_Reset(&pg_822958);
}

/* 0x4f6dd0 */
static void build_4f6dd0(void)
{
    Menu_Init(&pg_823df8, NULL, 3, &pg_81d6d8, false, NULL, 1, 1, 1);
    Menu_AddPage(&pg_823df8, NULL, 5, &pg_820798, NULL, 0);
    Menu_AddBack(&pg_823df8, NULL, 4, fe_4f3b20, 0);
    Menu_SetCallbacks(&pg_823df8, fe_4f3b00, fe_4f3ae0, fe_4e2ab0, fe_4f3b30);
    Menu_Reset(&pg_823df8);
}

/* 0x4f6e50 */
static void build_4f6e50(int p1)
{

    Menu_Init(&pg_81d318, NULL, 0x1c, &pg_81d4f8, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_81d318, NULL, 0xc5, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81d318, NULL, 0xc6, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81d318, NULL, 199, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81d318, NULL, 200, fe_4ef7c0, -1);
    Menu_SetCallbacks(&pg_81d318, fe_4f3610, fe_4f39f0, fe_4d5fb0, NULL);
    Menu_Reset(&pg_81d318);
    /* the player count into the first item's value (not the selection, as on the arcade pages) */
    pg_81d318.items[0].value = (uint8_t)(p1 - 1);
}

/* 0x4f6f10 */
static void build_4f6f10(int p1)
{

    Menu_Init(&pg_820978, NULL, 0x1c, &pg_824498, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_820978, NULL, 0xc5, fe_4ef7c0, -1);
    Menu_AddAction(&pg_820978, NULL, 0xc6, fe_4ef7c0, -1);
    Menu_AddAction(&pg_820978, NULL, 199, fe_4ef7c0, -1);
    Menu_AddAction(&pg_820978, NULL, 200, fe_4ef7c0, -1);
    Menu_SetCallbacks(&pg_820978, fe_4f3610, fe_4f39f0, fe_4d5fb0, NULL);
    Menu_Reset(&pg_820978);
    pg_820978.items[0].value = (uint8_t)(p1 - 1);
}

/* 0x4f6fd0 */
static void build_4f6fd0(int p1)
{

    Menu_Init(&pg_81f2f8, NULL, 0x1c, &pg_823fd8, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_81f2f8, NULL, 0xc5, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81f2f8, NULL, 0xc6, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81f2f8, NULL, 199, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81f2f8, NULL, 200, fe_4ef7c0, -1);
    Menu_SetCallbacks(&pg_81f2f8, fe_4f3610, fe_4f39f0, fe_4d5fb0, NULL);
    Menu_Reset(&pg_81f2f8);
    pg_81f2f8.items[0].value = (uint8_t)(p1 - 1);
}

/* 0x4f7090 */
static void build_4f7090(int p1)
{

    Menu_Init(&pg_824678, NULL, 0x1c, &pg_823a38, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_824678, NULL, 0xc5, fe_4ef7c0, -1);
    Menu_AddAction(&pg_824678, NULL, 0xc6, fe_4ef7c0, -1);
    Menu_AddAction(&pg_824678, NULL, 199, fe_4ef7c0, -1);
    Menu_AddAction(&pg_824678, NULL, 200, fe_4ef7c0, -1);
    Menu_SetCallbacks(&pg_824678, fe_4f3610, fe_4f39f0, fe_4d5fb0, NULL);
    Menu_Reset(&pg_824678);
    pg_824678.items[0].value = (uint8_t)(p1 - 1);
}

/* 0x4f7150 */
static void build_4f7150(int p1)
{

    Menu_Init(&pg_81e5d8, NULL, 0x40, &pg_823a38, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_81e5d8, NULL, 0xc5, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81e5d8, NULL, 0xc6, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81e5d8, NULL, 199, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81e5d8, NULL, 200, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81e5d8, NULL, 0xc9, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81e5d8, NULL, 0xca, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81e5d8, NULL, 0xcb, fe_4ef7c0, -1);
    Menu_AddAction(&pg_81e5d8, NULL, 0xcc, fe_4ef7c0, -1);
    Menu_SetCallbacks(&pg_81e5d8, fe_4f3530, fe_4f39f0, fe_4d5fb0, NULL);
    Menu_Reset(&pg_81e5d8);
    pg_81e5d8.items[0].value = (uint8_t)(p1 - 1);
}

/* 0x4f7270 */
static void build_4f7270(void)
{
    Menu_Init(&pg_81d4f8, NULL, 0x1d, &pg_81dc78, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_81d4f8, NULL, 0xd0, &pg_81d318, fe_4ef930, 0);
    Menu_AddPage(&pg_81d4f8, NULL, 0xd1, &pg_81d318, fe_4ef930, 0);
    Menu_AddPage(&pg_81d4f8, NULL, 0xd2, &pg_81d318, fe_4ef930, 0);
    Menu_SetCallbacks(&pg_81d4f8, fe_4ef8f0, NULL, fe_4d6290, NULL);
    Menu_Reset(&pg_81d4f8);
}

/* 0x4f7310 */
static void build_4f7310(void)
{
    Menu_Init(&pg_824498, NULL, 0x1d, &pg_823a38, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_824498, NULL, 0xd0, &pg_820978, fe_4ef930, 0);
    Menu_AddPage(&pg_824498, NULL, 0xd1, &pg_820978, fe_4ef930, 0);
    Menu_AddPage(&pg_824498, NULL, 0xd2, &pg_820978, fe_4ef930, 0);
    Menu_SetCallbacks(&pg_824498, fe_4ef8f0, NULL, fe_4d6290, NULL);
    Menu_Reset(&pg_824498);
}

/* 0x4f73b0 */
static void build_4f73b0(void)
{
    Menu_Init(&pg_823fd8, NULL, 0x1d, &pg_823a38, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_823fd8, NULL, 0xd0, &pg_81f2f8, fe_4ef930, 0);
    Menu_AddPage(&pg_823fd8, NULL, 0xd1, &pg_81f2f8, fe_4ef930, 0);
    Menu_AddPage(&pg_823fd8, NULL, 0xd2, &pg_81f2f8, fe_4ef930, 0);
    Menu_SetCallbacks(&pg_823fd8, fe_4ef8f0, NULL, fe_4d6290, NULL);
    Menu_Reset(&pg_823fd8);
}

/* 0x4f7450 */
static void build_4f7450(void)
{
    Menu_Init(&pg_81ed58, NULL, 0x41, &pg_823a38, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_81ed58, NULL, 0x42, &pg_823858, fe_4ef960, -1);
    Menu_AddPage(&pg_81ed58, NULL, 0xd0, &pg_823858, fe_4ef960, -1);
    Menu_AddPage(&pg_81ed58, NULL, 0xd1, &pg_823858, fe_4ef960, -1);
    Menu_AddPage(&pg_81ed58, NULL, 0xd2, &pg_823858, fe_4ef960, -1);
    Menu_SetCallbacks(&pg_81ed58, fe_4ef950, NULL, fe_4d63e0, NULL);
    Menu_Reset(&pg_81ed58);
}

/* 0x4f7510 */
static void build_4f7510(int p1)
{

    Menu_Init(&pg_8230d8, NULL, 0x1e, &pg_81d318, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_8230d8, NULL, 0xd3, &pg_8241b8, fe_4efb20, -1);
    Menu_AddPage(&pg_8230d8, NULL, 0xd4, &pg_8241b8, fe_4efb20, -1);
    Menu_SetCallbacks(&pg_8230d8, NULL, fe_4ef410, fe_4d6460, NULL);
    Menu_Reset(&pg_8230d8);
    pg_8230d8.sel = (int8_t)(p1);
    pg_8230d8.items[0].flags |= MIF_ENABLED;
}

/* 0x4f75b0 */
static void build_4f75b0(int p1)
{

    Menu_Init(&pg_824858, NULL, 0x1e, &pg_820978, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_824858, NULL, 0xd3, &pg_8241b8, fe_4efb20, -1);
    Menu_AddPage(&pg_824858, NULL, 0xd4, &pg_8241b8, fe_4efb20, -1);
    Menu_SetCallbacks(&pg_824858, NULL, fe_4ef410, fe_4d6460, NULL);
    Menu_Reset(&pg_824858);
    pg_824858.sel = (int8_t)(p1);
    pg_824858.items[0].flags |= MIF_ENABLED;
}

/* 0x4f7650 */
static void build_4f7650(int p1)
{

    Menu_Init(&pg_8251b8, NULL, 0x1e, &pg_81f2f8, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_8251b8, NULL, 0xd3, &pg_8241b8, fe_4efb20, -1);
    Menu_AddPage(&pg_8251b8, NULL, 0xd4, &pg_8241b8, fe_4efb20, -1);
    Menu_SetCallbacks(&pg_8251b8, NULL, fe_4ef410, fe_4d6460, NULL);
    Menu_Reset(&pg_8251b8);
    pg_8251b8.sel = (int8_t)(p1);
    pg_8251b8.items[0].flags |= MIF_ENABLED;
}

/* 0x4f76f0 */
static void build_4f76f0(void)
{

    Menu_Init(&pg_8241b8, NULL, 0x21, NULL, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_8241b8, NULL, 0x102, &pg_8203d8, fe_4f0c40, -1);
    Menu_AddAction(&pg_8241b8, NULL, 0xe6, fe_4f0960, 0);
    Menu_AddAction(&pg_8241b8, NULL, 0xe5, fe_4f0ac0, 0);
    for (int i = 0; i < 4; i++)
        Menu_AddAction(&pg_8241b8, NULL, -1, fe_4f0c50, -1);
    Menu_SetCallbacks(&pg_8241b8, fe_4f03f0, NULL, fe_4d9880, fe_4f07e0);
    Menu_Reset(&pg_8241b8);
}

/* 0x4f77a0 */
static void build_4f77a0(void)
{
    Menu_Init(&pg_8203d8, NULL, 0x17a, &pg_8241b8, false, NULL, 1, 0, 1);
    Menu_AddList6(&pg_8203d8, NULL, -1, 1, 0, true, fe_4f0820, -1);
    Menu_AddBack(&pg_8203d8, NULL, 0x11a, NULL, 0);
    Menu_SetCallbacks(&pg_8203d8, fe_4f0580, fe_4f0620, fe_4d6a60, fe_4f0600);
    Menu_Reset(&pg_8203d8);
}

/* 0x4f7820 */
static void build_4f7820(void)
{
    Menu_Init(&pg_824a38, NULL, 0x22, NULL, false, NULL, 1, 0, 1);
    Menu_AddList6(&pg_824a38, NULL, -1, 10, 0, true, fe_4f1040, 0);
    Menu_AddList6(&pg_824a38, NULL, -1, 10, 0, true, fe_4f1040, 1);
    Menu_AddList6(&pg_824a38, NULL, -1, 10, 0, true, fe_4f1040, 2);
    Menu_SetCallbacks(&pg_824a38, fe_4f0d30, fe_4f0e80, fe_4d6f10, fe_4f0da0);
    Menu_Reset(&pg_824a38);
}

/* 0x4f78c0 */
static void build_4f78c0(void)
{
    Menu_Init(&pg_81d8b8, NULL, 0x8c, &pg_824a38, false, NULL, 1, 0, 1);
    Menu_AddList6(&pg_81d8b8, NULL, -1, 10, 0, true, fe_4f13c0, 0);
    Menu_AddList6(&pg_81d8b8, NULL, -1, 10, 0, true, fe_4f13c0, 1);
    Menu_AddList6(&pg_81d8b8, NULL, -1, 10, 0, true, fe_4f13c0, 2);
    Menu_SetCallbacks(&pg_81d8b8, fe_4f1160, fe_4f11d0, fe_4d7380, fe_4f0e60);
    Menu_Reset(&pg_81d8b8);
}

/* 0x4f7970 */
static void build_4f7970(void)
{
    Menu_Init(&pg_81bc98, NULL, 0x8d, &pg_81d8b8, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81bc98, NULL, -1, 0xff, 100, true, NULL, 0);
    Menu_AddList(&pg_81bc98, NULL, -1, 0xc, 0, true, NULL, 1);
    Menu_AddList(&pg_81bc98, NULL, -1, 0x1f, 0, true, NULL, 2);
    Menu_AddAction(&pg_81bc98, NULL, 0x67, fe_4f15d0, -1);
    Menu_SetCallbacks(&pg_81bc98, fe_4f16f0, fe_4f1640, fe_4d7750, fe_4f1b90);
    Menu_Reset(&pg_81bc98);
}

/* 0x4f7a30 */
static void build_4f7a30(void)
{
    Menu_Init(&pg_821e18, NULL, 0x33, NULL, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_821e18, NULL, 0x98, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0x99, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0x9a, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0x9b, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0x9c, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0x9d, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0x9e, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0x9f, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa0, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa1, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa2, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa3, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa4, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa5, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa6, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa7, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa8, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xa9, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xaa, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xab, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xac, &pg_81cf58, fe_4f1a10, -1);
    Menu_AddPage(&pg_821e18, NULL, 0xad, &pg_81cf58, fe_4f1a10, -1);
    Menu_SetCallbacks(&pg_821e18, fe_4f17d0, fe_4f39d0, fe_4d7db0, fe_4f1b90);
    Menu_Reset(&pg_821e18);
}

/* 0x4f7d00 */
static void build_4f7d00(void)
{
    Menu_Init(&pg_81cf58, NULL, 0x97, &pg_821e18, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81cf58, NULL, 0x88, 2, 0, false, fe_4f1a40, 0);
    Menu_SetCallbacks(&pg_81cf58, fe_4f1960, fe_4f39d0, fe_4d8480, NULL);
    Menu_Reset(&pg_81cf58);
}

/* 0x4f7d70 */
static void build_4f7d70(void)
{
    Menu_Init(&pg_822d18, NULL, 0x33, &pg_8241b8, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_822d18, NULL, 0x88, 2, 0, false, fe_4f1b30, 0);
    Menu_SetCallbacks(&pg_822d18, fe_4f19d0, NULL, fe_4d9450, NULL);
    Menu_Reset(&pg_822d18);
}

/* 0x4f7de0 */
static void build_4f7de0(void)
{
    Menu_Init(&pg_81eb78, NULL, 0x24, NULL, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_81eb78, NULL, 0x27, fe_4efb70, -1);
    Menu_AddAction(&pg_81eb78, NULL, 0x28, fe_4efb70, -1);
    Menu_AddAction(&pg_81eb78, NULL, 0x29, fe_4efb70, -1);
    Menu_AddAction(&pg_81eb78, NULL, 0x2a, fe_4efb70, -1);
    Menu_AddAction(&pg_81eb78, NULL, 0x2b, fe_4efb70, -1);
    Menu_AddAction(&pg_81eb78, NULL, 0x2c, fe_4efb70, -1);
    Menu_AddAction(&pg_81eb78, NULL, 0x2d, fe_4efb70, -1);
    Menu_AddAction(&pg_81eb78, NULL, 0x2e, fe_4efb70, -1);
    Menu_SetCallbacks(&pg_81eb78, fe_4f36e0, fe_4efb50, fe_4d9ad0, NULL);
    Menu_Reset(&pg_81eb78);
}

/* 0x4f7ed0 */
static void build_4f7ed0(void)
{
    Menu_Init(&pg_81e3f8, NULL, 0x25, &pg_81eb78, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_81e3f8, NULL, 0xc5, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 0xc6, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 199, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 200, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 0xc9, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 0xca, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 0xcb, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 0xcc, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 0xcd, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 0xce, fe_4efde0, -1);
    Menu_AddAction(&pg_81e3f8, NULL, 0xcf, fe_4efde0, -1);
    Menu_SetCallbacks(&pg_81e3f8, fe_4f3010, fe_4efdc0, fe_4d9c40, NULL);
    Menu_Reset(&pg_81e3f8);
}

/* 0x4f8020 */
static void build_4f8020(void)
{
    Menu_Init(&pg_81f4d8, NULL, 0x155, &pg_81e3f8, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_81f4d8, NULL, 0xc5, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 0xc6, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 199, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 200, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 0xc9, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 0xca, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 0xcb, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 0xcc, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 0xcd, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 0xce, fe_4f0050, -1);
    Menu_AddAction(&pg_81f4d8, NULL, 0xcf, fe_4f0050, -1);
    Menu_SetCallbacks(&pg_81f4d8, fe_4f3120, fe_4efdd0, fe_4da630, NULL);
    Menu_Reset(&pg_81f4d8);
}

/* 0x4f8170 */
static void build_4f8170(void)
{

    Menu_Init(&pg_823858, NULL, 0x46, &pg_822d18, false, NULL, 1, 3, 1);
    Menu_AddList(&pg_823858, NULL, -1, 4, 0, false, NULL, 0);
    Menu_AddList(&pg_823858, NULL, -1, 4, 0, false, NULL, 1);
    Menu_AddList(&pg_823858, NULL, -1, 5, 0, false, NULL, 2);
    Menu_AddAction(&pg_823858, NULL, -1, fe_4f01c0, 3);
    Menu_SetCallbacks(&pg_823858, fe_4ef970, fe_4f0250, fe_4e1230, NULL);
    Menu_Reset(&pg_823858);
    pg_823858.items[0].flags |= MIF_SELECT_STEPS;
    pg_823858.items[1].flags |= MIF_SELECT_STEPS;
    pg_823858.items[2].flags |= MIF_SELECT_STEPS;
}

/* 0x4f8250 */
static void build_4f8250(void)
{
    Menu_Init(&pg_820798, NULL, -1, &pg_81d6d8, false, NULL, 1, 0, 0);
    Menu_SetCallbacks(&pg_820798, NULL, fe_4ef5e0, NULL, NULL);
    Menu_Reset(&pg_820798);
}

/* 0x4f8500 */
static void build_4f8500(void)
{
    Menu_Init(&pg_824fd8, NULL, 0x13, &pg_821c38, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_824fd8, NULL, -1, 0xb, 0, true, fe_4edca0, 0);
    Menu_AddList(&pg_824fd8, NULL, -1, 8, 0, true, NULL, 1);
    Menu_AddAction(&pg_824fd8, NULL, 0x67, fe_4edd50, 2);
    Menu_AddBack(&pg_824fd8, NULL, 0x1b, NULL, 3);
    Menu_SetCallbacks(&pg_824fd8, fe_4edb70, fe_4ede10, fe_4e77c0, NULL);
    Menu_Reset(&pg_824fd8);
}

/* 0x4f85b0 */
static void build_4f85b0(void)
{
    Menu_Init(&pg_81fc58, NULL, 0x13, &pg_821c38, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81fc58, NULL, -1, 8, 0, true, NULL, 0);
    Menu_AddList(&pg_81fc58, NULL, -1, 0xb, 0, true, NULL, 1);
    Menu_AddList(&pg_81fc58, NULL, -1, 4, 0, true, NULL, 2);
    Menu_AddAction(&pg_81fc58, NULL, 0x67, fe_4ee090, 3);
    Menu_AddBack(&pg_81fc58, NULL, 0x1b, NULL, 4);
    Menu_SetCallbacks(&pg_81fc58, fe_4edef0, fe_4ee170, fe_4e7ed0, NULL);
    Menu_Reset(&pg_81fc58);
}

/* 0x4f8670 */
static void build_4f8670(void)
{
    Menu_Init(&pg_81b338, NULL, 0x13, &pg_821c38, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81b338, NULL, -1, 8, 0, true, NULL, 0);
    Menu_AddList(&pg_81b338, NULL, -1, 0xb, 0, true, NULL, 1);
    Menu_AddList(&pg_81b338, NULL, -1, 0x39, 0, true, NULL, 2);
    Menu_AddAction(&pg_81b338, NULL, 0x67, fe_4ee600, 3);
    Menu_AddBack(&pg_81b338, NULL, 0x1b, NULL, 4);
    Menu_SetCallbacks(&pg_81b338, fe_4ee460, fe_4ee6e0, fe_4e8500, NULL);
    Menu_Reset(&pg_81b338);
}

/* 0x4f8730 */
static void build_4f8730(void)
{
    Menu_Init(&pg_81e998, NULL, 0x13, &pg_821c38, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81e998, NULL, -1, 8, 0, true, NULL, 0);
    Menu_AddList(&pg_81e998, NULL, -1, 10, 0, true, NULL, 1);
    Menu_AddAction(&pg_81e998, NULL, 0x67, fe_4ee9b0, 2);
    Menu_AddBack(&pg_81e998, NULL, 0x1b, NULL, 3);
    Menu_SetCallbacks(&pg_81e998, fe_4ee850, fe_4eec30, fe_4e8b60, NULL);
    Menu_Reset(&pg_81e998);
}

/* 0x4f87d0 */
static void build_4f87d0(void)
{
    Menu_Init(&pg_81d138, NULL, 0x13, &pg_821c38, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81d138, NULL, -1, 8, 0, true, NULL, 0);
    Menu_AddList(&pg_81d138, NULL, -1, 0x3d, 0, true, NULL, 1);
    Menu_AddAction(&pg_81d138, NULL, 0x67, fe_4eec40, 2);
    Menu_AddBack(&pg_81d138, NULL, 0x1b, NULL, 3);
    Menu_SetCallbacks(&pg_81d138, fe_4eeab0, fe_4eec30, fe_4e90f0, NULL);
    Menu_Reset(&pg_81d138);
}

/* 0x4f8870 */
static void build_4f8870(void)
{
    Menu_Init(&pg_81b158, NULL, 0x13, &pg_824c18, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_81b158, NULL, -1, fe_4eee60, -1);
    Menu_SetCallbacks(&pg_81b158, fe_4eed50, fe_4eed80, fe_4e9820, NULL);
    Menu_Reset(&pg_81b158);
}

/* 0x4f88e0 */
static void build_4f88e0(void)
{
    Menu_Init(&pg_81bab8, NULL, 0x13, &pg_821c38, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_81bab8, NULL, -1, 1, 0, true, fe_4eefb0, 0);
    Menu_AddAction(&pg_81bab8, NULL, 0x20, fe_4eefe0, 1);
    Menu_AddAction(&pg_81bab8, NULL, 0x81, fe_4ef000, 2);
    Menu_AddBack(&pg_81bab8, NULL, 0x1b, NULL, -1);
    Menu_SetCallbacks(&pg_81bab8, fe_4eef30, fe_4ef030, fe_4e9990, NULL);
    Menu_Reset(&pg_81bab8);
}

/* 0x4f9300 */
static void build_4f9300(void)
{

    Menu_Init(&pg_826140, NULL, -1, &pg_81d6d8, false, NULL, 1, 0, 0);
    Menu_AddBack(&pg_826140, NULL, -1, NULL, -1);
    Menu_SetCallbacks(&pg_826140, NULL, NULL, fe_4e96d0, NULL);
    Menu_Reset(&pg_826140);
}

/* 0x4f9370 */
static void build_4f9370(void)
{

    Menu_Init(&pg_8267e0, NULL, 0x58, &pg_81d6d8, false, NULL, 1, 1, 1);
    Menu_AddPage(&pg_8267e0, NULL, 0xc, &pg_826600, NULL, 0);
    Menu_AddPage(&pg_8267e0, NULL, 0xe2, &pg_8276e0, NULL, 0);
    Menu_AddAction(&pg_8267e0, NULL, 0x10, fe_4fa9b0, -1);
    Menu_SetCallbacks(&pg_8267e0, NULL, fe_4f3ae0, fe_4e2500, fe_4ef740);
    Menu_Reset(&pg_8267e0);
}

/* 0x4f9400 */
static void build_4f9400(int p1)
{

    Menu_Init(&pg_8269c0, NULL, 0x1c, &pg_826600, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_8269c0, NULL, 0x19f, fe_4fa910, -1);
    Menu_AddAction(&pg_8269c0, NULL, 0x1a0, fe_4fa910, -1);
    Menu_SetCallbacks(&pg_8269c0, fe_4fa890, fe_4f39f0, fe_4d5fb0, NULL);
    Menu_Reset(&pg_8269c0);
    pg_8269c0.sel = (int8_t)(p1 - 1);
}

/* 0x4f9490 */
static void build_4f9490(int p1)
{

    Menu_Init(&pg_8276e0, NULL, 0x1c, &pg_8267e0, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_8276e0, NULL, 0x19f, fe_4fa910, -1);
    Menu_AddAction(&pg_8276e0, NULL, 0x1a0, fe_4fa910, -1);
    Menu_SetCallbacks(&pg_8276e0, fe_4fa890, fe_4f9520, fe_4d5fb0, NULL);
    Menu_Reset(&pg_8276e0);
    pg_8276e0.items[0].value = (uint8_t)(p1 - 1);
}

/* 0x4f9530 */
static void build_4f9530(void)
{

    Menu_Init(&pg_826600, NULL, 0x1d, &pg_8267e0, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_826600, NULL, 0xd0, &pg_8241b8, fe_4ef930, 0);   /* 0x4f83a0 */
    Menu_AddPage(&pg_826600, NULL, 0xd1, &pg_8241b8, fe_4ef930, 0);   /* 0x4f83a0 */
    Menu_AddPage(&pg_826600, NULL, 0xd2, &pg_8241b8, fe_4ef930, 0);   /* 0x4f83a0 */
    Menu_SetCallbacks(&pg_826600, fe_4faa00, NULL, fe_4d6290, NULL);
    Menu_Reset(&pg_826600);
}

/* 0x4f95d0 */
static void build_4f95d0(void)
{
    Menu_Init(&pg_828220, NULL, 0x1d, &pg_8267e0, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_828220, NULL, 0xd0, &pg_827320, fe_4ef930, 0);
    Menu_AddPage(&pg_828220, NULL, 0xd1, &pg_827320, fe_4ef930, 0);
    Menu_AddPage(&pg_828220, NULL, 0xd2, &pg_827320, fe_4ef930, 0);
    Menu_SetCallbacks(&pg_828220, fe_4faa00, NULL, fe_4d6290, NULL);
    Menu_Reset(&pg_828220);
}

/* 0x4f9670 */
static void build_4f9670(void)
{
    Menu_Init(&pg_826f60, NULL, 0x33, &pg_8286e0, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_826f60, NULL, 0x98, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0x99, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0x9a, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0x9b, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0x9c, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0x9d, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0x9e, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0x9f, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa0, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa1, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa2, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa3, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa4, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa5, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa6, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa7, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa8, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xa9, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xaa, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xab, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xac, &pg_826d80, fe_4facd0, -1);
    Menu_AddPage(&pg_826f60, NULL, 0xad, &pg_826d80, fe_4facd0, -1);
    Menu_SetCallbacks(&pg_826f60, fe_4faa50, fe_4f39d0, fe_4d8950, NULL);
    Menu_Reset(&pg_826f60);
}

/* 0x4f9940 */
static void build_4f9940(void)
{
    Menu_Init(&pg_826d80, NULL, 0x97, &pg_826f60, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_826d80, NULL, 0x88, 2, 0, false, fe_4fad40, 0x88);
    Menu_SetCallbacks(&pg_826d80, fe_4fac70, fe_4f39d0, fe_4d8ed0, NULL);
    Menu_Reset(&pg_826d80);
}

/* 0x4f99b0 */
static void build_4f99b0(void)
{
    Menu_Init(&pg_828500, NULL, 0x33, &pg_827c80, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_828500, NULL, 0x98, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0x99, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0x9a, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0x9b, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0x9c, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0x9d, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0x9e, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0x9f, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa0, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa1, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa2, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa3, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa4, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa5, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa6, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa7, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa8, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xa9, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xaa, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xab, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xac, &pg_827140, fe_4fad90, -1);
    Menu_AddPage(&pg_828500, NULL, 0xad, &pg_827140, fe_4fad90, -1);
    Menu_SetCallbacks(&pg_828500, fe_4faa50, fe_4f39d0, fe_4d8950, NULL);
    Menu_Reset(&pg_828500);
}

/* 0x4f9c80 */
static void build_4f9c80(void)
{
    Menu_Init(&pg_827140, NULL, 0x97, &pg_828500, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_827140, NULL, 0x88, 2, 0, false, fe_4fadd0, 0x88);
    Menu_SetCallbacks(&pg_827140, fe_4fac70, fe_4f39d0, fe_4d8ed0, NULL);
    Menu_Reset(&pg_827140);
}

/* 0x4f9cf0 */
static void build_4f9cf0(void)
{
    Menu_Init(&pg_827e60, NULL, 0x33, &pg_8278c0, false, NULL, 1, 0, 0);
    Menu_AddPage(&pg_827e60, NULL, 0x98, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0x99, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0x9a, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0x9b, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0x9c, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0x9d, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0x9e, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0x9f, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa0, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa1, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa2, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa3, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa4, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa5, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa6, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa7, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa8, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xa9, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xaa, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xab, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xac, &pg_827500, fe_4fae70, -1);
    Menu_AddPage(&pg_827e60, NULL, 0xad, &pg_827500, fe_4fae70, -1);
    Menu_SetCallbacks(&pg_827e60, fe_4faa50, fe_4f39d0, fe_4d8950, NULL);
    Menu_Reset(&pg_827e60);
}

/* 0x4f9fc0 */
static void build_4f9fc0(void)
{
    Menu_Init(&pg_827500, NULL, 0x97, &pg_827e60, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_827500, NULL, 0x88, 2, 0, false, fe_4faea0, 0x88);
    Menu_SetCallbacks(&pg_827500, fe_4fac70, fe_4f39d0, fe_4d8ed0, NULL);
    Menu_Reset(&pg_827500);
}

/* 0x4fa030 */
static void build_4fa030(void)
{
    Menu_Init(&pg_826ba0, NULL, 0x26, &pg_826d80, false, NULL, 1, 0, 1);
    Menu_AddAction(&pg_826ba0, NULL, 0xe9, fe_4fafe0, 1);
    Menu_AddAction(&pg_826ba0, NULL, 0xe8, fe_4fafe0, 0);
    Menu_SetCallbacks(&pg_826ba0, fe_4faef0, fe_4fafd0, fe_4db850, NULL);
    Menu_Reset(&pg_826ba0);
}

/* 0x4fa0b0 */
static void build_4fa0b0(void)
{
    Menu_Init(&pg_827320, NULL, 0x25, &pg_828220, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_827320, NULL, 0xfa, fe_4fb370, -1);
    Menu_AddAction(&pg_827320, NULL, 0xfb, fe_4fb370, -1);
    Menu_AddAction(&pg_827320, NULL, 0xfc, fe_4fb370, -1);
    Menu_AddAction(&pg_827320, NULL, 0xfd, fe_4fb370, -1);
    Menu_AddAction(&pg_827320, NULL, 0xfe, fe_4fb370, -1);
    Menu_AddAction(&pg_827320, NULL, 0xff, fe_4fb370, -1);
    Menu_AddAction(&pg_827320, NULL, 0x100, fe_4fb370, -1);
    Menu_AddAction(&pg_827320, NULL, 0x101, fe_4fb370, -1);
    Menu_SetCallbacks(&pg_827320, fe_4fb010, fe_4fb360, fe_4dc710, NULL);
    Menu_Reset(&pg_827320);
}

/* 0x4fa1c0 */
static void build_4fa1c0(void)
{
    Menu_Init(&pg_826420, NULL, 0x25, &pg_827500, false, NULL, 1, 0, 0);
    Menu_AddAction(&pg_826420, NULL, 0xfa, fe_4fb370, -1);
    Menu_AddAction(&pg_826420, NULL, 0xfb, fe_4fb370, -1);
    Menu_AddAction(&pg_826420, NULL, 0xfc, fe_4fb370, -1);
    Menu_AddAction(&pg_826420, NULL, 0xfd, fe_4fb370, -1);
    Menu_AddAction(&pg_826420, NULL, 0xfe, fe_4fb370, -1);
    Menu_AddAction(&pg_826420, NULL, 0xff, fe_4fb370, -1);
    Menu_AddAction(&pg_826420, NULL, 0x100, fe_4fb370, -1);
    Menu_AddAction(&pg_826420, NULL, 0x101, fe_4fb370, -1);
    Menu_SetCallbacks(&pg_826420, fe_4fb010, fe_4fb360, fe_4dc710, NULL);
    Menu_Reset(&pg_826420);
}

/* 0x4fa370 */
static void build_4fa370(void)
{

    Menu_Init(&pg_828040, NULL, 0x1a7, &pg_827320, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_828040, NULL, -1, 10, 0, false, NULL, 0);
    Menu_AddList(&pg_828040, NULL, -1, 5, 0, false, NULL, 1);
    Menu_AddAction(&pg_828040, NULL, 0x67, fe_4f0090, -1);
    Menu_SetCallbacks(&pg_828040, fe_4f3220, fe_4fb360, fe_4da710, NULL);
    Menu_Reset(&pg_828040);
}

/* 0x4fa410 */
static void build_4fa410(void)
{

    Menu_Init(&pg_827aa0, NULL, 0x1a7, &pg_827320, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_827aa0, NULL, -1, 3, 0, false, NULL, 3);
    Menu_AddList(&pg_827aa0, NULL, -1, 5, 0, false, NULL, -1);
    Menu_AddList(&pg_827aa0, NULL, -1, 5, 0, false, NULL, 2);
    Menu_AddAction(&pg_827aa0, NULL, 0x67, fe_4f0110, -1);
    Menu_SetCallbacks(&pg_827aa0, fe_4f3280, fe_4f3310, fe_4daf90, NULL);
    Menu_Reset(&pg_827aa0);
}

/* 0x4fa540 */
static void build_4fa540(void)
{

    Menu_Init(&pg_828c80, NULL, 0x18, &pg_81f118, false, NULL, 1, 0, 1);
    Menu_AddPage(&pg_828c80, NULL, -1, &pg_829140, fe_4fbea0, 0);
    Menu_AddPage(&pg_828c80, NULL, -1, &pg_829140, fe_4fbea0, 0);
    Menu_AddBack(&pg_828c80, NULL, 0x67, NULL, 0);
    Menu_SetCallbacks(&pg_828c80, fe_4fba90, NULL, fe_4fccb0, NULL);
    Menu_Reset(&pg_828c80);
}

/* 0x4fa5d0 */
static void build_4fa5d0(void)
{

    Menu_Init(&pg_8288c0, NULL, 0x65, &pg_828c80, false, NULL, 1, 0, 1);
    for (int i = 0; i < 10; i++)
        Menu_AddAction(&pg_8288c0, NULL, i + 0x6b, fe_4fbec0, i);
    Menu_AddPage(&pg_8288c0, NULL, 0x67, &pg_828c80, NULL, 0);
    Menu_SetCallbacks(&pg_8288c0, fe_4fbf60, fe_4fc070, fe_4fd080, fe_4fbff0);
    Menu_Reset(&pg_8288c0);
}

/* 0x4fa650 */
static void build_4fa650(void)
{

    Menu_Init(&pg_828aa0, NULL, 0x75, &pg_8288c0, false, NULL, 1, 0, 1);
    for (int i = 0; i < 8; i++)
        Menu_AddAction(&pg_828aa0, NULL, -1, fe_4fc850, i);
    Menu_AddPage(&pg_828aa0, NULL, 0x67, &pg_828c80, NULL, 0);
    Menu_SetCallbacks(&pg_828aa0, fe_4fc500, fe_4fc620, fe_4fd480, NULL);
    Menu_Reset(&pg_828aa0);
}

/* 0x4fa6d0 */
static void build_4fa6d0(void)
{
    Menu_Init(&pg_828e60, NULL, 0x75, &pg_8288c0, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_828e60, NULL, 0x7a, 0xb, 0, false, NULL, 0);
    Menu_AddList(&pg_828e60, NULL, 0x7b, 0xb, 0, false, NULL, 1);
    Menu_AddList(&pg_828e60, NULL, 0x1a8, 2, 0, false, NULL, 2);
    Menu_AddPage(&pg_828e60, NULL, 0x67, &pg_828c80, NULL, 3);
    Menu_SetCallbacks(&pg_828e60, fe_4fc880, NULL, fe_4fdb10, fe_4fc8f0);
    Menu_Reset(&pg_828e60);
}

/* 0x4fa780 */
static void build_4fa780(void)
{

    Menu_Init(&pg_829140, NULL, 0x65, &pg_828c80, false, NULL, 1, 0, 1);
    Menu_AddList(&pg_829140, NULL, -1, (uint8_t)Input_DeviceCount(), 0, false, NULL, 0);   /* 0x49ef90 */
    Menu_AddList(&pg_829140, NULL, 0x68, 2, 0, false, NULL, 1);
    Menu_AddList(&pg_829140, NULL, 0x68, 2, 0, false, NULL, 2);
    Menu_AddList(&pg_829140, NULL, 0x6a, 2, 0, false, NULL, 3);
    Menu_AddList(&pg_829140, NULL, 0x20b, 2, 0, false, NULL, 4);
    Menu_AddList(&pg_829140, NULL, 0x20c, 2, 0, false, NULL, 5);
    Menu_AddPage(&pg_829140, NULL, 0x67, &pg_8288c0, NULL, 6);
    Menu_SetCallbacks(&pg_829140, fe_4fc9b0, fe_4fc9f0, fe_4fe240, fe_4fc970);
    Menu_Reset(&pg_829140);
}

/* 0x4fa4d0: the controller pages (0x40c050 between them, the input devices) */
void Fe_BuildControllers(void)
{
    build_4fa540();
    build_4fa5d0();
    build_4fa650();
    PENDING_NOTE(0x40c050, "the controller configuration");
    build_4fa780();
    build_4fa6d0();
}

/* 0x4d2790: the difficulties' names */
void Fe_DifficultyNames(void)
{
    g_fe_difficulty[0] = Text_Get(0xd0);
    g_fe_difficulty[1] = Text_Get(0xd1);
    g_fe_difficulty[2] = Text_Get(0xd2);
}

void Pages_Build(bool first, bool full)
{
    uint32_t mw = gm32(0x52afb4);
    uint32_t players = mw >> 13 & 0xf, mode2 = mw >> 10 & 7, bit17 = mw >> 17 & 1;
    if (mw >> 19 & 1) {
        PENDING_NOTE(0x4e9f70, "Pages_Build for a network game");
        return;
    }
    /* the first page: the languages the first time (not in Japan or Poland), else the main menu */
    Fe_SetPage(NULL);
    Fe_SetNextPage(first && gm32(0x52ea54) != 3 && gm32(0x52ea54) != 2 ? &pg_8221d8 : &pg_81d6d8);   /* 0x4f8290 */
    gw32(0x8189b8, Fe_Now());
    /* 0x4eaa30 is always true, so the three-minute check of 0x406450 is never made */
    Menu_Start();
    Menu_SoundsOn(false);
    Menu_SetSounds(-1, -1, -1, -1, -1);
    build_4f5670();
    gw32(0x818ac4, 0);
    build_4f5520();
    build_4f5580();
    uint32_t terr = gm32(0x52ea54);
    if (terr == 2 || terr == 3) {
        Text_Select(0);
        Fe_BuildControllers();
    } else if (!first) {
        /* back from the options: the language page returns there, at the current language */
        Text_Select((int)(mw & 7));
        Menu_SetParent(&pg_8221d8, &pg_81f118);
        for (int i = 0; i < 5; i++) pg_8221d8.items[i].target = &pg_81f118;
        Menu_SetSel(&pg_8221d8, (int8_t)(mw & 7));
    } else
        Text_Select(0);
    build_4f5770();
    build_4f5810();
    build_4f5850();
    build_4f58e0();
    build_4f5940();
    build_4f59a0();
    build_4f5a60();
    build_4f5ae0();
    build_4f5b50();
    build_4f9370();
    build_4f9400((int)players);
    build_4f9490((int)players);
    build_4f9530();
    build_4f95d0();
    build_4f9670();
    build_4f9940();
    build_4f99b0();
    build_4f9c80();
    build_4f9cf0();
    build_4f9fc0();
    build_4fa030();
    build_4fa0b0();
    build_4fa1c0();
    build_4f5b90();
    build_4f5c00();
    build_4f5c90();
    build_4f5d90();
    build_4f5e50();
    build_4f8500();
    build_4f85b0();
    build_4f8670();
    build_4f8730();
    build_4f87d0();
    build_4f9300();
    build_4f8870();
    build_4f88e0();
    build_4f5eb0();
    build_4f6040();
    build_4f6130();
    build_4f6300();
    build_4f6240();
    build_4f63b0();
    build_4f6420();
    build_4f6490();
    build_4f6540();
    build_4f66e0();
    build_4f6740();
    build_4f6830();
    build_4f6910();
    build_4f6990();
    build_4f6610();
    build_4f69f0();
    build_4f6a50();
    build_4f6b40();
    build_4f6c90();
    build_4f6d10();
    if (!first) Fe_BuildControllers();
    build_4f5fc0();
    build_4f6d70();
    build_4f6dd0();
    build_4f6e50((int)players);
    build_4f6f10((int)players);
    build_4f6fd0((int)players);
    build_4f7090((int)players);
    build_4f7150((int)players);
    build_4f7270();
    build_4f7310();
    build_4f73b0();
    build_4f7510((int)bit17);
    build_4f75b0((int)bit17);
    build_4f7650((int)bit17);
    build_4f76f0();
    build_4f77a0();
    build_4f7820();
    build_4f78c0();
    build_4f7970();
    build_4f7a30();
    build_4f7d00();
    build_4f7d70();
    build_4f7de0();
    build_4f7ed0();
    build_4f8020();
    build_4fa370();
    build_4fa410();
    build_4f7450();
    build_4f8170();
    build_4f8250();
    (void)mode2;
    if (!full) {
        /* back from a race: the pages at the mode's place */
        uint32_t m = mw >> 3 & 0x7f;
        if (m <= 4) {
            pg_823a38.sel = (int8_t)m;
            pg_81d6d8.sel = 1;
            Fe_SetScrollPos(1);
            pg_8267e0.sel = 1;
        } else if (m <= 7) {
            pg_8267e0.sel = (int8_t)(m - 5);
            pg_81d6d8.sel = 2;
            Fe_SetScrollPos(2);
            pg_823a38.sel = 2;
        } else if (m <= 12) {
            pg_81d6d8.sel = 3;
            Fe_SetScrollPos(3);
        }
        Fe_DifficultyNames();
        return;
    }
    /* the first time: rally, single stage, the second arcade item */
    pg_81d6d8.sel = 1;
    Fe_SetScrollPos(1);
    pg_823a38.sel = 2;
    pg_8267e0.sel = 1;
    Fe_DifficultyNames();
}
