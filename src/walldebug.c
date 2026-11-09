#include <stdbool.h>
#include <stdio.h>

#include <gint/display.h>
#include <gint/keyboard.h>

#include "walldebug.h"
#include "wall.h"
#include "housing.h"
#include "world.h"
#include "inventory.h"

struct WallDebug wallDebug = { .overlay = false, .brush = WALL_WOOD };

bool buildTestHouse(int x, int y)
{
	const int w = 10, h = 8;

	if(x < 3 || y < 3 || x + w + 3 >= game.WORLD_WIDTH || y + h + 3 >= game.WORLD_HEIGHT) return false;

	for(int dy = -2; dy < h + 2; dy++)
	{
		for(int dx = -1; dx < w + 1; dx++)
		{
			int tx = x + dx, ty = y + dy;
			bool frame = dx == 0 || dx == w - 1 || dy == 0 || dy == h - 1;
			bool inside = dx >= 0 && dx < w && dy >= 0 && dy < h;

			if(dy >= h) setTile(tx, ty, TILE_STONE);	// ground under the house
			else if(inside && frame) setTile(tx, ty, TILE_WOOD);
			else setTile(tx, ty, TILE_NOTHING);

			setWall(tx, ty, inside ? WALL_WOOD : WALL_NONE);
		}
	}
//	Door in the left wall, furniture inside
	for(int dy = h - 4; dy <= h - 2; dy++) setTile(x, y + dy, TILE_DOOR_C);
	setTile(x + 2, y + h - 2, TILE_WBENCH_L);
	setTile(x + 3, y + h - 2, TILE_WBENCH_R);
	setTile(x + 5, y + h - 3, TILE_CHAIR_R);
	setTile(x + 5, y + h - 2, TILE_CHAIR_R);
	setTile(x + 4, y + 2, TILE_TORCH);
	for(int dy = -2; dy < h + 2; dy++)
		for(int dx = -1; dx < w + 1; dx++) setVar(x + dx, y + dy);
	updateMarkerChecks((Coords){x + w / 2, y + h / 2});
	return true;
}

static void giveItem(enum Items id, int amount)
{
	Item stack = {id, PREFIX_NONE, amount};
	int slot = player.inventory.getFirstFreeSlot(id);
	if(slot > -1) player.inventory.stackItem(&player.inventory.items[slot], &stack);
}

static void paint(int cx, int cy, int wall)
{
	for(int y = cy - 4; y <= cy + 4; y++)
		for(int x = cx - 4; x <= cx + 4; x++) setWall(x, y, wall);
}

void wallDebugMenu()
{
	key_event_t key;
	Coords c = player.cursorTile;

	while(true)
	{
		int wall = getWall(c.x, c.y);
		struct HousingCheck check;
		const char *house;
		int hits = 0;

		if(player.tool.type == TOOL_TYPE_HAMMER)
		{
			int dmg = player.tool.data.pickData.power * 150 / 100;
			hits = (WALL_HIT_POINTS + dmg - 1) / dmg;
		}
		housingCheckRoom(c, &check, -1);
		house = housingMessage(&check, false);

		dclear(C_WHITE);
		dprint(0, 0, C_BLACK, "Wall: %s", wallData[wall].name);
		if(wall != WALL_NONE)
			dprint(0, 7, C_BLACK, "%s, %s", wallData[wall].safe ? "safe" : "unsafe",
				wallData[wall].safe || wallIsExposed(c.x, c.y) ? "hammerable" : "not at an edge");
		if(hits) dprint(0, 14, C_BLACK, "Held hammer: %d hits", hits);
		dprint(0, 21, C_BLACK, "Room: %d tiles", check.tiles);
		dtext(0, 28, C_BLACK, house);
		dprint(0, 42, C_BLACK, "F1 overlay:%s  F6 brush:", wallDebug.overlay ? "on" : "off");
		dtext(0, 49, C_BLACK, wallData[wallDebug.brush].name);
		dtext(0, 57, C_BLACK, "F2 give F3 fill F4 clr F5 house");
		dupdate();

		key = getkey_opt(GETKEY_NONE, NULL);
		switch(key.key)
		{
			case KEY_F1:
				wallDebug.overlay = !wallDebug.overlay;
				break;

			case KEY_F2:
				for(enum Items i = ITEM_DIRT_WALL; i <= ITEM_MUD_WALL; i++) giveItem(i, 999);
				giveItem(ITEM_IRON_HAMMER, 1);
				registerHeld();
				break;

			case KEY_F3:
				paint(c.x, c.y, wallDebug.brush);
				break;

			case KEY_F4:
				paint(c.x, c.y, WALL_NONE);
				break;

			case KEY_F5:
				buildTestHouse(c.x, c.y);
				return;

			case KEY_F6:
				wallDebug.brush = wallDebug.brush % (WALLS_COUNT - 1) + 1;
				break;

			case KEY_EXIT:
			case KEY_VARS:
				return;

			default:
				break;
		}
	}
}
