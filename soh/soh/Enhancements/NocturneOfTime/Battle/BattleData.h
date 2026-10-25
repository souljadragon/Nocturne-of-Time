#pragma once

// Nocturne of Time: party members, enemies and skills for the vertical slice.

#include "BattleEngine.h"

namespace Nocturne {

enum class EnemyKind : int { DekuBaba, Skulltula, FireKeese, QueenGohma, GohmaLarva, Count };
// Random encounters come from the first few kinds; bosses and their minions only appear in their own fights.
constexpr int kRandomEnemyKinds = 3;

Combatant MakeLink(int level);
Combatant MakeSaria(int level);
Combatant MakeEnemy(EnemyKind kind);

// Queen Gohma, boss of the Deku Tree. Two Press Turn icons a turn; her eye is weak to Pierce (the slingshot) and
// Fire. Below half HP she lays eggs: `larvae` Gohma Larvae hatch (each one defeated gives back a turn of the
// time limit).
Combatant MakeQueenGohma(int larvae);
// Turns to beat her in. A prepared party (level 8-9) usually wins in 9-10 turns, so this leaves room for
// healing and items but punishes stalling; each larva defeated gives one back.
constexpr int kGohmaTurnLimit = 15;

// Total XP needed to reach `level` from level 1. The step to the next level grows with the square of the level
// (a cubic total, like most SMT games), so early levels come quickly and later ones take real work.
int XpForLevel(int level);
// XP still needed to go from `level` to `level + 1`.
int XpToNext(int level);

// Each party member levels at their own pace, as each demon does in SMT (companions level more slowly than
// the hero): the EXP they need is the curve above times their rate. The same battle EXP then levels them at
// different times.
float XpRate(const std::string& name);
// Total XP `name` needs to reach `level`.
int XpForLevel(int level, const std::string& name);

// XP a party member earns from one enemy: full value near its level, less when far above it (as in SMT III,
// which cuts XP by about a quarter beyond 5 levels and by almost half beyond 11).
int ScaledXp(int baseXp, int memberLevel, int enemyLevel);

// Core stats in a fixed order (for level-up points and menus).
enum class Stat : int { St, Ma, Vi, Ag, Lu, Count };
const char* StatName(Stat s);
int GetStat(const Combatant& c, Stat s);
void AddStat(Combatant& c, Stat s, int amount);

// Stat points per level: some placed automatically by archetype, the rest chosen by the player.
// SMT III gives 1 point per level (cap 40), SMT V a few more; we use 2 automatic + 1 chosen.
constexpr int kAutoStatPoints = 2;
constexpr int kManualStatPoints = 1;
constexpr int kStatCap = 99; // base stats (equipment can go past it)

struct LevelGains {
    bool leveled = false;
    int hp = 0, mp = 0;
    std::array<int, (size_t)Stat::Count> stats{}; // automatic points per stat
};

// Raise a member one level: HP/MP grow by archetype and kAutoStatPoints are spread over the stats, weighted
// toward the archetype's important ones but with some variety. The kManualStatPoints are left to the caller.
LevelGains LevelUp(Combatant& member, const std::function<float()>& rand01);

} // namespace Nocturne
