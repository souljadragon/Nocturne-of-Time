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
// Skills: a member's active skills and the pool to choose from (and Link's Relic). Relics: choosing Link's Relic.
enum class Page { Closed, Members, Slots, Items, Use, UseTarget, Skills, Relics, Tree };

const char* kSlotLabels[(size_t)Slot::Count] = { "Weapon", "Armor", "Acc." };

struct Menu {
    Page page = Page::Closed;
    int member = 0;
    int slot = 0;
    int itemCursor = 0; // index into Choices()
    int itemScroll = 0;
    int stickDir = 0;
    // Z steps the right panel: 0 = the item's stats, 1.. = its lore, a page at a time
    int lorePage = 0;
    int lorePages = 1;     // how many lore pages the focused item has (counted when drawn)
    int focusKey = -1;     // which item the panel showed last, to start its lore from page 1
    unsigned blink = 0;
    int useCursor = 0;     // index into OwnedConsumables()
    int useTarget = 0;     // member receiving the item
    std::string note;      // result of the last item use
    // Skills page: 0 = Link's Relic row, 1 = active list, 2 = learned pool
    int skillCol = 1;
    int activeCursor = 0;
    int poolCursor = 0;
    int poolScroll = 0;
    int relicCursor = 0;
    // Tree page
    int treeBranch = 0;
    int treeCursor = 0;
    bool resetArmed = false; // Z once more resets the tree
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
    bool up = false, down = false, left = false, right = false, a = false, b = false, r = false, z = false;
};

Buttons Read(Input* input) {
    Buttons out;
    int dir = 0;
    if (input->cur.stick_y > 45) {
        dir = 1;
    } else if (input->cur.stick_y < -45) {
        dir = 2;
    } else if (input->cur.stick_x < -45) {
        dir = 3;
    } else if (input->cur.stick_x > 45) {
        dir = 4;
    }
    bool newStick = dir != 0 && dir != sMenu.stickDir;
    sMenu.stickDir = dir;
    out.up = (input->press.button & BTN_DUP) || (newStick && dir == 1);
    out.down = (input->press.button & BTN_DDOWN) || (newStick && dir == 2);
    out.left = (input->press.button & BTN_DLEFT) || (newStick && dir == 3);
    out.right = (input->press.button & BTN_DRIGHT) || (newStick && dir == 4);
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

// ---- Skills and Relics pages -----------------------------------------------------------------------------

constexpr int kSkillRowsShown = 8;

bool IsLink() {
    const std::vector<PartyMember>& members = Party_Members();
    return sMenu.member < (int)members.size() && members[sMenu.member].base.name == "Link";
}

std::vector<int> OwnedRelics() {
    std::vector<int> out;
    for (int i = 0; i < (int)RelicId::Count; i++) {
        if (Party_Relics()[i].owned) {
            out.push_back(i);
        }
    }
    return out;
}

void UpdateSkillsPage(const Buttons& in) {
    std::vector<PartyMember>& members = Party_Members();
    if (sMenu.member >= (int)members.size()) {
        sMenu.page = Page::Members;
        return;
    }
    PartyMember& m = members[sMenu.member];
    std::vector<PoolSkill> pool = Party_SkillPool(m);
    int activeCount = (int)m.activeSkills.size();
    sMenu.activeCursor = std::clamp(sMenu.activeCursor, 0, kMaxActiveSkills - 1);
    sMenu.poolCursor = std::clamp(sMenu.poolCursor, 0, std::max(0, (int)pool.size() - 1));
    if (sMenu.skillCol == 0 && !IsLink()) {
        sMenu.skillCol = 1;
    }

    if (in.b) {
        sMenu.page = Page::Members;
        sMenu.note.clear();
        PlaySfx(NA_SE_SY_CANCEL);
        return;
    }
    if (in.z && TreeFor(m.base.name) != nullptr) {
        sMenu.page = Page::Tree;
        sMenu.treeCursor = 0;
        sMenu.resetArmed = false;
        sMenu.note.clear();
        PlaySfx(NA_SE_SY_DECIDE);
        return;
    }
    if (in.left || in.right) {
        if (sMenu.skillCol != 0) {
            sMenu.skillCol = sMenu.skillCol == 1 ? 2 : 1;
            PlaySfx(NA_SE_SY_CURSOR);
        }
        return;
    }
    if (in.up || in.down) {
        if (sMenu.skillCol == 0) {
            if (in.down) {
                sMenu.skillCol = 1;
                sMenu.activeCursor = 0;
                PlaySfx(NA_SE_SY_CURSOR);
            }
        } else if (sMenu.skillCol == 1) {
            if (in.up && sMenu.activeCursor == 0 && IsLink()) {
                sMenu.skillCol = 0; // up from the top: the Relic
                PlaySfx(NA_SE_SY_CURSOR);
            } else {
                Move(sMenu.activeCursor, kMaxActiveSkills, in.up);
            }
        } else if (!pool.empty()) {
            if (in.up && sMenu.poolCursor == 0 && IsLink()) {
                sMenu.skillCol = 0;
                PlaySfx(NA_SE_SY_CURSOR);
            } else {
                Move(sMenu.poolCursor, (int)pool.size(), in.up);
            }
            if (sMenu.poolCursor < sMenu.poolScroll) {
                sMenu.poolScroll = sMenu.poolCursor;
            } else if (sMenu.poolCursor >= sMenu.poolScroll + kSkillRowsShown) {
                sMenu.poolScroll = sMenu.poolCursor - kSkillRowsShown + 1;
            }
        }
        return;
    }
    if (!in.a) {
        return;
    }
    sMenu.note.clear();
    if (sMenu.skillCol == 0) {
        std::vector<int> owned = OwnedRelics();
        sMenu.relicCursor = 0;
        for (int i = 0; i < (int)owned.size(); i++) {
            if (owned[i] == Party_EquippedRelic()) {
                sMenu.relicCursor = i;
            }
        }
        sMenu.page = Page::Relics;
        PlaySfx(NA_SE_SY_DECIDE);
    } else if (sMenu.skillCol == 1) {
        // Take the skill in this slot out
        if (sMenu.activeCursor < activeCount) {
            Party_DeactivateSkill(m, m.activeSkills[sMenu.activeCursor]);
            PlaySfx(NA_SE_SY_CANCEL);
        } else {
            sMenu.skillCol = 2; // an empty slot: go pick something for it
            PlaySfx(NA_SE_SY_CURSOR);
        }
    } else if (!pool.empty()) {
        const std::string& name = pool[sMenu.poolCursor].skill.name;
        if (Party_IsActive(m, name)) {
            Party_DeactivateSkill(m, name);
            PlaySfx(NA_SE_SY_CANCEL);
        } else if (Party_ActivateSkill(m, name)) {
            PlaySfx(NA_SE_SY_DECIDE);
        } else {
            sMenu.note = "Only 8 skills fit. Take one off first.";
            PlaySfx(NA_SE_SY_ERROR);
        }
    }
}

// Nodes of one branch, in tier order
std::vector<const TreeNode*> BranchNodes(const SkillTree& tree, int branch) {
    std::vector<const TreeNode*> out;
    for (int tier = 1; tier <= kTreeTiers; tier++) {
        for (const TreeNode& n : tree.nodes) {
            if (n.branch == branch && n.tier == tier) {
                out.push_back(&n);
            }
        }
    }
    return out;
}

void UpdateTreePage(const Buttons& in) {
    std::vector<PartyMember>& members = Party_Members();
    if (sMenu.member >= (int)members.size() || TreeFor(members[sMenu.member].base.name) == nullptr) {
        sMenu.page = Page::Skills;
        return;
    }
    PartyMember& m = members[sMenu.member];
    const SkillTree& tree = *TreeFor(m.base.name);
    std::vector<const TreeNode*> nodes = BranchNodes(tree, sMenu.treeBranch);
    sMenu.treeCursor = std::clamp(sMenu.treeCursor, 0, std::max(0, (int)nodes.size() - 1));
    if (!in.z && (in.up || in.down || in.left || in.right || in.a || in.b)) {
        sMenu.resetArmed = false;
    }
    if (in.b) {
        sMenu.page = Page::Skills;
        sMenu.note.clear();
        PlaySfx(NA_SE_SY_CANCEL);
    } else if (in.left || in.right) {
        Move(sMenu.treeBranch, (int)tree.branches.size(), in.left);
        sMenu.treeCursor = 0;
        sMenu.note.clear();
    } else if (in.up || in.down) {
        Move(sMenu.treeCursor, (int)nodes.size(), in.up);
        sMenu.note.clear();
    } else if (in.a && !nodes.empty()) {
        const TreeNode& n = *nodes[sMenu.treeCursor];
        std::string blocker = Party_NodeBlocker(m, n);
        if (blocker.empty() && Party_LearnNode(m, n.id)) {
            sMenu.note = "Saria learned " + n.name + "!";
            if (m.base.name != "Saria") {
                sMenu.note = m.base.name + " learned " + n.name + "!";
            }
            PlaySfx(NA_SE_SY_DECIDE);
        } else {
            sMenu.note = blocker == "Learned" ? "Already learned." : "Can't yet: " + blocker + ".";
            PlaySfx(NA_SE_SY_ERROR);
        }
    } else if (in.z) {
        if (!sMenu.resetArmed) {
            sMenu.resetArmed = true;
            sMenu.note = "Press Z again to unlearn the whole tree.";
            PlaySfx(NA_SE_SY_CURSOR);
        } else {
            sMenu.resetArmed = false;
            Party_ResetTree(m);
            sMenu.note = "The tree is reset. Every point is back.";
            PlaySfx(NA_SE_SY_CANCEL);
        }
    }
}

void UpdateRelicsPage(const Buttons& in) {
    std::vector<int> owned = OwnedRelics();
    if (owned.empty() || in.b) {
        sMenu.page = Page::Skills;
        PlaySfx(NA_SE_SY_CANCEL);
        return;
    }
    sMenu.relicCursor = std::clamp(sMenu.relicCursor, 0, (int)owned.size() - 1);
    if (in.up || in.down) {
        Move(sMenu.relicCursor, (int)owned.size(), in.up);
    } else if (in.a) {
        int id = owned[sMenu.relicCursor];
        if (id == Party_EquippedRelic()) {
            Party_UnequipRelic();
            PlaySfx(NA_SE_SY_CANCEL);
        } else {
            Party_EquipRelic((RelicId)id);
            PlaySfx(NA_SE_SY_DECIDE);
            sMenu.page = Page::Skills;
        }
        for (PartyMember& pm : Party_Members()) {
            Party_ClampToMax(pm);
        }
    }
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
        // stats -> lore 1 -> lore 2 (when it runs long) -> stats
        sMenu.lorePage = sMenu.lorePage >= sMenu.lorePages ? 0 : sMenu.lorePage + 1;
        PlaySfx(NA_SE_SY_CURSOR);
        return;
    }

    if (in.z && sMenu.page == Page::Members && !OnItemsRow()) {
        Party_RefreshAllSkills();
        sMenu.page = Page::Skills;
        sMenu.skillCol = 1;
        sMenu.activeCursor = 0;
        sMenu.poolCursor = 0;
        sMenu.poolScroll = 0;
        sMenu.note.clear();
        PlaySfx(NA_SE_SY_DECIDE);
        return;
    }

    switch (sMenu.page) {
        case Page::Skills:
            UpdateSkillsPage(in);
            break;

        case Page::Relics:
            UpdateRelicsPage(in);
            break;

        case Page::Tree:
            UpdateTreePage(in);
            break;

        case Page::Members:
            if (in.left || in.right) {
                Move(sMenu.member, (int)members.size() + 1, in.left); // the tabs: members, then ITEMS
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
                if (def.fieldTool) {
                    PlaySfx(NA_SE_SY_ERROR);
                    sMenu.note = "Light it at a torch in the dungeon (A).";
                } else if (def.battleOnly) {
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
            if (in.left || in.right) { // the party cards sit side by side
                Move(sMenu.useTarget, (int)members.size(), in.left);
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
    p.FillScreen(Black);
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
        Color c = sel ? Yellow : (def.battleOnly || def.fieldTool ? Grey : White);
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
        p.Text(kRightX, dy, def.fieldTool ? "FOR TORCHES" : def.battleOnly ? "BATTLE ONLY" : (def.effect.target == TargetType::AllAllies ? "PARTY" : "ONE ALLY"),
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

    const char* hint = sMenu.page == Page::Members ? "◀▶:TAB  A:CHOOSE  B:CLOSE"
                       : sMenu.page == Page::Use     ? "A:USE  B:BACK"
                                                     : "◀▶:WHO  A:GIVE  B:BACK";
    p.Text(14, 226, hint, Grey);
    CLOSE_DISPS(play->state.gfxCtx);
}

// What kind of skill, for the lists: element, or Heal / Bless / Hex / Dispel
static std::string SkillKind(const Skill& s) {
    if (s.heal) {
        return "Heal";
    }
    if (s.dispelBuffs || s.dispelDebuffs) {
        return "Dispel";
    }
    if (s.IsSupport()) {
        return s.target == TargetType::AllEnemies ? "Hex" : "Bless";
    }
    return ElementName(s.element);
}

static std::string SkillCost(const Skill& s) {
    return s.mpCost > 0 ? std::to_string(s.mpCost) + "MP" : (s.hpCost > 0 ? std::to_string(s.hpCost) + "HP" : "");
}

// Link's Relic: name, mastery and its bar, affinities and what it teaches (y is the box's top)
static void DrawRelicBox(Retro::Painter& p, int y, bool selected) {
    using namespace Retro;
    int equipped = Party_EquippedRelic();
    p.Window(10, y, SCREEN_WIDTH - 20, 62, selected ? Yellow : White);
    if (selected) {
        p.Text(14, y + 6, "▶", Yellow);
    }
    p.Text(24, y + 6, "RELIC", Green);
    if (equipped < 0) {
        p.Text(72, y + 6, "None", Grey);
        p.Text(24, y + 20, "A: choose a Relic to learn from.", Grey);
        return;
    }
    const RelicDef& def = RelicOf((RelicId)equipped);
    int points = Party_Relics()[equipped].points;
    int mastery = RelicMastery(def, points);
    p.Text(72, y + 6, def.name, selected ? Yellow : White);
    char lv[24];
    snprintf(lv, sizeof(lv), mastery >= def.MaxMastery() ? "MASTERED" : "M%d/%d", mastery, def.MaxMastery());
    p.TextRight(SCREEN_WIDTH - 16, y + 6, lv, mastery >= def.MaxMastery() ? Orange : Yellow);
    // Progress to the next mastery level
    p.Bar(24, y + 18, 20, (int)std::lround(RelicProgress(def, points) * 20), 6, 7, 1, Yellow, GreenDark);
    if (mastery < def.MaxMastery()) {
        p.Text(168, y + 18, "NEXT", Green);
        p.TextRight(SCREEN_WIDTH - 16, y + 18, std::to_string(RelicPointsToNext(def, points)), White);
    }
    p.Text(24, y + 29, RelicAffinityText(def), Cream);
    // Its skills: learned for good (white with ✻), usable while equipped (blue), not yet (grey, with the level)
    for (size_t i = 0; i < def.skills.size() && i < 4; i++) {
        const RelicSkill& rs = def.skills[i];
        int x = 24 + (int)(i % 2) * 144, ry = y + 40 + (int)(i / 2) * 10;
        bool permanent = mastery > rs.mastery, usable = mastery >= rs.mastery;
        std::string label = Fit(rs.skill.name, 13);
        if (permanent) {
            p.Text(x, ry, "✻" + label, White);
        } else if (usable) {
            p.Text(x + 8, ry, label, Blue);
        } else {
            p.Text(x + 8, ry, Fit(rs.skill.name, 10) + " M" + std::to_string(rs.mastery), Grey);
        }
    }
}

// SKILLS: the active list (left, up to 8) and everything learned (right); Link's Relic on top
static void DrawSkillsView(PlayState* play) {
    using namespace Retro;
    std::vector<PartyMember>& members = Party_Members();
    const PartyMember& m = members[std::clamp(sMenu.member, 0, (int)members.size() - 1)];
    std::vector<PoolSkill> pool = Party_SkillPool(m);
    bool link = m.base.name == "Link";

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);
    p.FillScreen(Black);
    DrawTabs(p, sMenu.member);
    p.Text(14, 20, "✻" + Upper(m.base.name) + "✻", White);
    char count[24];
    snprintf(count, sizeof(count), "SKILLS %d/%d", (int)m.activeSkills.size(), kMaxActiveSkills);
    p.TextRight(SCREEN_WIDTH - 14, 20, count, Green);

    int top = 32;
    if (link) {
        DrawRelicBox(p, top, sMenu.page == Page::Skills && sMenu.skillCol == 0);
        top += 64;
    }
    const int rowH = 11;
    const int leftX = 14, rightX = 168;
    p.Text(leftX + 8, top, "ACTIVE", Green);
    p.Text(rightX + 8, top, "LEARNED", Green);
    p.Rect(kColDivX, top, 2, rowH * kSkillRowsShown + 12, White);
    int listY = top + 12;

    // Left: the 8 slots
    for (int i = 0; i < kMaxActiveSkills; i++) {
        int y = listY + i * rowH;
        bool sel = sMenu.skillCol == 1 && i == sMenu.activeCursor;
        if (sel) {
            p.Text(leftX, y, "▶", Yellow);
        }
        if (i < (int)m.activeSkills.size()) {
            p.Text(leftX + 9, y, Fit(m.activeSkills[i], 17), sel ? Yellow : White);
        } else {
            p.Text(leftX + 9, y, "-----", sel ? Yellow : GreenDark);
        }
    }
    // Right: the pool, scrolling; active ones marked
    int first = std::clamp(sMenu.poolScroll, 0, std::max(0, (int)pool.size() - kSkillRowsShown));
    for (int r = 0; r < kSkillRowsShown && first + r < (int)pool.size(); r++) {
        int i = first + r;
        const PoolSkill& ps = pool[i];
        int y = listY + r * rowH;
        bool sel = sMenu.skillCol == 2 && i == sMenu.poolCursor;
        bool active = Party_IsActive(m, ps.skill.name);
        if (sel) {
            p.Text(rightX, y, "▶", Yellow);
        }
        Color c = sel ? Yellow : (!ps.permanent ? Blue : (active ? White : Grey));
        p.Text(rightX + 9, y, Fit(ps.skill.name, 15), c);
        if (active) {
            p.TextRight(SCREEN_WIDTH - 12, y, "◆", Green);
        }
    }
    if ((int)pool.size() > kSkillRowsShown) {
        char more[16];
        snprintf(more, sizeof(more), "%d/%d", sMenu.poolCursor + 1, (int)pool.size());
        p.TextRight(SCREEN_WIDTH - 14, top, more, Grey);
    }

    // The highlighted skill, or a note
    const Skill* focus = nullptr;
    if (sMenu.skillCol == 1 && sMenu.activeCursor < (int)m.activeSkills.size()) {
        for (const PoolSkill& ps : pool) {
            if (ps.skill.name == m.activeSkills[sMenu.activeCursor]) {
                focus = &ps.skill;
            }
        }
    } else if (sMenu.skillCol == 2 && sMenu.poolCursor < (int)pool.size()) {
        focus = &pool[sMenu.poolCursor].skill;
    }
    int infoY = listY + kSkillRowsShown * rowH + 2;
    if (!sMenu.note.empty()) {
        p.Text(14, infoY, Fit(sMenu.note, 37), Red);
    } else if (focus != nullptr) {
        std::string head = SkillKind(*focus) + " " + SkillCost(*focus);
        std::vector<std::string> lines = Wrap(focus->description, 37 - head.size() - 1);
        p.Text(14, infoY, lines.empty() ? "" : lines[0], Cream);
        p.TextRight(SCREEN_WIDTH - 12, infoY, head, Green);
        if (lines.size() > 1 && infoY + kLineH < 222) {
            std::string rest;
            for (size_t i = 1; i < lines.size(); i++) {
                rest += (i > 1 ? " " : "") + lines[i];
            }
            p.Text(14, infoY + kLineH, Fit(rest, 37), Cream);
        }
    }
    bool tree = TreeFor(m.base.name) != nullptr;
    std::string hint = sMenu.skillCol == 0 ? "A:CHANGE RELIC  B:BACK"
                       : sMenu.skillCol == 1 ? (tree ? "A:OFF ◀▶:LEARNED B:BACK Z:TREE" : "A:TAKE OFF  ◀▶:LEARNED  B:BACK")
                                             : (tree ? "A:SET/OFF ◀▶:ACTIVE B:BACK Z:TREE" : "A:SET/OFF  ◀▶:ACTIVE  B:BACK");
    p.Text(14, 228, hint, Grey);
    CLOSE_DISPS(play->state.gfxCtx);
}

// RELICS: Link's Relics with their mastery; the highlighted one in detail
static void DrawRelicsView(PlayState* play) {
    using namespace Retro;
    std::vector<int> owned = OwnedRelics();
    int cursor = owned.empty() ? 0 : std::clamp(sMenu.relicCursor, 0, (int)owned.size() - 1);

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);
    p.FillScreen(Black);
    DrawTabs(p, sMenu.member);
    p.Text(14, 20, "✻LINK✻", White);
    p.TextRight(SCREEN_WIDTH - 14, 20, "RELICS", Green);

    p.Rect(kColDivX, 34, 2, 186, White);
    for (int i = 0; i < (int)owned.size(); i++) {
        const RelicDef& def = RelicOf((RelicId)owned[i]);
        int points = Party_Relics()[owned[i]].points;
        int y = 36 + i * 24;
        bool sel = i == cursor;
        bool equipped = owned[i] == Party_EquippedRelic();
        if (sel) {
            p.Text(14, y, "▶", Yellow);
        }
        p.Text(24, y, Fit(def.name, 14), sel ? Yellow : White);
        if (equipped) {
            p.TextRight(kColDivX - 6, y, "E", Green);
        }
        int mastery = RelicMastery(def, points);
        p.Bar(24, y + 11, 10, (int)std::lround(RelicProgress(def, points) * 10), 6, 6, 1, Yellow, GreenDark);
        p.TextRight(kColDivX - 6, y + 11,
                    mastery >= def.MaxMastery() ? "MAX" : "M" + std::to_string(mastery), Yellow);
    }
    if (!owned.empty()) {
        const RelicDef& def = RelicOf((RelicId)owned[cursor]);
        int mastery = RelicMastery(def, Party_Relics()[owned[cursor]].points);
        int y = 36;
        for (const std::string& l : Wrap(def.description, kRightChars)) {
            p.Text(kRightX, y, l, Cream);
            y += kLineH;
        }
        y += 4;
        for (const auto& [element, affinity] : def.affinities) {
            p.Text(kRightX, y, std::string(affinity == Affinity::Weak ? "Weak " : "Resist ") + ElementName(element),
                   affinity == Affinity::Weak ? Red : Green);
            y += kLineH;
        }
        y += 4;
        for (const RelicSkill& rs : def.skills) {
            bool permanent = mastery > rs.mastery, usable = mastery >= rs.mastery;
            Color c = permanent ? White : (usable ? Blue : Grey);
            p.Text(kRightX, y, (permanent ? "✻" : " ") + Fit(rs.skill.name, kRightChars - 4), c);
            p.TextRight(SCREEN_WIDTH - 10, y, "M" + std::to_string(rs.mastery), c);
            y += kLineH;
        }
    }
    p.Text(14, 228, "A:EQUIP (AGAIN: REMOVE)  B:BACK", Grey);
    CLOSE_DISPS(play->state.gfxCtx);
}

// TREE: one branch at a time (◀▶), its nodes by tier, the highlighted one explained
static void DrawTreeView(PlayState* play) {
    using namespace Retro;
    std::vector<PartyMember>& members = Party_Members();
    const PartyMember& m = members[std::clamp(sMenu.member, 0, (int)members.size() - 1)];
    const SkillTree* tree = TreeFor(m.base.name);
    if (tree == nullptr) {
        return;
    }
    std::vector<const TreeNode*> nodes = BranchNodes(*tree, sMenu.treeBranch);
    int cursor = nodes.empty() ? 0 : std::clamp(sMenu.treeCursor, 0, (int)nodes.size() - 1);

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);
    p.FillScreen(Black);
    DrawTabs(p, sMenu.member);
    p.Text(14, 20, "✻" + Upper(m.base.name) + "✻ SKILL TREE", White);
    char pts[24];
    snprintf(pts, sizeof(pts), "POINTS %d", Party_SkillPointsLeft(m));
    p.TextRight(SCREEN_WIDTH - 14, 20, pts, Yellow);

    // Branch header
    std::string branch = Upper(tree->branches[sMenu.treeBranch]);
    p.Window(10, 32, SCREEN_WIDTH - 20, 18);
    p.Text(16, 37, "◀", Yellow);
    p.TextCenter(SCREEN_WIDTH / 2, 37, branch, Green);
    p.Text(SCREEN_WIDTH - 24, 37, "▶", Yellow);

    // Nodes
    const int rowH = 13;
    int y = 56;
    int lastTier = 0;
    for (int i = 0; i < (int)nodes.size(); i++) {
        const TreeNode& n = *nodes[i];
        bool sel = i == cursor;
        bool learned = Party_HasNode(m, n.id);
        std::string blocker = Party_NodeBlocker(m, n);
        if (n.tier != lastTier) {
            lastTier = n.tier;
            p.Text(14, y, n.tier == kTreeTiers ? "CAP" : "T" + std::to_string(n.tier), GreenDark);
        }
        if (sel) {
            p.Text(42, y, "▶", Yellow);
        }
        Color c = sel ? Yellow : (learned ? White : (blocker.empty() ? White : Grey));
        p.Text(52, y, (learned ? "✻" : " ") + Fit(n.name, 22), c);
        std::string right;
        Color rc = Grey;
        if (learned) {
            right = n.IsPassive() ? "PASSIVE" : "LEARNED";
            rc = Green;
        } else if (blocker.empty()) {
            right = std::to_string(n.cost) + "PT";
            rc = Yellow;
        } else if (blocker.rfind("Level", 0) == 0) {
            right = "LV" + std::to_string(TierLevel(n.tier)) + " " + std::to_string(n.cost) + "PT";
        } else {
            right = std::to_string(n.cost) + "PT";
        }
        p.TextRight(SCREEN_WIDTH - 14, y, right, rc);
        y += rowH;
    }

    // The highlighted node
    p.Rect(10, 150, SCREEN_WIDTH - 20, 2, White);
    if (!nodes.empty()) {
        const TreeNode& n = *nodes[cursor];
        std::string kind = n.IsPassive() ? "PASSIVE" : (!n.replaces.empty() ? "UPGRADES " + Upper(n.replaces) : "SKILL");
        p.Text(14, 156, Fit(kind, 36), Green);
        int dy = 168;
        for (const std::string& l : Wrap(n.description, 37)) {
            if (dy > 190) {
                break;
            }
            p.Text(14, dy, l, Cream);
            dy += kLineH;
        }
        if (!n.IsPassive()) {
            const Skill& sk = n.skill;
            std::string stats = sk.mpCost > 0 ? std::to_string(sk.mpCost) + " MP" : "";
            if (sk.power > 0) {
                stats += std::string(stats.empty() ? "" : "  ") + (sk.heal ? "HEALS " : "POWER ") + std::to_string(sk.power);
            }
            p.Text(14, 200, stats, White);
        }
        std::string blocker = Party_NodeBlocker(m, n);
        if (!blocker.empty() && blocker != "Learned") {
            p.TextRight(SCREEN_WIDTH - 14, 200, Upper(blocker), Red);
        }
    }
    if (!sMenu.note.empty()) {
        p.Text(14, 212, Fit(sMenu.note, 37), sMenu.resetArmed ? Red : Yellow);
    }
    p.Text(14, 228, "◀▶:BRANCH A:LEARN Z:RESET B:BACK", Grey);
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
    if (sMenu.page == Page::Skills) {
        DrawSkillsView(play);
        return;
    }
    if (sMenu.page == Page::Relics) {
        DrawRelicsView(play);
        return;
    }
    if (sMenu.page == Page::Tree) {
        DrawTreeView(play);
        return;
    }
    sMenu.member = std::clamp(sMenu.member, 0, (int)members.size() - 1);
    const PartyMember& m = members[sMenu.member];
    Combatant eff = Party_Effective(m);
    std::vector<int> choices = sMenu.page == Page::Items ? Choices() : std::vector<int>();

    OPEN_DISPS(play->state.gfxCtx);
    Painter p(&OVERLAY_DISP);
    p.FillScreen(Black);
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
        int lvStart = XpForLevel(m.base.level, m.base.name), lvNext = XpForLevel(m.base.level + 1, m.base.name);
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
        if (sel && sMenu.page != Page::Items) {
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
    // A new item under the cursor starts its lore from the first page
    int key = focus == nullptr ? -2 : (int)(sMenu.page == Page::Items ? 100 + sMenu.itemCursor : sMenu.slot);
    if (key != sMenu.focusKey) {
        sMenu.focusKey = key;
        sMenu.lorePage = std::min(sMenu.lorePage, 1);
    }
    sMenu.blink++;
    const bool loreView = sMenu.lorePage > 0 && sMenu.page != Page::Members;
    if (!loreView) {
        sMenu.lorePages = 1;
    }
    if (loreView) {
        // Lore view: name, rarity, then the story and the item's effect, 8 lines of 16 a page
        if (focus == nullptr) {
            sMenu.lorePages = 1;
            p.Text(kRightX, y, "Nothing here.", Grey);
        } else {
            constexpr size_t kLoreChars = 16;
            constexpr int kLoreLines = 8;
            // Choosing gear, the list is hidden behind the lore: the cursor sits on the item's name instead
            const bool choosing = sMenu.page == Page::Items;
            if (choosing) {
                p.Text(kRightX - 2, y, "▶", Yellow);
            }
            for (const std::string& l : Wrap(focus->name, kRightChars - 1)) {
                p.Text(choosing ? kRightX + 8 : kRightX, y, l, RarityRetro(focus->rarity));
                y += kLineH;
            }
            p.Text(kRightX, y, Upper(RarityName(focus->rarity)), Grey);
            y += 14;
            std::vector<std::pair<std::string, Color>> lines;
            for (const std::string& l : Wrap(ItemLore(*focus), kLoreChars)) {
                lines.push_back({ l, Cream });
            }
            if (!BaseOf(*focus).effectText.empty()) {
                if (!lines.empty()) {
                    lines.push_back({ "", Cream });
                }
                for (const std::string& l : Wrap(BaseOf(*focus).effectText, kLoreChars)) {
                    lines.push_back({ l, Orange });
                }
            }
            int perPage = std::clamp((bottom - y) / kLineH, 1, kLoreLines);
            sMenu.lorePages = std::max(1, ((int)lines.size() + perPage - 1) / perPage);
            sMenu.lorePage = std::clamp(sMenu.lorePage, 1, sMenu.lorePages);
            size_t first = (size_t)(sMenu.lorePage - 1) * perPage;
            if (first < lines.size() && lines[first].first.empty()) {
                first++; // don't start a page on the gap before the effect
            }
            for (size_t i = first; i < lines.size() && i < first + perPage; i++) {
                p.Text(kRightX, y, lines[i].first, lines[i].second);
                y += kLineH;
            }
            if (sMenu.lorePage < sMenu.lorePages && (sMenu.blink / 8) % 2 == 0) {
                p.Text(kRightX + kRightW - kCharW, bottom - 8, "▼", Yellow); // more on the next page
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

    const char* zKey = !loreView ? "Z:LORE" : (sMenu.lorePage < sMenu.lorePages ? "Z:MORE" : "Z:STATS");
    std::string hintText = sMenu.page == Page::Members ? "◀▶:TAB A:EQUIP Z:SKILLS B:CLOSE"
                           : sMenu.page == Page::Slots ? std::string("A:CHANGE B:BACK ") + zKey
                                                       : std::string("A:EQUIP B:BACK ") + zKey;
    const char* hint = hintText.c_str();
    p.Text(14, 226, hint, Grey);
    CLOSE_DISPS(play->state.gfxCtx);
}

static RegisterShipInitFunc initFunc(RegisterEquipMenu, { CVAR_CRAWLER_NAME });
