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
#include "../UI/RetroUI.h"

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
constexpr int kVisibleItems = 5; // equip choices shown at once (matches the status screen)

// Members: pick a member (or the "Items" row below them). Slots/Items: equipment. Use/UseTarget: consumables.
enum class Page { Closed, Members, Slots, Items, Use, UseTarget };

const char* kSlotLabels[(size_t)Slot::Count] = { "Weapon", "Armor", "Acc." };

struct Menu {
    Page page = Page::Closed;
    int member = 0;
    int slot = 0;
    int itemCursor = 0; // index into Choices()
    int itemScroll = 0;
    int stickDir = 0;
    bool showLore = false; // Z: the right panel shows the focused item's description and lore
    int useCursor = 0;     // index into OwnedConsumables()
    int useTarget = 0;     // member receiving the item
    std::string note;      // result of the last item use
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

std::vector<ConsumableId> OwnedConsumables() {
    std::vector<ConsumableId> owned;
    const auto& counts = Party_Consumables();
    for (size_t i = 0; i < counts.size(); i++) {
        if (counts[i] > 0) {
            owned.push_back((ConsumableId)i);
        }
    }
    return owned;
}

// The left list's "Items" row sits just below the members
bool OnItemsRow() {
    return sMenu.page == Page::Members && sMenu.member >= (int)Party_Members().size();
}

std::string JoinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) {
        out += (out.empty() ? "" : ". ") + l;
    }
    return out.empty() ? out : out + ".";
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
    sMenu.member = std::clamp(sMenu.member, 0, (int)Party_Members().size() - 1);
    sMenu.note.clear();
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
    if (in.z && (sMenu.page == Page::Slots || sMenu.page == Page::Items)) {
        sMenu.showLore = !sMenu.showLore;
        PlaySfx(NA_SE_SY_CURSOR);
        return;
    }

    switch (sMenu.page) {
        case Page::Members:
            if (in.up || in.down) {
                Move(sMenu.member, (int)members.size() + 1, in.up); // + the Items row
            } else if (in.a) {
                if (OnItemsRow()) {
                    if (OwnedConsumables().empty()) {
                        PlaySfx(NA_SE_SY_ERROR);
                    } else {
                        sMenu.page = Page::Use;
                        sMenu.useCursor = std::clamp(sMenu.useCursor, 0, (int)OwnedConsumables().size() - 1);
                        sMenu.note.clear();
                        PlaySfx(NA_SE_SY_DECIDE);
                    }
                } else {
                    sMenu.page = Page::Slots;
                    PlaySfx(NA_SE_SY_DECIDE);
                }
            } else if (in.b) {
                EquipMenu_Close();
            }
            break;

        case Page::Use: {
            std::vector<ConsumableId> owned = OwnedConsumables();
            if (owned.empty()) {
                sMenu.page = Page::Members;
                break;
            }
            sMenu.useCursor = std::clamp(sMenu.useCursor, 0, (int)owned.size() - 1);
            const ConsumableDef& def = ConsumableOf(owned[sMenu.useCursor]);
            if (in.up || in.down) {
                Move(sMenu.useCursor, (int)owned.size(), in.up);
            } else if (in.a) {
                if (def.battleOnly) {
                    PlaySfx(NA_SE_SY_ERROR);
                    sMenu.note = "That can only be used in battle.";
                } else if (def.effect.target == TargetType::AllAllies) {
                    std::vector<std::string> result = Party_UseConsumable(def.id, -1);
                    PlaySfx(result.empty() ? NA_SE_SY_ERROR : NA_SE_SY_HP_RECOVER);
                    sMenu.note = result.empty() ? "It would have no effect." : JoinLines(result);
                } else {
                    sMenu.page = Page::UseTarget;
                    sMenu.useTarget = 0;
                    sMenu.note.clear();
                    PlaySfx(NA_SE_SY_DECIDE);
                }
            } else if (in.b) {
                sMenu.page = Page::Members;
                PlaySfx(NA_SE_SY_CANCEL);
            }
            break;
        }

        case Page::UseTarget: {
            std::vector<ConsumableId> owned = OwnedConsumables();
            if (owned.empty() || sMenu.useCursor >= (int)owned.size()) {
                sMenu.page = owned.empty() ? Page::Members : Page::Use;
                break;
            }
            ConsumableId id = owned[sMenu.useCursor];
            if (in.up || in.down) {
                Move(sMenu.useTarget, (int)members.size(), in.up);
            } else if (in.a) {
                std::vector<std::string> result = Party_UseConsumable(id, sMenu.useTarget);
                PlaySfx(result.empty() ? NA_SE_SY_ERROR : NA_SE_SY_HP_RECOVER);
                sMenu.note = result.empty() ? "It would have no effect." : JoinLines(result);
                if (Party_Consumables()[(size_t)id] <= 0) {
                    sMenu.page = Page::Use; // used the last one
                }
            } else if (in.b) {
                sMenu.page = Page::Use;
                PlaySfx(NA_SE_SY_CANCEL);
            }
            break;
        }

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


// ---- Drawing: the Famicom Megami Tensei status screen (see UI/RetroUI) -----------------------------------

namespace {

std::string Upper(std::string s) {
    for (char& c : s) {
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
    }
    return s;
}

Retro::Color RarityRetro(Rarity r) {
    switch (r) {
        case Rarity::Uncommon:
            return Retro::Blue;
        case Rarity::Rare:
            return Retro::Violet;
        case Rarity::Legendary:
            return Retro::Orange;
        default:
            return Retro::White;
    }
}

constexpr int kTabsY = 6;
constexpr int kColDivX = 160; // rule between the lower columns
constexpr int kLowerY = 106;
constexpr int kRightX = 168, kRightW = SCREEN_WIDTH - 10 - kRightX;
constexpr size_t kRightChars = (size_t)kRightW / Retro::kCharW;

// Member tabs plus ITEMS along the top
void DrawTabs(Retro::Painter& p, int selected) {
    const std::vector<PartyMember>& members = Party_Members();
    int x = 14;
    for (int i = 0; i <= (int)members.size(); i++) {
        std::string name = i < (int)members.size() ? Upper(members[i].base.name) : "ITEMS";
        bool sel = i == selected;
        if (sel) {
            p.Text(x, kTabsY, "▶", Retro::Yellow);
        }
        p.Text(x + 9, kTabsY, name, sel ? Retro::Yellow : Retro::Grey);
        x += 9 + (int)Retro::Length(name) * Retro::kCharW + 10;
    }
}

void Diamond(Retro::Painter& p, int x, int y) {
    p.Text(x, y, "◆", Retro::Green);
}

} // namespace

// Consumables: the ITEMS tab. Carried items with counts, the highlighted one's description, then who to give
// it to (party cards with portraits) and what happened.
static void DrawItemsView(PlayState* play) {
    using namespace Retro;
    std::vector<PartyMember>& members = Party_Members();
    std::vector<ConsumableId> owned = OwnedConsumables();
    int useCursor = owned.empty() ? 0 : std::clamp(sMenu.useCursor, 0, (int)owned.size() - 1);
    bool listActive = sMenu.page == Page::Use || sMenu.page == Page::UseTarget;

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);
    p.Rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Black);
    DrawTabs(p, (int)members.size());

    // Header with the little green boxes, as on the Famicom screen
    p.Rect(118, 22, 9, 9, GreenLight);
    p.Rect(120, 24, 5, 5, GreenDark);
    p.TextCenter(160, 23, "Items", White);
    p.Rect(193, 22, 9, 9, GreenLight);
    p.Rect(195, 24, 5, 5, GreenDark);

    // List (left) and description (right), split by a rule
    p.Rect(kColDivX, 40, 2, 92, White);
    int y = 42;
    if (owned.empty()) {
        p.Text(18, y, "No items.", Grey);
    }
    for (int i = 0; i < (int)owned.size(); i++) {
        const ConsumableDef& def = ConsumableOf(owned[i]);
        bool sel = listActive && i == useCursor;
        Color c = sel ? Yellow : (def.battleOnly ? Grey : White);
        if (sel) {
            p.Text(14, y, "▶", Yellow);
        } else {
            Diamond(p, 14, y);
        }
        p.Text(26, y, Fit(def.name, 13), c);
        p.TextRight(kColDivX - 6, y, "×" + std::to_string(Party_Consumables()[(size_t)def.id]), c);
        y += 12;
    }
    if (listActive && !owned.empty()) {
        const ConsumableDef& def = ConsumableOf(owned[useCursor]);
        int dy = 42;
        p.Text(kRightX, dy, Fit(Upper(def.name), kRightChars), Green);
        dy += 14;
        for (const std::string& l : Wrap(def.description, kRightChars)) {
            p.Text(kRightX, dy, l, White);
            dy += kLineH;
        }
        dy += 4;
        p.Text(kRightX, dy, def.battleOnly ? "BATTLE ONLY" : (def.effect.target == TargetType::AllAllies ? "PARTY" : "ONE ALLY"),
               Grey);
    } else {
        p.Text(kRightX, 42, "A:CHOOSE", Grey);
    }

    // Party: who gets it
    p.Rect(10, 138, SCREEN_WIDTH - 20, 2, White);
    p.Text(14, 146, sMenu.page == Page::UseTarget ? "USE ON WHOM?" : "PARTY", Green);
    const int cardW = 148, cardH = 38;
    for (int i = 0; i < (int)members.size() && i < 2; i++) {
        Combatant c = Party_Effective(members[i]);
        int x = 10 + i * (cardW + 4), cy = 158;
        bool sel = sMenu.page == Page::UseTarget && i == sMenu.useTarget;
        p.Window(x, cy, cardW, cardH, sel ? Yellow : White);
        p.Portrait(c.name, x + 5, cy + 5, 28);
        p.Text(x + 38, cy + 5, (sel ? "▶" : "") + Upper(c.name), sel ? Yellow : White);
        const char* cond = c.ailment == Ailment::Poison ? "PSN" : (c.ailment == Ailment::Burn ? "BRN" : "GOOD");
        p.TextRight(x + cardW - 5, cy + 5, cond,
                    c.ailment == Ailment::Poison ? Violet : (c.ailment == Ailment::Burn ? Orange : Green));
        char line[32];
        snprintf(line, sizeof(line), "HP %d/%d", c.hp, c.maxHp);
        p.Text(x + 38, cy + 15, line, c.hp * 4 <= c.maxHp ? Red : White);
        snprintf(line, sizeof(line), "MP %d/%d", c.mp, c.maxMp);
        p.Text(x + 38, cy + 25, line, White);
    }

    if (!sMenu.note.empty()) {
        p.Text(14, 202, "▶", Green);
        std::vector<std::string> lines = Wrap(sMenu.note, 36);
        for (size_t i = 0; i < lines.size() && i < 2; i++) {
            p.Text(26, 202 + (int)i * kLineH, lines[i], Cream);
        }
    }

    const char* hint = sMenu.page == Page::Members ? "A:CHOOSE  B:CLOSE"
                       : sMenu.page == Page::Use     ? "A:USE  B:BACK"
                                                     : "A:GIVE  B:BACK";
    p.Text(14, 226, hint, Grey);
    CLOSE_DISPS(play->state.gfxCtx);
}

void EquipMenu_Draw(PlayState* play) {
    using namespace Retro;
    if (sMenu.page == Page::Closed) {
        return;
    }
    std::vector<PartyMember>& members = Party_Members();
    if (members.empty()) {
        return;
    }
    if (OnItemsRow() || sMenu.page == Page::Use || sMenu.page == Page::UseTarget) {
        DrawItemsView(play);
        return;
    }
    sMenu.member = std::clamp(sMenu.member, 0, (int)members.size() - 1);
    const PartyMember& m = members[sMenu.member];
    Combatant eff = Party_Effective(m);
    std::vector<int> choices = sMenu.page == Page::Items ? Choices() : std::vector<int>();

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);
    p.Rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, Black);
    DrawTabs(p, sMenu.member);

    // ---- Portrait in a white frame, name and level, stats on bars
    p.Rect(14, 18, 68, 68, White);
    p.Rect(16, 20, 64, 64, Black);
    p.Portrait(m.base.name, 16, 20, 64);
    p.Text(92, 20, "✻" + Upper(m.base.name) + "✻", White);
    p.TextRight(SCREEN_WIDTH - 12, 20, "Level " + std::to_string(m.base.level), White);
    const char* labels[] = { "STR", "MAG", "VIT", "AGI", "LUK" };
    for (int i = 0; i < (int)Stat::Count; i++) {
        int y = 34 + i * 10;
        int value = GetStat(eff, (Stat)i);
        p.Text(92, y, "○", White);
        p.Text(102, y, labels[i], White);
        p.TextRight(150, y, std::to_string(value), White);
        p.Bar(156, y, 20, std::min(20, value / 2), 6, 7, 1, GreenLight, GreenDark); // 2 points a block
    }

    // ---- EXP and points to the next level
    {
        int y = 92;
        int lvStart = XpForLevel(m.base.level), lvNext = XpForLevel(m.base.level + 1);
        Diamond(p, 34, y);
        p.Text(46, y, "EXP", White);
        p.TextRight(146, y, std::to_string(m.xp) + "Pts.", White);
        if (lvNext > lvStart) {
            int fill = std::clamp((m.xp - lvStart) * 10 / std::max(1, lvNext - lvStart), 0, 10);
            p.Bar(156, y, 10, fill, 6, 7, 1, Yellow, GreenDark);
            p.Text(230, y, "NEXT", Green);
            p.TextRight(SCREEN_WIDTH - 12, y, std::to_string(std::max(0, lvNext - m.xp)), White);
        } else {
            p.Text(230, y, "MAX", Green);
        }
    }

    // ---- Lower left: equipment (the slot cursor on the equip pages), HP, MP, rupees
    p.Rect(kColDivX, kLowerY, 2, 116, White);
    for (int i = 0; i < (int)Slot::Count; i++) {
        int y = kLowerY + 2 + i * 13;
        bool sel = sMenu.page != Page::Members && i == sMenu.slot;
        if (sel) {
            p.Text(14, y, "▶", Yellow);
        } else {
            Diamond(p, 14, y);
        }
        if (m.gear[i]) {
            p.Text(26, y, Fit(m.gear[i]->name, 16), sel ? Yellow : RarityRetro(m.gear[i]->rarity));
        } else {
            p.Text(26, y, std::string(kSlotLabels[i]) + ": ----", sel ? Yellow : Grey);
        }
    }
    {
        int y = kLowerY + 46;
        char line[32];
        Diamond(p, 14, y);
        p.Text(26, y, "HP", White);
        snprintf(line, sizeof(line), "%d/%d", eff.hp, eff.maxHp);
        p.TextRight(kColDivX - 6, y, line, eff.hp * 4 <= eff.maxHp ? Red : White);
        y += 13;
        Diamond(p, 14, y);
        p.Text(26, y, "MP", White);
        snprintf(line, sizeof(line), "%d/%d", eff.mp, eff.maxMp);
        p.TextRight(kColDivX - 6, y, line, White);
        y += 13;
        Diamond(p, 14, y);
        p.Text(26, y, "ATK", White);
        p.TextRight(88, y, "+" + std::to_string(eff.weaponPower), White);
        p.Text(96, y, "MAG", White);
        p.TextRight(kColDivX - 6, y, "+" + std::to_string(eff.magicPower), White);
        y += 13;
        Diamond(p, 14, y);
        p.Text(26, y, "Rupees", White);
        p.TextRight(kColDivX - 6, y, std::to_string(gSaveContext.rupees), White);
        if (eff.ailment != Ailment::None) {
            y += 13;
            p.Text(26, y, eff.ailment == Ailment::Poison ? "POISONED" : "BURNED",
                   eff.ailment == Ailment::Poison ? Violet : Orange);
        }
    }

    // ---- Lower right: depends on the page
    const Item* focus = nullptr;
    if (sMenu.page == Page::Slots && m.gear[sMenu.slot]) {
        focus = &*m.gear[sMenu.slot];
    } else if (sMenu.page == Page::Items && !choices.empty()) {
        int pick = choices[std::clamp(sMenu.itemCursor, 0, (int)choices.size() - 1)];
        if (pick >= 0) {
            focus = &Party_Bag()[pick];
        }
    }
    const int bottom = 220;
    int y = kLowerY + 2;
    if (sMenu.showLore && sMenu.page != Page::Members) {
        // Lore view: name, rarity, description and the item's story
        if (focus == nullptr) {
            p.Text(kRightX, y, "Nothing here.", Grey);
        } else {
            for (const std::string& l : Wrap(focus->name, kRightChars)) {
                p.Text(kRightX, y, l, RarityRetro(focus->rarity));
                y += kLineH;
            }
            p.Text(kRightX, y, Upper(RarityName(focus->rarity)), Grey);
            y += 14;
            std::string lore = ItemLore(*focus);
            for (const std::string& l : Wrap(lore, kRightChars)) {
                if (y > bottom - kLineH) {
                    break;
                }
                p.Text(kRightX, y, l, Cream);
                y += kLineH;
            }
            if (!BaseOf(*focus).effectText.empty() && y <= bottom - kLineH) {
                y += 4;
                for (const std::string& l : Wrap(BaseOf(*focus).effectText, kRightChars)) {
                    if (y > bottom - kLineH) {
                        break;
                    }
                    p.Text(kRightX, y, l, Orange);
                    y += kLineH;
                }
            }
        }
    } else if (sMenu.page == Page::Members) {
        // The Famicom screen's Items list: what the party carries
        p.Rect(kRightX + 20, y, 9, 9, GreenLight);
        p.Rect(kRightX + 22, y + 2, 5, 5, GreenDark);
        p.TextCenter(kRightX + kRightW / 2, y + 1, "Items", White);
        p.Rect(kRightX + kRightW - 29, y, 9, 9, GreenLight);
        p.Rect(kRightX + kRightW - 27, y + 2, 5, 5, GreenDark);
        y += 15;
        std::vector<ConsumableId> owned = OwnedConsumables();
        if (owned.empty()) {
            p.Text(kRightX, y, "None", Grey);
        }
        for (ConsumableId id : owned) {
            if (y > bottom - kLineH) {
                break;
            }
            p.Text(kRightX, y, Fit(ConsumableOf(id).name, 13), White);
            p.TextRight(kRightX + kRightW, y, "×" + std::to_string(Party_Consumables()[(size_t)id]), White);
            y += kLineH;
        }
        char bag[32];
        snprintf(bag, sizeof(bag), "Gear bag: %d", (int)Party_Bag().size());
        p.Text(kRightX, bottom - 8, bag, Grey);
    } else if (sMenu.page == Page::Slots) {
        // The equipped item in detail
        if (focus == nullptr) {
            p.Text(kRightX, y, "Nothing equipped.", Grey);
        } else {
            for (const std::string& l : Wrap(focus->name, kRightChars)) {
                p.Text(kRightX, y, l, RarityRetro(focus->rarity));
                y += kLineH;
            }
            p.Text(kRightX, y, Upper(std::string(RarityName(focus->rarity)) + " " + SlotName(BaseOf(*focus).slot)),
                   Grey);
            y += 13;
            for (const std::string& l : DescribeItem(*focus)) {
                if (y > bottom - kLineH) {
                    break;
                }
                p.Text(kRightX, y, Fit(l, kRightChars), White);
                y += kLineH;
            }
            y += 3;
            for (const std::string& l : Wrap(ItemDescription(*focus), kRightChars)) {
                if (y > bottom - kLineH) {
                    break;
                }
                p.Text(kRightX, y, l, Green);
                y += kLineH;
            }
        }
    } else {
        // Choosing gear for the slot: the list, then what changes
        if (choices.empty()) {
            p.Text(kRightX, y, "Nothing fits.", Grey);
        }
        const int visible = 5;
        for (int row = 0; row < visible; row++) {
            int i = sMenu.itemScroll + row;
            if (i >= (int)choices.size()) {
                break;
            }
            bool sel = i == sMenu.itemCursor;
            if (sel) {
                p.Text(kRightX - 2, y, "▶", Yellow);
            }
            if (choices[i] < 0) {
                p.Text(kRightX + 8, y, "(Remove)", sel ? Yellow : White);
            } else {
                const Item& it = Party_Bag()[choices[i]];
                p.Text(kRightX + 8, y, Fit(it.name, kRightChars - 1), RarityRetro(it.rarity));
            }
            y += kLineH;
        }
        if ((int)choices.size() > visible) {
            char more[16];
            snprintf(more, sizeof(more), "%d/%d", sMenu.itemCursor + 1, (int)choices.size());
            p.TextRight(kRightX + kRightW, kLowerY + 2 + visible * kLineH, more, Grey);
        }
        if (!choices.empty()) {
            y = kLowerY + 2 + visible * kLineH + 12;
            int pick = choices[std::clamp(sMenu.itemCursor, 0, (int)choices.size() - 1)];
            PartyMember next = m;
            if (pick < 0) {
                next.gear[sMenu.slot].reset();
            } else {
                next.gear[sMenu.slot] = Party_Bag()[pick];
            }
            for (const Diff& d : CompareStats(eff, Party_Effective(next))) {
                if (y > bottom - kLineH) {
                    break;
                }
                p.Text(kRightX, y, Fit(d.text, kRightChars), d.up ? Green : Red);
                y += kLineH;
            }
        }
    }

    const char* hint = sMenu.page == Page::Members ? "A:EQUIP  B:CLOSE"
                       : sMenu.page == Page::Slots ? (sMenu.showLore ? "A:CHANGE B:BACK Z:STATS"
                                                                     : "A:CHANGE B:BACK Z:LORE")
                                                   : (sMenu.showLore ? "A:EQUIP B:BACK Z:STATS"
                                                                     : "A:EQUIP B:BACK Z:LORE");
    p.Text(14, 226, hint, Grey);
    CLOSE_DISPS(play->state.gfxCtx);
}

static RegisterShipInitFunc initFunc(RegisterEquipMenu, { CVAR_CRAWLER_NAME });
