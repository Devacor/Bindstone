"""Tile-grid queries shared by sim/combat/enemies, plus the raycast snapshot.

Tile ints (G.tiles, flat row-major, 0-based): 0 floor, 1-4 walls, 5 door,
6 exit, 7 secret, 8 red door, 9 blue door. Solid = nonzero.
"""

import state

# per-cell RENDER kinds for the raycaster (secret -> wall kind 1, red/blue
# doors -> 7/8). The reference bit-packs this 15-per-int64 for its pure-value
# parallel contract; REFERENCE.md 4.2 lets serial ports read a flat array.
TILE_TO_KIND = (0, 1, 2, 3, 4, 5, 6, 1, 7, 8)


def rebuild_mapkind():
	g = state.G
	t2k = TILE_TO_KIND
	g.mapkind = [t2k[t] for t in g.tiles]


def tile_at(x, y):
	g = state.G
	if x < 0 or y < 0 or x >= g.mw or y >= g.mh:
		return 1
	return g.tiles[y * g.mw + x]


def tile_solid(t):
	return t != 0


def walkable(x, y):
	"""walkable for actors: floor only (doors must be opened first)"""
	return tile_at(x, y) == 0
