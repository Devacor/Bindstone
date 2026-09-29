// Frame composition and modal screens for the first-person crawler. The 3D
// panes come from view.jai; this file wraps them in the Moraff-style chrome:
// the log, the character sheet, the compass, the maps and what is in sight (in
// the multi-view's corner boxes, or the panel and log strip around the forward
// view alone), the modals, and the sizing that picks the layout for the console.
// Every line ends with ESC[K so frames overdraw each other without a clear.

var PANEL_W = 15;

function eol() -> string { return ESC + "[K"; }

function frame_join(var lines) -> string {
	var line_end = eol() + "\n";
	return lines.join(line_end) + line_end + ESC + "[J";
}

function present(var lines) -> string {
	var frame = frame_join(lines);
	draw(frame);
	return frame;
}

function bar(int cur, int cap, int width, string color_on) -> string {
	if (cap <= 0) { cap = 1; }
	var filled = iclamp((cur * width) / cap, 0, width);
	var out = col(color_on, "#".repeat(filled));
	return out + col("90", "-".repeat(width - filled));
}

// --------------------------------------------------------------- frames ----
var MAP_DIVIDER = col("90", "|");

function title_line() -> string {
	var god_tag = G.player.god ? col("95", " [GOD]") : "";
	return col("93", " GRUBWELL") + col("90", " -- dungeons of the unwashed --")
		+ `  Floor ${G.floor}/10  Turn ${G.turn}  ` + col("90", `~ ${theme_name(G.floor)} ~`) + god_tag;
}

// The multi-view carries everything in its corner boxes; the forward view
// alone has the panel beside it and the last five log lines under it.
function frame_lines(var view_lines) -> var {
	var lines = [];
	lines.push(title_line());
	if (VIEW_LAYOUT != LAYOUT_SINGLE) {
		for (int y = 0; y < view_lines.size(); ++y) { lines.push(view_lines[y]); }
		return lines;
	}
	var panel = build_panel();
	for (int y = 0; y < view_lines.size(); ++y) {
		var side = y < panel.size() ? panel[y] : "";
		lines.push(view_lines[y] + MAP_DIVIDER + side);
	}
	var start = imax(0, G.msgs.size() - 5);
	for (int i = start; i < G.msgs.size(); ++i) {
		lines.push(" " + G.msgs[i]);
	}
	return lines;
}

function compose_frame(var view_lines) -> void { present(frame_lines(view_lines)); }

// ---------------------------------------------------------------- sizing ---
// The console size when the host can tell (the runner shim measures it); 0 =
// unknown, and the view keeps the size it was given. The multi-view frame is
// the title over the canvas; the forward view alone adds the panel beside it
// and five log lines under it.
int SCREEN_COLS = 0;
int SCREEN_ROWS = 0;
int VIEW_ROWS_FIXED = 0;    // the host asked for this many forward rows: layouts keep them
int VIEW_ROWS_MAX = 48;

function set_view_rows(int rows) -> void {
	VIEW_H = rows;
	VIEW_W = rows * 16 / 5;
}

function layout_fits(int mode, int rows) -> bool {
	layout_panes(mode, rows * 16 / 5, rows);
	LAYOUT_KEY = -1;
	if (mode == LAYOUT_SINGLE) { return CANVAS_W + 18 <= SCREEN_COLS && CANVAS_H + 8 <= SCREEN_ROWS; }
	return CANVAS_W + 1 <= SCREEN_COLS && CANVAS_H + 3 <= SCREEN_ROWS;
}

// the tallest forward view (20 to 48 rows) whose frame fits the console, 0 if none
function fit_rows(int mode) -> int {
	for (int rows = VIEW_ROWS_MAX; rows >= 20; --rows) {
		if (layout_fits(mode, rows)) { return rows; }
	}
	return 0;
}

// The multi-view whenever it fits with a forward view of at least 20 rows,
// else the forward view alone, as large as the console allows.
function fit_screen() -> void {
	if (SCREEN_COLS <= 0) { return; }
	if (VIEW_ROWS_FIXED > 0) {
		set_view_rows(VIEW_ROWS_FIXED);
		VIEW_LAYOUT = layout_fits(LAYOUT_MULTI, VIEW_ROWS_FIXED) ? LAYOUT_MULTI : LAYOUT_SINGLE;
		return;
	}
	int rows = fit_rows(LAYOUT_MULTI);
	VIEW_LAYOUT = rows > 0 ? LAYOUT_MULTI : LAYOUT_SINGLE;
	if (rows == 0) { rows = imax(20, fit_rows(LAYOUT_SINGLE)); }
	set_view_rows(rows);
}

// v: forward view alone <-> multi-view; a measured console re-fits the forward view
function cycle_layout() -> void {
	VIEW_LAYOUT = VIEW_LAYOUT == LAYOUT_SINGLE ? LAYOUT_MULTI : LAYOUT_SINGLE;
	if (SCREEN_COLS > 0 && VIEW_ROWS_FIXED == 0) { set_view_rows(imax(20, fit_rows(VIEW_LAYOUT))); }
}

// m puts the overhead map in the forward pane's place; the other panes and
// the boxes stay live
function game_view_lines() -> var {
	if (LAYOUT_KEY != layout_key()) { build_layout(); }
	MAP_ON = G.show_map ? 1 : 0;
	if (G.show_map) { MAP_ROWS = map_window(0 - imax(0, PANE_W[0] - MAP_W) / 2, 0 - imax(0, PANE_H[0] - MAP_H) / 2, PANE_W[0], PANE_H[0]); }
	fill_boxes();
	return render_view(FACE_DX[G.facing], FACE_DY[G.facing]);
}

function render_game() -> void {
	frame_mark();   // --time probe: turn logic ends here, frame build begins
	compose_frame(game_view_lines());
}

// A fast 3-frame sweep between cardinals: the cheap trick that makes 90-degree
// snap turns read as motion. rot is -1 (left) or +1 (right).
function animate_turn(int old_facing, int rot) -> void {
	if (G.smoke || HOST_SCRIPTED || G.show_map) { return; }
	fill_boxes();
	float dx0 = FACE_DX[old_facing];
	float dy0 = FACE_DY[old_facing];
	var sweep = [0.5236, 1.0472];   // 30, 60 degrees; the loop draws the 90 frame
	for (auto a : sweep) {
		float ang = rot * a;
		float ca = cos(ang);
		float sa = sin(ang);
		compose_frame(render_view(dx0 * ca - dy0 * sa, dx0 * sa + dy0 * ca));
		time::sleep_ms(26);
	}
}

// -------------------------------------------------------------- minimap ----
// Explored-tiles overhead view: seen geometry, live monsters and loot where
// you can see them, and your facing arrow. The m map and the MAP box are
// windows onto it.
var TILE_CELL_VIS = [
	col("37", "#"), col("90", "."), col("96", ">"), col("93", "&"), col("95", "%")
];
var TILE_CELL_SEEN = [
	col("90", "#"), col("90", " "), col("90", ">"), col("90", "&"), col("90", "%")
];
var CELL_UNSEEN = " ";
var FACE_ARROW = ["^", ">", "v", "<"];

// w x h cells of the map from tile (x0, y0); off the map is blank
function map_window(int x0, int y0, int w, int h) -> array<string> {
	var overlay = {};
	for (auto it : G.ground) {
		overlay[idx(it.x, it.y)] = [it.glyph, it.color];
	}
	for (auto m : G.monsters) {
		if (!m.alive) { continue; }
		if (m.disguised) { overlay[idx(m.x, m.y)] = ["?", "33"]; }
		else { overlay[idx(m.x, m.y)] = [m.glyph, m.color]; }
	}
	var player_cell = col("93", FACE_ARROW[G.facing]);
	array<string> lines = [];
	for (int y = y0; y < y0 + h; ++y) {
		string row = "";
		for (int x = x0; x < x0 + w; ++x) {
			if (!in_bounds(x, y)) {
				row += CELL_UNSEEN;
				continue;
			}
			var i = idx(x, y);
			// hoisted: a BARE element read as an if condition is always truthy
			// (both backends, 2026-07 ref-lvalue regression — reported); a local
			// copy derefs correctly
			var tile_visible = G.visible[i];
			var tile_seen = G.seen[i];
			if (x == G.player.x && y == G.player.y) {
				row += player_cell;
			} else if (tile_visible) {
				var over = overlay[i];
				if (over != null) { row += col(over[1], over[0]); }
				else { row += TILE_CELL_VIS[G.tiles[i]]; }
			} else if (tile_seen) {
				row += TILE_CELL_SEEN[G.tiles[i]];
			} else {
				row += CELL_UNSEEN;
			}
		}
		lines.push(row);
	}
	return lines;
}

// ----------------------------------------------------------------- boxes ---
// Box text is ASCII (a byte is a cell) carrying SGR escapes, which take none.

function vis_len(string s) -> int {
	int n = s.size();
	int e = s.find(ESC);
	while (e >= 0) {
		int m = s.find("m", e);
		if (m < 0) { m = s.size() - 1; }
		n = n - (m - e + 1);
		e = s.find(ESC, m + 1);
	}
	return n;
}

// exactly w cells: clipped (a cut color is reset) or padded with blanks
function fit_width(string s, int w) -> string {
	int e = s.find(ESC);
	if (e < 0) {
		if (s.size() > w) { return s.substr(0, w); }
		return s + " ".repeat(w - s.size());
	}
	string out = "";
	int n = 0;
	int i = 0;
	while (i < s.size() && n < w) {
		if (i == e) {
			int m = s.find("m", e);
			if (m < 0) { m = s.size() - 1; }
			out += s.substr(e, m - e + 1);
			i = m + 1;
			e = s.find(ESC, i);
			continue;
		}
		int stop = e < 0 ? s.size() : e;
		int take = imin(stop - i, w - n);
		out += s.substr(i, take);
		n = n + take;
		i = i + take;
	}
	return out + VRESET + " ".repeat(w - n);
}

function plain_text(string s) -> string {
	string out = "";
	int i = 0;
	while (i < s.size()) {
		int e = s.find(ESC, i);
		if (e < 0) { return out + s.substr(i, s.size() - i); }
		out += s.substr(i, e - i);
		int m = s.find("m", e);
		if (m < 0) { return out; }
		i = m + 1;
	}
	return out;
}

// Word-wraps s to lines of w cells, continuation lines indented one cell; a
// color in force at a break carries over to the next line.
function wrap_text(string s, int w) -> array<string> {
	array<string> lines = [];
	if (vis_len(s) <= w) {
		lines.push(s);
		return lines;
	}
	array<string> ch = [];      // the visible characters
	array<string> pre = [];     // the escapes just before each one
	array<string> tint = [];    // the color each one is drawn in
	string pending = "";
	string cur = "";
	int i = 0;
	while (i < s.size()) {
		int e = s.find(ESC, i);
		int stop = e < 0 ? s.size() : e;
		for (int k = i; k < stop; ++k) {
			ch.push(s.substr(k, 1));
			pre.push(pending);
			tint.push(cur);
			pending = "";
		}
		if (e < 0) { break; }
		int m = s.find("m", e);
		if (m < 0) { break; }
		string esc = s.substr(e, m - e + 1);
		pending += esc;
		cur = esc == VRESET ? "" : esc;
		i = m + 1;
	}
	int n = ch.size();
	int a = 0;
	while (a < n) {
		while (a < n && ch[a] == " ") { a = a + 1; }
		if (a >= n) { break; }
		int room = lines.size() == 0 ? w : w - 1;
		int b = a + room;
		if (b >= n) {
			b = n;
		} else {
			int cut = b;
			while (cut > a && ch[cut] != " ") { cut = cut - 1; }
			if (cut > a) { b = cut; }
		}
		string line = lines.size() == 0 ? "" : " ";
		line += tint[a] + ch[a];
		for (int k = a + 1; k < b; ++k) { line += pre[k] + ch[k]; }
		lines.push(line);
		a = b;
	}
	return lines;
}

function pad_to(string s, int w) -> string { return s + " ".repeat(imax(1, w - vis_len(s))); }

function center_cells(string s, int w) -> string { return " ".repeat(imax(0, (w - vis_len(s)) / 2)) + s; }

// the newest messages at the bottom, as many as fit; those from before your
// last move are dimmed
function log_box(int w, int h) -> array<string> {
	array<string> rows = [];
	int i = G.msgs.size() - 1;
	while (i >= 0 && rows.size() < h) {
		int t = MSG_TURNS.size() - (G.msgs.size() - i);
		string msg = G.msgs[i];
		if (t < 0 || MSG_TURNS[t] < G.turn - 1) { msg = col("90", plain_text(msg)); }
		array<string> wrapped = wrap_text(msg, w);
		for (int k = wrapped.size() - 1; k >= 0 && rows.size() < h; --k) { rows.push(wrapped[k]); }
		i = i - 1;
	}
	array<string> out = [];
	for (int k = rows.size(); k < h; ++k) { out.push(""); }
	for (int k = rows.size() - 1; k >= 0; --k) { out.push(rows[k]); }
	return out;
}

// the explored map around you, north up
function map_box(int w, int h) -> array<string> {
	return map_window(G.player.x - w / 2, G.player.y - h / 2, w, h);
}

function compass_box(int w, int h) -> array<string> {
	array<string> block = [];
	block.push(compass_letter(0));
	block.push(col("90", "|"));
	block.push(compass_letter(3) + col("90", " -+- ") + compass_letter(1));
	block.push(col("90", "|"));
	block.push(compass_letter(2));
	block.push("");
	block.push(col("96", FACE_NAME[G.facing]));
	block.push(col("90", `floor ${G.floor}/10`));
	array<string> out = [];
	for (int k = 0; k < (h - block.size()) / 2; ++k) { out.push(""); }
	for (auto line : block) { out.push(center_cells(line, w)); }
	return out;
}

function gear_full(var item) -> string {
	if (item == null) { return col("90", "-"); }
	return col(item.color, item.name) + col("90", item_stats_label(item));
}

function hero_box(int w, int h) -> array<string> {
	var p = G.player;
	array<string> out = [];
	out.push(col("97", p.name));
	out.push(col("90", `${species_name(p.species_id)} ${class_name(p.class_id)}`));
	int bw = iclamp(w - 12, 4, 24);
	out.push(pad_to(`HP ${p.hp}/${p.total_max_hp()}`, 12) + bar(p.hp, p.total_max_hp(), bw, p.hp * 4 < p.total_max_hp() ? "91" : "92"));
	out.push(pad_to(`Fo ${p.focus}/${p.total_max_focus()}`, 12) + bar(p.focus, p.total_max_focus(), bw, "96"));
	out.push(G.last_dmg > 0 ? col("91", `-${G.last_dmg} HP`) + col("90", ` (${G.last_dmg_src})`) : "");
	out.push(`Lv ${p.level}  XP ${p.xp}/${p.xp_next()}`);
	string atk = `Atk ${p.total_atk()}  Def ${p.total_def()}`;
	string eva = `Eva ${p.total_eva()}  Crt ${p.total_crit()}`;
	if (atk.size() + eva.size() + 2 <= w) {
		out.push(atk + "  " + eva);
	} else {
		out.push(atk);
		out.push(eva);
	}
	out.push(col("33", `Gold ${p.gold}`) + `  Kills ${p.kills}`);
	for (auto c : CLASSES) {
		if (c["id"] == p.class_id) { out.push(col("90", `Skill ${c["skill_name"]} (${c["skill_cost"]} focus, z)`)); }
	}
	string fx_line = "";
	for (auto fx : p.effects) { fx_line = fx_line + `${fx["kind"]}:${fx["turns"]} `; }
	if (fx_line != "") {
		for (auto line : wrap_text(col("95", fx_line), w)) { out.push(line); }
	}
	out.push("");
	out.push("Wpn " + gear_full(p.weapon));
	out.push("Arm " + gear_full(p.armor));
	out.push("Trk " + gear_full(p.trinket));
	// a tall box also explains the kin's trait and the class skill, each only if it fits whole
	array<string> notes = [];
	for (auto s : SPECIES) {
		if (s["id"] == p.species_id) { notes.push(col("36", `${s["trait_name"]}: `) + col("90", s["trait_blurb"])); }
	}
	for (auto c : CLASSES) {
		if (c["id"] == p.class_id) { notes.push(col("36", `${c["skill_name"]}: `) + col("90", c["skill_blurb"])); }
	}
	for (auto note : notes) {
		array<string> wrapped = wrap_text(note, w);
		if (out.size() + 1 + wrapped.size() > h) { break; }
		out.push("");
		for (auto line : wrapped) { out.push(line); }
	}
	return out;
}

// "LEFT 3": the pane a tile shows up in, and how many steps away it is
function where_label(int x, int y) -> string {
	int dx = x - G.player.x;
	int dy = y - G.player.y;
	if (dx == 0 && dy == 0) { return "HERE"; }
	int ahead = dx * FACE_DX[G.facing] + dy * FACE_DY[G.facing];
	int right = dx * FACE_DX[(G.facing + 1) % 4] + dy * FACE_DY[(G.facing + 1) % 4];
	int pane = iabs(ahead) >= iabs(right) ? (ahead > 0 ? 0 : 2) : (right > 0 ? 1 : 3);
	return `${PANE_NAMES[pane]} ${imax(iabs(dx), iabs(dy))}`;
}

// nearest first: each name in its color, where it is on the right
function sighting_rows(array<string> names, array<string> colors, array<int> xs, array<int> ys, int w, int h, string none) -> array<string> {
	array<int> order = [];
	array<int> dist = [];
	for (int i = 0; i < names.size(); ++i) {
		int d = cheb(xs[i], ys[i], G.player.x, G.player.y);
		int j = order.size();
		order.push(i);
		dist.push(d);
		while (j > 0 && dist[j - 1] > d) {
			order[j] = order[j - 1];
			dist[j] = dist[j - 1];
			j = j - 1;
		}
		order[j] = i;
		dist[j] = d;
	}
	array<string> out = [];
	if (order.size() == 0) { out.push(col("90", none)); }
	for (int k = 0; k < order.size() && k < h; ++k) {
		int i = order[k];
		string where = where_label(xs[i], ys[i]);
		string name = names[i];
		int room = w - 1 - where.size();
		if (name.size() > room) { name = name.substr(0, imax(0, room)); }
		out.push(col(colors[i], name) + " ".repeat(imax(1, w - name.size() - where.size())) + col("96", where));
	}
	return out;
}

// the monsters you can see
function sight_box(int w, int h) -> array<string> {
	array<string> names = [];
	array<string> colors = [];
	array<int> xs = [];
	array<int> ys = [];
	for (auto m : G.monsters) {
		if (!m.alive || m.disguised) { continue; }
		var in_view = G.visible[idx(m.x, m.y)];
		if (!in_view) { continue; }
		names.push(`${m.glyph} ${m.name}`);
		colors.push(m.color);
		xs.push(m.x);
		ys.push(m.y);
	}
	return sighting_rows(names, colors, xs, ys, w, h, "nothing but the dark");
}

// the loot you can see; a mimic is loot until it very much is not
function loot_box(int w, int h) -> array<string> {
	array<string> names = [];
	array<string> colors = [];
	array<int> xs = [];
	array<int> ys = [];
	for (auto it : G.ground) {
		var in_view = G.visible[idx(it.x, it.y)];
		if (!in_view) { continue; }
		names.push(`${it.glyph} ${item_label(it)}`);
		colors.push(it.color);
		xs.push(it.x);
		ys.push(it.y);
	}
	for (auto m : G.monsters) {
		if (!m.alive || !m.disguised) { continue; }
		var in_view = G.visible[idx(m.x, m.y)];
		if (!in_view) { continue; }
		names.push("? a chest");
		colors.push("33");
		xs.push(m.x);
		ys.push(m.y);
	}
	return sighting_rows(names, colors, xs, ys, w, h, "no loot in sight");
}

function box_lines(int id, int w, int h) -> array<string> {
	if (id == BOX_LOG) { return log_box(w, h); }
	if (id == BOX_MAP) { return map_box(w, h); }
	if (id == BOX_COMPASS) { return compass_box(w, h); }
	if (id == BOX_HERO) { return hero_box(w, h); }
	if (id == BOX_SIGHT) { return sight_box(w, h); }
	return loot_box(w, h);
}

// text boxes keep a blank cell inside each side; the map runs edge to edge
function fill_boxes() -> void {
	for (int b = 0; b < BOX_ID.size(); ++b) {
		bool text = BOX_ID[b] != BOX_MAP;
		int w = text ? BOX_W[b] - 2 : BOX_W[b];
		int h = BOX_H[b];
		array<string> lines = box_lines(BOX_ID[b], w, h);
		for (int r = 0; r < h; ++r) {
			string row = fit_width(r < lines.size() ? lines[r] : "", w);
			BOX_ROWS[BOX_ROW0[b] + r] = text ? " " + row + " " : row;
		}
	}
}

// ---------------------------------------------------------------- panel ----
function compass_letter(int face) -> string {
	if (G.facing == face) { return col("93", FACE_LETTER[face]); }
	return col("90", FACE_LETTER[face]);
}

function build_panel() -> var {
	var p = G.player;
	var lines = [];
	lines.push(" " + col("97", p.name));
	lines.push(" " + col("90", species_name(p.species_id)));
	lines.push(` HP ${p.hp}/${p.total_max_hp()}`);
	lines.push(" " + bar(p.hp, p.total_max_hp(), 12, p.hp * 4 < p.total_max_hp() ? "91" : "92"));
	if (G.last_dmg > 0) {
		lines.push(" " + col("91", `-${G.last_dmg} HP`) + col("90", ` (${G.last_dmg_src})`));
	} else {
		lines.push("");
	}
	lines.push(` Fo ${p.focus}/${p.total_max_focus()}`);
	lines.push(" " + bar(p.focus, p.total_max_focus(), 12, "96"));
	lines.push(` Lv ${p.level}  XP ${p.xp}/${p.xp_next()}`);
	lines.push(` Atk ${p.total_atk()}  Def ${p.total_def()}`);
	lines.push(` Eva ${p.total_eva()}  Crt ${p.total_crit()}`);
	lines.push(" " + col("33", `Gold ${p.gold}`));
	lines.push("");
	lines.push("       " + compass_letter(0));
	lines.push("     " + compass_letter(3) + " " + col("90", "+") + " " + compass_letter(1));
	lines.push("       " + compass_letter(2));
	lines.push(` Face ${col("96", FACE_NAME[G.facing])}`);
	lines.push(` Wpn ${gear_label(p.weapon)}`);
	lines.push(` Arm ${gear_label(p.armor)}`);
	lines.push(` Trk ${gear_label(p.trinket)}`);
	var fx_line = " ";
	for (auto fx : p.effects) { fx_line = fx_line + `${fx["kind"]}:${fx["turns"]} `; }
	lines.push(col("95", fx_line));
	return lines;
}

function gear_label(var item) -> string {
	if (item == null) { return col("90", "-"); }
	var short = item.name;
	if (short.size() > 12) { short = short.substr(0, 12); }
	return col(item.color, short);
}

function species_name(string id) -> string {
	for (auto s : SPECIES) { if (s["id"] == id) { return s["name"]; } }
	return id;
}

function class_name(string id) -> string {
	for (auto c : CLASSES) { if (c["id"] == id) { return c["name"]; } }
	return id;
}

array<int> MSG_TURNS = [];  // the turn each G.msgs entry was logged on, newest last

function log_msg(string msg) -> void {
	G.msgs.push(msg);
	MSG_TURNS.push(G.turn);
	if (G.msgs.size() > 60) { G.msgs.remove(0); }
	while (MSG_TURNS.size() > G.msgs.size()) { MSG_TURNS.remove(0); }
}

// ------------------------------------------------------------ modals -------
function help_screen() -> void {
	var lines = [];
	lines.push(col("93", " GRUBWELL - keys"));
	lines.push("");
	lines.push("  step forward  w / up      (into a monster = attack)");
	lines.push("  step back     s / down");
	lines.push("  turn          a / d  (or left / right arrows)");
	lines.push("  sidestep      q / e");
	lines.push("  wait          .");
	lines.push("  minimap       m   (explored tiles + facing arrow)");
	lines.push("  views         v   (forward alone / forward + left, right, behind)");
	lines.push("  on the map    wasd / hjkl / arrows step by compass (no turning;");
	lines.push("                facing follows your step back into first person)");
	lines.push("  pick up       g");
	lines.push("  inventory     i   (letter to inspect, then e=use/equip d=drop)");
	lines.push("  quaff first   p   (first potion in pack)");
	lines.push("  equip best    E   (best upgrade in pack)");
	lines.push("  class skill   z   (fires down your facing)");
	lines.push("  descend       >   (on stairs)");
	lines.push("  character     c");
	lines.push("  save & quit   S");
	lines.push("  quit          Q");
	if (HOST_DEV) { lines.push(col("95", "  hot reload    R   (--dev: re-imports scripts mid-run)")); }
	lines.push("");
	lines.push("  The Well goes down ten floors and gets louder about it. JAI's");
	lines.push("  statues (&) bless your evasion; DEVACOR's murals (%) mend you.");
	lines.push("  Vexadrach squats at the bottom, hoarding the world's stolen color.");
	lines.push("");
	lines.push(col("90", "  any key to return"));
	present(lines);
	read_key();
}

function character_screen() -> void {
	var p = G.player;
	var species_row = null;
	for (auto s : SPECIES) { if (s["id"] == p.species_id) { species_row = s; } }
	var class_row = null;
	for (auto c : CLASSES) { if (c["id"] == p.class_id) { class_row = c; } }
	var lines = [];
	lines.push(col("93", ` ${p.name}`) + `  level ${p.level} ${species_row["name"]} ${class_row["name"]}`);
	lines.push("");
	lines.push(` ${species_row["blurb"]}`);
	lines.push(` Trait: ${species_row["trait_name"]} - ${species_row["trait_blurb"]}`);
	lines.push("");
	lines.push(` ${class_row["blurb"]}`);
	lines.push(` Skill: ${class_row["skill_name"]} (${class_row["skill_cost"]} focus) - ${class_row["skill_blurb"]}`);
	lines.push("");
	lines.push(` HP ${p.hp}/${p.total_max_hp()}   Focus ${p.focus}/${p.total_max_focus()}`);
	lines.push(` Atk ${p.total_atk()}  Def ${p.total_def()}  Eva ${p.total_eva()}  Crit ${p.total_crit()}`);
	lines.push(` Gold ${p.gold}   Kills ${p.kills}   Depth ${G.floor}   Facing ${FACE_NAME[G.facing]}`);
	lines.push("");
	lines.push(col("90", "  any key to return"));
	present(lines);
	read_key();
}

// returns true if a turn was consumed (using/equipping costs a turn)
function inventory_screen() -> bool {
	while (true) {
		var p = G.player;
		var lines = [];
		lines.push(col("93", " Pack") + col("90", `  (${p.inventory.size()}/22)  letter=inspect, esc=back`));
		lines.push("");
		var letters = "abcdefghijklmnopqrstuv";
		for (int i = 0; i < p.inventory.size(); ++i) {
			var it = p.inventory[i];
			lines.push(`  ${letters.at(i)}) ${col(it.color, item_label(it))}${item_stats_label(it)}`);
		}
		if (p.inventory.size() == 0) { lines.push(col("90", "  (lint, mostly)")); }
		present(lines);
		var key = read_key();
		if (key == "esc" || key == "i" || key == "Q") { return false; }
		var pick_index = letters.find(key);
		if (pick_index < 0 || pick_index >= p.inventory.size()) { continue; }
		var chosen = p.inventory[pick_index];
		var verb = chosen.kind == "potion" ? "quaff" : (chosen.kind == "scroll" ? "read" : "equip");
		var sub = [];
		sub.push(` ${col(chosen.color, item_label(chosen))}${item_stats_label(chosen)}`);
		sub.push(col("90", ` rarity: ${chosen.rarity}`));
		sub.push("");
		sub.push(`  e) ${verb}`);
		sub.push("  d) drop");
		sub.push(col("90", "  esc) back"));
		present(sub);
		var act = read_key();
		if (act == "e") { equip_item(G.player, pick_index); return true; }
		if (act == "d") { drop_item(G.player, pick_index); return true; }
	}
	return false;
}

function level_up_screen() -> void {
	var p = G.player;
	while (G.level_pending) {
		var lines = [];
		lines.push(col("93", ` LEVEL ${p.level}!`) + "  Pick a boon:");
		lines.push("");
		lines.push("  1) Vigor   +4 max HP (and heal 4)");
		lines.push("  2) Might   +1 attack");
		lines.push("  3) Guard   +1 defense");
		lines.push("  4) Wits    +3 max focus");
		present(lines);
		var key = G.autopilot ? to_string(1 + RNG.next(4)) : read_key();
		switch (key) {
			case "1": G.player.max_hp = G.player.max_hp + 4; heal_entity(G.player, 4); G.level_pending = false; break;
			case "2": G.player.atk = G.player.atk + 1; G.level_pending = false; break;
			case "3": G.player.def = G.player.def + 1; G.level_pending = false; break;
			case "4": G.player.max_focus = G.player.max_focus + 3; G.player.focus = G.player.focus + 3; G.level_pending = false; break;
			case "Q": G.over = true; G.death_cause = "quit"; return;
		}
	}
}

function death_screen() -> void {
	var p = G.player;
	var lines = [];
	lines.push("");
	lines.push(col("91", "   Y O U   H A V E   F A L L E N"));
	lines.push("");
	lines.push(`   Slain by ${G.death_cause} on floor ${G.floor} of the Well.`);
	lines.push(`   ${p.name}, level ${p.level} ${species_name(p.species_id)} ${class_name(p.class_id)}`);
	lines.push(`   ${p.kills} kills, ${p.gold} gold, ${G.turn} turns.`);
	lines.push("");
	lines.push("   " + col("90", pick(RNG, DEATH_QUIPS)));
	lines.push("");
	lines.push(col("90", "   any key to exit"));
	present(lines);
	if (!G.autopilot && !HOST_SCRIPTED) { read_key(); }
}

var RAINBOW_CODES = [196, 208, 226, 46, 51, 99, 201];

function rainbow_text(string text) -> string {
	var out = "";
	for (int i = 0; i < text.size(); ++i) {
		var c = text.at(i);
		if (c == ' ') { out = out + " "; continue; }
		out = out + col256(RAINBOW_CODES[i % RAINBOW_CODES.size()], to_string(c));
	}
	return out;
}

// The stolen fire comes home. The camera pulls up out of the Well for the
// epilogue: expanding rings of cycling color across the whole arena map.
coroutine string rainbow_frames(int bx, int by) {
	for (int frame = 0; frame < 26; ++frame) {
		var lines = [];
		lines.push("");
		for (int y = 0; y < MAP_H; ++y) {
			var row = "  ";
			for (int x = 0; x < MAP_W; ++x) {
				var d = cheb(x, y, bx, by);
				if (!is_walkable(x, y)) { row = row + " "; continue; }
				if (d <= frame) {
					var code = RAINBOW_CODES[(d + frame) % RAINBOW_CODES.size()];
					row = row + col256(code, "*");
				} else {
					row = row + " ";
				}
			}
			lines.push(row);
		}
		lines.push("");
		lines.push("  " + rainbow_text("THE RAINBOW FIRE REMEMBERS ITS MAKER"));
		var line_end = eol() + "\n";
		yield lines.join(line_end) + line_end;
	}
	return "";
}

function victory_ceremony(int bx, int by) -> void {
	for (auto frame : rainbow_frames(bx, by)) {
		if (frame == "") { continue; }
		draw(frame + ESC + "[J");
		if (!G.smoke && !HOST_SCRIPTED) { time::sleep_ms(55); }
	}
}

function victory_screen() -> void {
	var p = G.player;
	var lines = [];
	lines.push("");
	lines.push("   " + rainbow_text("V I C T O R Y"));
	lines.push("");
	lines.push("   Vexadrach unravels. The swallowed fire pours out of him in seven");
	lines.push("   colors and climbs the Well like sunrise practicing.");
	lines.push("");
	lines.push(`   ${p.name}, level ${p.level} ${species_name(p.species_id)} ${class_name(p.class_id)}`);
	lines.push(`   ${p.kills} kills, ${p.gold} gold, ${G.turn} turns, 10 floors.`);
	lines.push("");
	lines.push("   " + col("93", "Somewhere above, a stone jackodile grins approvingly."));
	lines.push("");
	lines.push(col("90", "   any key to exit"));
	present(lines);
	if (!G.autopilot && !HOST_SCRIPTED) { read_key(); }
}
