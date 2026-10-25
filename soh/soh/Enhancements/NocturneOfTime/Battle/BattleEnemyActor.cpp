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
#include "objects/object_goma/object_goma.h"
#include "objects/object_gol/object_gol.h"
}

#include <math.h>

namespace {

constexpr int kMaxLimbs = 90; // Queen Gohma has 86
constexpr s16 kDefeatFrames = 12;
constexpr s16 kBossDefeatFrames = 40;
constexpr int kGohmaLimbs = 86;  // BOSSGOMA_LIMB_MAX
constexpr int kLarvaLimbs = 24;  // GOMA_LIMB_MAX
constexpr s32 kGohmaLimbEye = 5; // BOSSGOMA_LIMB_EYE
constexpr s32 kGohmaLimbIris = 39;

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
    s16 defeatFrames;
    AnimationHeader* idle; // looping idle; one-off animations (attacks, flinches) return to it
    f32 idleSpeed;
    bool playingOnce;
};

void PlayIdle(BattleEnemy* self, f32 morph) {
    if (self->idle != NULL) {
        Animation_Change(&self->skelAnime, self->idle, self->idleSpeed, 0.0f, Animation_GetLastFrame(self->idle),
                         ANIMMODE_LOOP, morph);
    }
    self->playingOnce = false;
}

int sActorId = -1;

void BattleEnemy_Init(Actor* thisx, PlayState* play) {
    BattleEnemy* self = (BattleEnemy*)thisx;
    self->defeatTimer = 0;
    self->gone = false;
    self->defeatFrames = kDefeatFrames;
    self->idle = NULL;
    self->idleSpeed = 1.0f;
    self->playingOnce = false;
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
        case Nocturne::EnemyKind::QueenGohma:
            self->idle = (AnimationHeader*)gGohmaIdleCrouchedAnim;
            SkelAnime_Init(play, &self->skelAnime, (SkeletonHeader*)gGohmaSkel, self->idle, self->jointTable,
                           self->morphTable, kGohmaLimbs);
            PlayIdle(self, 0.0f);
            self->baseScale = 0.01f;
            self->defeatFrames = kBossDefeatFrames;
            break;
        case Nocturne::EnemyKind::GohmaLarva:
            self->idle = (AnimationHeader*)gObjectGolStandAnim;
            SkelAnime_Init(play, &self->skelAnime, (SkeletonHeader*)gObjectGolSkel, self->idle, self->jointTable,
                           self->morphTable, kLarvaLimbs);
            PlayIdle(self, 0.0f);
            self->baseScale = 0.01f;
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
    bool done = SkelAnime_Update(&self->skelAnime);
    if (sFast) {
        done = SkelAnime_Update(&self->skelAnime) || done;
        if (self->defeatTimer > 1) {
            self->defeatTimer--;
        }
    }

    if (done && self->playingOnce && self->defeatTimer == 0 && !self->gone) {
        PlayIdle(self, -6.0f);
    }

    // Keese hover
    if (thisx->params == (s16)Nocturne::EnemyKind::FireKeese) {
        thisx->shape.yOffset = sinf(play->gameplayFrames * 0.3f) * 400.0f;
    }

    if (self->defeatTimer > 0) {
        self->defeatTimer--;
        f32 t = (f32)self->defeatTimer / self->defeatFrames;
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

// Queen Gohma: red shell pulsing toward grey, the red eye and white iris (as BossGoma draws them)
static s32 Gohma_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx**, Vec3f*, Vec3s*, void*) {
    OPEN_DISPS(play->state.gfxCtx);
    f32 pulse = (Math_SinS((s16)(play->gameplayFrames * 0x600)) + 1.0f) * 0.5f;
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetEnvColor(POLY_OPA_DISP++, (u8)(255 - 205 * pulse), (u8)(17 + 33 * pulse), (u8)(50 * pulse), 255);
    if (limbIndex == kGohmaLimbEye) {
        gDPSetEnvColor(POLY_OPA_DISP++, 255, 17, 0, 63);
    } else if (limbIndex == kGohmaLimbIris) {
        gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
    }
    CLOSE_DISPS(play->state.gfxCtx);
    return false;
}

static s32 Larva_OverrideLimbDraw(PlayState* play, s32, Gfx**, Vec3f*, Vec3s*, void*) {
    OPEN_DISPS(play->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetEnvColor(POLY_OPA_DISP++, 255, 17, 0, 255);
    CLOSE_DISPS(play->state.gfxCtx);
    return false;
}

// Both Gohma models call segment 8 for a render-mode tweak; an empty list draws them as normal
static Gfx* EmptyDList(GraphicsContext* gfxCtx) {
    Gfx* dList = (Gfx*)Graph_Alloc(gfxCtx, sizeof(Gfx));
    gSPEndDisplayList(dList);
    return dList;
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
        case Nocturne::EnemyKind::QueenGohma:
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)EmptyDList(play->state.gfxCtx));
            SkelAnime_DrawSkeletonOpa(play, &self->skelAnime, Gohma_OverrideLimbDraw, NULL, self);
            break;
        case Nocturne::EnemyKind::GohmaLarva:
            gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)EmptyDList(play->state.gfxCtx));
            SkelAnime_DrawSkeletonOpa(play, &self->skelAnime, Larva_OverrideLimbDraw, NULL, self);
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
        case Nocturne::EnemyKind::QueenGohma:
        case Nocturne::EnemyKind::GohmaLarva:
            y = groundY;
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

void BattleEnemyActor_SetSize(Actor* actor, float factor) {
    BattleEnemy* self = (BattleEnemy*)actor;
    // Deku Babas are drawn from their own stem geometry and are narrow anyway; resize the others
    if (actor->params == (s16)Nocturne::EnemyKind::DekuBaba) {
        return;
    }
    self->baseScale *= factor;
    Actor_SetScale(actor, self->baseScale);
}

void BattleEnemyActor_Animate(Actor* actor, BattleAnim anim) {
    BattleEnemy* self = (BattleEnemy*)actor;
    if (actor == NULL || self->gone || self->defeatTimer > 0) {
        return;
    }
    AnimationHeader* once = NULL;
    switch ((Nocturne::EnemyKind)actor->params) {
        case Nocturne::EnemyKind::QueenGohma:
            once = anim == BattleAnim::Attack   ? (AnimationHeader*)gGohmaAttackAnim
                   : anim == BattleAnim::Hurt    ? (AnimationHeader*)gGohmaDamageAnim
                   : anim == BattleAnim::Special ? (AnimationHeader*)gGohmaLayEggsAnim
                                                 : NULL;
            break;
        case Nocturne::EnemyKind::GohmaLarva:
            once = anim == BattleAnim::Attack ? (AnimationHeader*)gObjectGolJumpHeadbuttAnim : NULL;
            break;
        default:
            break; // the others just keep their idle
    }
    if (once == NULL) {
        return;
    }
    Animation_Change(&self->skelAnime, once, 1.0f, 0.0f, Animation_GetLastFrame(once), ANIMMODE_ONCE, -4.0f);
    self->playingOnce = true;
}
