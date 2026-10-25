#pragma once

// Nocturne of Time: SMT-style automap for the dungeon crawler.

struct PlayState;
struct Player;

// Record the tile Link stands on (and its walls/doors). Called by the crawler every frame it's active.
void Automap_Update(PlayState* play, Player* player);

// Switch between the corner map and the large centred map.
void Automap_ToggleLarge();

// Draw the automap: in its own window filling the square box at (x, y), or the large map centred on screen
// when it is toggled on. The retro field HUD calls this.
void Automap_Draw(PlayState* play, int x, int y, int size);
// Is the large map up?
bool Automap_IsLarge();
