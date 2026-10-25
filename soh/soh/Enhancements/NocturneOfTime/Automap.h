#pragma once

// Nocturne of Time: SMT-style automap for the dungeon crawler.

struct PlayState;
struct Player;

// Record the tile Link stands on (and its walls/doors). Called by the crawler every frame it's active.
void Automap_Update(PlayState* play, Player* player);

// Switch between the corner map and the large centred map.
void Automap_ToggleLarge();

// Draw the automap (corner window, or the large centred map). The retro field HUD calls this.
void Automap_Draw(PlayState* play);
// Is the large map up?
bool Automap_IsLarge();
