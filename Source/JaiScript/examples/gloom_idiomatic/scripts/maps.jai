// The episode: hand-authored maps + boot-time validation + the World class —
// the tile grid and every query against it (collision, LOS, pathfinding,
// raycast snapshots). Entities stay on Game; World.load() populates them
// through G during the single parse pass.
//
// Map chars:  '#'.'%'.'='.'&'   wall kinds 1-4 (theme-colored)
//             'D' door   'R' red-locked door   'B' blue-locked door
//             'S' secret wall (looks like '#'; 'use' opens it)
//             'X' exit switch wall
//             '.' floor
// Spawns (replaced by floor): '@' player  'g' grunt  'z' spitter  't' turret
//             'W' warden   items: '1' stim '2' medkit '3' armor '4' clip
//             '5' shells '6' cells 'r' red key 'b' blue key '7' scattergun
//             '8' hexcaster '9' soul orb
//
// Tile ints:  0 floor, 1-4 walls, 5 door, 6 exit, 7 secret, 8 red, 9 blue.

var MAPS = [
	{
		"name": "E1M1: DIMLIT ANTECHAMBER",
		"par": 1650,
		"theme": {
			"walls": [[122, 108, 92], [96, 118, 88], [88, 96, 122], [130, 90, 74]],
			"ceil": [38, 34, 46], "floor": [64, 56, 44]
		},
		"rows": [
		"##########################",
		"#........#.....#.....%..4#",
		"#.@......D..g..#..1..%.g.#",
		"#........#.....#.....%...#",
		"#####D####..g..#..%%%%.%%#",
		"#...#....#.....#.........#",
		"#.1.#.g..##D####......g..#",
		"#...#....#.....###D#######",
		"##D###D###..7..#........=#",
		"#..#.....#.....S....2...=#",
		"#..#..g..#.....#........=#",
		"#.4#.....###########D###=#",
		"#..#...........#.....#..=#",
		"#..######D##.g.#..1..#..=#",
		"#......#...#...#.....#..=#",
		"#..2...D.5.#...D.....D..X#",
		"#......#...#...#.....#...#",
		"##########################"]
	},
	{
		"name": "E1M2: COOLANT WARRENS",
		"par": 2100,
		"theme": {
			"walls": [[86, 116, 128], [70, 100, 140], [96, 128, 100], [120, 100, 130]],
			"ceil": [26, 36, 46], "floor": [42, 56, 60]
		},
		"rows": [
		"#############################",
		"#.....#.......#......#......#",
		"#.@...#..g....#..z...#.66...#",
		"#.....D.......D......D......#",
		"#.....#....g..#......#....3.#",
		"#.....#.......#......#......#",
		"###D######D#############D####",
		"#.....#.......#......#......#",
		"#.5...#.......#......#......#",
		"#.....D.......S..9...#..r...#",
		"#..g..#...t...#...6..#......#",
		"#.....#.......#......#....z.#",
		"#.....#.......#......#......#",
		"###D######D#############R####",
		"#.....#.......#......#......#",
		"#.....#.......#..1...#......#",
		"#.2...D..z....D.g....#......X",
		"#..g..#....4..#....g.#....2.#",
		"#.....#.......#......#......#",
		"#############################"]
	},
	{
		"name": "E1M3: THE HEX FOUNDRY",
		"par": 2700,
		"theme": {
			"walls": [[140, 96, 60], [120, 70, 100], [100, 100, 60], [80, 110, 120]],
			"ceil": [44, 28, 24], "floor": [58, 46, 36]
		},
		"rows": [
		"#############################",
		"#.....%.......%......%......#",
		"#.@...%..g....%..z...B...8..#",
		"#.....D...&...D......%......#",
		"#.....%.......%..t...%.66.6.#",
		"#.....%.......%......%......#",
		"###D======D======D==========#",
		"#.....%.......%......%......#",
		"#.....%.&...&.%...3..%......#",
		"#..g..D...t...D..z...D......#",
		"#.....%.&...&.%...1..%....b.#",
		"#.....%.......%......%..z...#",
		"###D======D=============D===#",
		"#.....%.......%......%......#",
		"#.....%.......%...6..%...z..#",
		"#..z..%.......S..9...%......#",
		"#.5...%.2.....%......%..r...#",
		"#.....%.......%......%......#",
		"###D======D=================#",
		"#.....%....4..%......%......#",
		"#.2...%..g....%..1...R......X",
		"#..g..%....g..D....g.%..2...#",
		"#.....%.......%......%......#",
		"#############################"]
	},
	{
		"name": "E1M4: GLOOM'S THRONE",
		"par": 2400,
		"theme": {
			"walls": [[150, 60, 50], [100, 44, 40], [130, 110, 60], [70, 50, 60]],
			"ceil": [40, 18, 18], "floor": [52, 34, 30]
		},
		"rows": [
		"##########################",
		"#.....#..........#.......#",
		"#.@...D....66....D...9...#",
		"#.....#....55....#.......#",
		"#..2..#....22....#...3...#",
		"#.....#..........#.......#",
		"###D######....######D#####",
		"#...#....#....#....#.....#",
		"#.6.#.g..D....D..g.#..5..#",
		"#...#....#....#....#.....#",
		"#.t.#....#....#....#..t..#",
		"#...######....######.....#",
		"#........................#",
		"#..&&&...................#",
		"#..&......W..............#",
		"#..&&&...................#",
		"#........................#",
		"#####........###......####",
		"#..2#........#9#......#X.#",
		"#...#........#.#......D..#",
		"#...##########.########..#",
		"#........................#",
		"##########################"]
	}
];

// -------------------------------------------------------------- parsing -----
function wall_kind_for(string ch) -> int {
	if (ch == "#") { return 1; }
	if (ch == "%") { return 2; }
	if (ch == "=") { return 3; }
	if (ch == "&") { return 4; }
	if (ch == "D") { return 5; }
	if (ch == "X") { return 6; }
	if (ch == "S") { return 7; }
	if (ch == "R") { return 8; }
	if (ch == "B") { return 9; }
	return 0;
}

function item_kind_for(string ch) -> int {
	if (ch == "1") { return 0; }
	if (ch == "2") { return 1; }
	if (ch == "3") { return 2; }
	if (ch == "4") { return 3; }
	if (ch == "5") { return 4; }
	if (ch == "6") { return 5; }
	if (ch == "r") { return 6; }
	if (ch == "b") { return 7; }
	if (ch == "7") { return 8; }
	if (ch == "8") { return 9; }
	if (ch == "9") { return 10; }
	return -1;
}

// a pickup on the floor — a real object instead of a [kind, x, y, taken] row
class Item {
public:
	int kind = 0;
	float x = 0.0;
	float y = 0.0;
	bool taken = false;

	Item(int kind_, float x_, float y_) {
		kind = kind_;
		x = x_;
		y = y_;
	}
}

// raycast snapshot: render kinds packed 15-per-int64 (4 bits each), rebuilt
// only when a tile changes. Kind 0 floor/open, 1-4 walls, 5 door, 6 exit,
// 7 red, 8 blue; secrets render as wall kind 1 (that is the point of a secret).
var TILE_TO_KIND = [0, 1, 2, 3, 4, 5, 6, 1, 7, 8];

// ------------------------------------------------------------ the World -----
class World {
public:
	string map_name = "";
	int par = 0;
	int mw = 0;
	int mh = 0;
	var tiles = [];
	array<int> mappack = [];   // 15 tiles per int64, 4-bit render kinds
	int mapgen = 0;            // bumps with every mappack rebuild

	// parse MAPS[mi]: tiles into self, spawns into G (enemies/items/player pose)
	void load(int mi) {
		var rows = MAPS[mi]["rows"];
		map_name = MAPS[mi]["name"];
		par = MAPS[mi]["par"];
		mh = rows.size();
		mw = rows[0].size();
		tiles = [];
		for (int y = 0; y < mh; ++y) {
			var row = rows[y];
			for (int x = 0; x < mw; ++x) {
				var ch = row.substr(x, 1);
				var wk = wall_kind_for(ch);
				if (wk == 7) { G.secret_total = G.secret_total + 1; }
				if (wk > 0) { tiles.push(wk); continue; }
				tiles.push(0);
				if (ch == "@") { G.player.place(x + 0.5, y + 0.5); }
				else if (ch == "g") { G.spawn_enemy(0, x, y); }
				else if (ch == "z") { G.spawn_enemy(1, x, y); }
				else if (ch == "t") { G.spawn_enemy(2, x, y); }
				else if (ch == "W") { G.spawn_enemy(3, x, y); }
				else {
					var ik = item_kind_for(ch);
					if (ik >= 0) { G.items.push(new Item(ik, x + 0.5, y + 0.5)); }
				}
			}
		}
		rebuild_mappack();
	}

	void rebuild_mappack() {
		mappack = packed_from(tiles, mw * mh);
		mapgen = mapgen + 1;
	}

	int tile_at(int x, int y) {
		if (x < 0 || y < 0 || x >= mw || y >= mh) { return 1; }
		return tiles[y * mw + x];
	}

	bool solid_at(int x, int y) { return tile_at(x, y) != 0; }

	// walkable for actors: floor only (doors must be opened first)
	bool walkable(int x, int y) { return tile_at(x, y) == 0; }

	// circle-vs-grid: all four corners of the actor's square must be on floor
	bool spot_free(float x, float y, float r) {
		if (solid_at(math::ifloor(x - r), math::ifloor(y - r))) { return false; }
		if (solid_at(math::ifloor(x + r), math::ifloor(y - r))) { return false; }
		if (solid_at(math::ifloor(x - r), math::ifloor(y + r))) { return false; }
		if (solid_at(math::ifloor(x + r), math::ifloor(y + r))) { return false; }
		return true;
	}

	// stepped-ray LOS on the tile grid (deterministic float march)
	bool los_clear(float ax, float ay, float bx, float by) {
		float dx = bx - ax;
		float dy = by - ay;
		float len = math::sqrt(dx * dx + dy * dy);
		if (len < 0.001) { return true; }
		float inv = 1.0 / len;
		dx = dx * inv;
		dy = dy * inv;
		float t = 0.15;
		while (t < len) {
			if (solid_at(math::ifloor(ax + dx * t), math::ifloor(ay + dy * t))) { return false; }
			t += 0.15;
		}
		return true;
	}

	// wall distance along an arbitrary unit ray — the SAME pure DDA the renderer
	// uses (pure functions are just functions outside a region)
	float wall_dist(float ax, float ay, float dx, float dy) {
		var hit = gloom_ray(ray_record(ax, ay, dx, dy));
		float pd = hit[0];
		return pd;
	}

	// flat ray record for serial gloom_ray calls (hitscan, tracer garnish)
	var ray_record(float ax, float ay, float dx, float dy) {
		var rec = [ax, ay, dx, dy, mw];
		for (auto wv : mappack) { rec.push(wv); }
		return rec;
	}

	// doors and secrets open by becoming floor
	void clear_tile(int tx, int ty) {
		tiles[ty * mw + tx] = 0;
		rebuild_mappack();
	}

	// BFS distances FROM the goal, over floor + doors the given keys can open
	// (+ the goal tile itself so walls like secrets/exit are approachable)
	var bfs_field(int gx, int gy, bool key_r, bool key_b) {
		var field = [];
		int n = mw * mh;
		for (int i = 0; i < n; ++i) { field.push(-1); }
		if (gx < 0) { return field; }
		var queue = [[gx, gy]];
		field[gy * mw + gx] = 0;
		int head = 0;
		while (head < queue.size()) {
			var cur = queue[head];
			head = head + 1;
			int cx = cur[0];
			int cy = cur[1];
			var cd = field[cy * mw + cx];
			var dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
			for (auto& d : dirs) {
				int nx = cx + d[0];
				int ny = cy + d[1];
				if (nx < 0 || ny < 0 || nx >= mw || ny >= mh) { continue; }
				var seen = field[ny * mw + nx];
				if (seen >= 0) { continue; }
				int t = tiles[ny * mw + nx];
				bool pass = t == 0 || t == 5;
				if (t == 8 && key_r) { pass = true; }
				if (t == 9 && key_b) { pass = true; }
				if (!pass) { continue; }
				field[ny * mw + nx] = cd + 1;
				queue.push([nx, ny]);
			}
		}
		return field;
	}

private:
	var packed_from(var& src, int n) {
		var pack = [];
		var acc = 0;
		var slot = 0;
		for (int i = 0; i < n; ++i) {
			acc = acc | (TILE_TO_KIND[src[i]] << (slot * 4));
			slot = slot + 1;
			if (slot == 15) {
				pack.push(acc);
				acc = 0;
				slot = 0;
			}
		}
		if (slot > 0) { pack.push(acc); }
		return pack;
	}
}

// ----------------------------------------------------------- validation -----
// Boot-time sanity over the RAW map data (before any World exists), so these
// stay free functions: consistent widths, sealed border, one '@', doors flanked
// by floor on one axis, and (with keyed doors treated passable) every floor
// tile reachable from the start. Throws on structural errors: a broken map is
// a broken determinism battery.
function validate_map(int mi) -> void {
	var rows = MAPS[mi]["rows"];
	var h = rows.size();
	var w = rows[0].size();
	var start_count = 0;
	for (int y = 0; y < h; ++y) {
		if (rows[y].size() != w) {
			throw `map ${mi} row ${y}: width ${rows[y].size()} != ${w}`;
		}
		for (int x = 0; x < w; ++x) {
			var ch = rows[y].substr(x, 1);
			var border = x == 0 || y == 0 || x == w - 1 || y == h - 1;
			if (border && wall_kind_for(ch) == 0) {
				throw `map ${mi} (${x},${y}): border leak '${ch}'`;
			}
			if (ch == "@") { start_count = start_count + 1; }
			if (ch == "D" || ch == "R" || ch == "B") {
				var open_ns = wall_kind_for(rows[y - 1].substr(x, 1)) == 0 && wall_kind_for(rows[y + 1].substr(x, 1)) == 0;
				var open_ew = wall_kind_for(rows[y].substr(x - 1, 1)) == 0 && wall_kind_for(rows[y].substr(x + 1, 1)) == 0;
				if (!open_ns && !open_ew) {
					throw `map ${mi} (${x},${y}): door not flanked by floor`;
				}
			}
		}
	}
	if (start_count != 1) { throw `map ${mi}: ${start_count} player starts`; }

	// reachability: flood from '@' through floor/doors/secrets; every walkable
	// char and every 'X' neighbor must be reached
	var sx = 0;
	var sy = 0;
	var reach = [];
	for (int i = 0; i < w * h; ++i) { reach.push(0); }
	for (int y2 = 0; y2 < h; ++y2) {
		for (int x2 = 0; x2 < w; ++x2) {
			if (rows[y2].substr(x2, 1) == "@") { sx = x2; sy = y2; }
		}
	}
	var queue = [[sx, sy]];
	reach[sy * w + sx] = 1;
	var head = 0;
	while (head < queue.size()) {
		var cur = queue[head];
		head = head + 1;
		int cx = cur[0];
		int cy = cur[1];
		var dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
		for (auto& d : dirs) {
			int nx = cx + d[0];
			int ny = cy + d[1];
			if (nx < 0 || ny < 0 || nx >= w || ny >= h) { continue; }
			var already = reach[ny * w + nx];
			if (already == 1) { continue; }
			var ch2 = rows[ny].substr(nx, 1);
			var wk = wall_kind_for(ch2);
			// doors (any lock) and secrets are traversable for reachability
			if (wk != 0 && wk != 5 && wk != 7 && wk != 8 && wk != 9) { continue; }
			reach[ny * w + nx] = 1;
			queue.push([nx, ny]);
		}
	}
	var exit_reached = false;
	for (int y3 = 0; y3 < h; ++y3) {
		for (int x3 = 0; x3 < w; ++x3) {
			var ch3 = rows[y3].substr(x3, 1);
			var wk3 = wall_kind_for(ch3);
			var r = reach[y3 * w + x3];
			if (wk3 == 0 && r == 0) {
				throw `map ${mi} (${x3},${y3}): unreachable floor '${ch3}'`;
			}
			if (ch3 == "X") {
				var dirs2 = [[1, 0], [-1, 0], [0, 1], [0, -1]];
				for (auto& d2 : dirs2) {
					int ax = x3 + d2[0];
					int ay = y3 + d2[1];
					if (ax < 0 || ay < 0 || ax >= w || ay >= h) { continue; }
					var ar = reach[ay * w + ax];
					if (ar == 1) { exit_reached = true; }
				}
			}
		}
	}
	if (!exit_reached) { throw `map ${mi}: exit switch unreachable`; }
}

function validate_all_maps() -> void {
	for (int mi = 0; mi < MAPS.size(); ++mi) { validate_map(mi); }
}
