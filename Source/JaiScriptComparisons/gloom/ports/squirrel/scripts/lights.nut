// The showcase look's light: a grid of light texels, LIGHT_SUB to a tile edge,
// each holding a light variant (a channel and a level, 0 unlit). Ceiling lamps
// and the glowing fixtures (the exit switch, the lock lights) are baked when a
// floor loads or a door opens; the muzzle flash, projectiles and blasts are
// stamped over the bake every frame. Light flows through open tiles only, so it
// rounds corners and stops at walls. Surfaces read their texel and look the lit
// color up in a table per variant (look.nut builds them).

::LIGHT_SUB <- 4;
::LIGHT_LEVELS <- 4;
::LIGHT_LAMP <- 0;
::LIGHT_FIRE <- 1;
::LIGHT_HEX <- 2;
::LIGHT_ACID <- 3;
::LIGHT_RED <- 4;
::LIGHT_BLUE <- 5;
::LIGHT_CHANNELS <- 6;
::LIGHT_VARIANTS <- 25;     // unlit + channels x levels

// ordered dither over the light texels: light falls off in a fine stipple, not in rings
::BAYER4 <- [0.0, 0.5, 0.125, 0.625, 0.75, 0.25, 0.875, 0.375, 0.1875, 0.6875, 0.0625, 0.5625, 0.9375,
	0.4375, 0.8125, 0.3125];

// the four steps between neighboring tiles
::NEIGHBORS <- [[1, 0], [-1, 0], [0, 1], [0, -1]];

function light_level(variant) { return variant == 0 ? 0 : (variant - 1) % ::LIGHT_LEVELS + 1; }

// a light for one frame: [x, y, channel, radius, power]
function light_source(x, y, channel, radius, power) { return [x, y, channel, radius, power]; }

class LightGrid {
	gw = 0;            // texels across
	gh = 0;
	grid = null;       // this frame's light
	lamps = null;      // per tile: a lamp hangs over it
	baked = null;
	baked_gen = -1;
	seen = null;       // per tile: the stamp that last reached it
	stamp_id = 0;
	queue = null;

	constructor() {
		grid = [];
		lamps = [];
		baked = [];
		seen = [];
		queue = [];
	}

	// a floor's lamps: open tiles, LAMP_SPACING apart, picked in a fixed order from a hash
	function load() {
		local g = ::G;
		local mw = g.mw;
		local mh = g.mh;
		gw = mw * ::LIGHT_SUB;
		gh = mh * ::LIGHT_SUB;
		lamps = ::array(mw * mh, false);
		seen = ::array(mw * mh, 0);
		for (local ty = 1; ty < mh - 1; ++ty) {
			for (local tx = 1; tx < mw - 1; ++tx) {
				if (g.tiles[ty * mw + tx] != 0 || ::texel_noise(11, tx, ty) % 3 != 0) { continue; }
				if (!lamp_near(tx, ty)) { lamps[ty * mw + tx] = true; }
			}
		}
		baked_gen = -1;
	}

	// the bake, redone when a door or a secret opens
	function frame(sources) {
		local g = ::G;
		if (baked_gen != g.mapgen) {
			bake();
			baked_gen = g.mapgen;
		}
		grid = clone baked;
		foreach (s in sources) { stamp(grid, s[0], s[1], s[2], s[3], s[4]); }
	}

	function at(x, y) {
		local gx = (x * ::LIGHT_SUB).tointeger();
		local gy = (y * ::LIGHT_SUB).tointeger();
		if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) { return 0; }
		return grid[gy * gw + gx];
	}

	function lamp_near(tx, ty) {
		local g = ::G;
		local mw = g.mw;
		for (local y = ::imax(0, ty - ::LAMP_SPACING + 1); y < ::imin(g.mh, ty + ::LAMP_SPACING); ++y) {
			for (local x = ::imax(0, tx - ::LAMP_SPACING + 1); x < ::imin(mw, tx + ::LAMP_SPACING); ++x) {
				if (lamps[y * mw + x]) { return true; }
			}
		}
		return false;
	}

	function bake() {
		local g = ::G;
		local mw = g.mw;
		baked = ::array(gw * gh, 0);
		for (local ty = 0; ty < g.mh; ++ty) {
			for (local tx = 0; tx < mw; ++tx) {
				local t = g.tiles[ty * mw + tx];
				if (lamps[ty * mw + tx]) { stamp(baked, tx + 0.5, ty + 0.5, ::LIGHT_LAMP, ::LAMP_RADIUS, ::LAMP_POWER); }
				local channel = t == 6 ? ::LIGHT_ACID : (t == 8 ? ::LIGHT_RED : (t == 9 ? ::LIGHT_BLUE : -1));
				if (channel < 0) { continue; }
				foreach (step in ::NEIGHBORS) {
					if (::tile_at(tx + step[0], ty + step[1]) != 0) { continue; }
					stamp(baked, tx + 0.5 + step[0] * 0.55, ty + 0.5 + step[1] * 0.55, channel, 2.4, 0.75);
				}
			}
		}
	}

	// light from (lx, ly) over the open tiles a walk of the radius reaches; a
	// texel keeps the brighter of what it had and what this light gives it
	function stamp(into, lx, ly, channel, radius, power) {
		local mw = ::G.mw;
		local sx = lx.tointeger();
		local sy = ly.tointeger();
		if (::tile_at(sx, sy) != 0) { return; }
		stamp_id = stamp_id + 1;
		local id = stamp_id;
		local steps = radius.tointeger() + 1;
		local q = queue;
		local seen_ = seen;
		local bayer = ::BAYER4;
		local sub = ::LIGHT_SUB;
		local levels = ::LIGHT_LEVELS;
		local w = gw;
		q.clear();
		q.push(sy * mw + sx);
		seen_[sy * mw + sx] = id;
		local head = 0;
		local inv_r = 1.0 / radius;
		local first = 1 + channel * levels;
		while (head < q.len()) {
			local tile = q[head];
			head = head + 1;
			local tx = tile % mw;
			local ty = tile / mw;
			for (local sv = 0; sv < sub; ++sv) {
				local wy = ty + (sv + 0.5) / sub - ly;
				local row = (ty * sub + sv) * w + tx * sub;
				for (local su = 0; su < sub; ++su) {
					local wx = tx + (su + 0.5) / sub - lx;
					local d = ::sqrt(wx * wx + wy * wy) * inv_r;
					if (d >= 1.0) { continue; }
					local f = power * (1.0 - d) * (1.0 - d * 0.5);
					local level = (f * levels + bayer[((ty * sub + sv) & 3) * 4 + ((tx * sub + su) & 3)]).tointeger();
					if (level <= 0) { continue; }
					if (level > levels) { level = levels; }
					local i = row + su;
					if (level >= ::light_level(into[i])) { into[i] = first + level - 1; }
				}
			}
			if (::iabs(tx - sx) + ::iabs(ty - sy) >= steps) { continue; }
			foreach (step in ::NEIGHBORS) {
				local nx = tx + step[0];
				local ny = ty + step[1];
				if (::tile_at(nx, ny) != 0) { continue; }
				local next = ny * mw + nx;
				if (seen_[next] == id) { continue; }
				seen_[next] = id;
				q.push(next);
			}
		}
	}
}
