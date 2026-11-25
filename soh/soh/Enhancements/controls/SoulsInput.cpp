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
//   C-right = the right-hand item (any armament that is not a sword) or the current spell if that hand holds the catalyst
//   C-down  = the left-hand item (the same) or the spell if that hand holds the catalyst
//   B       = the sword of the hand pressed last (the equipped sword is switched to match)
//   R       = guard (L2)
// R1 presses the right hand (B for a sword, C-right for any other item or the spell); holding it charges the spin attack.
// R2: a tap with a sword drawn or sheathed is the game's jump slash (the attack the A button does while Z-targeting
// with a sword; it works without a target); holding R2 acts as R1 (swing, then the spin charge).
// L1 presses the left hand (B for a sword, C-down for any other item or the spell). Any armament goes in either hand. L2 guards with the equipped shield (the game's R button;
// the shield is gear on the Equipment page, not a slot). D-pad right / left / up / down switch the
// right hand / left hand / spell / quick item. Triangle is A (it fires on press, as before, so talking, opening and
// grabbing keep their timing); while it is held the pouch shows and the D-pad uses a pouch slot instead of switching
// (up, left, right, down = pouch 1-4). A pouch use ends the A press, so a pouch item never also holds A. The touchpad is L (the game's minimap toggle), the only button that sends L in play. L3 and Create are C-up (first person, Navi), R3 is Z (lock-on).
// The pause screen and text boxes keep the older layout: R2 = B, L1 and L3 = R, L2 and R3 = Z.
// The ocarina (playing mode): Cross = A note, D-pad = C-up / C-down / C-left / C-right notes, Circle = B (puts it away).
// Square with Epona as the quick item takes the ocarina out and presses her song's notes for the player (EponaTick).
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
s8 Player_ItemToItemAction(s32 item);
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
    // The ocarina (playing, songs, scarecrow, memory game, frogs) uses the C buttons: treat it as play.
    if (mode >= MSGMODE_OCARINA_STARTING && mode <= MSGMODE_FROGS_WAITING) {
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
    return mode >= MSGMODE_OCARINA_STARTING && mode <= MSGMODE_FROGS_WAITING;
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

static bool IsDrawItem(u8 item) { // an item that comes out into Link's hand when it is switched to
    return item == ITEM_SWORD_KOKIRI || item == ITEM_SWORD_MASTER || item == ITEM_SWORD_BGS || item == ITEM_STICK ||
           item == ITEM_HAMMER;
}

// Link holds this item now (sword drawn, Deku Stick or Hammer in hand)
static bool HandItemOut(Player* player, u8 item) {
    if (player == nullptr || !IsDrawItem(item)) {
        return false;
    }
    return player->heldItemAction == Player_ItemToItemAction(item);
}

// Input state (all in play)
static u16 sPrevDpad = 0;
static bool sPrevR1 = false, sPrevL1 = false, sPrevSquare = false;
static bool sPrevR2 = false;
static std::chrono::steady_clock::time_point sR2Start;
static bool sSlashPending = false; // R2 tapped: jump slash on the next item-button check
static u32 sSlashFrame = 0;
static bool sForceAttack = false; // press the right-hand attack for a few game frames
// Attack while guarding: the shield comes down for the attack and goes back up after it while L2 is still held
static bool sPrevAttack = false, sGuardWasUp = false;
static u32 sAttackStartFrame = 0, sAttackEndFrame = 0;
// Two sword hands: the hand whose sword is on the game's B (0 = right, 1 = left). Pressing the other hand's button
// switches the sword; the swing waits for the switch (sPendHand) and then goes out as a short B press.
static int sSwordHand = 0;
static int sPendHand = -1;
static u32 sPendFrame = 0, sPendOutUntil = 0;
static int sPendOutHand = -1;
static const u32 kPendMaxFrames = 60, kPendOutFrames = 6;
// Switching what is in Link's hand: when the old item is out (sword drawn, Deku Stick, Hammer), the new one comes out
// ready, by the game's own item change, and the change does not end with a swing (sNoSlashLeft, see the hook below).
static bool sDrawReq = false;       // a D-pad switch asked for this item to come out (ITEM_NONE = put the old one away)
static u8 sDrawReqItem = ITEM_NONE;
static u32 sDrawReqFrame = 0;
static int sNoSlashLeft = 0;        // frames left in which the change to sNoSlashItem does not end with a swing
static u8 sNoSlashItem = ITEM_NONE;
static bool sSlashWait = false;     // R2 tapped, the right-hand sword is not on B yet: jump slash once it is
static u32 sSlashWaitFrame = 0;
static const u32 kGuardResumeFrames = 8; // game frames after the attack input before the guard comes back
static u32 sForceAttackFrame = 0;
static const int kR2HoldMs = 160; // a R2 press shorter than this is a tap

// Pouch use: the item goes on C-left for a few game frames and C-left is pressed, so the game's own item code runs
// (ocarina, lens, beans, masks, trade items). The boot toggles change the boots directly.
static u8 sPouchItem = ITEM_NONE;
static u32 sPouchFrame = 0;
static bool sPouchActive = false;
// Triangle: A is held back for kTapWindowMs so that a D-pad press in that window is a pouch use and never also A. A tap
// (released inside the window) sends A for a game frame; a hold sends A from the end of the window until release.
static bool sTriPrev = false, sASent = false, sTriUsed = false, sTapActive = false;
static std::chrono::steady_clock::time_point sTriStart, sTapStart;
static u32 sTapFrame = 0;
static const int kTapWindowMs = 70;
static bool sMenuTriPrev = false; // Triangle in a text box
static std::chrono::steady_clock::time_point sMenuTriStart;
static const int kTextSkipHoldMs = 300;
// Swimming (not walking with the Iron Boots) or a dive offered: Triangle is a plain A (the game dives while A is
// held) and the pouch is off
static bool TriangleIsPlainA() {
    if (gPlayState == nullptr) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    u16 action = gPlayState->interfaceCtx.unk_1F0;
    bool diveOffered = action == DO_ACTION_DIVE || (action >= DO_ACTION_1 && action <= DO_ACTION_8);
    bool swimming = (player->stateFlags1 & PLAYER_STATE1_IN_WATER) && player->currentBoots != PLAYER_BOOTS_IRON &&
                    player->currentBoots != PLAYER_BOOTS_IRON_UNDERWATER;
    return diveOffered || swimming || (player->stateFlags2 & (PLAYER_STATE2_DIVING | PLAYER_STATE2_UNDERWATER));
}

static const int kTapMaxMs = 350; // a longer Triangle hold that ends without a D-pad press is a look at the pouch, not A

// The action shown on Triangle needs A held down (grab a block to push or pull it)
static bool TriangleHoldNeedsA() {
    return gPlayState != nullptr && gPlayState->interfaceCtx.unk_1F0 == DO_ACTION_GRAB;
}
static const u32 kPouchFrames = 3;
static void SetButton(int button, u8 item, u8 slot);

static u16 sHeldAtStart = 0;   // buttons held when the ocarina came out
static bool sWasOcarina = false;

// Epona's Song played for the player: the ocarina comes out (as from the pouch), then C-up, C-left, C-right twice.
enum { EPONA_OFF, EPONA_TAKE_OUT, EPONA_WAIT_STAFF, EPONA_PLAY, EPONA_DONE };
static int sEponaStage = EPONA_OFF;
static u32 sEponaFrame = 0;
static int sEponaNote = 0;
static const u16 kEponaNotes[6] = { BTN_CUP, BTN_CLEFT, BTN_CRIGHT, BTN_CUP, BTN_CLEFT, BTN_CRIGHT };
static const u32 kEponaNoteFrames = 3; // a note is held this many game frames, then released for as many

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

// The game's item change runs over several frames: the held item (heldItemId) is the new one while the held action
// is still the old one. (PLAYER_STATE1_START_CHANGING_HELD_ITEM is cleared the frame after the change starts.)
static bool ItemChanging(Player* player) {
    return player != nullptr && Player_ItemToItemAction(player->heldItemId) != player->heldItemAction;
}

// This sword is on B. The game puts the broken Giant's Knife on B when it breaks, which counts as the Giant's Knife.
static bool SwordOnB(u8 sword) {
    u8 b = gSaveContext.equips.buttonItems[0];
    return b == sword || (sword == ITEM_SWORD_BGS && b == ITEM_SWORD_KNIFE);
}

static u32 GameFrame() {
    return gPlayState != nullptr ? gPlayState->gameplayFrames : 0;
}

static u8 PouchSlotOf(u8 item) {
    if (item >= ITEM_LETTER_ZELDA && item <= ITEM_CLAIM_CHECK) {
        return gSaveContext.inventory.items[SLOT_TRADE_ADULT] == item ? (u8)SLOT_TRADE_ADULT : (u8)SLOT_TRADE_CHILD;
    }
    return gItemSlots[item];
}

static bool PouchCanUseNow(u8 item) {
    bool ocarina = item == ITEM_OCARINA_FAIRY || item == ITEM_OCARINA_TIME;
    if (ocarina && gPlayState != nullptr && gPlayState->interfaceCtx.restrictions.ocarina) {
        return false; // the scene does not allow it (the button status is from the last frame)
    }
    return gPlayState != nullptr && SoulsLoadout_ButtonItemsStashed() && !SoulsLoadout_TemporaryB() &&
           !ItemInUseNow() && gPlayState->csCtx.state == CS_STATE_IDLE;
}

// Use an item as if it was on a C button (the ocarina, lens, beans, masks, trade items)
static bool PouchPressItem(u8 item) {
    if (!PouchCanUseNow(item)) {
        return false;
    }
    sPouchItem = item;
    sPouchFrame = GameFrame();
    sPouchActive = true;
    SetButton(1, item, PouchSlotOf(item));
    return true;
}

static void PouchUse(int slot) {
    u8 item = SoulsLoadout_Get(SL_POUCH, slot);
    if (item == ITEM_NONE || !SoulsLoadout_CanEquip(SL_POUCH, item)) {
        return;
    }
    if (item == SOULS_ITEM_TOGGLE_IRON || item == SOULS_ITEM_TOGGLE_HOVER) {
        if (gPlayState == nullptr || ItemInUseNow() || gPlayState->csCtx.state != CS_STATE_IDLE) {
            return;
        }
        int want = item == SOULS_ITEM_TOGGLE_IRON ? EQUIP_VALUE_BOOTS_IRON : EQUIP_VALUE_BOOTS_HOVER;
        int boots = CUR_EQUIP_VALUE(EQUIP_TYPE_BOOTS) == want ? EQUIP_VALUE_BOOTS_KOKIRI : want;
        GameInteractor::RawAction::ForceEquipBoots(boots);
        Audio_PlaySfxGeneral(NA_SE_PL_CHANGE_ARMS, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                               &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
        SoulsLoadout_NoteUse(SL_POUCH);
        return;
    }
    if (PouchPressItem(item)) {
        SoulsLoadout_NoteUse(SL_POUCH);
    }
}

// Square with Epona as the quick item: take out the ocarina, then play her song
static void EponaStart() {
    if (sEponaStage != EPONA_OFF) {
        return;
    }
    u8 oca = INV_CONTENT(ITEM_OCARINA_TIME) == ITEM_OCARINA_TIME ? (u8)ITEM_OCARINA_TIME : (u8)ITEM_OCARINA_FAIRY;
    if (INV_CONTENT(oca) != oca || !PouchPressItem(oca)) {
        return;
    }
    sEponaStage = EPONA_TAKE_OUT;
    sEponaFrame = GameFrame();
    sEponaNote = 0;
    SoulsLoadout_NoteUse(SL_QUICK);
}

// The note buttons (C bits) Epona's Song presses now; also runs the stages
static u16 EponaTick(u16 in) {
    if (sEponaStage == EPONA_OFF) {
        return 0;
    }
    u32 now = GameFrame();
    bool ocarina = SoulsInput_InOcarina();
    // The player cancels with Circle or R1 (B)
    if ((in & BTN_B) || ((in & BTN_A) && (in & BTN_SOULS_MOD1)) ) {
        sEponaStage = EPONA_OFF;
        return 0;
    }
    switch (sEponaStage) {
        case EPONA_TAKE_OUT:
            if (ocarina) {
                sEponaStage = EPONA_WAIT_STAFF;
                sEponaFrame = now;
            } else if (now - sEponaFrame > 40) {
                sEponaStage = EPONA_OFF; // the ocarina did not come out (not allowed here)
            }
            return 0;
        case EPONA_WAIT_STAFF:
            if (!ocarina) {
                sEponaStage = EPONA_OFF;
            } else if (gPlayState->msgCtx.msgMode == MSGMODE_OCARINA_PLAYING && now - sEponaFrame > 8u) {
                sEponaStage = EPONA_PLAY;
                sEponaFrame = now;
            }
            return 0;
        case EPONA_PLAY: {
            if (!ocarina) {
                sEponaStage = EPONA_OFF;
                return 0;
            }
            u32 t = now - sEponaFrame;
            int note = (int)(t / (kEponaNoteFrames * 2));
            if (note >= 6) {
                sEponaStage = EPONA_DONE;
                sEponaFrame = now;
                return 0;
            }
            sEponaNote = note;
            return (t % (kEponaNoteFrames * 2)) < kEponaNoteFrames ? kEponaNotes[note] : (u16)0;
        }
        case EPONA_DONE:
            if (!ocarina || now - sEponaFrame > 100) {
                sEponaStage = EPONA_OFF;
            }
            return 0;
    }
    return 0;
}

// Play layout. Returns the N64 buttons the game gets.
static u16 SoulsInput_Play(u16 in) {
    // Cross (Modifier 2) is handled by the caller; the touchpad (L) passes through as the minimap toggle
    u16 out = in & ~(DPAD_BITS | BTN_B | BTN_R | BTN_Z | BTN_ALL_C | BTN_CUSTOM_MODIFIER2 | BTN_A);
    bool circle = (in & BTN_A) && (in & BTN_SOULS_MOD1);
    if (circle) out |= BTN_A; // Circle sends A + Modifier 1 (roll)
    bool triangle = (in & BTN_A) && !(in & BTN_SOULS_MOD1);
    bool r2 = (in & BTN_CRIGHT) != 0;
    auto now = std::chrono::steady_clock::now();
    if (r2 && !sPrevR2) {
        sR2Start = now;
    }
    if (sSlashWait) {
        Player* pl = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
        u8 rs = UsableCurrent(SL_RIGHT);
        if (SoulsLoadout_TemporaryB() || !IsSword(rs) || GameFrame() - sSlashWaitFrame > kPendMaxFrames) {
            sSlashWait = false;
        } else if (SwordOnB(rs) && !ItemChanging(pl)) {
            sSlashWait = false;
            sSlashPending = true;
            sSlashFrame = GameFrame();
        }
    }
    bool r2Held = r2 && std::chrono::duration_cast<std::chrono::milliseconds>(now - sR2Start).count() >= kR2HoldMs;
    bool temporaryB = SoulsLoadout_TemporaryB(); // a minigame item is on B: R1 and R2 both press B
    bool r1 = (in & BTN_B) || r2Held || (r2 && temporaryB) || (sForceAttack && GameFrame() - sForceAttackFrame <= 8);
    if (!r2 && sPrevR2 && !temporaryB &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - sR2Start).count() < kR2HoldMs) {
        // R2 tap: the jump slash with a sword, else the same as R1
        u8 rs = UsableCurrent(SL_RIGHT);
        if (IsSword(rs)) {
            sSwordHand = 0;
            sSlashWait = true; // the jump slash goes out as soon as the sword is on B and out of any change
            sSlashWaitFrame = GameFrame();
        } else {
            sForceAttack = true;
            sForceAttackFrame = GameFrame();
        }
    }
    sPrevR2 = r2;
    bool l1 = (in & BTN_R) != 0;  // L1
    bool square = (in & BTN_CLEFT) != 0;
    bool l2 = (in & BTN_CDOWN) != 0;  // L2
    bool plainA = TriangleIsPlainA();
    SoulsLoadout_SetPouchHeld(triangle && !plainA);

    // D-pad: switch hands, spell, quick item (not while Triangle is held: that is the pouch)
    u16 pressed = (in & DPAD_BITS) & ~sPrevDpad;
    sPrevDpad = in & DPAD_BITS;
    if (plainA) {
        // in the water: Triangle is A (press and hold: dive), no pouch
        if (triangle) {
            out |= BTN_A;
        }
        sTriPrev = triangle;
        sASent = triangle;
        sTriUsed = true; // a release here is not a tap
        sTapActive = false;
        triangle = false;
    }
    if (triangle && !sTriPrev) {
        sTriStart = now;
        sASent = false;
        sTriUsed = false;
        sTapActive = false;
    }
    if (triangle && (pressed & DPAD_BITS)) {
        // Triangle + D-pad: pouch 1-4 (up, left, right, down: the slots the HUD shows). A is never sent for this hold
        // when the press comes before A went out; after that A is released.
        sTriUsed = true;
        if (pressed & BTN_DUP) PouchUse(0);
        else if (pressed & BTN_DLEFT) PouchUse(1);
        else if (pressed & BTN_DRIGHT) PouchUse(2);
        else if (pressed & BTN_DDOWN) PouchUse(3);
    }
    bool sendA = false;
    if (triangle) {
        // A held Triangle only shows the pouch, so the player can look at it before choosing; A goes out during the
        // hold only when the action needs a held A (grab: push or pull a block). Everything else acts on a tap.
        if (!sTriUsed && !sASent && TriangleHoldNeedsA() &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - sTriStart).count() >= kTapWindowMs) {
            sASent = true;
        }
        sendA = sASent && !sTriUsed;
    } else {
        if (sTriPrev && !sTriUsed && !sASent &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - sTriStart).count() < kTapMaxMs) {
            sTapActive = true; // a tap: A for at least one game frame
            sTapStart = now;
            sTapFrame = GameFrame();
        }
        if (sTapActive) {
            if (GameFrame() == sTapFrame &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - sTapStart).count() < 120) {
                sendA = true;
            } else {
                sTapActive = false;
            }
        }
        sTriUsed = false;
        sASent = false;
    }
    if (sendA) out |= BTN_A;
    sTriPrev = triangle;
    if (!triangle && !ItemInUseNow()) {
        for (int hand = 0; hand < 2; hand++) {
            if (!(pressed & (hand == 0 ? BTN_DRIGHT : BTN_DLEFT))) {
                continue;
            }
            SoulsLoadoutKind kind = hand == 0 ? SL_RIGHT : SL_LEFT;
            Player* pl = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
            u8 old = UsableCurrent(kind);
            bool wasOut = HandItemOut(pl, old);
            if (sPendHand == hand) sPendHand = -1;
            if (sPendOutHand == hand) sPendOutUntil = 0;
            SoulsLoadout_Cycle(kind, 1);
            u8 now2 = UsableCurrent(kind);
            if (IsSword(now2)) {
                sSwordHand = hand; // the sword you chose goes on B
            }
            if (wasOut && now2 != old) {
                sDrawReq = true;
                sDrawReqItem = IsDrawItem(now2) ? now2 : (u8)ITEM_NONE; // a ranged tool or the catalyst stays away
                sDrawReqFrame = GameFrame();
            }
        }
        if (pressed & BTN_DUP) SoulsLoadout_Cycle(SL_SPELL, 1);
        if (pressed & BTN_DDOWN) SoulsLoadout_Cycle(SL_QUICK, 1);
    }

    u8 right = UsableCurrent(SL_RIGHT);
    u8 left = UsableCurrent(SL_LEFT);

    // Each hand has its own button: R1 = right hand, L1 = left hand. A sword is the game's B button (the equipped
    // sword is switched to that hand's sword first; the swing goes out once it is switched). Any other item (or the
    // spell, with the catalyst) is on the hand's C button: C-right for the right hand, C-down for the left hand.
    bool swordPressed = false; // a sword hand is swinging, or waits for its sword to be switched
    for (int hand = 0; hand < 2; hand++) {
        u8 item = hand == 0 ? right : left;
        bool pressed = hand == 0 ? r1 : l1;
        bool prev = hand == 0 ? sPrevR1 : sPrevL1;
        bool swordHand = IsSword(item);
        if (!swordHand || temporaryB || item == ITEM_NONE) { // nothing queued for a hand that holds no sword now
            if (sPendHand == hand) sPendHand = -1;
            if (sPendOutHand == hand) sPendOutUntil = 0;
        }
        if (hand == 0 && temporaryB) {
            if (pressed) out |= BTN_B; // horseback archery, shooting galleries, Bombchu Bowling, fishing rod
            continue;
        }
        if (item == ITEM_NONE) {
            continue;
        }
        if (pressed && !prev) SoulsLoadout_NoteUse(hand == 0 ? SL_RIGHT : SL_LEFT);
        if (!swordHand) {
            if (pressed) out |= hand == 0 ? BTN_CRIGHT : BTN_CDOWN;
            continue;
        }
        if (temporaryB) {
            continue; // the left hand on a horse or in a minigame: no swing is queued
        }
        Player* pl = gPlayState != nullptr ? GET_PLAYER(gPlayState) : nullptr;
        bool ready = SwordOnB(item) && !ItemChanging(pl); // not while the sword is changing
        if (pressed && !prev) {
            sSwordHand = hand; // SyncLoadout switches the sword to this hand's
            if (sPendHand >= 0 && sPendHand != hand) sPendHand = -1; // B goes to this hand now
            if (sPendOutHand != hand) sPendOutUntil = 0;
            if (!ready) {
                sPendHand = hand;
                sPendFrame = GameFrame();
            }
        }
        if (sPendHand == hand) {
            if (GameFrame() - sPendFrame > kPendMaxFrames) {
                sPendHand = -1;
            } else if (ready) {
                sPendHand = -1;
                sPendOutHand = hand;
                sPendOutUntil = GameFrame() + kPendOutFrames; // the press that waited goes out as a short swing
            }
        }
        if (ready && (pressed || (sPendOutHand == hand && GameFrame() < sPendOutUntil))) {
            out |= BTN_B;
        }
        swordPressed |= pressed || sPendHand == hand || (sPendOutHand == hand && GameFrame() < sPendOutUntil);
    }
    // L2: guard with the equipped shield (the game's R button), always available. An attack (R1 / R2) while guarding
    // lowers the shield first (one game frame with neither, so Link leaves the guard), then attacks; the guard comes
    // back kGuardResumeFrames after the attack input ends if L2 is still held.
    bool attackNow = (r1 && (right != ITEM_NONE || temporaryB)) || sSlashPending || sSlashWait || swordPressed;
    if (attackNow && !sPrevAttack) {
        sAttackStartFrame = GameFrame();
    }
    if (attackNow) {
        sAttackEndFrame = GameFrame();
    }
    sPrevAttack = attackNow;
    bool guardDown = attackNow || GameFrame() - sAttackEndFrame < kGuardResumeFrames;
    if (l2 && !guardDown) {
        out |= BTN_R;
    }
    if (l2 && attackNow && sGuardWasUp && GameFrame() == sAttackStartFrame) {
        out &= ~(BTN_B | BTN_CRIGHT); // this frame only lowers the shield (a left-hand sword is on B too)
    }
    if (GameFrame() != sAttackStartFrame || !attackNow) {
        sGuardWasUp = (out & BTN_R) != 0;
    }
    // Square: the quick item, on C-left
    if (square) {
        out |= BTN_CLEFT;
        if (!sPrevSquare && UsableCurrent(SL_QUICK) == SOULS_ITEM_EPONA) {
            EponaStart();
        } else if (!sPrevSquare && UsableCurrent(SL_QUICK) != ITEM_NONE) {
            SoulsLoadout_NoteUse(SL_QUICK);
        }
    }
    // A pouch item is pressed on C-left for a few frames
    if (sPouchActive) {
        if (GameFrame() - sPouchFrame < kPouchFrames) {
            out |= BTN_CLEFT;
        } else {
            sPouchActive = false;
        }
    }
    EponaTick(in); // runs the stages (the notes are pressed in the ocarina branch)
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

extern "C" u8 SoulsInput_NoAutoJump(void) {
    return CVarGetInteger(CVAR_SOULS_CONTROLS, 0) != 0;
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
        sWasOcarina = false;
        *buttons = SoulsInput_Play(in);
        return;
    }
    SoulsLoadout_SetPouchHeld(false);
    sTriPrev = sASent = sTriUsed = sTapActive = false;
    if (!SoulsInput_InOcarina()) {
        sWasOcarina = false;
    }
    sPrevDpad = in & DPAD_BITS;
    sPrevR1 = sPrevL1 = sPrevSquare = sPrevR2 = false;
    sPrevAttack = sGuardWasUp = false;
    sSlashPending = false;
    sPendHand = -1;
    sPendOutUntil = 0;
    sSlashWait = false;

    if (SoulsInput_InOcarina()) {
        // Cross = A note, D-pad = C notes, Circle (and R1) puts the ocarina away. Triangle does nothing here: it is
        // often still held from the pouch press that took the ocarina out, and the game ignores a held start button.
        // Buttons still held from the press that took the ocarina out (the pouch press, Triangle + D-pad) are not notes
        // until they are released.
        const u16 kNoteBits = DPAD_BITS | BTN_A | BTN_CUSTOM_MODIFIER2;
        if (!sWasOcarina) {
            sHeldAtStart = in & kNoteBits;
        }
        sHeldAtStart &= in;
        sWasOcarina = true;
        in &= ~sHeldAtStart;
        bool circle = (in & BTN_A) && (in & BTN_SOULS_MOD1);
        bool cross = (in & BTN_CUSTOM_MODIFIER2) != 0;
        u16 out = in & ~(DPAD_BITS | BTN_ALL_C | BTN_A | BTN_B | BTN_CUSTOM_MODIFIER2);
        u16 notes = SoulsInput_DpadToC(in) | EponaTick(in);
        if (circle || (in & BTN_B)) {
            out |= BTN_B;
        } else if (cross) {
            out |= BTN_A;
        }
        sCrossWasDown = cross;
        sJumpPending = false;
        sPouchActive = false;
        *buttons = out | notes;
        return;
    }

    // Menus: R2 = B, L2 = Z, L3 = R, as before the loadout. Cross (Modifier 2) confirms; the touchpad does nothing.
    bool pauseOff = gPlayState != nullptr && gPlayState->pauseCtx.state == 0 && gPlayState->pauseCtx.debugState == 0;
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
    // Text box: a Triangle tap turns one page (A); holding Triangle fast-forwards like holding B does with the
    // Skip Text setting. Not in choice boxes (a held skip there would pick an answer).
    bool triHeld = (in & BTN_A) && !(in & BTN_SOULS_MOD1);
    auto now = std::chrono::steady_clock::now();
    if (triHeld && !sMenuTriPrev) {
        sMenuTriStart = now;
    }
    sMenuTriPrev = triHeld;
    if (triHeld && gPlayState != nullptr && pauseOff && gPlayState->msgCtx.msgMode != MSGMODE_NONE &&
        gPlayState->msgCtx.textboxEndType != TEXTBOX_ENDTYPE_2_CHOICE &&
        gPlayState->msgCtx.textboxEndType != TEXTBOX_ENDTYPE_3_CHOICE &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - sMenuTriStart).count() >= kTextSkipHoldMs) {
        out &= ~BTN_A;
        out |= BTN_B;
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

static void RegisterSoulsNoSlash() {
    // After a switch the new item is in hand and ready, not swung
    COND_VB_SHOULD(VB_USE_HELD_ITEM_AFTER_CHANGE, CVarGetInteger(CVAR_SOULS_CONTROLS, 0), {
        Player* player = va_arg(args, Player*);
        if (sNoSlashLeft > 0 && player != nullptr && player->heldItemId == sNoSlashItem) {
            *should = false;
        }
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

static void RegisterSoulsMaskStays() {
    // A worn mask normally comes off when it leaves the C buttons; the loadout moves the buttons around
    COND_VB_SHOULD(VB_PLAYER_UNEQUIP_MASK_WITHOUT_BUTTON, CVarGetInteger(CVAR_SOULS_CONTROLS, 0), { *should = false; });
}

static RegisterShipInitFunc initSoulsMaskStays(RegisterSoulsMaskStays, { CVAR_SOULS_CONTROLS });
static RegisterShipInitFunc initSoulsNoSlash(RegisterSoulsNoSlash, { CVAR_SOULS_CONTROLS });
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
    if (sword == ITEM_SWORD_BGS && !(gSaveContext.bgsFlag == 0 && gSaveContext.swordHealth == 0)) { // a broken knife stays broken
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
    // The no-swing mark of a switch ends when the change is over
    if (sNoSlashLeft > 0 && (--sNoSlashLeft == 0 || !ItemChanging(player))) {
        sNoSlashLeft = 0;
    }
    // The player's own buttons are kept first; during minigames (B holds a temporary item) and while an item is in
    // use (a bottle being drunk) the buttons are left alone
    if (!SoulsLoadout_ButtonItemsStashed() || SoulsLoadout_TemporaryB() || ItemInUse(player)) {
        return;
    }
    // The hookshot slot turns into the longshot when it is upgraded
    for (int hand = SL_RIGHT; hand <= SL_LEFT; hand++) {
        for (int i = 0; i < SoulsLoadout_Capacity((SoulsLoadoutKind)hand); i++) {
            if (SoulsLoadout_Get((SoulsLoadoutKind)hand, i) == ITEM_HOOKSHOT && INV_CONTENT(ITEM_HOOKSHOT) == ITEM_LONGSHOT) {
                SoulsLoadout_Set((SoulsLoadoutKind)hand, i, ITEM_LONGSHOT);
            }
        }
    }
    // The Fairy Ocarina pouch slot turns into the Ocarina of Time when it is upgraded
    for (int i = 0; i < SoulsLoadout_Capacity(SL_POUCH); i++) {
        if (SoulsLoadout_Get(SL_POUCH, i) == ITEM_OCARINA_FAIRY && INV_CONTENT(ITEM_OCARINA_TIME) == ITEM_OCARINA_TIME) {
            SoulsLoadout_Set(SL_POUCH, i, ITEM_OCARINA_TIME);
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
        bool acted = false;
        // A D-pad switch while the old item was out: the new one comes out ready (the game's own item change, no swing)
        if (sDrawReq) {
            if (GameFrame() - sDrawReqFrame > 40) {
                sDrawReq = false;
            } else if (player->heldItemAction == player->itemAction && !ItemChanging(player)) {
                u8 req = sDrawReqItem;
                sDrawReq = false;
                if (req == ITEM_NONE) {
                    Player_UseItem(gPlayState, player, ITEM_NONE);
                    acted = true;
                } else if (player->heldItemAction != Player_ItemToItemAction(req)) {
                    if (IsSword(req) && gSaveContext.equips.buttonItems[0] != req) {
                        EquipSword(gPlayState, player, req);
                    }
                    sNoSlashLeft = 60;
                    sNoSlashItem = req;
                    Player_UseItem(gPlayState, player, req);
                    acted = true;
                }
            }
        }
        // The sword on B is the one of the hand pressed last (the other hand's sword if that hand holds none now).
        // A drawn sword is swapped for the new one in hand (drawn, not swung); a sheathed sword is just switched.
        u8 want = ITEM_NONE;
        u8 first = sSwordHand == 0 ? right : left, second = sSwordHand == 0 ? left : right;
        if (IsSword(first)) {
            want = first;
        } else if (IsSword(second)) {
            want = second;
        }
        if (!acted && want != ITEM_NONE && !SwordOnB(want)) {
            if (player->heldItemAction >= PLAYER_IA_SWORD_MASTER && player->heldItemAction <= PLAYER_IA_SWORD_BIGGORON) {
                // (not while a change runs: the old item is still being put away, the sword is switched after that)
                if (player->heldItemAction == player->itemAction && !ItemChanging(player)) {
                    EquipSword(gPlayState, player, want);
                    if (sPendHand >= 0) {
                        // a button press switched it: the game's own swing after the change is that press's swing
                        sPendHand = -1;
                        sPendOutUntil = 0;
                    } else {
                        sNoSlashLeft = 60; // a D-pad or R2 switch: the new sword comes out ready, not swung
                        sNoSlashItem = want;
                    }
                    Player_UseItem(gPlayState, player, want);
                }
            } else {
                EquipSword(gPlayState, player, want);
            }
        }
    }

    u8 item, slot;
    if (sPouchActive && GameFrame() - sPouchFrame < kPouchFrames + 1) {
        item = sPouchItem; // a pouch use is on C-left
        slot = PouchSlotOf(item);
    } else {
        QuickOnButton(&item, &slot);
    }
    SetButton(1, item, slot);
    ToolOnButton(left, &item, &slot);
    SetButton(2, item, slot);
    ToolOnButton(right, &item, &slot);
    SetButton(3, item, slot);

    // The game turns the lens off, and drops a held item, when it is on no C button. Derive what is in use from the game
    // state every frame and keep those items on a free C button (else C-left; the quick item waits). Masks stay on by
    // themselves (VB_PLAYER_UNEQUIP_MASK_WITHOUT_BUTTON below).
    {
        u8 keep[2] = { ITEM_NONE, ITEM_NONE };
        if (gPlayState->actorCtx.lensActive) {
            keep[0] = ITEM_LENS;
        }
        for (int i = 0; i < SoulsLoadout_Capacity(SL_POUCH); i++) {
            u8 it = SoulsLoadout_Get(SL_POUCH, i);
            if (it == ITEM_BEAN && Player_ItemToItemAction(it) == player->heldItemAction) {
                keep[1] = it;
            }
        }
        for (int k = 0; k < 2; k++) {
            if (keep[k] == ITEM_NONE) {
                continue;
            }
            int free = 1;
            for (int b = 3; b >= 1; b--) {
                if (gSaveContext.equips.buttonItems[b] == keep[k]) {
                    free = 0;
                    break;
                }
                if (gSaveContext.equips.buttonItems[b] == ITEM_NONE) {
                    free = b;
                    break;
                }
            }
            if (free > 0) {
                SetButton(free, keep[k], PouchSlotOf(keep[k]));
            }
        }
    }
}

static void RegisterSoulsLoadoutSync() {
    COND_HOOK(OnPlayerUpdate, CVarGetInteger(CVAR_SOULS_CONTROLS, 0), []() { SyncLoadout(); });
}

static RegisterShipInitFunc initSoulsLoadoutSync(RegisterSoulsLoadoutSync, { CVAR_SOULS_CONTROLS });
