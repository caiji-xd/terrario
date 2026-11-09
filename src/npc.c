#include <stdlib.h>
#include <string.h>

#include <gint/gray.h>
#include <gint/defs/util.h>
#include <gint/keyboard.h>
#include <gint/gint.h>
#include <gint/defs/attributes.h>

#include "npc.h"
#include "world.h"
#include "render.h"
#include "stream.h"
#include "housing.h"

/* NPCs outside the chunk window are frozen until the player comes back */
static bool npcInWindow(const NPC *npc)
{
	return npc->props.x >= 0 && npc->props.y >= 0
		&& npc->props.x + npc->props.width < game.WORLD_WIDTH * 8
		&& npc->props.y + npc->props.height < game.WORLD_HEIGHT * 8;
}

char *guideDialogue[] = {
	"Greetings, player. Is there something I can help you with?",
	"I am here to give you advice on what to do next. It is recommended that you talk with me anytime you get stuck.",
	"They say there is a person who will tell you how to survive in this land... oh wait. That's me."
};

char *guideHelpDialogue[] = {
	"Underground exploration can yield valuable treasures!",
	"A house can be a useful refuge from the beasts that roam at night.",
	"Ropes can be used to traverse pits!",
	"You can use [TAN] to mark a valid house as suitable for NPCs!"
};

char *nurseDialogue[] = {
	"Turn your head and cough.",
	"That's not the biggest I've ever seen... Yes, I've seen bigger wounds for sure.",
	"Would you like a lollipop?",
	"Show me where it hurts."
};

char *nurseHealDialogue[] = {
	"That didn't hurt too bad, now did it?",
	"All better. I don't want to see you jumping off anymore cliffs.",
	"That's probably going to leave a scar.",
	"I managed to sew your face back on. Be more careful next time.",
	"I don't give happy endings.",
	"I can't do anymore for you without plastic surgery.",
	"Quit wasting my time."
};

void guideMenu()
{
	while(npcTalk(sizeof(guideHelpDialogue) / sizeof(char*), guideHelpDialogue, MENU_GUIDE));
}

void nurseMenu()
{
	bool cont;
	char **dialogue;
	int numDialogue;
	do
	{
		if(player.combat.health < player.maxHealth)
		{
			numDialogue = 1;
			if(player.combat.health >= 0.75 * player.maxHealth) dialogue = &nurseHealDialogue[0];
			else if(player.combat.health >= 0.5 * player.maxHealth) dialogue = &nurseHealDialogue[1];
			else if(player.combat.health >= 0.25 * player.maxHealth) dialogue = &nurseHealDialogue[2];
			else dialogue = &nurseHealDialogue[3];
			player.combat.health = player.maxHealth;
		}
		else
		{
			numDialogue = 3;
			dialogue = &nurseHealDialogue[4];
		}

		cont = npcTalk(numDialogue, dialogue, MENU_NURSE);
	}
	while(cont);
}

bool isNPCAlive(enum NPCs id)
{
	for(int i = 0; i < world.numNPCs; i++)
	{
		if(world.npcs[i].id == id) return true;
	}

	return false;
}

void addNPC(enum NPCs id)
{
	extern bopti_image_t img_npcs_guide, img_npcs_head_guide, img_npcs_nurse, img_npcs_head_nurse;
	NPC *npc;

	if(id <= NPC_NONE || id > NPC_NURSE || world.numNPCs >= MAX_NPCS || isNPCAlive(id)) return;
	NPC *resized = realloc(world.npcs, (world.numNPCs + 1) * sizeof(NPC));
	allocCheck(resized);
	world.npcs = resized;
	world.numNPCs++;

	npc = &world.npcs[world.numNPCs - 1];

	switch(id)
	{
		case NPC_GUIDE:
			*npc = (NPC) {
				.sprite = &img_npcs_guide,
				.head = &img_npcs_head_guide,
				.props = {
					.width = 16,
					.height = 23
				},
				.numInteractDialogue = sizeof(guideDialogue) / sizeof(char*),
				.interactDialogue = guideDialogue,
				.menu = &guideMenu,
				.menuType = MENU_GUIDE
			};
			break;
		
		case NPC_NURSE:
			*npc = (NPC) {
				.sprite = &img_npcs_nurse,
				.head = &img_npcs_head_nurse,
				.props = {
					.width = 14,
					.height = 23
				},
				.numInteractDialogue = sizeof(nurseDialogue) / sizeof(char*),
				.interactDialogue = nurseDialogue,
				.menu = &nurseMenu,
				.menuType = MENU_NURSE
			};
			break;

//		Something's gone wrong!
		default:
			dclear(C_WHITE);
			dprint(0, 0, C_BLACK, "Invalid NPC %d", id);
			dupdate();
			while(1){getkey();}
			break;
	}

	npc->marker = -1;
	npc->id = id;

//	New NPCs arrive at the world spawn point, which may be outside the window
	npc->props.x = player.spawn.x - stream.originX * 8;
	npc->props.y = player.spawn.y - stream.originY * 8 + player.props.height - npc->props.height;
}

void doNPCHouseCheck()
{
	NPC *npc;
	HouseMarker *marker;
	bool spawnNPC = false;
	enum NPCs npcToAdd;

	if(!isNPCAlive(NPC_GUIDE))
	{
		spawnNPC = true;
		npcToAdd = NPC_GUIDE;
	}
	else if(!isNPCAlive(NPC_NURSE) && player.combat.health > 100)
	{
		spawnNPC = true;
		npcToAdd = NPC_NURSE;
	}

	if(!spawnNPC) return;

//	"NPCs that can live in houses will spawn after their specific spawning
//	conditions were met and there is an empty valid house" and a killed NPC
//	"will not respawn until a new house becomes available" (wiki, NPCs). The
//	Guide's very first appearance doesn't need one; that happens when the
//	world is created (main.c).
	for(int i = 0; i < world.numMarkers; i++)
	{
		marker = &world.markers[i];
		if(marker->occupant != -1) continue;
//		Only houses that are loaded can be re-checked before moving in
		if(marker->position.x <= 0 || marker->position.x >= game.WORLD_WIDTH - 1
		|| marker->position.y <= 0 || marker->position.y >= game.WORLD_HEIGHT - 1) continue;
		{
			Coords position = marker->position;
			bool valid;

			marker->position = (Coords){-1, -1};
			valid = checkHousingValid(position);
			marker = &world.markers[i];
			marker->position = position;
			if(!valid) continue;
		}

		addNPC(npcToAdd);
		npc = &world.npcs[world.numNPCs - 1];
		marker->occupant = world.numNPCs - 1;
		npc->marker = i;
		npc->props.x = marker->position.x << 3;
		npc->props.y = marker->position.y << 3;
		return;
	}
}

void npcUpdate(int frames)
{
	NPC *npc;
	HouseMarker *marker;
	bool distancedFromPlayer;
	bool distancedFromMarker;
	int walkMaxFrame;

	if(isDay() && frames % 1200 == 1199) doNPCHouseCheck();

	for(int idx = 0; idx < world.numNPCs; idx++)
	{
		npc = &world.npcs[idx];

		switch(npc->id)
		{
			case NPC_GUIDE:
				walkMaxFrame = 15;
				break;
			
			case NPC_NURSE:
				walkMaxFrame = 12;
				break;
			
			default:
				walkMaxFrame = 0;
				break;
		}

//		Housing
		if(frames % 300 == 0)
		{
			if(npc->marker == -1)
			{
				if(isDay())
				{
					for(int i = 0; i < world.numMarkers; i++)
					{
						marker = &world.markers[i];
						if(marker->occupant == -1)
						{
							marker->occupant = idx;
							npc->marker = i;
							break;
						}
					}
				}
			}
			else if(!isDay())
			{
				distancedFromPlayer = abs(npc->props.x - player.props.x) > SCREEN_WIDTH || abs(npc->props.y - player.props.y) > SCREEN_HEIGHT;
				distancedFromMarker = abs((world.markers[npc->marker].position.x << 3) - player.props.x) > (SCREEN_WIDTH >> 1) + 16 || abs((world.markers[npc->marker].position.y << 3) - player.props.y) > (SCREEN_HEIGHT >> 1) + 20;
				if(distancedFromPlayer && distancedFromMarker)
				{
					npc->props.x = (world.markers[npc->marker].position.x << 3);
					npc->props.y = (world.markers[npc->marker].position.y << 3);
				}
			}
		}

//		Physics and movement

		if(!npcInWindow(npc)) continue;
		handlePhysics(&npc->props, frames, false, WATER_FRICTION);

		if(npc->props.movingSelf)
		{
			if(rand() % 50 == 0) npc->props.movingSelf = false;
		}
		else if(rand() % 300 == 0)
		{
			npc->props.movingSelf = true;
			npc->props.xVel = (rand() % 2) ? -0.3 : 0.3;
			npc->anim.direction = npc->props.xVel < 0;
		}

//		Animation

		if(npc->props.yVel != 0)
		{
			npc->anim.animation = 1;
			npc->anim.animationFrame = 1;
		}
		else if(npc->props.xVel != 0)
		{
			if(npc->anim.animation != 2)
			{
				npc->anim.animation = 2;
				npc->anim.animationFrame = 2;
			}
		}
		else
		{
			npc->anim.animation = 0;
			npc->anim.animationFrame = 0;
		}

//		Walking animation is the only one with multiple frames
		if(frames & 1 && npc->anim.animation == 2)
		{
			npc->anim.animationFrame++;
			if(npc->anim.animationFrame > walkMaxFrame) npc->anim.animationFrame = 2;
		}
	}
}

bool npcTalk(int numDialogue, char **dialogue, enum MenuTypes type)
{
	char buffer[33];
	extern bopti_image_t img_ui_npctalk;
	key_event_t key;
	int selectedLine = rand() % numDialogue;
	int lines = strlen(dialogue[selectedLine]) / 32 + 1;

	buffer[32] = '\0';

	render(false);

	drect_border(0, 0, 127, 7 * lines + 1, C_WHITE, 1, C_BLACK);

	for(int line = 0; line < lines; line++)
	{
		strncpy(buffer, (char *)(dialogue[selectedLine] + 32 * line), 32);
		dtext(2, line * 7 + 2, C_BLACK, buffer);
	}

	switch(type)
	{
		case MENU_GUIDE:
			dsubimage(0, 7 * lines + 2, &img_ui_npctalk, 17, 0, 17, 7, DIMAGE_NONE);
			break;
		
		case MENU_SHOP:
			dsubimage(0, 7 * lines + 2, &img_ui_npctalk, 0, 0, 17, 7, DIMAGE_NONE);
			break;
		
		case MENU_NURSE:
			dsubimage(0, 7 * lines + 2, &img_ui_npctalk, 52, 0, 17, 7, DIMAGE_NONE);
			break;
	}
	dsubimage(18, 7 * lines + 2, &img_ui_npctalk, 34, 0, 17, 7, DIMAGE_NONE);

	dupdate();

	while(true)
	{
		key = getkey_opt(GETKEY_NONE, NULL);
		
		switch(key.key)
		{
			case KEY_F1:
				return true;

			case KEY_F2:
				return false;

			default:
				break;
		}
	}
}

/* Re-checks a house marker with the Terraria housing rules (housing.c). The
   marker sits 3 tiles above its home tile, inside the room. */
bool checkHousingValid(Coords position)
{
	struct HousingCheck check;

	return housingCheckRoom(position, &check, -1) == HOUSING_OK;
}

void addMarker(Coords position)
{
	if(world.numMarkers >= MAX_HOUSE_MARKERS) return;
	HouseMarker *resized = realloc(world.markers, (world.numMarkers + 1) * sizeof(HouseMarker));
	allocCheck(resized);
	world.markers = resized;
	world.numMarkers++;
	world.markers[world.numMarkers - 1] = (HouseMarker) {
		.position = position,
		.occupant = -1
	};
}

bool removeMarker(int idx)
{
	if(idx < 0 || idx >= world.numMarkers) return false;
	HouseMarker *marker = &world.markers[idx];

	if(marker->occupant != -1) world.npcs[marker->occupant].marker = -1;
//	Move the last marker into the hole, unless this is the last one: copying
//	it onto itself used to re-link the evicted NPC to the removed house
	if(idx != world.numMarkers - 1)
	{
		*marker = world.markers[world.numMarkers - 1];
		if(marker->occupant != -1) world.npcs[marker->occupant].marker = idx;
	}
	world.numMarkers--;
	if(world.numMarkers == 0) { free(world.markers); world.markers = NULL; }
	/* Keep bounded capacity on removal, avoiding realloc churn. */

	return true;
}

void updateMarkerChecks(Coords position)
{
	HouseMarker *marker;

	for(int idx = 0; idx < world.numMarkers; idx++)
	{
		marker = &world.markers[idx];
		if(abs(marker->position.x - position.x) < MARKER_CHECK_DISTANCE && abs(marker->position.y - position.y) < MARKER_CHECK_DISTANCE)
		{
			marker->doCheck = true;
		}
	}
}

void doMarkerChecks()
{
	HouseMarker *marker;
	Coords save;

	for(int idx = 0; idx < world.numMarkers; idx++)
	{
		marker = &world.markers[idx];
//		Can't check a house that isn't loaded; try again when it is
		if(marker->doCheck
		&& marker->position.x > 0 && marker->position.x < game.WORLD_WIDTH - 1
		&& marker->position.y > 0 && marker->position.y < game.WORLD_HEIGHT - 1)
		{
			save = marker->position;
			marker->position = (Coords){-1, -1};
			if(!checkHousingValid(save))
			{
				removeMarker(idx);
//				Last marker will have been moved to current index
				idx--;
			}
			else
			{
				marker->doCheck = false;
				marker->position = save;
			}
		}
	}
}