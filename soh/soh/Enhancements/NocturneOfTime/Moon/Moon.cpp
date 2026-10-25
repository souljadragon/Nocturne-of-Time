#include "Moon.h"

namespace Nocturne {

namespace {
int sCycle = 0; // position in the cycle, 0 .. kMoonCycleLength - 1
int sSteps = 0; // steps taken in this phase
} // namespace

const char* MoonPhaseName(MoonPhase phase) {
    switch (phase) {
        case MoonPhase::New:
            return "New Moon";
        case MoonPhase::TwoEighths:
            return "2/8 Moon";
        case MoonPhase::ThreeEighths:
            return "3/8 Moon";
        case MoonPhase::Half:
            return "Half Moon";
        case MoonPhase::FiveEighths:
            return "5/8 Moon";
        case MoonPhase::SixEighths:
            return "6/8 Moon";
        case MoonPhase::SevenEighths:
            return "7/8 Moon";
        case MoonPhase::Full:
            return "Full Moon";
        default:
            return "";
    }
}

MoonPhase Moon_Phase() {
    int index = sCycle <= 7 ? sCycle : kMoonCycleLength - sCycle; // 8..13 wane back through 6..1
    return (MoonPhase)index;
}

bool Moon_Waxing() {
    return sCycle < 7;
}

float Moon_Fullness() {
    return (float)Moon_Phase() / 7.0f;
}

bool Moon_OnStep() {
    if (++sSteps < kMoonStepsPerPhase) {
        return false;
    }
    sSteps = 0;
    sCycle = (sCycle + 1) % kMoonCycleLength;
    return true;
}

void Moon_Reset() {
    sCycle = 0;
    sSteps = 0;
}

float Moon_EncounterScale() {
    // About 15 steps between battles at New Moon, 9 at Half Moon, 4-5 at Full Moon
    return 0.6f + 1.4f * Moon_Fullness();
}

float Moon_DropScale() {
    return 1.0f + 0.6f * Moon_Fullness();
}

int Moon_EnemyCount(float roll) {
    float f = Moon_Fullness();
    float one = 0.60f - 0.55f * f;   // 60% at New Moon .. 5% at Full Moon
    float three = 0.05f + 0.50f * f; // 5% .. 55%
    if (roll < one) {
        return 1;
    }
    return roll < 1.0f - three ? 2 : 3;
}

bool Moon_BlocksEscape() {
    return Moon_Phase() == MoonPhase::Full;
}

float Moon_LootLuck() {
    return Moon_Fullness();
}

} // namespace Nocturne
