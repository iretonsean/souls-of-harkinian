// Souls of Harkinian: character creator in the file select.
//
// With gEnhancements.SoulsCreator on, choosing Vanilla or Master Quest for a new file opens this list before OoT's name
// entry. It reuses the file select's own list menu (the slot the randomizer and speedrun menus use), its text renderer,
// cursor arrows and scroll arrows, so it looks like the rest of the file select.
//
// Every row writes the same settings the Cosmetics Editor uses, then applies them; SoulsLook.cpp stores them with the
// save file, so each file keeps its own look and path. A = next row / Done, B = back to the quest menu, Start = Done.
#include <libultraship/bridge/consolevariablebridge.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "soh/Enhancements/cosmetics/SoulsCreator.h"

#include "soh/Enhancements/game-interactor/GameInteractor.h"
extern "C" {
#include "z64.h"
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "src/overlays/gamestates/ovl_file_choose/file_choose.h"
void FileChoose_UpdateStickDirectionPromptAnim(GameState* thisx);
}

// From CosmeticsEditor.cpp (its header pulls in ImGui; only this function is needed here)
void ApplyOrResetCustomGfxPatches(bool manualChange);

#define CVAR_SOULS_CREATOR CVAR_ENHANCEMENT("SoulsCreator")
#define CREATOR_VISIBLE_ROWS 7

namespace {

struct PaletteColor {
    const char* name;
    uint8_t r, g, b;
};

// Index 0 = the game's own color (the cosmetic setting is cleared)
const PaletteColor kPalette[] = {
    { "Original", 0, 0, 0 },          { "Forest Green", 30, 105, 27 }, { "Crimson", 170, 20, 30 },
    { "Royal Blue", 30, 60, 160 },    { "Midnight", 25, 25, 45 },      { "Ash Gray", 120, 120, 128 },
    { "Gold", 220, 170, 40 },         { "Snow", 235, 235, 240 },       { "Violet", 110, 50, 150 },
    { "Teal", 20, 130, 130 },         { "Copper", 170, 90, 40 },       { "Rose", 220, 110, 150 },
};
constexpr int kPaletteCount = sizeof(kPalette) / sizeof(kPalette[0]);

const char* kPaths[] = { "Story", "Hero", "Master", "Souls" };
const char* kSizes[] = { "70%", "80%", "90%", "100%", "110%", "120%", "130%" };
const float kSizeValues[] = { 0.007f, 0.008f, 0.009f, 0.010f, 0.011f, 0.012f, 0.013f };

enum RowKind { ROW_PATH, ROW_COLOR, ROW_SIZE, ROW_RANDOM, ROW_DONE };

struct Row {
    const char* label;
    RowKind kind;
    const char* cosmetic; // cosmetics id for ROW_COLOR, e.g. "Link.KokiriTunic"
    const char* cosmetic2; // optional second id that follows the first (Navi inner + outer glow)
};

const Row kRows[] = {
    { "Path", ROW_PATH, nullptr, nullptr },
    { "Kokiri Tunic", ROW_COLOR, "Link.KokiriTunic", nullptr },
    { "Goron Tunic", ROW_COLOR, "Link.GoronTunic", nullptr },
    { "Zora Tunic", ROW_COLOR, "Link.ZoraTunic", nullptr },
    { "Hair", ROW_COLOR, "Link.Hair", nullptr },
    { "Boots", ROW_COLOR, "Link.Boots", nullptr },
    { "Body Size", ROW_SIZE, nullptr, nullptr },
    { "Navi", ROW_COLOR, "Navi.IdlePrimary", "Navi.IdleSecondary" },
    { "Kokiri Sword", ROW_COLOR, "Swords.KokiriBlade", nullptr },
    { "Master Sword", ROW_COLOR, "Swords.MasterBlade", nullptr },
    { "Randomize Look", ROW_RANDOM, nullptr, nullptr },
    { "Done", ROW_DONE, nullptr, nullptr },
};
constexpr uint8_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

bool sActive = false;
uint8_t sIndex = 0;
uint8_t sOffset = 0;
int16_t sAlpha = 0;
uint16_t sArrowOffset = 0;
int sChoice[kRowCount];

std::string CosmeticCvar(const char* id, const char* field) {
    return std::string(CVAR_PREFIX_COSMETIC) + "." + id + "." + field;
}

void WriteColor(const char* id, int choice) {
    if (id == nullptr) {
        return;
    }
    if (choice == 0) {
        CVarClear(CosmeticCvar(id, "Value").c_str());
        CVarClear(CosmeticCvar(id, "Changed").c_str());
        return;
    }
    Color_RGBA8 color = { kPalette[choice].r, kPalette[choice].g, kPalette[choice].b, 255 };
    CVarSetColor(CosmeticCvar(id, "Value").c_str(), color);
    CVarSetInteger(CosmeticCvar(id, "Changed").c_str(), 1);
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

void WriteSize(int choice) {
    if (choice == 3) {
        CVarClear(CosmeticCvar("Link.BodySize", "Value").c_str());
        CVarClear(CosmeticCvar("Link.BodySize", "Changed").c_str());
        return;
    }
    CVarSetFloat(CosmeticCvar("Link.BodySize", "Value").c_str(), kSizeValues[choice]);
    CVarSetInteger(CosmeticCvar("Link.BodySize", "Changed").c_str(), 1);
}

void ApplyRow(uint8_t i) {
    const Row& row = kRows[i];
    switch (row.kind) {
        case ROW_PATH:
            WritePath(sChoice[i]);
            break;
        case ROW_COLOR:
            WriteColor(row.cosmetic, sChoice[i]);
            WriteColor(row.cosmetic2, sChoice[i]);
            break;
        case ROW_SIZE:
            WriteSize(sChoice[i]);
            break;
        default:
            break;
    }
}

int ChoiceCount(const Row& row) {
    switch (row.kind) {
        case ROW_PATH:
            return 4;
        case ROW_COLOR:
            return kPaletteCount;
        case ROW_SIZE:
            return 7;
        default:
            return 0;
    }
}

const char* ChoiceName(uint8_t i) {
    const Row& row = kRows[i];
    switch (row.kind) {
        case ROW_PATH:
            return kPaths[sChoice[i]];
        case ROW_COLOR:
            return kPalette[sChoice[i]].name;
        case ROW_SIZE:
            return kSizes[sChoice[i]];
        default:
            return "";
    }
}

// Start values from the current settings so reopening the creator shows what is set
void LoadChoices() {
    for (uint8_t i = 0; i < kRowCount; i++) {
        const Row& row = kRows[i];
        sChoice[i] = 0;
        if (row.kind == ROW_PATH) {
            sChoice[i] = CVarGetInteger(CVAR_ENHANCEMENT("SoulsPath"), 1);
        } else if (row.kind == ROW_SIZE) {
            float v = CVarGetInteger(CosmeticCvar("Link.BodySize", "Changed").c_str(), 0)
                          ? CVarGetFloat(CosmeticCvar("Link.BodySize", "Value").c_str(), 0.01f)
                          : 0.01f;
            int best = 3;
            for (int k = 0; k < 7; k++) {
                if (std::abs(kSizeValues[k] - v) < std::abs(kSizeValues[best] - v)) {
                    best = k;
                }
            }
            sChoice[i] = best;
        } else if (row.kind == ROW_COLOR && CVarGetInteger(CosmeticCvar(row.cosmetic, "Changed").c_str(), 0)) {
            Color_RGBA8 c = CVarGetColor(CosmeticCvar(row.cosmetic, "Value").c_str(), Color_RGBA8{ 0, 0, 0, 255 });
            for (int k = 1; k < kPaletteCount; k++) {
                if (kPalette[k].r == c.r && kPalette[k].g == c.g && kPalette[k].b == c.b) {
                    sChoice[i] = k;
                }
            }
        }
    }
}

void PlaySfx(u16 sfx) {
    Audio_PlaySfxGeneral(sfx, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                         &gSfxDefaultReverb);
}

} // namespace

extern "C" bool SoulsCreator_IsEnabled() {
    return CVarGetInteger(CVAR_SOULS_CREATOR, 0) != 0;
}

extern "C" bool SoulsCreator_IsActive() {
    return sActive;
}

extern "C" void SoulsCreator_Begin() {
    sActive = true;
    sIndex = 0;
    sOffset = 0;
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

extern "C" void SoulsCreator_Update(void* state) {
    GameState* gameState = (GameState*)state;
    FileChooseContext* ctx = (FileChooseContext*)gameState;
    Input* input = &ctx->state.input[0];
    bool dpad = CVarGetInteger(CVAR_SETTING("DpadInText"), 0);

    FileChoose_UpdateStickDirectionPromptAnim(gameState);
    FileChoose_UpdateListMenuAnim(&sAlpha, &sArrowOffset);
    FileChoose_MoveListCursor(ctx, &sIndex, &sOffset, kRowCount, CREATOR_VISIBLE_ROWS);

    const Row& row = kRows[sIndex];
    int count = ChoiceCount(row);
    if (count > 0 && (ABS(ctx->stickRelX) > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DLEFT | BTN_DRIGHT)))) {
        bool right = ctx->stickRelX > 30 || (dpad && CHECK_BTN_ANY(input->press.button, BTN_DRIGHT));
        sChoice[sIndex] = (sChoice[sIndex] + (right ? 1 : count - 1)) % count;
        ApplyRow(sIndex);
        ApplyOrResetCustomGfxPatches(false);
        PlaySfx(NA_SE_SY_FSEL_CURSOR);
    }

    if (CHECK_BTN_ALL(input->press.button, BTN_B)) {
        ctx->configMode = CM_SETTINGS_MENU_TO_QUEST;
        return;
    }

    bool done = CHECK_BTN_ALL(input->press.button, BTN_START) ||
                (CHECK_BTN_ALL(input->press.button, BTN_A) && row.kind == ROW_DONE);
    if (CHECK_BTN_ALL(input->press.button, BTN_A) && row.kind == ROW_RANDOM) {
        for (uint8_t i = 0; i < kRowCount; i++) {
            if (kRows[i].kind == ROW_COLOR) {
                sChoice[i] = 1 + rand() % (kPaletteCount - 1);
                ApplyRow(i);
            }
        }
        ApplyOrResetCustomGfxPatches(false);
        PlaySfx(NA_SE_SY_FSEL_DECIDE_S);
    } else if (CHECK_BTN_ALL(input->press.button, BTN_A) && !done) {
        // A moves on to the next row, like confirming a choice
        sIndex = (sIndex + 1) % kRowCount;
        if (sIndex >= sOffset + CREATOR_VISIBLE_ROWS) {
            sOffset = sIndex - CREATOR_VISIBLE_ROWS + 1;
        } else if (sIndex < sOffset) {
            sOffset = sIndex;
        }
        PlaySfx(NA_SE_SY_FSEL_CURSOR);
    }

    if (done) {
        for (uint8_t i = 0; i < kRowCount; i++) {
            ApplyRow(i);
        }
        ApplyOrResetCustomGfxPatches(false);
        PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
        FileChoose_StartNameEntryFromMenu(ctx);
    }
}

extern "C" void SoulsCreator_Draw(void* context) {
    FileChooseContext* ctx = (FileChooseContext*)context;
    int16_t alpha = sAlpha;
    FileChoose_DrawListScrollArrows(ctx, sOffset, kRowCount, CREATOR_VISIBLE_ROWS, sArrowOffset);
    for (uint8_t i = sOffset; i < kRowCount && i - sOffset < CREATOR_VISIBLE_ROWS; i++) {
        uint16_t y = 87 + (i - sOffset) * 16;
        bool selected = sIndex == i;
        // Row name: gold like the boss rush menu; the choice in white
        Interface_DrawTextLine(ctx->state.gfxCtx, (char*)kRows[i].label, 65, y, 255, 255, 80, alpha, 0.8f, true);
        if (ChoiceCount(kRows[i]) > 0) {
            uint16_t width = Interface_DrawTextLine(ctx->state.gfxCtx, (char*)ChoiceName(i), 165, y, 255, 255, 255,
                                                    alpha, 0.8f, true);
            if (selected) {
                FileChoose_DrawListCursorArrow(ctx, alpha, 160.0f, (f32)(y + 5), true);
                FileChoose_DrawListCursorArrow(ctx, alpha, (f32)(171 + width), (f32)(y + 5), false);
            }
        } else if (selected) {
            FileChoose_DrawListCursorArrow(ctx, alpha, 58.0f, (f32)(y + 5), false);
        }
    }
}
