// The showcase look's feel: what the game does on screen when something
// happens. Feel watches the game from outside, frame by frame: it compares what
// it sees with what it saw (a shot spent, an enemy flinching, ammo gone, a door
// open) and answers with light sources, screen shake, view bob, decals on the
// floor, hit markers, screen tints and the iris and title card that open a
// floor. It reads the simulation and never writes to it, so the reference
// frames and STATE_HASH never notice it. (The reference's sound cues feed only
// its audio; the port runs muted and leaves them out.)

// a light that outlives the particle that made it, a blast's afterglow: [x, y, channel, age]

class Feel {
	yaw = 0.0;            // shake: a turn of the view, radians
	pitch = 0;            // bob and shake: pixel rows the horizon moves
	sources = null;       // this frame's lights over the bake

	last_tick = 0;
	last_map_ticks = 0;
	last_hp = 100;
	last_armor = 0;
	last_ammo = null;
	last_key_r = false;
	last_key_b = false;
	last_secrets = 0;
	last_bob = 0.0;
	bob_amp = 0.0;
	e_flash = null;
	e_alive = null;
	shot_kind = null;
	shot_x = null;
	shot_y = null;
	blasts = null;
	trauma = 0.0;
	hurt_t = 0;
	pickup_t = 0;
	secret_t = 0;
	blast_t = 0;
	hit_t = 0;
	hit_kill = false;
	mask_w = 0;
	mask_h = 0;
	mask_px = null;       // vignette: the pixels it darkens, and how much (0..4)
	mask_level = null;
	mask_all = null;      // every pixel's level, for the full-screen tints
	remaps = null;        // [tint * 5 + level]: palette index -> tinted index, -1 not yet made
	card_shades = null;

	constructor() {
		sources = [];
		last_ammo = [48, 0, 0];
		e_flash = [];
		e_alive = [];
		shot_kind = [];
		shot_x = [];
		shot_y = [];
		blasts = [];
		mask_px = [];
		mask_level = [];
		mask_all = [];
		remaps = [];
		card_shades = [];
	}

	// once per frame, before the view draws
	function frame() {
		local g = ::G;
		local ticks = g.tick - last_tick;
		last_tick = g.tick;
		if (ticks < 0) { ticks = 0; }
		if (g.mode != 1) { return; }
		if (g.map_ticks < last_map_ticks || g.enemies.len() != e_alive.len()) { new_floor(); }
		last_map_ticks = g.map_ticks;
		for (local i = 0; i < ticks; ++i) { age_one_tick(); }
		watch_player(ticks);
		watch_enemies();
		watch_shots();
		watch_world();
		stain_landed();
		::dry_stains(g.tick);
		gather_lights();
		camera();
	}

	// after the view, the gun and the particles: tints, the vignette, the hit marker, the floor's opening
	function overlay(vw, vh) {
		if (mask_w != vw || mask_h != vh) { build_mask(vw, vh); }
		local level0 = 0;
		local tint = -1;
		if (blast_t > 0) { tint = 4; level0 = ::imin(3, blast_t); }
		else if (hurt_t > 0) { tint = 1; level0 = ::imin(3, hurt_t / 2 + 1); }
		else if (pickup_t > 0) { tint = 2; level0 = ::imin(2, pickup_t / 2); }
		else if (secret_t > 0) { tint = 3; level0 = ::imin(2, secret_t / 3); }
		else if (::G.hp <= 25 && (::G.tick % 24) < 5) { tint = 1; level0 = 1; }
		shade_pass(0, 0);
		if (tint >= 0) { shade_pass(tint, level0); }
		draw_hit_marker(vw);
		opening(vw, vh);
	}

	function kick(amount) { trauma = ::fclamp(trauma + amount, 0.0, 1.0); }

	// --------------------------------------------------------------- watching
	function new_floor() {
		local g = ::G;
		e_flash = [];
		e_alive = [];
		foreach (e in g.enemies) {
			e_flash.push(e.flash);
			e_alive.push(e.alive);
		}
		shot_kind = [];
		shot_x = [];
		shot_y = [];
		blasts = [];
		last_secrets = g.secrets;
		last_hp = g.hp;
		last_armor = g.armor;
		last_ammo = clone g.ammo;
		card_shades = [];
		trauma = 0.0;
		hurt_t = 0;
		pickup_t = 0;
		secret_t = 0;
		blast_t = 0;
		hit_t = 0;
	}

	function age_one_tick() {
		trauma = trauma * 0.86;
		if (trauma < 0.01) { trauma = 0.0; }
		if (hurt_t > 0) { hurt_t = hurt_t - 1; }
		if (pickup_t > 0) { pickup_t = pickup_t - 1; }
		if (secret_t > 0) { secret_t = secret_t - 1; }
		if (blast_t > 0) { blast_t = blast_t - 1; }
		if (hit_t > 0) { hit_t = hit_t - 1; }
		local kept = [];
		foreach (b in blasts) {
			b[3] = b[3] + 1;
			if (b[3] < 14) { kept.push(b); }
		}
		blasts = kept;
	}

	// everything that fades does so per tick, so the feel is the same at any frame rate
	function watch_player(ticks) {
		local g = ::G;
		local lost = (last_hp - g.hp) + (last_armor - g.armor);
		if (g.hp < last_hp) {
			hurt_t = 8;
			kick(0.25 + lost / 30.0);
		}
		for (local pool = 0; pool < 3; ++pool) {
			if (g.ammo[pool] < last_ammo[pool]) {
				local weapon = g.weapon;
				if (weapon == 0) { kick(0.10); }
				else if (weapon == 1) { kick(0.42); }
				else { kick(0.18); }
			} else if (g.ammo[pool] > last_ammo[pool]) {
				pickup_t = 5;
			}
		}
		if ((g.key_r && !last_key_r) || (g.key_b && !last_key_b)) { pickup_t = 6; }
		if (g.hp > last_hp || g.armor > last_armor) { pickup_t = 5; }
		last_hp = g.hp;
		last_armor = g.armor;
		last_ammo = clone g.ammo;
		last_key_r = g.key_r;
		last_key_b = g.key_b;
		local moved = g.bob - last_bob;
		last_bob = g.bob;
		for (local i = 0; i < ticks; ++i) { bob_amp = moved > 0.0 ? ::fclamp(bob_amp + 0.25, 0.0, 1.0) : bob_amp * 0.8; }
	}

	function watch_enemies() {
		local enemies = ::G.enemies;
		for (local i = 0; i < enemies.len(); ++i) {
			local e = enemies[i];
			if (e.flash > e_flash[i] && e_alive[i]) {
				hit_t = 6;
				hit_kill = false;
			}
			if (e_alive[i] && !e.alive) {
				hit_t = 9;
				hit_kill = true;
				kick(e.kind == 3 ? 0.6 : 0.12);
				::stain_floor(e.x, e.y, e.kind == 2 ? 0.0 : 0.34, ::TONE_BLOOD, 170);
			}
			e_flash[i] = e.flash;
			e_alive[i] = e.alive;
		}
	}

	// a shot gone since the last frame burst somewhere near where it was
	function watch_shots() {
		local g = ::G;
		local shots = g.shots;    // [kind, x, y, vx, vy, ttl]
		local matched = ::array(shots.len(), false);
		for (local i = 0; i < shot_kind.len(); ++i) {
			local found = false;
			for (local j = 0; j < shots.len(); ++j) {
				local s = shots[j];
				if (!matched[j] && s[0] == shot_kind[i] && ::dist2(s[1], s[2], shot_x[i], shot_y[i]) < 1.2) {
					matched[j] = true;
					found = true;
					break;
				}
			}
			if (found) { continue; }
			local kind = shot_kind[i];
			if (kind == 1) { continue; }
			blasts.push([shot_x[i], shot_y[i], kind == 3 ? ::LIGHT_HEX : ::LIGHT_FIRE, 0]);
			::stain_floor(shot_x[i], shot_y[i], 0.55, ::TONE_SCORCH, 190);
			::stain_floor(shot_x[i], shot_y[i], 0.8, ::TONE_ASH, 40);
			local d = ::sqrt(::dist2(shot_x[i], shot_y[i], g.px, g.py));
			if (d < 6.0) {
				kick(0.75 * (1.0 - d / 6.0) + 0.1);
				blast_t = ::imax(blast_t, d < 3.0 ? 3 : 2);
			}
		}
		shot_kind = [];
		shot_x = [];
		shot_y = [];
		foreach (s in shots) {
			shot_kind.push(s[0]);
			shot_x.push(s[1]);
			shot_y.push(s[2]);
		}
	}

	function watch_world() {
		local g = ::G;
		if (g.secrets > last_secrets) { secret_t = 9; }
		last_secrets = g.secrets;
	}

	// blood and meat that reached the floor leave their mark
	function stain_landed() {
		foreach (p in ::G.fx.pool) {
			local kind = p[0];
			if ((kind == 2 || kind == 3) && p[3] <= 0.021) {
				::stain_floor(p[1], p[2], kind == 3 ? 0.09 : 0.06, ::TONE_BLOOD, 230);
			}
		}
	}

	// ----------------------------------------------------------------- light
	function gather_lights() {
		local g = ::G;
		sources = [];
		if (g.muzzle > 0) {
			local dx = ::cos(g.pang);
			local dy = ::sin(g.pang);
			local channel = g.weapon == 2 ? ::LIGHT_HEX : ::LIGHT_FIRE;
			sources.push(::light_source(g.px + dx * 0.6, g.py + dy * 0.6, channel,
				::MUZZLE_RADIUS * (0.55 + 0.15 * g.muzzle), 1.0));
		}
		local shot_light = [0, ::LIGHT_ACID, ::LIGHT_FIRE, ::LIGHT_HEX];
		foreach (s in g.shots) { sources.push(::light_source(s[1], s[2], shot_light[s[0]], ::SHOT_RADIUS, 0.9)); }
		foreach (b in blasts) {
			local t = 1.0 - b[3] / 14.0;
			sources.push(::light_source(b[0], b[1], b[2], ::BLAST_RADIUS * (0.4 + 0.6 * t), 0.35 + 0.65 * t));
		}
	}

	// ---------------------------------------------------------------- camera
	// shake is trauma squared, so small knocks barely move the view and big ones rattle it
	function camera() {
		local g = ::G;
		local shake = trauma * trauma * ::SHAKE;
		local n1 = (::mix32(g.tick, 101) % 2001) / 1000.0 - 1.0;
		local n2 = (::mix32(g.tick, 202) % 2001) / 1000.0 - 1.0;
		yaw = shake * 0.045 * n1;
		local bob = (::fabs(::cos(g.bob * 0.5)) - 0.5) * 2.0 * ::BOB_PIXELS * bob_amp * ::PIXH / 2.0;
		pitch = (bob + shake * 5.0 * ::PIXH / 2.0 * n2).tointeger();
	}

	// ------------------------------------------------------------- overlays
	function build_mask(vw, vh) {
		mask_w = vw;
		mask_h = vh;
		mask_px = [];
		mask_level = [];
		mask_all = [];
		local cx = vw / 2.0;
		local cy = vh / 2.0;
		for (local y = 0; y < vh; ++y) {
			for (local x = 0; x < vw; ++x) {
				local dx = (x + 0.5 - cx) / cx;
				local dy = (y + 0.5 - cy) / cy;
				local e = dx * dx * 0.8 + dy * dy;
				local level = ::iclamp(((e - 0.5) * 3.2 + ::BAYER4[(y & 3) * 4 + (x & 3)]).tointeger(), 0, 4);
				mask_all.push(level);
				if (level > 0) {
					mask_px.push(y * vw + x);
					mask_level.push(level);
				}
			}
		}
	}

	// tint 0 darkens the corners; the others wash the screen from the edges in: 1 hurt, 2 pickup, 3 secret, 4 blast
	function shade_pass(tint, level0) {
		while (remaps.len() < 25) { remaps.push([]); }
		local pal = ::PAL_RGB.len();
		for (local level = 1; level <= 4; ++level) {
			local table = remaps[tint * 5 + level];
			while (table.len() < pal) { table.push(-1); }
		}
		local pix = ::G.pix;
		if (tint == 0) {
			if (::VIGNETTE <= 0.0) { return; }
			for (local k = 0; k < mask_px.len(); ++k) {
				local i = mask_px[k];
				pix[i] = remapped(0, mask_level[k], pix[i]);
			}
			return;
		}
		for (local i = 0; i < pix.len(); ++i) {
			local level = ::imin(4, mask_all[i] + level0);
			if (level > 0) { pix[i] = remapped(tint, level, pix[i]); }
		}
	}

	function remapped(tint, level, p) {
		local table = remaps[tint * 5 + level];
		local m = table[p];
		if (m >= 0) { return m; }
		local key = ::PAL_RGB[p];
		local r = (key >> 16) & 255;
		local gr = (key >> 8) & 255;
		local b = key & 255;
		local a = level / 4.0;
		if (tint == 0) {
			local f = 1.0 - ::VIGNETTE * a;
			m = ::rgb_idx((r * f).tointeger(), (gr * f).tointeger(), (b * f).tointeger());
		} else if (tint == 1) {
			m = ::rgb_idx((r + (255.0 - r) * a * 0.55).tointeger(), (gr * (1.0 - a * 0.65)).tointeger(), (b * (1.0 - a * 0.65)).tointeger());
		} else {
			local toward = [[0.0, 0.0, 0.0], [0.0, 0.0, 0.0], [255.0, 222.0, 120.0], [120.0, 255.0, 230.0],
				[255.0, 236.0, 200.0]];
			local k = tint == 4 ? a * 0.55 : a * 0.32;
			m = ::rgb_idx((r + (toward[tint][0] - r) * k).tointeger(), (gr + (toward[tint][1] - gr) * k).tointeger(),
				(b + (toward[tint][2] - b) * k).tointeger());
		}
		table[p] = m;
		return m;
	}

	// four ticks around the aim point: white on a hit, red and wider on a kill
	function draw_hit_marker(vw) {
		local g = ::G;
		if (hit_t <= 0 || g.automap) { return; }
		local col = hit_kill ? ::rgb_idx(255, 64, 48) : ::rgb_idx(255, 250, 236);
		local cx = vw / 2;
		local cy = g.horizon;
		local gap = (hit_kill ? 3 : 2) * ::PIXW;
		local len = 2 * ::PIXW;
		local pix = g.pix;
		local vh = g.vh;
		for (local k = 0; k < len; ++k) {
			local ox = (gap + k);
			local oy = (gap + k) * ::PIXH / ::PIXW / 2 + 1;
			for (local s = 0; s < 4; ++s) {
				local x = cx + ((s & 1) == 0 ? ox : -ox);
				local y = cy + ((s & 2) == 0 ? oy : -oy);
				if (x >= 0 && x < vw && y >= 0 && y < vh) { pix[y * vw + x] = col; }
			}
		}
	}

	// a floor opens with an iris from the center, then its name, which fades into the dark
	function opening(vw, vh) {
		local g = ::G;
		local t = g.map_ticks;
		if (t >= 80) { return; }
		local pix = g.pix;
		if (t < 16) {
			// in cell widths: a pixel is 1 / PIXW of a cell across and 2 / PIXH down
			local cx = vw / 2.0;
			local cy = vh / 2.0;
			local sx = 1.0 / ::PIXW;
			local sy = 2.0 / ::PIXH;
			local r = (t / 16.0) * ::sqrt(cx * sx * cx * sx + cy * sy * cy * sy) * 1.02;
			local r2 = r * r;
			for (local y = 0; y < vh; ++y) {
				local dy = (y + 0.5 - cy) * sy;
				local row = y * vw;
				for (local x = 0; x < vw; ++x) {
					local dx = (x + 0.5 - cx) * sx;
					if (dx * dx + dy * dy > r2) { pix[row + x] = 0; }
				}
			}
		}
		if (t < 6 || g.automap) { return; }
		if (card_shades.len() == 0) { build_card_shades(); }
		local fade = t < 60 ? 0 : (t - 60) / 4;
		if (fade >= 5) { return; }
		local name = g.map_name;
		local code = name.slice(0, 4);
		local title = name.slice(6);
		// a font pixel spans sx view pixels across and sy down, so letters stay square on 1:2 cells
		local sx2 = ::imax(1, ::PIXW * 2 / ::PIXH);
		local scale = 3;
		while (scale > 1 && ::lettering_width(title, scale) * sx2 > vw - 8) { scale = scale - 1; }
		local top = vh / 5;
		pix_lettering(code, (vw - ::lettering_width(code, 1) * sx2) / 2, top, 1, sx2, card_shades[fade + 5], card_shades[10]);
		pix_lettering(title, (vw - ::lettering_width(title, scale) * sx2) / 2, top + 8, scale, sx2, card_shades[fade],
			card_shades[10]);
	}

	function build_card_shades() {
		local mood = ::MOODS[::G.map_i];
		local bone = ::rgb(250, 240, 220);
		card_shades = [];
		for (local i = 0; i < 5; ++i) { card_shades.push(::color_idx(::mix_rgb(bone, mood.fog, i / 5.0))); }
		for (local i = 0; i < 5; ++i) { card_shades.push(::color_idx(::mix_rgb(mood.light, mood.fog, i / 5.0))); }
		card_shades.push(::rgb_idx(4, 2, 4));
	}

	// the 3x5 font straight into the view's pixels, a font pixel scale rows tall and scale * sx columns wide
	function pix_lettering(s, x, y, scale, sx, col, shadow) {
		local g = ::G;
		local pix = g.pix;
		local vw = g.vw;
		local vh = g.vh;
		local sy = scale;
		local cw = sx * scale;
		local font = ::FONT5;
		for (local pass = 0; pass < 2; ++pass) {
			local ink = pass == 0 ? shadow : col;
			local off = pass == 0 ? 1 : 0;
			for (local i = 0; i < s.len(); ++i) {
				local ch = s[i];
				if (!(ch in font)) { continue; }
				local glyph_rows = font[ch];
				local gx = x + i * 4 * cw;
				for (local ry = 0; ry < 5; ++ry) {
					for (local rx = 0; rx < 3; ++rx) {
						if (glyph_rows[ry][rx] != '#') { continue; }
						for (local yy = 0; yy < sy; ++yy) {
							local py = y + (ry + off) * sy + yy;
							if (py < 0 || py >= vh) { continue; }
							for (local xx = 0; xx < cw; ++xx) {
								local qx = gx + (rx + off) * cw + xx;
								if (qx >= 0 && qx < vw) { pix[py * vw + qx] = ink; }
							}
						}
					}
				}
			}
		}
	}
}
