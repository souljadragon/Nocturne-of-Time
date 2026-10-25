#pragma once

// Nocturne of Time: the way to a dungeon's boss. Until the dungeons are remixed for the crawler, the Deku Tree's
// boss gate is the great web on the lobby floor: it stays sealed until the party has won kBossGateWins battles
// in the dungeon, then stepping onto it offers a warp straight to Queen Gohma's chamber. Beating her clears
// the dungeon; losing sends the party back to the dungeon entrance to try again.

#include <nlohmann/json.hpp>
#include <string>

struct PlayState;
struct Player;

namespace Retro {
class Painter;
}

constexpr int kBossGateWins = 8;

// ---- Progress (part of the save file) ----
int BossGate_Wins();
bool BossGate_IsOpen();    // enough battles won
bool BossGate_IsCleared(); // Gohma beaten
void BossGate_Reset();
nlohmann::json BossGate_Save();
void BossGate_Load(const nlohmann::json& data);

// A battle in the dungeon was won. Returns a line for the battle to show ("The web stirs... 3/8", or the gate
// opening), empty when there is nothing to say.
std::string BossGate_OnVictory(PlayState* play);
// Gohma is beaten.
void BossGate_SetCleared();

// ---- On the dungeon floor ----
// Every frame the crawler runs in the dungeon: finds the web and makes the gate sparkle.
void BossGate_Tick(PlayState* play, Player* player);
// The crawler finished a step. True if Link is now on the gate: its prompt opens (and no encounter is rolled).
bool BossGate_OnStep(PlayState* play, Player* player);
// The prompt in the message window: A to descend (or dismiss), B to cancel.
bool BossGate_PromptOpen();
void BossGate_Update(PlayState* play, void* input);
// Draw the prompt into the field message window (empty when closed). Returns true if it drew.
bool BossGate_DrawPrompt(Retro::Painter& p, int x, int y, int w);
// World position of the gate in this scene, for the automap; false if there is none (yet).
bool BossGate_Position(PlayState* play, float& x, float& y, float& z);

// ---- Warps ----
bool BossGate_InBossRoom(PlayState* play);
void BossGate_WarpToBoss(PlayState* play);
void BossGate_ReturnToDungeon(PlayState* play);
