// The showcase look's status bar, panel and full-screen pages (title, floor
// tally, death, victory), painted on half-block canvases. hud.nut keeps the
// reference look's versions, the frames --smoke hashes.

// the face in the panel: 10x10 pixel art, one per expression (face_name)
::FACE_PAL <- {h = [58, 40, 30], s = [214, 160, 120], S = [156, 106, 78], e = [240, 236, 226], p = [34, 30, 40],
	B = [70, 44, 30], m = [96, 30, 30], t = [236, 228, 210], b = [186, 26, 22], g = [150, 150, 140], x = [40, 20, 20]};
::FACE_SRC <- {
	ok =   ["..hhhhhh..", ".hhhhhhhh.", ".hsssssshh", ".sBBssBBsh", ".sepssepsS",
	        ".sssSsssss", ".ssSSSssS.", ".ssmmmmsS.", "..ssssss..", "...SSSS..."],
	hurt = ["..hhhhhh..", ".hhhhhhhh.", ".hsssssshh", ".sBBssBBsh", ".sepssepsS",
	        ".sssSssSSs", ".ssSSSsSS.", ".ssmmmmsS.", "..sSssss..", "...SSSS..."],
	bad =  ["..hhhhhh..", ".hhbhhhhh.", ".hsbsssshh", ".sBBssBBsh", ".sxxsseps.",
	        ".bssSssSSs", ".bsSSSsSS.", ".ssmmmmsS.", "..sSsSss..", "...SSSS..."],
	crit = ["..hhbhhh..", ".hhbbhhhh.", ".hbbsbsshh", ".sBBbsBBsh", ".sxxssxxb.",
	        ".bbsSssSbs", ".bsSSSsbS.", ".sbttttbS.", "..sbsSsb..", "...SbSS..."],
	pain = ["..hhhhhh..", ".hhhhhhhh.", ".hsBBsBBhh", ".sssssssh.", ".sxxssxxs.",
	        ".sssSsssss", ".ssmmmmsS.", ".ssmttmsS.", "..smmmms..", "...SSSS..."],
	grin = ["..hhhhhh..", ".hhhhhhhh.", ".hsssssshh", ".sBBssBBsh", ".sepssepsS",
	        ".sssSsssss", ".smSSSSms.", ".sttttttS.", "..smmmms..", "...SSSS..."],
	dead = ["..hhhhhh..", ".hhhbhhhh.", ".hgggggghh", ".gxgggxgg.", ".ggxggxgg.",
	        ".gxgggxgg.", ".gggSgggg.", ".ggxxxxgb.", "..gggggb..", "...gggb..."]
};

// the logo, 57 x 11 pixels: G L O O M
::LOGO_ART <- [
	"..######....###...........######......######....###....###",
	".########...###..........########....########...####..####",
	"###....###..###.........###....###..###....###..##.####.##",
	"##..........###.........##......##..##......##..##..##..##",
	"##..........###.........##......##..##......##..##......##",
	"##...#####..###.........##......##..##......##..##......##",
	"##...#####..###.........##......##..##......##..##......##",
	"##......##..###.........##......##..##......##..##......##",
	"###....###..###.........###....###..###....###..##......##",
	".########...#########....########....########...##......##",
	"..######....#########.....######......######....##......##"
];

function ember(i, salt) { return ::mix32(::mix32(i * 2654435 + 7, salt), 99991); }

function blink(period) { return (::G.tick / period) % 2 == 0; }

class Screens {
	panel = null;          // the six rows under the view
	page = null;           // a whole-screen page
	faces = null;
	face_luts = null;
	// the title's fire
	heat = null;
	fire_colors = null;
	fire_tick = -1;

	constructor(w, h) {
		panel = ::Canvas(w, 6);
		page = ::Canvas(w, h);
		faces = {};
		face_luts = {};
		heat = [];
		fire_colors = [];
		foreach (name, rows in ::FACE_SRC) {
			local art = ::parse_art({pal = ::FACE_PAL, rows = rows});
			faces[name] <- art;
			face_luts[name] <- plain_lut(art);
		}
	}

	function frame(fps, ms_sim, ms_draw) {
		local g = ::G;
		g.feel.frame();
		if (g.mode == 0) { return ::join_arr(title_page(), "\n"); }
		if (g.mode == 2) { return ::join_arr(tally_page(), "\n"); }
		if (g.mode == 3) { return ::join_arr(dead_page(), "\n"); }
		if (g.mode == 4) { return ::join_arr(victory_page(), "\n"); }
		local rows = ::render_view();
		foreach (r in hud_rows(fps, ms_sim, ms_draw)) { rows.push(r); }
		return ::join_arr(rows, "\n");
	}

	// a code -> palette index table for an art, unshaded
	function plain_lut(a) {
		local lut = [0];
		local rgbs = a.rgb;
		for (local c = 1; c < rgbs.len(); ++c) { lut.push(::rgb_idx(rgbs[c][0], rgbs[c][1], rgbs[c][2])); }
		return lut;
	}

	// five font-row colors from top to bottom
	function shades5(top, bottom) {
		local out = [];
		for (local i = 0; i < 5; ++i) { out.push(::color_idx(::mix_rgb(top, bottom, i / 4.0))); }
		return out;
	}

	// the status-bar color of a message (show_msg takes an SGR color code)
	function message_color(code) {
		if (code == "91") { return ::rgb_idx(255, 96, 80); }
		if (code == "92") { return ::rgb_idx(120, 236, 120); }
		if (code == "93") { return ::rgb_idx(255, 214, 90); }
		if (code == "94") { return ::rgb_idx(110, 160, 255); }
		if (code == "96") { return ::rgb_idx(110, 230, 230); }
		if (code == "90") { return ::rgb_idx(130, 126, 136); }
		return ::rgb_idx(240, 236, 228);
	}

	// ---------------------------------------------------------------- HUD ----
	// HUD rows under the view: the status bar, then health, armor, the face, the
	// weapon's ammo and the arms rack with keycards
	function hud_rows(fps, ms_sim, ms_draw) {
		local g = ::G;
		local c = panel;
		local w = c.w;
		local mood = ::MOODS[g.map_i];
		c.clear(::rgb_idx(14, 12, 18));
		c.gradient(2, 12, ::rgb(62, 58, 70), ::rgb(22, 20, 26));
		c.rect(0, 11, w, 1, ::rgb_idx(10, 8, 12));

		// status bar: where, how it is going, what just happened
		local dim = ::rgb_idx(132, 124, 136);
		local map_code = g.map_name.slice(0, 4);
		local map_title = g.map_name.slice(6);
		c.text(1, 0, map_code, ::color_idx(mood.light));
		c.text(6, 0, map_title, ::rgb_idx(214, 206, 196));
		local tally = "KILLS " + g.kills + "/" + g.kill_total + "   SECRETS " + g.secrets + "/" + g.secret_total;
		local tally_x = 7 + map_title.len() + 3;
		c.text(tally_x, 0, tally, dim);
		if (g.msg_t > 0) {
			local room = w - (tally_x + tally.len()) - 3;
			local m = g.msg.len() > room ? g.msg.slice(0, ::imax(0, room)) : g.msg;
			c.text_right(w - 1, 0, m, message_color(g.msg_color));
		}

		local stat_w = ::iclamp((w - 33) / 3, 12, 17);
		local x_hp = 1;
		local x_armor = x_hp + stat_w + 1;
		local x_face = x_armor + stat_w + 1;
		local x_ammo = x_face + 13;
		local x_arms = x_ammo + stat_w + 1;
		local sep = ::rgb_idx(14, 12, 18);
		foreach (x in [x_armor - 1, x_face - 1, x_ammo - 1, x_arms - 1]) { c.rect(x, 3, 1, 8, sep); }

		local red_digits = shades5(::rgb(255, 220, 130), ::rgb(214, 64, 40));
		local blue_digits = shades5(::rgb(200, 232, 255), ::rgb(64, 120, 220));
		local gold_digits = shades5(::rgb(255, 244, 196), ::rgb(214, 150, 54));
		local hp_bar = g.hp > 100 ? ::rgb_idx(120, 220, 255) : (g.hp > 60 ? ::rgb_idx(110, 220, 90) : (g.hp > 30 ? ::rgb_idx(240, 190, 60) : ::rgb_idx(240, 60, 40)));
		stat_block(c, x_hp, stat_w, "HEALTH", g.hp, ::imin(g.hp, 100), 100, red_digits, hp_bar);
		stat_block(c, x_armor, stat_w, "ARMOR", g.armor, g.armor, 100, blue_digits, ::rgb_idx(90, 150, 240));

		c.rect(x_face, 2, 12, 10, ::rgb_idx(18, 16, 22));
		local face = ::face_name();
		c.art(faces[face], x_face + 1, 2, face_luts[face]);

		local weapon = ::WEAPONS[g.weapon];
		local pool = weapon.ammo;
		local pool_colors = [::rgb_idx(226, 186, 84), ::rgb_idx(226, 96, 64), ::rgb_idx(90, 230, 220)];
		stat_block(c, x_ammo, stat_w, weapon.name, g.ammo[pool], g.ammo[pool], ::AMMO_MAX[pool], gold_digits, pool_colors[pool]);

		local arms_w = w - x_arms - 1;
		if (arms_w >= 11) {
			c.text(x_arms + 1, 1, "ARMS", dim);
			for (local slot = 0; slot < 3; ++slot) {
				local sx = x_arms + 1 + slot * 4;
				local owned = g.have[slot] == 1;
				if (slot == g.weapon) { c.rect(sx, 4, 3, 2, ::rgb_idx(132, 100, 40)); }
				c.text(sx + 1, 2, "" + (slot + 1), owned ? ::rgb_idx(250, 240, 210) : ::rgb_idx(70, 66, 76));
			}
			if (arms_w >= 26) {
				c.text(x_arms + 14, 2, "B" + g.ammo[0] + " S" + g.ammo[1] + " C" + g.ammo[2], dim);
			}
			key_chip(c, x_arms + 1, 3, "RED", g.key_r, ::rgb_idx(200, 40, 34));
			key_chip(c, x_arms + 7, 3, "BLUE", g.key_b, ::rgb_idx(44, 90, 210));
			if (g.autopilot) { c.text(x_arms + 1, 5, "AUTOPILOT", ::rgb_idx(110, 230, 230)); }
			else if (fps > 0.0) {
				local cores = ::HOST_WORKERS > 0 ? "  " + ::HOST_WORKERS + " cores" : "";
				c.text(x_arms + 1, 5, ::format("%.0f fps %.1f ms", fps, ms_sim + ms_draw) + cores, dim);
			}
		}
		return c.rows(0, 6);
	}

	// a labelled big number with a bar beneath it
	function stat_block(c, x, width, label, value, bar, bar_max, digits, bar_color) {
		c.text(x + 1, 1, label, ::rgb_idx(150, 140, 132));
		local number = "" + value;
		c.lettering(number, x + width - 1 - ::lettering_width(number, 1), 4, 1, digits, ::rgb_idx(16, 12, 14));
		local bar_w = width - 2;
		local filled = ::iclamp(bar * bar_w / ::imax(1, bar_max), 0, bar_w);
		c.rect(x + 1, 10, bar_w, 1, ::rgb_idx(40, 36, 44));
		c.rect(x + 1, 10, filled, 1, bar_color);
	}

	function key_chip(c, x, row, name, held, col) {
		if (held) {
			c.rect(x, row * 2, name.len() + 2, 2, col);
			c.text(x + 1, row, name, ::rgb_idx(255, 255, 255));
		} else {
			c.text(x + 1, row, name, ::rgb_idx(76, 70, 82));
		}
	}

	// -------------------------------------------------------------- pages ----
	// the title's fire: each cell takes the average of the cells under it, a little
	// cooler, over a bottom row of flickering embers; one step a tick, the flicker a
	// hash of the tick, so every run burns alike
	function draw_fire(c, rows) {
		local w = c.w;
		local height = c.h * 2;
		if (fire_colors.len() == 0) {
			local stops = [::rgb(0, 0, 0), ::rgb(40, 6, 6), ::rgb(120, 16, 8), ::rgb(196, 48, 10), ::rgb(236, 110, 20),
				::rgb(250, 176, 50), ::rgb(255, 228, 120), ::rgb(255, 250, 220)];
			for (local i = 0; i < 36; ++i) {
				local t = i / 35.0 * (stops.len() - 1);
				local k = t.tointeger();
				local k2 = ::imin(stops.len() - 1, k + 1);
				fire_colors.push(::color_idx(::mix_rgb(stops[k], stops[k2], t - k)));
			}
		}
		if (heat.len() != w * (rows + 2)) {
			heat = ::array(w * (rows + 2), 0);
			fire_tick = -1;
		}
		local tick = ::G.tick;
		local steps = fire_tick < 0 ? rows * 2 : ::imin(4, tick - fire_tick);
		for (local s = 0; s < steps; ++s) { fire_step(w, rows + 2, (fire_tick < 0 ? 0 : fire_tick) + s); }
		fire_tick = tick;
		local top = height - rows;
		local p = c.px;
		local hh = heat;
		local colors = fire_colors;
		for (local y = 0; y < rows; ++y) {
			for (local x = 0; x < w; ++x) {
				local h = hh[y * w + x] * 35 / 255;
				if (h > 2) { p[(top + y) * w + x] = colors[h]; }
			}
		}
	}

	function fire_step(w, rows, tick) {
		local hh = heat;
		for (local r = rows - 2; r < rows; ++r) {
			for (local x = 0; x < w; ++x) {
				local n = (::mix32(::mix32(tick, x), 17 + r) >> 9) & 255;
				hh[r * w + x] = n < 90 ? 60 : 255;
			}
		}
		local cool = 2 + 230 / ::imax(1, rows);
		for (local y = 0; y < rows - 2; ++y) {
			local row = y * w;
			for (local x = 0; x < w; ++x) {
				local below = row + w + x;
				local left = x > 0 ? hh[below - 1] : hh[below];
				local right = x < w - 1 ? hh[below + 1] : hh[below];
				local h = (left + hh[below] + right + hh[below + w]) / 4 - cool;
				hh[row + x] = h < 0 ? 0 : h;
			}
		}
	}

	// sparks rising from the bottom of the page
	function draw_embers(c, count, hot, cool) {
		local tick = ::G.tick;
		local height = c.h * 2;
		local period = height + 16;
		for (local i = 0; i < count; ++i) {
			local speed = 1 + ::ember(i, 1) % 3;
			local x = ::ember(i, 2) % c.w;
			local rise = (tick * speed / 2 + ::ember(i, 3) % period) % period;
			local y = height - rise;
			if ((::ember(i, tick / 4) & 3) == 0) { continue; }
			local t = 1.0 * rise / period;
			c.dot(x + ((rise / 6 + i) % 3) - 1, y, ::color_idx(::mix_rgb(hot, cool, t)));
		}
	}

	// a heading in the pixel font, centered at pixel row y, at the largest scale that fits
	function heading(c, s, y, max_scale, top_bottom, shadow) {
		local scale = max_scale;
		while (scale > 1 && ::lettering_width(s, scale) > c.w - 4) { scale = scale - 1; }
		c.lettering(s, (c.w - ::lettering_width(s, scale)) / 2, y, scale, top_bottom, shadow);
		return 5 * scale;
	}

	// outlined, shadowed, lit from above; a bleeding logo gathers drops under its
	// letters that fall
	function draw_logo(c, x, y, top, bottom, bleeding) {
		local art = ::LOGO_ART;
		local lw = art[0].len();
		local lh = art.len();
		local outline = ::rgb_idx(6, 2, 4);
		local shadow = ::rgb_idx(40, 6, 8);
		for (local ly = 0; ly < lh; ++ly) {
			for (local lx = 0; lx < lw; ++lx) {
				if (art[ly][lx] != '#') { continue; }
				c.rect(x + lx - 1, y + ly - 1, 3, 3, outline);
			}
		}
		for (local ly = 0; ly < lh; ++ly) {
			for (local lx = 0; lx < lw; ++lx) {
				if (art[ly][lx] == '#') { c.dot(x + lx + 1, y + ly + 2, shadow); }
			}
		}
		for (local ly = 0; ly < lh; ++ly) {
			local fill = ::color_idx(::mix_rgb(top, bottom, 1.0 * ly / (lh - 1)));
			for (local lx = 0; lx < lw; ++lx) {
				if (art[ly][lx] == '#') { c.dot(x + lx, y + ly, fill); }
			}
		}
		if (!bleeding) { return; }
		local blood = ::rgb_idx(150, 14, 12);
		local drop = ::rgb_idx(210, 28, 22);
		for (local lx = 0; lx < lw; ++lx) {
			if (art[lh - 1][lx] != '#' || ::ember(lx, 21) % 4 != 0) { continue; }
			local length = 1 + ::ember(lx, 22) % 4;
			c.rect(x + lx, y + lh, 1, length, blood);
			local fall = (::G.tick + ::ember(lx, 23) % 60) % 60;
			if (fall < 24) { c.dot(x + lx, y + lh + length + fall / 2, drop); }
		}
	}

	function title_page() {
		local g = ::G;
		local c = page;
		c.clear(0);
		local height = c.h * 2;
		local glow_at = height * 3 / 5;
		c.gradient(0, glow_at, ::rgb(6, 5, 9), ::rgb(22, 8, 12));
		c.gradient(glow_at, height, ::rgb(22, 8, 12), ::rgb(84, 26, 12));
		draw_fire(c, height * 2 / 5);
		draw_embers(c, c.w / 2, ::rgb(255, 214, 120), ::rgb(110, 30, 16));

		local flicker = ::ember(g.tick / 3, 5) % 9 == 0;
		local bone = flicker ? ::rgb(190, 180, 160) : ::rgb(248, 238, 216);
		local logo_y = ::imax(2, height / 2 - 24);
		draw_logo(c, (c.w - ::LOGO_ART[0].len()) / 2, logo_y, bone, ::rgb(160, 24, 20), true);
		local row = (logo_y + ::LOGO_ART.len()) / 2 + 4;
		c.text_centered(row, "a terminal raycaster, written in JaiScript", ::rgb_idx(186, 154, 130));
		if (::blink(12)) { c.text_centered(row + 3, "-  press FIRE to descend  -", ::rgb_idx(255, 226, 160)); }
		local keys = ::rgb_idx(250, 214, 150);
		local words = ::rgb_idx(150, 132, 126);
		controls_line(c, row + 6, ["W S", "move", "A D", "strafe", "LEFT RIGHT", "turn", "SHIFT", "run"], keys, words);
		controls_line(c, row + 7, ["SPACE", "fire", "E", "use", "1 2 3", "weapons", "M", "map", "ESC", "quit"], keys, words);
		return c.rows(0, c.h);
	}

	// "key word   key word ..." centered, keys bright, words dim
	function controls_line(c, row, pairs, key_color, word_color) {
		local width = 0;
		for (local i = 0; i < pairs.len(); i += 2) { width += pairs[i].len() + 1 + pairs[i + 1].len() + 4; }
		local x = (c.w - (width - 4)) / 2;
		for (local i = 0; i < pairs.len(); i += 2) {
			c.text(x, row, pairs[i], key_color);
			x += pairs[i].len() + 1;
			c.text(x, row, pairs[i + 1], word_color);
			x += pairs[i + 1].len() + 4;
		}
	}

	function clock_text(ticks) {
		local secs = ticks / 30;
		local pad = secs % 60 < 10 ? "0" : "";
		return "" + (secs / 60) + ":" + pad + (secs % 60);
	}

	// the intermission: the floor's numbers count up, DOOM style
	function tally_page() {
		local g = ::G;
		local c = page;
		c.clear(0);
		local mood = ::MOODS[g.map_i];
		local height = c.h * 2;
		c.gradient(0, height, ::mix_rgb(mood.fog, ::rgb(0, 0, 0), 0.3), ::mix_rgb(mood.ceiling, mood.light, 0.12));
		local gold = shades5(::rgb(255, 246, 200), ::rgb(220, 150, 60));
		local top = ::imax(2, height / 2 - 22);
		c.text_centered(top / 2, g.map_name, ::color_idx(mood.light));
		local head_h = heading(c, "FLOOR CLEARED", top + 4, 2, gold, ::rgb_idx(8, 6, 8));
		local row = (top + 4 + head_h) / 2 + 3;
		local counted = g.mode_t * 2;
		local kills = ::imin(g.kills, counted / 3);
		local secrets = ::imin(g.secrets, counted / 9);
		local time = ::imin(g.map_ticks, counted * 40);
		local kp = g.kill_total > 0 ? kills * 100 / g.kill_total : 100;
		local sp = g.secret_total > 0 ? secrets * 100 / g.secret_total : 100;
		local label = ::rgb_idx(170, 160, 150);
		local value = ::rgb_idx(255, 236, 190);
		local x = c.w / 2 - 17;
		tally_row(c, x, row, "KILLS", "" + kills + " / " + g.kill_total, kp, label, value);
		tally_row(c, x, row + 2, "SECRETS", "" + secrets + " / " + g.secret_total, sp, label, value);
		c.text(x, row + 4, "TIME", label);
		c.text(x + 10, row + 4, clock_text(time), value);
		c.text(x + 18, row + 4, "PAR " + clock_text(g.par), label);
		local done = kills == g.kills && secrets == g.secrets && time == g.map_ticks;
		if (done && ::blink(12)) {
			local next = g.map_i + 1 < ::MAPS.len() ? "press FIRE for the next floor" : "press FIRE to face the dark";
			c.text_centered(row + 8, next, ::rgb_idx(255, 226, 160));
		}
		return c.rows(0, c.h);
	}

	function tally_row(c, x, row, name, count, percent, label, value) {
		c.text(x, row, name, label);
		c.text(x + 10, row, count, value);
		c.text_right(x + 24, row, "" + percent + "%", value);
		c.rect(x + 26, row * 2, 10, 2, ::rgb_idx(40, 34, 40));
		c.rect(x + 26, row * 2, percent / 10, 2, percent == 100 ? ::rgb_idx(120, 230, 110) : ::rgb_idx(226, 176, 70));
	}

	function dead_page() {
		local g = ::G;
		local c = page;
		c.clear(0);
		local height = c.h * 2;
		c.gradient(0, height, ::rgb(36, 4, 6), ::rgb(8, 2, 3));
		// blood runs down from the top, each run at its own pace and to its own depth
		local trail = ::rgb_idx(96, 8, 8);
		local bead = ::rgb_idx(214, 34, 28);
		for (local x = 0; x < c.w; ++x) {
			if (::ember(x, 10) % 3 != 0) { continue; }
			local depth = height / 4 + ::ember(x, 13) % (height / 2);
			local length = ::imin(depth, g.mode_t * (2 + ::ember(x, 11) % 5) / 6 - ::ember(x, 12) % 12);
			for (local y = 0; y < length; ++y) { c.dot(x, y, y == length - 1 ? bead : trail); }
		}
		local red = shades5(::rgb(255, 120, 100), ::rgb(150, 10, 10));
		local top = height / 2 - 14;
		local head_h = heading(c, "YOU DIED", top, 2, red, ::rgb_idx(6, 0, 0));
		local row = (top + head_h) / 2 + 2;
		c.text_centered(row, "slain by " + g.death_cause + " on " + g.map_name, ::rgb_idx(236, 200, 190));
		c.text_centered(row + 2, "kills " + g.kills + "/" + g.kill_total + "    deaths " + g.deaths, ::rgb_idx(170, 130, 126));
		if (g.mode_t > 25 && ::blink(12)) { c.text_centered(row + 5, "press FIRE to rise again", ::rgb_idx(255, 226, 160)); }
		return c.rows(0, c.h);
	}

	function victory_page() {
		local g = ::G;
		local c = page;
		c.clear(0);
		local height = c.h * 2;
		c.gradient(0, height, ::rgb(10, 8, 14), ::rgb(70, 52, 20));
		draw_embers(c, c.w / 3, ::rgb(255, 240, 170), ::rgb(120, 90, 30));
		local top = ::imax(2, height / 2 - 24);
		draw_logo(c, (c.w - ::LOGO_ART[0].len()) / 2, top, ::rgb(255, 250, 220), ::rgb(214, 140, 40), false);
		local row = (top + ::LOGO_ART.len()) / 2 + 4;
		c.text_centered(row, "T H E   W A R D E N   I S   D O W N", ::rgb_idx(255, 230, 160));
		local label = ::rgb_idx(186, 170, 150);
		local x = c.w / 2 - 14;
		c.text(x, row + 3, "EPISODE KILLS", label);
		c.text(x + 18, row + 3, "" + g.ep_kills + " / " + g.ep_kill_total, ::rgb_idx(255, 246, 210));
		c.text(x, row + 4, "SECRETS", label);
		c.text(x + 18, row + 4, "" + g.ep_secrets + " / " + g.ep_secret_total, ::rgb_idx(255, 246, 210));
		c.text(x, row + 5, "TIME", label);
		c.text(x + 18, row + 5, clock_text(g.ep_ticks), ::rgb_idx(255, 246, 210));
		c.text(x, row + 6, "DEATHS", label);
		c.text(x + 18, row + 6, "" + g.deaths, ::rgb_idx(255, 246, 210));
		c.text_centered(row + 9, "the gloom lifts. for now.      ESC quits", ::rgb_idx(170, 150, 120));
		return c.rows(0, c.h);
	}
}
