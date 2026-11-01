#pragma once

// Nocturne of Time: turn-based battle rules (SMT III "Press Turn"), independent of the game engine so they
// can be unit-tested on their own. BattleScene.cpp drives this from inside the game.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Nocturne {

enum class Element : uint8_t {
    Slash,
    Strike,
    Pierce,
    Fire,
    Ice,
    Wind,
    Shock,
    Light,
    Shadow,
    Almighty,
    Count,
};

const char* ElementName(Element e);

inline bool IsPhysical(Element e) {
    return e == Element::Slash || e == Element::Strike || e == Element::Pierce;
}

enum class Affinity : uint8_t { Normal, Weak, Resist, Null, Repel, Drain };

const char* AffinityName(Affinity a);

// Targets are from the user's point of view: an enemy's "AllAllies" is its own side. Self is the user alone.
enum class TargetType : uint8_t { OneEnemy, AllEnemies, OneAlly, AllAllies, Self };

// Status ailments. Poison lasts until cured (even after the battle); Burn wears off after a few turns and at the
// end of the battle. Neither can take the last HP. Stun is different: a skill that inflicts it makes the victim
// lose their next turn (Combatant::stunTurns); it never sits in the ailment slot and doesn't outlast the battle.
enum class Ailment : uint8_t { None, Poison, Burn, Stun, Count };
const char* AilmentName(Ailment a); // "Poison"
const char* AilmentTag(Ailment a);  // "PSN", for the party panel
constexpr int kBurnTurns = 3;
// Damage an ailment deals at the start of its victim's turn.
int AilmentDamage(Ailment a, int maxHp);

struct Skill {
    std::string name;
    Element element = Element::Slash;
    TargetType target = TargetType::OneEnemy;
    int power = 0;       // damage power, or HP restored for heals
    bool heal = false;   // restores HP instead of dealing damage
    int mpCost = 0;
    int hpCost = 0;      // flat HP cost (physical techniques)
    float accuracy = 0.95f;
    Ailment inflict = Ailment::None; // chance to afflict the target on a hit
    float inflictChance = 0.0f;
    std::string description; // shown in the skill menu

    // Support (no damage): stat stages added to every target, as SMT's -kaja (+) and -nda (-) spells.
    int buffAtk = 0, buffDef = 0, buffAgi = 0;
    bool dispelBuffs = false;   // removes every raised stage from the targets (SMT's Dekaja)
    bool dispelDebuffs = false; // removes every lowered stage from the targets (SMT's Dekunda)
    bool charge = false;        // the user's next damaging skill deals kChargeMult times the damage (Focus)
    std::string useText;        // announcement instead of "X uses Y!" (e.g. a boss's telegraph)
    int learnLevel = 1;         // party members know the skill from this level on
    float revive = 0.0f;        // heals: also revives a fallen target with this fraction of max HP
    bool fullHeal = false;      // heals: restores every HP
    bool curesAilments = false; // support: cures poison and burns on its targets

    bool IsSupport() const {
        return !heal && power == 0;
    }
};

// Stat stages, from -kMaxStage to +kMaxStage, last for the whole battle (as in SMT III). Each stage is worth
// kStageStep: +2 attack deals 1.5x damage, +2 defence takes 1/1.5x. Agility stages move hit chance by
// kStageHit each: an evasive target makes the party miss, and a miss costs two Press Turn icons.
constexpr int kMaxStage = 4;
constexpr float kStageStep = 0.25f;
constexpr float kStageHit = 0.08f;
constexpr float kChargeMult = 2.5f;
float StageFactor(int stage);

// What a consumable does when used on someone (see Items/Consumables for the actual items).
struct ItemEffect {
    TargetType target = TargetType::OneAlly; // OneAlly or AllAllies
    int hp = 0, mp = 0;                      // restored
    bool fullHp = false, fullMp = false;
    float revive = 0.0f;               // > 0: revives a fallen ally with this fraction of max HP
    Ailment cures = Ailment::None;     // cures this ailment...
    bool curesAll = false;             // ...or every ailment
};

struct Combatant {
    std::string name;
    bool isEnemy = false;
    int kind = 0; // enemy type id for the scene (which model to draw); unused by the rules

    int level = 1;
    int maxHp = 1, hp = 1;
    int maxMp = 0, mp = 0;
    int st = 1, ma = 1, vi = 1, ag = 1, lu = 1;

    std::array<Affinity, (size_t)Element::Count> affinity{}; // all Normal by default
    std::vector<Skill> skills; // skills[0] is the basic Attack

    // From equipment (see Loot): added to the power of physical / magic skills, and modifiers
    int weaponPower = 0;
    int magicPower = 0;
    float critBonus = 0.0f; // added to the critical chance of physical attacks
    float healBonus = 0.0f; // heals restore (1 + healBonus) times their power
    bool ignoreResist = false; // Biggoron's Edge: Resist counts as a normal hit
    bool healCures = false;    // Fairy Ocarina of the Lost Woods: heals also cure ailments
    // Passives (skill trees)
    std::array<float, (size_t)Element::Count> elementBoost{};    // +x damage with skills of that element
    std::array<bool, (size_t)Element::Count> pierceElement{};    // that element treats Resist as a normal hit
    bool ailmentWard = false;  // can't be poisoned or burned
    bool firstLight = false;   // the first action of a battle costs half an icon
    bool firstLightUsed = false;

    int xpReward = 0;
    int rupeeMin = 0, rupeeMax = 0;

    bool guarding = false;
    int atkStage = 0, defStage = 0, agiStage = 0; // see kMaxStage
    bool charged = false;                         // a Focus-like skill is waiting to be unleashed
    int ai = 0;                                   // EnemyAi: how an enemy picks its actions
    Ailment ailment = Ailment::None;
    int ailmentTurns = 0; // turns left for ailments that wear off
    int stunTurns = 0;      // own turns still to be lost to Stun
    bool skipTurn = false;  // stunned: sits out the side's current turn

    // Bosses
    int pressTurns = 1;        // Press Turn icons this combatant brings to its side's turn
    bool boss = false;
    float summonAt = 0.0f;     // when HP first falls to this fraction of max, `summons` join the battle (0 = never)
    bool summoned = false;
    std::vector<Combatant> summons;
    std::string summonText;    // shown when they arrive
    int timeBonus = 0;         // turns added to the battle's time limit when this one is defeated

    bool Alive() const {
        return hp > 0;
    }
    Affinity AffinityTo(Element e) const {
        return affinity[(size_t)e];
    }
};

// How an enemy chooses what to do. Random: any affordable skill. QueenGohma: see Battle::ChooseGohmaSkill.
enum class EnemyAi : int { Random, QueenGohma, Brood };

// "ATK+2 DEF-1" for a combatant's stages, empty when none
std::string StageText(const Combatant& c);

// Press Turn icons for the side that is acting.
struct PressTurns {
    int solid = 0;
    int blinking = 0;

    int Total() const {
        return solid + blinking;
    }
    bool Empty() const {
        return Total() == 0;
    }
    // Normal action: one icon, blinking first.
    void UseFull();
    // Weakness / critical / pass: turns a solid icon blinking, or uses a blinking one.
    void UseHalf();
    // Miss / null: two icons.
    void UseTwo();
    // Repel / drain: the whole turn.
    void UseAll();
};

// What happened, in order, for the scene to show as messages and effects.
struct BattleEvent {
    enum class Type {
        Message,  // plain text
        Hit,      // target took `amount` damage
        Heal,     // target recovered `amount` HP
        Miss,
        Weak,
        Critical,
        Resist,
        Null,
        Repel,    // `target` reflected the attack back at `actor`
        Drain,    // target absorbed `amount`
        Defeated, // target was knocked out
        Analyze,  // target's affinities revealed
        TurnLost, // the side's remaining icons were lost
        PhaseChange,
        Summon,   // `target` (an enemy index) joined the battle, called by `actor`
        Warning,  // the time limit is running out
        Victory,
        Defeat,
        Escaped,
    };
    Type type = Type::Message;
    bool actorIsEnemy = false;
    int actor = -1;
    bool targetIsEnemy = false;
    int target = -1;
    int amount = 0;
    std::string text;
    bool hold = false;  // the scene keeps this message up until the player presses A
    uint32_t color = 0; // 0xRRGGBB00 text colour for the scene (0 = default white), e.g. loot rarity
};

using Events = std::vector<BattleEvent>;

// Would this item do anything for `c`? (Healing at full HP, curing what isn't there or reviving the living
// does nothing.)
bool ItemHelps(const ItemEffect& effect, const Combatant& c);
// Apply an item to `c` (in or out of battle), adding a message per effect to `messages`. False if nothing changed.
bool ApplyItemEffect(const ItemEffect& effect, Combatant& c, std::vector<std::string>* messages);

enum class BattlePhase { PartyCommand, EnemyTurn, Victory, Defeat, Escaped };

class Battle {
  public:
    // rand01 returns a uniform float in [0, 1); injectable for deterministic tests.
    Battle(std::vector<Combatant> party, std::vector<Combatant> enemies, std::function<float()> rand01);

    BattlePhase Phase() const {
        return mPhase;
    }
    const PressTurns& Icons() const {
        return mIcons;
    }
    const std::vector<Combatant>& Party() const {
        return mParty;
    }
    const std::vector<Combatant>& Enemies() const {
        return mEnemies;
    }
    std::vector<Combatant>& PartyMutable() {
        return mParty;
    }
    // Party member whose command is being chosen (valid in PartyCommand).
    int ActivePartyMember() const {
        return mActive;
    }

    // ---- Party commands (only in PartyCommand). `target` is an enemy or ally index; ignored for "all".
    bool CanUseSkill(int skill) const;
    Events UseSkill(int skill, int target);
    Events Guard();
    Events Pass();
    Events Analyze(int enemy);
    Events Run();
    // Bosses: running is impossible (Run then says so and costs nothing).
    void SetNoEscape(bool noEscape) {
        mNoEscape = noEscape;
    }
    bool NoEscape() const {
        return mNoEscape;
    }
    // A time limit in party turns (0 = none). When the last one has passed without a victory the battle is
    // lost (TimedOut). Defeating an enemy with a timeBonus adds turns.
    void SetTurnLimit(int turns);
    int TurnsLeft() const {
        return mTurnsLeft; // including the current one, while it's the party's turn
    }
    bool HasTurnLimit() const {
        return mTurnLimit > 0;
    }
    bool TimedOut() const {
        return mTimedOut;
    }
    static constexpr int kTimeWarning = 5; // "The ceiling trembles!" with this many turns left
    // Use a consumable on party member `target` (ignored for AllAllies). The scene removes it from the bag when
    // this returns events. Uses one Press Turn icon.
    bool CanUseItem(const ItemEffect& effect, int target) const;
    Events UseItem(const std::string& itemName, const ItemEffect& effect, int target);

    // ---- Enemy turn: one enemy acts per call (only in EnemyTurn).
    Events EnemyAct();

    // Rewards once Victory is reached.
    int XpEarned() const;
    int RupeesEarned();

    // Damage `attacker` would deal to `defender` with `skill` before randomness (for tests / UI previews).
    int BaseDamage(const Combatant& attacker, const Combatant& defender, const Skill& skill) const;

  private:
    enum class Outcome { Normal, WeakOrCrit, MissOrNull, RepelOrDrain };

    Outcome ApplySkill(Combatant& user, bool userIsEnemy, int userIndex, const Skill& skill, bool targetIsEnemy,
                       int target, Events& out);
    void SpendIcons(Outcome outcome);
    void AfterAction(Events& out);
    void StartPartyTurn(Events& out);
    void StartEnemyTurn(Events& out);
    void TickAilments(std::vector<Combatant>& side, bool sideIsEnemy, Events& out);
    bool AdvanceActive(); // next living party member who can act; false if none
    bool CanAct(const Combatant& c) const {
        return c.Alive() && !c.skipTurn;
    }
    int CountIcons(std::vector<Combatant>& side, bool sideIsEnemy, Events& out); // also applies Stun
    void CheckSummons(Events& out);
    bool CheckEnd(Events& out);
    int CountAlive(const std::vector<Combatant>& side) const;
    void ApplySupport(const Combatant& user, bool userIsEnemy, int userIndex, const Skill& skill, bool targetIsEnemy,
                      int target, Events& out);
    int ChooseEnemySkill(int enemy);
    int ChooseGohmaSkill(const Combatant& gohma);
    int FindSkill(const Combatant& c, const char* name) const;
    int mEnemyTurns = 0;   // enemy turns started so far
    int mActedThisTurn = 0; // actions the current enemy side has taken this turn
    int mLastFury = -10;    // enemy turn of Queen Gohma's last Brood Fury

    std::vector<Combatant> mParty;
    std::vector<Combatant> mEnemies;
    std::function<float()> mRand;
    BattlePhase mPhase = BattlePhase::PartyCommand;
    PressTurns mIcons;
    int mActive = 0;      // party member choosing a command
    int mEnemyCursor = 0; // next enemy to act
    int mRupees = -1;
    bool mNoEscape = false;
    int mTurnLimit = 0;
    int mTurnsLeft = 0;
    bool mTimedOut = false;
    bool mInStart = false; // StartPartyTurn <-> StartEnemyTurn hand-off when a whole side is stunned
};

} // namespace Nocturne
