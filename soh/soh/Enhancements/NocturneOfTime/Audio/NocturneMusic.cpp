#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/audio/AudioCollection.h"
#include "NocturneMusic.h"
#include <string>

extern "C" {
#include "z64.h"
#include "functions.h"
#include "sequence.h"
extern PlayState* gPlayState;
}

namespace {

// Same switch the dungeon crawler uses: Nocturne's music comes and goes with the mod
bool Enabled() {
    return CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.DungeonCrawler"), 1) != 0 &&
           CVarGetInteger(CVAR_ENHANCEMENT("Nocturne.Music"), 1) != 0;
}

// Custom sequences are numbered at audio init in name order, so look each one up by its name
// (the file name under custom/music/) the first time it's needed.
struct Track {
    const char* name;
    int seq; // -1 not looked up yet, 0 missing
};
Track sDungeon = { "NocturneHollowedHuskDungeon", -1 };
Track sBattleA = { "NocturneHollowedHuskBattleA", -1 };
Track sShop = { "NocturneKokiriHavenShop", -1 };

uint16_t Find(Track& t) {
    if (t.seq < 0) {
        t.seq = 0;
        if (AudioCollection::Instance != nullptr) {
            for (const auto& [num, info] : AudioCollection::Instance->GetAllSequences()) {
                if (info.label == t.name) {
                    t.seq = num;
                    break;
                }
            }
        }
    }
    return (uint16_t)t.seq;
}

int16_t Scene() {
    return gPlayState != nullptr ? gPlayState->sceneNum : -1;
}

bool sBattleMusic = false;

} // namespace

extern "C" uint16_t NocturneMusic_Override(uint16_t seqId) {
    if (!Enabled()) {
        return seqId;
    }
    uint16_t to = 0;
    switch (seqId) {
        case NA_BGM_INSIDE_DEKU_TREE: // the Hollowed Husk
            to = Find(sDungeon);
            break;
        case NA_BGM_MINI_BOSS: // battles in the Hollowed Husk (vanilla never plays this there)
            if (Scene() == SCENE_DEKU_TREE) {
                to = Find(sBattleA);
            }
            break;
        case NA_BGM_SHOP: // only the Kokiri Shop; other shops keep theirs
            if (Scene() == SCENE_KOKIRI_SHOP) {
                to = Find(sShop);
            }
            break;
    }
    return to != 0 ? to : seqId;
}

void NocturneMusic_StartBattle(PlayState* play) {
    sBattleMusic = false;
    if (!Enabled() || play == nullptr || play->sceneNum != SCENE_DEKU_TREE || Find(sBattleA) == 0) {
        return;
    }
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_MINI_BOSS);
    sBattleMusic = true;
}

void NocturneMusic_EndBattle(PlayState* play) {
    if (!sBattleMusic) {
        return;
    }
    sBattleMusic = false;
    if (play != nullptr && play->sceneNum == SCENE_DEKU_TREE) {
        // Back to exploring: the dungeon theme fades back in
        Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | 0x0F0000 | NA_BGM_INSIDE_DEKU_TREE);
    }
}
