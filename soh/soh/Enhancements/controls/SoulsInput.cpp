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
// In play: the D-pad acts as the C buttons (items, Navi, ocarina notes) and Cross jumps. The jump is the game's own
// ledge jump (the running jump when Link runs forward, the standing jump otherwise, his normal jump voice), started
// from the item-button check, so the game's rules apply (no jump while carrying something or in a cutscene). One jump
// until Link has been on the ground for 3 frames; it can still grab ledges.
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
#include "functions.h"
#include "variables.h"
#include "objects/gameplay_keep/gameplay_keep.h"
extern PlayState* gPlayState;
}

// Cross pressed in play: jump on the next item-button check (within 2 gameplay frames)
static bool sJumpPending = false;
static u32 sJumpFrame = 0;
static bool sCrossWasDown = false;
static bool sJumpUsed = false;
static uint8_t sGroundFrames = 0;

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
        if (gPlayState == nullptr || gPlayState->gameplayFrames - sJumpFrame > 2 || *item < ITEM_NONE_FE || sJumpUsed) {
            return;
        }
        if (!(player->actor.bgCheckFlags & 1) ||
            (player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_HANGING_OFF_LEDGE))) {
            return; // only from the ground
        }
        sJumpUsed = true;
        s16 yawDiff = player->yaw - player->actor.shape.rot.y;
        bool running = ABS(yawDiff) < 0x1000 && player->linearVelocity > 4.0f;
        func_80838940(player,
                      (LinkAnimationHeader*)(running ? gPlayerAnim_link_normal_run_jump : gPlayerAnim_link_normal_jump),
                      5.8f, gPlayState, NA_SE_VO_LI_AUTO_JUMP);
        player->av2.actionVar2 = 1; // no animation morph glitch (same as the game's ledge jump)
        player->actor.velocity.y = (gSaveContext.linkAge == LINK_AGE_CHILD) ? 7.0f : 7.5f;
        player->stateFlags2 &= ~PLAYER_STATE2_HOPPING; // ledges can still be grabbed
    });
}

static void RegisterSoulsJumpReset() {
    // One jump per landing: Link may jump again after 3 frames on the ground
    COND_HOOK(OnPlayerUpdate, CVarGetInteger(CVAR_SOULS_CONTROLS, 0), []() {
        Player* player = GET_PLAYER(gPlayState);
        if (player->actor.bgCheckFlags & 1) {
            if (sGroundFrames < 3) {
                sGroundFrames++;
            }
        } else {
            sGroundFrames = 0;
        }
        if (sGroundFrames >= 3) {
            sJumpUsed = false;
        }
    });
}

static RegisterShipInitFunc initSoulsJump(RegisterSoulsJump, { CVAR_SOULS_CONTROLS });
static RegisterShipInitFunc initSoulsJumpReset(RegisterSoulsJumpReset, { CVAR_SOULS_CONTROLS });
