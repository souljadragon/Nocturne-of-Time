// Nocturne of Time: party / equipment menu (see EquipMenu.h).

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"

#include "EquipMenu.h"
#include "Loot.h"
#include "../Party.h"
#include "../Battle/BattleData.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include "global.h"
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
}

#define CVAR_CRAWLER_NAME CVAR_ENHANCEMENT("Nocturne.DungeonCrawler")
#define CVAR_CRAWLER_VALUE CVarGetInteger(CVAR_CRAWLER_NAME, 1)

using namespace Nocturne;

namespace {

constexpr int kStartLevel = 5; // matches the battle scene, used if the menu opens before any battle
constexpr int kVisibleItems = 6;

enum class Page { Closed, Members, Slots, Items };

const char* kSlotLabels[(size_t)Slot::Count] = { "Weapon", "Armor", "Acc." };

struct Menu {
    Page page = Page::Closed;
    int member = 0;
    int slot = 0;
    int itemCursor = 0; // index into Choices()
    int itemScroll = 0;
    int stickDir = 0;
    bool showLore = false; // Z: the right panel shows the focused item's description and lore
};

Menu sMenu;

void PlaySfx(u16 id) {
    Sfx_PlaySfxCentered(id);
}

// The choices in the item list: -1 = "(Remove)" when something is equipped, else bag indices that fit.
std::vector<int> Choices() {
    std::vector<int> out;
    std::vector<PartyMember>& members = Party_Members();
    if (sMenu.member >= (int)members.size()) {
        return out;
    }
    const PartyMember& m = members[sMenu.member];
    if (m.gear[sMenu.slot]) {
        out.push_back(-1);
    }
    const std::vector<Item>& bag = Party_Bag();
    for (int i = 0; i < (int)bag.size(); i++) {
        if ((int)BaseOf(bag[i]).slot == sMenu.slot && CanEquip(bag[i], m.base.name)) {
            out.push_back(i);
        }
    }
    return out;
}

struct Buttons {
    bool up = false, down = false, a = false, b = false, r = false, z = false;
};

Buttons Read(Input* input) {
    Buttons out;
    int dir = 0;
    if (input->cur.stick_y > 45) {
        dir = 1;
    } else if (input->cur.stick_y < -45) {
        dir = 2;
    }
    bool newStick = dir != 0 && dir != sMenu.stickDir;
    sMenu.stickDir = dir;
    out.up = (input->press.button & BTN_DUP) || (newStick && dir == 1);
    out.down = (input->press.button & BTN_DDOWN) || (newStick && dir == 2);
    out.a = input->press.button & BTN_A;
    out.b = input->press.button & BTN_B;
    out.r = input->press.button & BTN_R;
    out.z = input->press.button & BTN_Z;
    return out;
}

void Move(int& cursor, int count, bool up) {
    if (count <= 0) {
        return;
    }
    cursor = (cursor + (up ? count - 1 : 1)) % count;
    PlaySfx(NA_SE_SY_CURSOR);
}

void RegisterEquipMenu() {
    bool enabled = CVAR_CRAWLER_VALUE != 0;
    if (!enabled) {
        EquipMenu_Close();
    }

    // Freeze the dungeon while the menu is open (Link only runs the menu)
    COND_HOOK(ShouldActorUpdate, enabled, [](void* refActor, bool* result) {
        if (sMenu.page == Page::Closed || gPlayState == NULL) {
            return;
        }
        if ((Actor*)refActor != &GET_PLAYER(gPlayState)->actor) {
            *result = false;
        }
    });

    COND_VB_SHOULD(VB_DRAW_INTERFACE, enabled, {
        PlayState* play = va_arg(args, PlayState*);
        if (sMenu.page != Page::Closed && play->pauseCtx.state == 0) {
            EquipMenu_Draw(play);
            *should = false;
        }
    });

    COND_HOOK(OnSceneInit, enabled, [](int16_t) { sMenu.page = Page::Closed; });
}

} // namespace

bool EquipMenu_IsOpen() {
    return sMenu.page != Page::Closed;
}

void EquipMenu_Open() {
    Party_EnsureCreated(kStartLevel);
    sMenu.page = Page::Members;
    sMenu.slot = 0;
    sMenu.itemCursor = 0;
    sMenu.itemScroll = 0;
    PlaySfx(NA_SE_SY_WIN_OPEN);
}

void EquipMenu_Close() {
    if (sMenu.page != Page::Closed) {
        sMenu.page = Page::Closed;
        PlaySfx(NA_SE_SY_CANCEL);
    }
}

void EquipMenu_Update(void* inputPtr) {
    if (sMenu.page == Page::Closed) {
        return;
    }
    Buttons in = Read((Input*)inputPtr);
    std::vector<PartyMember>& members = Party_Members();

    if (in.r) {
        EquipMenu_Close();
        return;
    }
    if (in.z && sMenu.page != Page::Members) {
        sMenu.showLore = !sMenu.showLore;
        PlaySfx(NA_SE_SY_CURSOR);
        return;
    }

    switch (sMenu.page) {
        case Page::Members:
            if (in.up || in.down) {
                Move(sMenu.member, (int)members.size(), in.up);
            } else if (in.a) {
                sMenu.page = Page::Slots;
                PlaySfx(NA_SE_SY_DECIDE);
            } else if (in.b) {
                EquipMenu_Close();
            }
            break;

        case Page::Slots:
            if (in.up || in.down) {
                Move(sMenu.slot, (int)Slot::Count, in.up);
            } else if (in.a) {
                sMenu.page = Page::Items;
                sMenu.itemCursor = 0;
                sMenu.itemScroll = 0;
                PlaySfx(NA_SE_SY_DECIDE);
            } else if (in.b) {
                sMenu.page = Page::Members;
                PlaySfx(NA_SE_SY_CANCEL);
            }
            break;

        case Page::Items: {
            std::vector<int> choices = Choices();
            if (in.up || in.down) {
                Move(sMenu.itemCursor, (int)choices.size(), in.up);
            } else if (in.a) {
                if (choices.empty()) {
                    PlaySfx(NA_SE_SY_ERROR);
                } else {
                    int pick = choices[sMenu.itemCursor];
                    if (pick < 0) {
                        Party_Unequip(sMenu.member, (Slot)sMenu.slot);
                    } else {
                        Party_Equip(sMenu.member, pick);
                    }
                    PlaySfx(NA_SE_SY_DECIDE);
                    sMenu.page = Page::Slots;
                }
            } else if (in.b) {
                sMenu.page = Page::Slots;
                PlaySfx(NA_SE_SY_CANCEL);
            }
            // keep the cursor in view
            int count = (int)Choices().size();
            sMenu.itemCursor = std::clamp(sMenu.itemCursor, 0, std::max(0, count - 1));
            if (sMenu.itemCursor < sMenu.itemScroll) {
                sMenu.itemScroll = sMenu.itemCursor;
            } else if (sMenu.itemCursor >= sMenu.itemScroll + kVisibleItems) {
                sMenu.itemScroll = sMenu.itemCursor - kVisibleItems + 1;
            }
            break;
        }

        default:
            break;
    }
}

// ---- Drawing --------------------------------------------------------------------------------------------
// Outside the anonymous namespace: OPEN_DISPS declares helpers at block scope that must resolve globally.

static Gfx sMenuRectDL[] = {
    gsDPPipeSync(),
    gsSPClearGeometryMode(G_ZBUFFER | G_SHADE | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN |
                          G_TEXTURE_GEN_LINEAR | G_SHADING_SMOOTH | G_LOD),
    gsDPSetOtherMode(G_AD_DISABLE | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE | G_TL_TILE |
                         G_TD_CLAMP | G_TP_NONE | G_CYC_1CYCLE | G_PM_1PRIMITIVE,
                     G_AC_NONE | G_ZS_PIXEL | G_RM_CLD_SURF | G_RM_CLD_SURF2),
    gsDPSetCombineMode(G_CC_PRIMITIVE, G_CC_PRIMITIVE),
    gsSPEndDisplayList(),
};

static void MenuRect(Gfx** gfx, s32 x, s32 y, s32 w, s32 h, u8 r, u8 g, u8 b, u8 a) {
    gDPPipeSync((*gfx)++);
    gDPSetPrimColor((*gfx)++, 0, 0, r, g, b, a);
    gDPFillRectangle((*gfx)++, x, y, x + w, y + h);
}

static void MenuPanel(Gfx** gfx, s32 x, s32 y, s32 w, s32 h) {
    MenuRect(gfx, x - 1, y - 1, w + 2, h + 2, 120, 140, 170, 210);
    MenuRect(gfx, x, y, w, h, 10, 14, 30, 215);
}

// Word-wrap into lines of at most `chars` characters.
static std::vector<std::string> Wrap(const std::string& text, size_t chars) {
    std::vector<std::string> lines;
    std::string rest = text;
    while (!rest.empty()) {
        size_t cut = rest.size() <= chars ? rest.size() : rest.rfind(' ', chars);
        if (cut == std::string::npos || cut == 0) {
            cut = std::min(rest.size(), chars);
        }
        lines.push_back(rest.substr(0, cut));
        rest = cut < rest.size() ? rest.substr(cut + (rest[cut] == ' ' ? 1 : 0)) : "";
    }
    return lines;
}

static std::string Fit(const std::string& s, size_t chars) {
    if (s.size() <= chars) {
        return s;
    }
    return s.substr(0, chars - 2) + "..";
}

// Stat lines that differ between two versions of a member, e.g. "Max HP 84 > 94" (up = true if it improves)
struct Diff {
    std::string text;
    bool up;
};

static std::vector<Diff> CompareStats(const Combatant& now, const Combatant& next) {
    std::vector<Diff> out;
    auto add = [&](const char* label, int a, int b, const char* unit = "") {
        if (a != b) {
            char buf[40];
            snprintf(buf, sizeof(buf), "%s %d%s > %d%s", label, a, unit, b, unit);
            out.push_back({ buf, b > a });
        }
    };
    add("Max HP", now.maxHp, next.maxHp);
    add("Max MP", now.maxMp, next.maxMp);
    add("Attack", now.weaponPower, next.weaponPower);
    add("Magic", now.magicPower, next.magicPower);
    add("St", now.st, next.st);
    add("Ma", now.ma, next.ma);
    add("Vi", now.vi, next.vi);
    add("Ag", now.ag, next.ag);
    add("Lu", now.lu, next.lu);
    add("Critical", (int)std::lround(now.critBonus * 100), (int)std::lround(next.critBonus * 100), "%");
    add("Healing", (int)std::lround(now.healBonus * 100), (int)std::lround(next.healBonus * 100), "%");
    for (size_t e = 0; e < (size_t)Element::Count; e++) {
        if (now.affinity[e] != next.affinity[e]) {
            std::string t = std::string(ElementName((Element)e)) + " " + AffinityName(now.affinity[e]) + " > " +
                            AffinityName(next.affinity[e]);
            // better = the affinity's enum is "stronger" (Normal < Resist < Null...), weak is worst
            auto rank = [](Affinity a) { return a == Affinity::Weak ? -1 : (int)a; };
            out.push_back({ t, rank(next.affinity[e]) > rank(now.affinity[e]) });
        }
    }
    return out;
}

void EquipMenu_Draw(PlayState* play) {
    if (sMenu.page == Page::Closed) {
        return;
    }
    std::vector<PartyMember>& members = Party_Members();
    if (members.empty()) {
        return;
    }
    sMenu.member = std::clamp(sMenu.member, 0, (int)members.size() - 1);
    const PartyMember& m = members[sMenu.member];
    Combatant eff = Party_Effective(m);

    const int leftX = 10, leftW = 92;
    const int memY = 26, memH = (int)members.size() * 10 + 4;
    const int slotY = memY + memH + 8, slotH = (int)Slot::Count * 10 + 4;
    const int rightX = 108, rightW = SCREEN_WIDTH - 118, rightY = 26, rightH = 178;
    const size_t rightChars = (size_t)(rightW - 8) / 8;

    std::vector<int> choices = sMenu.page == Page::Items ? Choices() : std::vector<int>();

    OPEN_DISPS(play->state.gfxCtx);
    Gfx* gfx = OVERLAY_DISP;
    gSPDisplayList(gfx++, sMenuRectDL);

    MenuRect(&gfx, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, 110); // dim the dungeon behind
    MenuPanel(&gfx, leftX, memY, leftW, memH);
    MenuPanel(&gfx, leftX, slotY, leftW, slotH);
    MenuPanel(&gfx, rightX, rightY, rightW, rightH);
    if (sMenu.page == Page::Members) {
        MenuRect(&gfx, leftX + 1, memY + 2 + sMenu.member * 10, leftW - 2, 10, 40, 60, 110, 230);
    } else {
        MenuRect(&gfx, leftX + 1, slotY + 2 + sMenu.slot * 10, leftW - 2, 10, 40, 60, 110, 230);
    }

    // EXP bar, drawn after the text once its row is known
    int expBarX = -1, expBarY = 0;
    float expFill = 0.0f;
    const int expBarW = 80, expBarH = 5;

    GfxPrint printer;
    GfxPrint_Init(&printer);
    GfxPrint_Open(&printer, gfx);
    auto text = [&](int x, int y, const std::string& s, u8 r, u8 g, u8 b) {
        GfxPrint_SetColor(&printer, r, g, b, 255);
        GfxPrint_SetPosPx(&printer, x, y);
        GfxPrint_Printf(&printer, "%s", s.c_str());
    };
    auto rarityText = [&](int x, int y, const Item& item, size_t chars) {
        Rgb c = RarityColor(item.rarity);
        text(x, y, Fit(item.name, chars), c.r, c.g, c.b);
    };

    text(leftX, 12, "EQUIPMENT", 230, 230, 230);
    text(rightX, 12, std::string("Bag: ") + std::to_string(Party_Bag().size()) + " items", 150, 150, 170);

    for (int i = 0; i < (int)members.size(); i++) {
        char line[24];
        snprintf(line, sizeof(line), "%-6s Lv%d", members[i].base.name.c_str(), members[i].base.level);
        bool sel = i == sMenu.member;
        text(leftX + 3, memY + 3 + i * 10, line, sel ? 255 : 210, sel ? 230 : 210, sel ? 90 : 210);
    }
    for (int i = 0; i < (int)Slot::Count; i++) {
        bool sel = sMenu.page != Page::Members && i == sMenu.slot;
        text(leftX + 3, slotY + 3 + i * 10, kSlotLabels[i], sel ? 255 : 200, sel ? 230 : 200, sel ? 90 : 200);
    }

    // The item the right panel is about: the equipped one on the slot page, the highlighted one in the list
    const Item* focus = nullptr;
    if (sMenu.page == Page::Slots && m.gear[sMenu.slot]) {
        focus = &*m.gear[sMenu.slot];
    } else if (sMenu.page == Page::Items && !choices.empty()) {
        int pick = choices[std::clamp(sMenu.itemCursor, 0, (int)choices.size() - 1)];
        if (pick >= 0) {
            focus = &Party_Bag()[pick];
        }
    }

    int y = rightY + 4;
    if (sMenu.showLore && sMenu.page != Page::Members) {
        // Lore view: name, rarity, description, then the item's story
        if (focus == nullptr) {
            text(rightX + 4, y, "Nothing selected.", 150, 150, 150);
        } else {
            Rgb c = RarityColor(focus->rarity);
            for (const std::string& l : Wrap(focus->name, rightChars)) {
                text(rightX + 4, y, l, c.r, c.g, c.b);
                y += 9;
            }
            text(rightX + 4, y, std::string(RarityName(focus->rarity)) + " " + SlotName(BaseOf(*focus).slot), 150,
                 150, 170);
            y += 14;
            std::string lore = ItemLore(*focus);
            std::string desc = ItemDescription(*focus);
            if (lore != desc) {
                for (const std::string& l : Wrap(desc, rightChars)) {
                    text(rightX + 4, y, l, 170, 200, 160);
                    y += 9;
                }
                y += 5;
            }
            for (const std::string& l : Wrap(lore, rightChars)) {
                if (y > rightY + rightH - 20) {
                    break;
                }
                text(rightX + 4, y, l, 225, 220, 205);
                y += 9;
            }
            if (!BaseOf(*focus).effectText.empty()) {
                y += 5;
                for (const std::string& l : Wrap(BaseOf(*focus).effectText, rightChars)) {
                    text(rightX + 4, y, l, 255, 170, 80);
                    y += 9;
                }
            }
        }
    } else if (sMenu.page != Page::Items) {
        // Equipped gear, then the member's stats
        for (int i = 0; i < (int)Slot::Count; i++) {
            text(rightX + 4, y, std::string(kSlotLabels[i]), 140, 150, 180);
            if (m.gear[i]) {
                rarityText(rightX + 4 + 7 * 8, y, *m.gear[i], rightChars - 7);
            } else {
                text(rightX + 4 + 7 * 8, y, "-", 110, 110, 110);
            }
            y += 10;
        }
        y += 6;
        char line[48];
        snprintf(line, sizeof(line), "HP %d/%d  MP %d/%d", eff.hp, eff.maxHp, eff.mp, eff.maxMp);
        text(rightX + 4, y, line, 200, 220, 200);
        y += 10;
        snprintf(line, sizeof(line), "St %d  Ma %d  Vi %d", eff.st, eff.ma, eff.vi);
        text(rightX + 4, y, line, 220, 220, 220);
        y += 10;
        snprintf(line, sizeof(line), "Ag %d  Lu %d", eff.ag, eff.lu);
        text(rightX + 4, y, line, 220, 220, 220);
        y += 10;
        snprintf(line, sizeof(line), "Attack +%d  Magic +%d", eff.weaponPower, eff.magicPower);
        text(rightX + 4, y, line, 220, 220, 220);
        y += 10;
        // EXP toward the next level
        text(rightX + 4, y, "EXP", 140, 150, 180);
        int lvStart = XpForLevel(m.base.level), lvNext = XpForLevel(m.base.level + 1);
        if (lvNext > lvStart) {
            expFill = std::clamp((float)(m.xp - lvStart) / (float)(lvNext - lvStart), 0.0f, 1.0f);
            snprintf(line, sizeof(line), "Next %d", std::max(0, lvNext - m.xp));
        } else {
            expFill = 1.0f;
            snprintf(line, sizeof(line), "Max");
        }
        expBarX = rightX + 4 + 4 * 8;
        expBarY = y + 2;
        text(expBarX + expBarW + 6, y, line, 200, 200, 210);
        y += 10;
        if (eff.critBonus > 0 || eff.healBonus > 0) {
            snprintf(line, sizeof(line), "Crit +%d%%  Heal +%d%%", (int)std::lround(eff.critBonus * 100),
                     (int)std::lround(eff.healBonus * 100));
            text(rightX + 4, y, line, 220, 220, 220);
            y += 10;
        }
        // Selected slot's item in detail
        if (sMenu.page == Page::Slots && m.gear[sMenu.slot]) {
            y += 6;
            const Item& item = *m.gear[sMenu.slot];
            Rgb c = RarityColor(item.rarity);
            text(rightX + 4, y, std::string(RarityName(item.rarity)) + " " + SlotName(BaseOf(item).slot), c.r, c.g,
                 c.b);
            y += 10;
            for (const std::string& l : DescribeItem(item)) {
                if (y > rightY + rightH - 10) {
                    break;
                }
                text(rightX + 8, y, l, 200, 200, 210);
                y += 9;
            }
            y += 4;
            for (const std::string& l : Wrap(ItemDescription(item), rightChars)) {
                if (y > rightY + rightH - 10) {
                    break;
                }
                text(rightX + 4, y, l, 170, 200, 160);
                y += 9;
            }
        }
    } else {
        // Item list for the slot
        if (choices.empty()) {
            text(rightX + 4, y, "Nothing to equip here.", 150, 150, 150);
        }
        for (int row = 0; row < kVisibleItems; row++) {
            int i = sMenu.itemScroll + row;
            if (i >= (int)choices.size()) {
                break;
            }
            bool sel = i == sMenu.itemCursor;
            text(rightX + 2, y, sel ? ">" : " ", 255, 230, 90);
            if (choices[i] < 0) {
                text(rightX + 10, y, "(Remove)", 200, 200, 200);
            } else {
                rarityText(rightX + 10, y, Party_Bag()[choices[i]], rightChars - 1);
            }
            y += 10;
        }
        if ((int)choices.size() > kVisibleItems) {
            char more[24];
            snprintf(more, sizeof(more), "%d/%d", sMenu.itemCursor + 1, (int)choices.size());
            text(rightX + rightW - (int)strlen(more) * 8 - 4, rightY + 4 + kVisibleItems * 10, more, 140, 140, 160);
        }

        // Details of the highlighted choice, and how the member's stats would change
        if (!choices.empty()) {
            y = rightY + 4 + kVisibleItems * 10 + 8;
            int pick = choices[std::clamp(sMenu.itemCursor, 0, (int)choices.size() - 1)];
            PartyMember next = m;
            if (pick < 0) {
                next.gear[sMenu.slot].reset();
            } else {
                const Item& item = Party_Bag()[pick];
                next.gear[sMenu.slot] = item;
                Rgb c = RarityColor(item.rarity);
                // Full name here (wrapped), since the list above may have shortened it
                std::string name = item.name;
                for (int line = 0; line < 2 && !name.empty(); line++) {
                    size_t cut = name.size() <= rightChars ? name.size() : name.rfind(' ', rightChars);
                    if (cut == std::string::npos || cut == 0) {
                        cut = std::min(name.size(), rightChars);
                    }
                    text(rightX + 4, y, name.substr(0, cut), c.r, c.g, c.b);
                    name = cut < name.size() ? name.substr(cut + 1) : "";
                    y += 9;
                }
                for (const std::string& l : Wrap(ItemDescription(item), rightChars)) {
                    text(rightX + 4, y, l, 170, 200, 160);
                    y += 9;
                }
                std::string mods;
                for (const std::string& l : DescribeItem(item)) {
                    mods += (mods.empty() ? "" : ", ") + l;
                }
                // wrap the stat summary
                while (!mods.empty() && y < rightY + rightH - 46) {
                    size_t cut = mods.size() <= rightChars ? mods.size() : mods.rfind(' ', rightChars);
                    if (cut == std::string::npos || cut == 0) {
                        cut = std::min(mods.size(), rightChars);
                    }
                    text(rightX + 4, y, mods.substr(0, cut), 190, 190, 200);
                    mods = cut < mods.size() ? mods.substr(cut + 1) : "";
                    y += 9;
                }
                if (!BaseOf(item).effectText.empty() && y < rightY + rightH - 40) {
                    text(rightX + 4, y, Fit(BaseOf(item).effectText, rightChars), 255, 170, 80);
                    y += 9;
                }
            }
            y += 3;
            for (const Diff& d : CompareStats(eff, Party_Effective(next))) {
                if (y > rightY + rightH - 10) {
                    break;
                }
                text(rightX + 4, y, Fit(d.text, rightChars), d.up ? 110 : 255, d.up ? 230 : 110, d.up ? 120 : 110);
                y += 9;
            }
        }
    }

    const char* hint = sMenu.page == Page::Members ? "A: Choose  B/R: Close"
                       : sMenu.page == Page::Slots ? (sMenu.showLore ? "A: Change  B: Back  Z: Stats"
                                                                     : "A: Change  B: Back  Z: Lore")
                                                   : (sMenu.showLore ? "A: Equip  B: Back  Z: Stats"
                                                                     : "A: Equip  B: Back  Z: Lore");
    text(leftX, SCREEN_HEIGHT - 26, hint, 150, 150, 170);

    gfx = GfxPrint_Close(&printer);
    if (expBarX >= 0) {
        gSPDisplayList(gfx++, sMenuRectDL);
        MenuRect(&gfx, expBarX, expBarY, expBarW, expBarH, 40, 40, 50, 230);
        int fill = (int)(expBarW * expFill);
        if (fill > 0) {
            MenuRect(&gfx, expBarX, expBarY, fill, expBarH, 240, 200, 70, 255);
        }
    }
    GfxPrint_Destroy(&printer);
    OVERLAY_DISP = gfx;
    CLOSE_DISPS(play->state.gfxCtx);
}

static RegisterShipInitFunc initFunc(RegisterEquipMenu, { CVAR_CRAWLER_NAME });
