// Souls of Harkinian: the Elden Ring style HUD (spec: souls/ER-SYSTEM.md section 3).
// Everything is drawn with OoT's own art: the Quest Status page stone, the magic meter sprites, the item icons, the
// heart piece sprites, the rupee icon and digits and the do-action words. The only generated art are a few small
// rounded frames, a soft band and a triangle mark, built in memory at run time (no image files).
#include "soh/Enhancements/controls/SoulsHud.h"
#include "soh/Enhancements/controls/SoulsLoadout.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <cstring>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "textures/parameter_static/parameter_static.h"
#include "textures/icon_item_static/icon_item_static.h"
#include "textures/icon_item_nes_static/icon_item_nes_static.h"
#include "textures/icon_item_24_static/icon_item_24_static.h"
#include "textures/do_action_static/do_action_static.h"
#include "soh_assets.h"
extern void* gItemIcons[];
extern const char* _gAmmoDigit0Tex[];
}
#include "soh/ShipUtils.h"
extern "C" {
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);
}


// The helpers are in a C linkage block so that OPEN_DISPS / CLOSE_DISPS find the C frame interpolation functions.
extern "C" {

// ---------------------------------------------------------------------------------------------------------------------
// Layout constants, N64 screen units (spec section 3)
const float kMargin = 12.0f;
const float kStoneW = 22.0f;
const float kStoneH = 22.0f * 120.0f / 130.0f;
const float kMeterScale = 0.68f;
const float kSlotH = 23.75f; // slots are portrait, 5:6: 19.7 wide, 23.75 tall, gap 3.5 (staggered cross), no border
const float kSlotW = 19.7f;
const float kSlotGap = 3.5f;

// ---------------------------------------------------------------------------------------------------------------------
// Generated art: IA16 textures (byte 0 = intensity, byte 1 = alpha)
alignas(16) static uint8_t sSlotFill[56 * 68 * 2];
alignas(16) static uint8_t sSlotInner[56 * 68 * 2];
alignas(16) static uint8_t sSlotHalo[56 * 64 * 2];
alignas(16) static uint8_t sStoneRing[64 * 64 * 2];
alignas(16) static uint8_t sStoneEdge[64 * 64 * 2];
alignas(16) static uint8_t sStoneInner[64 * 64 * 2];
alignas(16) static uint8_t sPromptFill[128 * 24 * 2];
alignas(16) static uint8_t sPromptRing[128 * 24 * 2];
alignas(16) static uint8_t sBand[64 * 4 * 2];
alignas(16) static uint8_t sTriangle[32 * 32 * 2];
static bool sGenerated = false;

static float Clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static float SdRoundBox(float x, float y, float hx, float hy, float r) {
    float qx = std::fabs(x) - (hx - r), qy = std::fabs(y) - (hy - r);
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
}

// Fills a texture from a coverage function f(x, y) with x, y = pixel centre relative to the texture centre.
static void Gen(uint8_t* d, int w, int h, const std::function<float(float, float)>& f) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            float c = Clamp01(f(i + 0.5f - w * 0.5f, j + 0.5f - h * 0.5f));
            d[(j * w + i) * 2] = 255;
            d[(j * w + i) * 2 + 1] = (uint8_t)(c * 255.0f + 0.5f);
        }
    }
}

// Coverage of the band sd in [lo, hi] (sd = signed distance in pixels, negative inside), anti-aliased over one pixel.
static float Band(float sd, float lo, float hi) {
    return Clamp01(0.5f - (sd - hi)) * Clamp01(0.5f + (sd - lo));
}

static float DistSeg(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = Clamp01((wx * vx + wy * vy) / (vx * vx + vy * vy));
    return std::hypot(px - (ax + vx * t), py - (ay + vy * t));
}

static void GenerateArt() {
    if (sGenerated) {
        return;
    }
    sGenerated = true;
    {   // slot 19 units on 64 px
        // panel: 19.7 x 23.75 units on 64 x 78 px, rounding 1.5; inner glow for the flash (no border line)
        const float ppu = 56.0f / kSlotW, hx = 28.0f, hy = 34.0f, r = 1.5f * ppu;
        Gen(sSlotFill, 56, 68, [&](float x, float y) { return Band(SdRoundBox(x, y, hx, hy, r), -1e9f, 0.0f); });
        Gen(sSlotInner, 56, 68, [&](float x, float y) {
            float sd = SdRoundBox(x, y, hx, hy, r);
            float t = sd > 0.0f ? 0.0f : Clamp01(1.0f + sd / (5.0f * ppu));
            return 0.55f * t * t; // soft warm glow inside the panel, no edge line
        });
    }
    {   // soft darker halo around the panel: panel + 4 units on every side (27.7 x 31.75 units on 64 x 74 px)
        const float ppu = 56.0f / (kSlotW + 8.0f), hx = kSlotW * 0.5f * ppu, hy = kSlotH * 0.5f * ppu, r = 1.5f * ppu;
        Gen(sSlotHalo, 56, 64, [&](float x, float y) {
            return 0.55f * Clamp01(0.5f - SdRoundBox(x, y, hx, hy, r) / (5.0f * ppu));
        });
    }
    {   // stone frame: white rim 0..0.8 and dark edge 0.8..1.6 outside the stone, 25.2 units on 64 px
        const float ppu = 64.0f / (kStoneW + 3.2f), h = kStoneW * 0.5f * ppu, r = 2.5f * ppu;
        Gen(sStoneRing, 64, 64, [&](float x, float y) { return Band(SdRoundBox(x, y, h, h, r), 0.0f, 0.8f * ppu); });
        Gen(sStoneEdge, 64, 64,
            [&](float x, float y) { return Band(SdRoundBox(x, y, h, h, r), 0.8f * ppu, 1.6f * ppu); });
    }
    {   // inner shadow of the stone
        const float ppu = 64.0f / kStoneW, h = 32.0f, r = 2.5f * ppu;
        Gen(sStoneInner, 64, 64, [&](float x, float y) {
            float sd = SdRoundBox(x, y, h, h, r);
            return sd > 0.0f ? 0.0f : 0.55f * Clamp01(1.0f + sd / (2.5f * ppu));
        });
    }
    {   // prompt box 80 x 15 units on 160 x 30 px
        const float ppu = 1.6f, hx = 64.0f, hy = 12.0f, r = 2.0f * ppu;
        Gen(sPromptFill, 128, 24, [&](float x, float y) { return Band(SdRoundBox(x, y, hx, hy, r), -1e9f, 0.0f); });
        Gen(sPromptRing, 128, 24,
            [&](float x, float y) { return Band(SdRoundBox(x, y, hx, hy, r), -0.6f * ppu, 0.0f); });
    }
    for (int j = 0; j < 4; j++) {   // rupee band: solid on the right 55 %, fades out to the left
        for (int i = 0; i < 64; i++) {
            float u = (i + 0.5f) / 64.0f;
            sBand[(j * 64 + i) * 2] = 255;
            sBand[(j * 64 + i) * 2 + 1] = (uint8_t)(Clamp01(u / 0.45f) * 255.0f);
        }
    }
    {   // triangle outline, 10 units on 32 px: M5 1.6 L8.6 8 H1.4 Z, stroke 1.3
        const float ppu = 3.2f;
        Gen(sTriangle, 32, 32, [&](float x, float y) {
            float ux = x / ppu + 5.0f, uy = y / ppu + 5.0f;
            float d = std::min({ DistSeg(ux, uy, 5.0f, 1.6f, 8.6f, 8.0f), DistSeg(ux, uy, 8.6f, 8.0f, 1.4f, 8.0f),
                                 DistSeg(ux, uy, 1.4f, 8.0f, 5.0f, 1.6f) });
            return 0.5f - (d - 0.65f) * ppu;
        });
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Drawing helpers (all in the overlay display list)
static int sAlpha = 255; // the game's HUD alpha (fades in cutscenes)

static int A(int v) {
    return v * sAlpha / 255;
}

#define RECT(x, y, w, h, s, t, dsdx, dtdy)                                                                          \
    gSPWideTextureRectangle(OVERLAY_DISP++, (int)((x)*4.0f), (int)((y)*4.0f), (int)(((x) + (w)) * 4.0f),        \
                            (int)(((y) + (h)) * 4.0f), G_TX_RENDERTILE, (s), (t), (int)(dsdx), (int)(dtdy))

// IA16 generated texture, stretched over the rectangle.
static void DrawGen(PlayState* play, uint8_t* tex, int tw, int th, float x, float y, float w, float h, int r, int g, int b,
             int a) {
    if (a <= 0) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, a);
    gDPLoadTextureBlock(OVERLAY_DISP++, tex, G_IM_FMT_IA, G_IM_SIZ_16b, tw, th, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    RECT(x, y, w, h, 0, 0, 1024.0f * tw / w, 1024.0f * th / h);
    CLOSE_DISPS(play->state.gfxCtx);
}

// RGBA32 game icon (item icons are 32 x 32, quest icons 24 x 24).
static void DrawIcon(PlayState* play, const void* tex, int tw, float x, float y, float w, float h, int r, int g, int b,
              int a) {
    if (a <= 0 || tex == nullptr) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, a);
    gDPLoadTextureBlock(OVERLAY_DISP++, tex, G_IM_FMT_RGBA, G_IM_SIZ_32b, tw, tw, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    RECT(x, y, w, h, 0, 0, 1024.0f * tw / w, 1024.0f * tw / h);
    CLOSE_DISPS(play->state.gfxCtx);
}

// Text in the game's message font (the routine of dialogue and the creator: 16 x 16 I4 characters from
// Ship_GetCharFontTexture, advance from Ship_GetCharFontWidth), drawn into the overlay with a small drop shadow.
static const float kFontBase = 0.75f; // the message font's usual character scale (R_TEXT_CHAR_SCALE 75)

static float TextWidth(const char* str, float sc) {
    float w = 0.0f;
    for (const char* c = str; *c; c++) {
        w += Ship_GetCharFontWidth((u8)*c) * kFontBase * sc;
    }
    return w;
}

static void DrawText(PlayState* play, const char* str, float x, float y, float sc, int r, int g, int b, int a) {
    const float size = 16.0f * kFontBase * sc;
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineLERP(OVERLAY_DISP++, 0, 0, 0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, PRIMITIVE, TEXEL0, 0,
                      PRIMITIVE, 0);
    for (const char* c = str; *c; c++) {
        if (*c != ' ') {
            gDPLoadTextureBlock_4b(OVERLAY_DISP++, Ship_GetCharFontTexture((u8)*c), G_IM_FMT_I, 16, 16, 0,
                                   G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                                   G_TX_NOLOD, G_TX_NOLOD);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 0, 0, 0, a * 3 / 4); // drop shadow
            RECT(x + 0.5f, y + 0.5f, size, size, 0, 0, 1024.0f * 16.0f / size, 1024.0f * 16.0f / size);
            gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, a);
            RECT(x, y, size, size, 0, 0, 1024.0f * 16.0f / size, 1024.0f * 16.0f / size);
        }
        x += Ship_GetCharFontWidth((u8)*c) * kFontBase * sc;
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// One digit of the ammo font (8 x 8 IA8) at size d.
static void DrawAmmoDigit(PlayState* play, int digit, float x, float y, float d, int r, int g, int b, int a) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 0);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, a);
    gDPLoadTextureBlock(OVERLAY_DISP++, _gAmmoDigit0Tex[digit], G_IM_FMT_IA, G_IM_SIZ_8b, 8, 8, 0,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    RECT(x, y, d, d, 0, 0, 1024.0f * 8.0f / d, 1024.0f * 8.0f / d);
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---------------------------------------------------------------------------------------------------------------------
// HUD state (all per frame of Interface_Draw)
struct SoulsTrail {
    float last = -1.0f, trail = -1.0f;
    int hold = 0;
};
static SoulsTrail sHp, sMp;
static int sPrevPieces = -1;
static int sPieceFlash = 0;
static int sPrevCapacity = 0;

// The part just lost stays for a moment, then drains. Returns the trail fraction.
static float UpdateSoulsTrail(SoulsTrail& t, float cur, float drain) {
    if (t.last < 0.0f) {
        t.last = t.trail = cur;
    }
    if (cur < t.last - 0.0005f) {
        t.hold = 0; // new loss: hold the old end for a moment
    } else if (cur > t.trail) {
        t.trail = cur;
    }
    if (cur < t.trail) {
        if (t.hold < 14) {
            t.hold++;
        } else {
            t.trail = std::max(cur, t.trail - drain);
        }
    }
    t.last = cur;
    return t.trail;
}

// Draws one meter with OoT's own magic meter sprites at scale 0.68. len = inner length in units.
static void DrawMeter(PlayState* play, float x, float y, float len, float frac, float trail, int r, int g, int b) {
    const float ms = kMeterScale, e = 8.0f * ms, h = 16.0f * ms, ds = 1024.0f / ms;
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 255, 255, 255, A(255));
    gDPLoadTextureBlock(OVERLAY_DISP++, gMagicMeterEndTex, G_IM_FMT_IA, G_IM_SIZ_8b, 8, 16, 0,
                        G_TX_MIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 3, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    RECT(x, y, e, h, 0, 0, ds, ds);
    RECT(x + e + len, y, e, h, 256, 0, ds, ds); // mirrored end cap
    gDPLoadTextureBlock(OVERLAY_DISP++, gMagicMeterMidTex, G_IM_FMT_IA, G_IM_SIZ_8b, 24, 16, 0,
                        G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                        G_TX_NOLOD);
    RECT(x + e, y, len, h, 0, 0, ds, ds);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, PRIMITIVE);
    gDPSetEnvColor(OVERLAY_DISP++, 0, 0, 0, 255);
    gDPLoadMultiBlock_4b(OVERLAY_DISP++, gMagicMeterFillTex, 0, G_TX_RENDERTILE, G_IM_FMT_I, 16, 16, 0,
                         G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
                         G_TX_NOLOD);
    if (trail > frac) {
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 250, 250, 0, A(255)); // the part just lost
        RECT(x + e, y + 3.0f * ms, len * trail, 7.0f * ms, 0, 0, ds, ds);
    }
    if (frac > 0.0f) {
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, A(255));
        RECT(x + e, y + 3.0f * ms, len * frac, 7.0f * ms, 0, 0, ds, ds);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

// The carved stone heart: a patch of the Quest Status page background (tiles 80 x 32 IA8, 3 columns x 5 rows from
// x -120, y 80). The stone is the region around the heart hollow, x -58.5..-0.5, y 62.7..9.1 on the page.
static void DrawStone(PlayState* play, float px, float py) {
    static const char* const kTiles[3][3] = {
        { gPauseQuestStatus00Tex, gPauseQuestStatus01Tex, gPauseQuestStatus02Tex },
        { gPauseQuestStatus10ENGTex, gPauseQuestStatus11Tex, gPauseQuestStatus12Tex },
        { nullptr, nullptr, nullptr },
    };
    const float rx0 = -58.5f, rx1 = -0.5f, ry0 = 62.7f, ry1 = 9.1f; // page units, y up
    const float kx = kStoneW / (rx1 - rx0), ky = kStoneH / (ry0 - ry1);
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, 132, 130, 82, A(255));
    for (int c = 0; c < 2; c++) {
        for (int r = 0; r < 3; r++) {
            const char* tex = kTiles[c][r];
            float tx0 = -120.0f + 80.0f * c, ty0 = 80.0f - 32.0f * r; // tile left / top on the page
            float ix0 = std::max(rx0, tx0), ix1 = std::min(rx1, tx0 + 80.0f);
            float iy0 = std::min(ry0, ty0), iy1 = std::max(ry1, ty0 - 32.0f);
            if (ix1 <= ix0 || iy0 <= iy1 || tex == nullptr) {
                continue;
            }
            float sx = px + (ix0 - rx0) * kx, sy = py + (ry0 - iy0) * ky;
            float sw = (ix1 - ix0) * kx, sh = (iy0 - iy1) * ky;
            int s = (int)((ix0 - tx0) * 32.0f), t = (int)((ty0 - iy0) * 32.0f);
            gDPLoadTextureBlock(OVERLAY_DISP++, tex, G_IM_FMT_IA, G_IM_SIZ_8b, 80, 32, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                                G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
            RECT(sx, sy, sw, sh, s, t, 1024.0f / kx, 1024.0f / ky);
        }
    }
    CLOSE_DISPS(play->state.gfxCtx);
    DrawGen(play, sStoneInner, 64, 64, px, py, kStoneW, kStoneH, 0, 0, 0, A(255));
    DrawGen(play, sStoneEdge, 64, 64, px - 1.6f, py - 1.6f, kStoneW + 3.2f, kStoneH + 3.2f, 6, 8, 14, A(140));
    DrawGen(play, sStoneRing, 64, 64, px - 1.6f, py - 1.6f, kStoneW + 3.2f, kStoneH + 3.2f, 255, 255, 255, A(235));
}

// A heart piece sprite (48 x 48 IA8), tinted heart red, optionally mirrored.
static void DrawPiece(PlayState* play, const char* tex, float x, float y, float w, bool mirror, int r, int g, int b, int a) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(OVERLAY_DISP++);
    gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, r, g, b, a);
    gDPLoadTextureBlock(OVERLAY_DISP++, tex, G_IM_FMT_IA, G_IM_SIZ_8b, 48, 48, 0, G_TX_NOMIRROR | G_TX_CLAMP,
                        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    float ds = 1024.0f * 48.0f / w;
    if (mirror) {
        RECT(x, y, w, w, (48 << 5) - 1, 0, -ds, ds);
    } else {
        RECT(x, y, w, w, 0, 0, ds, ds);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static void DrawHeartPieces(PlayState* play, float px, float py) {
    int pieces = (gSaveContext.inventory.questItems >> 28) & 0xF;
    if (sPrevPieces >= 0 && pieces < sPrevPieces && sPrevPieces == 3) {
        sPieceFlash = 18; // the 4th piece: the whole heart lights up, then the hollow empties
    }
    sPrevPieces = pieces;
    const char* tex[3] = { gHeartPieceIcon1Tex, gHeartPieceIcon2Tex, gHeartPieceIcon3Tex };
    float rx = px + kStoneW * 10.0f / 130.0f, ry = py + kStoneH * 10.5f / 120.0f, rw = kStoneW * 107.5f / 130.0f;
    if (sPieceFlash > 0) {
        sPieceFlash--;
        DrawPiece(play, tex[2], rx - 1.0f, ry - 1.0f, rw + 2.0f, false, 255, 150, 120, A(110));
        DrawPiece(play, tex[0], rx - 1.0f, ry - 1.0f, rw + 2.0f, true, 255, 150, 120, A(110));
        DrawPiece(play, tex[2], rx, ry, rw, false, 255, 70, 50, A(255));
        DrawPiece(play, tex[0], rx, ry, rw, true, 255, 70, 50, A(255));
    } else if (pieces > 0 && pieces <= 3) {
        DrawPiece(play, tex[pieces - 1], rx, ry, rw, false, 255, 70, 50, A(255));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Items

static const void* IconFor(uint8_t item, int* tw) {
    *tw = 32;
    switch (item) {
        case SOULS_ITEM_CATALYST:
            *tw = 24;
            return gQuestIconMagicJarSmallTex;
        case SOULS_ITEM_TOGGLE_IRON:
            return gItemIcons[ITEM_BOOTS_IRON];
        case SOULS_ITEM_TOGGLE_HOVER:
            return gItemIcons[ITEM_BOOTS_HOVER];
        case SOULS_ITEM_EPONA:
            return gItemIcons[ITEM_OCARINA_TIME];
        default:
            break;
    }
    if (item >= 0xF0 || gItemIcons[item] == nullptr || ((const char*)gItemIcons[item])[0] == 0) {
        return nullptr;
    }
    return gItemIcons[item];
}

// Ammo of an item, as the C buttons show it; -1 when the item has no count. *full: at capacity (green digits).
static int AmmoFor(uint8_t item, bool* full) {
    *full = false;
    switch (item) {
        case ITEM_STICK:
            *full = AMMO(ITEM_STICK) == CUR_CAPACITY(UPG_STICKS);
            return AMMO(ITEM_STICK);
        case ITEM_NUT:
            *full = AMMO(ITEM_NUT) == CUR_CAPACITY(UPG_NUTS);
            return AMMO(ITEM_NUT);
        case ITEM_BOMB:
            *full = AMMO(ITEM_BOMB) == CUR_CAPACITY(UPG_BOMB_BAG);
            return AMMO(ITEM_BOMB);
        case ITEM_BOW:
        case ITEM_BOW_ARROW_FIRE:
        case ITEM_BOW_ARROW_ICE:
        case ITEM_BOW_ARROW_LIGHT:
            *full = AMMO(ITEM_BOW) == CUR_CAPACITY(UPG_QUIVER);
            return AMMO(ITEM_BOW);
        case ITEM_SLINGSHOT:
            *full = AMMO(ITEM_SLINGSHOT) == CUR_CAPACITY(UPG_BULLET_BAG);
            return AMMO(ITEM_SLINGSHOT);
        case ITEM_BOMBCHU:
            *full = AMMO(ITEM_BOMBCHU) == 50;
            return AMMO(ITEM_BOMBCHU);
        case ITEM_BEAN:
            *full = AMMO(ITEM_BEAN) == 15;
            return AMMO(ITEM_BEAN);
        default:
            return -1;
    }
}

static bool Flashing(PlayState* play, SoulsLoadoutKind kind) {
    uint32_t now = play->gameplayFrames;
    uint32_t use = SoulsLoadout_LastUseFrame(kind), sw = SoulsLoadout_LastSwitchFrame(kind);
    return (use != 0 && now >= use && now - use < 5) || (sw != 0 && now >= sw && now - sw < 5);
}

// One slot, w x h units: a soft dark panel with a darker halo, no border. The flash darkens the panel and adds a soft warm
// glow inside it. Icon at 84 % of the smaller side, centred; ammo digits in the bottom right corner.
static void DrawSlot(PlayState* play, float x, float y, float w, float h, uint8_t item, bool dim, bool flash,
                     bool showAmmo = true) {
    DrawGen(play, sSlotHalo, 56, 64, x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f, 0, 0, 0, A(255));
    DrawGen(play, sSlotFill, 56, 68, x, y, w, h, 8, 10, 16, A(flash ? 215 : 140));
    if (flash) {
        DrawGen(play, sSlotInner, 56, 68, x, y, w, h, 255, 205, 120, A(255));
    }
    if (item != ITEM_NONE) {
        int tw;
        const void* icon = IconFor(item, &tw);
        bool full;
        int ammo = AmmoFor(item, &full);
        int ia = (dim || ammo == 0) ? 115 : 255;
        float is = std::min(w, h) * 0.84f;
        DrawIcon(play, icon, tw, x + (w - is) / 2, y + (h - is) / 2, is, is, 255, 255, 255, A(ia));
        if (ammo >= 0 && showAmmo) {
            const float d = w < 12.0f ? 2.6f : 3.6f;
            char buf[8];
            int n = snprintf(buf, sizeof(buf), "%d", std::min(ammo, 99));
            int r = 255, g = 255, b = 255;
            if (full) {
                r = 120, g = 255, b = 0;
            } else if (ammo == 0) {
                r = g = b = 100;
            }
            for (int i = 0; i < n; i++) {
                DrawAmmoDigit(play, buf[i] - '0', x + w - 1.2f - (n - i) * d * 0.75f - d * 0.25f, y + h - d - 1.2f, d, r, g, b, A(255));
            }
        }
    }
}

// The item after the current one in a list (next filled slot), or ITEM_NONE when the list has less than 2 items.
static uint8_t NextItem(SoulsLoadoutKind kind) {
    int cap = SoulsLoadout_Capacity(kind), cur = SoulsLoadout_CurrentSlot(kind);
    if (SoulsLoadout_Count(kind) < 2 || cur < 0) {
        return ITEM_NONE;
    }
    for (int k = 1; k < cap; k++) {
        uint8_t it = SoulsLoadout_Get(kind, (cur + k) % cap);
        if (it != ITEM_NONE) {
            return it;
        }
    }
    return ITEM_NONE;
}

// The cross: top (spell) and bottom (quick item) in the middle column, left and right hand straddling the gap.
static void DrawCross(PlayState* play, float left, float bottom, bool testFlash) {
    const float sw = kSlotW, sh = kSlotH, g = kSlotGap;
    const float colL = left, colM = left + sw + g, colR = colM + sw + g;
    const float yBot = bottom - sh, yTop = yBot - g - sh, ySide = yTop + (sh + g) / 2;
    if (SoulsLoadout_PouchHeld()) {
        const float px[4] = { colM, colL, colR, colM }, py[4] = { yTop, ySide, ySide, yBot };
        for (int i = 0; i < 4; i++) {
            DrawSlot(play, px[i], py[i], sw, sh, SoulsLoadout_Get(SL_POUCH, i), false, Flashing(play, SL_POUCH), false);
        }
        return;
    }
    bool spellOn = SoulsLoadout_HasCatalyst();
    DrawSlot(play, colM, yTop, sw, sh, SoulsLoadout_Current(SL_SPELL), !spellOn, Flashing(play, SL_SPELL));
    DrawSlot(play, colL, ySide, sw, sh, SoulsLoadout_Current(SL_LEFT), false, Flashing(play, SL_LEFT));
    DrawSlot(play, colR, ySide, sw, sh, SoulsLoadout_Current(SL_RIGHT), false, testFlash || Flashing(play, SL_RIGHT));
    DrawSlot(play, colM, yBot, sw, sh, SoulsLoadout_Current(SL_QUICK), false, Flashing(play, SL_QUICK));
    // previews of the next spell and the next quick item, 80 % opacity
    const float ps = sh * 0.45f, pw = ps * 0.83f, px = colM + sw + g * 0.6f;
    int saved = sAlpha;
    sAlpha = saved * 80 / 100;
    uint8_t ns = NextItem(SL_SPELL), nq = NextItem(SL_QUICK);
    if (ns != ITEM_NONE) {
        DrawSlot(play, px, yTop, pw, ps, ns, !spellOn, false, false);
    }
    if (nq != ITEM_NONE) {
        DrawSlot(play, px, yBot + sh - ps, pw, ps, nq, false, false, false);
    }
    sAlpha = saved;
}

// ---------------------------------------------------------------------------------------------------------------------
// Rupees, quest row, prompt

static void DrawRupees(PlayState* play, float right, float bottom) {
    static const Color_RGB8 kWallet[4] = { { 90, 235, 70 }, { 0x82, 0x82, 0xFF }, { 0xFF, 0x64, 0x64 },
                                           { 0xFF, 0x5A, 0xFF } };
    Color_RGB8 col = kWallet[0];
    if (CVarGetInteger(CVAR_ENHANCEMENT("DynamicWalletIcon"), 0)) {
        col = kWallet[std::min(3, (int)CUR_UPG_VALUE(UPG_WALLET))];
    }
    DrawGen(play, sBand, 64, 4, right - 96.0f, bottom - 11.0f, 96.0f, 11.0f, 0, 0, 0, A(158));
    // rupee icon: IA8 16 x 16
    {
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(OVERLAY_DISP++);
        gDPSetCombineMode(OVERLAY_DISP++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
        gDPSetPrimColor(OVERLAY_DISP++, 0, 0, col.r, col.g, col.b, A(255));
        gDPLoadTextureBlock(OVERLAY_DISP++, gRupeeCounterIconTex, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK,
                            G_TX_NOLOD, G_TX_NOLOD);
        RECT(right - 30.9f, bottom - 10.9f, 10.2f, 10.2f, 0, 0, 1024.0f * 16.0f / 10.2f, 1024.0f * 16.0f / 10.2f);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    // number: the message font, upright white, right aligned
    int rupees = gSaveContext.rupees;
    if (rupees < 0 || rupees > 9999) {
        rupees = 0;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", rupees);
    const float sc = 0.85f;
    int pr = 255, pg = 255, pb = 255;
    if (rupees == CUR_CAPACITY(UPG_WALLET)) {
        pr = 120, pg = 255, pb = 0;
    } else if (rupees == 0) {
        pr = pg = pb = 160;
    }
    DrawText(play, buf, right - 2.0f - TextWidth(buf, sc), bottom - 10.6f, sc, pr, pg, pb, A(255));
}

struct SoulsQuestIcon {
    const char* tex;
    int bit;
    int insetTop, insetRight; // transparent border of the 24 px icon (the medallions also have 1 left and 1 bottom)
};

// The row sits on the HUD margin by its visible art: top edge at the margin, rightmost right edge at the margin.
// TODO: record the real collection order (the loadout save section can store it); for now a fixed order.
static void DrawQuestRow(PlayState* play, float right, float top) {
    static const SoulsQuestIcon kStones[] = { { gQuestIconKokiriEmeraldTex, QUEST_KOKIRI_EMERALD, 0, 1 },
                                         { gQuestIconGoronRubyTex, QUEST_GORON_RUBY, 0, 1 },
                                         { gQuestIconZoraSapphireTex, QUEST_ZORA_SAPPHIRE, 1, 0 } };
    static const SoulsQuestIcon kMedals[] = { { gQuestIconMedallionLightTex, QUEST_MEDALLION_LIGHT, 1, 1 },
                                         { gQuestIconMedallionForestTex, QUEST_MEDALLION_FOREST, 1, 1 },
                                         { gQuestIconMedallionFireTex, QUEST_MEDALLION_FIRE, 1, 1 },
                                         { gQuestIconMedallionWaterTex, QUEST_MEDALLION_WATER, 1, 1 },
                                         { gQuestIconMedallionShadowTex, QUEST_MEDALLION_SHADOW, 1, 1 },
                                         { gQuestIconMedallionSpiritTex, QUEST_MEDALLION_SPIRIT, 1, 1 } };
    const bool adult = LINK_IS_ADULT;
    const SoulsQuestIcon* list = adult ? kMedals : kStones;
    const int count = adult ? 6 : 3;
    const float sz = 11.25f, gap = 2.25f, px = sz / 24.0f;
    float boxRight = 0.0f;
    bool first = true;
    for (int i = 0; i < count; i++) {
        if (!CHECK_QUEST_ITEM(list[i].bit)) {
            continue;
        }
        if (first) {
            boxRight = right + list[i].insetRight * px; // the first item's visible art ends at the margin
            first = false;
        }
        float x = boxRight - sz, y = top - list[i].insetTop * px;
        boxRight = x - gap;
        // the same soft drop shadow for every icon
        DrawIcon(play, list[i].tex, 24, x, y + 0.6f, sz, sz, 0, 0, 0, A(150));
        if (adult) {
            // the stone icons have a hard black shadow in their texture; medallions get two black copies under the
            // icon: 100 % at 1.8 / 1.6 icon pixels right / down, 90 % at 1.0 / 1.0
            const float off[2][3] = { { 1.8f, 1.6f, 255.0f }, { 1.0f, 1.0f, 230.0f } };
            for (int k = 0; k < 2; k++) {
                float ox = off[k][0] * px, oy = off[k][1] * px;
                DrawIcon(play, list[i].tex, 24, x + ox, y + oy, sz, sz, 0, 0, 0, A((int)off[k][2]));
            }
        }
        DrawIcon(play, list[i].tex, 24, x, y, sz, sz, 255, 255, 255, A(255));
    }
}

static void DrawPrompt(PlayState* play, const char* word) {
    const float w = 80.0f, h = 15.0f, x = 160.0f - w / 2, y = 196.0f, sc = 0.93f;
    DrawGen(play, sPromptFill, 128, 24, x, y, w, h, 8, 10, 16, A(158));
    DrawGen(play, sPromptRing, 128, 24, x, y, w, h, 232, 214, 160, A(128));
    DrawGen(play, sTriangle, 32, 32, x + 6.5f, y + 2.5f, 10.0f, 10.0f, 120, 220, 200, A(255));
    DrawText(play, word, x + 20.0f, y + 3.0f, sc, 255, 255, 255, A(255));
}

// The words OoT's A button shows (the do-action labels, in the order of the DO_ACTION_ ids); nullptr = no label.
static const char* ActionWord(PlayState* play) {
    static const char* const kWords[] = { "Attack", "Check", "Enter", "Return", "Open", "Jump", "Decide", "Dive",
                                          "Faster", "Throw", nullptr, "Climb", "Drop", "Down", "Save", "Speak",
                                          "Next", "Grab", "Stop", "Put Away", "Reel" };
    InterfaceContext* ic = &play->interfaceCtx;
    if (ic->aAlpha < 40 || play->pauseCtx.state != 0 || play->pauseCtx.debugState != 0) {
        return nullptr; // faded out, or the game is paused: no prompt
    }
    if (ic->unk_1F0 >= sizeof(kWords) / sizeof(kWords[0])) {
        return nullptr;
    }
    return kWords[ic->unk_1F0];
}

// ---------------------------------------------------------------------------------------------------------------------
// Developer test helper: CVar gDeveloperTools.SoulsHudTestFill fills the loadout so the HUD can be looked at.
//   1 = filled, spell dimmed; 2 = catalyst in the left hand (spell active); 3 = pouch view; 4 = right slot flash;
//   5 = filled + the "Check" prompt.
static int sTestMode = 0;

static void TestFill(int mode) {
    const bool adult = LINK_IS_ADULT;
    const uint8_t right[] = { adult ? (uint8_t)ITEM_SWORD_MASTER : (uint8_t)ITEM_SWORD_KOKIRI, ITEM_STICK };
    const uint8_t left[] = { adult ? (uint8_t)ITEM_BOW : (uint8_t)ITEM_SLINGSHOT, ITEM_BOOMERANG }; // tools, no shield
    for (int k = 0; k < SL_KIND_COUNT; k++) {
        for (int i = 0; i < SoulsLoadout_Capacity((SoulsLoadoutKind)k); i++) {
            SoulsLoadout_Set((SoulsLoadoutKind)k, i, ITEM_NONE);
        }
    }
    for (int i = 0; i < 2; i++) {
        SoulsLoadout_Set(SL_RIGHT, i, right[i]);
        SoulsLoadout_Set(SL_LEFT, i, left[i]);
    }
    if (mode == 2) {
        SoulsLoadout_Set(SL_LEFT, 0, SOULS_ITEM_CATALYST);
    }
    const uint8_t spells[] = { ITEM_DINS_FIRE, ITEM_FARORES_WIND, ITEM_NAYRUS_LOVE };
    for (int i = 0; i < 3; i++) {
        SoulsLoadout_Set(SL_SPELL, i, spells[i]);
    }
    const uint8_t quick[] = { ITEM_NUT, ITEM_BOMB, ITEM_POTION_RED };
    for (int i = 0; i < 3; i++) {
        SoulsLoadout_Set(SL_QUICK, i, quick[i]);
    }
    const uint8_t pouch[] = { ITEM_OCARINA_TIME, ITEM_LENS, SOULS_ITEM_TOGGLE_IRON, ITEM_BEAN };
    for (int i = 0; i < 4; i++) {
        SoulsLoadout_Set(SL_POUCH, i, pouch[i]);
    }
}


// ---------------------------------------------------------------------------------------------------------------------

extern "C" void SoulsHud_Draw(PlayState* play) {
    GenerateArt();
    int mode = CVarGetInteger("gDeveloperTools.SoulsHudTestFill", 0); // test helper, see above
    if (mode != sTestMode) {
        sTestMode = mode;
        if (mode > 0) {
            TestFill(mode);
        }
    }
    if (mode == 3) {
        SoulsLoadout_SetPouchHeld(true);
    }

    sAlpha = play->interfaceCtx.magicAlpha;
    const float left = OTRGetDimensionFromLeftEdge(kMargin);
    const float right = OTRGetDimensionFromRightEdge(320.0f - kMargin);
    const float bottom = 240.0f - kMargin;

    Gfx_SetupDL_39Overlay(play->state.gfxCtx);
    {
        OPEN_DISPS(play->state.gfxCtx);
        gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
        CLOSE_DISPS(play->state.gfxCtx);
    }

    // top left: stone heart, HP and magic meters
    const float py = kMargin;
    DrawStone(play, left, py);
    DrawHeartPieces(play, left, py);

    int capacity = gSaveContext.healthCapacity;
    if (sPieceFlash > 0 && capacity >= 16) {
        capacity -= 16; // the new heart appears when the flash ends
    }
    sPrevCapacity = capacity;
    float hpFrac = capacity > 0 ? Clamp01((float)gSaveContext.health / (float)capacity) : 0.0f;
    float hpTrail = UpdateSoulsTrail(sHp, hpFrac, 0.02f);
    const float bx = left + kStoneW + 6.0f;
    const float vis = 13.0f * kMeterScale, gapM = 1.2f;
    const float by = py + kStoneH / 2 - (vis * 2 + gapM) / 2;
    DrawMeter(play, bx, by, 14.0f + 7.0f * (capacity / 16), hpFrac, hpTrail, 255, 70, 50);
    if (gSaveContext.magicLevel != 0) {
        float cap = (float)std::max<int>(1, gSaveContext.magicCapacity);
        float mpFrac = Clamp01(gSaveContext.magic / cap);
        float mpTrail = UpdateSoulsTrail(sMp, mpFrac, 0.008f);
        DrawMeter(play, bx, by + vis + gapM, cap, mpFrac, mpTrail, 0, 200, 0);
    }

    // top right: collected spiritual stones (child) or medallions (adult)
    DrawQuestRow(play, right, py);

    // bottom left: the cross, bottom right: rupees
    DrawCross(play, left, bottom, mode == 4);
    DrawRupees(play, right, bottom);

    // action prompt near the bottom centre
    const char* word = ActionWord(play);
    if (mode == 5 && play->pauseCtx.state == 0) {
        word = "Check";
    }
    if (word != nullptr) {
        DrawPrompt(play, word);
    }

    Gfx_SetupDL_39Overlay(play->state.gfxCtx); // leave the display list state as Interface_Draw expects it
}

extern "C" void SoulsHud_MinimapShift(int origX, int origY, int w, int h, int* dx, int* dy) {
    *dx = *dy = 0;
    if (!SoulsLoadout_IsEnabled()) {
        return;
    }
    // right edge at the HUD margin, bottom edge 2 units above the rupee band (11 high at the same margin)
    *dx = (int)(320.0f - kMargin - w) - origX;
    *dy = (int)(240.0f - kMargin - 11.0f - 2.0f - h) - origY;
}

} // extern "C"
