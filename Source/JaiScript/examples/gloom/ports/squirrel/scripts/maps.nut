// The episode: hand-authored maps + parser + boot-time validation.
// Map chars / tile ints per the reference (REFERENCE.md section 1.9/1.11).
// Chars are compared as Squirrel char-code integers (s[i] is an int).

::MAPS <- [
	{
		name = "E1M1: DIMLIT ANTECHAMBER",
		par = 1650,
		theme = {
			walls = [[122, 108, 92], [96, 118, 88], [88, 96, 122], [130, 90, 74]],
			ceil = [38, 34, 46], floor = [64, 56, 44]
		},
		rows = [
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
		name = "E1M2: COOLANT WARRENS",
		par = 2100,
		theme = {
			walls = [[86, 116, 128], [70, 100, 140], [96, 128, 100], [120, 100, 130]],
			ceil = [26, 36, 46], floor = [42, 56, 60]
		},
		rows = [
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
		name = "E1M3: THE HEX FOUNDRY",
		par = 2700,
		theme = {
			walls = [[140, 96, 60], [120, 70, 100], [100, 100, 60], [80, 110, 120]],
			ceil = [44, 28, 24], floor = [58, 46, 36]
		},
		rows = [
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
		name = "E1M4: GLOOM'S THRONE",
		par = 2400,
		theme = {
			walls = [[150, 60, 50], [100, 44, 40], [130, 110, 60], [70, 50, 60]],
			ceil = [40, 18, 18], floor = [52, 34, 30]
		},
		rows = [
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
function wall_kind_for(ch) {
	if (ch == '#') { return 1; }
	if (ch == '%') { return 2; }
	if (ch == '=') { return 3; }
	if (ch == '&') { return 4; }
	if (ch == 'D') { return 5; }
	if (ch == 'X') { return 6; }
	if (ch == 'S') { return 7; }
	if (ch == 'R') { return 8; }
	if (ch == 'B') { return 9; }
	return 0;
}

function item_kind_for(ch) {
	if (ch == '1') { return 0; }
	if (ch == '2') { return 1; }
	if (ch == '3') { return 2; }
	if (ch == '4') { return 3; }
	if (ch == '5') { return 4; }
	if (ch == '6') { return 5; }
	if (ch == 'r') { return 6; }
	if (ch == 'b') { return 7; }
	if (ch == '7') { return 8; }
	if (ch == '8') { return 9; }
	if (ch == '9') { return 10; }
	return -1;
}

// parse MAPS[mi] into G: tiles, dimensions, spawns. Entities land in G.enemies
// (instances are reference-semantic in Squirrel), items as plain arrays.
function load_map(mi) {
	local g = ::G;
	local m = ::MAPS[mi];
	local rows = m.rows;
	g.map_name = m.name;
	g.par = m.par;
	g.mh = rows.len();
	g.mw = rows[0].len();
	g.tiles = [];
	g.enemies = [];
	g.items = [];
	g.shots = [];
	g.kills = 0;
	g.kill_total = 0;
	g.secrets = 0;
	g.secret_total = 0;
	g.map_ticks = 0;
	for (local y = 0; y < g.mh; ++y) {
		local row = rows[y];
		for (local x = 0; x < g.mw; ++x) {
			local ch = row[x];
			local wk = ::wall_kind_for(ch);
			if (wk == 7) { g.secret_total++; }
			if (wk > 0) { g.tiles.push(wk); continue; }
			g.tiles.push(0);
			if (ch == '@') {
				g.px = x + 0.5;
				g.py = y + 0.5;
				g.pang = 0.0;
			} else if (ch == 'g') { ::spawn_enemy(0, x, y); }
			else if (ch == 'z') { ::spawn_enemy(1, x, y); }
			else if (ch == 't') { ::spawn_enemy(2, x, y); }
			else if (ch == 'W') { ::spawn_enemy(3, x, y); }
			else {
				local ik = ::item_kind_for(ch);
				if (ik >= 0) { g.items.push([ik, x + 0.5, y + 0.5, 0]); }
			}
		}
	}
	::build_render_tables(mi);
}

// raycast render kinds: kind 0 floor/open, 1-4 walls, 5 door, 6 exit, 7 red,
// 8 blue; secrets render as wall kind 1 (that is the point of a secret).
// The reference packs these 15-per-int64 for its parallel contract; the
// serial port reads the tile array directly through this table at ray time.
::TILE_TO_KIND <- [0, 1, 2, 3, 4, 5, 6, 1, 7, 8];

function tile_at(x, y) {
	local g = ::G;
	if (x < 0 || y < 0 || x >= g.mw || y >= g.mh) { return 1; }
	return g.tiles[y * g.mw + x];
}

function tile_solid(t) { return t != 0; }

// walkable for actors: floor only (doors must be opened first)
function walkable(x, y) { return ::tile_at(x, y) == 0; }

// ----------------------------------------------------------- validation -----
// Boot-time sanity: consistent widths, sealed border, one '@', doors flanked
// by floor on one axis, and (with keyed doors treated passable) every floor
// tile reachable from the start. Throws on structural errors.
function validate_map(mi) {
	local rows = ::MAPS[mi].rows;
	local h = rows.len();
	local w = rows[0].len();
	local start_count = 0;
	for (local y = 0; y < h; ++y) {
		if (rows[y].len() != w) {
			throw "map " + mi + " row " + y + ": width " + rows[y].len() + " != " + w;
		}
		for (local x = 0; x < w; ++x) {
			local ch = rows[y][x];
			local border = x == 0 || y == 0 || x == w - 1 || y == h - 1;
			if (border && ::wall_kind_for(ch) == 0) {
				throw "map " + mi + " (" + x + "," + y + "): border leak '" + ch.tochar() + "'";
			}
			if (ch == '@') { start_count++; }
			if (ch == 'D' || ch == 'R' || ch == 'B') {
				local open_ns = ::wall_kind_for(rows[y - 1][x]) == 0 && ::wall_kind_for(rows[y + 1][x]) == 0;
				local open_ew = ::wall_kind_for(rows[y][x - 1]) == 0 && ::wall_kind_for(rows[y][x + 1]) == 0;
				if (!open_ns && !open_ew) {
					throw "map " + mi + " (" + x + "," + y + "): door not flanked by floor";
				}
			}
		}
	}
	if (start_count != 1) { throw "map " + mi + ": " + start_count + " player starts"; }

	// reachability: flood from '@' through floor/doors/secrets; every walkable
	// char and every 'X' neighbor must be reached
	local sx = 0;
	local sy = 0;
	local reach = ::array(w * h, 0);
	for (local y2 = 0; y2 < h; ++y2) {
		for (local x2 = 0; x2 < w; ++x2) {
			if (rows[y2][x2] == '@') { sx = x2; sy = y2; }
		}
	}
	local queue = [[sx, sy]];
	reach[sy * w + sx] = 1;
	local head = 0;
	while (head < queue.len()) {
		local cur = queue[head];
		head++;
		local cx = cur[0];
		local cy = cur[1];
		local dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
		foreach (d in dirs) {
			local nx = cx + d[0];
			local ny = cy + d[1];
			if (nx < 0 || ny < 0 || nx >= w || ny >= h) { continue; }
			if (reach[ny * w + nx] == 1) { continue; }
			local wk = ::wall_kind_for(rows[ny][nx]);
			// doors (any lock) and secrets are traversable for reachability
			if (wk != 0 && wk != 5 && wk != 7 && wk != 8 && wk != 9) { continue; }
			reach[ny * w + nx] = 1;
			queue.push([nx, ny]);
		}
	}
	local exit_reached = false;
	for (local y3 = 0; y3 < h; ++y3) {
		for (local x3 = 0; x3 < w; ++x3) {
			local ch3 = rows[y3][x3];
			local wk3 = ::wall_kind_for(ch3);
			if (wk3 == 0 && reach[y3 * w + x3] == 0) {
				throw "map " + mi + " (" + x3 + "," + y3 + "): unreachable floor '" + ch3.tochar() + "'";
			}
			if (ch3 == 'X') {
				local dirs2 = [[1, 0], [-1, 0], [0, 1], [0, -1]];
				foreach (d2 in dirs2) {
					local ax = x3 + d2[0];
					local ay = y3 + d2[1];
					if (ax < 0 || ay < 0 || ax >= w || ay >= h) { continue; }
					if (reach[ay * w + ax] == 1) { exit_reached = true; }
				}
			}
		}
	}
	if (!exit_reached) { throw "map " + mi + ": exit switch unreachable"; }
}

function validate_all_maps() {
	for (local mi = 0; mi < ::MAPS.len(); ++mi) { ::validate_map(mi); }
}
