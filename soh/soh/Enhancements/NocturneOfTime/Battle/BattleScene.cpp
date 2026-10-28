// Nocturne of Time: battles inside the game (see BattleScene.h).

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"

#include "BattleScene.h"
#include "BattleData.h"
#include "BattleEngine.h"
#include "BattleEnemyActor.h"
#include "../Moon/Moon.h"
#include "../Moon/MoonHud.h"
#include "../UI/RetroUI.h"
#include "../UI/RetroScreen.h"
#include "../Loot/Loot.h"
#include "../Party.h"
#include "../DungeonCrawler.h"
#include "../Boss/BossGate.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <math.h>
#include <memory>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
}

#define CVAR_CRAWLER_NAME CVAR_ENHANCEMENT("Nocturne.DungeonCrawler")
#define CVAR_CRAWLER_VALUE CVarGetInteger(CVAR_CRAWLER_NAME, 1)

using namespace Nocturne;

namespace {

// ---- Tuning ---------------------------------------------------------------------------------------------
constexpr int kDangerTrigger = 100;   // meter value that starts a battle
constexpr int kDangerStepMin = 6;     // meter gained per step: min..max
constexpr int kDangerStepMax = 16;
constexpr int kDangerYellow = 35;     // gem turns yellow
constexpr int kDangerRed = 70;        // gem turns red
constexpr int kMessageFrames = 24;    // how long a battle message stays up (20 game frames = 1 second)
constexpr int kMessageMinFrames = 5;  // A can skip a message after this
constexpr int kEnemyThinkFrames = 8;  // pause before each enemy acts
constexpr int kIntroFrames = 20;
constexpr int kStartLevel = 5;
constexpr float kDropChance = 0.35f; // per defeated enemy
constexpr int kMaxDrops = 2;         // per battle
constexpr f32 kEnemyDistance = 160.0f;
constexpr f32 kEnemyMinDistance = 90.0f;
constexpr f32 kEnemyGap = 14.0f; // space between neighbouring enemies
constexpr f32 kBossDistance = 250.0f;    // Queen Gohma stands further back: she's big
constexpr f32 kBossMinDistance = 200.0f;
// Her arena, from the vanilla fight: the room's centre, and the entrance where her cutscene starts
constexpr f32 kArenaCentreX = -150.0f, kArenaCentreZ = -350.0f;
constexpr f32 kArenaEntranceX = 150.0f, kArenaEntranceZ = 350.0f;
constexpr f32 kArenaEntranceBox = 70.0f; // OoT uses 60; a little slack for the crawler's grid
// Where the party stands for her entrance: where OoT puts Link for it, just inside the doorway
constexpr f32 kArenaStandX = 150.0f, kArenaStandZ = 300.0f;
constexpr int kHeartContainerHp = 8;     // max HP for everyone when a boss falls

enum class Ui { None, BossIntro, Intro, Command, Skills, Items, TargetEnemy, TargetAlly, Messages, StatPoints, Ending };

enum Command { CMD_ATTACK, CMD_SKILL, CMD_ITEM, CMD_GUARD, CMD_PASS, CMD_ANALYZE, CMD_AUTO, CMD_RUN, CMD_COUNT };
const char* kCommandNames[CMD_COUNT] = { "Attack", "Skill", "Item", "Guard", "Pass", "Analyze", "Auto", "Run" };
constexpr float kConsumableDropChance = 0.25f; // per defeated enemy
constexpr int kMaxConsumableDrops = 2;          // per battle

// Auto-battle: everyone attacks with their weapon and messages fly by (for clearing easy fights quickly)
constexpr int kAutoMessageFrames = 7;  // normal messages
constexpr int kAutoHeldFrames = 16;    // messages that usually wait for A (rewards, level-ups, finds)
constexpr int kAutoThinkFrames = 2;    // pause before each enemy acts

constexpr int kPendingAnalyze = -2;
constexpr int kPendingItem = -3; // TargetAlly is choosing who gets sScene.pendingItem

struct Scene {
    std::unique_ptr<Battle> battle;
    std::vector<Actor*> enemyActors; // parallel to battle->Enemies()
    std::vector<std::string> enemyLabels;
    Ui ui = Ui::None;
    int timer = 0;

    std::deque<BattleEvent> queue;
    std::string message;
    uint32_t messageColor = 0;
    bool showingMessage = false;
    bool messageHeld = false; // waits for A
    bool rewardsGiven = false;
    bool defeat = false;

    int commandCursor = 0;
    int skillCursor = 0;  // index into the member's skills, from 1
    int targetCursor = 0;
    int pendingSkill = -1; // skill waiting for a target, or kPendingAnalyze

    int stickDir = 0; // last stick direction for edge detection: 0 none, 1 up, 2 down, 3 left, 4 right
    int damageFlash = 0;
    int impactFlash = 0;   // white flash for weaknesses and criticals
    bool showSkillInfo = false; // Z in the skill list
    int statMember = -1;        // party member placing level-up points
    int statCursor = 0;
    bool autoBattle = false; // until B is pressed or the battle ends
    int itemCursor = 0;      // index into OwnedItems()
    ConsumableId pendingItem = ConsumableId::RedPotion;

    bool boss = false; // a boss fight: time limit, no running, its own rewards and ending
    // Where the enemies stand (for enemies that join mid-battle)
    Vec3f origin = { 0.0f, 0.0f, 0.0f };
    Vec3f fwd = { 0.0f, 0.0f, 1.0f };
    Vec3f right = { 1.0f, 0.0f, 0.0f };
    f32 dist = 0.0f;
    // Keeping a boss centred (CentreBossInView)
    bool centrePrimed = false; // the next measurement is applied all at once
    int centreDelay = 0;       // frames to wait first (her landing pose settling)
    f32 centreError = 0.0f;    // smoothed sideways error, so her idle sway isn't chased
};

Scene sScene;
std::set<int> sAnalyzedKinds; // bestiary: enemy kinds whose affinities are known
int sDanger = 0;
s16 sLastScene = -1;
bool sBossDone = false; // this visit's boss fight is over (the warp out is under way)
// Her chamber: where the vanilla Queen Gohma would have spawned (seen as we stop her), and which room that is
bool sArenaKnown = false;
s8 sArenaRoom = -1;
Vec3f sArenaPos = { 0.0f, 0.0f, 0.0f };

// The boss's entrance (Ui::BossIntro): she hangs from the ceiling, the view tilts up to her, she drops and lands,
// then her title card
enum class IntroPhase { LookUp, Watch, Fall, Title };
struct BossIntro {
    IntroPhase phase = IntroPhase::LookUp;
    int timer = 0;
    f32 floorY = 0.0f, ceilY = 0.0f, fallSpeed = 0.0f;
    s16 pitch = 0, pitchTarget = 0;
};
BossIntro sIntro;
constexpr int kIntroLookFrames = 30;
constexpr int kIntroWatchFrames = 26;
constexpr int kIntroTitleFrames = 70;

void EnsureParty() {
    Party_EnsureCreated(kStartLevel);
}

void HealParty() {
    Party_HealAll();
}

void PlaySfx(u16 id) {
    Sfx_PlaySfxCentered(id);
}


std::string AffinitySummary(const Combatant& c) {
    static const Affinity kOrder[] = { Affinity::Weak, Affinity::Resist, Affinity::Null, Affinity::Repel,
                                       Affinity::Drain };
    std::string out;
    for (Affinity a : kOrder) {
        std::string group;
        for (size_t e = 0; e < (size_t)Element::Count; e++) {
            if (c.affinity[e] == a) {
                group += (group.empty() ? "" : " ") + std::string(ElementName((Element)e));
            }
        }
        if (!group.empty()) {
            out += (out.empty() ? "" : "  ") + std::string(AffinityName(a)) + ":" + group;
        }
    }
    return out.empty() ? "No weaknesses" : out;
}

// ---- Starting and ending --------------------------------------------------------------------------------

f32 FreeDistance(PlayState* play, const Vec3f& from, s16 yaw, f32 maxDist) {
    Vec3f a = { from.x, from.y + 30.0f, from.z };
    Vec3f b = { from.x + Math_SinS(yaw) * maxDist, from.y + 30.0f, from.z + Math_CosS(yaw) * maxDist };
    Vec3f hit;
    CollisionPoly* poly;
    s32 bgId;
    if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, true, false, false, true, &bgId)) {
        f32 dx = hit.x - a.x, dz = hit.z - a.z;
        return sqrtf(dx * dx + dz * dz);
    }
    return maxDist;
}

std::vector<EnemyKind> RollEncounter() {
    int count = Moon_EnemyCount(Rand_ZeroOne()); // fuller moon, bigger packs
    std::vector<EnemyKind> kinds;
    for (int i = 0; i < count; i++) {
        kinds.push_back((EnemyKind)(int)(Rand_ZeroOne() * (f32)kRandomEnemyKinds * 0.9999f));
    }
    return kinds;
}

void BeginBossIntro(PlayState* play);

void StartBattle(PlayState* play, Player* player, std::vector<Combatant> enemies, bool boss) {
    EnsureParty();
    sScene.boss = boss;
    sScene.autoBattle = false;
    BattleEnemyActor_SetFast(false);

    // Face the roomiest direction: enough floor ahead, and the widest space side to side where the enemies
    // will stand (the current facing wins ties, so battles usually start the way you were looking)
    auto roomAt = [&](s8 f, f32& ahead) {
        s16 y = (s16)(f * 0x4000);
        ahead = FreeDistance(play, player->actor.world.pos, y, 240.0f);
        f32 d = CLAMP(ahead - 40.0f, kEnemyMinDistance, kEnemyDistance);
        Vec3f c = { player->actor.world.pos.x + Math_SinS(y) * d, player->actor.world.pos.y,
                    player->actor.world.pos.z + Math_CosS(y) * d };
        f32 l = FreeDistance(play, c, (s16)(y + 0x4000), 220.0f), r = FreeDistance(play, c, (s16)(y - 0x4000), 220.0f);
        return std::min(ahead, 200.0f) + 2.0f * std::min(l, r); // forward space and the narrower side's width
    };
    s8 bestFacing = DungeonCrawler_GetFacing();
    f32 bestDist = 0.0f;
    f32 bestScore = roomAt(bestFacing, bestDist) + 30.0f; // a little bias toward the current facing
    s16 bossYaw = 0;
    if (boss) {
        // Look straight at the middle of her chamber (any angle, as OoT's own camera does)
        bossYaw = Math_Atan2S(kArenaCentreZ - player->actor.world.pos.z, kArenaCentreX - player->actor.world.pos.x);
    }
    for (s8 f = 0; f < 4 && !boss; f++) {
        f32 ahead;
        f32 score = roomAt(f, ahead);
        if (f != bestFacing && score > bestScore && ahead >= kEnemyMinDistance + 40.0f) {
            bestScore = score;
            bestFacing = f;
            bestDist = ahead;
        }
    }
    if (boss) {
        DungeonCrawler_FaceYaw(bossYaw);
        // Bosses need far more room than the 240 units the facing test looks at
        bestDist = FreeDistance(play, player->actor.world.pos, bossYaw, 600.0f);
    } else {
        DungeonCrawler_SetFacing(bestFacing);
    }

    // Labels: "Deku Baba A", "Deku Baba B" when a kind repeats
    sScene.enemyLabels.clear();
    for (size_t i = 0; i < enemies.size(); i++) {
        int same = 0, index = 0;
        for (size_t j = 0; j < enemies.size(); j++) {
            if (enemies[j].kind == enemies[i].kind) {
                if (j < i) {
                    index++;
                }
                same++;
            }
        }
        std::string label = enemies[i].name;
        if (same > 1) {
            label += std::string(" ") + (char)('A' + index);
            enemies[i].name = label;
        }
        sScene.enemyLabels.push_back(label);
    }

    // Line the enemies up in front of the camera, left to right on screen
    s16 yaw = boss ? bossYaw : (s16)(bestFacing * 0x4000);
    Vec3f fwd = { Math_SinS(yaw), 0.0f, Math_CosS(yaw) };
    Vec3f right = { -Math_CosS(yaw), 0.0f, Math_SinS(yaw) };
    f32 dist = boss ? CLAMP(bestDist - 120.0f, kBossMinDistance, kBossDistance)
                    : CLAMP(bestDist - 40.0f, kEnemyMinDistance, kEnemyDistance);
    // Space enemies by their size so wide ones (Skulltulas) don't overlap
    auto widthOf = [](int kind) -> f32 {
        switch ((EnemyKind)kind) {
            case EnemyKind::Skulltula:
                return 80.0f;
            case EnemyKind::FireKeese:
                return 46.0f;
            case EnemyKind::QueenGohma:
                return 150.0f;
            default:
                return 42.0f;
        }
    };
    f32 rowWidth = kEnemyGap * (f32)(enemies.size() - 1);
    for (const Combatant& e : enemies) {
        rowWidth += widthOf(e.kind);
    }
    // The row is always centred straight ahead of the camera (as in the Famicom games). Where walls are
    // close it is squeezed together, never shifted sideways: the narrower side decides how far it can spread.
    const Vec3f& origin = player->actor.world.pos;
    sScene.origin = origin;
    sScene.fwd = fwd;
    sScene.right = right;
    sScene.dist = dist;
    Vec3f centre = { origin.x + fwd.x * dist, origin.y, origin.z + fwd.z * dist };
    const f32 kWallMargin = 24.0f;
    f32 freeRight = std::max(0.0f, FreeDistance(play, centre, (s16)(yaw - 0x4000), 220.0f) - kWallMargin);
    f32 freeLeft = std::max(0.0f, FreeDistance(play, centre, (s16)(yaw + 0x4000), 220.0f) - kWallMargin);
    f32 room = 2.0f * std::min(freeLeft, freeRight);
    // keep the whole group comfortably inside the view too
    room = std::min(room, dist * 1.3f);
    f32 squeeze = 1.0f;
    if (rowWidth > room && rowWidth > 0.0f) {
        squeeze = std::max(0.35f, room / rowWidth); // crowd them together rather than into the wall
    }
    // Still too wide (a Skulltula in a narrow corridor): shrink them a little so they don't clip the walls
    f32 sizeScale = 1.0f;
    if (!boss && rowWidth * squeeze > room && rowWidth > 0.0f) {
        sizeScale = std::max(0.6f, room / (rowWidth * squeeze));
        squeeze *= sizeScale;
    }
    const f32 shift = 0.0f;

    sScene.enemyActors.clear();
    f32 cursor = -rowWidth * 0.5f;
    for (size_t i = 0; i < enemies.size(); i++) {
        f32 w = widthOf(enemies[i].kind);
        f32 lateral = (cursor + w * 0.5f) * squeeze + shift;
        cursor += w + kEnemyGap;
        Vec3f pos = { centre.x + right.x * lateral, origin.y, centre.z + right.z * lateral };

        // Last check: a straight line from Link to the enemy must be clear, else pull the enemy in front of
        // whatever is in the way (tapering rooms, pillars)
        {
            Vec3f eyeA = { origin.x, origin.y + 30.0f, origin.z };
            Vec3f eyeB = { pos.x, origin.y + 30.0f, pos.z };
            Vec3f hit;
            CollisionPoly* hitPoly;
            s32 hitBg;
            if (BgCheck_EntityLineTest1(&play->colCtx, &eyeA, &eyeB, &hit, &hitPoly, true, false, false, true,
                                        &hitBg)) {
                f32 dx = hit.x - origin.x, dz = hit.z - origin.z;
                f32 len = sqrtf(dx * dx + dz * dz);
                f32 keep = std::max(0.0f, len - kWallMargin);
                if (len > 0.001f) {
                    pos.x = origin.x + dx / len * keep;
                    pos.z = origin.z + dz / len * keep;
                }
            }
        }
        Vec3f probe = { pos.x, pos.y + 50.0f, pos.z };
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if (floorY <= BGCHECK_Y_MIN + 1.0f || floorY < pos.y - 80.0f) {
            floorY = pos.y;
        }
        // Each enemy turns to look straight at the camera
        s16 face = Math_Atan2S(origin.z - pos.z, origin.x - pos.x);
        Actor* a = BattleEnemyActor_Spawn(play, enemies[i].kind, pos.x, floorY, pos.z, face);
        if (a != NULL && sizeScale < 1.0f) {
            BattleEnemyActor_SetSize(a, sizeScale);
        }
        sScene.enemyActors.push_back(a);
    }

    // Battle stats include equipment
    std::vector<Combatant> party;
    for (const PartyMember& m : Party_Members()) {
        party.push_back(Party_Effective(m));
    }
    // Mask of Truth: every foe's affinities are known from the start
    if (Party_HasEffect(UniqueEffect::AutoAnalyze)) {
        for (const Combatant& e : enemies) {
            sAnalyzedKinds.insert(e.kind);
        }
    }
    sScene.battle = std::make_unique<Battle>(party, enemies, [] { return Rand_ZeroOne() * 0.99999f; });
    if (boss) {
        sScene.battle->SetTurnLimit(kGohmaTurnLimit);
        sScene.battle->SetNoEscape(true);
    }
    sScene.queue.clear();
    sScene.rewardsGiven = false;
    sScene.defeat = false;
    sScene.commandCursor = 0;
    sScene.ui = Ui::Intro;
    sScene.timer = 0;
    sScene.message = enemies.size() == 1 ? "A " + enemies[0].name + " appears!" : "Enemies appear!";
    sScene.messageColor = 0;
    if (boss) {
        sScene.message = "The chamber falls silent...";
        sScene.messageColor = 0;
        BeginBossIntro(play);
    } else if (Moon_BlocksEscape()) {
        sScene.message += "\nThe full moon has driven them mad!";
        sScene.messageColor = 0xC8A0FF00u; // violet
    }
    sScene.showingMessage = true;
    PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
}

// ---- The boss's entrance ---------------------------------------------------------------------------------

f32 CeilingAbove(PlayState* play, const Vec3f& floor) {
    Vec3f a = { floor.x, floor.y + 60.0f, floor.z };
    Vec3f b = { floor.x, floor.y + 900.0f, floor.z };
    Vec3f hit;
    CollisionPoly* poly;
    s32 bgId;
    f32 ceilingY = floor.y + 320.0f;
    if (BgCheck_EntityLineTest1(&play->colCtx, &a, &b, &hit, &poly, false, true, true, false, &bgId)) {
        ceilingY = hit.y;
    }
    // A very high ceiling would hang her out of sight
    return std::min(ceilingY, floor.y + 420.0f);
}

s16 PitchToward(f32 targetY) {
    f32 dy = targetY - (sScene.origin.y + 58.0f);
    return (s16)std::clamp<s32>(Math_Atan2S(std::max(1.0f, sScene.dist), dy), -0x1000, 0x3800);
}

void BeginBossIntro(PlayState* play) {
    Actor* gohma = sScene.enemyActors.empty() ? NULL : sScene.enemyActors[0];
    sIntro = BossIntro();
    sScene.ui = Ui::BossIntro;
    if (gohma == NULL) {
        sIntro.phase = IntroPhase::Title;
        return;
    }
    sScene.centrePrimed = false;
    sScene.centreDelay = 0;
    sScene.centreError = 0.0f;
    sIntro.floorY = gohma->world.pos.y;
    sIntro.ceilY = CeilingAbove(play, gohma->world.pos);
    // Upside down on the ceiling, as in OoT
    gohma->world.pos.y = sIntro.ceilY - 4.0f;
    gohma->shape.rot.x = (s16)0x8000;
    BattleEnemyActor_Animate(gohma, BattleAnim::Hang);
    sIntro.pitchTarget = PitchToward(sIntro.ceilY - 150.0f); // until her body has been measured
    Audio_QueueSeqCmd(0x1 << 28 | SEQ_PLAYER_BGM_MAIN << 24 | 0x100FF); // silence before she appears
}

void LandGohma(Actor* gohma) {
    gohma->world.pos.y = sIntro.floorY;
    gohma->shape.rot.x = 0;
    sScene.centrePrimed = false; // re-centre in one go under the landing quake, once her landing pose settles
    sScene.centreDelay = 6;
    if (gPlayState != NULL) {
        s16 quake = Quake_Add(Play_GetCamera(gPlayState, CAM_ID_MAIN), 3);
        Quake_SetSpeed(quake, 22000);
        Quake_SetQuakeValues(quake, 8, 0, 0, 0);
        Quake_SetCountdown(quake, 14);
    }
    Rumble_Request(0.0f, 220, 20, 20);
    PlaySfx(NA_SE_EN_GOMA_DOWN);
    PlaySfx(NA_SE_IT_BOMB_EXPLOSION);
    sIntro.phase = IntroPhase::Title;
    sIntro.timer = 0;
    sScene.showingMessage = false;
}

void UpdateBossIntro(bool skip) {
    Actor* gohma = sScene.enemyActors.empty() ? NULL : sScene.enemyActors[0];
    sIntro.timer++;
    if (skip && sIntro.phase != IntroPhase::Title && gohma != NULL) {
        BattleEnemyActor_Animate(gohma, BattleAnim::Land);
        LandGohma(gohma);
    }
    switch (sIntro.phase) {
        case IntroPhase::LookUp: {
            // Something is up there: the view tilts up to her
            // Aim at the middle of her body as she hangs (she's big, so well below the ceiling)
            float bx, by, bz;
            if (gohma != NULL && BattleEnemyActor_BodyPos(gohma, &bx, &by, &bz)) {
                sIntro.pitchTarget = PitchToward(by);
            }
            f32 t = std::min(1.0f, (f32)sIntro.timer / kIntroLookFrames);
            t = t * t * (3.0f - 2.0f * t);
            sIntro.pitch = (s16)(sIntro.pitchTarget * t);
            if (sIntro.timer == 6) {
                PlaySfx(NA_SE_EN_GOMA_UNARI);
            }
            if (sIntro.timer >= kIntroLookFrames) {
                sIntro.phase = IntroPhase::Watch;
                sIntro.timer = 0;
                sScene.message = "A great eye opens above you!";
                sScene.messageColor = 0xF8380000u;
                sScene.showingMessage = true;
                PlaySfx(NA_SE_EN_GOMA_DEMO_EYE);
            }
            break;
        }
        case IntroPhase::Watch:
            sIntro.pitch = sIntro.pitchTarget;
            if (sIntro.timer >= kIntroWatchFrames && gohma != NULL) {
                sIntro.phase = IntroPhase::Fall;
                sIntro.timer = 0;
                BattleEnemyActor_Animate(gohma, BattleAnim::Land);
                PlaySfx(NA_SE_EN_GOMA_HIGH);
            }
            break;
        case IntroPhase::Fall:
            if (gohma != NULL) {
                // Drops like a stone, turning right side up on the way (OoT eases rot.x the same way)
                sIntro.fallSpeed += 2.0f;
                gohma->world.pos.y -= sIntro.fallSpeed;
                Math_ApproachS(&gohma->shape.rot.x, 0, 2, 0xBB8);
                float bx, by, bz;
                f32 follow = BattleEnemyActor_BodyPos(gohma, &bx, &by, &bz) ? by : gohma->world.pos.y - 60.0f;
                sIntro.pitch = std::max<s16>(0, PitchToward(follow));
                if (gohma->world.pos.y <= sIntro.floorY) {
                    LandGohma(gohma);
                }
            }
            break;
        case IntroPhase::Title:
            Math_ApproachS(&sIntro.pitch, 0, 3, 0x200); // settle back to eye level for the fight
            if (sIntro.timer == 8) {
                PlaySfx(NA_SE_EN_GOMA_CRY1);
                Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_BOSS);
            }
            if (sIntro.timer >= kIntroTitleFrames || (skip && sIntro.timer > 12)) {
                sIntro.pitch = 0;
                sScene.ui = Ui::Intro;
                sScene.timer = 0;
                sScene.message = "Defeat her in " + std::to_string(kGohmaTurnLimit) + " turns!";
                sScene.messageColor = 0xF8380000u;
                sScene.showingMessage = true;
            }
            break;
    }
    DungeonCrawler_SetPitch(sIntro.pitch);
}

// Her body is drawn well off her actor's position (her animations move it), so placing her "straight ahead"
// isn't enough: each frame, slide her sideways until her eye is in the middle of the view.
void CentreBossInView() {
    Actor* gohma = sScene.enemyActors.empty() ? NULL : sScene.enemyActors[0];
    float ex, ey, ez;
    if (gohma == NULL || !BattleEnemyActor_BodyPos(gohma, &ex, &ey, &ez)) {
        return;
    }
    if (sScene.ui == Ui::BossIntro && sIntro.phase == IntroPhase::Fall) {
        return; // she's turning over in the air, so hold still until she lands
    }
    if (sScene.centreDelay > 0) {
        sScene.centreDelay--;
        return;
    }
    f32 off = (ex - sScene.origin.x) * sScene.right.x + (ez - sScene.origin.z) * sScene.right.z;
    f32 step;
    if (!sScene.centrePrimed) {
        // First sight of her (on the ceiling), and again as she lands: straight to the middle
        sScene.centrePrimed = true;
        sScene.centreError = 0.0f;
        step = off;
    } else {
        // Then only slow corrections from the average error, which her swaying cancels out of
        sScene.centreError += (off - sScene.centreError) * 0.08f;
        step = std::clamp(sScene.centreError * 0.15f, -3.0f, 3.0f);
        sScene.centreError -= step;
    }
    gohma->world.pos.x -= sScene.right.x * step;
    gohma->world.pos.z -= sScene.right.z * step;
}

void StartRandomBattle(PlayState* play, Player* player) {
    std::vector<Combatant> enemies;
    for (EnemyKind k : RollEncounter()) {
        enemies.push_back(MakeEnemy(k));
    }
    StartBattle(play, player, enemies, false);
}

void StartBossBattle(PlayState* play, Player* player) {
    // Two larvae, three under a gibbous or full moon
    int larvae = Moon_Phase() >= MoonPhase::SixEighths ? 3 : 2;
    StartBattle(play, player, { MakeQueenGohma(larvae) }, true);
}

// A larva hatches beside the queen, a little closer to the party
void SpawnJoiningEnemy(PlayState* play, int index) {
    if (play == NULL || !sScene.battle || index < 0 || index >= (int)sScene.battle->Enemies().size()) {
        return;
    }
    const Combatant& c = sScene.battle->Enemies()[index];
    int k = index - 1; // the queen is enemy 0
    // Out from under her, to either side and nearer the camera, so they're in plain view
    // (all well inside the view: about 25-35 degrees off centre at most)
    static const f32 kLateral[] = { -75.0f, 75.0f, -140.0f, 140.0f, 0.0f };
    static const f32 kCloser[] = { 90.0f, 90.0f, 30.0f, 30.0f, 150.0f };
    int slot = std::clamp(k, 0, 4);
    f32 d = std::max(60.0f, sScene.dist - kCloser[slot]);
    Vec3f pos = { sScene.origin.x + sScene.fwd.x * d + sScene.right.x * kLateral[slot], sScene.origin.y,
                  sScene.origin.z + sScene.fwd.z * d + sScene.right.z * kLateral[slot] };
    Vec3f probe = { pos.x, pos.y + 50.0f, pos.z };
    CollisionPoly* poly;
    s32 bgId;
    f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
    if (floorY <= BGCHECK_Y_MIN + 1.0f || floorY < pos.y - 80.0f) {
        floorY = pos.y;
    }
    s16 face = Math_Atan2S(sScene.origin.z - pos.z, sScene.origin.x - pos.x);
    while ((int)sScene.enemyActors.size() < index) {
        sScene.enemyActors.push_back(NULL);
    }
    Actor* a = BattleEnemyActor_Spawn(play, c.kind, pos.x, floorY, pos.z, face);
    if ((int)sScene.enemyActors.size() == index) {
        sScene.enemyActors.push_back(a);
    } else {
        sScene.enemyActors[index] = a;
    }
    while ((int)sScene.enemyLabels.size() <= index) {
        sScene.enemyLabels.push_back(c.name);
    }
}

void KillEnemyActors() {
    // Battle enemies never remove themselves, so these pointers stay valid until the scene changes
    // (OnSceneInit drops the list without touching it).
    for (Actor* a : sScene.enemyActors) {
        if (a != NULL) {
            Actor_Kill(a);
        }
    }
    sScene.enemyActors.clear();
}

void EndBattle() {
    if (sScene.defeat) {
        // A game over reloads the dungeon without reloading the save: start the party fresh
        sScene.battle.reset();
        HealParty();
    }
    if (sScene.battle) {
        // Carry HP/MP back to the party; a fallen ally gets back up with 1 HP
        const std::vector<Combatant>& after = sScene.battle->Party();
        std::vector<PartyMember>& members = Party_Members();
        for (size_t i = 0; i < after.size() && i < members.size(); i++) {
            members[i].base.hp = std::max(after[i].hp, 1);
            members[i].base.mp = after[i].mp;
            // Poison lingers after the battle; burns go out
            members[i].base.ailment = after[i].ailment == Ailment::Poison ? Ailment::Poison : Ailment::None;
            members[i].base.ailmentTurns = 0;
        }
    }
    KillEnemyActors();
    sScene.autoBattle = false;
    BattleEnemyActor_SetFast(false);
    sScene.battle.reset();
    sScene.ui = Ui::None;
    sScene.queue.clear();
    sScene.showingMessage = false;
    sScene.boss = false;
    sDanger = 0;
}

BattleEvent HeldMessage(const std::string& text, uint32_t color) {
    BattleEvent e;
    e.type = BattleEvent::Type::Message;
    e.text = text;
    e.hold = true;
    e.color = color;
    return e;
}

void QueueRewards() {
    Battle& b = *sScene.battle;
    int rupees = b.RupeesEarned();
    const std::vector<Combatant>& after = b.Party();
    std::vector<PartyMember>& members = Party_Members();
    // XP per member: each enemy's reward shrinks when the member is far above its level
    auto xpFor = [&](const PartyMember& m) {
        int xp = 0;
        for (const Combatant& foe : b.Enemies()) {
            xp += ScaledXp(foe.xpReward, m.base.level, foe.level);
        }
        return xp;
    };
    int shownXp = 0;
    for (size_t i = 0; i < members.size() && i < after.size(); i++) {
        if (after[i].Alive()) {
            shownXp = std::max(shownXp, xpFor(members[i]));
        }
    }
    BattleEvent e;
    e.type = BattleEvent::Type::Message;
    e.text = "Got " + std::to_string(shownXp) + " XP and " + std::to_string(rupees) + " rupees.";
    e.hold = true;
    sScene.queue.push_back(e);
    Rupees_ChangeBy((s16)rupees);

    for (size_t i = 0; i < members.size() && i < after.size(); i++) {
        PartyMember& m = members[i];
        m.base.hp = after[i].hp;
        m.base.mp = after[i].mp;
        m.base.ailment = after[i].ailment == Ailment::Poison ? Ailment::Poison : Ailment::None;
        m.base.ailmentTurns = 0;
        if (!after[i].Alive()) {
            continue; // fallen allies earn nothing
        }
        m.xp += xpFor(m);
        while (m.xp >= XpForLevel(m.base.level + 1, m.base.name)) {
            LevelGains gains = LevelUp(m.base, [] { return Rand_ZeroOne() * 0.99999f; });
            if (!gains.leveled) {
                break;
            }
            // Level-ups add the gained max HP/MP to current HP/MP
            m.base.hp += gains.hp;
            m.base.mp += gains.mp;
            m.unspentStatPoints += kManualStatPoints;
            std::string grew;
            for (size_t st = 0; st < gains.stats.size(); st++) {
                if (gains.stats[st] > 0) {
                    grew += std::string(grew.empty() ? "" : " ") + StatName((Stat)st) + "+" +
                            std::to_string(gains.stats[st]);
                }
            }
            BattleEvent lv;
            lv.type = BattleEvent::Type::Message;
            lv.text = m.base.name + " reached level " + std::to_string(m.base.level) + "!\n" + grew;
            lv.hold = true;
            sScene.queue.push_back(lv);
        }
        Party_ClampToMax(m);
    }

    // Drops: each defeated enemy has a chance to leave an item (at most two per battle)
    int itemLevel = 1, drops = 0;
    for (const Combatant& e : b.Enemies()) {
        itemLevel = std::max(itemLevel, e.level);
    }
    LootRng rng = [] { return Rand_ZeroOne() * 0.99999f; };
    auto addFound = [&](const Item& item) {
        Party_Bag().push_back(item);
        Rgb c = RarityColor(item.rarity);
        sScene.queue.push_back(HeldMessage("Found " + item.name + "!",
                                           ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8)));
    };
    if (sScene.boss) {
        // A boss always leaves something special (Rare or better), and maybe a second piece
        addFound(RollItem(itemLevel, DropSource::Boss, Moon_LootLuck(), rng));
        drops++;
        if (Rand_ZeroOne() < 0.5f * Moon_DropScale()) {
            addFound(RollItem(itemLevel, DropSource::Elite, Moon_LootLuck(), rng));
            drops++;
        }
    }
    for (size_t i = 0; i < b.Enemies().size() && drops < kMaxDrops && !sScene.boss; i++) {
        if (Rand_ZeroOne() < kDropChance * Moon_DropScale()) {
            Item item = RollItem(itemLevel, DropSource::Common, Moon_LootLuck(), rng); // only common enemies so far
            Party_Bag().push_back(item);
            drops++;
            BattleEvent found;
            found.type = BattleEvent::Type::Message;
            found.text = "Found " + item.name + "!";
            found.hold = true;
            Rgb c = RarityColor(item.rarity);
            found.color = ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8);
            sScene.queue.push_back(found);
        }
    }

    // Consumables: potions, herbs and the like, separate from gear
    int consumableDrops = 0;
    for (size_t i = 0; i < b.Enemies().size() && consumableDrops < kMaxConsumableDrops; i++) {
        if (Rand_ZeroOne() < kConsumableDropChance) {
            ConsumableId id = RollConsumable(rng);
            consumableDrops++;
            BattleEvent found;
            found.type = BattleEvent::Type::Message;
            found.hold = true;
            found.text = Party_AddConsumable(id) > 0 ? "Found a " + ConsumableOf(id).name + "!"
                                                     : "Found a " + ConsumableOf(id).name + ", but the bag is full.";
            found.color = 0x90E0A000u; // pale green
            sScene.queue.push_back(found);
        }
    }

    if (sScene.boss && BossGate_IsRematch()) {
        sScene.queue.push_back(HeldMessage("Queen Gohma falls once more.", 0x58D85400u));
        Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_BOSS_CLEAR);
    } else if (sScene.boss) {
        // Heart Container: everyone's max HP grows for good, and they're healed
        for (PartyMember& m : members) {
            m.base.maxHp += kHeartContainerHp;
            m.base.hp = m.base.maxHp + 999; // clamped to the max with gear below
            Party_ClampToMax(m);
        }
        sScene.queue.push_back(HeldMessage("Got a Heart Container!\nMax HP +" + std::to_string(kHeartContainerHp) +
                                               " for everyone.",
                                           0xF8380000u));
        sScene.queue.push_back(HeldMessage("The Deku Tree is cleansed!", 0x58D85400u));
        BossGate_SetCleared();
        Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_BOSS_CLEAR);
    } else {
        // Battles in the dungeon wear down the seal on its boss gate
        std::string gate = BossGate_OnVictory(gPlayState);
        if (!gate.empty()) {
            sScene.queue.push_back(HeldMessage(gate, BossGate_IsOpen() ? 0x3CBCFC00u : 0xC8A0FF00u));
        }
    }

    // Keep the engine's copy in step so EndBattle doesn't undo the level-up HP
    for (size_t i = 0; i < members.size() && i < after.size(); i++) {
        b.PartyMutable()[i].hp = members[i].base.hp;
        b.PartyMutable()[i].mp = members[i].base.mp;
    }
}

// ---- Events -> messages and effects ---------------------------------------------------------------------

// A heavy hit: camera shake, controller rumble, a white flash and a weighty sound.
void Impact(bool critical) {
    if (gPlayState != NULL) {
        s16 quake = Quake_Add(Play_GetCamera(gPlayState, CAM_ID_MAIN), 3);
        Quake_SetSpeed(quake, 20000);
        Quake_SetQuakeValues(quake, critical ? 6 : 4, 0, 0, 0);
        Quake_SetCountdown(quake, critical ? 8 : 6);
    }
    Rumble_Request(0.0f, critical ? 255 : 200, critical ? 14 : 10, 150);
    sScene.impactFlash = critical ? 4 : 3;
    PlaySfx(critical ? NA_SE_IT_HAMMER_HIT : NA_SE_IT_SWORD_STRIKE_HARD);
}

void PlayEventEffects(const BattleEvent& e) {
    auto enemyActor = [](int i) -> Actor* {
        return (i >= 0 && i < (int)sScene.enemyActors.size()) ? sScene.enemyActors[i] : NULL;
    };
    switch (e.type) {
        case BattleEvent::Type::Message:
            // An enemy announcing its move lunges (bosses and larvae have attack animations)
            if (e.actorIsEnemy && e.actor >= 0 && e.target < 0) {
                if (Actor* a = enemyActor(e.actor)) {
                    BattleEnemyActor_Animate(a, BattleAnim::Attack);
                }
            }
            break;
        case BattleEvent::Type::Summon:
            if (Actor* a = enemyActor(e.actor)) {
                BattleEnemyActor_Animate(a, BattleAnim::Special); // Gohma lays her eggs
            }
            SpawnJoiningEnemy(gPlayState, e.target);
            PlaySfx(NA_SE_EN_GOMA_BJR_EGG2);
            break;
        case BattleEvent::Type::Warning:
            if (gPlayState != NULL) {
                s16 quake = Quake_Add(Play_GetCamera(gPlayState, CAM_ID_MAIN), 3);
                Quake_SetSpeed(quake, 12000);
                Quake_SetQuakeValues(quake, 3, 0, 0, 0);
                Quake_SetCountdown(quake, 30);
            }
            Rumble_Request(0.0f, 180, 30, 100);
            PlaySfx(NA_SE_EV_STONE_BOUND);
            break;
        case BattleEvent::Type::Hit:
            if (e.targetIsEnemy) {
                if (Actor* a = enemyActor(e.target)) {
                    Actor_SetColorFilter(a, 0x4000, 255, 0, 8);
                    BattleEnemyActor_Animate(a, BattleAnim::Hurt);
                }
                PlaySfx(NA_SE_IT_SWORD_IMPACT);
            } else {
                sScene.damageFlash = 6;
                PlaySfx(NA_SE_PL_DAMAGE);
            }
            break;
        case BattleEvent::Type::Weak:
        case BattleEvent::Type::Critical:
            Impact(e.type == BattleEvent::Type::Critical);
            break;
        case BattleEvent::Type::Miss:
        case BattleEvent::Type::Null:
        case BattleEvent::Type::Repel:
        case BattleEvent::Type::Drain:
            PlaySfx(NA_SE_SY_ERROR);
            break;
        case BattleEvent::Type::Heal:
            PlaySfx(NA_SE_SY_HP_RECOVER);
            break;
        case BattleEvent::Type::Defeated:
            if (e.targetIsEnemy) {
                if (Actor* a = enemyActor(e.target)) {
                    BattleEnemyActor_Defeat(a);
                }
                bool boss = sScene.battle && e.target >= 0 && e.target < (int)sScene.battle->Enemies().size() &&
                            sScene.battle->Enemies()[e.target].boss;
                PlaySfx(boss ? NA_SE_EN_GOMA_DEAD : NA_SE_EN_EXTINCT);
            }
            break;
        case BattleEvent::Type::Analyze:
            if (sScene.battle && e.target >= 0 && e.target < (int)sScene.battle->Enemies().size()) {
                const Combatant& c = sScene.battle->Enemies()[e.target];
                sAnalyzedKinds.insert(c.kind);
                BattleEvent detail;
                detail.type = BattleEvent::Type::Message;
                detail.text = c.name + "  HP " + std::to_string(c.hp) + "/" + std::to_string(c.maxHp) + "\n" +
                              AffinitySummary(c);
                detail.hold = true;
                sScene.queue.push_front(detail);
            }
            break;
        case BattleEvent::Type::Victory:
            PlaySfx(NA_SE_SY_GET_RUPY);
            break;
        default:
            break;
    }
}

void Queue(const Events& events) {
    for (const BattleEvent& e : events) {
        sScene.queue.push_back(e);
    }
    sScene.ui = Ui::Messages;
    sScene.showingMessage = false;
}

// Pull the next message (applying its effects); false when the queue is empty.
bool NextMessage() {
    while (!sScene.queue.empty()) {
        BattleEvent e = sScene.queue.front();
        sScene.queue.pop_front();
        PlayEventEffects(e);
        if (e.type == BattleEvent::Type::PhaseChange && e.actorIsEnemy == false && !sScene.queue.empty()) {
            continue; // "Your turn" only matters when nothing else is queued
        }
        if (!e.text.empty()) {
            sScene.message = e.text;
            sScene.messageHeld = e.hold;
            sScene.messageColor = e.color;
            sScene.showingMessage = true;
            sScene.timer = 0;
            return true;
        }
    }
    return false;
}

int NextMemberWithPoints() {
    std::vector<PartyMember>& members = Party_Members();
    for (int i = 0; i < (int)members.size(); i++) {
        if (members[i].unspentStatPoints > 0) {
            return i;
        }
    }
    return -1;
}

// After messages: decide what the battle does next.
void Proceed() {
    Battle& b = *sScene.battle;
    switch (b.Phase()) {
        case BattlePhase::PartyCommand:
            sScene.ui = Ui::Command;
            sScene.showingMessage = false;
            break;
        case BattlePhase::EnemyTurn:
            if (sScene.timer >= (sScene.autoBattle ? kAutoThinkFrames : kEnemyThinkFrames)) {
                Queue(b.EnemyAct());
            }
            break;
        case BattlePhase::Victory:
            if (!sScene.rewardsGiven) {
                sScene.rewardsGiven = true;
                QueueRewards();
                sScene.ui = Ui::Messages;
            } else if (NextMemberWithPoints() >= 0) {
                sScene.statMember = NextMemberWithPoints();
                sScene.statCursor = 0;
                sScene.showingMessage = false;
                sScene.ui = Ui::StatPoints;
            } else {
                sScene.ui = Ui::Ending;
            }
            break;
        case BattlePhase::Defeat:
            sScene.defeat = true;
            sScene.ui = Ui::Ending;
            break;
        case BattlePhase::Escaped:
            sScene.ui = Ui::Ending;
            break;
    }
}

// ---- Input ----------------------------------------------------------------------------------------------

struct Buttons {
    bool up = false, down = false, left = false, right = false, a = false, b = false, z = false;
};

Buttons ReadButtons(Input* input) {
    Buttons out;
    const OSContPad& cur = input->cur;
    const OSContPad& press = input->press;
    int dir = 0;
    if (cur.stick_y > 45) {
        dir = 1;
    } else if (cur.stick_y < -45) {
        dir = 2;
    } else if (cur.stick_x < -45) {
        dir = 3;
    } else if (cur.stick_x > 45) {
        dir = 4;
    }
    bool newStick = dir != 0 && dir != sScene.stickDir;
    sScene.stickDir = dir;
    out.up = (press.button & BTN_DUP) || (newStick && dir == 1);
    out.down = (press.button & BTN_DDOWN) || (newStick && dir == 2);
    out.left = (press.button & BTN_DLEFT) || (newStick && dir == 3);
    out.right = (press.button & BTN_DRIGHT) || (newStick && dir == 4);
    out.a = press.button & BTN_A;
    out.b = press.button & BTN_B;
    out.z = press.button & BTN_Z;
    return out;
}

int FirstAliveEnemy() {
    const std::vector<Combatant>& e = sScene.battle->Enemies();
    for (int i = 0; i < (int)e.size(); i++) {
        if (e[i].Alive()) {
            return i;
        }
    }
    return 0;
}

// Consumables the party has at least one of, in ConsumableId order.
std::vector<ConsumableId> OwnedItems() {
    std::vector<ConsumableId> owned;
    const auto& counts = Party_Consumables();
    for (size_t i = 0; i < counts.size(); i++) {
        if (counts[i] > 0) {
            owned.push_back((ConsumableId)i);
        }
    }
    return owned;
}

// Choosing who gets a revival item: fallen allies are valid targets.
bool TargetingFallen() {
    return sScene.pendingSkill == kPendingItem && ConsumableOf(sScene.pendingItem).effect.revive > 0.0f;
}

void StepTarget(int delta, bool enemies) {
    const std::vector<Combatant>& side = enemies ? sScene.battle->Enemies() : sScene.battle->Party();
    int n = (int)side.size();
    bool fallenOk = !enemies && TargetingFallen();
    for (int step = 1; step <= n; step++) {
        int i = ((sScene.targetCursor + delta * step) % n + n) % n;
        if (side[i].Alive() || fallenOk) {
            sScene.targetCursor = i;
            PlaySfx(NA_SE_SY_CURSOR);
            return;
        }
    }
}

void ChooseSkill(int skill) {
    Battle& b = *sScene.battle;
    const Skill& s = b.Party()[b.ActivePartyMember()].skills[skill];
    if (!b.CanUseSkill(skill)) {
        PlaySfx(NA_SE_SY_ERROR);
        return;
    }
    PlaySfx(NA_SE_SY_DECIDE);
    switch (s.target) {
        case TargetType::OneEnemy:
            sScene.pendingSkill = skill;
            sScene.targetCursor = FirstAliveEnemy();
            sScene.ui = Ui::TargetEnemy;
            break;
        case TargetType::OneAlly:
            sScene.pendingSkill = skill;
            sScene.targetCursor = b.ActivePartyMember();
            sScene.ui = Ui::TargetAlly;
            break;
        default:
            Queue(b.UseSkill(skill, -1));
            break;
    }
}

void UseItemOn(int target) {
    Battle& b = *sScene.battle;
    const ConsumableDef& def = ConsumableOf(sScene.pendingItem);
    if (Party_Consumables()[(size_t)def.id] <= 0 || !b.CanUseItem(def.effect, target)) {
        PlaySfx(NA_SE_SY_ERROR); // it would do nothing (full HP, nothing to cure...)
        return;
    }
    PlaySfx(NA_SE_SY_DECIDE);
    Events ev = b.UseItem(def.name, def.effect, target);
    if (!ev.empty()) {
        Party_Consumables()[(size_t)def.id]--;
        Queue(ev);
    }
}

void ChooseItem(ConsumableId id) {
    Battle& b = *sScene.battle;
    const ConsumableDef& def = ConsumableOf(id);
    sScene.pendingItem = id;
    if (def.effect.target == TargetType::AllAllies) {
        UseItemOn(-1);
        return;
    }
    PlaySfx(NA_SE_SY_DECIDE);
    sScene.pendingSkill = kPendingItem;
    // Start on someone it would help
    sScene.targetCursor = b.ActivePartyMember();
    for (int i = 0; i < (int)b.Party().size(); i++) {
        if (ItemHelps(def.effect, b.Party()[i])) {
            sScene.targetCursor = i;
            break;
        }
    }
    sScene.ui = Ui::TargetAlly;
}

void HandleInput(const Buttons& in) {
    Battle& b = *sScene.battle;
    const Combatant& member = b.Party()[std::clamp(b.ActivePartyMember(), 0, (int)b.Party().size() - 1)];

    switch (sScene.ui) {
        case Ui::Command:
            // Two columns of four: up/down within a column, left/right across
            if (in.up || in.down) {
                int col = sScene.commandCursor / 4, row = sScene.commandCursor % 4;
                row = (row + (in.up ? 3 : 1)) % 4;
                sScene.commandCursor = col * 4 + row;
                PlaySfx(NA_SE_SY_CURSOR);
            } else if (in.left || in.right) {
                sScene.commandCursor = (sScene.commandCursor + 4) % CMD_COUNT;
                PlaySfx(NA_SE_SY_CURSOR);
            } else if (in.a) {
                switch (sScene.commandCursor) {
                    case CMD_ATTACK:
                        ChooseSkill(0);
                        break;
                    case CMD_SKILL:
                        if (member.skills.size() > 1) {
                            sScene.skillCursor = 1;
                            sScene.ui = Ui::Skills;
                            PlaySfx(NA_SE_SY_DECIDE);
                        } else {
                            PlaySfx(NA_SE_SY_ERROR);
                        }
                        break;
                    case CMD_ITEM:
                        if (!OwnedItems().empty()) {
                            sScene.itemCursor = std::clamp(sScene.itemCursor, 0, (int)OwnedItems().size() - 1);
                            sScene.ui = Ui::Items;
                            PlaySfx(NA_SE_SY_DECIDE);
                        } else {
                            PlaySfx(NA_SE_SY_ERROR);
                        }
                        break;
                    case CMD_GUARD:
                        PlaySfx(NA_SE_SY_DECIDE);
                        Queue(b.Guard());
                        break;
                    case CMD_PASS:
                        PlaySfx(NA_SE_SY_DECIDE);
                        Queue(b.Pass());
                        break;
                    case CMD_ANALYZE:
                        PlaySfx(NA_SE_SY_DECIDE);
                        sScene.pendingSkill = kPendingAnalyze;
                        sScene.targetCursor = FirstAliveEnemy();
                        sScene.ui = Ui::TargetEnemy;
                        break;
                    case CMD_AUTO:
                        PlaySfx(NA_SE_SY_DECIDE);
                        sScene.autoBattle = true;
                        BattleEnemyActor_SetFast(true);
                        break;
                    case CMD_RUN:
                        if (b.NoEscape()) {
                            PlaySfx(NA_SE_SY_ERROR);
                            Queue(b.Run()); // "There's no escape!" (costs no turn)
                            break;
                        }
                        if (Moon_BlocksEscape()) {
                            // Frenzied by the full moon, the monsters won't let the party go (costs no turn)
                            PlaySfx(NA_SE_SY_ERROR);
                            BattleEvent e;
                            e.type = BattleEvent::Type::Message;
                            e.text = "The full moon frenzies them. There's no escape!";
                            e.color = 0xC8A0FF00u;
                            Queue({ e });
                            break;
                        }
                        PlaySfx(NA_SE_SY_DECIDE);
                        Queue(b.Run());
                        break;
                }
            }
            break;

        case Ui::Skills: {
            int count = (int)member.skills.size() - 1;
            if (in.up || in.down) {
                int i = sScene.skillCursor - 1;
                i = (i + (in.up ? count - 1 : 1)) % count;
                sScene.skillCursor = i + 1;
                PlaySfx(NA_SE_SY_CURSOR);
            } else if (in.z) {
                sScene.showSkillInfo = !sScene.showSkillInfo;
                PlaySfx(NA_SE_SY_CURSOR);
            } else if (in.a) {
                ChooseSkill(sScene.skillCursor);
            } else if (in.b) {
                sScene.ui = Ui::Command;
                PlaySfx(NA_SE_SY_CANCEL);
            }
            break;
        }

        case Ui::Items: {
            std::vector<ConsumableId> owned = OwnedItems();
            if (owned.empty()) {
                sScene.ui = Ui::Command;
                break;
            }
            int count = (int)owned.size();
            sScene.itemCursor = std::clamp(sScene.itemCursor, 0, count - 1);
            if (in.up || in.down) {
                sScene.itemCursor = (sScene.itemCursor + (in.up ? count - 1 : 1)) % count;
                PlaySfx(NA_SE_SY_CURSOR);
            } else if (in.z) {
                sScene.showSkillInfo = !sScene.showSkillInfo;
                PlaySfx(NA_SE_SY_CURSOR);
            } else if (in.a) {
                ChooseItem(owned[sScene.itemCursor]);
            } else if (in.b) {
                sScene.ui = Ui::Command;
                PlaySfx(NA_SE_SY_CANCEL);
            }
            break;
        }

        case Ui::TargetEnemy:
        case Ui::TargetAlly: {
            bool enemies = sScene.ui == Ui::TargetEnemy;
            if (in.left || in.up) {
                StepTarget(-1, enemies);
            } else if (in.right || in.down) {
                StepTarget(1, enemies);
            } else if (in.a) {
                if (sScene.pendingSkill == kPendingItem) {
                    UseItemOn(sScene.targetCursor);
                } else if (sScene.pendingSkill == kPendingAnalyze) {
                    PlaySfx(NA_SE_SY_DECIDE);
                    Queue(b.Analyze(sScene.targetCursor));
                } else {
                    PlaySfx(NA_SE_SY_DECIDE);
                    Queue(b.UseSkill(sScene.pendingSkill, sScene.targetCursor));
                }
            } else if (in.b) {
                PlaySfx(NA_SE_SY_CANCEL);
                sScene.ui = sScene.pendingSkill > 0             ? Ui::Skills
                            : sScene.pendingSkill == kPendingItem ? Ui::Items
                                                                  : Ui::Command;
            }
            break;
        }

        case Ui::StatPoints: {
            std::vector<PartyMember>& members = Party_Members();
            if (sScene.statMember < 0 || sScene.statMember >= (int)members.size()) {
                sScene.ui = Ui::Ending;
                break;
            }
            const int count = (int)Stat::Count;
            if (in.up || in.down) {
                sScene.statCursor = (sScene.statCursor + (in.up ? count - 1 : 1)) % count;
                PlaySfx(NA_SE_SY_CURSOR);
            } else if (in.a) {
                PartyMember& m = members[sScene.statMember];
                if (GetStat(m.base, (Stat)sScene.statCursor) >= kStatCap) {
                    PlaySfx(NA_SE_SY_ERROR);
                    break;
                }
                AddStat(m.base, (Stat)sScene.statCursor, 1);
                m.unspentStatPoints--;
                PlaySfx(NA_SE_SY_DECIDE);
                if (m.unspentStatPoints <= 0) {
                    int next = NextMemberWithPoints();
                    if (next >= 0) {
                        sScene.statMember = next;
                        sScene.statCursor = 0;
                    } else {
                        sScene.ui = Ui::Ending;
                    }
                }
            }
            break;
        }

        default:
            break;
    }
}

void RegisterBattleScene() {
    bool enabled = CVAR_CRAWLER_VALUE != 0;

    if (!enabled) {
        Battle_Abort();
    }

    // Freeze the dungeon during a battle: only Link (who runs the menus) and the battle enemies update
    COND_HOOK(ShouldActorUpdate, enabled, [](void* refActor, bool* result) {
        if (sScene.ui == Ui::None || gPlayState == NULL) {
            return;
        }
        Actor* actor = (Actor*)refActor;
        if (actor != &GET_PLAYER(gPlayState)->actor && !BattleEnemyActor_Is(actor)) {
            *result = false;
        }
    });

    // During a battle its own interface replaces the HUD (hearts, buttons, minimap) entirely
    COND_VB_SHOULD(VB_DRAW_INTERFACE, enabled, {
        PlayState* play = va_arg(args, PlayState*);
        if (Battle_IsActive() && play->pauseCtx.state == 0) {
            Battle_DrawUi(play);
            *should = false;
        }
    });

    // Link's hearts aren't his battle HP: no low-health alarm or tired panting while fighting
    COND_VB_SHOULD(VB_HEALTH_METER_BE_CRITICAL, enabled, {
        if (Battle_IsActive()) {
            *should = false;
        }
    });

    // Queen Gohma's chamber: the crawler's battle replaces her vanilla fight
    COND_HOOK(ShouldActorInit, enabled, [](void* refActor, bool* result) {
        Actor* actor = (Actor*)refActor;
        if (actor->id == ACTOR_BOSS_GOMA && gPlayState != NULL && BossGate_InBossRoom(gPlayState)) {
            // Remember where she lives: the fight starts when Link walks into this room
            sArenaKnown = true;
            sArenaRoom = actor->room;
            sArenaPos = actor->world.pos;
            *result = false;
        }
    });

    COND_HOOK(OnSceneInit, enabled, [](int16_t sceneNum) {
        // Actors from the old scene are gone; drop the battle without touching them
        sScene.enemyActors.clear();
        sScene.battle.reset();
        sScene.ui = Ui::None;
        sScene.queue.clear();
        sScene.boss = false;
        sBossDone = false;
        sDanger = 0;
        // Coming into a new area patches the party up (Sheikah Stones and inns come later)
        if (sceneNum != sLastScene) {
            EnsureParty();
            HealParty();
        }
        sLastScene = sceneNum;
    });

    // Loading a save: Save/SaveHooks resets everything (Battle_ResetForLoad) and then applies the save
}

} // namespace

// ---- Public ---------------------------------------------------------------------------------------------

void Battle_ResetForLoad() {
    sAnalyzedKinds.clear();
    sDanger = 0;
    sLastScene = -1;
}

std::set<int>& Battle_Bestiary() {
    return sAnalyzedKinds;
}

bool Battle_IsActive() {
    return sScene.ui != Ui::None;
}

void Battle_Abort() {
    KillEnemyActors();
    sScene.battle.reset();
    sScene.ui = Ui::None;
    sScene.queue.clear();
    sScene.showingMessage = false;
}

void Battle_OnStep(PlayState* play, Player* player) {
    if (Battle_IsActive() || BossGate_InBossRoom(play) || play->transitionTrigger != TRANS_TRIGGER_OFF) {
        return; // no random encounters in a boss's chamber, or while a warp is fading out
    }
    MoonHud_OnStep();
    // The fuller the moon, the faster danger builds
    int gain = kDangerStepMin + (int)(Rand_ZeroOne() * (kDangerStepMax - kDangerStepMin + 1) * 0.9999f);
    sDanger += (int)(gain * Moon_EncounterScale() + 0.5f);
    if (sDanger >= kDangerTrigger) {
        StartRandomBattle(play, player);
    }
}

void Battle_FieldTick(PlayState* play, Player* player) {
    if (!BossGate_InBossRoom(play) || Battle_IsActive() || sBossDone ||
        (BossGate_IsCleared() && !BossGate_IsRematch()) ||
        play->transitionTrigger != TRANS_TRIGGER_OFF) {
        return;
    }
    // Link walks down the corridor himself; she comes for him once he's inside her chamber
    if (!(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
        return;
    }
    // (the same spot OoT watches for: the doorway every way in passes through)
    const Vec3f& pos = player->actor.world.pos;
    bool atEntrance =
        fabsf(pos.x - kArenaEntranceX) < kArenaEntranceBox && fabsf(pos.z - kArenaEntranceZ) < kArenaEntranceBox;
    if (atEntrance) {
        // Step into the open floor of the arena, facing its centre, for her entrance
        Vec3f stand = { kArenaStandX, pos.y + 40.0f, kArenaStandZ };
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &stand);
        if (floorY > BGCHECK_Y_MIN + 1.0f && fabsf(floorY - pos.y) < 60.0f) {
            player->actor.world.pos = { kArenaStandX, floorY, kArenaStandZ };
            player->actor.prevPos = player->actor.world.pos;
        }
        StartBossBattle(play, player);
    }
}

void Battle_Update(PlayState* play, Player*, void* inputPtr) {
    if (!Battle_IsActive() || !sScene.battle) {
        return;
    }
    Input* input = (Input*)inputPtr;
    Buttons in = ReadButtons(input);
    sScene.timer++;
    if (sScene.damageFlash > 0) {
        sScene.damageFlash--;
    }
    if (sScene.impactFlash > 0) {
        sScene.impactFlash--;
    }

    if (sScene.autoBattle && in.b) {
        // B stops auto-battle; the current action finishes and the next turn waits for commands
        sScene.autoBattle = false;
        BattleEnemyActor_SetFast(false);
        PlaySfx(NA_SE_SY_CANCEL);
        in.b = false;
    }

    if (sScene.boss) {
        CentreBossInView(); // her eye in the middle of the view, whatever her animation does
    }

    switch (sScene.ui) {
        case Ui::BossIntro:
            UpdateBossIntro(in.a && sIntro.timer > 5);
            break;

        case Ui::Intro:
            if (sScene.timer >= kIntroFrames || (in.a && sScene.timer >= kMessageMinFrames)) {
                sScene.showingMessage = false;
                sScene.ui = Ui::Command;
            }
            break;

        case Ui::Messages:
            if (sScene.showingMessage) {
                bool pressed = in.a && sScene.timer >= kMessageMinFrames;
                int frames = sScene.messageHeld ? (sScene.autoBattle ? kAutoHeldFrames : -1)
                                                : (sScene.autoBattle ? kAutoMessageFrames : kMessageFrames);
                bool done = pressed || (frames >= 0 && sScene.timer >= frames);
                if (done) {
                    sScene.showingMessage = false;
                    sScene.timer = 0;
                }
            } else if (!NextMessage()) {
                Proceed();
            }
            break;

        case Ui::Ending:
            if (sScene.boss) {
                // Win or lose, the boss fight ends with a warp back up to the dungeon entrance (losing is no game
                // over: the party is patched up there and can take the gate again)
                sBossDone = true;
                EndBattle();
                BossGate_ReturnToDungeon(play);
                break;
            }
            if (sScene.defeat) {
                // Link has fallen: hand over to the game's own death and game over
                gSaveContext.health = 0;
            }
            EndBattle();
            break;

        case Ui::Command:
            if (sScene.autoBattle) {
                // Weapon attack on the weakest-looking foe: the one with the least HP left
                Battle& b = *sScene.battle;
                int target = FirstAliveEnemy();
                for (int i = 0; i < (int)b.Enemies().size(); i++) {
                    if (b.Enemies()[i].Alive() && b.Enemies()[i].hp < b.Enemies()[target].hp) {
                        target = i;
                    }
                }
                Queue(b.UseSkill(0, target));
                break;
            }
            HandleInput(in);
            break;

        default:
            HandleInput(in);
            break;
    }
    (void)play;
}

// ---- Drawing --------------------------------------------------------------------------------------------
// Outside the anonymous namespace: OPEN_DISPS declares helpers at block scope that must resolve globally.

// The Famicom Megami Tensei look (see UI/RetroUI): black windows with white rules, our pixel font, ▶ cursors.

int Battle_DangerLevel() {
    return sDanger >= kDangerRed ? 2 : (sDanger >= kDangerYellow ? 1 : 0);
}

namespace {

std::string Upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
    }
    return s;
}

// Condition column: GOOD, PSN, BRN or DOWN, in its colour
void Condition(const Combatant& c, std::string& text, Retro::Color& color) {
    if (!c.Alive()) {
        text = "DOWN";
        color = Retro::Red;
    } else if (c.skipTurn || c.stunTurns > 0) {
        text = "STN";
        color = Retro::Yellow;
    } else if (c.ailment == Ailment::Poison) {
        text = "PSN";
        color = Retro::Violet;
    } else if (c.ailment == Ailment::Burn) {
        text = "BRN";
        color = Retro::Orange;
    } else {
        text = "GOOD";
        color = Retro::Green;
    }
}

} // namespace

void Battle_DrawUi(PlayState* play) {
    if (!Battle_IsActive() || !sScene.battle) {
        return;
    }
    using namespace Retro;
    Battle& b = *sScene.battle;
    bool partyTurn = b.Phase() == BattlePhase::PartyCommand;
    bool blinkOn = (play->gameplayFrames / 4) % 2 == 0;

    // Layout (320x240), as on the Famicom: turn icons and the moon on a top bar, the 3D scene in a window
    // (RetroScreen), the message window under it, then commands (two columns) and the party table. Skill,
    // item and target lists open as windows over the scene.
    const RetroRect view = kBattleView;
    const int iconY = 6;
    const int msgX = 6, msgY = view.y + view.h + 6, msgW = SCREEN_WIDTH - 12, msgH = 28;
    const size_t msgChars = (size_t)(msgW - 28) / kCharW; // after the ▶ marker
    std::vector<std::string> msgLines = Wrap(sScene.message, msgChars);
    if (msgLines.size() > 2) {
        msgLines.resize(2);
    }
    const int bottomY = msgY + msgH + 4, bottomH = SCREEN_HEIGHT - 4 - bottomY;
    const int cmdX = 6, cmdW = 148, cmdY = bottomY, cmdH = bottomH;
    const int partyX = cmdX + cmdW + 4, partyY = bottomY, partyW = SCREEN_WIDTH - 6 - partyX, partyH = bottomH;
    const int partyRows = (int)std::min<size_t>(b.Party().size(), 3);
    const int listX = 96, listW = SCREEN_WIDTH - 6 - listX, listBottom = view.y + view.h - 2;

    bool commandUi = sScene.ui == Ui::Command || sScene.ui == Ui::Skills || sScene.ui == Ui::Items ||
                     sScene.ui == Ui::TargetEnemy || sScene.ui == Ui::TargetAlly;
    std::vector<ConsumableId> owned = sScene.ui == Ui::Items ? OwnedItems() : std::vector<ConsumableId>();
    const Combatant& active = b.Party()[std::clamp(b.ActivePartyMember(), 0, (int)b.Party().size() - 1)];
    int listRows = 0;
    if (sScene.ui == Ui::Skills) {
        listRows = (int)active.skills.size() - 1;
    } else if (sScene.ui == Ui::Items) {
        listRows = (int)owned.size();
    } else if (sScene.ui == Ui::TargetEnemy) {
        listRows = (int)b.Enemies().size() + 2; // + two lines of affinity info
    } else if (sScene.ui == Ui::TargetAlly) {
        listRows = (int)b.Party().size();
    }
    const int listH = listRows * kLineH + 8;
    const int listY = listBottom - listH;

    // Skill / item info (Z): description plus a stats line, above the list
    const size_t infoChars = (size_t)(listW - 12) / kCharW;
    std::vector<std::string> infoLines;
    std::string info3;
    bool showInfo = sScene.ui == Ui::Skills && sScene.showSkillInfo && sScene.skillCursor > 0 &&
                    sScene.skillCursor < (int)active.skills.size();
    if (showInfo) {
        const Skill& sk = active.skills[sScene.skillCursor];
        infoLines = Wrap(sk.description, infoChars);
        const char* who = sk.target == TargetType::OneEnemy     ? "ONE FOE"
                          : sk.target == TargetType::AllEnemies ? "ALL FOES"
                          : sk.target == TargetType::OneAlly    ? "ONE ALLY"
                                                                : "PARTY";
        info3 = std::string(sk.heal ? "HEALS " : "POWER ") + std::to_string(sk.power) + "  " + who;
    }
    if (sScene.ui == Ui::Items && sScene.showSkillInfo && !owned.empty()) {
        showInfo = true;
        const ConsumableDef& def = ConsumableOf(owned[std::clamp(sScene.itemCursor, 0, (int)owned.size() - 1)]);
        infoLines = Wrap(def.description, infoChars);
        info3 = def.effect.target == TargetType::AllAllies ? "PARTY" : "ONE ALLY";
    }
    const int infoH = (int)infoLines.size() * kLineH + kLineH + 10;
    const int infoY = listY - infoH - 2;

    const int ptsW = 232, ptsH = 128, ptsX = (SCREEN_WIDTH - ptsW) / 2, ptsY = view.y + (view.h - ptsH) / 2;
    bool pointsUi = sScene.ui == Ui::StatPoints && sScene.statMember >= 0 &&
                    sScene.statMember < (int)Party_Members().size();

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);
    RetroScreen_DrawFrame(p, view);

    if (sScene.damageFlash > 0) {
        p.Rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WithAlpha(Red, (u8)(sScene.damageFlash * 18)));
    }
    if (sScene.impactFlash > 0) {
        p.Rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WithAlpha(White, (u8)(sScene.impactFlash * 30)));
    }

    // ---- Top: Press Turn icons (blue for the party, red for enemies; blinking icons flash), the moon
    const PressTurns& icons = b.Icons();
    for (int i = 0; i < icons.Total(); i++) {
        bool blinking = i >= icons.solid;
        int x = 10 + i * 13;
        p.Rect(x, iconY, 11, 11, White);
        p.Rect(x + 1, iconY + 1, 9, 9, Black);
        if (blinking && !blinkOn) {
            continue;
        }
        p.Rect(x + 2, iconY + 2, 7, 7, partyTurn ? Blue : Red);
    }
    p.Text(10 + icons.Total() * 13 + 4, iconY + 2, partyTurn ? "PRESS TURN" : "ENEMY TURN", White);
    const int moonW = MoonHud_BoxWidth(16);
    MoonHud_DrawBox(p, SCREEN_WIDTH - 6 - moonW, 2, 16);
    int rightEdge = SCREEN_WIDTH - 6 - moonW - 6;
    if (b.HasTurnLimit()) {
        // Turns left to win: red and flashing once the ceiling starts to tremble
        int left = std::max(0, b.TurnsLeft());
        bool urgent = left <= Battle::kTimeWarning;
        std::string t = "TIME " + std::to_string(left);
        if (!urgent || blinkOn) {
            p.TextRight(rightEdge, iconY + 2, t, urgent ? Red : Cream);
        }
        rightEdge -= (int)Length(t) * kCharW + 8;
    }
    if (sScene.autoBattle) {
        p.TextRight(rightEdge, iconY + 2, b.HasTurnLimit() ? "AUTO" : "AUTO B:STOP", Green);
    }

    // ---- Message window (always there, like the Famicom's; empty between messages)
    p.Window(msgX, msgY, msgW, msgH);
    p.Text(msgX + 8, msgY + 6, "▶", Green);
    if (sScene.showingMessage) {
        Color mc = FromPacked(sScene.messageColor, White);
        for (size_t i = 0; i < msgLines.size(); i++) {
            p.Text(msgX + 20, msgY + 6 + (int)i * kLineH, msgLines[i], mc);
        }
        if (sScene.messageHeld && blinkOn) {
            p.Text(msgX + msgW - 14, msgY + msgH - 10, "▼", Yellow); // press A
        }
    }

    // ---- The boss's title card, over the bottom of the view as she lands
    if (sScene.ui == Ui::BossIntro && sIntro.phase == IntroPhase::Title) {
        int fade = std::min(255, sIntro.timer * 32);
        int cardY = view.y + view.h - 46;
        p.Rect(view.x + 2, cardY, view.w - 4, 40, WithAlpha(Black, (u8)std::min(200, fade)));
        p.TextCenterScaled(SCREEN_WIDTH / 2, cardY + 6, "QUEEN GOHMA", WithAlpha(Red, (u8)fade), 2);
        p.TextCenter(SCREEN_WIDTH / 2, cardY + 27, "PARASITIC ARMORED ARACHNID", WithAlpha(Cream, (u8)fade));
    }

    // ---- Commands: two columns of four (greyed out while messages play)
    p.Window(cmdX, cmdY, cmdW, cmdH);
    for (int i = 0; i < CMD_COUNT; i++) {
        bool sel = commandUi && sScene.ui == Ui::Command && i == sScene.commandCursor;
        // no running at Full Moon or from a boss
        bool enabled = commandUi && !(i == CMD_RUN && (Moon_BlocksEscape() || b.NoEscape()));
        int x = cmdX + 4 + (i / 4) * 70, y = cmdY + 6 + (i % 4) * kLineH;
        if (sel) {
            p.Text(x, y, "▶", Yellow);
        }
        p.Text(x + 9, y, Upper(kCommandNames[i]), sel ? Yellow : (enabled ? White : Grey));
    }

    // ---- Lists beside the commands
    if (listRows > 0) {
        p.Window(listX, listY, listW, listH);
    }
    auto option = [&](int x, int y, const std::string& s, bool selected, bool enabled) {
        if (selected) {
            p.Text(x, y, "▶", Yellow);
        }
        p.Text(x + 9, y, s, selected ? Yellow : (enabled ? White : Grey));
    };

    if (sScene.ui == Ui::Skills) {
        for (int i = 1; i < (int)active.skills.size(); i++) {
            const Skill& s = active.skills[i];
            int y = listY + 5 + (i - 1) * kLineH;
            bool sel = i == sScene.skillCursor;
            bool usable = b.CanUseSkill(i);
            Color c = sel ? Yellow : (usable ? White : Grey);
            // Columns: name, element (or Heal), cost right-aligned
            std::string cost = s.mpCost > 0 ? std::to_string(s.mpCost) + "MP"
                                            : (s.hpCost > 0 ? std::to_string(s.hpCost) + "HP" : "");
            std::string element = s.heal ? "Heal" : ElementName(s.element);
            option(listX + 4, y, Fit(s.name, 13), sel, usable);
            p.Text(listX + 4 + 15 * kCharW, y, element.substr(0, 6), sel ? Yellow : (usable ? Green : Grey));
            p.TextRight(listX + listW - 6, y, cost, c);
        }
    }

    if (showInfo) {
        p.Window(listX, infoY, listW, infoH);
        for (size_t i = 0; i < infoLines.size(); i++) {
            p.Text(listX + 6, infoY + 6 + (int)i * kLineH, infoLines[i], White);
        }
        p.Text(listX + 6, infoY + 8 + (int)infoLines.size() * kLineH, info3, Green);
    } else if (sScene.ui == Ui::Skills || sScene.ui == Ui::Items) {
        p.TextRight(listX + listW - 2, listY - 10, "Z:INFO", Grey);
    }

    if (sScene.ui == Ui::Items) {
        for (int i = 0; i < (int)owned.size(); i++) {
            const ConsumableDef& def = ConsumableOf(owned[i]);
            bool sel = i == sScene.itemCursor;
            int y = listY + 5 + i * kLineH;
            option(listX + 4, y, def.name, sel, true);
            p.TextRight(listX + listW - 6, y, "×" + std::to_string(Party_Consumables()[(size_t)def.id]),
                        sel ? Yellow : White);
        }
    }

    if (sScene.ui == Ui::TargetEnemy) {
        const std::vector<Combatant>& e = b.Enemies();
        for (int i = 0; i < (int)e.size(); i++) {
            option(listX + 4, listY + 5 + i * kLineH, Upper(e[i].name), i == sScene.targetCursor, e[i].Alive());
        }
        const Combatant& t = e[std::clamp(sScene.targetCursor, 0, (int)e.size() - 1)];
        std::string info = sAnalyzedKinds.count(t.kind) ? AffinitySummary(t) : "Unknown - try Analyze";
        std::vector<std::string> lines = Wrap(info, (size_t)(listW - 18) / kCharW);
        for (size_t i = 0; i < lines.size() && i < 2; i++) {
            p.Text(listX + 13, listY + 5 + ((int)e.size() + (int)i) * kLineH, lines[i], Green);
        }
    }

    if (sScene.ui == Ui::TargetAlly) {
        const std::vector<Combatant>& pt = b.Party();
        for (int i = 0; i < (int)pt.size(); i++) {
            char line[40];
            snprintf(line, sizeof(line), "%-6s HP %3d/%-3d", Upper(pt[i].name).c_str(), pt[i].hp, pt[i].maxHp);
            option(listX + 4, listY + 5 + i * kLineH, line, i == sScene.targetCursor, pt[i].Alive() || TargetingFallen());
        }
    }

    // ---- Party table: NAME HP MP COND
    p.Window(partyX, partyY, partyW, partyH);
    {
        int y = partyY + 6;
        p.Text(partyX + 12, y, "NAME", Green);
        p.TextRight(partyX + 88, y, "HP", Green);
        p.TextRight(partyX + 116, y, "MP", Green);
        p.Text(partyX + 122, y, "COND", Green);
        for (int i = 0; i < partyRows; i++) {
            const Combatant& c = b.Party()[i];
            y = partyY + 6 + (i + 1) * kLineH;
            bool isActive = partyTurn && commandUi && i == b.ActivePartyMember();
            Color nameColor = isActive ? Yellow : (c.Alive() ? White : Grey);
            if (isActive) {
                p.Text(partyX + 3, y, "▶", Yellow);
            }
            p.Text(partyX + 12, y, Fit(Upper(c.name), 6), nameColor);
            p.TextRight(partyX + 88, y, std::to_string(c.hp), c.hp * 4 <= c.maxHp ? Red : White);
            p.TextRight(partyX + 116, y, std::to_string(c.mp), White);
            std::string cond;
            Color cc;
            Condition(c, cond, cc);
            p.Text(partyX + 122, y, cond, cc);
        }
    }

    // ---- Level-up points: portrait, then the stats on bars; the cursor row shows the bonus point
    if (pointsUi) {
        const PartyMember& pm = Party_Members()[sScene.statMember];
        p.Window(ptsX, ptsY, ptsW, ptsH);
        p.Rect(ptsX + 8, ptsY + 8, 44, 44, White);
        p.Rect(ptsX + 10, ptsY + 10, 40, 40, Black);
        p.Portrait(pm.base.name, ptsX + 10, ptsY + 10, 40);
        p.Text(ptsX + 60, ptsY + 10, "✻" + Upper(pm.base.name) + "✻", White);
        p.Text(ptsX + 60, ptsY + 22, "LEVEL UP!", Yellow);
        char bonus[32];
        snprintf(bonus, sizeof(bonus), "BONUS ×%d", pm.unspentStatPoints);
        p.Text(ptsX + 60, ptsY + 34, bonus, Green);
        p.TextRight(ptsX + ptsW - 8, ptsY + 10, "Lv" + std::to_string(pm.base.level), White);
        for (int i = 0; i < (int)Stat::Count; i++) {
            bool sel = i == sScene.statCursor;
            int y = ptsY + 62 + i * 12;
            int value = GetStat(pm.base, (Stat)i);
            bool capped = value >= kStatCap;
            if (sel) {
                p.Rect(ptsX + 4, y - 2, ptsW - 8, 11, { 0, 40, 0, 255 });
                p.Text(ptsX + 6, y, "▶", Yellow);
            }
            p.Text(ptsX + 16, y, Upper(StatName((Stat)i)), sel ? Yellow : White);
            p.TextRight(ptsX + 56, y, std::to_string(value), White);
            int lit = std::min(20, value / 2);
            p.Bar(ptsX + 62, y, 20, lit, 3, 7, 1, GreenLight, GreenDark);
            if (sel && !capped) {
                p.Rect(ptsX + 62 + std::min(lit, 19) * 4, y, 3, 7, Yellow); // where the point goes
            }
            if (capped) {
                p.Text(ptsX + 148, y, "MAX", Grey);
            } else if (sel) {
                p.Text(ptsX + 148, y, "+1", Yellow);
            }
        }
        p.TextRight(ptsX + ptsW - 8, ptsY + ptsH - 11, "A:RAISE", Grey);
    }

    MoonHud_Tick();
    CLOSE_DISPS(play->state.gfxCtx);
}

static RegisterShipInitFunc initFunc(RegisterBattleScene, { CVAR_CRAWLER_NAME });
