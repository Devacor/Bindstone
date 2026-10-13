"""Map parsing + boot-time validation (map DATA is in data.py, generated from
the reference maps.jai so the rows are byte-exact)."""

import enemies
import render
import state
from data import MAPS
from world import rebuild_mapkind

WALL_KINDS = {"#": 1, "%": 2, "=": 3, "&": 4, "D": 5, "X": 6, "S": 7, "R": 8, "B": 9}
ITEM_KINDS = {"1": 0, "2": 1, "3": 2, "4": 3, "5": 4, "6": 5, "r": 6, "b": 7, "7": 8, "8": 9, "9": 10}


def wall_kind_for(ch):
	return WALL_KINDS.get(ch, 0)


def item_kind_for(ch):
	return ITEM_KINDS.get(ch, -1)


def load_map(mi):
	"""parse MAPS[mi] into G: tiles, dimensions, spawns."""
	g = state.G
	m = MAPS[mi]
	rows = m["rows"]
	g.map_name = m["name"]
	g.par = m["par"]
	g.mh = len(rows)
	g.mw = len(rows[0])
	g.tiles = []
	g.enemies = []
	g.items = []
	g.shots = []
	g.kills = 0
	g.kill_total = 0
	g.secrets = 0
	g.secret_total = 0
	g.map_ticks = 0
	tiles = g.tiles
	items = g.items
	for y, row in enumerate(rows):
		for x, ch in enumerate(row):
			wk = wall_kind_for(ch)
			if wk == 7:
				g.secret_total += 1
			if wk > 0:
				tiles.append(wk)
			else:
				tiles.append(0)
				if ch == "@":
					g.px = x + 0.5
					g.py = y + 0.5
					g.pang = 0.0
				elif ch == "g":
					enemies.spawn_enemy(0, x, y)
				elif ch == "z":
					enemies.spawn_enemy(1, x, y)
				elif ch == "t":
					enemies.spawn_enemy(2, x, y)
				elif ch == "W":
					enemies.spawn_enemy(3, x, y)
				else:
					ik = item_kind_for(ch)
					if ik >= 0:
						items.append([ik, x + 0.5, y + 0.5, 0])
	rebuild_mapkind()
	render.build_render_tables(mi)


# ----------------------------------------------------------- validation -----
def validate_map(mi):
	rows = MAPS[mi]["rows"]
	h = len(rows)
	w = len(rows[0])
	start_count = 0
	for y, row in enumerate(rows):
		if len(row) != w:
			raise ValueError(f"map {mi} row {y}: width {len(row)} != {w}")
		for x, ch in enumerate(row):
			border = x == 0 or y == 0 or x == w - 1 or y == h - 1
			if border and wall_kind_for(ch) == 0:
				raise ValueError(f"map {mi} ({x},{y}): border leak '{ch}'")
			if ch == "@":
				start_count += 1
			if ch in "DRB":
				open_ns = wall_kind_for(rows[y - 1][x]) == 0 and wall_kind_for(rows[y + 1][x]) == 0
				open_ew = wall_kind_for(row[x - 1]) == 0 and wall_kind_for(row[x + 1]) == 0
				if not open_ns and not open_ew:
					raise ValueError(f"map {mi} ({x},{y}): door not flanked by floor")
	if start_count != 1:
		raise ValueError(f"map {mi}: {start_count} player starts")

	# reachability: flood from '@' through floor/doors/secrets
	sx = sy = 0
	for y, row in enumerate(rows):
		x = row.find("@")
		if x >= 0:
			sx, sy = x, y
	reach = [0] * (w * h)
	reach[sy * w + sx] = 1
	queue = [(sx, sy)]
	head = 0
	dirs = ((1, 0), (-1, 0), (0, 1), (0, -1))
	while head < len(queue):
		cx, cy = queue[head]
		head += 1
		for dx, dy in dirs:
			nx, ny = cx + dx, cy + dy
			if 0 <= nx < w and 0 <= ny < h and reach[ny * w + nx] != 1:
				wk = wall_kind_for(rows[ny][nx])
				# doors (any lock) and secrets are traversable for reachability
				if wk in (0, 5, 7, 8, 9):
					reach[ny * w + nx] = 1
					queue.append((nx, ny))
	exit_reached = False
	for y, row in enumerate(rows):
		for x, ch in enumerate(row):
			if wall_kind_for(ch) == 0 and reach[y * w + x] == 0:
				raise ValueError(f"map {mi} ({x},{y}): unreachable floor '{ch}'")
			if ch == "X":
				for dx, dy in dirs:
					ax, ay = x + dx, y + dy
					if 0 <= ax < w and 0 <= ay < h and reach[ay * w + ax] == 1:
						exit_reached = True
	if not exit_reached:
		raise ValueError(f"map {mi}: exit switch unreachable")


def validate_all_maps():
	for mi in range(len(MAPS)):
		validate_map(mi)
