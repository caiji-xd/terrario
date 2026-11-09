#include <stdbool.h>

#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/defs/util.h>

#include "map.h"
#include "world.h"
#include "wall.h"
#include "stream.h"
#include "npc.h"

#define MAP_DITHER -1

int mapShade(int x, int y)
{
	int id = getTile(x, y).id;
	int wall = getWall(x, y);

	if(id == TILE_WATER) return C_DARK;
	if(id != TILE_NOTHING && tiles[id].render)
	{
		switch(tiles[id].physics)
		{
			case PHYS_SOLID:
			case PHYS_SAND:
				return C_BLACK;
			default:
				return C_DARK;
		}
	}
	if(wall != WALL_NONE) return wallData[wall].mapShade == MAP_WALL_DARK ? C_LIGHT : MAP_DITHER;
	return C_WHITE;
}

static void drawMap(int top, bool walls, unsigned int frame)
{
	int playerX = (player.props.x + (player.props.width >> 1)) >> 3;
	int playerY = (player.props.y + (player.props.height >> 1)) >> 3;

	dclear(C_WHITE);
	for(int sy = 0; sy < SCREEN_HEIGHT - 7; sy++)
	{
		int y = top + sy;
		for(int x = 0; x < game.WORLD_WIDTH && x < SCREEN_WIDTH; x++)
		{
			int shade = mapShade(x, y);
			if(!walls && shade != C_BLACK && shade != C_DARK) shade = C_WHITE;
			if(shade == MAP_DITHER) shade = ((x + y) & 1) ? C_LIGHT : C_WHITE;
			if(shade != C_WHITE) dpixel(x, sy, shade);
		}
	}

//	House banners and NPCs
	for(int i = 0; i < world.numMarkers; i++)
	{
		Coords p = world.markers[i].position;
		if(p.y - top >= 0 && p.y - top < SCREEN_HEIGHT - 7) drect(p.x - 1, p.y - top - 1, p.x + 1, p.y - top, C_INVERT);
	}
	for(int i = 0; i < world.numNPCs; i++)
	{
		int x = world.npcs[i].props.x >> 3, y = (world.npcs[i].props.y >> 3) - top;
		if(y >= 0 && y < SCREEN_HEIGHT - 7 && (frame & 8)) dpixel(x, y, C_INVERT);
	}

//	Blinking player
	if(frame & 4) drect(playerX - 1, playerY - top - 1, playerX + 1, playerY - top + 1, C_INVERT);

	dprint_opt(0, SCREEN_HEIGHT, C_BLACK, C_WHITE, DTEXT_LEFT, DTEXT_BOTTOM,
		"x%d y%d F1:walls %s", playerX + stream.originX, playerY + stream.originY, walls ? "on" : "off");
	dupdate();
}

void mapMenu()
{
	int maxTop = game.WORLD_HEIGHT - (SCREEN_HEIGHT - 7);
	int playerY = (player.props.y >> 3);
	int top = min(max(0, playerY - (SCREEN_HEIGHT - 7) / 2), maxTop);
	bool walls = true;
	unsigned int frame = 0;
	key_event_t key;

	while(true)
	{
		drawMap(top, walls, frame++);
		key = getkey_opt(GETKEY_REP_ARROWS, NULL);
		switch(key.key)
		{
			case KEY_UP: top = max(0, top - 4); break;
			case KEY_DOWN: top = min(maxTop, top + 4); break;
			case KEY_F1: walls = !walls; break;
			case KEY_EXIT:
			case KEY_OPTN:
			case KEY_MENU:
				return;
			default:
				break;
		}
	}
}
