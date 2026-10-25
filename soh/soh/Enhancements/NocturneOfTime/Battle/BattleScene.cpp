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
constexpr int kMessageFrames = 24;    // how long a battle message stays up (20 game frames = 1 second)
constexpr int kMessageMinFrames = 5;  // A can skip a message after this
constexpr int kEnemyThinkFrames = 8;  // pause before each enemy acts
constexpr int kIntroFrames = 20;
constexpr int kStartLevel = 5;
constexpr f32 kEnemyDistance = 160.0f;
constexpr f32 kEnemyMinDistance = 90.0f;
constexpr f32 kEnemyGap = 14.0f; // space between neighbouring enemies

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
    bool messageHeld = false; // waits for A
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
    // Space enemies by their size so wide ones (Skulltulas) don't overlap
    auto widthOf = [](int kind) -> f32 {
        switch ((EnemyKind)kind) {
            case EnemyKind::Skulltula:
                return 80.0f;
            case EnemyKind::FireKeese:
                return 46.0f;
            default:
                return 42.0f;
        }
    };
    f32 rowWidth = kEnemyGap * (f32)(enemies.size() - 1);
    for (const Combatant& e : enemies) {
        rowWidth += widthOf(e.kind);
    }
    sScene.enemyActors.clear();
    f32 cursor = -rowWidth * 0.5f;
    for (size_t i = 0; i < enemies.size(); i++) {
        f32 w = widthOf(enemies[i].kind);
        f32 lateral = cursor + w * 0.5f;
        cursor += w + kEnemyGap;
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
    e.hold = true;
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
            lv.hold = true;
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
                detail.text = c.name + ": " + AffinitySummary(c);
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
                bool pressed = in.a && sScene.timer >= kMessageMinFrames;
                bool done = sScene.messageHeld ? pressed : (sScene.timer >= kMessageFrames || pressed);
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

    // Layout (320x240): turn icons top-left, messages along the top, commands bottom-left with lists beside
    // them, party along the bottom. The middle of the screen is left to the enemies.
    const int iconY = 16;
    const int msgX = 10, msgY = 32, msgW = SCREEN_WIDTH - 20;
    const size_t msgChars = (size_t)(msgW - 24) / 8;
    // Long messages wrap onto a second line at a space
    std::string msg1 = sScene.message, msg2;
    if (msg1.size() > msgChars) {
        size_t cut = msg1.rfind(' ', msgChars);
        cut = (cut == std::string::npos) ? msgChars : cut;
        msg2 = msg1.substr(cut + 1, msgChars);
        msg1 = msg1.substr(0, cut);
    }
    const int msgH = msg2.empty() ? 14 : 24;
    const int cmdX = 10, cmdY = 134, cmdW = 76, cmdH = CMD_COUNT * 10 + 4;
    const int listX = 92, listW = SCREEN_WIDTH - 102, listBottom = cmdY + cmdH;
    const int partyX = 10, partyY = 204, partyW = SCREEN_WIDTH - 20, partyH = 30;
    const int colW = partyW / 2;

    bool commandUi = sScene.ui == Ui::Command || sScene.ui == Ui::Skills || sScene.ui == Ui::TargetEnemy ||
                     sScene.ui == Ui::TargetAlly;
    const Combatant& active = b.Party()[std::clamp(b.ActivePartyMember(), 0, (int)b.Party().size() - 1)];
    int listRows = 0;
    if (sScene.ui == Ui::Skills) {
        listRows = (int)active.skills.size() - 1;
    } else if (sScene.ui == Ui::TargetEnemy) {
        listRows = (int)b.Enemies().size() + 2; // + two lines of affinity info
    } else if (sScene.ui == Ui::TargetAlly) {
        listRows = (int)b.Party().size();
    }
    int listH = listRows * 10 + 4;
    int listY = listBottom - listH;

    OPEN_DISPS(play->state.gfxCtx);
    Gfx* gfx = OVERLAY_DISP;
    gSPDisplayList(gfx++, sBattleRectDL);

    if (sScene.damageFlash > 0) {
        Rect(&gfx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 255, 30, 20, (u8)(sScene.damageFlash * 18));
    }

    // Press Turn icons: blue for the party, red for enemies; blinking icons flash
    const PressTurns& icons = b.Icons();
    for (int i = 0; i < icons.Total(); i++) {
        bool blinking = i >= icons.solid;
        int x = 10 + i * 13;
        Rect(&gfx, x, iconY, 10, 10, 20, 20, 30, 220);
        if (blinking && !blinkOn) {
            continue;
        }
        u8 r = partyTurn ? 70 : 255, g = partyTurn ? 160 : 60, bl = partyTurn ? 255 : 60;
        Rect(&gfx, x + 1, iconY + 1, 8, 8, r, g, bl, 255);
    }

    if (sScene.showingMessage) {
        Panel(&gfx, msgX, msgY, msgW, msgH);
    }
    if (commandUi) {
        Panel(&gfx, cmdX, cmdY, cmdW, cmdH);
    }
    if (listRows > 0) {
        Panel(&gfx, listX, listY, listW, listH);
    }

    // Party: one column per member; the one choosing a command is lit up
    Panel(&gfx, partyX, partyY, partyW, partyH);
    for (size_t i = 0; i < b.Party().size() && i < 2; i++) {
        const Combatant& c = b.Party()[i];
        int x = partyX + (int)i * colW;
        bool isActive = partyTurn && commandUi && (int)i == b.ActivePartyMember();
        if (isActive) {
            Rect(&gfx, x + 1, partyY + 1, colW - 2, partyH - 2, 40, 60, 110, 230);
        }
        int hpW = c.maxHp > 0 ? 50 * c.hp / c.maxHp : 0;
        int mpW = c.maxMp > 0 ? 50 * c.mp / c.maxMp : 0;
        Rect(&gfx, x + colW - 56, partyY + 6, 50, 4, 40, 40, 40, 255);
        Rect(&gfx, x + colW - 56, partyY + 6, hpW, 4, 80, 220, 90, 255);
        Rect(&gfx, x + colW - 56, partyY + 17, 50, 3, 40, 40, 40, 255);
        Rect(&gfx, x + colW - 56, partyY + 17, mpW, 3, 90, 140, 255, 255);
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
    auto option = [&](int x, int y, const std::string& s, bool selected, bool enabled) {
        u8 shade = enabled ? 210 : 100;
        text(x, y, std::string(selected ? ">" : " ") + s, selected ? 255 : shade, selected ? 230 : shade,
             selected ? 90 : shade);
    };

    text(10 + icons.Total() * 13 + 4, iconY + 1, partyTurn ? "PRESS TURN" : "ENEMY TURN", 230, 230, 230);

    if (sScene.showingMessage) {
        text(msgX + msgW / 2 - (int)msg1.size() * 4, msgY + 3, msg1, 255, 255, 255);
        if (!msg2.empty()) {
            text(msgX + msgW / 2 - (int)msg2.size() * 4, msgY + 13, msg2, 255, 255, 255);
        }
        if (sScene.messageHeld && blinkOn) {
            text(msgX + msgW - 12, msgY + 3, "A", 120, 220, 120);
        }
    }

    if (commandUi) {
        for (int i = 0; i < CMD_COUNT; i++) {
            option(cmdX + 2, cmdY + 3 + i * 10, kCommandNames[i], sScene.ui == Ui::Command && i == sScene.commandCursor,
                   true);
        }
    }

    if (sScene.ui == Ui::Skills) {
        for (int i = 1; i < (int)active.skills.size(); i++) {
            const Skill& s = active.skills[i];
            std::string cost = s.mpCost > 0 ? std::to_string(s.mpCost) + "MP"
                                            : (s.hpCost > 0 ? std::to_string(s.hpCost) + "HP" : "");
            std::string line = s.name;
            while (line.size() < 15) {
                line += ' ';
            }
            line += std::string(ElementName(s.element)).substr(0, 6);
            while (line.size() < 21) {
                line += ' ';
            }
            line += cost;
            option(listX + 2, listY + 3 + (i - 1) * 10, line, i == sScene.skillCursor, b.CanUseSkill(i));
        }
    }

    if (sScene.ui == Ui::TargetEnemy) {
        const std::vector<Combatant>& e = b.Enemies();
        for (int i = 0; i < (int)e.size(); i++) {
            option(listX + 2, listY + 3 + i * 10, e[i].name, i == sScene.targetCursor, e[i].Alive());
        }
        const Combatant& t = e[std::clamp(sScene.targetCursor, 0, (int)e.size() - 1)];
        std::string info = sAnalyzedKinds.count(t.kind) ? AffinitySummary(t) : "Unknown - try Analyze";
        const size_t width = (size_t)(listW - 12) / 8;
        std::string first = info, second;
        if (info.size() > width) {
            size_t cut = info.rfind(' ', width);
            cut = (cut == std::string::npos) ? width : cut;
            first = info.substr(0, cut);
            second = info.substr(cut + 1, width);
        }
        int infoY = listY + 3 + (int)e.size() * 10;
        text(listX + 10, infoY, first, 150, 200, 255);
        if (!second.empty()) {
            text(listX + 10, infoY + 10, second, 150, 200, 255);
        }
    }

    if (sScene.ui == Ui::TargetAlly) {
        const std::vector<Combatant>& p = b.Party();
        for (int i = 0; i < (int)p.size(); i++) {
            char line[40];
            snprintf(line, sizeof(line), "%-8s HP %3d/%3d", p[i].name.c_str(), p[i].hp, p[i].maxHp);
            option(listX + 2, listY + 3 + i * 10, line, i == sScene.targetCursor, p[i].Alive());
        }
    }

    for (size_t i = 0; i < b.Party().size() && i < 2; i++) {
        const Combatant& c = b.Party()[i];
        int x = partyX + (int)i * colW + 4;
        char line[40];
        snprintf(line, sizeof(line), "%-6s Lv%d", c.name.c_str(), c.level);
        u8 shade = c.Alive() ? 255 : 110;
        text(x, partyY + 4, line, shade, shade, shade);
        snprintf(line, sizeof(line), "HP%3d MP%3d", c.hp, c.mp);
        text(x, partyY + 15, line, 200, 220, 200);
    }

    gfx = GfxPrint_Close(&printer);
    GfxPrint_Destroy(&printer);
    OVERLAY_DISP = gfx;
    CLOSE_DISPS(play->state.gfxCtx);
}

static RegisterShipInitFunc initFunc(RegisterBattleScene, { CVAR_CRAWLER_NAME });
