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

enum class TargetType : uint8_t { OneEnemy, AllEnemies, OneAlly, AllAllies };

struct Skill {
    std::string name;
    Element element = Element::Slash;
    TargetType target = TargetType::OneEnemy;
    int power = 0;       // damage power, or HP restored for heals
    bool heal = false;   // restores HP instead of dealing damage
    int mpCost = 0;
    int hpCost = 0;      // flat HP cost (physical techniques)
    float accuracy = 0.95f;
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

    int xpReward = 0;
    int rupeeMin = 0, rupeeMax = 0;

    bool guarding = false;

    bool Alive() const {
        return hp > 0;
    }
    Affinity AffinityTo(Element e) const {
        return affinity[(size_t)e];
    }
};

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
};

using Events = std::vector<BattleEvent>;

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
    bool AdvanceActive(); // next living party member; false if none
    bool CheckEnd(Events& out);
    int CountAlive(const std::vector<Combatant>& side) const;

    std::vector<Combatant> mParty;
    std::vector<Combatant> mEnemies;
    std::function<float()> mRand;
    BattlePhase mPhase = BattlePhase::PartyCommand;
    PressTurns mIcons;
    int mActive = 0;      // party member choosing a command
    int mEnemyCursor = 0; // next enemy to act
    int mRupees = -1;
};

} // namespace Nocturne
