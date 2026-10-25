#include "BattleEnemyActor.h"
#include "BattleData.h"

#include "soh/ActorDB.h"
#include "soh/frame_interpolation.h"

#include <memory> // z64.h includes it for C++; it must not first appear inside extern "C"

extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
#include "objects/object_dekubaba/object_dekubaba.h"
#include "objects/object_st/object_st.h"
#include "objects/object_firefly/object_firefly.h"
}

#include <math.h>

namespace {

constexpr int kMaxLimbs = 30;
constexpr s16 kDefeatFrames = 12;

// Deku Baba stem pose (fully grown, as in EnDekubaba_Grow), head to base
constexpr s16 kBabaStemAngles[3] = { -0xAAA, -0x31C7, -0x5555 };
constexpr f32 kBabaSize = 1.0f;

struct BattleEnemy {
    Actor actor;
    SkelAnime skelAnime;
    Vec3s jointTable[kMaxLimbs];
    Vec3s morphTable[kMaxLimbs];
    s16 defeatTimer; // counts down once defeated; 0 = alive or gone
    bool gone;       // defeated and faded out: drawn no more, removed when the battle ends
    f32 baseScale;
};

int sActorId = -1;

void BattleEnemy_Init(Actor* thisx, PlayState* play) {
    BattleEnemy* self = (BattleEnemy*)thisx;
    self->defeatTimer = 0;
    self->gone = false;
    thisx->flags |= ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;

    switch ((Nocturne::EnemyKind)thisx->params) {
        case Nocturne::EnemyKind::DekuBaba:
            SkelAnime_Init(play, &self->skelAnime, (SkeletonHeader*)gDekuBabaSkel,
                           (AnimationHeader*)gDekuBabaFastChompAnim, self->jointTable, self->morphTable, 8);
            self->skelAnime.playSpeed = 0.5f; // a lazy chomp while waiting
            self->baseScale = kBabaSize * 0.01f;
            break;
        case Nocturne::EnemyKind::Skulltula:
            SkelAnime_Init(play, &self->skelAnime, (SkeletonHeader*)object_st_Skel_005298,
                           (AnimationHeader*)object_st_Anim_000304, self->jointTable, self->morphTable, 30);
            self->baseScale = 0.04f;
            break;
        case Nocturne::EnemyKind::FireKeese:
        default:
            SkelAnime_Init(play, &self->skelAnime, (SkeletonHeader*)gKeeseSkeleton, (AnimationHeader*)gKeeseFlyAnim,
                           self->jointTable, self->morphTable, 28);
            self->baseScale = 0.005f;
            break;
    }
    // No shadow: the actor never runs floor checks, so one would sit at the wrong height
    ActorShape_Init(&thisx->shape, 0.0f, NULL, 0.0f);
    Actor_SetScale(thisx, self->baseScale);
}

void BattleEnemy_Destroy(Actor*, PlayState*) {
}

bool sFast = false;

void BattleEnemy_Update(Actor* thisx, PlayState* play) {
    BattleEnemy* self = (BattleEnemy*)thisx;
    SkelAnime_Update(&self->skelAnime);
    if (sFast) {
        SkelAnime_Update(&self->skelAnime);
        if (self->defeatTimer > 1) {
            self->defeatTimer--;
        }
    }

    // Keese hover
    if (thisx->params == (s16)Nocturne::EnemyKind::FireKeese) {
        thisx->shape.yOffset = sinf(play->gameplayFrames * 0.3f) * 400.0f;
    }

    if (self->defeatTimer > 0) {
        self->defeatTimer--;
        f32 t = (f32)self->defeatTimer / kDefeatFrames;
        Actor_SetScale(thisx, self->baseScale * t);
        if (self->defeatTimer == 0) {
            // Don't Actor_Kill here: the battle still holds a pointer to us and removes us when it ends
            self->gone = true;
        }
    }
}

s32 Keese_OverrideLimbDraw(PlayState*, s32 limbIndex, Gfx**, Vec3f* pos, Vec3s*, void*) {
    if (limbIndex == 1) {
        pos->y += 2300.0f; // as EnFirefly does
    }
    return false;
}

} // namespace

// Outside the anonymous namespace: OPEN_DISPS declares helpers at block scope that must resolve globally.
static s32 Skulltula_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx**, Vec3f*, Vec3s*, void*) {
    if (limbIndex == 4) {
        // teeth colour (EnSt sets this per frame; without it the teeth take whatever env colour is left over)
        OPEN_DISPS(play->state.gfxCtx);
        gDPPipeSync(POLY_OPA_DISP++);
        gDPSetEnvColor(POLY_OPA_DISP++, 200, 200, 200, 0);
        CLOSE_DISPS(play->state.gfxCtx);
    }
    return false;
}

static void DrawDekuBaba(BattleEnemy* self, PlayState* play) {
    static Gfx* stemDLists[] = { (Gfx*)gDekuBabaStemTopDL, (Gfx*)gDekuBabaStemMiddleDL,
                                 (Gfx*)gDekuBabaStemBaseDL };
    Actor* a = &self->actor;
    f32 size = kBabaSize * (a->scale.x / self->baseScale); // shrinks with the defeat effect

    OPEN_DISPS(play->state.gfxCtx);

    SkelAnime_DrawSkeletonOpa(play, &self->skelAnime, NULL, NULL, self);

    // Stem: from the head down to the base, the way EnDekubaba_DrawStemExtended builds it
    MtxF mtx;
    f32 scale = size * 0.01f;
    Matrix_Translate(a->world.pos.x, a->world.pos.y, a->world.pos.z, MTXMODE_NEW);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    Matrix_Get(&mtx);
    for (s32 i = 0; i < 3; i++) {
        mtx.yw += 20.0f * Math_SinS(kBabaStemAngles[i]) * size;
        f32 step = 20.0f * Math_CosS(kBabaStemAngles[i]) * size;
        mtx.xw -= step * Math_SinS(a->shape.rot.y);
        mtx.zw -= step * Math_CosS(a->shape.rot.y);
        Matrix_Put(&mtx);
        Matrix_RotateZYX(kBabaStemAngles[i], a->shape.rot.y, 0, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, stemDLists[i]);
    }

    // Leaves around the base
    Matrix_Translate(a->home.pos.x, a->home.pos.y, a->home.pos.z, MTXMODE_NEW);
    Matrix_RotateY(a->shape.rot.y * (3.14159265f / 0x8000), MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)gDekuBabaBaseLeavesDL);

    CLOSE_DISPS(play->state.gfxCtx);
}

static void BattleEnemy_Draw(Actor* thisx, PlayState* play) {
    BattleEnemy* self = (BattleEnemy*)thisx;
    if (self->gone) {
        return;
    }

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);

    switch ((Nocturne::EnemyKind)thisx->params) {
        case Nocturne::EnemyKind::DekuBaba:
            DrawDekuBaba(self, play);
            break;
        case Nocturne::EnemyKind::Skulltula:
            SkelAnime_DrawSkeletonOpa(play, &self->skelAnime, Skulltula_OverrideLimbDraw, NULL, self);
            break;
        case Nocturne::EnemyKind::FireKeese:
        default:
            gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 0, 0); // fire variant tint, as EnFirefly
            SkelAnime_DrawSkeletonOpa(play, &self->skelAnime, Keese_OverrideLimbDraw, NULL, self);
            break;
    }

    CLOSE_DISPS(play->state.gfxCtx);
}

static int EnsureRegistered() {
    if (sActorId < 0) {
        ActorDBInit init;
        init.name = "Nocturne_BattleEnemy";
        init.desc = "Nocturne of Time battle enemy";
        init.category = ACTORCAT_MISC; // not ACTORCAT_ENEMY: must not count toward room-clear checks
        init.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        init.objectId = OBJECT_GAMEPLAY_KEEP;
        init.instanceSize = sizeof(BattleEnemy);
        init.init = BattleEnemy_Init;
        init.destroy = BattleEnemy_Destroy;
        init.update = BattleEnemy_Update;
        init.draw = BattleEnemy_Draw;
        sActorId = ActorDB::Instance->AddEntry(init).entry.id;
    }
    return sActorId;
}

Actor* BattleEnemyActor_Spawn(PlayState* play, int kind, float x, float groundY, float z, short yaw) {
    int id = EnsureRegistered();
    f32 y = groundY;
    f32 headX = x, headZ = z;

    switch ((Nocturne::EnemyKind)kind) {
        case Nocturne::EnemyKind::DekuBaba: {
            // The head is the actor position; work out where it sits so the stem's foot lands on (x, groundY, z)
            f32 dy = 0.0f, dh = 0.0f;
            for (s16 angle : kBabaStemAngles) {
                dy += 20.0f * Math_SinS(angle) * kBabaSize;
                dh += 20.0f * Math_CosS(angle) * kBabaSize;
            }
            y = groundY - dy;
            headX = x + dh * Math_SinS(yaw);
            headZ = z + dh * Math_CosS(yaw);
            break;
        }
        case Nocturne::EnemyKind::Skulltula:
            y = groundY + 35.0f; // hanging just above the floor
            break;
        case Nocturne::EnemyKind::FireKeese:
        default:
            y = groundY + 55.0f; // eye level
            break;
    }

    Actor* actor = Actor_Spawn(&play->actorCtx, play, id, headX, y, headZ, 0, yaw, 0, (s16)kind);
    if (actor != NULL) {
        actor->home.pos = { x, groundY, z }; // Deku Baba leaves
    }
    return actor;
}

void BattleEnemyActor_Defeat(Actor* actor) {
    BattleEnemy* self = (BattleEnemy*)actor;
    if (self->defeatTimer == 0 && !self->gone) {
        self->defeatTimer = kDefeatFrames;
    }
}

bool BattleEnemyActor_Is(Actor* actor) {
    return actor != NULL && sActorId >= 0 && actor->id == sActorId;
}

void BattleEnemyActor_SetFast(bool fast) {
    sFast = fast;
}
