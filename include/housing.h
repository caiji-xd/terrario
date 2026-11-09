#pragma once

/*
----- HOUSING -----

House and bedroom validity, following the Official Terraria Wiki ("House",
House validity evaluation; "Beds", Setting player spawn point). The check
runs in three phases:

1. Frame: flood fill (8 directions) of the room interior from the start tile.
   The fill stops at "frame" tiles: solid blocks, platforms and closed doors
   (or the hinge tile of an open door). Every tile of the room, frame
   included, needs a solid tile or a safe wall within 2 tiles up, down, left
   or right ("a cross with an arm length of 5 tiles"). The room must have at
   least 60 and fewer than 750 tiles, frame included, and be at most 100
   tiles wide and tall.
2. Furniture: a light source, a table, a chair and a door somewhere in the
   room (frame included).
3. Properties: not already occupied, and a valid "home tile" to stand on,
   chosen with the wiki's home tile score.

Bedrooms only need phase 1 and an unobstructed bed.
*/

#include <stdbool.h>

#include "entity.h"

enum HousingResults {
	HOUSING_OK,
	HOUSING_SOLID_START,		// The query started inside a block
	HOUSING_HOLE,				// Part of the room has no wall nearby
	HOUSING_UNSAFE_WALLS,		// Same, but natural walls are what's there
	HOUSING_TOO_SMALL,
	HOUSING_TOO_BIG,
	HOUSING_EDGE,				// Reaches the edge of the loaded world
	HOUSING_MISSING_FURNITURE,
	HOUSING_OCCUPIED,
	HOUSING_NO_STANDING_SPACE,
	HOUSING_BED_OBSTRUCTED
};

/* Bits of HousingCheck.missing, in the order the wiki's message lists them */
enum HousingNeeds {
	NEED_LIGHT = 1,
	NEED_DOOR = 2,
	NEED_TABLE = 4,
	NEED_CHAIR = 8
};

/* Upper bounds of the checks, from the wiki */
#define HOUSE_MIN_TILES 60
#define HOUSE_MAX_TILES 750
#define HOUSE_MAX_SIZE 100
#define HOUSE_CROSS_ARM 2
#define HOME_TILE_BASE_SCORE 50

struct HousingCheck {
	int result;
	int missing;		// enum HousingNeeds bits when furniture is missing
	int tiles;			// Room size, frame included
	int x1, y1, x2, y2;	// Bounds of the room, frame included
	Coords home;		// Home tile (the floor tile the NPC stands on)
	int homeScore;
	int marker;			// House marker already inside the room, or -1
	Coords failAt;		// Tile whose cross check failed
};

/* housingCheckRoom
Evaluates the room containing the given window-local tile.

ignoreMarker: index of a house marker to ignore for the occupancy check (the
marker being re-validated), or -1.

Returns check->result.
*/
int housingCheckRoom(Coords start, struct HousingCheck *check, int ignoreMarker);

/* housingCheckBed
Evaluates the bedroom of the bed whose top middle tile is (x, y).

Returns check->result.
*/
int housingCheckBed(int x, int y, struct HousingCheck *check);

/* housingMessage
Returns Terraria's status message for a check result.

bed: Use the bedroom messages instead of the housing ones.
*/
const char *housingMessage(const struct HousingCheck *check, bool bed);

/* housingQuery
The housing query: checks the room at the cursor, shows the status message
and highlights the evaluated room for a moment. A valid room without a
banner gets one at its home tile, so a homeless NPC can move in.
*/
void housingQuery(Coords cursor);

/* housingMarkerPosition
Banner/NPC position for a house whose home tile is `home`.
*/
Coords housingMarkerPosition(Coords home);

/* housingTryAutoFind
Terraria-style automatic house finding: samples one random tile around the
player and, if it is an empty tile in front of a safe wall, checks the room
around it. Returns true if a new house marker was created.
*/
bool housingTryAutoFind();

/* Room highlight shown after a housing query */
extern int housingHighlightTicks;
extern int housingLastResult;
extern Coords housingLastFail;

/* housingInLastRoom
Whether a window-local tile belonged to the room of the last check.
*/
bool housingInLastRoom(int x, int y);

/* housingShift
Keeps the last checked room aligned when the chunk window moves.
*/
void housingShift();
