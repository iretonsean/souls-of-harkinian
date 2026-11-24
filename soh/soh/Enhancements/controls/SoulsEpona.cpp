// Souls of Harkinian: Epona's head as an item icon. Her skinned model (object_horse, the same skeleton and idle
// animation the horse actor uses) is drawn live over the icon's box, the way the character creator draws Link:
// a viewport over the box, a perspective camera on the head limb, the game's own skin drawing.
#include "soh/Enhancements/controls/SoulsEpona.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "objects/object_horse/object_horse.h"
extern PlayState* gPlayState;
extern MtxF gSkinLimbMatrices[60];
float OTRGetDimensionFromLeftEdge(float v);
float OTRGetDimensionFromRightEdge(float v);

static Skin sSkin;
static Actor sActor;
static bool sReady = false;
static u32 sTickFrame = 0xFFFFFFFF;

// Camera: limb that holds the head, view yaw from the front of the horse (degrees), pitch, distance, field of view,
// and the look-at point's offset from the head limb (game units).
static float sCam[8] = { 13.0f, 45.0f, 6.0f, 40.0f, 35.0f, 0.0f, -5.0f, 4.0f };

static s32 HeadOverride(Actor* thisx, PlayState* play, s32 limbIndex, Skin* skin) {
    (void)thisx;
    (void)skin;
    if (limbIndex == 13) { // the eye: open (the horse actor blinks)
        OPEN_DISPS(play->state.gfxCtx);
        gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)SEGMENTED_TO_VIRTUAL(gEponaEyeOpenTex));
        CLOSE_DISPS(play->state.gfxCtx);
    }
    return true;
}

static bool Init(PlayState* play) {
    if (sReady) {
        return true;
    }
    Skin_Init(play, &sSkin, (SkeletonHeader*)gEponaSkel, (AnimationHeader*)gEponaIdleAnim);
    memset(&sActor, 0, sizeof(sActor));
    sActor.scale.x = sActor.scale.y = sActor.scale.z = 0.01f; // as the horse actor: model units are 1/100 of game units
    sReady = true;
    return true;
}

// The skin writes into the opaque list. For the HUD the overlay list takes its place while the head is drawn.
static void DrawHead(PlayState* play, float x, float y, float size) {
    GraphicsContext* gfx = play->state.gfxCtx;

    // Where the head is (game units), from the pose the draw will use
    Skin_ApplyAnimTransformations(&sSkin, gSkinLimbMatrices, &sActor, 1);
    int limb = (int)sCam[0];
    if (limb < 0 || limb >= sSkin.skeletonHeader->limbCount) {
        limb = 0;
    }
    Vec3f origin = { 0.0f, 0.0f, 0.0f }, head;
    SkinMatrix_Vec3fMtxFMultXYZ(&gSkinLimbMatrices[limb], &origin, &head);
    head.x = head.x * 0.01f + sCam[5];
    head.y = head.y * 0.01f + sCam[6];
    head.z = head.z * 0.01f + sCam[7];
    const float yaw = sCam[1] * 3.14159265f / 180.0f, pitch = sCam[2] * 3.14159265f / 180.0f;
    Vec3f dir = { sinf(yaw) * cosf(pitch), sinf(pitch), cosf(yaw) * cosf(pitch) };
    Vec3f eye = { head.x + dir.x * sCam[3], head.y + dir.y * sCam[3], head.z + dir.z * sCam[3] };

    // The viewport is stretched over the whole screen while the 2D drawing is a centred 4:3 area (as in the creator)
    const float wl = OTRGetDimensionFromLeftEdge(0.0f), wr = OTRGetDimensionFromRightEdge(320.0f);
    const float vw = size * 320.0f / (wr - wl), vx = (x - wl) * 320.0f / (wr - wl);

    static Vp viewport;
    static Lights1 lights = gdSPDefLights1(95, 95, 100, 255, 245, 235, 0, 0, 0);
    lights.l[0].l.dir[0] = (s8)(dir.x * 127.0f);
    lights.l[0].l.dir[1] = (s8)(dir.y * 127.0f + 40.0f);
    lights.l[0].l.dir[2] = (s8)(dir.z * 127.0f);
    u16 perspNorm;
    Mtx* perspMtx = (Mtx*)Graph_Alloc(gfx, sizeof(Mtx));
    Mtx* lookAtMtx = (Mtx*)Graph_Alloc(gfx, sizeof(Mtx));

    OPEN_DISPS(gfx);
    gDPPipeSync(POLY_OPA_DISP++);
    gSPLoadGeometryMode(POLY_OPA_DISP++, G_SHADE | G_CULL_BACK | G_LIGHTING | G_SHADING_SMOOTH);
    viewport.vp.vscale[0] = (s16)(vw * 2.0f);
    viewport.vp.vscale[1] = (s16)(size * 2.0f);
    viewport.vp.vscale[2] = G_MAXZ / 2;
    viewport.vp.vscale[3] = 0;
    viewport.vp.vtrans[0] = (s16)((vx + vw / 2.0f) * 4.0f);
    viewport.vp.vtrans[1] = (s16)((y + size / 2.0f) * 4.0f);
    viewport.vp.vtrans[2] = G_MAXZ / 2;
    viewport.vp.vtrans[3] = 0;
    gSPViewport(POLY_OPA_DISP++, &viewport);
    guPerspective(perspMtx, &perspNorm, sCam[4], 1.0f, 10.0f, 4000.0f, 1.0f);
    gSPPerspNormalize(POLY_OPA_DISP++, perspNorm);
    gSPMatrix(POLY_OPA_DISP++, perspMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
    guLookAt(lookAtMtx, eye.x, eye.y, eye.z, head.x, head.y, head.z, 0.0f, 1.0f, 0.0f);
    gSPMatrix(POLY_OPA_DISP++, lookAtMtx, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
    gSPSetLights1(POLY_OPA_DISP++, lights);
    func_80093C80(play);
    // no depth buffer here (the scene's would clip the head): limbs come parent first, so the head draws last
    gSPClearGeometryMode(POLY_OPA_DISP++, G_ZBUFFER);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_AA_OPA_SURF, G_RM_AA_OPA_SURF2);
    func_800A6360(&sActor, play, &sSkin, NULL, HeadOverride, 1);
    CLOSE_DISPS(gfx);
}

void SoulsEpona_DrawIcon(GraphicsContext* gfx, int overlay, float x, float y, float size) {
    PlayState* play = gPlayState;
    if (play == nullptr || !Init(play)) {
        return;
    }
    if (sTickFrame != play->state.frames) {
        sTickFrame = play->state.frames;
        SkelAnime_Update(&sSkin.skelAnime); // her idle animation, once per game frame
    }
    if (overlay) {
        TwoHeadGfxArena opa = gfx->polyOpa;
        gfx->polyOpa = gfx->overlay;
        DrawHead(play, x, y, size);
        gfx->overlay = gfx->polyOpa;
        gfx->polyOpa = opa;
        func_800AB2C4(&play->interfaceCtx.view); // the HUD's viewport, scissor and projection again
        Gfx_SetupDL_39Overlay(gfx);
        OPEN_DISPS(gfx);
        gDPSetTextureFilter(OVERLAY_DISP++, G_TF_BILERP);
        CLOSE_DISPS(gfx);
    } else {
        DrawHead(play, x, y, size);
        Gfx_SetupDL_39Opa(gfx);
    }
}

} // extern "C"
