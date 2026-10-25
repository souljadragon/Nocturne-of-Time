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
#include "../Loot/Loot.h"
#include "../Party.h"
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
constexpr float kDropChance = 0.35f; // per defeated enemy
constexpr int kMaxDrops = 2;         // per battle
constexpr f32 kEnemyDistance = 160.0f;
constexpr f32 kEnemyMinDistance = 90.0f;
constexpr f32 kEnemyGap = 14.0f; // space between neighbouring enemies

enum class Ui { None, Intro, Command, Skills, Items, TargetEnemy, TargetAlly, Messages, StatPoints, Ending };

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
};

Scene sScene;
std::set<int> sAnalyzedKinds; // bestiary: enemy kinds whose affinities are known
int sDanger = 0;
s16 sLastScene = -1;

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
        kinds.push_back((EnemyKind)(int)(Rand_ZeroOne() * (f32)EnemyKind::Count * 0.9999f));
    }
    return kinds;
}

// Navi is frozen with the rest of the dungeon during a battle and would hang in front of the camera: hide her
// by clearing her draw function, and put it back afterwards (only if she is still the same actor).
Actor* sHiddenNavi = NULL;
ActorFunc sNaviDraw = NULL;

void HideNavi(Player* player) {
    if (player == NULL || player->naviActor == NULL || sHiddenNavi != NULL) {
        return;
    }
    sHiddenNavi = player->naviActor;
    sNaviDraw = sHiddenNavi->draw;
    sHiddenNavi->draw = NULL;
}

void ShowNavi() {
    if (sHiddenNavi != NULL && gPlayState != NULL && GET_PLAYER(gPlayState) != NULL &&
        GET_PLAYER(gPlayState)->naviActor == sHiddenNavi && sHiddenNavi->draw == NULL) {
        sHiddenNavi->draw = sNaviDraw;
    }
    sHiddenNavi = NULL;
    sNaviDraw = NULL;
}

void StartBattle(PlayState* play, Player* player) {
    EnsureParty();
    HideNavi(player);
    sScene.autoBattle = false;
    BattleEnemyActor_SetFast(false);

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
    // Fit the row between the walls: measure the open space to each side of the row's centre and squeeze /
    // shift the row so every enemy stands on open floor
    const Vec3f& origin = player->actor.world.pos;
    Vec3f centre = { origin.x + fwd.x * dist, origin.y, origin.z + fwd.z * dist };
    const f32 kWallMargin = 24.0f;
    f32 freeRight = std::max(0.0f, FreeDistance(play, centre, (s16)(yaw - 0x4000), 220.0f) - kWallMargin);
    f32 freeLeft = std::max(0.0f, FreeDistance(play, centre, (s16)(yaw + 0x4000), 220.0f) - kWallMargin);
    f32 squeeze = 1.0f;
    f32 room = freeLeft + freeRight;
    if (rowWidth > room && rowWidth > 0.0f) {
        squeeze = std::max(0.45f, room / rowWidth); // crowd them together rather than into the wall
    }
    f32 half = rowWidth * squeeze * 0.5f;
    // offset of the row centre along `right` so its edges stay within [-freeLeft, freeRight]
    f32 shift = (half * 2.0f <= room) ? CLAMP(0.0f, half - freeLeft, freeRight - half) : (freeRight - freeLeft) * 0.5f;

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
        sScene.enemyActors.push_back(
            BattleEnemyActor_Spawn(play, enemies[i].kind, pos.x, floorY, pos.z, (s16)(yaw + 0x8000)));
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
    sScene.queue.clear();
    sScene.rewardsGiven = false;
    sScene.defeat = false;
    sScene.commandCursor = 0;
    sScene.ui = Ui::Intro;
    sScene.timer = 0;
    sScene.message = enemies.size() == 1 ? "A " + enemies[0].name + " appears!" : "Enemies appear!";
    sScene.messageColor = 0;
    if (Moon_BlocksEscape()) {
        sScene.message += "\nThe full moon has driven them mad!";
        sScene.messageColor = 0xC8A0FF00u; // violet
    }
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
    ShowNavi();
    sScene.autoBattle = false;
    BattleEnemyActor_SetFast(false);
    sScene.battle.reset();
    sScene.ui = Ui::None;
    sScene.queue.clear();
    sScene.showingMessage = false;
    sDanger = 0;
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
        while (m.xp >= XpForLevel(m.base.level + 1)) {
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
    for (size_t i = 0; i < b.Enemies().size() && drops < kMaxDrops; i++) {
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
                PlaySfx(NA_SE_EN_EXTINCT);
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

    COND_HOOK(OnSceneInit, enabled, [](int16_t sceneNum) {
        // Actors from the old scene are gone; drop the battle without touching them
        sScene.enemyActors.clear();
        sHiddenNavi = NULL; // the old Navi is gone with the old scene
        sNaviDraw = NULL;
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
        Party_Reset();
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
    ShowNavi();
    sScene.battle.reset();
    sScene.ui = Ui::None;
    sScene.queue.clear();
    sScene.showingMessage = false;
}

void Battle_OnStep(PlayState* play, Player* player) {
    if (Battle_IsActive()) {
        return;
    }
    MoonHud_OnStep();
    // The fuller the moon, the faster danger builds
    int gain = kDangerStepMin + (int)(Rand_ZeroOne() * (kDangerStepMax - kDangerStepMin + 1) * 0.9999f);
    sDanger += (int)(gain * Moon_EncounterScale() + 0.5f);
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

void Battle_DrawDangerGem(PlayState* play, int x, int y) {
    if (Battle_IsActive()) {
        return;
    }
    Retro::Color c = Retro::Blue;
    if (sDanger >= kDangerRed) {
        c = Retro::Red;
    } else if (sDanger >= kDangerYellow) {
        c = Retro::Yellow;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Retro::Painter p(&OVERLAY_DISP);
    p.Text(x, y, "◆", c);
    CLOSE_DISPS(play->state.gfxCtx);
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

    // Layout (320x240): turn icons and the moon on top, the message window under them, commands on the left
    // with lists beside them, the party table along the bottom. The middle is left to the enemies.
    const int iconY = 8;
    const int msgX = 10, msgY = 30, msgW = SCREEN_WIDTH - 20;
    const size_t msgChars = (size_t)(msgW - 28) / kCharW; // after the ▶ marker
    std::vector<std::string> msgLines = Wrap(sScene.message, msgChars);
    if (msgLines.size() > 2) {
        msgLines.resize(2);
    }
    const int msgH = msgLines.size() > 1 ? 28 : 18;
    const int partyX = 10, partyY = 190, partyW = SCREEN_WIDTH - 20;
    const int partyRows = (int)std::min<size_t>(b.Party().size(), 3);
    const int partyH = 8 + (partyRows + 1) * kLineH;
    const int cmdX = 10, cmdW = 76, cmdH = CMD_COUNT * kLineH + 8, cmdY = partyY - 4 - cmdH;
    const int listX = cmdX + cmdW + 4, listW = SCREEN_WIDTH - 10 - listX, listBottom = partyY - 4;

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

    const int ptsW = 232, ptsH = 128, ptsX = (SCREEN_WIDTH - ptsW) / 2, ptsY = 50;
    bool pointsUi = sScene.ui == Ui::StatPoints && sScene.statMember >= 0 &&
                    sScene.statMember < (int)Party_Members().size();

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);

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
    MoonHud_DrawBox(p, SCREEN_WIDTH - 10 - moonW, 4, 16);
    if (sScene.autoBattle) {
        p.TextRight(SCREEN_WIDTH - 10 - moonW - 6, iconY + 2, "AUTO B:STOP", Green);
    }

    // ---- Message window
    if (sScene.showingMessage) {
        Color mc = FromPacked(sScene.messageColor, White);
        p.Window(msgX, msgY, msgW, msgH);
        p.Text(msgX + 8, msgY + 6, "▶", Green);
        for (size_t i = 0; i < msgLines.size(); i++) {
            p.Text(msgX + 20, msgY + 6 + (int)i * kLineH, msgLines[i], mc);
        }
        if (sScene.messageHeld && blinkOn) {
            p.Text(msgX + msgW - 14, msgY + msgH - 10, "▼", Yellow); // press A
        }
    }

    // ---- Commands
    if (commandUi) {
        p.Window(cmdX, cmdY, cmdW, cmdH);
        for (int i = 0; i < CMD_COUNT; i++) {
            bool sel = sScene.ui == Ui::Command && i == sScene.commandCursor;
            bool enabled = !(i == CMD_RUN && Moon_BlocksEscape()); // no running from a full-moon fight
            int y = cmdY + 5 + i * kLineH;
            if (sel) {
                p.Text(cmdX + 4, y, "▶", Yellow);
            }
            p.Text(cmdX + 13, y, Upper(kCommandNames[i]), sel ? Yellow : (enabled ? White : Grey));
        }
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

    // ---- Party table: NAME LV HP MP COND
    p.Window(partyX, partyY, partyW, partyH);
    {
        int y = partyY + 5;
        p.Text(partyX + 14, y, "NAME", Green);
        p.TextRight(partyX + 102, y, "LV", Green);
        p.TextRight(partyX + 160, y, "HP", Green);
        p.TextRight(partyX + 212, y, "MP", Green);
        p.Text(partyX + 232, y, "COND", Green);
        for (int i = 0; i < partyRows; i++) {
            const Combatant& c = b.Party()[i];
            y = partyY + 5 + (i + 1) * kLineH;
            bool isActive = partyTurn && commandUi && i == b.ActivePartyMember();
            Color nameColor = isActive ? Yellow : (c.Alive() ? White : Grey);
            if (isActive) {
                p.Text(partyX + 4, y, "▶", Yellow);
            }
            p.Text(partyX + 14, y, Fit(Upper(c.name), 8), nameColor);
            p.TextRight(partyX + 102, y, std::to_string(c.level), White);
            p.TextRight(partyX + 160, y, std::to_string(c.hp), c.hp * 4 <= c.maxHp ? Red : White);
            p.TextRight(partyX + 212, y, std::to_string(c.mp), White);
            std::string cond;
            Color cc;
            Condition(c, cond, cc);
            p.Text(partyX + 232, y, cond, cc);
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
