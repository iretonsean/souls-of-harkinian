#pragma once

#include <libultraship/libultra/types.h>

// Souls of Harkinian: context-dependent controls (menus vs. play). Off unless CVAR_ENHANCEMENT("SoulsControls") is 1.
#ifdef __cplusplus
extern "C" {
#endif
void SoulsInput_Transform(u16* buttons);
#ifdef __cplusplus
}; // extern "C"
#endif
