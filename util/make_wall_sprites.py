#!/usr/bin/env python3
"""
make_wall_sprites.py - draws every background wall graphic of Terrario.

All the art made here is drawn procedurally by this script (no Terraria
sprite is used or traced). It is released with the rest of the image assets
under CC BY 4.0, see docs/WALLS.md for the asset list.

Usage (from the repository root, needs Pillow):
	python3 util/make_wall_sprites.py           # writes the PNG assets
	python3 util/make_wall_sprites.py --preview out.png   # also renders a test scene

Outputs:
	assets-fx/img/walls/<material>.png    16 connection states x 3 variants
	assets-fx/img/swing_<metal>_hammer.png swing frames, same layout as picks
	assets-fx/img/items.png               icons for the new items (patched in)

Wall sheet layout (matches render.c):
	frame size 10x10 = 8x8 tile + 1 pixel of overhang on every side
	frame (state, variant) at x = 1 + 11 * state, y = 1 + 11 * variant
	state bits: 1 = wall on the left, 2 = above, 4 = right, 8 = below
"""

import os
import sys
from PIL import Image

# The four gray shades of the fx gray engine, exactly as fxconv expects them
W = (255, 255, 255, 255)
L = (170, 170, 170, 255)
D = (85, 85, 85, 255)
B = (0, 0, 0, 255)
T = (0, 0, 0, 0)

FRAME = 10
PITCH = 11
STATES = 16
VARIANTS = 3

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
IMG = os.path.join(ROOT, "assets-fx", "img")


def h32(*values):
	"""Small deterministic hash, so the assets are reproducible."""
	v = 0x811C9DC5
	for value in values:
		v ^= value & 0xFFFFFFFF
		v = (v * 0x01000193) & 0xFFFFFFFF
		v ^= v >> 15
		v = (v * 0x2C1B3C6D) & 0xFFFFFFFF
		v ^= v >> 12
	return v


# ---------------------------------------------------------------------------
# Interior textures. (x, y) are tile pixel coordinates 0..7; patterns that
# have structure (boards, bricks) repeat every 8 pixels so that neighbouring
# walls join without a seam.
# ---------------------------------------------------------------------------

# Walls are drawn one shade darker than the matching blocks (dirt blocks are
# LIGHT/WHITE, dirt walls DARK/LIGHT...), the gray screen equivalent of the
# darker, low contrast wall textures of Terraria.

def tex_dirt(x, y, v):
	r = h32(1, x, y, v) % 100
	if r < 16: return L
	if r < 22: return B
	return D


def tex_stone(x, y, v):
	# Cobbles: rounded dark stones with black gaps and a light glint each
	cx, cy = (x + (y // 4) * 2 + v) % 4, y % 4
	if cy == 3 or cx == 3: return B
	if cx == 0 and cy == 0 and h32(2, x // 4, y // 4, v) % 3: return L
	return D


def tex_wood(x, y, v):
	# Horizontal boards with a dark seam, a lit top edge and one joint per board
	if y % 4 == 3: return B
	board = y // 4
	if x == (v * 3 + board * 5 + 2) % 8: return B
	if y % 4 == 0: return L if (x + v) % 3 else D
	return D


def tex_planked(x, y, v):
	# Wooden planks alternating with strips of stone
	if y in (3, 7): return B
	if y < 3:
		if x == (v * 3 + 1) % 8: return B
		return L if y == 0 and (x + v) % 2 else D
	return L if (x + y + v) % 3 == 0 else D


def tex_glass(x, y, v):
	# Mostly see-through. A short diagonal glint, and a sparse ordered dither
	# that reads as a faint tint on the gray screen.
	g = (v * 3 + 1) % 6
	if x + y == g + 2 and 1 <= x <= 5: return W
	if (x + 2 * y + v) % 8 == 0 and (x * y) % 2 == 1: return L
	return T


def tex_mud(x, y, v):
	r = h32(6, x, y, v) % 100
	if r < 38: return B
	if r < 42: return L
	return D


def tex_jungle(x, y, v):
	# Leafy blobs on a black background
	r = h32(7, x // 2, y // 2, v) % 100
	if r < 45 and h32(8, x, y, v) % 100 < 85: return L if h32(9, x, y, v) % 100 < 20 else D
	return B


# Edge styles: "rough" outlines are ragged like natural walls, "straight" ones
# are clean like crafted walls, "glass" has a light inner rim.
MATERIALS = {
#	name		texture			edge		outline
	"dirt":		(tex_dirt,		"rough",	B),
	"stone":	(tex_stone,		"straight",	B),
	"wood":		(tex_wood,		"straight",	B),
	"planked":	(tex_planked,	"straight",	B),
	"glass":	(tex_glass,		"glass",	D),
	"mud":		(tex_mud,		"rough",	B),
	"jungle":	(tex_jungle,	"rough",	D),
}


def lip_depth(edge, side, pos, variant, name):
	"""How far the wall reaches past its tile on an open side: 1 = one pixel
	past the tile, 0 = up to the tile edge, -1 = eaten one pixel into it."""
	if edge != "rough": return 1
	r = h32(10, side, pos, variant, len(name)) % 10
	return -1 if r < 2 else (0 if r < 7 else 1)


def wall_frame(name, state, variant):
	tex, edge, outline = MATERIALS[name]
	img = Image.new("RGBA", (FRAME, FRAME), T)
	left, up, right, down = state & 1, state & 2, state & 4, state & 8

	for fy in range(FRAME):
		for fx in range(FRAME):
			cx, cy = fx - 1, fy - 1		# tile coordinates, -1..8

#			Overhang beyond a connected side belongs to the neighbour
			if (cx < 0 and left) or (cx > 7 and right) or (cy < 0 and up) or (cy > 7 and down):
				continue

#			Distance past the edge of each open side (0 = the tile's own edge pixel)
			dx = dy = -99
			lx = ly = 1
			if not left: dx, lx = max(dx, -cx), lip_depth(edge, 0, cy, variant, name)
			if not right and cx - 7 > dx: dx, lx = cx - 7, lip_depth(edge, 2, cy, variant, name)
			if not up: dy, ly = max(dy, -cy), lip_depth(edge, 1, cx, variant, name)
			if not down and cy - 7 > dy: dy, ly = cy - 7, lip_depth(edge, 3, cx, variant, name)

			if dx >= 0 and dy >= 0:
				d, lip = dx + dy, min(lx, ly)		# chamfered corner
			elif dx >= dy:
				d, lip = dx, lx
			else:
				d, lip = dy, ly

			if d > lip: continue
			if d == lip and d > -99:
				img.putpixel((fx, fy), outline)
			elif edge == "glass" and d == lip - 1:
				img.putpixel((fx, fy), L)			# light inner rim
			elif 0 <= cx <= 7 and 0 <= cy <= 7:
				img.putpixel((fx, fy), tex(cx, cy, variant))
	return img


def wall_sheet(name):
	sheet = Image.new("RGBA", (1 + PITCH * STATES, 1 + PITCH * VARIANTS), T)
	for variant in range(VARIANTS):
		for state in range(STATES):
			sheet.paste(wall_frame(name, state, variant), (1 + PITCH * state, 1 + PITCH * variant))
	return sheet


# ---------------------------------------------------------------------------
# Hammers
# ---------------------------------------------------------------------------

HAMMERS = {
#	name		head fill	head light
	"wooden":	(D,			L),
	"copper":	(L,			W),
	"tin":		(L,			L),
	"iron":		(D,			W),
}


def hammer_right(name):
	"""Canonical swing frame pointing right: handle from the grip on the left,
	head standing up at the right end."""
	fill, light = HAMMERS[name]
	img = Image.new("RGBA", (16, 16), T)
	for x in range(0, 11):
		img.putpixel((x, 7), D)
		img.putpixel((x, 8), L if x % 3 else D)
	for y in range(3, 13):
		for x in range(10, 15):
			border = x in (10, 14) or y in (3, 12)
			if border: c = B
			elif x == 11: c = light
			else: c = fill
			img.putpixel((x, y), c)
	return img


def rotate(img, quarter_turns=0, diagonal=0):
	"""Rotates a 16x16 frame by quarter turns (counter clockwise), or by +/-45
	degrees with nearest neighbour sampling around the grip."""
	if quarter_turns:
		return img.rotate(90 * quarter_turns)
	out = Image.new("RGBA", (16, 16), T)
	src = img.load()
	import math
	a = math.radians(45 * diagonal)
	cx, cy = 7.5, 7.5
	for y in range(16):
		for x in range(16):
			# Inverse rotation of the destination pixel centre
			dx, dy = x - cx, y - cy
			sx = dx * math.cos(a) - dy * math.sin(a) + cx
			sy = dx * math.sin(a) + dy * math.cos(a) + cy
			ix, iy = int(round(sx)), int(round(sy))
			if 0 <= ix < 16 and 0 <= iy < 16 and src[ix, iy][3]:
				out.putpixel((x, y), src[ix, iy])
	return out


def hammer_sheet(name):
	base = hammer_right(name)
	frames = [
		rotate(base, quarter_turns=1),	# up
		rotate(base, diagonal=1),		# up and right
		base,							# right
		rotate(base, diagonal=-1),		# down and right
	]
	sheet = Image.new("RGBA", (69, 33), T)
	for i, frame in enumerate(frames):
		sheet.paste(frame, (1 + 17 * i, 0), frame)
		mirrored = frame.transpose(Image.FLIP_LEFT_RIGHT)
		sheet.paste(mirrored, (1 + 17 * i, 17), mirrored)
	return sheet


# ---------------------------------------------------------------------------
# Item icons (items.png: 16 columns, 9 pixel pitch, item id = row * 16 + col)
# ---------------------------------------------------------------------------

FIRST_NEW_ITEM = 74
NEW_ITEMS = ["hammer:wooden", "hammer:copper", "hammer:tin", "hammer:iron",
	"wall:dirt", "wall:stone", "wall:wood", "wall:planked", "wall:glass", "wall:mud"]


def hammer_icon(name):
	fill, light = HAMMERS[name]
	img = Image.new("RGBA", (8, 8), T)
	for i in range(5):
		img.putpixel((i, 7 - i), D if i % 2 else L)
	head = [(3, 1), (4, 0), (5, 1), (6, 2), (7, 3), (6, 4), (5, 5), (4, 4), (3, 3), (2, 2)]
	for p in head: img.putpixel(p, B)
	for p in [(4, 1), (5, 2), (6, 3), (3, 2), (4, 3), (5, 4), (4, 2), (5, 3)]:
		img.putpixel(p, light if p[0] == p[1] + 3 else fill)
	return img


def wall_icon(name):
	"""A small, fully surrounded patch of the wall, like Terraria's wall
	items (a wall frame with no neighbours)."""
	frame = wall_frame(name, 0, 0)
	return frame.crop((1, 1, 9, 9))


def patch_items():
	path = os.path.join(IMG, "items.png")
	sheet = Image.open(path).convert("RGBA")
	for i, item in enumerate(NEW_ITEMS):
		item_id = FIRST_NEW_ITEM + i
		x, y = (item_id & 15) * 9 + 1, (item_id >> 4) * 9 + 1
		kind, name = item.split(":")
		icon = hammer_icon(name) if kind == "hammer" else wall_icon(name)
		sheet.paste(Image.new("RGBA", (8, 8), T), (x, y))
		sheet.paste(icon, (x, y), icon)
	sheet.save(path)


# ---------------------------------------------------------------------------
# Preview: a small scene composed the way render.c draws it
# ---------------------------------------------------------------------------

def scene_preview(out_path):
	"""Renders a 32x16 tile test scene at 4x: background, walls (with the
	same connection states and draw order as render.c), then blocks."""
	tiles_dir = os.path.join(IMG, "tiles")
	block_sheets = {c: Image.open(os.path.join(tiles_dir, f + ".png")).convert("RGBA")
		for c, f in {"#": "dirt", "S": "stone", "=": "wood", "g": "grass"}.items()}
	wall_sheets = {c: wall_sheet(n) for c, n in
		{"d": "dirt", "s": "stone", "w": "wood", "p": "planked", "G": "glass", "m": "mud", "j": "jungle"}.items()}

	# Two layers of 32x16: blocks and walls
	blocks = [
		"                                ",
		"                                ",
		"   =========          gggggggggg",
		"   =       =          ##########",
		"   =       =        gg##########",
		"   =       =       g############",
		"   =       =      g#####  ######",
		"   =       =     g####     #####",
		"   =========    g####       ####",
		"ggggggggggggggggg####       ####",
		"################ ###         ###",
		"################  ##         ###",
		"######SSSSSSSSSS             ###",
		"######SSSSSSSSSSSSSSSSSSSSSSS###",
		"################################",
		"################################",
	]
	walls = [
		"                                ",
		"                                ",
		"   www  wGGw                    ",
		"   wwwwwwGGwww                  ",
		"   wwwwwwGGwww                  ",
		"   ppppppppppp       dddd ddd   ",
		"   ppp  pppppp      ddddddddddd ",
		"   ppppppppppp     ddddddddddddd",
		"   ppppppppppp    dddddddddddddd",
		"                  dddddddddddddd",
		"ddddddddddddddddddddddddddddddd ",
		"ddddddddddddddddddsssssssssssss ",
		"ddddddddddddddddddsssssssmmmmmm ",
		"ddddddssssssssssssssssssmmmmmmm ",
		"ddddddssssssssssssssssssjjjjjjjj",
		"ddddddssssssssssssssssssjjjjjjjj",
	]
	Wd, Hd = 32, 16
	scr = Image.new("RGBA", (Wd * 8, Hd * 8), W)
	# Underground background checker below row 9, as bg_underground.png
	for y in range(9 * 8, Hd * 8):
		for x in range(Wd * 8):
			if (x + y) % 2 == 0: scr.putpixel((x, y), L)

	def at(layer, x, y):
		if 0 <= y < Hd and 0 <= x < Wd: return layer[y][x]
		return " "

	for y in range(Hd):
		for x in range(Wd):
			c = at(walls, x, y)
			if c == " ": continue
			state = (at(walls, x - 1, y) != " ") | (at(walls, x, y - 1) != " ") << 1 \
				| (at(walls, x + 1, y) != " ") << 2 | (at(walls, x, y + 1) != " ") << 3
			v = h32(x, y) % 3
			frame = wall_sheets[c].crop((1 + PITCH * state, 1 + PITCH * v,
				1 + PITCH * state + FRAME, 1 + PITCH * v + FRAME))
			scr.paste(frame, (x * 8 - 1, y * 8 - 1), frame)

	for y in range(Hd):
		for x in range(Wd):
			c = at(blocks, x, y)
			if c == " ": continue
			same = lambda a, b: at(blocks, a, b) != " "
			state = same(x - 1, y) | same(x, y - 1) << 1 | same(x + 1, y) << 2 | same(x, y + 1) << 3
			v = h32(x, y, 9) % 3
			sx = ((state & 3) * 9) + 1 + 37 * v
			sy = ((state >> 2) * 9) + 1
			frame = block_sheets[c].crop((sx, sy, sx + 8, sy + 8))
			scr.paste(frame, (x * 8, y * 8), frame)

	# Show the same scene as the calculator would: fxconv maps every color to
	# the nearest of the 4 shades
	shades = [B, D, L, W]
	px = scr.load()
	for y in range(scr.size[1]):
		for x in range(scr.size[0]):
			r, g, b, a = px[x, y]
			px[x, y] = min(shades, key=lambda c: (c[0] - r) ** 2 + (c[1] - g) ** 2 + (c[2] - b) ** 2)
	scr = scr.convert("RGB").resize((Wd * 8 * 4, Hd * 8 * 4), Image.NEAREST)
	scr.save(out_path)


def main():
	os.makedirs(os.path.join(IMG, "walls"), exist_ok=True)
	for name in MATERIALS:
		wall_sheet(name).save(os.path.join(IMG, "walls", name + ".png"))
	for name in HAMMERS:
		hammer_sheet(name).save(os.path.join(IMG, "swing_%s_hammer.png" % name))
	patch_items()
	if "--preview" in sys.argv:
		scene_preview(sys.argv[sys.argv.index("--preview") + 1])


if __name__ == "__main__":
	main()
