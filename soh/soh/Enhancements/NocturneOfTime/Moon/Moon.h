#pragma once

// Nocturne of Time: the moon. Its phase advances as the party walks the dungeon - New Moon, waxing to Full Moon,
// then waning back - like Kagutsuchi in SMT III. The fuller the moon, the more often monsters attack and the
// better the loot they leave. Pure C++ (testable); MoonHud draws it.

#include <cstdint>

namespace Nocturne {

enum class MoonPhase : uint8_t { New, TwoEighths, ThreeEighths, Half, FiveEighths, SixEighths, SevenEighths, Full, Count };

constexpr int kMoonStepsPerPhase = 16;
constexpr int kMoonCycleLength = 14; // New .. Full (8) and back down 7/8 .. 2/8 (6)

const char* MoonPhaseName(MoonPhase phase); // "New Moon", "2/8 Moon", "Half Moon", "Full Moon"...

MoonPhase Moon_Phase();
bool Moon_Waxing(); // growing toward Full Moon (Full and New count as turning points)
float Moon_Fullness(); // 0 at New Moon .. 1 at Full Moon

// One grid step in a dungeon. True when the phase changed.
bool Moon_OnStep();
void Moon_Reset(); // back to New Moon

// Position in the cycle (0 .. kMoonCycleLength - 1) and steps taken in this phase, for saving.
int Moon_CyclePos();
int Moon_StepsInPhase();
void Moon_SetState(int cyclePos, int stepsInPhase);

// Effects of the current phase
float Moon_EncounterScale(); // multiplies the danger gained per step: 0.6 at New Moon .. 2.0 at Full Moon
float Moon_LootLuck();       // rarity luck for drops: 0 .. 1
float Moon_DropScale();      // multiplies the chance of an enemy dropping gear: 1.0 .. 1.6
// Number of enemies in an encounter (1-3) for a uniform roll in [0, 1): mostly lone foes at New Moon, packs of
// three at Full Moon.
int Moon_EnemyCount(float roll);
// Full Moon: the monsters are frenzied and the party can't run.
bool Moon_BlocksEscape();

} // namespace Nocturne
