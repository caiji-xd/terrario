#pragma once

/*
----- MAP -----

The full screen map (OPTN). Shows the loaded part of the world at one pixel
per tile: blocks dark, background walls in a light dither, air white, like a
4-shade take on Terraria's map where walls are drawn in their own, dimmer
colours behind blocks.
*/

/* mapShade
Map shade (a gint color, or -1 for a 50% dither) of a window-local tile.
*/
int mapShade(int x, int y);

/* mapMenu
Opens the map. Arrows scroll, F1 toggles walls, EXIT/OPTN closes.
*/
void mapMenu();
