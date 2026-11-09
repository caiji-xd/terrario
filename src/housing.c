#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gint/defs/attributes.h>

#include "housing.h"
#include "world.h"
#include "wall.h"
#include "npc.h"
#include "chat.h"
#include "stream.h"

/* Flood fill state, in on-chip memory: one bit per window tile and a queue of
   room tiles. Housing checks never call the OS, so XYRAM is safe here. */
static GYRAM uint8_t visited[WINDOW_WIDTH * WINDOW_HEIGHT / 8];
static GYRAM uint16_t queue[HOUSE_MAX_TILES];

int housingHighlightTicks = 0;
int housingLastResult = HOUSING_OK;
Coords housingLastFail = {-1, -1};

static inline bool isVisited(int x, int y)
{
	int i = y * WINDOW_WIDTH + x;
	return visited[i >> 3] & (1 << (i & 7));
}

static inline void markVisited(int x, int y)
{
	int i = y * WINDOW_WIDTH + x;
	visited[i >> 3] |= 1 << (i & 7);
}

bool housingInLastRoom(int x, int y)
{
	if((unsigned)x >= WINDOW_WIDTH || (unsigned)y >= WINDOW_HEIGHT) return false;
	return isVisited(x, y);
}

void housingShift()
{
//	The visited bitmap is in window coordinates; drop it when the window moves
	housingHighlightTicks = 0;
	memset(visited, 0, sizeof(visited));
}

/* "Solid tiles": blocks and platforms (Terraria's tileSolid). Work benches and
   anvils are only solid on top, so they are not. */
static bool isSolidTile(int x, int y)
{
	int id = getTile(x, y).id;
	enum PhysicsTypes physics = tiles[id].physics;
	return physics == PHYS_SOLID || physics == PHYS_SAND || id == TILE_PLATFORM;
}

static bool isDoorFrame(int id)
{
//	"only the tiles occupied when the doors are closed will count into the
//	frame": the closed door, or the hinge column of an open one
	return id == TILE_DOOR_C || id == TILE_DOOR_O_L_R || id == TILE_DOOR_O_R_L;
}

/* Frame: "solid blocks or platforms that are not actuated, Trap Doors, doors,
   or Tall Gates" */
static bool isFrame(int x, int y)
{
	return isSolidTile(x, y) || isDoorFrame(getTile(x, y).id);
}

static bool hasAnyTile(int x, int y)
{
	int id = getTile(x, y).id;
	return id != TILE_NOTHING && id != TILE_WATER;
}

/* Rooms are kept 2 tiles from the window edge, so the cross check never has
   to look at unloaded tiles (the window stands in for the world edges) */
static bool inRoomArea(int x, int y)
{
	return x >= HOUSE_CROSS_ARM && y >= HOUSE_CROSS_ARM
		&& x < WINDOW_WIDTH - HOUSE_CROSS_ARM && y < WINDOW_HEIGHT - HOUSE_CROSS_ARM;
}

/* "There must be at least one tile occupied by a solid tile or a safe wall
   within the two tiles directly above/below/at the left side/at the right
   side of each tile". sawWall tells whether an unsafe wall was seen. */
static bool crossCheck(int x, int y, bool *sawWall)
{
	for(int d = -HOUSE_CROSS_ARM; d <= HOUSE_CROSS_ARM; d++)
	{
		if(isSolidTile(x + d, y) || wallIsSafe(x + d, y)) return true;
		if(isSolidTile(x, y + d) || wallIsSafe(x, y + d)) return true;
		if(getWall(x + d, y) != WALL_NONE || getWall(x, y + d) != WALL_NONE) *sawWall = true;
	}
	return false;
}

static void includeTile(struct HousingCheck *check, int x, int y)
{
	check->tiles++;
	if(x < check->x1) check->x1 = x;
	if(x > check->x2) check->x2 = x;
	if(y < check->y1) check->y1 = y;
	if(y > check->y2) check->y2 = y;
}

static int fail(struct HousingCheck *check, int result)
{
	check->result = result;
	return result;
}

/* Phase 1. Returns HOUSING_OK if the frame is fine. */
static int checkFrame(Coords start, struct HousingCheck *check)
{
	static const int8_t deltas[8][2] = {
		{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
	};
	int head = 0, tail = 0;
	bool sawWall = false;

	memset(visited, 0, sizeof(visited));
	*check = (struct HousingCheck){
		.result = HOUSING_OK,
		.x1 = start.x, .x2 = start.x, .y1 = start.y, .y2 = start.y,
		.marker = -1,
		.home = {-1, -1}
	};

	if(!inRoomArea(start.x, start.y)) return fail(check, HOUSING_EDGE);
	if(isSolidTile(start.x, start.y)) return fail(check, HOUSING_SOLID_START);

	markVisited(start.x, start.y);
	queue[tail++] = start.y * WINDOW_WIDTH + start.x;

	while(head < tail)
	{
		int x = queue[head] % WINDOW_WIDTH, y = queue[head] / WINDOW_WIDTH;
		head++;

		includeTile(check, x, y);
		if(!crossCheck(x, y, &sawWall))
		{
			check->failAt = (Coords){x, y};
			return fail(check, sawWall ? HOUSING_UNSAFE_WALLS : HOUSING_HOLE);
		}

		for(int i = 0; i < 8; i++)
		{
			int nx = x + deltas[i][0], ny = y + deltas[i][1];

			if(!inRoomArea(nx, ny)) return fail(check, HOUSING_EDGE);
			if(isVisited(nx, ny)) continue;
			markVisited(nx, ny);

			if(isFrame(nx, ny))
			{
				includeTile(check, nx, ny);
				if(!crossCheck(nx, ny, &sawWall))
				{
					check->failAt = (Coords){nx, ny};
					return fail(check, sawWall ? HOUSING_UNSAFE_WALLS : HOUSING_HOLE);
				}
			}
			else
			{
				if(tail >= HOUSE_MAX_TILES) return fail(check, HOUSING_TOO_BIG);
				queue[tail++] = ny * WINDOW_WIDTH + nx;
			}
		}

		if(check->tiles >= HOUSE_MAX_TILES
		|| check->x2 - check->x1 + 1 > HOUSE_MAX_SIZE
		|| check->y2 - check->y1 + 1 > HOUSE_MAX_SIZE) return fail(check, HOUSING_TOO_BIG);
	}

	if(check->tiles < HOUSE_MIN_TILES) return fail(check, HOUSING_TOO_SMALL);
	return HOUSING_OK;
}

/* Phase 2: one of each furniture category (wiki, House: Furniture) */
static int checkFurniture(struct HousingCheck *check)
{
	int found = 0;

	for(int y = check->y1; y <= check->y2; y++)
	{
		for(int x = check->x1; x <= check->x2; x++)
		{
			if(!isVisited(x, y)) continue;
			switch(getTile(x, y).id)
			{
				case TILE_TORCH:
					found |= NEED_LIGHT;
					break;

				case TILE_WBENCH_L: case TILE_WBENCH_R:
					found |= NEED_TABLE;
					break;

				case TILE_CHAIR_L: case TILE_CHAIR_R:
				case TILE_BED_EDGE: case TILE_BED_MID:
					found |= NEED_CHAIR;
					break;

				case TILE_DOOR_C:
				case TILE_DOOR_O_L_L: case TILE_DOOR_O_L_R:
				case TILE_DOOR_O_R_L: case TILE_DOOR_O_R_R:
				case TILE_PLATFORM:
					found |= NEED_DOOR;
					break;

				default:
					break;
			}
		}
	}

	check->missing = (NEED_LIGHT | NEED_DOOR | NEED_TABLE | NEED_CHAIR) & ~found;
	if(check->missing) return fail(check, HOUSING_MISSING_FURNITURE);
	return HOUSING_OK;
}

/* Home tile score of a candidate floor tile, or 0 if it can't be one (wiki,
   House: Home tile score). The evil score doesn't exist in this game. */
static int homeTileScore(int x, int y)
{
	int score = HOME_TILE_BASE_SCORE;

	if(!isSolidTile(x, y) || !isSolidTile(x - 1, y) || !isSolidTile(x + 1, y)) return 0;
	for(int dy = 1; dy <= 3; dy++)
	{
//		Nothing that blocks entities in the 3x3 area above (platforms are fine)
		for(int dx = -1; dx <= 1; dx++)
		{
			enum PhysicsTypes physics = tiles[getTile(x + dx, y - dy).id].physics;
			if(physics == PHYS_SOLID || physics == PHYS_SAND) return 0;
		}
//		The 3 tiles right above: not solid, and inside the house
		if(isSolidTile(x, y - dy) || !isVisited(x, y - dy)) return 0;
	}

//	First calculation: the 5x4 area above, without its middle column
	for(int dy = 1; dy <= 4; dy++)
	{
		for(int dx = -2; dx <= 2; dx++)
		{
			int id = getTile(x + dx, y - dy).id;
			if(dx == 0) continue;
			if(isDoorFrame(id)) score -= 20;
			else if(isSolidTile(x + dx, y - dy)) score -= 5;
			else score += 5;
		}
	}
	if(score <= 0) return 0;

//	Second calculation: chests around, then anything in the middle column
	for(int dy = 1; dy <= 4; dy++)
	{
		for(int dx = -2; dx <= 2; dx++)
		{
			int id = getTile(x + dx, y - dy).id;
			if(dx != 0 && (id == TILE_CHEST_L || id == TILE_CHEST_R)) score -= 30;
		}
	}
	if(score < 1) score = 1;
	for(int dy = 1; dy <= 4; dy++)
	{
		if(hasAnyTile(x, y - dy)) score -= 15;
	}
	if(score < 1) score = 1;
	return score;
}

/* Phase 3 */
static int checkProperties(struct HousingCheck *check, int ignoreMarker)
{
	for(int i = 0; i < world.numMarkers; i++)
	{
		Coords p = world.markers[i].position;
		if(i == ignoreMarker || !housingInLastRoom(p.x, p.y)) continue;
		if(world.markers[i].occupant != -1) return fail(check, HOUSING_OCCUPIED);
		check->marker = i;
	}

//	Candidates from X1+1 to X2-1 and Y1+2 to Y2, top to bottom and left to
//	right; ties keep the first one
	check->homeScore = 0;
	for(int y = check->y1 + 2; y <= check->y2; y++)
	{
		for(int x = check->x1 + 1; x <= check->x2 - 1; x++)
		{
			int score;
			if(!isVisited(x, y)) continue;
			score = homeTileScore(x, y);
			if(score > check->homeScore)
			{
				check->homeScore = score;
				check->home = (Coords){x, y};
			}
		}
	}
	if(check->homeScore <= 0) return fail(check, HOUSING_NO_STANDING_SPACE);
	return HOUSING_OK;
}

int housingCheckRoom(Coords start, struct HousingCheck *check, int ignoreMarker)
{
	housingHighlightTicks = 0;
	if(checkFrame(start, check) != HOUSING_OK) return check->result;
	if(checkFurniture(check) != HOUSING_OK) return check->result;
	return checkProperties(check, ignoreMarker);
}

int housingCheckBed(int x, int y, struct HousingCheck *check)
{
	housingHighlightTicks = 0;
	*check = (struct HousingCheck){ .result = HOUSING_OK, .marker = -1 };

//	"No solid blocks above the 3 tiles at the head of the Bed (platforms are
//	fine)". This bed is 3 tiles wide; all 3 are checked.
	for(int dx = -1; dx <= 1; dx++)
	{
		enum PhysicsTypes physics = tiles[getTile(x + dx, y - 1).id].physics;
		if(physics == PHYS_SOLID || physics == PHYS_SAND) return fail(check, HOUSING_BED_OBSTRUCTED);
	}
//	The room is evaluated from the bed itself (beds are not solid)
	return checkFrame((Coords){x, y}, check);
}

const char *housingMessage(const struct HousingCheck *check, bool bed)
{
	static char buffer[72];
	const char *parts[4];
	int n = 0;

	switch(check->result)
	{
		case HOUSING_OK:
			return bed ? "Spawn point set!" : "This housing is suitable.";
		case HOUSING_SOLID_START:
			return bed ? "This is not a valid room for a bed." : "This is a solid block!";
		case HOUSING_HOLE:
			return bed ? "Your bedroom is missing a wall!" : "This room is missing a wall";
		case HOUSING_UNSAFE_WALLS:
			return bed ? "Natural walls in this bedroom need replacing!" : "Natural walls in this room need replacing!";
		case HOUSING_TOO_SMALL:
			return bed ? "Your bedroom is too small!" : "This room is too small";
		case HOUSING_TOO_BIG:
			return bed ? "Your bedroom is too big!" : "This room is too big";
		case HOUSING_OCCUPIED:
			return "This housing is already occupied.";
		case HOUSING_NO_STANDING_SPACE:
			return "This room has no space to stand in!";
		case HOUSING_BED_OBSTRUCTED:
			return "Your bed is obstructed.";

		case HOUSING_MISSING_FURNITURE:
			if(check->missing & NEED_LIGHT) parts[n++] = "a light source";
			if(check->missing & NEED_DOOR) parts[n++] = "a door";
			if(check->missing & NEED_TABLE) parts[n++] = "a table";
			if(check->missing & NEED_CHAIR) parts[n++] = "a chair";
//			Game.HouseMissing_1 to _4
			if(n == 1) snprintf(buffer, sizeof(buffer), "This house is missing %s.", parts[0]);
			else if(n == 2) snprintf(buffer, sizeof(buffer), "This house is missing %s and %s.", parts[0], parts[1]);
			else if(n == 3) snprintf(buffer, sizeof(buffer), "This house is missing %s, %s, and %s.", parts[0], parts[1], parts[2]);
			else snprintf(buffer, sizeof(buffer), "This house is missing %s, %s, %s, and %s.", parts[0], parts[1], parts[2], parts[3]);
			return buffer;

		case HOUSING_EDGE:
		default:
			return bed ? "This is not a valid room for a bed." : "This is not valid housing.";
	}
}

Coords housingMarkerPosition(Coords home)
{
//	NPCs are 3 tiles tall; their top left corner goes 3 tiles above the floor
	return (Coords){home.x, home.y - 3};
}

void housingQuery(Coords cursor)
{
	struct HousingCheck check;

	housingCheckRoom(cursor, &check, -1);
	setStatusMessage(housingMessage(&check, false));
	housingLastResult = check.result;
	housingLastFail = (check.result == HOUSING_HOLE || check.result == HOUSING_UNSAFE_WALLS) ? check.failAt : (Coords){-1, -1};
//	Desktop 1.4.5.0: "The housing query now gives visual feedback on the
//	suitability of any queried space."
	housingHighlightTicks = 120;
	if(check.result == HOUSING_OK && check.marker == -1) addMarker(housingMarkerPosition(check.home));
}

bool housingTryAutoFind()
{
	struct HousingCheck check;
	int px = (player.props.x + (player.props.width >> 1)) >> 3;
	int py = (player.props.y + (player.props.height >> 1)) >> 3;
	int x = px + rand() % 81 - 40;
	int y = py + rand() % 49 - 24;

//	Don't overwrite the room shown by a housing query
	if(housingHighlightTicks > 0) return false;

//	Only empty tiles in front of a safe wall start a check, so rooms with more
//	open wall area are found sooner (wiki, House: Notes)
	if(getTile(x, y).id != TILE_NOTHING || !wallIsSafe(x, y)) return false;
	if(housingCheckRoom((Coords){x, y}, &check, -1) != HOUSING_OK || check.marker != -1) return false;
	addMarker(housingMarkerPosition(check.home));
	return true;
}
