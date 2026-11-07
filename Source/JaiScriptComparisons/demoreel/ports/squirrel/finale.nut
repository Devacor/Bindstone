// FINALE (Squirrel port of scenes/finale.jai): the fire scene with the word
// JAISCRIPT stamped as heat sources, burning rainbow; then the fire dies, the
// letters cool to a neon rainbow and the credits roll up beneath them.

class Finale extends Fire {
	letter_at = null;
	letter_of = null;
	rim_at = null;
	fuel = null;
	word_x = 0;
	word_w = 0;
	word_top = 0;
	word_rows = 0;
	ignite_t = 2.2;
	rainbow_t = 4.5;
	wall_out_t = 10.5;
	embers_t = 11.5;
	neon_t = 14.0;
	credits_t = 13.0;
	credit_rows_per_s = 5.0;
	credits = null;

	constructor() {
		base.constructor();
		letter_at = [];
		letter_of = [];
		rim_at = [];
		fuel = [];
		credits = [
			["title", "THE JAISCRIPT DEMOREEL", ""],
			["", "", ""],
			["body", "seventeen scenes . five transitions", ""],
			["body", "every pixel computed by the jaiscript vm", ""],
			["", "", ""],
			["role", "PLASMA", "four sine fields"],
			["role", "STARFIELD", "a warp run through a nebula"],
			["role", "TUNNEL", "a table-driven texture tunnel"],
			["role", "DONUT", "a sphere-traced torus"],
			["role", "JULIA", "a morphing julia set"],
			["role", "LAVALAMP", "lit metaballs"],
			["role", "PIPEDREAM", "the 3d pipes screensaver"],
			["role", "POWDER", "sand, water, ember and plant"],
			["role", "BOIDS", "a murmuration and a hawk"],
			["role", "KINSTEIN 3D", "a textured raycaster"],
			["role", "CANYONRUN", "voxelspace terrain"],
			["role", "SPONGEWORKS", "a raymarched menger sponge"],
			["role", "BANNERFALL", "verlet cloth in a gale"],
			["role", "CHROME", "a ray tracer"],
			["role", "FIRE", "the doom fire, in rainbow"],
			["role", "OSSUARY", "a skull spider on a morphing gyroid"],
			["role", "FINALE", "you are here"],
			["", "", ""],
			["body", "half-block pixels, two to a cell", ""],
			["body", "deterministic to the last byte", ""],
			["", "", ""],
			["", "", ""],
			["title", "thanks for watching", ""],
			["link", "jaiscript.com", ""]
		];
	}

	function build() {
		name = "FINALE";
		handle = "jaiscript forever . rainbow-fire salute";
		build_fire();
		fuel = ::gfx.make_grid(w);
		stamp_word("JAISCRIPT");
	}

	// the 5x7 font at double size, its second column kept single so a glyph is
	// 9 px wide on a 10 px pitch; narrow screens get it at single size
	function stamp_word(text) {
		local big = w >= 95;
		local font_col = big ? [0, 0, 1, 2, 2, 3, 3, 4, 4] : [0, 1, 2, 3, 4];
		local sy = big ? 2 : 1;
		local pitch = font_col.len() + 1;
		word_w = text.len() * pitch - 1;
		word_x = (w - word_w) / 2;
		word_rows = 7 * sy;
		word_top = h / 2 - word_rows - 2;
		local mask = ::gfx.make_grid(w * h);
		for (local li = 0; li < text.len(); ++li) {
			local bits = ::FONT5[text.slice(li, li + 1)];
			for (local ry = 0; ry < word_rows; ++ry) {
				local b = bits[ry / sy];
				for (local rx = 0; rx < font_col.len(); ++rx) {
					if (((b >> (4 - font_col[rx])) & 1) == 1) {
						local i = (word_top + ry) * w + word_x + li * pitch + rx;
						mask[i] = 1;
						letter_at.push(i);
						letter_of.push(li);
					}
				}
			}
		}
		for (local i = w; i < w * (h - 1); ++i) {
			if (mask[i] == 0 && (mask[i - 1] == 1 || mask[i + 1] == 1 || mask[i - w] == 1 || mask[i + w] == 1)) { rim_at.push(i); }
		}
	}

	function update(t, dt) {
		tt = t;
		fr = fr + 1;
		rainbow_on = t > rainbow_t ? 1 : 0;
		if (rainbow_on == 1) { hue_t += dt * 3.0; }
		local amp = t > wall_out_t ? (42.0 - (t - wall_out_t) * 19.0).tointeger() : 42;
		amp = amp < 0 ? 0 : amp;
		seed_rows(amp, amp / 2 + 2);
		convect();
		if (t > ignite_t) { burn_letters(t); }
		move_sparks(dt, amp > 12);
	}

	// each column's fuel swells and ebbs where two waves cross, and the air
	// above the word cools fast, so the flames rise as separate tongues; once
	// the letters have cooled, the last embers anywhere go out the same way
	function burn_letters(t) {
		local strength = t < embers_t ? 1.0 : (t > neon_t ? 0.0 : (neon_t - t) / (neon_t - embers_t));
		for (local x = 0; x < w; ++x) {
			local wave = ::sin(x * 0.61 + t * 6.1) + ::sin(x * 0.23 - t * 3.7 + 1.7);
			fuel[x] = ((46.0 + wave * 15.0) * strength).tointeger();
		}
		local ht = heat;
		local rn = cool;
		local at = letter_at;
		local n = at.len();
		for (local k = 0; k < n; ++k) {
			local i = at[k];
			local v = fuel[i % w] + (rn[(i * 7 + fr) & 1023] & 1) * 8;
			if (ht[i] < v) { ht[i] = v; }
		}
		local quenched = t > neon_t ? w * h : word_top * w;
		for (local i = 0; i < quenched; ++i) {
			local v = ht[i] - 2;
			ht[i] = v > 0 ? v : 0;
		}
	}

	// the letters burn white-hot over their own flames, ringed by a dark rim;
	// once the fire is out each cools to its own hue of the rainbow, and a
	// glint sweeps across the word every few seconds
	function render(grid) {
		local b = bank();
		local n = w * h;
		local ht = heat;
		for (local i = 0; i < n; ++i) {
			local v = ht[i];
			grid[i] = (v > 72 ? 72 : v) + b;
		}
		if (tt > ignite_t) {
			local rim = rim_at;
			for (local k = 0; k < rim.len(); ++k) {
				local i = rim[k];
				grid[i] = b + ht[i] / 3;
			}
			draw_letters(grid, b);
		}
		draw_sparks(grid, b);
	}

	function draw_letters(grid, b) {
		local at = letter_at;
		local n = at.len();
		if (tt < embers_t) {
			for (local k = 0; k < n; ++k) {
				local i = at[k];
				grid[i] = b + 64 + ((i * 7 + fr) & 7);
			}
			return;
		}
		local cooled = tt > neon_t ? 1.0 : (tt - embers_t) / (neon_t - embers_t);
		local level = 64 - (cooled * 12.0).tointeger();
		local roll = (tt * 2.0).tointeger();
		local glint = tt > neon_t ? ((tt - neon_t) * 60.0).tointeger() % (word_w + 160) - 20 : -20;
		for (local k = 0; k < n; ++k) {
			local i = at[k];
			local d = i % w - word_x + i / w - word_top - glint;
			local lv = d >= 0 && d < 6 ? 72 - d : level + ((i * 7 + fr) & 3);
			grid[i] = levels + ((letter_of[k] + roll) % phases) * levels + lv;
		}
	}

	// the credits roll up from the bottom row, under the word, and stop with the
	// last lines resting midway; rows near the edges of the band dim
	function text_rows(rows) {
		if (tt < credits_t) { return; }
		local top = (word_top + word_rows) / 2 + 2;
		local bottom = h / 2 - 2;
		if (bottom - top < 2) { return; }
		local last = credits.len() - 1;
		local rest = bottom + last - (top + bottom) / 2;
		local scroll = ((tt - credits_t) * credit_rows_per_s).tointeger();
		scroll = scroll > rest ? rest : scroll;
		for (local k = 0; k <= last; ++k) {
			local row = bottom + 1 + k - scroll;
			if (row < top || row > bottom) { continue; }
			local line = credits[k];
			if (line[0] == "") { continue; }
			local edge = row - top < bottom - row ? row - top : bottom - row;
			rows[row] = credit_row(line, edge >= 2 ? 0 : 2 - edge);
		}
	}

	function credit_row(line, dim) {
		local kind = line[0];
		local left = line[1];
		local right = line[2];
		local result = ::ESC + "[48;2;0;0;0m";
		if (kind == "role") {
			local gutter = w / 2;
			left = left.len() > gutter - 2 ? left.slice(0, gutter - 2) : left;
			right = right.len() > w - gutter - 1 ? right.slice(0, w - gutter - 1) : right;
			result += ::rep(" ", gutter - 1 - left.len()) + credit_ink(255, 206, 110, dim) + left
				+ "  " + credit_ink(186, 180, 236, dim) + right + ::rep(" ", w - gutter - 1 - right.len());
		} else {
			left = left.len() > w ? left.slice(0, w) : left;
			local pad = (w - left.len()) / 2;
			local ink = credit_ink(214, 208, 255, dim);
			if (kind == "title") { ink = credit_ink(255, 236, 190, dim); }
			if (kind == "link") { ink = credit_ink(120, 200, 255, dim); }
			result += ::rep(" ", pad) + ink + left + ::rep(" ", w - pad - left.len());
		}
		return result + ::RESET;
	}

	function credit_ink(r, g, b, dim) {
		return ::ESC + "[38;2;" + (r * (3 - dim) / 3) + ";" + (g * (3 - dim) / 3) + ";" + (b * (3 - dim) / 3) + "m";
	}
}
