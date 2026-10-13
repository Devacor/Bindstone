// FIRE (Squirrel port of scenes/fire.jai): bottom-seeded Doom-fire convection
// with jitter, cooling and sparks; rainbow mode offsets into hue-rolled banks.

class Fire extends Scene {
	heat = null;
	cool = null;
	mask = null;
	spx = null;
	spy = null;
	svx = null;
	svy = null;
	slife = null;
	nsparks = 48;
	levels = 73;
	phases = 6;
	fr = 0;
	rainbow_on = 0;
	hue_t = 0.0;
	tt = 0.0;

	constructor() {
		heat = [];
		cool = [];
		mask = [];
		spx = [];
		spy = [];
		svx = [];
		svy = [];
		slife = [];
	}

	function build() {
		name = "FIRE";
		handle = "ember chapel -> rainbow mode . for devacor";
		build_fire();
	}

	function build_fire() {
		heat = ::gfx.make_grid(w * (h + 2));
		for (local i = 0; i < 1024; ++i) { cool.push(rng.next(4)); }
		for (local i = 0; i < levels; ++i) {
			local f = i / (levels - 1.0);
			local r = 255.0 * f * 2.2;
			local g = f > 0.28 ? 255.0 * (f - 0.28) * 1.7 : 0.0;
			local b = f > 0.75 ? 255.0 * (f - 0.75) * 3.2 : 0.0;
			pal.add(r, g, b);
		}
		for (local p = 0; p < phases; ++p) {
			local base_hue = p * 6.28318530718 / phases;
			for (local i = 0; i < levels; ++i) {
				local f = i / (levels - 1.0);
				local c = ::gfx.rainbow_rgb(base_hue + f * 1.4);
				local glow = f * 1.8 > 1.0 ? 1.0 : f * 1.8;
				local wh = f > 0.85 ? (f - 0.85) * 4.0 : 0.0;
				local cr = c[0] * glow;
				local cg = c[1] * glow;
				local cb = c[2] * glow;
				pal.add(cr + (255.0 - cr) * wh, cg + (255.0 - cg) * wh, cb + (255.0 - cb) * wh);
			}
		}
		for (local s = 0; s < nsparks; ++s) {
			spx.push(0.0);
			spy.push(0.0);
			svx.push(0.0);
			svy.push(0.0);
			slife.push(0.0);
		}
	}

	function update(t, dt) {
		tt = t;
		fr = fr + 1;
		rainbow_on = t > 5.5 ? 1 : 0;
		if (rainbow_on == 1) { hue_t += dt * 3.0; }
		seed_rows(40, 33);
		convect(false);
		move_sparks(dt, true);
	}

	// the two hidden bottom rows: mostly furious, occasionally gapped
	function seed_rows(lo, span) {
		local bse = h * w;
		for (local x = 0; x < w; ++x) {
			local v = lo + rng.next(span);
			v = rng.next(100) < 13 ? rng.next(lo / 3 + 2) : v;
			heat[bse + x] = v;
			heat[bse + w + x] = v;
		}
	}

	function convect(lit) {
		local ww = w;
		local hh = h;
		local ht = heat;
		local rn = cool;
		local mk = mask;
		local salt = fr * 131;
		for (local y = 0; y < hh; ++y) {
			local row = y * ww;
			local below = row + ww;
			local below2 = below + ww;
			for (local x = 0; x < ww; ++x) {
				local r = rn[(row + x * 7 + salt) & 1023];
				local xs = x + (r == 3 ? 0 : r - 1);
				xs = xs < 1 ? 1 : (xs > ww - 2 ? ww - 2 : xs);
				local v = ((ht[below + xs - 1] + ht[below + xs] + ht[below + xs + 1] + ht[below2 + xs]) >> 2) - ((r + 1) >> 1);
				ht[row + x] = v > 0 ? v : 0;
			}
		}
		if (lit) {
			local n = ww * hh;
			for (local i = 0; i < n; ++i) {
				if (mk[i] == 1) {
					local glow = 58 + (rn[(i * 7 + fr) & 1023] & 1) * 8;
					if (ht[i] < glow) { ht[i] = glow; }
				}
			}
		}
	}

	function move_sparks(dt, spawn) {
		for (local s = 0; s < nsparks; ++s) {
			local life = slife[s] - dt;
			if (life <= 0.0) {
				if (!spawn || rng.next(100) >= 12) { slife[s] = 0.0; continue; }
				spx[s] = rng.next(w * 10) * 0.1;
				spy[s] = h * (0.62 + rng.next(30) * 0.01);
				svx[s] = (rng.next(200) - 100) * 0.08;
				svy[s] = 0.0 - (18.0 + rng.next(220) * 0.1);
				life = 0.8 + rng.next(100) * 0.012;
			}
			slife[s] = life;
			svx[s] = svx[s] + ::sin(tt * 3.0 + s) * 6.0 * dt;
			spx[s] = spx[s] + svx[s] * dt;
			spy[s] = spy[s] + svy[s] * dt;
		}
	}

	function bank() {
		return rainbow_on == 1 ? levels + ((hue_t * 4.0).tointeger() % phases) * levels : 0;
	}

	function render(grid) {
		local b = bank();
		local n = w * h;
		local ht = heat;
		for (local i = 0; i < n; ++i) {
			local v = ht[i];
			grid[i] = (v > 72 ? 72 : v) + b;
		}
		draw_sparks(grid, b);
	}

	function draw_sparks(grid, b) {
		for (local s = 0; s < nsparks; ++s) {
			local life = slife[s];
			if (life <= 0.0) { continue; }
			local x = spx[s].tointeger();
			local y = spy[s].tointeger();
			if (x < 0 || x >= w || y < 0 || y >= h) { continue; }
			local lv = 40 + (life * 30.0).tointeger() + ((fr + s) & 3) * 3;
			grid[y * w + x] = (lv > 72 ? 72 : lv) + b;
		}
	}
}
