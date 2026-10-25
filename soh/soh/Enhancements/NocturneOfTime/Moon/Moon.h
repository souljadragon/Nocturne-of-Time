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

// Effects of the current phase
float Moon_EncounterScale(); // multiplies the danger gained per step: 1.0 at New Moon .. 1.6 at Full Moon
float Moon_LootLuck();       // rarity luck for drops: 0 .. 1

} // namespace Nocturne
