// Souls of Harkinian: one controller layout for play and another for menus (see souls/ER-SYSTEM.md section 1).
//
// Expected bindings (Settings > Controls), all on port 1. Every PlayStation button needs its own N64 button, because
// the game only sees N64 buttons:
//   d-pad      -> N64 D-pad          Cross    -> Modifier 2     Square -> C-left
//   Triangle   -> A                  Circle   -> A + Modifier 1 R1     -> B
//   R2         -> C-right            L1       -> R              L2     -> C-down
//   L3, Create -> C-up               R3       -> Z              Touchpad -> L
//   Options    -> Start
//
// In play the loadout (SoulsLoadout) decides what the buttons do. Each frame the current items are copied into the
// game's own button slots, so the game's item code (aiming, ammo, bottles, magic, age rules) works as for a C button:
//   C-left  = the current quick item (Square)
//   C-right = the right-hand tool (Megaton Hammer, Deku Stick) or the current spell if the right hand holds the catalyst
//   C-down  = the left-hand tool (Slingshot, Bow, Hookshot, Longshot, Boomerang) or the spell if it holds the catalyst
//   B       = the sword of the right hand (the equipped sword is switched to match)
//   R       = guard (L2)
// R1 presses the right hand (B for a sword, C-right for a tool or spell); holding it charges the spin attack.
// R2: a tap with a sword drawn or sheathed is the game's jump slash (the attack the A button does while Z-targeting
// with a sword; it works without a target); holding R2 acts as R1 (swing, then the spin charge).
// L1 presses the left-hand item (C-down: tool or spell). L2 guards with the equipped shield (the game's R button;
// the shield is gear on the Equipment page, not a slot). D-pad right / left / up / down switch the
// right hand / left hand / spell / quick item. Triangle is A; while it is held the pouch shows and the D-pad does not
// switch. The touchpad is L (the game's minimap toggle), the only button that sends L in play. L3 and Create are C-up (first person, Navi), R3 is Z (lock-on).
// The pause screen and text boxes keep the older layout: R2 = B, L1 and L3 = R, L2 and R3 = Z.
//
// Cross jumps. The jump is the game's own ledge jump (the running jump when Link runs forward, the standing jump
// otherwise, his normal jump voice), started from the item-button check, so the game's rules apply (no jump while
// carrying something or in a cutscene). One jump until Link has been on the ground for 3 frames; it can still grab
// ledges.
// In menus (pause screen, file select / title, text boxes): the D-pad stays the D-pad, so with
// "D-pad Support on Pause Screen" and "D-pad Support in Text Boxes" on it moves the cursor;
// Cross confirms (A), Circle goes back (B), and holding R1 (B) while pressing a D-pad direction
// assigns the highlighted item to that C button.
#include <libultraship/bridge/consolevariablebridge.h>
#include <chrono>
#include "SoulsInput.h"
#include "SoulsLoadout.h"
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
extern u8 gItemSlots[];
void Interface_LoadItemIcon1(PlayState* play, u16 button);
void Player_UseItem(PlayState* play, Player* player, s32 item);
int Player_CanUpdateItems(Player* player);
void func_8083BA90(PlayState* play, Player* player, s32 arg2, f32 xzVelocity, f32 yVelocity); // start a jump slash
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

#define BTN_ALL_C (BTN_CUP | BTN_CDOWN | BTN_CLEFT | BTN_CRIGHT)

static bool SoulsInput_InOcarina() {
    if (gPlayState == nullptr) {
        return false;
    }
    u8 mode = gPlayState->msgCtx.msgMode;
    return mode >= MSGMODE_OCARINA_STARTING && mode <= MSGMODE_SCARECROW_LONG_RECORDING_START;
}

static bool IsSword(u8 item) {
    return item == ITEM_SWORD_KOKIRI || item == ITEM_SWORD_MASTER || item == ITEM_SWORD_BGS ||
           item == ITEM_SWORD_KNIFE;
}

static bool IsShield(u8 item) {
    return item >= ITEM_SHIELD_DEKU && item <= ITEM_SHIELD_MIRROR;
}

// The current item of a hand, or ITEM_NONE if it cannot be used now (not owned, wrong age)
static u8 UsableCurrent(SoulsLoadoutKind kind) {
    u8 item = SoulsLoadout_Current(kind);
    return (item != ITEM_NONE && SoulsLoadout_CanEquip(kind, item)) ? item : (u8)ITEM_NONE;
}

// Input state (all in play)
static u16 sPrevDpad = 0;
static bool sPrevR1 = false, sPrevL1 = false, sPrevSquare = false;
static bool sPrevR2 = false;
static std::chrono::steady_clock::time_point sR2Start;
static bool sSlashPending = false; // R2 tapped: jump slash on the next item-button check
static u32 sSlashFrame = 0;
static bool sForceAttack = false; // press the right-hand attack for a few game frames
static u32 sForceAttackFrame = 0;
static const int kR2HoldMs = 160; // a R2 press shorter than this is a tap

// The player is in the middle of using an item (a bottle being drunk, an item change, an item cutscene). The game's
// item code reads the C buttons then (the bottle is emptied in cButtonSlots[heldItemButton - 1]), so they stay as they are.
static bool ItemInUse(Player* player) {
    if (player == nullptr) {
        return false;
    }
    return (player->heldItemAction >= PLAYER_IA_BOTTLE && player->heldItemAction <= PLAYER_IA_BOTTLE_FAIRY) ||
           (player->itemAction >= PLAYER_IA_BOTTLE && player->itemAction <= PLAYER_IA_BOTTLE_FAIRY) ||
           (player->stateFlags1 & (PLAYER_STATE1_START_CHANGING_HELD_ITEM | PLAYER_STATE1_GETTING_ITEM));
}

static bool ItemInUseNow() {
    return gPlayState != nullptr && ItemInUse(GET_PLAYER(gPlayState));
}

static u32 GameFrame() {
    return gPlayState != nullptr ? gPlayState->gameplayFrames : 0;
}

// Play layout. Returns the N64 buttons the game gets.
static u16 SoulsInput_Play(u16 in) {
    // Cross (Modifier 2) is handled by the caller; the touchpad (L) passes through as the minimap toggle
    u16 out = in & ~(DPAD_BITS | BTN_B | BTN_R | BTN_Z | BTN_ALL_C | BTN_CUSTOM_MODIFIER2);
    bool triangle = (in & BTN_A) && !(in & BTN_SOULS_MOD1);
    bool r2 = (in & BTN_CRIGHT) != 0;
    auto now = std::chrono::steady_clock::now();
    if (r2 && !sPrevR2) {
        sR2Start = now;
    }
    bool r2Held = r2 && std::chrono::duration_cast<std::chrono::milliseconds>(now - sR2Start).count() >= kR2HoldMs;
    bool temporaryB = SoulsLoadout_TemporaryB(); // a minigame item is on B: R1 and R2 both press B
    bool r1 = (in & BTN_B) || r2Held || (r2 && temporaryB) || (sForceAttack && GameFrame() - sForceAttackFrame <= 8);
    if (!r2 && sPrevR2 && !temporaryB &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - sR2Start).count() < kR2HoldMs) {
        // R2 tap: the jump slash with a sword, else the same as R1
        if (IsSword(UsableCurrent(SL_RIGHT))) {
            sSlashPending = true;
            sSlashFrame = GameFrame();
        } else {
            sForceAttack = true;
            sForceAttackFrame = GameFrame();
        }
    }
    sPrevR2 = r2;
    bool l1 = (in & BTN_R) != 0;  // L1
    bool square = (in & BTN_CLEFT) != 0;
    bool l2 = (in & BTN_CDOWN) != 0;  // L2
    SoulsLoadout_SetPouchHeld(triangle);

    // D-pad: switch hands, spell, quick item (not while Triangle is held: that is the pouch)
    u16 pressed = (in & DPAD_BITS) & ~sPrevDpad;
    sPrevDpad = in & DPAD_BITS;
    if (!triangle && !ItemInUseNow()) {
        if (pressed & BTN_DRIGHT) SoulsLoadout_Cycle(SL_RIGHT, 1);
        if (pressed & BTN_DLEFT) SoulsLoadout_Cycle(SL_LEFT, 1);
        if (pressed & BTN_DUP) SoulsLoadout_Cycle(SL_SPELL, 1);
        if (pressed & BTN_DDOWN) SoulsLoadout_Cycle(SL_QUICK, 1);
    }

    u8 right = UsableCurrent(SL_RIGHT);
    u8 left = UsableCurrent(SL_LEFT);

    // Right hand: a sword is the game's B button, any other item (or the spell) is on C-right
    if (r1 && temporaryB) {
        out |= BTN_B; // horseback archery, shooting galleries, Bombchu Bowling, fishing rod: B is the minigame item
    } else if (r1 && right != ITEM_NONE) {
        out |= IsSword(right) ? BTN_B : BTN_CRIGHT;
        if (!sPrevR1) SoulsLoadout_NoteUse(SL_RIGHT);
    }
    // Left hand (L1): a ranged tool or the catalyst, on C-down (hold to aim, release to fire; the spell casts)
    if (l1 && left != ITEM_NONE) {
        out |= BTN_CDOWN;
        if (!sPrevL1) SoulsLoadout_NoteUse(SL_LEFT);
    }
    // L2: guard with the equipped shield (the game's R button), always available
    if (l2) out |= BTN_R;
    // Square: the quick item, on C-left
    if (square) {
        out |= BTN_CLEFT;
        if (!sPrevSquare && UsableCurrent(SL_QUICK) != ITEM_NONE) SoulsLoadout_NoteUse(SL_QUICK);
    }
    sPrevR1 = r1;
    sPrevL1 = l1;
    sPrevSquare = square;

    // L3 and Create: first person look / Navi (the game decides). R3: lock-on.
    if (in & BTN_CUP) out |= BTN_CUP;
    if (in & BTN_Z) out |= BTN_Z;
    return out;
}

// The pad buttons before the Souls layout changes them (the Equipment page tells Triangle from Cross with this:
// in menus both become A).
extern "C" {
uint16_t gSoulsRawPadButtons = 0;
}

extern "C" void SoulsInput_Transform(u16* buttons) {
    if (!CVarGetInteger(CVAR_SOULS_CONTROLS, 0)) {
        return;
    }
    u16 in = *buttons;
    gSoulsRawPadButtons = in;

    if (!SoulsInput_InMenu() && !SoulsInput_InOcarina()) {
        bool cross = (in & BTN_CUSTOM_MODIFIER2) != 0;
        if (cross && !sCrossWasDown && gPlayState != nullptr) {
            sJumpPending = true;
            sJumpFrame = gPlayState->gameplayFrames;
        }
        sCrossWasDown = cross;
        *buttons = SoulsInput_Play(in);
        return;
    }
    SoulsLoadout_SetPouchHeld(false);
    sPrevDpad = in & DPAD_BITS;
    sPrevR1 = sPrevL1 = sPrevSquare = sPrevR2 = false;
    sSlashPending = false;

    if (SoulsInput_InOcarina()) {
        // The ocarina: the D-pad plays the C notes, Triangle is A
        sCrossWasDown = (in & BTN_CUSTOM_MODIFIER2) != 0;
        sJumpPending = false;
        *buttons = (in & ~(DPAD_BITS | BTN_CUSTOM_MODIFIER2)) | SoulsInput_DpadToC(in);
        return;
    }

    // Menus: R2 = B, L2 = Z, L3 = R, as before the loadout. Cross (Modifier 2) confirms; the touchpad does nothing.
    bool crossDown = (in & BTN_CUSTOM_MODIFIER2) != 0;
    in &= ~(BTN_CUSTOM_MODIFIER2 | BTN_L);
    if (crossDown) in |= BTN_L; // below, BTN_L means Cross
    u16 conv = in & ~(BTN_CRIGHT | BTN_CDOWN | BTN_CUP);
    if (in & BTN_CRIGHT) conv |= BTN_B;
    if (in & BTN_CDOWN) conv |= BTN_Z;
    if (in & BTN_CUP) conv |= BTN_R;
    in = conv;
    u16 out = in & ~(DPAD_BITS | BTN_L);
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
        if (sSlashPending) {
            if (gPlayState == nullptr || GameFrame() - sSlashFrame > 4) {
                sSlashPending = false;
            } else if (*item >= ITEM_NONE_FE) {
                bool ground = (player->actor.bgCheckFlags & 1) != 0;
                bool blocked = (player->stateFlags1 & (PLAYER_STATE1_SHIELDING | PLAYER_STATE1_CHARGING_SPIN_ATTACK |
                                                       PLAYER_STATE1_CARRYING_ACTOR | PLAYER_STATE1_IN_WATER |
                                                       PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_HANGING_OFF_LEDGE |
                                                       PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_FIRST_PERSON |
                                                       PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_CUTSCENE |
                                                       PLAYER_STATE1_START_CHANGING_HELD_ITEM)) ||
                               (player->stateFlags3 & PLAYER_STATE3_MIDAIR) || player->meleeWeaponState != 0;
                if (ground && !blocked && Player_CanUpdateItems(player)) {
                    sSlashPending = false;
                    if (Player_GetMeleeWeaponHeld(player) != 0) {
                        func_8083BA90(gPlayState, player, PLAYER_MWA_JUMPSLASH_START, 5.0f, 5.0f);
                    } else {
                        // The sword is sheathed: the plain attack draws it and swings
                        sForceAttack = true;
                        sForceAttackFrame = GameFrame();
                    }
                    return;
                }
            }
        }
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

// MARK: - Loadout -> the game's button slots (every player update)

static void SetButton(int button, u8 item, u8 slot) {
    if (item < ITEM_NONE_FE && (slot == SLOT_NONE || slot >= 24)) {
        item = ITEM_NONE; // the game writes items[cButtonSlots[x]]: a real item always needs a real slot
        slot = SLOT_NONE;
    }
    gSaveContext.equips.buttonItems[button] = item;
    gSaveContext.equips.cButtonSlots[button - 1] = slot;
}

// The arrow item on the bow button for the chosen arrow slot (plain bow if none, no magic or not owned)
static u8 BowItemForArrow(u8 arrow, u8* slot) {
    *slot = SLOT_BOW;
    if (arrow == ITEM_NONE || !gSaveContext.isMagicAcquired || gSaveContext.magic == 0 ||
        !SoulsLoadout_CanEquip(SL_ARROW, arrow)) {
        return ITEM_BOW;
    }
    *slot = gItemSlots[arrow];
    return arrow == ITEM_ARROW_FIRE ? ITEM_BOW_ARROW_FIRE : (arrow == ITEM_ARROW_ICE ? ITEM_BOW_ARROW_ICE : ITEM_BOW_ARROW_LIGHT);
}

// What a hand puts on its C button: the spell if it holds the catalyst, else the tool. Shields and swords put nothing.
static void ToolOnButton(u8 hand, u8* item, u8* slot) {
    *item = ITEM_NONE;
    *slot = SLOT_NONE;
    if (hand == ITEM_NONE || IsSword(hand) || IsShield(hand)) {
        return;
    }
    if (hand == SOULS_ITEM_CATALYST) {
        u8 spell = UsableCurrent(SL_SPELL);
        if (spell != ITEM_NONE) {
            *item = spell;
            *slot = gItemSlots[spell];
        }
    } else if (hand == ITEM_BOW) {
        *item = BowItemForArrow(UsableCurrent(SL_ARROW), slot);
    } else {
        *item = hand;
        *slot = gItemSlots[hand];
    }
}

static void QuickOnButton(u8* item, u8* slot) {
    *item = ITEM_NONE;
    *slot = SLOT_NONE;
    u8 quick = UsableCurrent(SL_QUICK);
    if (quick == ITEM_NONE || quick == SOULS_ITEM_EPONA) {
        return;
    }
    if (quick >= ITEM_BOTTLE && quick <= ITEM_POE) {
        for (int s = SLOT_BOTTLE_1; s <= SLOT_BOTTLE_4; s++) {
            if (gSaveContext.inventory.items[s] == quick) {
                *item = quick;
                *slot = s;
                return;
            }
        }
        return;
    }
    *item = quick;
    *slot = gItemSlots[quick];
}

// If the active slot of a list cannot be used now (age, item gone) move to a slot that can
static void SkipUnusable(SoulsLoadoutKind kind) {
    u8 cur = SoulsLoadout_Current(kind);
    if (cur == ITEM_NONE || SoulsLoadout_CanEquip(kind, cur)) {
        return;
    }
    for (int i = 0; i < SoulsLoadout_Capacity(kind); i++) {
        u8 item = SoulsLoadout_Get(kind, i);
        if (item != ITEM_NONE && SoulsLoadout_CanEquip(kind, item)) {
            SoulsLoadout_Cycle(kind, 1);
            return;
        }
    }
}

static void EquipSword(PlayState* play, Player* player, u8 sword) {
    int value = sword == ITEM_SWORD_KOKIRI ? EQUIP_VALUE_SWORD_KOKIRI
                                           : (sword == ITEM_SWORD_MASTER ? EQUIP_VALUE_SWORD_MASTER : EQUIP_VALUE_SWORD_BIGGORON);
    gSaveContext.equips.buttonItems[0] = sword;
    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, value);
    gSaveContext.infTable[29] = 0;
    if (sword == ITEM_SWORD_BGS) {
        gSaveContext.swordHealth = 8;
    }
    Interface_LoadItemIcon1(play, 0);
    Player_SetEquipmentData(play, player);
}

static void SyncLoadout() {
    if (gPlayState == nullptr) {
        return;
    }
    Player* player = GET_PLAYER(gPlayState);
    if (player == nullptr) {
        return;
    }
    // The player's own buttons are kept first; during minigames (B holds a temporary item) and while an item is in
    // use (a bottle being drunk) the buttons are left alone
    if (!SoulsLoadout_ButtonItemsStashed() || SoulsLoadout_TemporaryB() || ItemInUse(player)) {
        return;
    }
    // The hookshot slot turns into the longshot when it is upgraded
    for (int i = 0; i < SoulsLoadout_Capacity(SL_LEFT); i++) {
        if (SoulsLoadout_Get(SL_LEFT, i) == ITEM_HOOKSHOT && INV_CONTENT(ITEM_HOOKSHOT) == ITEM_LONGSHOT) {
            SoulsLoadout_Set(SL_LEFT, i, ITEM_LONGSHOT);
        }
    }
    for (int k = SL_RIGHT; k <= SL_QUICK; k++) {
        SkipUnusable((SoulsLoadoutKind)k);
    }

    // Sword and shield follow the hands (not in the middle of a swing or a guard)
    bool busy = player->meleeWeaponState != 0 || (player->stateFlags1 & (PLAYER_STATE1_SHIELDING | PLAYER_STATE1_IN_CUTSCENE)) ||
                gPlayState->csCtx.state != CS_STATE_IDLE;
    u8 right = UsableCurrent(SL_RIGHT);
    u8 left = UsableCurrent(SL_LEFT);
    if (!busy) {
        if (IsSword(right) && gSaveContext.equips.buttonItems[0] != right) {
            if (player->heldItemAction >= PLAYER_IA_SWORD_MASTER && player->heldItemAction <= PLAYER_IA_SWORD_BIGGORON) {
                // A drawn sword: put it away first (the game's own item change), then switch the sword
                if (player->heldItemAction == player->itemAction) {
                    Player_UseItem(gPlayState, player, ITEM_NONE);
                }
            } else {
                EquipSword(gPlayState, player, right);
            }
        }
    }

    u8 item, slot;
    QuickOnButton(&item, &slot);
    SetButton(1, item, slot);
    ToolOnButton(left, &item, &slot);
    SetButton(2, item, slot);
    ToolOnButton(right, &item, &slot);
    SetButton(3, item, slot);
}

static void RegisterSoulsLoadoutSync() {
    COND_HOOK(OnPlayerUpdate, CVarGetInteger(CVAR_SOULS_CONTROLS, 0), []() { SyncLoadout(); });
}

static RegisterShipInitFunc initSoulsLoadoutSync(RegisterSoulsLoadoutSync, { CVAR_SOULS_CONTROLS });
