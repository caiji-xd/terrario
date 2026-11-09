#pragma once

/*
----- WALLS (IDs) -----

Background wall IDs. Like tiles.h this has no gint dependency, so the world
generator and the PC tools can use it.

Walls live in their own layer: every tile position holds one block/object
(tiles.h) and, independently, one background wall. As in Terraria, the "safe"
(player-placed) and "unsafe" (natural) versions of a wall are different wall
IDs that share a look; see wall.c for their properties.

Wall IDs are 4 bits wide: two walls are packed per byte (wallsGet/wallsSet).
*/

#include <stdint.h>

enum Walls {
	WALL_NONE,
	WALL_DIRT_NATURAL,		// Unsafe Dirt Wall (Terraria wall ID 2, per the wiki)
	WALL_DIRT,				// Safe (player-placed) Dirt Wall
	WALL_STONE_NATURAL,		// Unsafe stone wall; stands in for natural cave walls
	WALL_STONE,				// Safe Stone Wall
	WALL_WOOD,				// Wood Wall
	WALL_PLANKED,			// Planked Wall (also generated in cabins, collectible)
	WALL_GLASS,				// Glass Wall
	WALL_MUD_NATURAL,		// Unsafe Mud Wall (Terraria wall ID 15, per the wiki)
	WALL_MUD,				// Safe Mud Wall
	WALL_JUNGLE_NATURAL,	// Unsafe Jungle Wall of the underground jungle

	WALLS_COUNT
};

_Static_assert(WALLS_COUNT <= 16, "wall IDs must fit in 4 bits");

/* Nibble-packed wall buffers. Even X in the low nibble. `stride` is the width
   of a row in walls (must be even). */
static inline uint8_t wallsGet(const uint8_t *buffer, int stride, int x, int y)
{
	uint8_t byte = buffer[(y * stride + x) >> 1];
	return (x & 1) ? byte >> 4 : byte & 15;
}

static inline void wallsSet(uint8_t *buffer, int stride, int x, int y, uint8_t wall)
{
	uint8_t *byte = &buffer[(y * stride + x) >> 1];
	if(x & 1) *byte = (*byte & 0x0f) | (uint8_t)(wall << 4);
	else *byte = (*byte & 0xf0) | (wall & 15);
}
