#include "FieldFire.h"
#include "../Party.h"

#include <string>

#include <memory>
extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
#include "src/overlays/actors/ovl_Obj_Syokudai/z_obj_syokudai.h"
#include "src/overlays/actors/ovl_Bg_Ydan_Sp/z_bg_ydan_sp.h"
}

using namespace Nocturne;

namespace {

constexpr int kStickSteps = 12;
constexpr int kMessageFrames = 90;
constexpr f32 kReach = 130.0f; // how far ahead a torch or web can be

int sStick = 0;
std::string sMsg;
unsigned int sColor = 0;
int sTimer = 0;

void Say(const std::string& text, unsigned int color = 0xFCE0A800u) {
    sMsg = text;
    sColor = color;
    sTimer = kMessageFrames;
}

// The nearest actor with this id in front of Link (within kReach and about 50 degrees of his facing)
Actor* Ahead(PlayState* play, Player* player, short yaw, s16 id, s32 category) {
    Actor* best = nullptr;
    f32 bestDist = kReach;
    for (Actor* a = play->actorCtx.actorLists[category].head; a != nullptr; a = a->next) {
        if (a->id != id) {
            continue;
        }
        f32 dx = a->world.pos.x - player->actor.world.pos.x, dz = a->world.pos.z - player->actor.world.pos.z;
        f32 d = sqrtf(dx * dx + dz * dz);
        s16 toward = Math_Atan2S(dz, dx);
        if (d < bestDist && ABS((s16)(toward - yaw)) < 0x2400) {
            best = a;
            bestDist = d;
        }
    }
    return best;
}

void Ignite() {
    Audio_PlaySfxGeneral(NA_SE_EV_FLAME_IGNITION, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
}

// What a fire arrow hitting a torch looks like to it: the torch lights itself the way it always does
ColliderInfo sFireHit;

} // namespace

bool FieldFire_OnA(PlayState* play, Player* player, short yaw) {
    // A torch ahead
    if (Actor* t = Ahead(play, player, yaw, ACTOR_OBJ_SYOKUDAI, ACTORCAT_PROP)) {
        ObjSyokudai* torch = (ObjSyokudai*)t;
        if (torch->litTimer != 0) {
            if (sStick > 0) {
                sStick = kStickSteps;
                Say("Your stick blazes anew!", 0xF8B80000u);
            } else if (Party_Consumables()[(size_t)ConsumableId::DekuStick] > 0) {
                Party_Consumables()[(size_t)ConsumableId::DekuStick]--;
                sStick = kStickSteps;
                Say("Your stick blazes! It burns for 12 steps.", 0xF8B80000u);
            } else {
                Say("A torch. You have no Deku Sticks to light.");
                return true;
            }
            Ignite();
        } else if (sStick > 0) {
            sFireHit.toucher.dmgFlags = 0x800; // as a fire arrow
            torch->colliderFlame.base.acFlags |= AC_HIT;
            torch->colliderFlame.info.acHitInfo = &sFireHit;
            Say("The torch catches!", 0xF8B80000u);
        } else {
            Say("A cold torch. A burning stick would light it.");
        }
        return true;
    }
    // A web ahead (on the wall, or underfoot)
    if (Actor* w = Ahead(play, player, yaw, ACTOR_BG_YDAN_SP, ACTORCAT_BG)) {
        if (sStick > 0) {
            ((BgYdanSp*)w)->trisCollider.base.acFlags |= 2; // touched by fire: it burns itself away
            Say("The web shrivels in the flame!", 0xF8B80000u);
        } else {
            Say("A thick web. Fire would burn it.");
        }
        return true;
    }
    return false;
}

void FieldFire_OnStep() {
    if (sStick > 0 && --sStick == 0) {
        Say("The stick burns out.", 0xF8380000u);
    }
}

void FieldFire_Tick(PlayState* play) {
    if (sTimer > 0 && --sTimer == 0) {
        sMsg.clear();
    }
    // Vanilla Deku Sticks (bought, dropped, from grass) become Nocturne ones
    if (INV_CONTENT(ITEM_STICK) == ITEM_STICK && AMMO(ITEM_STICK) > 0) {
        int added = Party_AddConsumable(ConsumableId::DekuStick, AMMO(ITEM_STICK));
        if (added > 0) {
            AMMO(ITEM_STICK) -= added;
            Say("Deku Sticks: " + std::to_string(Party_Consumables()[(size_t)ConsumableId::DekuStick]) + ".",
                0xF8B80000u);
        }
    }
}

int FieldFire_StickSteps() {
    return sStick;
}

const char* FieldFire_Message() {
    return sMsg.empty() ? nullptr : sMsg.c_str();
}

unsigned int FieldFire_MessageColor() {
    return sColor;
}
