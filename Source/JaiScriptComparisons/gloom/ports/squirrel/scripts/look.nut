// The showcase look: how GLOOM is lit and colored when it is played. Floor
// moods, wall, floor and ceiling textures, the lit color tables, sprite shading
// and particle colors are all built at boot and on map load; the per-pixel work
// only looks them up. The reference look keeps its own tables (render.nut).
//
// A color is a [r, g, b] array of binary32 values (the reference's math::color;
// its alpha never reaches a channel, so it is not carried). Squirrel has only
// binary64 floats, so every color op rounds its result through a reused blob:
// writen(x, 'f') is a C (float) cast, round to nearest.

::SHOWCASE <- false;   // gloom_boot: HOST_LOOK == "showcase"

::F32_BLOB <- ::blob(4);

function f32(x) {
	local b = ::F32_BLOB;
	b.seek(0);
	b.writen(x, 'f');
	b.seek(0);
	return b.readn('f');
}

function color(r, g, b) { return [::f32(r), ::f32(g), ::f32(b)]; }

function rgb(r, g, b) { return [::f32(r / 255.0), ::f32(g / 255.0), ::f32(b / 255.0)]; }

// color * scalar: the scalar rounds to binary32 first
function cscale(c, s) {
	local k = ::f32(s);
	return [::f32(c[0] * k), ::f32(c[1] * k), ::f32(c[2] * k)];
}

function cadd(a, b) { return [::f32(a[0] + b[0]), ::f32(a[1] + b[1]), ::f32(a[2] + b[2])]; }

function cmul(a, b) { return [::f32(a[0] * b[0]), ::f32(a[1] * b[1]), ::f32(a[2] * b[2])]; }

// math::mix_rgb: a + (b - a) * t in binary64, rounded once
function mix_rgb(a, b, t) {
	return [::f32(a[0] + (b[0] - a[0]) * t), ::f32(a[1] + (b[1] - a[1]) * t), ::f32(a[2] + (b[2] - a[2]) * t)];
}

// math::clamp(c, 0.0, 1.0): exact, no rounding needed
function cclamp01(c) {
	local r = c[0] < 0.0 ? 0.0 : (c[0] > 1.0 ? 1.0 : c[0]);
	local g = c[1] < 0.0 ? 0.0 : (c[1] > 1.0 ? 1.0 : c[1]);
	local b = c[2] < 0.0 ? 0.0 : (c[2] > 1.0 ? 1.0 : c[2]);
	return [r, g, b];
}

function color_idx(c) {
	return ::rgb_idx((c[0] * 255.0 + 0.5).tointeger(), (c[1] * 255.0 + 0.5).tointeger(), (c[2] * 255.0 + 0.5).tointeger());
}

// A floor's lighting: the torchlight that tints what is near, the haze that
// distance fades into, the tint a wall turned away from the light takes, and
// the surfaces themselves.
class Mood {
	light = null;
	fog = null;
	turned = null;
	ceiling = null;
	floor = null;
	walls = null;     // wall kinds 1-4

	constructor(light_, fog_, turned_, ceiling_, floor_, walls_) {
		light = light_;
		fog = fog_;
		turned = turned_;
		ceiling = ceiling_;
		floor = floor_;
		walls = walls_;
	}
}

::MOODS <- [
	// E1M1: torchlit sandstone, violet shadows
	::Mood(::rgb(255, 230, 196), ::rgb(22, 16, 26), ::rgb(120, 110, 170), ::rgb(58, 46, 54), ::rgb(96, 80, 60),
		[::rgb(156, 136, 112), ::rgb(108, 128, 92), ::rgb(104, 110, 136), ::rgb(152, 100, 80)]),
	// E1M2: coolant glow, teal haze
	::Mood(::rgb(190, 236, 255), ::rgb(6, 20, 26), ::rgb(70, 120, 150), ::rgb(26, 46, 56), ::rgb(44, 70, 72),
		[::rgb(100, 140, 152), ::rgb(84, 112, 164), ::rgb(104, 152, 116), ::rgb(136, 114, 152)]),
	// E1M3: furnace heat, ember haze
	::Mood(::rgb(255, 196, 140), ::rgb(34, 12, 8), ::rgb(150, 70, 60), ::rgb(70, 34, 24), ::rgb(86, 60, 42),
		[::rgb(176, 114, 70), ::rgb(144, 86, 118), ::rgb(132, 124, 72), ::rgb(96, 130, 140)]),
	// E1M4: the throne's red dark
	::Mood(::rgb(255, 170, 150), ::rgb(30, 6, 8), ::rgb(110, 40, 70), ::rgb(58, 18, 20), ::rgb(70, 42, 36),
		[::rgb(176, 72, 58), ::rgb(122, 54, 48), ::rgb(154, 130, 78), ::rgb(92, 66, 80)])
];

// Wall texels name one of eight tones; a tone is a color per (side, shade).
::TONE_BASE <- 0;
::TONE_ACCENT <- 1;
::TONE_GAP <- 2;
::TONE_LIT_EDGE <- 3;
::TONE_DARK_EDGE <- 4;
::TONE_GLOW <- 5;       // emissive: lamps and lock lights stay bright in the dark
::TONE_GRIT_DARK <- 6;
::TONE_GRIT_LIGHT <- 7;
::TONES <- 8;

// The surfaces every floor shares: doors, the exit switch, the keyed doors.
// ('base' is reserved in Squirrel)
class Fixture {
	body = null;
	accent = null;
	glow = null;

	constructor(body_, accent_, glow_) {
		body = body_;
		accent = accent_;
		glow = glow_;
	}
}

::FIXTURES <- [
	::Fixture(::rgb(128, 108, 90), ::rgb(170, 144, 110), ::rgb(255, 210, 140)),   // 5 door
	::Fixture(::rgb(62, 66, 60), ::rgb(226, 190, 60), ::rgb(110, 255, 130)),      // 6 exit switch
	::Fixture(::rgb(118, 62, 56), ::rgb(176, 92, 72), ::rgb(255, 70, 50)),        // 7 red door
	::Fixture(::rgb(62, 76, 124), ::rgb(94, 114, 176), ::rgb(80, 156, 255))       // 8 blue door
];

// distance shade 0 (far) .. 15 (near) -> light reaching the wall
function shade_light(shade) {
	local t = shade / 15.0;
	return 0.05 + 0.8 * t * ::sqrt(t);
}

// deterministic texel noise: 0..255
function texel_noise(kind, tx, ty) {
	return ::mix32(::mix32(kind * 7919 + 17, tx), ty * 31 + kind) & 255;
}

function grit(kind, tx, ty) {
	local n = ::texel_noise(kind, tx, ty);
	if (n < 26) { return ::TONE_GRIT_DARK; }
	if (n > 236) { return ::TONE_GRIT_LIGHT; }
	return ::TONE_BASE;
}

// the showcase texture of a wall kind: tone at texel (tx, ty), both 0..31
function showcase_tone(kind, tx, ty) {
	if (kind == 1) { return ::stone_tone(tx, ty); }
	if (kind == 2) { return ::panel_tone(tx, ty); }
	if (kind == 3) { return ::conduit_tone(tx, ty); }
	if (kind == 4) { return ::block_tone(tx, ty); }
	if (kind == 6) { return ::switch_tone(tx, ty); }
	if (kind >= 5) { return ::door_tone(kind, tx, ty); }
	return ::TONE_BASE;
}

// staggered bricks, eight texels tall
function stone_tone(tx, ty) {
	local row = ty >> 3;
	local y = ty & 7;
	local x = (tx + (row & 1) * 8) & 15;
	if (y == 7 || x == 15) { return ::TONE_GAP; }
	if (y == 0 || x == 0) { return ::TONE_LIT_EDGE; }
	if (y == 6) { return ::TONE_DARK_EDGE; }
	if (((row * 5 + ((tx + (row & 1) * 8) >> 4)) % 3) == 0) { return ::TONE_ACCENT; }
	return ::grit(1, tx, ty);
}

// riveted plates with a painted band
function panel_tone(tx, ty) {
	local x = tx & 15;
	local y = ty & 15;
	if (x == 15 || y == 15) { return ::TONE_GAP; }
	if (x == 0 || y == 0) { return ::TONE_LIT_EDGE; }
	if (x == 14 || y == 14) { return ::TONE_DARK_EDGE; }
	if ((x == 2 || x == 12) && (y == 2 || y == 12)) { return ::TONE_LIT_EDGE; }
	if ((x == 2 || x == 12) && (y == 3 || y == 13)) { return ::TONE_DARK_EDGE; }
	if (y >= 6 && y <= 8) { return ::TONE_ACCENT; }
	return ::grit(2, tx, ty);
}

// runs of pipe held by brackets
function conduit_tone(tx, ty) {
	local y = ty & 7;
	if ((tx & 15) < 2) {
		if (y == 0) { return ::TONE_LIT_EDGE; }
		return (tx & 15) == 0 ? ::TONE_ACCENT : ::TONE_DARK_EDGE;
	}
	if (y == 0) { return ::TONE_LIT_EDGE; }
	if (y == 5) { return ::TONE_DARK_EDGE; }
	if (y >= 6) { return ::TONE_GAP; }
	if (y == 1) { return ::TONE_ACCENT; }
	return ::grit(3, tx, ty);
}

// rough cut blocks, cracked and mossy
function block_tone(tx, ty) {
	local row = ty >> 4;
	local y = ty & 15;
	local x = (tx + row * 11) & 31;
	if (y == 15 || x == 31) { return ::TONE_GAP; }
	if (y == 0 || x == 0) { return ::TONE_LIT_EDGE; }
	if (y == 14 || x == 30) { return ::TONE_DARK_EDGE; }
	local n = ::texel_noise(4, tx, ty);
	if ((tx + ty * 2 + n % 3) % 23 == 0) { return ::TONE_GAP; }
	if (y > 9 && n % 5 == 0) { return ::TONE_ACCENT; }
	return ::grit(4, tx, ty);
}

// a framed door of planks, banded, with a handle; keyed doors carry lock lights
function door_tone(kind, tx, ty) {
	if (tx < 2 || tx > 29 || ty < 1) { return ::TONE_DARK_EDGE; }
	if (ty > 29) { return ::TONE_GAP; }
	if (tx == 2 || tx == 29 || ty == 1) { return ::TONE_LIT_EDGE; }
	if (tx == 15 || tx == 16) { return ::TONE_GAP; }
	if (kind != 5 && (tx == 6 || tx == 7 || tx == 24 || tx == 25) && ty > 4 && ty < 27) { return ::TONE_GLOW; }
	if (ty == 7 || ty == 22) { return ::TONE_LIT_EDGE; }
	if (ty == 8 || ty == 9 || ty == 23 || ty == 24) { return ::TONE_ACCENT; }
	if (ty >= 14 && ty <= 17 && (tx == 13 || tx == 18)) { return ::TONE_LIT_EDGE; }
	if (tx % 5 == 0) { return ::TONE_DARK_EDGE; }
	return ::grit(kind, tx, ty);
}

// the exit: a switch box over hazard stripes, two green lamps
function switch_tone(tx, ty) {
	if (tx < 2 || tx > 29 || ty < 2) { return ::TONE_DARK_EDGE; }
	if (ty >= 24) { return ((tx + ty) >> 2 & 1) == 0 ? ::TONE_ACCENT : ::TONE_GAP; }
	if (tx >= 9 && tx <= 22 && ty >= 5 && ty <= 20) {
		if (tx == 9 || ty == 5) { return ::TONE_LIT_EDGE; }
		if (tx == 22 || ty == 20) { return ::TONE_GAP; }
		if (tx >= 15 && tx <= 16 && ty >= 7 && ty <= 14) { return ::TONE_LIT_EDGE; }
		if ((tx == 12 || tx == 19) && ty >= 16 && ty <= 18) { return ::TONE_GLOW; }
		return ::TONE_GRIT_DARK;
	}
	return ::grit(6, tx, ty);
}

// --------------------------------------------------------------- sprites ----
// Sprites keep more light than walls at the same distance so a threat stays
// readable down a dark corridor; uppercase palette keys (eyes, gobs, flame)
// and every projectile glow at full brightness.
function showcase_sprite_lut(name, band, flash, variant) {
	local mood = ::MOODS[::G.map_i];
	local art = ::ARTS[name];
	local glowing = name.slice(0, 3) == "pr_";
	local lum = 1.0 - band * 0.085;
	local glow = ::sprite_light(mood, variant);
	local light = ::cadd(::cscale(::mix_rgb(mood.light, ::rgb(255, 255, 255), 0.55), lum), glow);
	local haze = ::cscale(mood.fog, (1.0 - lum) * 0.8 * (1.0 - 0.15 * ::light_level(variant)));
	local lut = [0];
	local rgbs = art.rgb;
	local keys = art.keys;
	for (local c = 1; c < rgbs.len(); ++c) {
		local r = rgbs[c][0];
		local gr = rgbs[c][1];
		local b = rgbs[c][2];
		if (flash == 1) {
			r = ::imin(255, r + 170);
			gr = ::imin(255, gr + 150);
			b = ::imin(255, b + 140);
		}
		local col = ::rgb(r, gr, b);
		local key = keys[c];
		if (glowing || (key >= 'A' && key <= 'Z')) {
			lut.push(::color_idx(col));
		} else {
			lut.push(::color_idx(::cadd(::cmul(col, light), haze)));
		}
	}
	return lut;
}

// per-kind particle ramps, phase 0 = fresh
::SHOWCASE_PARTICLE_RAMPS <- [
	[::rgb(0, 0, 0), ::rgb(0, 0, 0), ::rgb(0, 0, 0), ::rgb(0, 0, 0)],
	[::rgb(255, 250, 214), ::rgb(255, 212, 110), ::rgb(240, 140, 50), ::rgb(150, 70, 30)],      // spark
	[::rgb(232, 40, 30), ::rgb(190, 30, 24), ::rgb(140, 22, 18), ::rgb(90, 14, 12)],            // blood
	[::rgb(214, 72, 54), ::rgb(172, 50, 38), ::rgb(128, 36, 28), ::rgb(86, 24, 20)],            // gib
	[::rgb(104, 98, 94), ::rgb(84, 80, 78), ::rgb(64, 62, 62), ::rgb(46, 44, 46)],             // smoke
	[::rgb(255, 255, 236), ::rgb(255, 240, 170), ::rgb(255, 200, 100), ::rgb(220, 130, 50)],    // flash
	[::rgb(190, 255, 200), ::rgb(130, 245, 220), ::rgb(100, 200, 255), ::rgb(110, 120, 240)]    // hexmote
];

// ----------------------------------------------------------------- light ----
// The lit tables: one palette index per (light variant, side, shade, tone), so
// a surface under a lamp, a muzzle flash or a hex bolt costs one lookup more
// than an unlit one. Variant 0 is unlit; lights.nut names the rest.

::SURFACE_SHADES <- 16;
::FAR_TEXTURE <- 3.2;     // past this distance surfaces draw their far texture
::FLOOR_TONES <- 12;      // the eight wall tones, then blood, dried blood, scorch, ash
::TONE_BLOOD <- 8;
::TONE_BLOOD_DRY <- 9;
::TONE_SCORCH <- 10;
::TONE_ASH <- 11;

// the lit tables' colors, rounded to steps of LIT_STEP a channel
::LIT_STEP <- 6;

function lit_idx(c) {
	local half = ::LIT_STEP / 2;
	local r = ((c[0] * 255.0).tointeger() + half) / ::LIT_STEP * ::LIT_STEP;
	local g = ((c[1] * 255.0).tointeger() + half) / ::LIT_STEP * ::LIT_STEP;
	local b = ((c[2] * 255.0).tointeger() + half) / ::LIT_STEP * ::LIT_STEP;
	return ::rgb_idx(r, g, b);
}

function light_color_of(mood, variant) {
	local channel = (variant - 1) / ::LIGHT_LEVELS;
	if (channel == ::LIGHT_LAMP) { return ::mix_rgb(mood.light, ::LIGHT_COLORS[::LIGHT_LAMP], 0.5); }
	return ::LIGHT_COLORS[channel];
}

// [variant][side][shade][tone] palette indices for one surface, appended to out
function push_lit_tones(out, mood, albedo, glow, sides) {
	local white = ::rgb(255, 255, 255);
	local black = ::rgb(0, 0, 0);
	local ntones = albedo.len();
	for (local v = 0; v < ::LIGHT_VARIANTS; ++v) {
		local s = v == 0 ? 0.0 : 1.0 * ::light_level(v) / ::LIGHT_LEVELS;
		local lc = v == 0 ? black : ::light_color_of(mood, v);
		for (local side = 0; side < sides; ++side) {
			local light = ::cscale(side == 1 ? ::cscale(::mix_rgb(mood.light, mood.turned, 0.3), 0.7) : mood.light, ::AMBIENT);
			for (local shade = 0; shade < ::SURFACE_SHADES; ++shade) {
				local lum = ::shade_light(shade);
				local haze = ::cscale(mood.fog, 1.0 - lum);
				local lit = ::cadd(::cscale(light, lum), ::cscale(lc, s * ::LIGHT_GAIN * (0.45 + 0.55 * lum)));
				local fog = ::cscale(haze, 1.0 - 0.6 * s);
				for (local tone = 0; tone < ntones; ++tone) {
					if (tone == ::TONE_GLOW) {
						out.push(::lit_idx(::cadd(::cscale(::mix_rgb(glow, white, 0.15), 0.6 + 0.4 * lum), ::cscale(haze, 0.3))));
					} else {
						out.push(::lit_idx(::cadd(::cmul(albedo[tone], lit), fog)));
					}
				}
			}
		}
	}
}

function wall_albedo(tint, accent) {
	return [tint, accent, ::cscale(tint, 0.36), ::cclamp01(::cscale(tint, 1.25)), ::cscale(tint, 0.62), tint,
		::cscale(tint, 0.86), ::cclamp01(::cscale(tint, 1.1))];
}

::WALL_TABLE_STRIDE <- 6400;    // LIGHT_VARIANTS x 2 sides x 16 shades x 8 tones

// the lit wall table of floor mi: [kind 0..8][variant][side][shade][tone]
function showcase_lit_wall_tones(mi) {
	local mood = ::MOODS[mi];
	local tones = [];
	local unseen = ::rgb(10, 10, 12);
	::push_lit_tones(tones, mood, ::wall_albedo(unseen, unseen), unseen, 2);
	foreach (wall in mood.walls) {
		::push_lit_tones(tones, mood, ::wall_albedo(wall, ::mix_rgb(wall, mood.light, 0.18)), wall, 2);
	}
	foreach (fixture in ::FIXTURES) {
		::push_lit_tones(tones, mood, ::wall_albedo(fixture.body, fixture.accent), fixture.glow, 2);
	}
	return tones;
}

// what each floor is paved with, and what glows in its cracks
::FLOOR_GLOWS <- [::rgb(255, 210, 140), ::rgb(90, 236, 226), ::rgb(255, 122, 40), ::rgb(255, 60, 44)];

function showcase_floor_tones(mi) {
	local mood = ::MOODS[mi];
	local paving = ::cclamp01(::cscale(mood.floor, 1.3));
	local accent = ::mix_rgb(paving, mood.walls[0], 0.35);
	local albedo = ::wall_albedo(paving, accent);
	albedo[::TONE_GAP] = ::cscale(paving, 0.55);
	albedo[::TONE_GRIT_DARK] = ::cscale(paving, 0.9);
	albedo[::TONE_GRIT_LIGHT] = ::cclamp01(::cscale(paving, 1.06));
	albedo.push(::rgb(150, 16, 12));
	albedo.push(::rgb(84, 10, 10));
	albedo.push(::rgb(22, 18, 16));
	albedo.push(::rgb(52, 46, 44));
	local tones = [];
	::push_lit_tones(tones, mood, albedo, ::FLOOR_GLOWS[mi], 1);
	return tones;
}

function showcase_ceiling_tones(mi) {
	local mood = ::MOODS[mi];
	local panel = ::cscale(mood.ceiling, 0.85);
	local albedo = ::wall_albedo(panel, ::mix_rgb(panel, mood.light, 0.12));
	albedo[::TONE_GRIT_DARK] = ::cscale(panel, 0.92);
	albedo[::TONE_GRIT_LIGHT] = panel;
	local tones = [];
	::push_lit_tones(tones, mood, albedo, ::mix_rgb(mood.light, ::rgb(255, 255, 255), 0.45), 1);
	return tones;
}

// ------------------------------------------------------ floor and ceiling ----
// 16x16 texels a tile; four variants a floor, picked per tile from a hash

// each texture comes twice: as drawn, then as seen from afar, where the
// one-texel lines and the grit would shimmer: only glow, tint and beams stay
function far_tone(tone, keep_edges) {
	if (tone == ::TONE_GLOW || tone == ::TONE_ACCENT) { return tone; }
	if (keep_edges && (tone == ::TONE_GAP || tone == ::TONE_DARK_EDGE)) { return tone; }
	return ::TONE_BASE;
}

function showcase_floor_texture(mi) {
	local tex = [];
	for (local far = 0; far < 2; ++far) {
		for (local variant = 0; variant < 4; ++variant) {
			for (local v = 0; v < 16; ++v) {
				for (local u = 0; u < 16; ++u) {
					local tone = ::floor_tone(mi, variant, u, v);
					if (far == 1) { tone = mi == 1 ? ::far_tone(tone, false) : (tone == ::TONE_GLOW ? ::TONE_DARK_EDGE : ::far_tone(tone, false)); }
					tex.push(tone);
				}
			}
		}
	}
	return tex;
}

function floor_tone(mi, variant, u, v) {
	local n = ::texel_noise(20 + variant, u, v);
	if (mi == 1) {
		// coolant grating: plates over a lit channel
		if (u == 0 || v == 0) { return ::TONE_LIT_EDGE; }
		if (u == 15 || v == 15) { return ::TONE_GAP; }
		if (v >= 6 && v <= 9 && u >= 2 && u <= 13 && (u & 1) == 0) { return variant == 0 ? ::TONE_GLOW : ::TONE_GAP; }
		if ((u == 3 || u == 12) && (v == 3 || v == 12)) { return ::TONE_LIT_EDGE; }
		return n < 30 ? ::TONE_GRIT_DARK : ::TONE_BASE;
	}
	if (mi == 2) {
		// foundry basalt: blocks split by cracks that glow
		local x = (u + (v >> 3) * 5) & 15;
		local y = v & 7;
		local crack_at = variant == 0 ? (v * 3) / 4 + 3 + (v >> 2 & 1) : 99;
		if (u == crack_at || (variant == 0 && u == crack_at + 1 && (v & 3) == 0)) { return ::TONE_GLOW; }
		if (x == 15 || y == 7) { return ::TONE_GAP; }
		if (x == 0 || y == 0) { return ::TONE_LIT_EDGE; }
		if (n < 40) { return ::TONE_GRIT_DARK; }
		return variant == 3 ? ::TONE_ACCENT : ::TONE_BASE;
	}
	if (mi == 3) {
		// the throne: checkered tiles trimmed in gold
		if (u == 0 || v == 0 || u == 8 || v == 8) { return ::TONE_LIT_EDGE; }
		local dark = ((u >> 3) + (v >> 3)) % 2 == 0;
		if (dark) { return n < 50 ? ::TONE_GRIT_DARK : ::TONE_DARK_EDGE; }
		return n < 30 ? ::TONE_GRIT_LIGHT : ::TONE_ACCENT;
	}
	// flagstones, four to a tile, each its own tint
	local sx = u >> 3;
	local sy = v >> 3;
	local x = u & 7;
	local y = v & 7;
	if (x == 7 || y == 7) { return ::TONE_GAP; }
	if (x == 0 || y == 0) { return ::TONE_LIT_EDGE; }
	if (x == 6 || y == 6) { return ::TONE_DARK_EDGE; }
	if (((sx + sy * 2 + variant) % 3) == 0) { return n < 24 ? ::TONE_GRIT_DARK : ::TONE_ACCENT; }
	if (n < 26) { return ::TONE_GRIT_DARK; }
	if (n > 238) { return ::TONE_GRIT_LIGHT; }
	return ::TONE_BASE;
}

// ceiling panels between beams; variant 1 is a lamp
function showcase_ceiling_texture() {
	local tex = [];
	for (local far = 0; far < 2; ++far) {
		for (local lamp = 0; lamp < 2; ++lamp) {
			for (local v = 0; v < 16; ++v) {
				for (local u = 0; u < 16; ++u) {
					local tone = ::ceiling_tone(lamp == 1, u, v);
					tex.push(far == 1 ? ::far_tone(tone, true) : tone);
				}
			}
		}
	}
	return tex;
}

function ceiling_tone(lamp, u, v) {
	if (u < 2 || v < 2) { return u == 1 || v == 1 ? ::TONE_GAP : ::TONE_DARK_EDGE; }
	if (lamp && u >= 5 && u <= 12 && v >= 5 && v <= 12) {
		if (u == 5 || v == 5 || u == 12 || v == 12) { return ::TONE_LIT_EDGE; }
		return ::TONE_GLOW;
	}
	if (u == 2 || v == 2) { return ::TONE_LIT_EDGE; }
	return ::grit(9, u, v);
}

// every wall kind's texture as tones, 32x32 texels: [kind][ty][tx]
function showcase_wall_texture() {
	local tex = [];
	for (local far = 0; far < 2; ++far) {
		for (local kind = 0; kind < 9; ++kind) {
			for (local ty = 0; ty < 32; ++ty) {
				for (local tx = 0; tx < 32; ++tx) {
					local tone = ::showcase_tone(kind, tx, ty);
					tex.push(far == 1 ? ::far_tone(tone, true) : tone);
				}
			}
		}
	}
	return tex;
}

// a sprite's light: how a variant tints it
function sprite_light(mood, variant) {
	if (variant == 0) { return ::rgb(0, 0, 0); }
	return ::cscale(::light_color_of(mood, variant), 0.9 * ::light_level(variant) / ::LIGHT_LEVELS);
}
