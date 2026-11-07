#include "HuskProps.h"

#include "global.h"
#include "macros.h"
#include "functions.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "objects/object_syokudai/object_syokudai.h"
#include "objects/object_ydan_objects/object_ydan_objects.h"
#include "objects/object_gs/object_gs.h"
#include "objects/object_gol/object_gol.h"
#include "objects/object_box/object_box.h"

#include <math.h>

// Matrix_SetTranslateRotateYXZ with a plain yaw (no compound literals: MSVC's C compiler)
static void PlaceAt(f32 x, f32 y, f32 z, s16 yaw) {
    Vec3s rot;
    rot.x = 0;
    rot.y = yaw;
    rot.z = 0;
    Matrix_SetTranslateRotateYXZ(x, y, z, &rot);
}

// ---- Chests: the treasure chest's own skeleton, posed closed, open, or opening -------------------------------
// (sizes measured from the models: the chest is 50 wide at its usual 0.01 scale; the wooden torch 58 tall at 1.0;
// the wall web 2800 x 2888 and the floor web 2400 across in model units; the Gossip Stone 415 tall; the egg 2854)
#define CHEST_LIMBS 5

typedef struct {
    SkelAnime skel;
    Vec3s joints[CHEST_LIMBS];
    Vec3s morph[CHEST_LIMBS];
} ChestPose;

static ChestPose sClosed, sOpen, sOpening;
static s32 sReady = 0;
static s32 sOpeningActive = 0;
static Vec3f sOpeningAt;

static AnimationHeader* ChestAnim(void) {
    return (AnimationHeader*)gTreasureChestAnim_00024C; // the adult chest-opening animation
}

static void PoseInit(PlayState* play, ChestPose* p, f32 frame, f32 speed) {
    SkelAnime_Init(play, &p->skel, (SkeletonHeader*)gTreasureChestSkel, ChestAnim(), p->joints, p->morph, CHEST_LIMBS);
    Animation_Change(&p->skel, ChestAnim(), speed, frame, Animation_GetLastFrame(ChestAnim()), ANIMMODE_ONCE, 0.0f);
}

void HuskProps_Reset(void) {
    sReady = 0;
    sOpeningActive = 0;
}

void HuskProps_Update(PlayState* play) {
    if (!sReady) {
        PoseInit(play, &sClosed, 0.0f, 0.0f);
        PoseInit(play, &sOpen, Animation_GetLastFrame(ChestAnim()), 0.0f);
        PoseInit(play, &sOpening, 0.0f, 1.5f);
        sReady = 1;
    }
    SkelAnime_Update(&sClosed.skel);
    SkelAnime_Update(&sOpen.skel);
    if (sOpeningActive && SkelAnime_Update(&sOpening.skel)) {
        sOpeningActive = 0; // done: the chest now draws open
    }
}

void HuskProps_OpenChest(f32 x, f32 y, f32 z) {
    if (!sReady) {
        return;
    }
    sOpeningAt.x = x;
    sOpeningAt.y = y;
    sOpeningAt.z = z;
    Animation_Change(&sOpening.skel, ChestAnim(), 1.5f, 0.0f, Animation_GetLastFrame(ChestAnim()), ANIMMODE_ONCE, 0.0f);
    sOpeningActive = 1;
}

static void ChestPostLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg) {
    OPEN_DISPS(play->state.gfxCtx);
    if (limbIndex == 1) {
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gTreasureChestChestFrontDL);
    } else if (limbIndex == 3) {
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gTreasureChestChestSideAndLidDL);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static Gfx* EmptyDL(GraphicsContext* gfxCtx) {
    Gfx* dl = Graph_Alloc(gfxCtx, sizeof(Gfx));
    gSPEndDisplayList(dl);
    return dl;
}

void HuskProps_Chest(PlayState* play, f32 x, f32 y, f32 z, s16 yaw, f32 scale, s32 open) {
    if (!sReady) {
        return;
    }
    ChestPose* pose = open ? &sOpen : &sClosed;
    if (open && sOpeningActive && fabsf(x - sOpeningAt.x) < 1.0f && fabsf(z - sOpeningAt.z) < 1.0f) {
        pose = &sOpening;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 255);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)EmptyDL(play->state.gfxCtx));
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Push();
    // The chest's front (its lock) faces local -z, so turn it round to face `yaw`
    PlaceAt(x, y, z, (s16)(yaw + 0x8000));
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    CLOSE_DISPS(play->state.gfxCtx);
    SkelAnime_DrawOpa(play, pose->skel.skeleton, pose->skel.jointTable, NULL, ChestPostLimbDraw, NULL);
    Matrix_Pop();
}

// ---- Torches: the wooden torch, and OoT's scrolling flame on top ---------------------------------------------
void HuskProps_Torch(PlayState* play, f32 x, f32 y, f32 z, f32 scale, s32 lit) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Push();
    PlaceAt(x, y, z, 0);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gWoodenTorchDL);
    if (lit) {
        u32 frames = play->gameplayFrames;
        Gfx_SetupDL_25Xlu(play->state.gfxCtx);
        gSPSegment(POLY_XLU_DISP++, 0x08,
                   (uintptr_t)Gfx_TwoTexScrollEx(play->state.gfxCtx, 0, 0, 0, 0x20, 0x40, 1, 0, (frames * -20) & 0x1FF, 0x20,
                                      0x80, 0, 0, 0, -20));
        gDPSetPrimColor(POLY_XLU_DISP++, 0x80, 0x80, 255, 255, 0, 255);
        gDPSetEnvColor(POLY_XLU_DISP++, 255, 0, 0, 0);
        Matrix_Translate(0.0f, 52.0f, 0.0f, MTXMODE_APPLY);
        Matrix_RotateY((s16)(Camera_GetCamDirYaw(GET_ACTIVE_CAM(play)) + 0x8000) * (3.14159265f / 32768.0f), MTXMODE_APPLY);
        Matrix_Scale(0.0027f, 0.0027f, 0.0027f, MTXMODE_APPLY);
        gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gEffFire1DL);
    }
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- The Deku Tree's webs ------------------------------------------------------------------------------------
void HuskProps_WallWeb(PlayState* play, f32 x, f32 y, f32 z, s16 yaw, f32 width, f32 height) {
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    Matrix_Push();
    PlaceAt(x, y, z, yaw);
    Matrix_Scale(width / 2800.0f, height / 2888.0f, 1.0f, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gDTWebWallDL);
    // The tree's wall web is one-sided (OoT only shows it from one side): draw its back too
    Matrix_RotateY(3.14159265f, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gDTWebWallDL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

void HuskProps_FloorWeb(PlayState* play, f32 x, f32 y, f32 z, f32 width) {
    f32 s = width / 2400.0f;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    Matrix_Push();
    PlaceAt(x, y, z, 0);
    Matrix_Scale(s, s, s, MTXMODE_APPLY);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_XLU_DISP++, (Gfx*)gDTWebFloorDL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- Sheikah Stones: OoT's Gossip Stone, which already bears the Sheikah eye ----------------------------------
void HuskProps_SheikahStone(PlayState* play, f32 x, f32 y, f32 z, s16 yaw, f32 scale) {
    f32 s = scale;
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Matrix_Push();
    PlaceAt(x, y, z, yaw); // OoT draws the stone from its base, as EnGs does
    Matrix_Scale(s, s, s, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gGossipStoneMaterialDL);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gGossipStoneDL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// ---- Egg sacs: Gohma's egg, grown huge, breathing --------------------------------------------------------------
void HuskProps_EggSac(PlayState* play, f32 x, f32 y, f32 z, f32 size) {
    u32 frames = play->gameplayFrames;
    f32 breath = sinf((f32)frames * 0.12f) * 0.05f;
    f32 s = size / 2854.0f;
    s16 scroll = (s16)((s16)(sinf(((f32)frames * 5.0f * 3.1415f) / 180.0f) * 31.9f) + 31);
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)Gfx_TexScroll(play->state.gfxCtx, 0, scroll, 0, 0));
    Matrix_Push();
    PlaceAt(x, y + size * 0.5f * (1.0f + breath), z, (s16)(frames * 40));
    Matrix_Scale(s * (1.0f - breath), s * (1.0f + breath), s * (1.0f - breath), MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gObjectGolEggDL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}
