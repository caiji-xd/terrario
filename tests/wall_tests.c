/*
wall_tests.c - reproducible tests of the background wall system.

Runs the real game code (wall.c, housing.c, world.c, stream.c, worldgen.c,
entity.c, npc.c, ...) on a PC. gint functions are stubbed, the calculator's
storage memory is an in-memory filesystem. Every expectation quotes the
Terraria wiki rule it checks; see docs/WALLS.md for the full table.

Build and run: tests/run_wall_tests.sh
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <gint/gint.h>
#include <gint/bfile.h>

#include "defs.h"
#include "world.h"
#include "wall.h"
#include "housing.h"
#include "stream.h"
#include "save.h"
#include "worldgen.h"
#include "walldebug.h"
#include "inventory.h"
#include "chat.h"
#include "update.h"
#include "crafting.h"
#include "npc.h"

/* Globals normally defined by main.c */
char varBuffer[VAR_BUF_HEIGHT][VAR_BUF_WIDTH];
struct SaveData save;
struct World world;
struct Player player;
char versionBuffer[16];
struct GameCompatibilityPresets game;
int frameCallback(volatile int *flag) { *flag = 1; return 0; }

/* gint_world_switch(): run the call directly. The arguments are forwarded as
   full pointer-sized values so pointer arguments survive on 64-bit hosts. */
int gint_world_switch(gint_call_t call)
{
	int (*f)(void *, void *, void *, void *) = call.function;
	return f(call.args[0].pv, call.args[1].pv, call.args[2].pv, call.args[3].pv);
}

/* In-memory BFile */
#define MAX_FILES 512
static struct { char name[64]; uint8_t *data; int size; int used; } files[MAX_FILES];
static struct { int file, pos; } fds[8] = {{-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}, {-1, 0}};
static void pathName(const uint16_t *p, char *out) { int i = 0; for(; p[i] && i < 63; i++) out[i] = (char)p[i]; out[i] = 0; }
static int findFile(const uint16_t *p) { char n[64]; pathName(p, n); for(int i = 0; i < MAX_FILES; i++) if(files[i].used && !strcmp(files[i].name, n)) return i; return -1; }
int BFile_Remove(const uint16_t *p) { int i = findFile(p); if(i < 0) return -1; files[i].used = 0; free(files[i].data); return 0; }
int BFile_Create(const uint16_t *p, int type, int *size)
{
	if(findFile(p) >= 0) return -3;
	for(int i = 0; i < MAX_FILES; i++) if(!files[i].used)
	{
		files[i].used = 1; pathName(p, files[i].name);
		files[i].size = type == BFile_Folder ? 0 : *size;
		files[i].data = calloc(1, files[i].size + 1);
		return 0;
	}
	return -2;
}
int BFile_Open(const uint16_t *p, int mode) { (void)mode; int i = findFile(p); if(i < 0) return -1; for(int k = 0; k < 8; k++) if(fds[k].file < 0) { fds[k].file = i; fds[k].pos = 0; return k; } return -5; }
int BFile_Close(int fd) { fds[fd].file = -1; return 0; }
int BFile_Size(int fd) { return files[fds[fd].file].size; }
int BFile_Write(int fd, const void *d, int n)
{
	int f = fds[fd].file;
	if(n & 1) { printf("ODD WRITE (CASIOWIN would break)\n"); exit(2); }
	if(fds[fd].pos + n > files[f].size) return -1;
	memcpy(files[f].data + fds[fd].pos, d, n); fds[fd].pos += n; return fds[fd].pos;
}
int BFile_Read(int fd, void *d, int n, int whence) { int f = fds[fd].file; if(whence < 0) whence = fds[fd].pos; if(whence + n > files[f].size) return -1; memcpy(d, files[f].data + whence, n); fds[fd].pos = whence + n; return files[f].size - fds[fd].pos; }
int BFile_FindFirst(const uint16_t *pattern, int *handle, uint16_t *found, struct BFile_FileInfo *info)
{
	char n[64]; pathName(pattern, n); (void)info;
	*handle = 0;
	for(int i = 0; i < MAX_FILES; i++)
	{
		if(!files[i].used) continue;
		if(strchr(n, '*') ? !strncmp(files[i].name, n, strchr(n, '*') - n) : !strcmp(files[i].name, n))
		{
			for(int k = 0; files[i].name[k - 1 < 0 ? 0 : k] || k == 0; k++) { found[k] = files[i].name[k]; if(!files[i].name[k]) break; }
			*handle = i + 1;
			return 0;
		}
	}
	return -1;
}
int BFile_FindNext(int handle, uint16_t *found, struct BFile_FileInfo *info) { (void)handle; (void)found; (void)info; return -16; }
int BFile_FindClose(int handle) { (void)handle; return 0; }

/* ---------- test helpers ---------- */

static int failures, checks;

#define CHECK(cond, ...) do { \
	checks++; \
	if(!(cond)) { failures++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while(0)

static void clearArea(int x0, int y0, int x1, int y1)
{
	for(int y = y0; y <= y1; y++)
		for(int x = x0; x <= x1; x++) { setTile(x, y, TILE_NOTHING); setWall(x, y, WALL_NONE); }
}

static void resetWorld()
{
	memset(world.tiles, 0, WINDOW_WIDTH * WINDOW_HEIGHT);
	memset(world.walls, 0, WINDOW_WIDTH * WINDOW_HEIGHT / 2);
	for(int i = 0; i < MAX_ENTITIES; i++) world.entities[i].id = -1;
	free(world.markers); world.markers = NULL; world.numMarkers = 0;
	free(world.npcs); world.npcs = NULL; world.numNPCs = 0;
	for(int i = 0; i < INVENTORY_SIZE; i++) player.inventory.items[i] = (Item){ITEM_NULL, PREFIX_NONE, 0};
}

static int checkAt(int x, int y, struct HousingCheck *c)
{
	return housingCheckRoom((Coords){x, y}, c, -1);
}

static void giveHammer(enum Items hammer)
{
	player.inventory.items[0] = (Item){hammer, PREFIX_NONE, 1};
	player.inventory.hotbarSlot = 0;
	registerHeld();
}

/* Hammers the wall until it breaks; returns the number of hits, or -1 */
static int hitsToBreak(int x, int y, int power)
{
	int damage = 0;
	for(int hits = 1; hits <= 10; hits++)
	{
		int r = hitWall(x, y, power, &damage);
		if(r == WALL_HIT_BROKEN) return hits;
		if(r != WALL_HIT_DAMAGED) return -1;
	}
	return -1;
}

/* House used by most tests (walldebug.c): 10x8 exterior, top left at HX,HY.
   Interior is (HX+1..HX+8, HY+1..HY+6). */
#define HX 40
#define HY 40

/* ---------- tests ---------- */

static void testPlacement()
{
	Item stack;
	printf("T-PLACE placement rules\n");
	resetWorld();

//	"The player can also place walls manually on any tile which does not
//	already contain a wall, given that it is adjacent to another wall or block."
	stack = (Item){ITEM_WOOD_WALL, PREFIX_NONE, 10};
	CHECK(!placeWall(60, 30, &stack), "wall placed in mid-air with nothing adjacent");
	CHECK(stack.amount == 10, "item used by a failed placement");

	setTile(60, 31, TILE_STONE);
	CHECK(placeWall(60, 30, &stack), "wall not placed next to a block");
	CHECK(getWall(60, 30) == WALL_WOOD && stack.amount == 9, "wrong wall or amount after placing");
	CHECK(placeWall(61, 30, &stack), "wall not placed next to another wall");
	CHECK(!placeWall(61, 30, &stack), "wall placed over an existing wall");

//	Walls can be placed behind blocks (they are on their own layer)
	CHECK(placeWall(60, 31, &stack), "wall not placed behind a block");
	CHECK(getTile(60, 31).id == TILE_STONE, "placing a wall changed the block in front");

//	Desktop 1.0.6: "one-block holes are now automatically filled in", and
//	1.0.6.1: the filled walls "are now correctly taken from the inventory"
	resetWorld();
	setTile(50, 52, TILE_STONE);
	for(int y = 49; y <= 51; y++) for(int x = 49; x <= 51; x++) if(!(x == 50 && y == 50) && !(x == 51 && y == 50)) setWall(x, y, WALL_WOOD);
	setWall(52, 50, WALL_WOOD);
	stack = (Item){ITEM_WOOD_WALL, PREFIX_NONE, 5};
	CHECK(placeWall(51, 50, &stack), "wall not placed");
	CHECK(getWall(50, 50) == WALL_WOOD, "one-tile hole next to the placed wall not filled");
	CHECK(stack.amount == 3, "auto-filled wall not taken from the stack (amount %d)", stack.amount);

	stack = (Item){ITEM_WOOD_WALL, PREFIX_NONE, 1};
	setWall(50, 50, WALL_NONE); setWall(51, 50, WALL_NONE);
	CHECK(placeWall(51, 50, &stack) && stack.id == ITEM_NULL && getWall(50, 50) == WALL_NONE,
		"hole filled without items left");

//	Walls don't block movement: no wall-only tile is solid for physics
	for(int w = 1; w < WALLS_COUNT; w++)
	{
		setWall(70, 30, w);
		CHECK(tiles[getTile(70, 30).id].physics == PHYS_NON_SOLID, "wall %d made its tile solid", w);
	}
}

static void testHammering()
{
	printf("T-HAMMER hammer power and removal rules\n");
	resetWorld();

//	"All walls have 100 hit points [...] subtracts 150 hit points multiplied
//	by that hammer's hammer power, rounded down" and the thresholds table:
//	23-33% -> 3 hits, 34-66% -> 2, >= 67% -> 1
	static const struct { enum Items item; int power, hits; } expected[] = {
		{ITEM_WOODEN_HAMMER, 25, 3}, {ITEM_COPPER_HAMMER, 35, 2},
		{ITEM_TIN_HAMMER, 38, 2}, {ITEM_IRON_HAMMER, 40, 2},
	};
	for(unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
	{
		giveHammer(expected[i].item);
		CHECK(player.tool.type == TOOL_TYPE_HAMMER && player.tool.data.pickData.power == expected[i].power,
			"%s: wrong hammer data", items[expected[i].item].name);
		setWall(30, 30, WALL_WOOD);
		CHECK(hitsToBreak(30, 30, player.tool.data.pickData.power) == expected[i].hits,
			"%s should need %d hits", items[expected[i].item].name, expected[i].hits);
	}
	CHECK(hitsToBreak(30, 30, 23) == -1, "hammering an empty tile did something");
	setWall(30, 30, WALL_WOOD);
	{
		int damage = 0;
		hitWall(30, 30, 23, &damage);
		CHECK(damage == 34, "23%% hammer should take floor(23*1.5)=34 HP, took %d", damage);
		CHECK(hitWall(30, 30, 23, &damage) == WALL_HIT_DAMAGED && damage == 68, "second 23%% hit");
		CHECK(hitWall(30, 30, 23, &damage) == WALL_HIT_BROKEN, "23%% hammer: the 3rd hit breaks the wall");
//		Threshold table of the wiki: 33% -> 3 hits, 34% -> 2, 66% -> 2, 67% -> 1
		static const int thresholds[][2] = {{33, 3}, {34, 2}, {66, 2}, {67, 1}};
		for(int i = 0; i < 4; i++)
		{
			setWall(30, 30, WALL_WOOD);
			CHECK(hitsToBreak(30, 30, thresholds[i][0]) == thresholds[i][1], "%d%% hammer should need %d hits", thresholds[i][0], thresholds[i][1]);
		}
	}

//	Unsafe walls "must be removed by starting from their edge (i.e., adjacent
//	space with no wall)"; safe walls can be removed anywhere
	resetWorld();
	for(int y = 20; y <= 26; y++) for(int x = 20; x <= 26; x++) setWall(x, y, WALL_DIRT_NATURAL);
	{
		int damage = 0;
		CHECK(hitWall(23, 23, 40, &damage) == WALL_HIT_PROTECTED, "unsafe wall in the middle could be hammered");
		CHECK(hitsToBreak(20, 23, 40) == 2, "unsafe wall at the edge couldn't be hammered");
		CHECK(hitsToBreak(21, 23, 40) == 2, "newly exposed unsafe wall couldn't be hammered");
		setWall(23, 23, WALL_DIRT);
		CHECK(hitsToBreak(23, 23, 40) == 2, "safe wall surrounded by unsafe walls couldn't be hammered");
	}

//	Blocks in front of a wall protect it from the hammer
	resetWorld();
	setWall(30, 30, WALL_STONE);
	setTile(30, 30, TILE_STONE);
	CHECK(hitsToBreak(30, 30, 40) == -1, "wall behind a block was hammered");

//	Drops: natural Dirt/Mud/cave walls "do not drop as an item", placed walls
//	drop their item, natural Planked Walls "can be collected"
	static const struct { int wall; enum Items drop; } drops[] = {
		{WALL_DIRT_NATURAL, ITEM_NULL}, {WALL_MUD_NATURAL, ITEM_NULL}, {WALL_STONE_NATURAL, ITEM_NULL},
		{WALL_JUNGLE_NATURAL, ITEM_NULL}, {WALL_DIRT, ITEM_DIRT_WALL}, {WALL_STONE, ITEM_STONE_WALL},
		{WALL_WOOD, ITEM_WOOD_WALL}, {WALL_PLANKED, ITEM_PLANKED_WALL}, {WALL_GLASS, ITEM_GLASS_WALL},
		{WALL_MUD, ITEM_MUD_WALL},
	};
	for(unsigned i = 0; i < sizeof(drops) / sizeof(drops[0]); i++)
	{
		resetWorld();
		setWall(30, 30, drops[i].wall);
		hitsToBreak(30, 30, 100);
		CHECK(getWall(30, 30) == WALL_NONE, "%s not removed", wallData[drops[i].wall].name);
		if(drops[i].drop == ITEM_NULL) CHECK(player.inventory.tallyItem(ITEM_DIRT_WALL) + player.inventory.tallyItem(ITEM_MUD_WALL) + player.inventory.tallyItem(ITEM_STONE_WALL) == 0,
			"%s dropped an item", wallData[drops[i].wall].name);
		else CHECK(player.inventory.tallyItem(drops[i].drop) == 1, "%s didn't drop its item", wallData[drops[i].wall].name);
	}

//	Torches hang on walls; a torch only held by its wall falls with it
	resetWorld();
	setWall(30, 30, WALL_WOOD);
	{
		Item torch = {ITEM_TORCH, PREFIX_NONE, 2};
		placeTile(30, 30, &torch);
		CHECK(getTile(30, 30).id == TILE_TORCH, "torch couldn't be placed on a wall");
		placeTile(32, 30, &torch);
		CHECK(getTile(32, 30).id == TILE_NOTHING, "torch placed in mid-air without a wall");
		hitsToBreak(30, 30, 100);
		CHECK(getTile(30, 30).id == TILE_NOTHING, "torch kept floating after its wall was removed");
	}
}

static void testHousing()
{
	struct HousingCheck c;
	printf("T-HOUSE house validity\n");

//	T1: the reference house is valid
	resetWorld();
	CHECK(buildTestHouse(HX, HY), "test house didn't fit");
	CHECK(checkAt(HX + 4, HY + 3, &c) == HOUSING_OK, "test house invalid: %s", housingMessage(&c, false));
	CHECK(c.tiles == 80, "test house should be 10x8 = 80 tiles with its frame, got %d", c.tiles);
	CHECK(!strcmp(housingMessage(&c, false), "This housing is suitable."), "wrong message");
	CHECK(c.home.y == HY + 7 - 0 || c.home.y == HY + 7, "home tile should be on the floor row, got y=%d", c.home.y);

	CHECK(checkAt(HX, HY, &c) == HOUSING_SOLID_START && !strcmp(housingMessage(&c, false), "This is a solid block!"),
		"query on the frame: %s", housingMessage(&c, false));

//	A 4x4 patch without walls is fine, a 5-tile run isn't ("no more than 4x4")
	buildTestHouse(HX, HY);
	for(int y = HY + 1; y <= HY + 4; y++) for(int x = HX + 2; x <= HX + 5; x++) setWall(x, y, WALL_NONE);
	CHECK(checkAt(HX + 6, HY + 3, &c) == HOUSING_OK, "4x4 hole made the house invalid: %s", housingMessage(&c, false));

	buildTestHouse(HX, HY);
	for(int y = HY + 1; y <= HY + 5; y++) for(int x = HX + 2; x <= HX + 6; x++) setWall(x, y, WALL_NONE);
	CHECK(checkAt(HX + 1, HY + 3, &c) == HOUSING_HOLE && !strcmp(housingMessage(&c, false), "This room is missing a wall"),
		"5x5 hole: %s", housingMessage(&c, false));

//	Unsafe walls don't count: "Natural walls in this room need replacing!"
	buildTestHouse(HX, HY);
	for(int y = HY + 1; y <= HY + 6; y++) for(int x = HX + 1; x <= HX + 8; x++) setWall(x, y, WALL_DIRT_NATURAL);
	CHECK(checkAt(HX + 4, HY + 3, &c) == HOUSING_UNSAFE_WALLS
		&& !strcmp(housingMessage(&c, false), "Natural walls in this room need replacing!"), "natural walls: %s", housingMessage(&c, false));

//	A 3-wide gap in the frame (open to the outside air) leaks
	buildTestHouse(HX, HY);
	setTile(HX + 4, HY, TILE_NOTHING); setTile(HX + 5, HY, TILE_NOTHING);
	CHECK(checkAt(HX + 4, HY + 3, &c) != HOUSING_OK, "house with a hole in its roof is valid");

//	Size limits: 60 tiles minimum, frame included
	resetWorld();
	setTile(20, 25, TILE_STONE);
	for(int x = 10; x <= 15; x++) { setTile(x, 10, TILE_WOOD); setTile(x, 18, TILE_WOOD); }
	for(int y = 10; y <= 18; y++) { setTile(10, y, TILE_WOOD); setTile(15, y, TILE_WOOD); }
	for(int y = 11; y <= 17; y++) for(int x = 11; x <= 14; x++) setWall(x, y, WALL_WOOD);
	CHECK(checkAt(12, 14, &c) == HOUSING_TOO_SMALL && c.tiles == 54, "6x9 room (54 tiles) should be too small: %s (%d)", housingMessage(&c, false), c.tiles);
	CHECK(!strcmp(housingMessage(&c, false), "This room is too small"), "wrong too small message");
//	6 x 10 = 60: big enough ("4 tiles wide x 8 tiles high" interior)
	for(int x = 10; x <= 15; x++) { setTile(x, 18, TILE_NOTHING); setTile(x, 19, TILE_WOOD); }
	setTile(10, 18, TILE_WOOD); setTile(15, 18, TILE_WOOD);
	for(int x = 11; x <= 14; x++) setWall(x, 18, WALL_WOOD);
	checkAt(12, 14, &c);
	CHECK(c.tiles == 60 && c.result != HOUSING_TOO_SMALL, "6x10 room should be big enough (%d tiles, %s)", c.tiles, housingMessage(&c, false));

//	Too big: more than 100 tiles across
	resetWorld();
	for(int x = 5; x <= 110; x++) { setTile(x, 10, TILE_WOOD); setTile(x, 14, TILE_WOOD); }
	for(int y = 10; y <= 14; y++) { setTile(5, y, TILE_WOOD); setTile(110, y, TILE_WOOD); }
	for(int y = 11; y <= 13; y++) for(int x = 6; x <= 109; x++) setWall(x, y, WALL_WOOD);
	CHECK(checkAt(50, 12, &c) == HOUSING_TOO_BIG && !strcmp(housingMessage(&c, false), "This room is too big"),
		"106 wide room: %s", housingMessage(&c, false));

//	Furniture: the message names what is missing, in Terraria's order
	resetWorld();
	buildTestHouse(HX, HY);
	setTile(HX + 4, HY + 2, TILE_NOTHING);					// torch
	setTile(HX + 5, HY + 5, TILE_NOTHING); setTile(HX + 5, HY + 6, TILE_NOTHING);	// chair
	CHECK(checkAt(HX + 3, HY + 3, &c) == HOUSING_MISSING_FURNITURE
		&& !strcmp(housingMessage(&c, false), "This house is missing a light source and a chair."),
		"missing torch+chair: %s", housingMessage(&c, false));
	for(int y = HY + 4; y <= HY + 6; y++) setTile(HX, y, TILE_WOOD);	// door -> wall
	setTile(HX + 2, HY + 6, TILE_NOTHING); setTile(HX + 3, HY + 6, TILE_NOTHING);	// bench
	checkAt(HX + 3, HY + 3, &c);
	CHECK(!strcmp(housingMessage(&c, false), "This house is missing a light source, a door, a table, and a chair."),
		"empty house: %s", housingMessage(&c, false));
//	"an indoor platform also works" as the door
	resetWorld();
	buildTestHouse(HX, HY);
	for(int y = HY + 4; y <= HY + 6; y++) setTile(HX, y, TILE_WOOD);
	setTile(HX + 4, HY, TILE_PLATFORM);
	CHECK(checkAt(HX + 3, HY + 3, &c) == HOUSING_OK, "platform as a door: %s", housingMessage(&c, false));

//	Occupied
	resetWorld();
	buildTestHouse(HX, HY);
	housingQuery((Coords){HX + 3, HY + 3});
	CHECK(world.numMarkers == 1, "a valid housing query should put up a banner");
	world.markers[0].occupant = 0;
	CHECK(checkAt(HX + 3, HY + 3, &c) == HOUSING_OCCUPIED && !strcmp(housingMessage(&c, false), "This housing is already occupied."),
		"occupied: %s", housingMessage(&c, false));
	world.markers[0].occupant = -1;

//	Liquids don't matter ("Liquids will not affect the validity of a house")
	resetWorld();
	buildTestHouse(HX, HY);
	setTile(HX + 7, HY + 6, TILE_WATER);
	CHECK(checkAt(HX + 3, HY + 3, &c) == HOUSING_OK, "water made the house invalid: %s", housingMessage(&c, false));

//	Glass Walls are safe ("Glass Walls [...] prevent enemy spawns")
	buildTestHouse(HX, HY);
	for(int y = HY + 1; y <= HY + 6; y++) for(int x = HX + 1; x <= HX + 8; x++) setWall(x, y, WALL_GLASS);
	CHECK(checkAt(HX + 3, HY + 3, &c) == HOUSING_OK, "glass walls: %s", housingMessage(&c, false));
}

static void testBed()
{
	struct HousingCheck c;
	printf("T-BED bedroom rules\n");
	resetWorld();
	buildTestHouse(HX, HY);
//	Bedrooms only need the frame rules and the bed: remove all furniture
	setTile(HX + 4, HY + 2, TILE_NOTHING);
	for(int x = HX + 2; x <= HX + 5; x++) for(int y = HY + 5; y <= HY + 6; y++) setTile(x, y, TILE_NOTHING);
	setTile(HX + 2, HY + 5, TILE_BED_EDGE); setTile(HX + 3, HY + 5, TILE_BED_MID); setTile(HX + 4, HY + 5, TILE_BED_EDGE);
	setTile(HX + 2, HY + 6, TILE_BED_EDGE); setTile(HX + 3, HY + 6, TILE_BED_MID); setTile(HX + 4, HY + 6, TILE_BED_EDGE);
	CHECK(housingCheckBed(HX + 3, HY + 5, &c) == HOUSING_OK && !strcmp(housingMessage(&c, true), "Spawn point set!"),
		"bedroom: %s", housingMessage(&c, true));
	for(int y = HY + 1; y <= HY + 6; y++) for(int x = HX + 1; x <= HX + 8; x++) setWall(x, y, WALL_DIRT_NATURAL);
	CHECK(housingCheckBed(HX + 3, HY + 5, &c) == HOUSING_UNSAFE_WALLS
		&& !strcmp(housingMessage(&c, true), "Natural walls in this bedroom need replacing!"), "natural walls: %s", housingMessage(&c, true));
	for(int y = HY + 1; y <= HY + 6; y++) for(int x = HX + 1; x <= HX + 8; x++) setWall(x, y, WALL_WOOD);
	setTile(HX + 3, HY + 4, TILE_WOOD);
	CHECK(housingCheckBed(HX + 3, HY + 5, &c) == HOUSING_BED_OBSTRUCTED && !strcmp(housingMessage(&c, true), "Your bed is obstructed."),
		"obstructed: %s", housingMessage(&c, true));
}

static void testNPCs()
{
	printf("T-NPC town NPC move-in\n");

//	A killed NPC "will not respawn until a new house becomes available" and
//	NPCs "will spawn after their specific spawning conditions were met and
//	there is an empty valid house" (wiki, House / NPCs)
	resetWorld();
	player.props.x = (HX + 4) * 8; player.props.y = (HY + 2) * 8;
	doNPCHouseCheck();
	CHECK(world.numNPCs == 0, "the Guide came back without any house");

	buildTestHouse(HX, HY);
	setWall(HX + 4, HY + 3, WALL_DIRT_NATURAL);
	for(int y = HY + 1; y <= HY + 6; y++) for(int x = HX + 1; x <= HX + 8; x++) setWall(x, y, WALL_DIRT_NATURAL);
	for(int i = 0; i < 2000; i++) housingTryAutoFind();
	CHECK(world.numMarkers == 0, "a room with natural walls was found as a house");

	for(int y = HY + 1; y <= HY + 6; y++) for(int x = HX + 1; x <= HX + 8; x++) setWall(x, y, WALL_WOOD);
	srand(99);
	for(int i = 0; i < 2000 && world.numMarkers == 0; i++) housingTryAutoFind();
	CHECK(world.numMarkers == 1, "the valid house was not found by the automatic house search");
	doNPCHouseCheck();
	CHECK(world.numNPCs == 1 && world.npcs[0].id == NPC_GUIDE && world.npcs[0].marker == 0,
		"the Guide didn't move into the valid house");

//	Breaking the house (removing walls) makes the NPC move out at the next
//	marker check
	for(int y = HY + 1; y <= HY + 6; y++) for(int x = HX + 1; x <= HX + 8; x++) setWall(x, y, WALL_NONE);
	world.markers[0].doCheck = true;
	doMarkerChecks();
	CHECK(world.numMarkers == 0 && world.npcs[0].marker == -1, "the NPC stayed in a house without walls");
}

static void testSpawning()
{
	int spawned[2] = {0, 0};
	printf("T-SPAWN enemy spawn suppression\n");

//	"If the tile chosen [...] has a safe background wall behind it, the
//	attempt is considered invalid" while unsafe walls don't prevent spawns.
//	Flat ground in an open area, background filled with each kind of wall,
//	many spawn cycles: count spawns.
	for(int pass = 0; pass < 2; pass++)
	{
		resetWorld();
		for(int x = 0; x < WINDOW_WIDTH; x++) for(int y = 60; y < WINDOW_HEIGHT; y++) setTile(x, y, TILE_DIRT);
		for(int x = 0; x < WINDOW_WIDTH; x++) for(int y = 0; y < 60; y++) setWall(x, y, pass ? WALL_WOOD : WALL_DIRT_NATURAL);
		player.props.x = 64 * 8; player.props.y = 57 * 8;
		world.timeTicks = timeToTicks(12, 0);
		srand(1234);
		for(int i = 0; i < 200000; i++)
		{
			doSpawningCycle();
			for(int e = 0; e < MAX_ENTITIES; e++) if(world.entities[e].id != -1) { spawned[pass]++; world.entities[e].id = -1; }
		}
	}
	CHECK(spawned[0] > 50, "unsafe walls should not stop spawns (got %d)", spawned[0]);
	CHECK(spawned[1] == 0, "safe walls should stop all spawns in front of them (got %d)", spawned[1]);
	printf("  spawns: unsafe walls %d, safe walls %d\n", spawned[0], spawned[1]);
}

static void testWorldgenAndSave()
{
	printf("T-SAVE world generation, streaming and save/load of walls\n");
	resetWorld();

	save.worldSeed = 4242;
	wgSetSeed(4242);
	int gx = wgFindSpawnX();
	int gy = wgSurfaceY(gx);
	CHECK(streamInit(gx, gy), "streamInit failed");

//	Natural walls exist, and are unsafe (wiki: "Most naturally generated walls
//	are considered unsafe")
	{
		int natural = 0, safe = 0;
		for(int y = 0; y < WINDOW_HEIGHT; y++) for(int x = 0; x < WINDOW_WIDTH; x++)
		{
			int w = getWall(x, y);
			if(w == WALL_NONE) continue;
			natural++;
			if(wallData[w].safe && w != WALL_WOOD && w != WALL_PLANKED) safe++;
		}
		CHECK(natural > 1000, "too few natural walls generated (%d)", natural);
		CHECK(safe == 0, "%d natural walls are safe", safe);
	}

//	Place walls, walk far away (chunks unload and get saved), come back
	int lx = gx - stream.originX, ly = gy - stream.originY - 3;
	for(int x = lx - 3; x <= lx + 3; x++) setWall(x, ly, WALL_GLASS);
	int savedX = lx + stream.originX, savedY = ly + stream.originY;
	player.props.x = (gx - stream.originX) * 8; player.props.y = (gy - 4 - stream.originY) * 8;
	CHECK(streamTeleport((gx + 5000) * 8, gy * 8), "teleport away failed");
	CHECK(streamTeleport(gx * 8, (gy - 4) * 8), "teleport back failed");
	for(int x = -3; x <= 3; x++)
		CHECK(getWall(savedX + x - stream.originX, savedY - stream.originY) == WALL_GLASS, "placed wall lost after unloading (x%+d)", x);

//	Regenerated chunks still have their walls (determinism), and saves only
//	happen for modified chunks
	int filesBefore = 0;
	for(int i = 0; i < MAX_FILES; i++) filesBefore += files[i].used;
	CHECK(streamTeleport((gx + 20000) * 8, gy * 8), "teleport far failed");
	int filesAfter = 0;
	for(int i = 0; i < MAX_FILES; i++) filesAfter += files[i].used;
	CHECK(filesAfter == filesBefore, "unmodified chunks were saved (%d new files)", filesAfter - filesBefore);

//	The wall nibbles survive being shifted by an odd number of tiles? The
//	window always moves by whole chunks (32 tiles), so the packed layout
//	stays aligned. Check a pattern across one step.
	resetWorld();
	CHECK(streamInit(0, 0), "streamInit failed");
	for(int x = 0; x < WINDOW_WIDTH; x++) setWall(x, 40, 1 + (x % (WALLS_COUNT - 1)));
	int ox = stream.originX;
	player.props.x = (WINDOW_WIDTH - SHIFT_MARGIN_X + 2) * 8; player.props.y = 40 * 8;
	CHECK(streamUpdate(), "streamUpdate failed");
	CHECK(stream.originX == ox + CHUNK_SIZE, "window didn't move");
	for(int x = 0; x < WINDOW_WIDTH - CHUNK_SIZE; x++)
		CHECK(getWall(x, 40) == 1 + ((x + CHUNK_SIZE) % (WALLS_COUNT - 1)), "wall pattern broken after the window moved (x=%d)", x);
}

static void testRecipes()
{
	extern const struct Recipe recipes[];
	extern const int numRecipes;
	printf("T-CRAFT recipes\n");
	static const struct { enum Items result; int amount; enum Items ing; int n; } expected[] = {
		{ITEM_WOOD_WALL, 4, ITEM_WOOD, 1}, {ITEM_STONE_WALL, 4, ITEM_STONE, 1},
		{ITEM_DIRT_WALL, 4, ITEM_DIRT, 1}, {ITEM_GLASS_WALL, 4, ITEM_GLASS, 1},
		{ITEM_MUD_WALL, 4, ITEM_MUD, 1}, {ITEM_WOOD, 1, ITEM_WOOD_WALL, 4},
		{ITEM_WOODEN_HAMMER, 1, ITEM_WOOD, 8}, {ITEM_IRON_HAMMER, 1, ITEM_IRON_BAR, 8},
	};
	for(unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
	{
		bool found = false;
		for(int r = 0; r < numRecipes; r++)
		{
			const struct Recipe *rec = &recipes[r];
			if(rec->result.id != expected[i].result || rec->result.amount != expected[i].amount) continue;
			for(int k = 0; k < rec->numIngredients; k++)
				if(rec->ingredients[k].id == expected[i].ing && rec->ingredients[k].amount == expected[i].n) found = true;
		}
		CHECK(found, "missing recipe for %d x %s", expected[i].amount, items[expected[i].result].name);
	}
}

int main()
{
	game = (struct GameCompatibilityPresets){WINDOW_WIDTH, WINDOW_HEIGHT};
	world = (struct World){
		.tiles = calloc(WINDOW_WIDTH * WINDOW_HEIGHT, 1),
		.walls = calloc(WINDOW_WIDTH * WINDOW_HEIGHT / 2, 1),
		.entities = calloc(MAX_ENTITIES, sizeof(Entity)),
		.explosion = { .particles = calloc(EXPLOSION_NUM_PARTICLES, sizeof(Particle)) },
		.chests = { .addChest = addChest, .removeChest = removeChest, .findChest = findChest,
			.flushChest = flushChest, .closeChest = closeChest },
		.placeTile = placeTile, .removeTile = removeTile,
		.spawnEntity = spawnEntity, .removeEntity = removeEntity, .checkFreeEntitySlot = checkFreeEntitySlot
	};
	player = (struct Player){
		.props = {.width = 12, .height = 21},
		.combat = {.health = 100},
		.inventory = {.getFirstFreeSlot = getFirstFreeSlot, .removeItem = removeItem, .stackItem = stackItem,
			.tallyItem = tallyItem, .findSlot = findSlot, .getSelected = getSelected},
		.tool = {TOOL_TYPE_NONE}
	};
	save.error = -99;

	testPlacement();
	testHammering();
	testHousing();
	testBed();
	testNPCs();
	testSpawning();
	testRecipes();
	testWorldgenAndSave();

	printf("\n%d checks, %d failures\n", checks, failures);
	return failures != 0;
}
