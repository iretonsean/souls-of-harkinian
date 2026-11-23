// Souls of Harkinian: one controller layout for play and another for menus.
//
// Expected bindings (Settings > Controls), all on port 1:
//   d-pad      -> N64 D-pad (D-up/down/left/right)
//   Cross      -> L   (unused by the game otherwise)
//   Circle     -> A + Modifier 1
//   Triangle   -> A
//   Square     -> C-left
//   R1/R2      -> B, L1/L3 -> R, L2/R3 -> Z, Options -> Start
//
// In play: the D-pad acts as the C buttons (items, Navi, ocarina notes) and Cross jumps: it uses the
// Roc's Feather jump (Enhancements/randomizer/RocsFeather.cpp) as a built-in move, so the feather
// never sits on a button, in the inventory or on the HUD. The game's own item-button checks apply
// (no jump while carrying something or in a cutscene; one jump until Link lands).
// In menus (pause screen, file select / title, text boxes): the D-pad stays the D-pad, so with
// "D-pad Support on Pause Screen" and "D-pad Support in Text Boxes" on it moves the cursor;
// Cross confirms (A), Circle goes back (B), and holding R1 (B) while pressing a D-pad direction
// assigns the highlighted item to that C button.
#include <libultraship/bridge/consolevariablebridge.h>
#include "SoulsInput.h"
#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "z64player.h"
extern PlayState* gPlayState;
}

// Cross pressed in play: jump on the next item-button check (within 2 gameplay frames)
static bool sJumpPending = false;
static u32 sJumpFrame = 0;
static bool sCrossWasDown = false;

#define CVAR_SOULS_CONTROLS CVAR_ENHANCEMENT("SoulsControls")
#define BTN_SOULS_MOD1 0x0040 // BTN_CUSTOM_MODIFIER1 (Circle also sends it)
#define DPAD_BITS (BTN_DUP | BTN_DDOWN | BTN_DLEFT | BTN_DRIGHT)

static bool SoulsInput_InMenu() {
    if (gPlayState == nullptr) {
        return true; // title screen, file select, name entry
    }
    if (gPlayState->pauseCtx.state != 0 || gPlayState->pauseCtx.debugState != 0) {
        return true;
    }
    u8 mode = gPlayState->msgCtx.msgMode;
    if (mode == MSGMODE_NONE) {
        return false;
    }
    // The ocarina (playing, songs, scarecrow) uses the C buttons: treat it as play.
    if (mode >= MSGMODE_OCARINA_STARTING && mode <= MSGMODE_SCARECROW_LONG_RECORDING_START) {
        return false;
    }
    return true;
}

// D-pad bits -> the matching C bits
static u16 SoulsInput_DpadToC(u16 b) {
    u16 out = 0;
    if (b & BTN_DUP) out |= BTN_CUP;
    if (b & BTN_DDOWN) out |= BTN_CDOWN;
    if (b & BTN_DLEFT) out |= BTN_CLEFT;
    if (b & BTN_DRIGHT) out |= BTN_CRIGHT;
    return out;
}

extern "C" void SoulsInput_Transform(u16* buttons) {
    if (!CVarGetInteger(CVAR_SOULS_CONTROLS, 0)) {
        return;
    }
    u16 in = *buttons;
    u16 out = in & ~(DPAD_BITS | BTN_L);

    if (!SoulsInput_InMenu()) {
        out |= SoulsInput_DpadToC(in);
        bool cross = (in & BTN_L) != 0;
        if (cross && !sCrossWasDown && gPlayState != nullptr) {
            sJumpPending = true;
            sJumpFrame = gPlayState->gameplayFrames;
        }
        sCrossWasDown = cross;
        *buttons = out;
        return;
    }

    // Menus
    if ((in & BTN_B) && (in & DPAD_BITS)) {
        // R1 + direction: equip the highlighted item to that C button
        out &= ~BTN_B;
        out |= SoulsInput_DpadToC(in);
    } else {
        out |= in & DPAD_BITS;
    }
    if (in & BTN_SOULS_MOD1) {
        // Circle (sends A + Modifier 1): back
        out &= ~(BTN_A | BTN_SOULS_MOD1);
        out |= BTN_B;
    }
    if (in & BTN_L) {
        out |= BTN_A; // Cross: confirm
    }
    sCrossWasDown = (in & BTN_L) != 0;
    sJumpPending = false;
    *buttons = out;
}

static void RegisterSoulsJump() {
    COND_VB_SHOULD(VB_OVERRIDE_BUTTON_ITEM_USED, CVarGetInteger(CVAR_SOULS_CONTROLS, 0), {
        s32* i = va_arg(args, s32*);
        Player* player = va_arg(args, Player*);
        s32* item = va_arg(args, s32*);
        (void)i;
        (void)player;
        if (!sJumpPending) {
            return;
        }
        sJumpPending = false;
        if (gPlayState != nullptr && gPlayState->gameplayFrames - sJumpFrame <= 2 && *item >= ITEM_NONE_FE) {
            *item = ITEM_ROCS_FEATHER; // handled by the Roc's Feather hook: jump, no held item change
        }
    });
}

static RegisterShipInitFunc initSoulsJump(RegisterSoulsJump, { CVAR_SOULS_CONTROLS });
