#pragma once

// Nocturne of Time: party members, enemies and skills for the vertical slice.

#include "BattleEngine.h"

namespace Nocturne {

enum class EnemyKind : int { DekuBaba, Skulltula, FireKeese, Count };

Combatant MakeLink(int level);
Combatant MakeSaria(int level);
Combatant MakeEnemy(EnemyKind kind);

// Total XP needed to reach `level` from level 1.
int XpForLevel(int level);

// Core stats in a fixed order (for level-up points and menus).
enum class Stat : int { St, Ma, Vi, Ag, Lu, Count };
const char* StatName(Stat s);
int GetStat(const Combatant& c, Stat s);
void AddStat(Combatant& c, Stat s, int amount);

// Stat points per level: some placed automatically by archetype, the rest chosen by the player.
constexpr int kAutoStatPoints = 5;
constexpr int kManualStatPoints = 1;

struct LevelGains {
    bool leveled = false;
    int hp = 0, mp = 0;
    std::array<int, (size_t)Stat::Count> stats{}; // automatic points per stat
};

// Raise a member one level: HP/MP grow by archetype and kAutoStatPoints are spread over the stats, weighted
// toward the archetype's important ones but with some variety. The kManualStatPoints are left to the caller.
LevelGains LevelUp(Combatant& member, const std::function<float()>& rand01);

} // namespace Nocturne
