#pragma once

#include <libultraship/libultra/types.h>

// Souls of Harkinian: context-dependent controls (menus vs. play). Off unless CVAR_ENHANCEMENT("SoulsControls") is 1.
#ifdef __cplusplus
extern "C" {
#endif
void SoulsInput_Transform(u16* buttons);
// true while the Souls controls (and so the Cross jump) are on: the game's automatic ledge jump is off
u8 SoulsInput_NoAutoJump(void);
#ifdef __cplusplus
}; // extern "C"
#endif
