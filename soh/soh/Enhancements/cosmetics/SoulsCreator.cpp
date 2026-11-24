// Souls of Harkinian: character creator in the file select ("Creator v2").
//
// With gEnhancements.SoulsCreator on, choosing Vanilla or Master Quest for a new file opens this creator before the
// file is made. The layout follows the approved web prototype: a "Detailed Look" list on the left (Name, Base Model,
// Hair, Tunics, ... Path, Randomize Look, Done) with submenus, a color picker, a description box and the live Link
// preview on the right. It is drawn on top of the file select window (see SoulsCreator_DrawPreview), using the game's
// own text renderer, so it looks like the rest of the file select.
//
// Every color writes the same settings the Cosmetics Editor uses, then applies them at once; SoulsLook.cpp stores them
// with the save file, so each file keeps its own look and path.
// Controls: D-pad / stick move, A (Cross) picks, B (Circle) goes back, Start = Done, L1 (N64 R / Z in menus) switches
// the preview age. The Name row opens the game's name entry and returns here when the name is confirmed.
#include <libultraship/bridge/consolevariablebridge.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "soh/Enhancements/cosmetics/SoulsCreator.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipUtils.h"
#include "soh/frame_interpolation.h"
extern "C" {
#include "z64.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "regs.h"
#include "src/overlays/gamestates/ovl_file_choose/file_choose.h"
void FileChoose_UpdateStickDirectionPromptAnim(GameState* thisx);
s32 Player_OverrideLimbDrawPause(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg);
}

// From CosmeticsEditor.cpp (its header pulls in ImGui; only this function is needed here)
void ApplyOrResetCustomGfxPatches(bool manualChange);

#define CVAR_SOULS_CREATOR CVAR_ENHANCEMENT("SoulsCreator")

namespace {

// ---------------------------------------------------------------------------------------------------------------
// Data

// The 24 colors of the picker (0xRRGGBB). A 25th entry, "Original", clears the setting.
const uint32_t kPalette[] = {
    0x1e691b, 0x2f9e44, 0x7ac74f, 0x0f5132, 0x641400, 0xb33a1a, 0xe0662b, 0xf2a03d,
    0x003c64, 0x1c6fd1, 0x4fb3ff, 0x7b4bd6, 0xe9c35c, 0xf5e08a, 0x9a6a2f, 0x3a2414,
    0xe8e8f0, 0x9aa3b5, 0x3b3f4a, 0x111318, 0xfecf0f, 0xff3c5a, 0xff8fc8, 0xffffff,
};
constexpr int kPaletteCount = 24;
constexpr int kPaletteCols = 8;
constexpr int kOriginalSwatch = kPaletteCount; // cursor position of the "Original" button

const char* const kPaths[] = { "Story", "Hero", "Master", "Souls" };
const char* const kPathDesc[] = { "Take half damage. Bosses expect less power.", "The original game.",
                                  "Take double damage. Faster, tougher bosses.",
                                  "Take 4x damage. Toughest bosses, no heart drops." };
const char* const kAges[] = { "Child", "Adult" };
const char* const kTunics[] = { "Kokiri", "Goron", "Zora" };
const char* const kOriginalOnly[] = { "Original" };

enum ItemType { IT_NAME, IT_MENU, IT_PATH, IT_RANDOM, IT_DONE, IT_COLOR, IT_SLIDER, IT_CHOICE };

struct CItem {
    const char* label;
    ItemType type;
    const char* desc;
    const char* ids[3];      // cosmetics ids a color row writes, e.g. "Link.KokiriTunic"
    Color_RGBA8 def;         // the game's own color, shown while the row is on Original
    int* var;                // IT_CHOICE / IT_SLIDER / IT_PATH: the value
    const char* const* opts; // IT_CHOICE / IT_PATH: the names
    int optCount;
    int menu;          // IT_MENU: index in kMenus
    bool gap;          // a small gap above the row
    bool changed;      // IT_COLOR: a color is set (false = Original)
    Color_RGBA8 color; // IT_COLOR: the set color
};

enum MenuId { M_BASE, M_HAIR, M_TUNICS, M_GEAR, M_BODY, M_ARMS, M_NAVI, M_EQUIP, M_COUNT };

int sPath = 1;
int sSizePct = 100;
int sAge = 0; // preview age: 0 child, 1 adult
int sPreviewTunic = 0;
int sPackChoice = 0; // model packs are not loaded yet: every pack row only has "Original"

#define COLOR_ITEM(label, desc, def, ...) \
    { label, IT_COLOR, desc, { __VA_ARGS__ }, def, nullptr, nullptr, 0, 0, false, false, { 0, 0, 0, 255 } }
#define PACK_ITEM(label)                                                                       \
    { label, IT_CHOICE, kPackDesc, { nullptr }, { 0, 0, 0, 255 }, &sPackChoice, kOriginalOnly, \
      1,     0,         false,     false,       { 0, 0, 0, 255 } }
#define ARRAY_N(a) ((int)(sizeof(a) / sizeof((a)[0])))

const char* const kPackDesc = "Model packs come in a later update.";
const char* const kPickColor = "Pick a color.";

CItem sBase[] = {
    PACK_ITEM("Model"),
    { "Preview Age",
      IT_CHOICE,
      "Preview only. Link still grows up in the story.",
      { nullptr },
      { 0, 0, 0, 255 },
      &sAge,
      kAges,
      2,
      0,
      false,
      false,
      { 0, 0, 0, 255 } },
    { "Preview Tunic",
      IT_CHOICE,
      "Which tunic the preview wears.",
      { nullptr },
      { 0, 0, 0, 255 },
      &sPreviewTunic,
      kTunics,
      3,
      0,
      false,
      false,
      { 0, 0, 0, 255 } },
};
CItem sHair[] = {
    COLOR_ITEM("Hair Color", "Hair color.", (Color_RGBA8{ 255, 173, 27, 255 }), "Link.Hair"),
};
CItem sTunics[] = {
    COLOR_ITEM("Kokiri Tunic", kPickColor, (Color_RGBA8{ 30, 105, 27, 255 }), "Link.KokiriTunic"),
    COLOR_ITEM("Goron Tunic", kPickColor, (Color_RGBA8{ 100, 20, 0, 255 }), "Link.GoronTunic"),
    COLOR_ITEM("Zora Tunic", kPickColor, (Color_RGBA8{ 0, 60, 100, 255 }), "Link.ZoraTunic"),
    { "Preview Tunic",
      IT_CHOICE,
      "Which tunic the preview wears.",
      { nullptr },
      { 0, 0, 0, 255 },
      &sPreviewTunic,
      kTunics,
      3,
      0,
      false,
      false,
      { 0, 0, 0, 255 } },
};
CItem sGear[] = {
    COLOR_ITEM("Boots", kPickColor, (Color_RGBA8{ 93, 44, 18, 255 }), "Link.Boots"),
    COLOR_ITEM("Silver Gauntlets", "Adult only.", (Color_RGBA8{ 255, 255, 255, 255 }), "Gloves.SilverGauntlets"),
    COLOR_ITEM("Golden Gauntlets", "Adult only.", (Color_RGBA8{ 254, 207, 15, 255 }), "Gloves.GoldenGauntlets"),
    COLOR_ITEM("Gauntlet Gem", kPickColor, (Color_RGBA8{ 255, 60, 100, 255 }), "Gloves.GauntletsGem"),
    COLOR_ITEM("Goron Bracelet", "Child only.", (Color_RGBA8{ 255, 255, 170, 255 }), "Gloves.GoronBracelet"),
};
CItem sBody[] = {
    { "Body Size",
      IT_SLIDER,
      "100 is the original size.",
      { nullptr },
      { 0, 0, 0, 255 },
      &sSizePct,
      nullptr,
      0,
      0,
      false,
      false,
      { 0, 0, 0, 255 } },
};
CItem sArms[] = {
    COLOR_ITEM("Kokiri Sword", kPickColor, (Color_RGBA8{ 255, 255, 255, 255 }), "Swords.KokiriBlade"),
    COLOR_ITEM("Master Sword", kPickColor, (Color_RGBA8{ 255, 255, 255, 255 }), "Swords.MasterBlade"),
    COLOR_ITEM("Biggoron Sword", kPickColor, (Color_RGBA8{ 255, 255, 255, 255 }), "Swords.BiggoronBlade"),
    COLOR_ITEM("Hilts", "The hilt of all three swords.", (Color_RGBA8{ 80, 80, 168, 255 }), "Swords.KokiriHilt",
               "Swords.MasterHilt", "Swords.BiggoronHilt"),
    COLOR_ITEM("Mirror Emblem", kPickColor, (Color_RGBA8{ 205, 225, 255, 255 }), "MirrorShield.Emblem"),
    COLOR_ITEM("Mirror Shield", kPickColor, (Color_RGBA8{ 215, 0, 0, 255 }), "MirrorShield.Body"),
};
CItem sNavi[] = {
    COLOR_ITEM("Inner Glow", kPickColor, (Color_RGBA8{ 255, 255, 255, 255 }), "Navi.IdlePrimary"),
    COLOR_ITEM("Outer Glow", kPickColor, (Color_RGBA8{ 0, 0, 255, 255 }), "Navi.IdleSecondary"),
};
CItem sEquip[] = {
    PACK_ITEM("Kokiri Sword"), PACK_ITEM("Master Sword"),  PACK_ITEM("Biggoron Sword"),
    PACK_ITEM("Deku Shield"),  PACK_ITEM("Hylian Shield"), PACK_ITEM("Gear Set"),
};

struct CMenu {
    const char* title;
    CItem* rows;
    int count;
};
const CMenu kMenus[M_COUNT] = {
    { "Base Model", sBase, ARRAY_N(sBase) }, { "Hair", sHair, ARRAY_N(sHair) },
    { "Tunics", sTunics, ARRAY_N(sTunics) }, { "Boots & Gauntlets", sGear, ARRAY_N(sGear) },
    { "Body", sBody, ARRAY_N(sBody) },       { "Sword & Shield", sArms, ARRAY_N(sArms) },
    { "Navi", sNavi, ARRAY_N(sNavi) },       { "Equipment", sEquip, ARRAY_N(sEquip) },
};

#define MENU_ITEM(label, desc, id, isGap) \
    { label, IT_MENU, desc, { nullptr }, { 0, 0, 0, 255 }, nullptr, nullptr, 0, id, isGap, false, { 0, 0, 0, 255 } }
#define ACTION_ITEM(label, type, desc, isGap) \
    { label, type, desc, { nullptr }, { 0, 0, 0, 255 }, nullptr, nullptr, 0, 0, isGap, false, { 0, 0, 0, 255 } }

CItem sRoot[] = {
    ACTION_ITEM("Name", IT_NAME, "Opens the name entry. END brings you back here.", false),
    MENU_ITEM("Base Model", "Pick a model pack and the preview age.", M_BASE, false),
    MENU_ITEM("Hair", "Hair color.", M_HAIR, true),
    MENU_ITEM("Tunics", "Kokiri, Goron and Zora tunic colors.", M_TUNICS, false),
    MENU_ITEM("Boots & Gauntlets", "Boots, gauntlets, gem and bracelet.", M_GEAR, false),
    MENU_ITEM("Body", "Overall size.", M_BODY, false),
    MENU_ITEM("Sword & Shield", "Blades, hilts and shields.", M_ARMS, false),
    MENU_ITEM("Navi", "Your fairy's glow.", M_NAVI, false),
    MENU_ITEM("Equipment", "Sword, shield and gear models from model packs.", M_EQUIP, false),
    { "Path", IT_PATH, nullptr, { nullptr }, { 0, 0, 0, 255 }, &sPath, kPaths, 4, 0, true, false, { 0, 0, 0, 255 } },
    ACTION_ITEM("Randomize Look", IT_RANDOM, "New random colors for every part.", false),
    ACTION_ITEM("Done", IT_DONE, "Save this look and start the adventure.", false),
};
constexpr int kRootCount = ARRAY_N(sRoot);

// ---------------------------------------------------------------------------------------------------------------
// State

enum COverlay { OV_NONE, OV_COLOR, OV_CONFIRM };

bool sActive = false;
bool sFinishing = false; // Done was pressed with no name yet: the name entry makes the file
int sMenu = -1;          // -1 = main list, else a MenuId
int sRootCur = 0;
int sSubCur = 0;
COverlay sOverlay = OV_NONE;
int sOv = 0;   // cursor in the color picker (0..24) or in the confirm box (0 yes, 1 no)
CItem sBackup; // color before the picker opened, for B (cancel)
int16_t sAlpha = 0;
uint16_t sArrowOffset = 0;

CItem* CurRows(int& count) {
    if (sMenu < 0) {
        count = kRootCount;
        return sRoot;
    }
    count = kMenus[sMenu].count;
    return kMenus[sMenu].rows;
}

std::string CosmeticCvar(const char* id, const char* field) {
    return std::string(CVAR_PREFIX_COSMETIC) + "." + id + "." + field;
}

// Write a color row's setting; Original clears it.
void WriteColor(const CItem& item) {
    for (const char* id : item.ids) {
        if (id == nullptr) {
            continue;
        }
        if (!item.changed) {
            CVarClear(CosmeticCvar(id, "Value").c_str());
            CVarClear(CosmeticCvar(id, "Changed").c_str());
        } else {
            CVarSetColor(CosmeticCvar(id, "Value").c_str(), item.color);
            CVarSetInteger(CosmeticCvar(id, "Changed").c_str(), 1);
        }
    }
}

void WritePath(int path) {
    // Story and Hero: damage x1. Master: x2 + Hyper Bosses. Souls: x4 + Hyper Bosses + Hyper Enemies + no heart drops.
    // (Story's lower damage and boss expectation come with the weapon-power damage hook.)
    CVarSetInteger(CVAR_ENHANCEMENT("SoulsPath"), path);
    CVarSetInteger(CVAR_ENHANCEMENT("DamageMult"), path == 2 ? 1 : path == 3 ? 2 : 0);
    CVarSetInteger(CVAR_ENHANCEMENT("HyperBosses"), path >= 2 ? 1 : 0);
    CVarSetInteger(CVAR_ENHANCEMENT("HyperEnemies"), path == 3 ? 1 : 0);
    CVarSetInteger(CVAR_ENHANCEMENT("NoHeartDrops"), path == 3 ? 1 : 0);
}

void WriteSize(int pct) {
    if (pct == 100) {
        CVarClear(CosmeticCvar("Link.BodySize", "Value").c_str());
        CVarClear(CosmeticCvar("Link.BodySize", "Changed").c_str());
        return;
    }
    CVarSetFloat(CosmeticCvar("Link.BodySize", "Value").c_str(), pct / 10000.0f);
    CVarSetInteger(CosmeticCvar("Link.BodySize", "Changed").c_str(), 1);
}

void ForEachColorItem(void (*fn)(CItem&)) {
    for (const CMenu& menu : kMenus) {
        for (int i = 0; i < menu.count; i++) {
            if (menu.rows[i].type == IT_COLOR) {
                fn(menu.rows[i]);
            }
        }
    }
}

void SetColor(CItem& item, int paletteIndex) {
    if (paletteIndex >= kPaletteCount) {
        item.changed = false;
        return;
    }
    uint32_t c = kPalette[paletteIndex];
    item.changed = true;
    item.color = Color_RGBA8{ (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c, 255 };
}

// Start values from the current settings so reopening the creator shows what is set
void LoadChoices() {
    sPath = CVarGetInteger(CVAR_ENHANCEMENT("SoulsPath"), 1);
    if (sPath < 0 || sPath > 3) {
        sPath = 1;
    }
    sSizePct = 100;
    if (CVarGetInteger(CosmeticCvar("Link.BodySize", "Changed").c_str(), 0)) {
        float v = CVarGetFloat(CosmeticCvar("Link.BodySize", "Value").c_str(), 0.01f);
        sSizePct = (int)(v * 10000.0f / 5.0f + 0.5f) * 5; // nearest 5 %
        sSizePct = sSizePct < 60 ? 60 : sSizePct > 140 ? 140 : sSizePct;
    }
    sAge = 0;
    sPreviewTunic = 0;
    for (const CMenu& menu : kMenus) {
        for (int i = 0; i < menu.count; i++) {
            CItem& item = menu.rows[i];
            if (item.type != IT_COLOR) {
                continue;
            }
            item.changed = CVarGetInteger(CosmeticCvar(item.ids[0], "Changed").c_str(), 0) != 0;
            if (item.changed) {
                item.color = CVarGetColor(CosmeticCvar(item.ids[0], "Value").c_str(), Color_RGBA8{ 0, 0, 0, 255 });
                item.color.a = 255;
            }
        }
    }
}

void PlaySfx(u16 sfx) {
    Audio_PlaySfxGeneral(sfx, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                         &gSfxDefaultReverb);
}

// The picker cursor position that shows the row's current color
int PickerIndexFor(const CItem& item) {
    if (!item.changed) {
        return kOriginalSwatch;
    }
    for (int k = 0; k < kPaletteCount; k++) {
        uint32_t c = kPalette[k];
        if (item.color.r == (uint8_t)(c >> 16) && item.color.g == (uint8_t)(c >> 8) && item.color.b == (uint8_t)c) {
            return k;
        }
    }
    return 0; // a custom color from the Cosmetics Editor: it stays until the cursor moves
}

// Apply every row (used when the look is final)
void ApplyAll() {
    for (const CMenu& menu : kMenus) {
        for (int i = 0; i < menu.count; i++) {
            if (menu.rows[i].type == IT_COLOR) {
                WriteColor(menu.rows[i]);
            }
        }
    }
    WriteSize(sSizePct);
    WritePath(sPath);
    ApplyOrResetCustomGfxPatches(false);
}

void FinishCreator(FileChooseContext* ctx) {
    ApplyAll();
    PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
    char name[16];
    if (FileChoose_GetNewFileName(ctx, name, sizeof(name)) > 0) {
        FileChoose_FinishNewFileFromMenu(ctx); // the name is set: make the file
    } else {
        sFinishing = true; // no name yet: the name entry makes the file when the player confirms it
        FileChoose_StartNameEntryFromMenu(ctx);
    }
}

// The drawing code is outside the anonymous namespace: OPEN_DISPS declares frame interpolation functions that
// must have C linkage.
} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Drawing helpers (320x240 screen space)

static int Fade(int base) {
    return base * sAlpha / 255;
}

static void UiRect(GraphicsContext* gfx, int x, int y, int w, int h, int r, int g, int b, int a) {
    if (a <= 0 || w <= 0 || h <= 0) {
        return;
    }
    OPEN_DISPS(gfx);
    gDPPipeSync(POLY_OPA_DISP++);
    gSPClearGeometryMode(POLY_OPA_DISP++, G_ZBUFFER | G_SHADE | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                                              G_TEXTURE_GEN_LINEAR | G_LOD | G_SHADING_SMOOTH);
    gDPSetOtherMode(POLY_OPA_DISP++,
                    G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                        G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_1PRIMITIVE,
                    G_AC_NONE | G_ZS_PIXEL | G_RM_CLD_SURF | G_RM_CLD_SURF2);
    gDPSetCombineMode(POLY_OPA_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, r, g, b, a);
    gDPFillRectangle(POLY_OPA_DISP++, x, y, x + w, y + h); // one-cycle rectangles end exclusive
    CLOSE_DISPS(gfx);
}

// Draws a line of text and returns its width in pixels
static int UiText(GraphicsContext* gfx, const char* text, int x, int y, int r, int g, int b, float scale) {
    OPEN_DISPS(gfx);
    Gfx_SetupDL_39Opa(gfx);
    gDPSetCombineLERP(POLY_OPA_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0, PRIMITIVE,
                      ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 0);
    CLOSE_DISPS(gfx);
    return Interface_DrawTextLine(gfx, (char*)text, x, y, r, g, b, sAlpha, scale, true);
}

// Width of a text in pixels (same rule as Interface_DrawTextLine)
static int TextWidth(const char* text, float scale) {
    int width = 0;
    for (const char* c = text; *c != '\0'; c++) {
        width += (uint16_t)(Ship_GetCharFontWidth((u8)*c) * (R_TEXT_CHAR_SCALE / 100.0f) * scale);
    }
    return width;
}

// A framed dark-blue panel with a vertical gradient, like the prototype's window
static void UiPanel(GraphicsContext* gfx, int x, int y, int w, int h, int alpha) {
    UiRect(gfx, x - 1, y - 1, w + 2, h + 2, 150, 180, 255, Fade(alpha));
    const int strips = 8;
    for (int i = 0; i < strips; i++) {
        int y0 = y + h * i / strips, y1 = y + h * (i + 1) / strips;
        UiRect(gfx, x, y0, w, y1 - y0, 22 - i * 2, 44 - i * 4, 104 - i * 7, Fade(alpha));
    }
}

const float kTextScale = 0.62f;
const int kRowH = 12;
const int kRowPitch = 13;
const int kRowsTop = 42;
const int kGapPx = 4;
const int kPanelX = 6, kPanelY = 6, kPanelW = 198, kPanelH = 228;
const int kRowX = 12, kRowW = 186;

static int RowY(const CItem* rows, int index) {
    int y = kRowsTop;
    for (int i = 0; i <= index; i++) {
        if (rows[i].gap) {
            y += kGapPx;
        }
        if (i < index) {
            y += kRowPitch;
        }
    }
    return y;
}

// Three bars of falling length: the prototype's "submenu" mark
static void SubmenuMark(GraphicsContext* gfx, int x, int y) {
    UiRect(gfx, x, y + 3, 10, 1, 207, 217, 245, Fade(255));
    UiRect(gfx, x + 2, y + 6, 8, 1, 207, 217, 245, Fade(255));
    UiRect(gfx, x + 4, y + 9, 6, 1, 207, 217, 245, Fade(255));
}

static void UiHighlight(GraphicsContext* gfx, int x, int y, int w, int h) {
    UiRect(gfx, x, y, w, h, 110, 160, 255, Fade(90));
    UiRect(gfx, x, y, w, 1, 210, 230, 255, Fade(230));
    UiRect(gfx, x, y + h - 1, w, 1, 210, 230, 255, Fade(230));
    UiRect(gfx, x, y, 1, h, 210, 230, 255, Fade(230));
    UiRect(gfx, x + w - 1, y, 1, h, 210, 230, 255, Fade(230));
}

// "< value >" in the value column
static void ChoiceText(GraphicsContext* gfx, const char* value, int x, int y, bool arrows) {
    float scale = kTextScale;
    int w = TextWidth(value, scale);
    if (w > 70) {
        scale = 0.5f;
        w = TextWidth(value, scale);
    }
    if (arrows) {
        UiText(gfx, "<", x - 10, y, 255, 215, 80, scale);
    }
    UiText(gfx, value, x, y, 255, 255, 255, scale);
    if (arrows) {
        UiText(gfx, ">", x + w + 4, y, 255, 215, 80, scale);
    }
}

static const char* ChoiceName(const CItem& item) {
    if (item.type == IT_PATH || item.type == IT_CHOICE) {
        int v = *item.var;
        return item.opts[v < 0 || v >= item.optCount ? 0 : v];
    }
    return "";
}

static void DrawRow(FileChooseContext* ctx, const CItem& item, int y, bool selected, bool inMenu) {
    GraphicsContext* gfx = ctx->state.gfxCtx;
    char buf[16];
    UiRect(gfx, kRowX, y, kRowW, kRowH, 20, 40, 100, Fade(150));
    if (selected) {
        UiHighlight(gfx, kRowX, y, kRowW, kRowH);
    }
    UiText(gfx, item.label, kRowX + 8, y + 1, 255, 255, 255, kTextScale);
    const int valueX = 112;
    switch (item.type) {
        case IT_NAME: {
            char name[16];
            if (FileChoose_GetNewFileName(ctx, name, sizeof(name)) == 0) {
                strcpy(name, "- - - -");
            }
            UiText(gfx, name, valueX, y + 1, 255, 215, 80, kTextScale);
            break;
        }
        case IT_MENU:
            SubmenuMark(gfx, kRowX + kRowW - 18, y);
            break;
        case IT_PATH:
        case IT_CHOICE:
            ChoiceText(gfx, ChoiceName(item), valueX, y + 1, selected || item.type == IT_PATH);
            break;
        case IT_SLIDER:
            snprintf(buf, sizeof(buf), "%d", *item.var);
            ChoiceText(gfx, buf, valueX, y + 1, true);
            break;
        case IT_COLOR: {
            Color_RGBA8 c = item.changed ? item.color : item.def;
            UiRect(gfx, valueX + 41, y + 2, 34, 8, 230, 238, 255, Fade(255));
            UiRect(gfx, valueX + 42, y + 3, 32, 6, c.r, c.g, c.b, Fade(255));
            if (!item.changed) {
                UiText(gfx, "Original", valueX - 8, y + 1, 170, 185, 215, 0.5f);
            }
            break;
        }
        default:
            break;
    }
    (void)inMenu;
}

// Splits text into lines that fit the width
static std::vector<std::string> WrapText(const char* text, int maxWidth, float scale) {
    std::vector<std::string> lines;
    std::string line, word;
    std::string all = std::string(text) + " ";
    for (char c : all) {
        if (c != ' ') {
            word += c;
            continue;
        }
        std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && TextWidth(trial.c_str(), scale) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

static const char* DescriptionFor(const CItem& item) {
    if (item.type == IT_PATH) {
        return kPathDesc[sPath];
    }
    return item.desc != nullptr ? item.desc : "";
}

static void DrawColorPicker(FileChooseContext* ctx) {
    GraphicsContext* gfx = ctx->state.gfxCtx;
    int count;
    const CItem& item = CurRows(count)[sSubCur];
    Color_RGBA8 c = item.changed ? item.color : item.def;

    UiRect(gfx, 0, 0, 320, 240, 0, 4, 20, Fade(130));
    UiPanel(gfx, 50, 26, 220, 188, 255);
    UiText(gfx, item.label, 62, 32, 255, 255, 255, 0.8f);
    UiRect(gfx, 61, 51, 198, 12, 230, 238, 255, Fade(255));
    UiRect(gfx, 62, 52, 196, 10, c.r, c.g, c.b, Fade(255));
    for (int i = 0; i < kPaletteCount; i++) {
        int x = 64 + (i % kPaletteCols) * 24, y = 72 + (i / kPaletteCols) * 19;
        uint32_t p = kPalette[i];
        UiRect(gfx, x - 1, y - 1, 22, 16, 255, 255, 255, Fade(110));
        UiRect(gfx, x, y, 20, 14, (p >> 16) & 255, (p >> 8) & 255, p & 255, Fade(255));
        if (sOv == i) {
            UiHighlight(gfx, x - 3, y - 3, 26, 20);
        }
    }
    UiRect(gfx, 64, 130, 100, 14, 20, 40, 100, Fade(200));
    UiText(gfx, "Original", 72, 131, 255, 255, 255, kTextScale);
    if (sOv == kOriginalSwatch) {
        UiHighlight(gfx, 62, 128, 104, 18);
    }
    const int rgb[3] = { c.r, c.g, c.b };
    const char* names[3] = { "R", "G", "B" };
    for (int i = 0; i < 3; i++) {
        int y = 156 + i * 12;
        char num[8];
        snprintf(num, sizeof(num), "%d", rgb[i]);
        UiText(gfx, names[i], 62, y - 2, 255, 255, 255, 0.55f);
        UiRect(gfx, 76, y + 2, 156, 3, 12, 26, 72, Fade(255));
        UiRect(gfx, 76, y + 2, 156 * rgb[i] / 255, 3, 100, 150, 255, Fade(255));
        UiText(gfx, num, 240, y - 2, 255, 255, 255, 0.55f);
    }
    UiText(gfx, "(X) Pick   (O) Cancel", 62, 196, 200, 215, 245, 0.55f);
}

static void DrawConfirm(FileChooseContext* ctx) {
    GraphicsContext* gfx = ctx->state.gfxCtx;
    UiRect(gfx, 0, 0, 320, 240, 0, 4, 20, Fade(115));
    UiPanel(gfx, 80, 80, 160, 78, 255);
    UiText(gfx, "Is this OK?", 100, 88, 255, 255, 255, 0.85f);
    char name[16];
    if (FileChoose_GetNewFileName(ctx, name, sizeof(name)) == 0) {
        strcpy(name, "Link");
    }
    std::string line = std::string(name) + " - " + kPaths[sPath] + " path";
    UiText(gfx, line.c_str(), 92, 108, 200, 215, 245, kTextScale);
    UiRect(gfx, 96, 128, 56, 16, 20, 40, 100, Fade(220));
    UiRect(gfx, 168, 128, 56, 16, 20, 40, 100, Fade(220));
    UiText(gfx, "Yes", 112, 131, 255, 255, 255, kTextScale);
    UiText(gfx, "Not yet", 176, 131, 255, 255, 255, kTextScale);
    UiHighlight(gfx, sOv == 0 ? 96 : 168, 128, 56, 16);
}

// The left panel, labels, description box and button prompts
static void DrawMain(FileChooseContext* ctx) {
    GraphicsContext* gfx = ctx->state.gfxCtx;
    int count;
    CItem* rows = CurRows(count);
    int cur = sMenu < 0 ? sRootCur : sSubCur;

    UiPanel(gfx, kPanelX, kPanelY, kPanelW, kPanelH, 255);
    UiText(gfx, "Create Character", 16, 10, 255, 255, 255, 0.85f);
    std::string sub = "Detailed Look";
    if (sMenu >= 0) {
        sub += std::string(" > ") + kMenus[sMenu].title;
    }
    UiText(gfx, sub.c_str(), 16, 27, 255, 215, 80, 0.6f);

    for (int i = 0; i < count; i++) {
        DrawRow(ctx, rows[i], RowY(rows, i), i == cur, sMenu >= 0);
    }

    // Button prompts under the list
    const CItem& item = rows[cur];
    std::string prompts = "(X) OK   (O) Back";
    if (item.type == IT_PATH || item.type == IT_SLIDER || item.type == IT_CHOICE) {
        prompts += "   > Change";
    }
    UiText(gfx, prompts.c_str(), 16, 216, 255, 255, 255, 0.55f);

    // Description box (black message box under the preview)
    UiRect(gfx, 205, 183, 112, 49, 255, 255, 255, Fade(120));
    UiRect(gfx, 206, 184, 110, 47, 0, 0, 0, Fade(240));
    std::vector<std::string> lines = WrapText(DescriptionFor(item), 98, 0.55f);
    for (size_t i = 0; i < lines.size() && i < 3; i++) {
        UiText(gfx, lines[i].c_str(), 212, 190 + (int)i * 11, 255, 255, 255, 0.55f);
    }

    // Age labels over the preview: the active one white, the other dimmed. L1 switches (R1 is Back in the Souls
    // layout).
    UiText(gfx, "Child", 218, 8, sAge == 0 ? 255 : 150, sAge == 0 ? 255 : 165, sAge == 0 ? 255 : 200, 0.6f);
    UiText(gfx, "(L1)", 250, 8, 190, 205, 235, 0.5f);
    UiText(gfx, "Adult", 276, 8, sAge == 1 ? 255 : 150, sAge == 1 ? 255 : 165, sAge == 1 ? 255 : 200, 0.6f);
}

// ---------------------------------------------------------------------------------------------------------------
// Entry points

extern "C" bool SoulsCreator_IsEnabled() {
    return CVarGetInteger(CVAR_SOULS_CREATOR, 0) != 0;
}

extern "C" bool SoulsCreator_IsActive() {
    return sActive;
}

extern "C" void SoulsCreator_Begin() {
    sActive = true;
    sFinishing = false;
    sMenu = -1;
    sRootCur = 0;
    sSubCur = 0;
    sOverlay = OV_NONE;
    LoadChoices();
}

extern "C" void SoulsCreator_End() {
    sActive = false;
}

extern "C" int16_t* SoulsCreator_UIAlpha() {
    return &sAlpha;
}

extern "C" uint16_t* SoulsCreator_ArrowOffset() {
    return &sArrowOffset;
}

// The name entry's END button: with the Name row, go back to the creator instead of making the file
extern "C" bool SoulsCreator_OnNameConfirmed(void* context) {
    FileChooseContext* ctx = (FileChooseContext*)context;
    if (!sActive || sFinishing) {
        return false;
    }
    ctx->prevConfigMode = CM_NAME_ENTRY;
    ctx->configMode = CM_NAME_ENTRY_TO_SETTINGS_MENU;
    CVarSetInteger(CVAR_GENERAL("OnFileSelectNameEntry"), 0);
    return true;
}

extern "C" void SoulsCreator_Update(void* state) {
    GameState* gameState = (GameState*)state;
    FileChooseContext* ctx = (FileChooseContext*)gameState;
    Input* input = &ctx->state.input[0];
    bool dpad = CVarGetInteger(CVAR_SETTING("DpadInText"), 0);

    FileChoose_UpdateStickDirectionPromptAnim(gameState);
    FileChoose_UpdateListMenuAnim(&sAlpha, &sArrowOffset);

    bool up = ctx->stickRelY > 30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DUP));
    bool down = ctx->stickRelY < -30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DDOWN));
    bool left = ctx->stickRelX < -30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DLEFT));
    bool right = ctx->stickRelX > 30 || (dpad && CHECK_BTN_ALL(input->press.button, BTN_DRIGHT));
    bool pressA = CHECK_BTN_ALL(input->press.button, BTN_A);
    bool pressB = CHECK_BTN_ALL(input->press.button, BTN_B);
    bool pressStart = CHECK_BTN_ALL(input->press.button, BTN_START);

    // R or Z switches the preview between child and adult
    if (CHECK_BTN_ANY(input->press.button, BTN_R | BTN_Z) && sOverlay != OV_CONFIRM) {
        sAge = !sAge;
        PlaySfx(NA_SE_SY_FSEL_CURSOR);
    }

    if (sOverlay == OV_CONFIRM) {
        if (up || down || left || right) {
            sOv = 1 - sOv;
            PlaySfx(NA_SE_SY_FSEL_CURSOR);
        } else if (pressA || pressStart) {
            sOverlay = OV_NONE;
            if (sOv == 0) {
                FinishCreator(ctx);
            } else {
                PlaySfx(NA_SE_SY_FSEL_DECIDE_S);
            }
        } else if (pressB) {
            sOverlay = OV_NONE;
            PlaySfx(NA_SE_SY_FSEL_DECIDE_S);
        }
        return;
    }

    int count;
    CItem* rows = CurRows(count);
    int& cur = sMenu < 0 ? sRootCur : sSubCur;
    CItem& item = rows[cur];

    if (sOverlay == OV_COLOR) {
        int old = sOv;
        int col = sOv % kPaletteCols;
        if (sOv == kOriginalSwatch) {
            if (up) {
                sOv = kPaletteCount - kPaletteCols; // first color of the last row
            } else if (down) {
                sOv = 0;
            }
        } else if (left) {
            sOv = (sOv / kPaletteCols) * kPaletteCols + (col + kPaletteCols - 1) % kPaletteCols;
        } else if (right) {
            sOv = (sOv / kPaletteCols) * kPaletteCols + (col + 1) % kPaletteCols;
        } else if (up) {
            sOv = sOv >= kPaletteCols ? sOv - kPaletteCols : kOriginalSwatch;
        } else if (down) {
            sOv = sOv + kPaletteCols < kPaletteCount ? sOv + kPaletteCols : kOriginalSwatch;
        }
        if (sOv != old) {
            SetColor(item, sOv); // live: the preview changes at once
            WriteColor(item);
            ApplyOrResetCustomGfxPatches(false);
            PlaySfx(NA_SE_SY_FSEL_CURSOR);
        }
        if (pressA) {
            sOverlay = OV_NONE;
            PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
        } else if (pressB) {
            item.changed = sBackup.changed; // cancel: back to the color from before
            item.color = sBackup.color;
            WriteColor(item);
            ApplyOrResetCustomGfxPatches(false);
            sOverlay = OV_NONE;
            PlaySfx(NA_SE_SY_FSEL_DECIDE_S);
        }
        return;
    }

    // Move the cursor
    if (up || down) {
        cur = (cur + (down ? 1 : count - 1)) % count;
        PlaySfx(NA_SE_SY_FSEL_CURSOR);
        return;
    }

    // Change a choice or slider in place
    if ((left || right) && (item.type == IT_PATH || item.type == IT_CHOICE || item.type == IT_SLIDER)) {
        int dir = right ? 1 : -1;
        if (item.type == IT_SLIDER) {
            *item.var = *item.var + dir * 5;
            *item.var = *item.var < 60 ? 60 : *item.var > 140 ? 140 : *item.var;
            WriteSize(*item.var);
            ApplyOrResetCustomGfxPatches(false);
        } else {
            *item.var = (*item.var + dir + item.optCount) % item.optCount;
            if (item.type == IT_PATH) {
                WritePath(sPath);
            }
        }
        PlaySfx(NA_SE_SY_FSEL_CURSOR);
        return;
    }

    if (pressB) {
        PlaySfx(NA_SE_SY_FSEL_DECIDE_S);
        if (sMenu >= 0) {
            sMenu = -1; // back to the main list, on the same row
        } else {
            ctx->configMode = CM_SETTINGS_MENU_TO_QUEST;
        }
        return;
    }

    if (pressStart) {
        sOverlay = OV_CONFIRM;
        sOv = 0;
        PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
        return;
    }

    if (pressA) {
        switch (item.type) {
            case IT_NAME:
                PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
                sFinishing = false;
                FileChoose_StartNameEntryFromMenu(ctx);
                break;
            case IT_MENU:
                sMenu = item.menu;
                sSubCur = 0;
                PlaySfx(NA_SE_SY_FSEL_DECIDE_S);
                break;
            case IT_PATH:
            case IT_CHOICE:
                *item.var = (*item.var + 1) % item.optCount;
                if (item.type == IT_PATH) {
                    WritePath(sPath);
                }
                PlaySfx(NA_SE_SY_FSEL_CURSOR);
                break;
            case IT_COLOR:
                sBackup = item;
                sOv = PickerIndexFor(item);
                sOverlay = OV_COLOR;
                PlaySfx(NA_SE_SY_FSEL_DECIDE_S);
                break;
            case IT_RANDOM:
                ForEachColorItem([](CItem& it) {
                    SetColor(it, rand() % kPaletteCount);
                    WriteColor(it);
                });
                ApplyOrResetCustomGfxPatches(false);
                PlaySfx(NA_SE_SY_FSEL_DECIDE_S);
                break;
            case IT_DONE:
                sOverlay = OV_CONFIRM;
                sOv = 0;
                PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
                break;
            default:
                break;
        }
    }
}

// Everything is drawn from SoulsCreator_DrawPreview, at the end of the frame, over the file select window.
extern "C" void SoulsCreator_Draw(void* context) {
    (void)context;
}

// ---------------------------------------------------------------------------------------------------------------
// Live preview: the real child/adult Link (skeleton, idle animation, current cosmetics), drawn on the right side of
// the screen the way the pause screen's equipment page draws him, but straight onto the screen.
// The file select has no PlayState, so a zeroed one stands in: Link's draw code only reads its graphics context.
namespace {
PlayState* sPreviewPlay = nullptr;
u8* sPreviewSegment = nullptr;
SkelAnime sPreviewSkel;
s32 sPreviewLoadedAge = -1;
s32 sPreviewAge = LINK_AGE_CHILD;
s16 sPreviewYaw = 32300;
} // namespace

extern "C" void SoulsCreator_SetPreviewAge(int age) {
    sPreviewAge = age ? LINK_AGE_CHILD : LINK_AGE_ADULT;
}

extern "C" void SoulsCreator_DrawPreview(void* state) {
    GameState* gameState = (GameState*)state;
    FileChooseContext* ctx = (FileChooseContext*)gameState;
    if (!sActive || ctx->configMode != CM_SETTINGS_MENU) {
        return;
    }
    if (sPreviewPlay == nullptr) {
        sPreviewPlay = (PlayState*)calloc(1, sizeof(PlayState));
        sPreviewSegment = (u8*)malloc(0x400000);
    }
    DrawMain(ctx); // the panel and list go under the preview
    sPreviewPlay->state.gfxCtx = gameState->gfxCtx;
    sPreviewPlay->view.eye = Vec3f{ 0.0f, 0.0f, -400.0f };

    sPreviewAge = sAge ? LINK_AGE_ADULT : LINK_AGE_CHILD;
    s32 savedAge = gSaveContext.linkAge;
    gSaveContext.linkAge = sPreviewAge;
    if (sPreviewLoadedAge != sPreviewAge) {
        func_80091738(sPreviewPlay, sPreviewSegment, &sPreviewSkel);
        sPreviewLoadedAge = sPreviewAge;
    }
    LinkAnimation_Update(sPreviewPlay, &sPreviewSkel);

    // Right side of the 320x240 screen, above the description box
    // (the viewport is stretched over the whole 16:9 screen while the 2D drawing is a centered 4:3 area, so it is
    // narrower)
    const s32 vx = 202, vy = 20, vw = 74, vh = 160;
    static Vp viewport;
    static Lights1 lights = gdSPDefLights1(80, 80, 80, 255, 255, 255, 84, 84, -84);
    Vec3f lightDir = { 89.8f, 0.0f, 89.8f };
    Vec3f eye = { 0.0f, 0.0f, -400.0f };
    Vec3f at = { 0.0f, (sPreviewAge == LINK_AGE_CHILD) ? 60.0f : 0.0f, 0.0f };
    Vec3f pos = (sPreviewAge == LINK_AGE_CHILD) ? Vec3f{ 0.0f, -185.0f, -80.0f } : Vec3f{ 20.0f, -180.0f, -40.0f };
    Vec3s rot = { 0, sPreviewYaw, 0 };
    f32 scale = (sPreviewAge == LINK_AGE_CHILD) ? 0.058f : 0.047f;
    u8 swordAndShield[2] = { (u8)(sPreviewAge == LINK_AGE_CHILD ? PLAYER_SWORD_KOKIRI : PLAYER_SWORD_MASTER),
                             (u8)(sPreviewAge == LINK_AGE_CHILD ? PLAYER_SHIELD_DEKU : PLAYER_SHIELD_HYLIAN) };
    u16 perspNorm;
    Mtx* perspMtx = (Mtx*)Graph_Alloc(gameState->gfxCtx, sizeof(Mtx));
    Mtx* lookAtMtx = (Mtx*)Graph_Alloc(gameState->gfxCtx, sizeof(Mtx));

    OPEN_DISPS(gameState->gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gSPLoadGeometryMode(POLY_OPA_DISP++, G_ZBUFFER | G_SHADE | G_CULL_BACK | G_LIGHTING | G_SHADING_SMOOTH);
    viewport.vp.vscale[0] = vw * 2;
    viewport.vp.vscale[1] = vh * 2;
    viewport.vp.vscale[2] = G_MAXZ / 2;
    viewport.vp.vtrans[0] = (vx + vw / 2) * 4;
    viewport.vp.vtrans[1] = (vy + vh / 2) * 4;
    viewport.vp.vtrans[2] = G_MAXZ / 2;
    gSPViewport(POLY_OPA_DISP++, &viewport);
    guPerspective(perspMtx, &perspNorm, 60.0f, 98.0f / 160.0f, 10.0f, 4000.0f, 1.0f);
    gSPPerspNormalize(POLY_OPA_DISP++, perspNorm);
    gSPMatrix(POLY_OPA_DISP++, perspMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    guLookAt(lookAtMtx, eye.x, eye.y, eye.z, at.x, at.y, at.z, 0.0f, 1.0f, 0.0f);
    gSPMatrix(POLY_OPA_DISP++, lookAtMtx, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);

    Matrix_SetTranslateRotateYXZ(pos.x, pos.y, pos.z, &rot);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPSegment(POLY_OPA_DISP++, 0x04, (uintptr_t)(sPreviewSegment + 0x3800));
    gSPSegment(POLY_OPA_DISP++, 0x06, (uintptr_t)(sPreviewSegment + 0x8800));
    gSPSetLights1(POLY_OPA_DISP++, lights);
    func_80093C80(sPreviewPlay);
    POLY_OPA_DISP = Gfx_SetFog2(POLY_OPA_DISP++, 0, 0, 0, 0, 997, 1000);
    func_8002EABC(&pos, &eye, &lightDir, gameState->gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    Player_DrawImpl(sPreviewPlay, sPreviewSkel.skeleton, sPreviewSkel.jointTable, sPreviewSkel.dListCount, 0,
                    PLAYER_TUNIC_KOKIRI + sPreviewTunic, PLAYER_BOOTS_KOKIRI, 0, Player_OverrideLimbDrawPause, NULL,
                    swordAndShield);
    CLOSE_DISPS(gameState->gfxCtx);

    gSaveContext.linkAge = savedAge;

    // Dialogs go over everything, the preview included
    if (sOverlay == OV_COLOR) {
        DrawColorPicker(ctx);
    } else if (sOverlay == OV_CONFIRM) {
        DrawConfirm(ctx);
    }
}
