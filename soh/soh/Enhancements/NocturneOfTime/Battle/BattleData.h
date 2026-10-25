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

// Raise a party member one level (stats grow by archetype). Returns false at the cap.
bool LevelUp(Combatant& member);

} // namespace Nocturne
