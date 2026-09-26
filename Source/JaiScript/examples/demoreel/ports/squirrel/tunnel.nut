// TUNNEL (Squirrel port of scenes/tunnel.jai): the table-driven texture tunnel;
// four integer lookups per pixel through a sliding window.

class Tunnel extends Scene {
	ang = null;
	dep = null;
	fog = null;
	tex = null;
	tw = 0;
	tt = 0.0;
	travel = 0.0;
	spin = 0.0;

	constructor() {
		ang = [];
		dep = [];
		fog = [];
		tex = [];
	}

	function build() {
		name = "TUNNEL";
		handle = "down the wormhole . by lookup table";
		tw = w * 2;
		local th = h * 2;
		local rmax = w * 0.62;
		for (local y = 0; y < th; ++y) {
			for (local x = 0; x < tw; ++x) {
				local dx = x - w + 0.5;
				local dy = y - h + 0.5;
				local r = ::sqrt(dx * dx + dy * dy);
				r = r < 1.0 ? 1.0 : r;
				ang.push(((::atan2(dy, dx) + 3.14159265) * 40.7436654).tointeger() & 255);
				dep.push((2600.0 / r).tointeger());
				local s = (r * 12.0 / rmax).tointeger();
				fog.push((s > 11 ? 11 : s) * 16);
			}
		}
		for (local s2 = 0; s2 < 12; ++s2) {
			local f = s2 / 11.0;
			local k = f * f * (3.0 - 2.0 * f);
			for (local lv = 0; lv < 16; ++lv) {
				local r0 = 0.0, g0 = 0.0, b0 = 0.0;
				if (lv < 12) {
					local m = lv / 11.0;
					r0 = 18.0 + 120.0 * m; g0 = 26.0 + 150.0 * m; b0 = 48.0 + 190.0 * m;
				} else {
					local m2 = (lv - 11) / 4.0;
					r0 = 120.0 + 135.0 * m2; g0 = 40.0 + 160.0 * m2 * m2; b0 = 200.0 + 55.0 * m2;
				}
				local glow = lv >= 12 ? 0.45 + 0.55 * k : k;
				local haze = (1.0 - k) * (1.0 - k);
				pal.add(r0 * glow + 255.0 * haze, g0 * glow + 150.0 * haze, b0 * glow + 120.0 * haze);
			}
		}
		for (local v = 0; v < 64; ++v) {
			for (local u = 0; u < 64; ++u) {
				local row = v / 16;
				local bu = (u + (row % 2) * 8) % 16;
				local bv = v % 16;
				local plate = ((u + (row % 2) * 8) / 16 + row * 5) % 7;
				local lv = 5 + plate % 4;
				if (bu == 0 || bv == 0) { lv = 1; }
				else if (bu == 1 || bv == 1) { lv = lv + 3; }
				else if (bu == 15 || bv == 15) { lv = lv - 3; }
				if ((bu == 3 || bu == 12) && (bv == 3 || bv == 12)) { lv = 11; }
				if (v >= 28 && v < 36) {
					local d = v < 32 ? v - 28 : 35 - v;
					lv = 12 + d;
				}
				tex.push(lv < 0 ? 0 : lv);
			}
		}
	}

	function update(t, dt) {
		tt = t;
		local sp = 34.0 + 22.0 * ::sin(t * 0.37);
		travel += dt * sp;
		spin += dt * (0.9 * ::sin(t * 0.29));
	}

	function render(grid) {
		local ww = w;
		local hh = h;
		local tww = tw;
		local an = ang;
		local de = dep;
		local fo = fog;
		local tx = tex;
		local ox = (w * 0.5 + w * 0.34 * ::sin(tt * 0.53)).tointeger();
		local oy = (h * 0.5 + h * 0.30 * ::sin(tt * 0.71 + 0.9)).tointeger();
		local mv = travel.tointeger();
		local rot = (spin * 40.0).tointeger() + 4096;
		local twist = (6.0 + 5.0 * ::sin(tt * 0.23)).tointeger();
		local idx = 0;
		for (local y = 0; y < hh; ++y) {
			local trow = (y + oy) * tww + ox;
			for (local x = 0; x < ww; ++x) {
				local ti = trow + x;
				local d = de[ti];
				local u = (an[ti] + rot + ((d * twist) >> 5)) & 63;
				local v = (d + mv) & 63;
				grid[idx] = fo[ti] + tx[v * 64 + u];
				idx = idx + 1;
			}
		}
	}
}
