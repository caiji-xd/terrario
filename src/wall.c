#include <stdbool.h>
#include <stdint.h>

#include "wall.h"
#include "world.h"
#include "stream.h"
#include "npc.h"
#include "render.h"

extern bopti_image_t
img_walls_dirt,
img_walls_stone,
img_walls_wood,
img_walls_planked,
img_walls_glass,
img_walls_mud,
img_walls_jungle;

/*
Safe and unsafe versions of a wall share their sprite: the wiki notes that
they are visually identical ("Background walls", Safe and unsafe walls).
Natural Dirt, Mud and cave walls drop nothing ("Dirt Wall", "Mud Wall": "It
does not drop as an item when destroyed"), naturally generated Planked Walls
can be collected ("Planked Wall", Notes).
*/
const WallData wallData[WALLS_COUNT] = {
//		Sprite					Name					Drop				Safe	Map shade
	{	NULL,					"No wall",				ITEM_NULL,			false,	MAP_WALL_LIGHT	},	// WALL_NONE
	{	&img_walls_dirt,		"Dirt Wall (natural)",	ITEM_NULL,			false,	MAP_WALL_LIGHT	},	// WALL_DIRT_NATURAL
	{	&img_walls_dirt,		"Dirt Wall",			ITEM_DIRT_WALL,		true,	MAP_WALL_LIGHT	},	// WALL_DIRT
	{	&img_walls_stone,		"Stone Wall (natural)",	ITEM_NULL,			false,	MAP_WALL_DARK	},	// WALL_STONE_NATURAL
	{	&img_walls_stone,		"Stone Wall",			ITEM_STONE_WALL,	true,	MAP_WALL_DARK	},	// WALL_STONE
	{	&img_walls_wood,		"Wood Wall",			ITEM_WOOD_WALL,		true,	MAP_WALL_LIGHT	},	// WALL_WOOD
	{	&img_walls_planked,		"Planked Wall",			ITEM_PLANKED_WALL,	true,	MAP_WALL_LIGHT	},	// WALL_PLANKED
	{	&img_walls_glass,		"Glass Wall",			ITEM_GLASS_WALL,	true,	MAP_WALL_LIGHT	},	// WALL_GLASS
	{	&img_walls_mud,			"Mud Wall (natural)",	ITEM_NULL,			false,	MAP_WALL_DARK	},	// WALL_MUD_NATURAL
	{	&img_walls_mud,			"Mud Wall",				ITEM_MUD_WALL,		true,	MAP_WALL_DARK	},	// WALL_MUD
	{	&img_walls_jungle,		"Jungle Wall (natural)",ITEM_NULL,			false,	MAP_WALL_DARK	},	// WALL_JUNGLE_NATURAL
};

int itemWall(enum Items item)
{
	switch(item)
	{
		case ITEM_DIRT_WALL:	return WALL_DIRT;
		case ITEM_STONE_WALL:	return WALL_STONE;
		case ITEM_WOOD_WALL:	return WALL_WOOD;
		case ITEM_PLANKED_WALL:	return WALL_PLANKED;
		case ITEM_GLASS_WALL:	return WALL_GLASS;
		case ITEM_MUD_WALL:		return WALL_MUD;
		default:				return WALL_NONE;
	}
}

bool wallIsSafe(int x, int y)
{
	return wallData[getWall(x, y)].safe;
}

/* Anything occupying a tile counts as a "block" for wall support, except
   water: liquids aren't tiles in Terraria. */
static bool hasTile(int x, int y)
{
	int id = getTile(x, y).id;
	return id != TILE_NOTHING && id != TILE_WATER;
}

static bool inWindow(int x, int y)
{
	return (unsigned)x < (unsigned)game.WORLD_WIDTH && (unsigned)y < (unsigned)game.WORLD_HEIGHT;
}

bool canPlaceWall(int x, int y)
{
	static const int8_t deltas[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

	if(!inWindow(x, y) || getWall(x, y) != WALL_NONE) return false;
	for(int i = 0; i < 4; i++)
	{
		int nx = x + deltas[i][0], ny = y + deltas[i][1];
		if(getWall(nx, ny) != WALL_NONE || hasTile(nx, ny)) return true;
	}
	return false;
}

/* A wall-less tile whose four neighbours all have walls */
static bool isOneTileHole(int x, int y)
{
	return inWindow(x, y) && getWall(x, y) == WALL_NONE
		&& getWall(x - 1, y) != WALL_NONE && getWall(x + 1, y) != WALL_NONE
		&& getWall(x, y - 1) != WALL_NONE && getWall(x, y + 1) != WALL_NONE;
}

static void useWallItem(Item *item)
{
	item->amount--;
	if(item->amount <= 0) *item = (Item){ITEM_NULL, PREFIX_NONE, 0};
}

bool placeWall(int x, int y, Item *item)
{
	static const int8_t deltas[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
	int wall = itemWall(item->id);

	if(wall == WALL_NONE || item->amount <= 0 || !canPlaceWall(x, y)) return false;

	setWall(x, y, wall);
	useWallItem(item);
	updateMarkerChecks((Coords){x, y});

//	Desktop 1.0.6: "When placing background walls, one-block holes are now
//	automatically filled in." 1.0.6.1: the extra walls come out of the
//	inventory, so stop when the stack runs out.
	for(int i = 0; i < 4 && item->id != ITEM_NULL; i++)
	{
		int nx = x + deltas[i][0], ny = y + deltas[i][1];
		if(!isOneTileHole(nx, ny)) continue;
		setWall(nx, ny, wall);
		useWallItem(item);
	}
	return true;
}

bool wallIsExposed(int x, int y)
{
//	The wiki says "adjacent space with no wall" without saying whether
//	diagonals count; the 8 surrounding tiles are used here.
	for(int dy = -1; dy <= 1; dy++)
	{
		for(int dx = -1; dx <= 1; dx++)
		{
			if((dx || dy) && inWindow(x + dx, y + dy) && getWall(x + dx, y + dy) == WALL_NONE) return true;
		}
	}
	return false;
}

/* Blocks in front of a wall take the hammer hit instead (Terraria turns them
   into slopes, which this game doesn't have). */
static bool blocksHammer(int x, int y)
{
	enum PhysicsTypes physics = tiles[getTile(x, y).id].physics;
	return physics == PHYS_SOLID || physics == PHYS_SAND || getTile(x, y).id == TILE_PLATFORM;
}

int hitWall(int x, int y, int hammerPower, int *damage)
{
	int wall = getWall(x, y);

	if(!inWindow(x, y) || wall == WALL_NONE || blocksHammer(x, y)) return WALL_HIT_NOTHING;
	if(!wallData[wall].safe && !wallIsExposed(x, y)) return WALL_HIT_PROTECTED;

//	"All walls have 100 hit points each. Hitting a wall with a hammer
//	subtracts 150 hit points multiplied by that hammer's hammer power, rounded
//	down." (Hammers, Hammer power)
	*damage += hammerPower * 150 / 100;
	if(*damage < WALL_HIT_POINTS) return WALL_HIT_DAMAGED;

	*damage = 0;
	destroyWall(x, y, true);
	return WALL_HIT_BROKEN;
}

/* Torches can hang on walls (wiki: "Torches can now be placed directly onto
   walls", 1.2). One that only hung on this wall falls off with it. */
static bool torchSupported(int x, int y)
{
	return hasTile(x - 1, y) || hasTile(x + 1, y) || hasTile(x, y - 1) || hasTile(x, y + 1)
		|| getWall(x, y) != WALL_NONE;
}

void destroyWall(int x, int y, bool drop)
{
	int wall = getWall(x, y);
	enum Items item = wallData[wall].drop;

	if(wall == WALL_NONE) return;
	setWall(x, y, WALL_NONE);

	if(drop && item != ITEM_NULL)
	{
		Item stack = {item, PREFIX_NONE, 1};
		int slot = player.inventory.getFirstFreeSlot(item);
		if(slot > -1) player.inventory.stackItem(&player.inventory.items[slot], &stack);
	}

	if(getTile(x, y).id == TILE_TORCH && !torchSupported(x, y)) removeTile(x, y);
	updateMarkerChecks((Coords){x, y});
}

int wallVariant(int x, int y)
{
	uint32_t h = (uint32_t)(x + stream.originX) * 0x9E3779B1u ^ (uint32_t)(y + stream.originY) * 0x85EBCA77u;
	h ^= h >> 15;
	h *= 0x2C1B3C6Du;
	h ^= h >> 12;
	return h % 3;
}
