// Dungeon generation (rooms + corridors), field of view, and pathfinding.
// Tiles: 0 wall, 1 floor, 2 stairs down, 3 JAI statue, 4 DEVACOR mural.

var TILE_WALL = 0;
var TILE_FLOOR = 1;
var TILE_STAIRS = 2;
var TILE_STATUE = 3;
var TILE_MURAL = 4;

function tile_at(int x, int y) -> int {
	if (!in_bounds(x, y)) { return TILE_WALL; }
	return G.tiles[idx(x, y)];
}

function is_walkable(int x, int y) -> bool {
	var t = tile_at(x, y);
	return t != TILE_WALL;
}

function carve_room(var room) -> void {
	for (int ry = room[1]; ry < room[1] + room[3]; ++ry) {
		for (int rx = room[0]; rx < room[0] + room[2]; ++rx) {
			G.tiles[idx(rx, ry)] = TILE_FLOOR;
		}
	}
}

function carve_corridor(int x1, int y1, int x2, int y2) -> void {
	var cx = x1;
	var cy = y1;
	var horizontal_first = RNG.chance(50);
	if (horizontal_first) {
		while (cx != x2) { G.tiles[idx(cx, cy)] = TILE_FLOOR; cx = cx + (x2 > cx ? 1 : -1); }
		while (cy != y2) { G.tiles[idx(cx, cy)] = TILE_FLOOR; cy = cy + (y2 > cy ? 1 : -1); }
	} else {
		while (cy != y2) { G.tiles[idx(cx, cy)] = TILE_FLOOR; cy = cy + (y2 > cy ? 1 : -1); }
		while (cx != x2) { G.tiles[idx(cx, cy)] = TILE_FLOOR; cx = cx + (x2 > cx ? 1 : -1); }
	}
	G.tiles[idx(cx, cy)] = TILE_FLOOR;
}

function room_center(var room) -> var {
	return [room[0] + room[2] / 2, room[1] + room[3] / 2];
}

function rooms_overlap(var a, var b) -> bool {
	return a[0] - 1 < b[0] + b[2] && a[0] + a[2] + 1 > b[0] &&
	       a[1] - 1 < b[1] + b[3] && a[1] + a[3] + 1 > b[1];
}

// Fills G.tiles/G.rooms and returns {"start": [x,y], "stairs": [x,y]}.
function generate_floor(int floor) -> var {
	G.tiles = [];
	for (int i = 0; i < MAP_W * MAP_H; ++i) { G.tiles.push(TILE_WALL); }
	G.seen = [];
	for (int i = 0; i < MAP_W * MAP_H; ++i) { G.seen.push(false); }
	G.rooms = [];

	if (floor >= 10) { return generate_boss_arena(); }

	var room_target = 7 + RNG.next(3);
	for (int attempt = 0; attempt < 80 && G.rooms.size() < room_target; ++attempt) {
		var rw = 5 + RNG.next(8);
		var rh = 3 + RNG.next(4);
		var rx = 1 + RNG.next(MAP_W - rw - 2);
		var ry = 1 + RNG.next(MAP_H - rh - 2);
		var candidate = [rx, ry, rw, rh];
		var blocked = false;
		for (auto other : G.rooms) {
			if (rooms_overlap(candidate, other)) { blocked = true; break; }
		}
		if (blocked) { continue; }
		G.rooms.push(candidate);
		carve_room(candidate);
	}

	for (int i = 1; i < G.rooms.size(); ++i) {
		var from = room_center(G.rooms[i - 1]);
		var to = room_center(G.rooms[i]);
		carve_corridor(from[0], from[1], to[0], to[1]);
	}

	var start = room_center(G.rooms[0]);
	var stairs = room_center(G.rooms[G.rooms.size() - 1]);
	G.tiles[idx(stairs[0], stairs[1])] = TILE_STAIRS;

	// Landmarks of the two legends, sometimes.
	if (RNG.chance(40)) { place_landmark(TILE_STATUE, start, stairs); }
	if (RNG.chance(30)) { place_landmark(TILE_MURAL, start, stairs); }

	return {"start": start, "stairs": stairs};
}

function place_landmark(int tile, var start, var stairs) -> void {
	if (G.rooms.size() < 2) { return; }
	for (int attempt = 0; attempt < 20; ++attempt) {
		var room = G.rooms[RNG.next(G.rooms.size())];
		var lx = room[0] + RNG.next(room[2]);
		var ly = room[1] + RNG.next(room[3]);
		if (lx == start[0] && ly == start[1]) { continue; }
		if (lx == stairs[0] && ly == stairs[1]) { continue; }
		if (G.tiles[idx(lx, ly)] != TILE_FLOOR) { continue; }
		G.tiles[idx(lx, ly)] = tile;
		return;
	}
}

function generate_boss_arena() -> var {
	var arena = [8, 3, 48, 14];
	G.rooms.push(arena);
	carve_room(arena);
	// entry corridor from the west
	var start = [3, 10];
	carve_corridor(3, 10, 8, 10);
	G.tiles[idx(3, 10)] = TILE_FLOOR;
	// Devacor's defaced mural watches the arena
	G.tiles[idx(32, 4)] = TILE_MURAL;
	return {"start": start, "stairs": [-1, -1]};
}

// ------------------------------------------------------------------- FOV ---
function line_clear(int x1, int y1, int x2, int y2) -> bool {
	// integer line walk; every tile strictly between must be transparent
	var dx = iabs(x2 - x1);
	var dy = iabs(y2 - y1);
	var sx = x2 > x1 ? 1 : -1;
	var sy = y2 > y1 ? 1 : -1;
	var err = dx - dy;
	var cx = x1;
	var cy = y1;
	while (true) {
		if (cx == x2 && cy == y2) { return true; }
		if ((cx != x1 || cy != y1) && tile_at(cx, cy) == TILE_WALL) { return false; }
		var e2 = err * 2;
		if (e2 > -dy) { err = err - dy; cx = cx + sx; }
		if (e2 < dx) { err = err + dx; cy = cy + sy; }
	}
	return false;
}

function compute_fov() -> void {
	G.visible = [];
	for (int i = 0; i < MAP_W * MAP_H; ++i) { G.visible.push(false); }
	var px = G.player.x;
	var py = G.player.y;
	var r = G.player.fov_radius;
	for (int vy = imax(0, py - r); vy <= imin(MAP_H - 1, py + r); ++vy) {
		for (int vx = imax(0, px - r); vx <= imin(MAP_W - 1, px + r); ++vx) {
			var ddx = vx - px;
			var ddy = vy - py;
			if (ddx * ddx + ddy * ddy > r * r + 1) { continue; }
			if (line_clear(px, py, vx, vy)) {
				G.visible[idx(vx, vy)] = true;
				G.seen[idx(vx, vy)] = true;
			}
		}
	}
}

function is_visible(int x, int y) -> bool {
	if (!in_bounds(x, y)) { return false; }
	return G.visible[idx(x, y)];
}

// ------------------------------------------------------------------- BFS ---
// First step [dx, dy] of a shortest walkable path, or null when unreachable.
// Used by the smoke autopilot (full map knowledge is fine for a test pilot).
function bfs_step(int sx, int sy, int tx, int ty) -> var {
	if (sx == tx && sy == ty) { return null; }
	var prev = [];
	for (int i = 0; i < MAP_W * MAP_H; ++i) { prev.push(-1); }
	prev[idx(sx, sy)] = idx(sx, sy);
	var queue = [idx(sx, sy)];
	var head = 0;
	var goal = idx(tx, ty);
	while (head < queue.size()) {
		var cur = queue[head];
		head = head + 1;
		if (cur == goal) { break; }
		var cx = cur % MAP_W;
		var cy = cur / MAP_W;
		for (auto d : EIGHT_DIRS) {
			var nx = cx + d[0];
			var ny = cy + d[1];
			if (!in_bounds(nx, ny) || !is_walkable(nx, ny)) { continue; }
			var ni = idx(nx, ny);
			if (prev[ni] != -1) { continue; }
			prev[ni] = cur;
			queue.push(ni);
		}
	}
	if (prev[goal] == -1) { return null; }
	// walk back from goal to the node adjacent to start
	var node = goal;
	while (prev[node] != idx(sx, sy)) { node = prev[node]; }
	return [node % MAP_W - sx, node / MAP_W - sy];
}
