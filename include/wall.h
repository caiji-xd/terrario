#pragma once

/*
----- WALL -----

Background walls: properties, placing, hammering and safety.

Rules follow the Official Terraria Wiki (terraria.wiki.gg), see
docs/WALLS.md for the page-by-page comparison:
- A wall can be placed on any tile that has no wall yet, if a neighbouring
  tile has a wall or a block ("Background walls").
- Placing a wall also fills one-tile holes next to it (1.0.6 history entry).
- Walls are removed with a hammer. Every wall has 100 hit points, each hit
  removes floor(hammer power * 150%) of them, and hammers hit walls at half
  their tool speed ("Hammers").
- Unsafe (natural) walls can only be removed from their edge, i.e. next to a
  space without a wall ("Background walls", Safe and unsafe walls).
- Walls never block movement or liquids; they only matter for housing, enemy
  spawning, torch support and looks.
*/

#include <stdbool.h>
#include <gint/display.h>

#include "walls.h"
#include "inventory.h"

#define WALL_HIT_POINTS 100

typedef struct {
	bopti_image_t *sprite;
	const char *name;
//	Item given back when the wall is hammered (ITEM_NULL: none)
	enum Items drop;
//	Safe walls count for housing and stop enemies spawning in front of them
	bool safe;
//	Shade on the map (MAP_WALL_*)
	unsigned char mapShade;
} WallData;

extern const WallData wallData[WALLS_COUNT];

/* Shades used by the full screen map */
enum MapWallShades {
	MAP_WALL_LIGHT,
	MAP_WALL_DARK
};

/* Results of hitWall() */
enum WallHitResults {
	WALL_HIT_NOTHING,	// No wall there, or a block in front of it
	WALL_HIT_PROTECTED,	// Unsafe wall that isn't at an edge yet
	WALL_HIT_DAMAGED,
	WALL_HIT_BROKEN
};

/* itemWall
Returns the wall placed by an item, or WALL_NONE if it isn't a wall item.
*/
int itemWall(enum Items item);

/* wallIsSafe
Whether the window-local tile has a safe (player-placed style) wall.
*/
bool wallIsSafe(int x, int y);

/* canPlaceWall
Whether a wall can be placed at the window-local tile.
*/
bool canPlaceWall(int x, int y);

/* placeWall
Places the wall of a wall item at the given tile, then fills one-tile holes
next to it while there are items left. Decreases the item amount.

Returns true if at least one wall was placed.
*/
bool placeWall(int x, int y, Item *item);

/* wallIsExposed
Whether an unsafe wall touches a space without a wall, which is required
before it can be hammered.
*/
bool wallIsExposed(int x, int y);

/* hitWall
One hammer hit on the wall at the given tile.

x, y: Window-local tile.
hammerPower: Hammer power in percent.
damage: Accumulated damage on the targeted wall, updated by the call. Reset
it to 0 when the target changes.

Returns one of enum WallHitResults.
*/
int hitWall(int x, int y, int hammerPower, int *damage);

/* destroyWall
Removes the wall at the given tile, giving its item to the player if drop is
true, and updates everything that depends on it (torches, housing).
*/
void destroyWall(int x, int y, bool drop);

/* wallVariant
Sprite variant (0-2) of the wall at a window-local tile. Derived from the
global position, so it never changes when the chunk window moves.
*/
int wallVariant(int x, int y);
