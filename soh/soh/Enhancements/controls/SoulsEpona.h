#pragma once
// Souls of Harkinian: Epona's head as an item icon (see souls/ER-SYSTEM.md section 5).
// The head is drawn live from her 3D model (object_horse), the way the character creator draws Link: her skeleton and
// idle animation, a small viewport over the icon's box. x, y, size are in game screen units (the units of the HUD and
// Equipment page rects).
#ifdef __cplusplus
extern "C" {
#endif

struct GraphicsContext;
// Draws the head into the overlay display list (HUD, overlay = 1) or the opaque one (pause screen, overlay = 0).
void SoulsEpona_DrawIcon(struct GraphicsContext* gfx, int overlay, float x, float y, float size);

#ifdef __cplusplus
}
#endif
