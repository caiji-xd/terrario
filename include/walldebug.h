#pragma once

/*
----- WALL DEBUG -----

Debug tools for the background wall system, on [VARS] when
ENABLE_WALL_DEBUG is set in defs.h. The menu shows what is under the cursor
(wall, safety, whether it is hammerable, the hits the held hammer needs and
the result of a housing check there) and offers:

F1  Safety overlay on/off (dot = safe wall, X = unsafe wall)
F2  Give every wall item and an Iron Hammer
F3  Paint a 9x9 square of the selected wall around the cursor
F4  Clear the walls of a 9x9 square around the cursor
F5  Build the reference test house at the cursor (docs/WALLS.md, test T1)
F6  Change the selected wall (also cycles through natural/unsafe walls)
*/

#include <stdbool.h>

struct WallDebug {
	bool overlay;
	int brush;
};

extern struct WallDebug wallDebug;

/* wallDebugMenu
Opens the wall debug menu.
*/
void wallDebugMenu();

/* buildTestHouse
Builds the reference house used by the tests, with its top left frame tile at
(x, y): a 10x8 wooden frame (exterior), safe Wood Walls, a door, a torch, a
work bench and a chair. Returns false if it doesn't fit in the window.
*/
bool buildTestHouse(int x, int y);
