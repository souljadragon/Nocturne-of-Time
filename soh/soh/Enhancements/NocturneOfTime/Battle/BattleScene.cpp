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
#include "../DungeonCrawler.h"

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
constexpr int kMessageFrames = 18;    // how long a battle message stays up (20 game frames = 1 second)
constexpr int kMessageMinFrames = 5;  // A can skip a message after this
constexpr int kEnemyThinkFrames = 8;  // pause before each enemy acts
constexpr int kIntroFrames = 20;
constexpr int kStartLevel = 5;
constexpr f32 kEnemyDistance = 140.0f;
constexpr f32 kEnemyMinDistance = 75.0f;

enum class Ui { None, Intro, Command, Skills, TargetEnemy, TargetAlly, Messages, Ending };

enum Command { CMD_ATTACK, CMD_SKILL, CMD_GUARD, CMD_PASS, CMD_ANALYZE, CMD_RUN, CMD_COUNT };
const char* kCommandNames[CMD_COUNT] = { "Attack", "Skill", "Guard", "Pass", "Analyze", "Run" };

constexpr int kPendingAnalyze = -2;

struct PartyMember {
    Combatant stats;
    int xp = 0;
};

struct Scene {
    std::unique_ptr<Battle> battle;
    std::vector<Actor*> enemyActors; // parallel to battle->Enemies()
    std::vector<std::string> enemyLabels;
    Ui ui = Ui::None;
    int timer = 0;

    std::deque<BattleEvent> queue;
    std::string message;
    bool showingMessage = false;
    bool rewardsGiven = false;
    bool defeat = false;

    int commandCursor = 0;
    int skillCursor = 0;  // index into the member's skills, from 1
    int targetCursor = 0;
    int pendingSkill = -1; // skill waiting for a target, or kPendingAnalyze

    int stickDir = 0; // last stick direction for edge detection: 0 none, 1 up, 2 down, 3 left, 4 right
    int damageFlash = 0;
};

Scene sScene;
std::vector<PartyMember> sParty;
std::set<int> sAnalyzedKinds; // bestiary: enemy kinds whose affinities are known
int sDanger = 0;
s16 sLastScene = -1;

void EnsureParty() {
    if (sParty.empty()) {
        sParty.push_back({ MakeLink(kStartLevel), XpForLevel(kStartLevel) });
        sParty.push_back({ MakeSaria(kStartLevel), XpForLevel(kStartLevel) });
    }
}

void HealParty() {
    for (PartyMember& m : sParty) {
        m.stats.hp = m.stats.maxHp;
        m.stats.mp = m.stats.maxMp;
    }
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
    f32 r = Rand_ZeroOne();
    int count = r < 0.4f ? 1 : (r < 0.8f ? 2 : 3);
    std::vector<EnemyKind> kinds;
    for (int i = 0; i < count; i++) {
        kinds.push_back((EnemyKind)(int)(Rand_ZeroOne() * (f32)EnemyKind::Count * 0.9999f));
    }
    return kinds;
}

void StartBattle(PlayState* play, Player* player) {
    EnsureParty();

    // Face the most open direction so the enemies have room
    s8 bestFacing = DungeonCrawler_GetFacing();
    f32 bestDist = FreeDistance(play, player->actor.world.pos, (s16)(bestFacing * 0x4000), 240.0f);
    if (bestDist < 160.0f) {
        for (s8 f = 0; f < 4; f++) {
            f32 d = FreeDistance(play, player->actor.world.pos, (s16)(f * 0x4000), 240.0f);
            if (d > bestDist) {
                bestDist = d;
                bestFacing = f;
            }
        }
    }
    DungeonCrawler_SetFacing(bestFacing);

    std::vector<EnemyKind> kinds = RollEncounter();
    std::vector<Combatant> enemies;
    for (EnemyKind k : kinds) {
        enemies.push_back(MakeEnemy(k));
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
    s16 yaw = (s16)(bestFacing * 0x4000);
    Vec3f fwd = { Math_SinS(yaw), 0.0f, Math_CosS(yaw) };
    Vec3f right = { -Math_CosS(yaw), 0.0f, Math_SinS(yaw) };
    f32 dist = CLAMP(bestDist - 40.0f, kEnemyMinDistance, kEnemyDistance);
    f32 spacing = enemies.size() >= 3 ? 50.0f : 34.0f;
    sScene.enemyActors.clear();
    for (size_t i = 0; i < enemies.size(); i++) {
        f32 lateral = ((f32)i - (enemies.size() - 1) * 0.5f) * spacing;
        Vec3f pos = { player->actor.world.pos.x + fwd.x * dist + right.x * lateral, player->actor.world.pos.y,
                      player->actor.world.pos.z + fwd.z * dist + right.z * lateral };
        Vec3f probe = { pos.x, pos.y + 50.0f, pos.z };
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &probe);
        if (floorY <= BGCHECK_Y_MIN + 1.0f || floorY < pos.y - 80.0f) {
            floorY = pos.y;
        }
        sScene.enemyActors.push_back(
            BattleEnemyActor_Spawn(play, enemies[i].kind, pos.x, floorY, pos.z, (s16)(yaw + 0x8000)));
    }

    std::vector<Combatant> party;
    for (PartyMember& m : sParty) {
        party.push_back(m.stats);
    }
    sScene.battle = std::make_unique<Battle>(party, enemies, [] { return Rand_ZeroOne() * 0.99999f; });
    sScene.queue.clear();
    sScene.rewardsGiven = false;
    sScene.defeat = false;
    sScene.commandCursor = 0;
    sScene.ui = Ui::Intro;
    sScene.timer = 0;
    sScene.message = enemies.size() == 1 ? "A " + enemies[0].name + " appears!" : "Enemies appear!";
    sScene.showingMessage = true;
    PlaySfx(NA_SE_SY_FSEL_DECIDE_L);
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
        for (size_t i = 0; i < after.size() && i < sParty.size(); i++) {
            sParty[i].stats.hp = std::max(after[i].hp, 1);
            sParty[i].stats.mp = after[i].mp;
        }
    }
    KillEnemyActors();
    sScene.battle.reset();
    sScene.ui = Ui::None;
    sScene.queue.clear();
    sScene.showingMessage = false;
    sDanger = 0;
}

void QueueRewards() {
    Battle& b = *sScene.battle;
    int xp = b.XpEarned();
    int rupees = b.RupeesEarned();
    BattleEvent e;
    e.type = BattleEvent::Type::Message;
    e.text = "Got " + std::to_string(xp) + " XP and " + std::to_string(rupees) + " rupees.";
    sScene.queue.push_back(e);
    Rupees_ChangeBy((s16)rupees);

    const std::vector<Combatant>& after = b.Party();
    for (size_t i = 0; i < sParty.size(); i++) {
        PartyMember& m = sParty[i];
        m.stats.hp = after[i].hp;
        m.stats.mp = after[i].mp;
        if (!after[i].Alive()) {
            continue; // fallen allies earn nothing
        }
        m.xp += xp;
        while (m.xp >= XpForLevel(m.stats.level + 1)) {
            int oldMaxHp = m.stats.maxHp, oldMaxMp = m.stats.maxMp;
            if (!LevelUp(m.stats)) {
                break;
            }
            // Level-ups add the gained max HP/MP to current HP/MP
            m.stats.hp += m.stats.maxHp - oldMaxHp;
            m.stats.mp += m.stats.maxMp - oldMaxMp;
            BattleEvent lv;
            lv.type = BattleEvent::Type::Message;
            lv.text = m.stats.name + " reached level " + std::to_string(m.stats.level) + "!";
            sScene.queue.push_back(lv);
        }
    }
    // Keep the engine's copy in step so EndBattle doesn't undo the level-up HP
    for (size_t i = 0; i < sParty.size(); i++) {
        b.PartyMutable()[i].hp = sParty[i].stats.hp;
        b.PartyMutable()[i].mp = sParty[i].stats.mp;
    }
}

// ---- Events -> messages and effects ---------------------------------------------------------------------

void PlayEventEffects(const BattleEvent& e) {
    auto enemyActor = [](int i) -> Actor* {
        return (i >= 0 && i < (int)sScene.enemyActors.size()) ? sScene.enemyActors[i] : NULL;
    };
    switch (e.type) {
        case BattleEvent::Type::Hit:
            if (e.targetIsEnemy) {
                if (Actor* a = enemyActor(e.target)) {
                    Actor_SetColorFilter(a, 0x4000, 255, 0, 8);
                }
                PlaySfx(NA_SE_IT_SWORD_IMPACT);
            } else {
                sScene.damageFlash = 6;
                PlaySfx(NA_SE_PL_DAMAGE);
            }
            break;
        case BattleEvent::Type::Weak:
        case BattleEvent::Type::Critical:
            PlaySfx(NA_SE_SY_CORRECT_CHIME);
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
                PlaySfx(NA_SE_EN_EXTINCT);
            }
            break;
        case BattleEvent::Type::Analyze:
            if (sScene.battle && e.target >= 0 && e.target < (int)sScene.battle->Enemies().size()) {
                const Combatant& c = sScene.battle->Enemies()[e.target];
                sAnalyzedKinds.insert(c.kind);
                BattleEvent detail;
                detail.type = BattleEvent::Type::Message;
                detail.text = AffinitySummary(c);
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
            sScene.showingMessage = true;
            sScene.timer = 0;
            return true;
        }
    }
    return false;
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
            if (sScene.timer >= kEnemyThinkFrames) {
                Queue(b.EnemyAct());
            }
            break;
        case BattlePhase::Victory:
            if (!sScene.rewardsGiven) {
                sScene.rewardsGiven = true;
                QueueRewards();
                sScene.ui = Ui::Messages;
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
    bool up = false, down = false, left = false, right = false, a = false, b = false;
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

void StepTarget(int delta, bool enemies) {
    const std::vector<Combatant>& side = enemies ? sScene.battle->Enemies() : sScene.battle->Party();
    int n = (int)side.size();
    for (int step = 1; step <= n; step++) {
        int i = ((sScene.targetCursor + delta * step) % n + n) % n;
        if (side[i].Alive()) {
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

void HandleInput(const Buttons& in) {
    Battle& b = *sScene.battle;
    const Combatant& member = b.Party()[b.ActivePartyMember()];

    switch (sScene.ui) {
        case Ui::Command:
            if (in.up || in.down) {
                sScene.commandCursor = (sScene.commandCursor + (in.up ? CMD_COUNT - 1 : 1)) % CMD_COUNT;
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
                    case CMD_RUN:
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
            } else if (in.a) {
                ChooseSkill(sScene.skillCursor);
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
                PlaySfx(NA_SE_SY_DECIDE);
                if (sScene.pendingSkill == kPendingAnalyze) {
                    Queue(b.Analyze(sScene.targetCursor));
                } else {
                    Queue(b.UseSkill(sScene.pendingSkill, sScene.targetCursor));
                }
            } else if (in.b) {
                PlaySfx(NA_SE_SY_CANCEL);
                bool fromSkills = sScene.pendingSkill > 0;
                sScene.ui = fromSkills ? Ui::Skills : Ui::Command;
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

    COND_HOOK(OnSceneInit, enabled, [](int16_t sceneNum) {
        // Actors from the old scene are gone; drop the battle without touching them
        sScene.enemyActors.clear();
        sScene.battle.reset();
        sScene.ui = Ui::None;
        sScene.queue.clear();
        sDanger = 0;
        // Coming into a new area patches the party up (Sheikah Stones and inns come later)
        if (sceneNum != sLastScene) {
            EnsureParty();
            HealParty();
        }
        sLastScene = sceneNum;
    });

    COND_HOOK(OnLoadGame, enabled, [](int32_t) {
        sParty.clear();
        sAnalyzedKinds.clear();
        sDanger = 0;
        sLastScene = -1;
    });
}

} // namespace

// ---- Public ---------------------------------------------------------------------------------------------

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
    if (Battle_IsActive()) {
        return;
    }
    sDanger += kDangerStepMin + (int)(Rand_ZeroOne() * (kDangerStepMax - kDangerStepMin + 1) * 0.9999f);
    if (sDanger >= kDangerTrigger) {
        StartBattle(play, player);
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

    switch (sScene.ui) {
        case Ui::Intro:
            if (sScene.timer >= kIntroFrames || (in.a && sScene.timer >= kMessageMinFrames)) {
                sScene.showingMessage = false;
                sScene.ui = Ui::Command;
            }
            break;

        case Ui::Messages:
            if (sScene.showingMessage) {
                bool done = sScene.timer >= kMessageFrames || (in.a && sScene.timer >= kMessageMinFrames);
                if (done) {
                    sScene.showingMessage = false;
                    sScene.timer = 0;
                }
            } else if (!NextMessage()) {
                Proceed();
            }
            break;

        case Ui::Ending:
            if (sScene.defeat) {
                // Link has fallen: hand over to the game's own death and game over
                gSaveContext.health = 0;
            }
            EndBattle();
            break;

        default:
            HandleInput(in);
            break;
    }
    (void)play;
}

// ---- Drawing --------------------------------------------------------------------------------------------
// Outside the anonymous namespace: OPEN_DISPS declares helpers at block scope that must resolve globally.

static Gfx sBattleRectDL[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_ZBUFFER | G_SHADE | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                          G_TEXTURE_GEN_LINEAR | G_SHADING_SMOOTH | G_LOD),
    gsDPSetOtherMode(G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                         G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_1PRIMITIVE,
                     G_AC_NONE | G_ZS_PIXEL | G_RM_CLD_SURF | G_RM_CLD_SURF2),
    gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE),
    gsSPEndDisplayList(),
};

static void Rect(Gfx** gfx, s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync((*gfx)++);
    gDPSetPrimColor((*gfx)++, 0, 0, r, g, b, a);
    gDPFillRectangle((*gfx)++, x, y, x + w, y + h);
}

static void Panel(Gfx** gfx, s32 x, s32 y, s32 w, s32 h) {
    Rect(gfx, x - 1, y - 1, w + 2, h + 2, 120, 140, 170, 210);
    Rect(gfx, x, y, w, h, 10, 14, 30, 200);
}

void Battle_DrawDangerGem(PlayState* play, int x, int y) {
    if (Battle_IsActive()) {
        return;
    }
    u8 r = 60, g = 140, b = 255; // blue
    if (sDanger >= kDangerRed) {
        r = 255, g = 50, b = 40;
    } else if (sDanger >= kDangerYellow) {
        r = 255, g = 210, b = 40;
    }
    OPEN_DISPS(play->state.gfxCtx);
    gSPDisplayList(OVERLAY_DISP++, sBattleRectDL);
    Rect(&OVERLAY_DISP, x - 1, y - 1, 9, 9, 20, 20, 30, 220);
    Rect(&OVERLAY_DISP, x, y + 2, 7, 3, r, g, b, 255); // a little diamond
    Rect(&OVERLAY_DISP, x + 2, y, 3, 7, r, g, b, 255);
    CLOSE_DISPS(play->state.gfxCtx);
}

void Battle_DrawUi(PlayState* play) {
    if (!Battle_IsActive() || !sScene.battle) {
        return;
    }
    Battle& b = *sScene.battle;
    bool partyTurn = b.Phase() == BattlePhase::PartyCommand;
    bool blinkOn = (play->gameplayFrames / 4) % 2 == 0;

    OPEN_DISPS(play->state.gfxCtx);
    Gfx* gfx = OVERLAY_DISP;
    gSPDisplayList(gfx++, sBattleRectDL);

    // Damage flash on the whole screen when the party is hit
    if (sScene.damageFlash > 0) {
        Rect(&gfx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 255, 30, 20, (u8)(sScene.damageFlash * 18));
    }

    // Press Turn icons, top centre: blue for the party, red for enemies; blinking icons flash
    const PressTurns& icons = b.Icons();
    int total = icons.Total();
    int iconX = SCREEN_WIDTH / 2 - (total * 14) / 2;
    for (int i = 0; i < total; i++) {
        bool blinking = i >= icons.solid;
        if (blinking && !blinkOn) {
            continue;
        }
        u8 r = partyTurn ? 70 : 255, g = partyTurn ? 160 : 60, bl = partyTurn ? 255 : 60;
        Rect(&gfx, iconX + i * 14, 12, 10, 10, 20, 20, 30, 220);
        Rect(&gfx, iconX + i * 14 + 1, 13, 8, 8, r, g, bl, 255);
    }

    // Panels (text goes on top afterwards)
    bool commandUi = sScene.ui == Ui::Command || sScene.ui == Ui::Skills || sScene.ui == Ui::TargetEnemy ||
                     sScene.ui == Ui::TargetAlly;
    if (commandUi) {
        Panel(&gfx, 14, 70, 88, 6 * 10 + 8);
    }
    if (sScene.ui == Ui::Skills) {
        Panel(&gfx, 108, 70, 200, ((int)b.Party()[b.ActivePartyMember()].skills.size() - 1) * 10 + 8);
    }
    if (sScene.ui == Ui::TargetEnemy || sScene.ui == Ui::TargetAlly) {
        int rows = sScene.ui == Ui::TargetEnemy ? (int)b.Enemies().size() : (int)b.Party().size();
        Panel(&gfx, 108, 70, 200, rows * 10 + 8 + (sScene.ui == Ui::TargetEnemy ? 20 : 0));
    }
    if (sScene.showingMessage) {
        Panel(&gfx, 20, 150, SCREEN_WIDTH - 40, 14);
    }
    // Party panel, bottom right, with HP/MP bars
    const int partyX = 128, partyY = 182;
    Panel(&gfx, partyX, partyY, 184, (int)b.Party().size() * 20 + 4);
    for (size_t i = 0; i < b.Party().size(); i++) {
        const Combatant& c = b.Party()[i];
        int y = partyY + 3 + (int)i * 20;
        int hpW = c.maxHp > 0 ? 60 * c.hp / c.maxHp : 0;
        int mpW = c.maxMp > 0 ? 60 * c.mp / c.maxMp : 0;
        Rect(&gfx, partyX + 118, y + 11, 60, 3, 40, 40, 40, 255);
        Rect(&gfx, partyX + 118, y + 11, hpW, 3, 80, 220, 90, 255);
        Rect(&gfx, partyX + 118, y + 15, 60, 2, 40, 40, 40, 255);
        Rect(&gfx, partyX + 118, y + 15, mpW, 2, 90, 140, 255, 255);
    }

    // ---- Text
    GfxPrint printer;
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);

    auto text = [&](int x, int y, const std::string& s, u8 r, u8 g, u8 bl) {
        GfxPrint_SetColor(&printer, r, g, bl, 255);
        GfxPrint_SetPosPx(&printer, x, y);
        GfxPrint_Printf(&printer, "%s", s.c_str());
    };

    text(SCREEN_WIDTH / 2 - 40, 2, partyTurn ? "PRESS TURN" : "ENEMY TURN", 230, 230, 230);

    if (commandUi) {
        for (int i = 0; i < CMD_COUNT; i++) {
            bool sel = sScene.ui == Ui::Command && i == sScene.commandCursor;
            text(18, 74 + i * 10, std::string(sel ? ">" : " ") + kCommandNames[i], sel ? 255 : 200, sel ? 230 : 200,
                 sel ? 90 : 200);
        }
    }

    if (sScene.ui == Ui::Skills) {
        const Combatant& m = b.Party()[b.ActivePartyMember()];
        for (int i = 1; i < (int)m.skills.size(); i++) {
            const Skill& s = m.skills[i];
            bool sel = i == sScene.skillCursor;
            bool usable = b.CanUseSkill(i);
            std::string cost = s.mpCost > 0 ? std::to_string(s.mpCost) + "MP" : (s.hpCost > 0 ? std::to_string(s.hpCost) + "HP" : "");
            std::string line = std::string(sel ? ">" : " ") + s.name;
            while (line.size() < 17) {
                line += ' ';
            }
            line += cost;
            u8 shade = usable ? 210 : 110;
            text(112, 74 + (i - 1) * 10, line, sel ? 255 : shade, sel ? 230 : shade, sel ? 90 : shade);
        }
    }

    if (sScene.ui == Ui::TargetEnemy) {
        const std::vector<Combatant>& e = b.Enemies();
        for (int i = 0; i < (int)e.size(); i++) {
            bool sel = i == sScene.targetCursor;
            u8 shade = e[i].Alive() ? 210 : 90;
            text(112, 74 + i * 10, std::string(sel ? ">" : " ") + e[i].name, sel ? 255 : shade, sel ? 230 : shade,
                 sel ? 90 : shade);
        }
        // Known affinities for the highlighted enemy
        const Combatant& t = e[std::clamp(sScene.targetCursor, 0, (int)e.size() - 1)];
        std::string info = sAnalyzedKinds.count(t.kind) ? AffinitySummary(t) : "Unknown - try Analyze";
        // Wrap at a space to fit the 24-character panel
        std::string first = info, second;
        if (info.size() > 24) {
            size_t cut = info.rfind(' ', 24);
            cut = (cut == std::string::npos) ? 24 : cut;
            first = info.substr(0, cut);
            second = info.substr(cut + 1, 24);
        }
        text(112, 74 + (int)e.size() * 10 + 2, first, 150, 200, 255);
        if (!second.empty()) {
            text(112, 74 + (int)e.size() * 10 + 11, second, 150, 200, 255);
        }
    }

    if (sScene.ui == Ui::TargetAlly) {
        const std::vector<Combatant>& p = b.Party();
        for (int i = 0; i < (int)p.size(); i++) {
            bool sel = i == sScene.targetCursor;
            text(112, 74 + i * 10, std::string(sel ? ">" : " ") + p[i].name, sel ? 255 : 210, sel ? 230 : 210,
                 sel ? 90 : 210);
        }
    }

    if (sScene.showingMessage) {
        std::string msg = sScene.message.substr(0, 34);
        text(SCREEN_WIDTH / 2 - (int)msg.size() * 4, 153, msg, 255, 255, 255);
    }

    for (size_t i = 0; i < b.Party().size(); i++) {
        const Combatant& c = b.Party()[i];
        int y = partyY + 3 + (int)i * 20;
        bool active = partyTurn && (int)i == b.ActivePartyMember() && sScene.ui != Ui::Messages;
        char line[48];
        snprintf(line, sizeof(line), "%s%-6s Lv%-2d", active ? ">" : " ", c.name.c_str(), c.level);
        u8 nameShade = c.Alive() ? 255 : 120;
        text(partyX + 2, y, line, active ? 255 : nameShade, active ? 230 : nameShade, active ? 90 : nameShade);
        snprintf(line, sizeof(line), "HP%3d MP%3d", c.hp, c.mp);
        text(partyX + 2, y + 9, line, 200, 220, 200);
    }

    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    OVERLAY_DISP = gfx;
    CLOSE_DISPS(play->state.gfxCtx);
}

static RegisterShipInitFunc initFunc(RegisterBattleScene, { CVAR_CRAWLER_NAME });
