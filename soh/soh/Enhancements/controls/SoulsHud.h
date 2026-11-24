#pragma once
// Souls of Harkinian: the Elden Ring style HUD (see souls/ER-SYSTEM.md section 3).
// Drawn from Interface_Draw (z_parameter.c) only while SoulsLoadout_IsEnabled().
#ifdef __cplusplus
extern "C" {
#endif

struct PlayState;

// Draws the stone heart, HP and magic meters, the quest row, the slot cross, the rupee band and the action prompt.
void SoulsHud_Draw(struct PlayState* play);

// Offset (N64 units, + = right / down) that moves a minimap, whose original left/top are (origX, origY) and whose size is
// (w, h), so that it sits above the rupee band at the HUD margin. Zero when the Souls HUD is off.
void SoulsHud_MinimapShift(int origX, int origY, int w, int h, int* dx, int* dy);

#ifdef __cplusplus
}
#endif
