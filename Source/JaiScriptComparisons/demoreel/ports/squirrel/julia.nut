// JULIA (Squirrel port of scenes/julia.jai): the whole Julia set, morphing in frame. The constant is
// steered through its fixed point's multiplier; each pixel iterates z and its derivative, the smooth
// escape count picks the hue, the distance estimate draws the rim, the potential's gradient the relief.
// A frame computes half its rows and mirrors them (the set is its own half turn).

::JU_CA <- 0.0;
::JU_CB <- 0.0;
::JU_FX <- 0.0;
::JU_FY <- 0.0;
::JU_TRAP <- 0.0;
::JU_DRIFT <- 0.0;
::JU_MAXIT <- 40;
::JU_W <- 0;
::JU_H <- 0;
::JU_UX <- 0.0;
::JU_UY <- 0.0;
::JU_VX <- 0.0;
::JU_VY <- 0.0;
::JU_PIXELS <- 1.0;
::JU_UNRESOLVED <- 0.0;
::JU_LX <- 0.0;
::JU_LY <- 0.0;
::JU_PHASE <- 0.0;
::JU_INPHASE <- 0.0;
::JU_LIFT <- 0.0;
::JU_RIM <- 1.0;
::JU_INSIDE <- 0;
::JU_ROWS <- [];

::JU_HUES <- 64;
::JU_LEVELS <- 12;
::JU_INHUES <- 32;

::JU_RHO <- 0;
::JU_THETA <- 1;
::JU_DIVE <- 2;
::JU_SPIN <- 3;

// one row of the top half; each entry packs the pixel and the one half a turn away
function julia_row(y) {
	local out = [];
	local log2 = ::log2;
	local sqrt = ::sqrt;
	local w = ::JU_W;
	local mi = ::JU_MAXIT;
	local ca = ::JU_CA;
	local cb = ::JU_CB;
	local ux = ::JU_UX;
	local uy = ::JU_UY;
	local py = y + 0.5 - ::JU_H * 0.5;
	local left = 0.5 - w * 0.5;
	local rx = py * ::JU_VX + left * ux;
	local ry = py * ::JU_VY + left * uy;
	local lx = ::JU_LX;
	local ly = ::JU_LY;
	local fx = ::JU_FX;
	local fy = ::JU_FY;
	local trap = ::JU_TRAP;
	local drift = ::JU_DRIFT;
	local pixels = ::JU_PIXELS;
	local phase = ::JU_PHASE;
	local inphase = ::JU_INPHASE;
	local lift = ::JU_LIFT;
	local rim = ::JU_RIM;
	local rimfall = 0.5 / rim;
	local inside = ::JU_INSIDE;
	local unresolved = ::JU_UNRESOLVED;
	local hot = ((mi * 3.5 + phase).tointeger() & 63) * 12 + 11;
	for (local x = 0; x < w; ++x) {
		local zx = rx + x * ux;
		local zy = ry + x * uy;
		local x2 = zx * zx;
		local y2 = zy * zy;
		local dx = 1.0;
		local dy = 0.0;
		local e2 = 1.0;
		local k = 0;
		local nd = 0.0;
		// four steps to a test: an escaped orbit's count and distance stay exact however far it runs on
		while (k < mi) {
			nd = 2.0 * (zx * dx - zy * dy);
			dy = 2.0 * (zx * dy + zy * dx);
			dx = nd;
			zy = 2.0 * zx * zy + cb;
			zx = x2 - y2 + ca;
			x2 = zx * zx;
			y2 = zy * zy;
			nd = 2.0 * (zx * dx - zy * dy);
			dy = 2.0 * (zx * dy + zy * dx);
			dx = nd;
			zy = 2.0 * zx * zy + cb;
			zx = x2 - y2 + ca;
			x2 = zx * zx;
			y2 = zy * zy;
			nd = 2.0 * (zx * dx - zy * dy);
			dy = 2.0 * (zx * dy + zy * dx);
			dx = nd;
			zy = 2.0 * zx * zy + cb;
			zx = x2 - y2 + ca;
			x2 = zx * zx;
			y2 = zy * zy;
			nd = 2.0 * (zx * dx - zy * dy);
			dy = 2.0 * (zx * dy + zy * dx);
			dx = nd;
			zy = 2.0 * zx * zy + cb;
			zx = x2 - y2 + ca;
			x2 = zx * zx;
			y2 = zy * zy;
			k += 4;
			if (x2 + y2 >= 64.0) { break; }
			local ex = zx - fx;
			local ey = zy - fy;
			e2 = ex * ex + ey * ey;
			if (e2 < trap) { break; }
		}
		local m2 = x2 + y2;
		local d2 = dx * dx + dy * dy;
		if (m2 < 64.0) {
			local c = hot;
			// an orbit still in after stretching its pixel past the whole set sits on the boundary
			if (d2 < unresolved) {
				local band = k * drift - log2(e2 + 0.000001) * 1.7 + inphase;
				c = inside + ((band + 1024.0).tointeger() & 31);
			}
			out.push(c * 1025);
			continue;
		}
		local lg = log2(m2);
		local nu = k + 1.0 - log2(lg * 0.5);
		local unit = 1.0 / sqrt(m2 * d2 + 1.0e-300);
		// the distance to the set, in pixels: |z| ln|z| / |z'|
		local de = m2 * unit * lg * 0.34657 * pixels;
		// the potential's gradient runs along z * conj(z')
		local nl = ((zx * dx + zy * dy) * lx + (zy * dx - zx * dy) * ly) * unit;
		local glow = 1.0 / (1.0 + de * 0.3);
		local lit = 0.4 + 0.3 * glow + lift + (de < rim ? 0.5 - de * rimfall : 0.0);
		local relief = nl * 0.2;
		local hue = ((nu * 3.5 + phase).tointeger() & 63) * 12;
		local lv = ((lit + relief) * 11.99).tointeger();
		lv = lv < 0 ? 0 : (lv > 11 ? 11 : lv);
		local lm = ((lit - relief) * 11.99).tointeger();
		lm = lm < 0 ? 0 : (lm > 11 ? 11 : lm);
		out.push(hue + lv + (hue + lm) * 1024);
	}
	return out;
}

class Julia extends ::Scene {
	tt = 0.0;

	// the path, in beats: over key_from..key_to a track glides to key_value
	key_track = null;
	key_from = null;
	key_to = null;
	key_value = null;
	key_lands = null;

	constructor() {
		key_track = [];
		key_from = [];
		key_to = [];
		key_value = [];
		key_lands = [];
	}

	function hit_times() { return [::MUSIC_BAR * 3.0, ::MUSIC_BAR * 5.0, ::MUSIC_BAR * 8.0, ::MUSIC_BAR * 9.0]; }
	function hit_levels() { return [2, 1, 2, 1]; }
	function length_bars() { return 10; }

	function glide(track, from, to, value) {
		key_track.push(track);
		key_from.push(from);
		key_to.push(to);
		key_value.push(value);
		key_lands.push(false);
	}

	// a glide that leaves at full speed and settles: the move a hit throws
	function land(track, from, to, value) {
		glide(track, from, to, value);
		key_lands[key_lands.len() - 1] = true;
	}

	function build() {
		name = "JULIA";
		handle = "one constant, every shape . by the type ladder";
		local night = [24.0, 12.0, 66.0];
		local stops = [[40.0, 30.0, 150.0], [120.0, 50.0, 210.0], [240.0, 50.0, 150.0], [255.0, 130.0, 50.0],
			[255.0, 220.0, 110.0], [80.0, 230.0, 220.0], [30.0, 120.0, 235.0], [40.0, 30.0, 150.0]];
		local ns = stops.len() - 1;
		for (local hq = 0; hq < ::JU_HUES; ++hq) {
			local f = hq * ns * 1.0 / ::JU_HUES;
			local k = f.tointeger();
			local u = f - k;
			local a = stops[k];
			local b = stops[k + 1];
			// a hue's ramp runs night, the hue in full, white hot
			for (local l = 0; l < ::JU_LEVELS; ++l) {
				local lit = l / (::JU_LEVELS - 1.0);
				local sat = lit * 1.8 > 1.0 ? 1.0 : lit * 1.8;
				local wh = lit > 0.75 ? (lit - 0.75) * 3.0 : 0.0;
				local rgb = [0.0, 0.0, 0.0];
				for (local ch = 0; ch < 3; ++ch) {
					local full = (a[ch] + (b[ch] - a[ch]) * u) * (0.5 + 0.5 * lit);
					local v = night[ch] + (full - night[ch]) * sat;
					rgb[ch] = v + (255.0 - v) * wh;
				}
				pal.add(rgb[0], rgb[1], rgb[2]);
			}
		}
		::JU_INSIDE = pal.gradient(::JU_INHUES + 1, [[6.0, 4.0, 22.0], [60.0, 18.0, 96.0], [10.0, 8.0, 36.0], [14.0, 62.0, 96.0], [6.0, 4.0, 22.0]]);

		// a fat set swells four ears by bar 1, trades them for the rabbit's three by bar 2,
		// and through the riser the rabbit starves to the six-armed dendrite past its bulb's tip
		glide(::JU_THETA, 0.0, 2.5, 1.5708);
		glide(::JU_RHO, 2.0, 4.0, 1.0625);
		glide(::JU_RHO, 4.5, 5.5, 0.96);
		glide(::JU_THETA, 5.0, 7.0, 2.0944);
		glide(::JU_RHO, 6.5, 8.0, 1.107);
		glide(::JU_RHO, 9.0, 12.0, 1.3283);
		glide(::JU_THETA, 9.0, 12.0, 2.0866);
		glide(::JU_DIVE, 9.0, 12.0, 0.7);
		// the bar 3 hit dives into the junction at its heart and turns; the pull back fattens the
		// arms and slides along the cardioid to San Marco, which the bar 5 accent twists into a dragon
		land(::JU_DIVE, 12.0, 15.5, 3.7);
		land(::JU_SPIN, 12.0, 20.0, 3.14159265);
		glide(::JU_DIVE, 15.5, 17.5, 0.0);
		glide(::JU_RHO, 16.5, 18.2, 1.0);
		glide(::JU_THETA, 17.7, 19.7, 3.14159265);
		land(::JU_RHO, 20.0, 21.5, 1.0723);
		land(::JU_THETA, 20.0, 21.5, 3.0068);
		// the basilica on bar 6, a fat set, the Siegel disk on bar 7
		glide(::JU_RHO, 22.5, 24.0, 1.236);
		glide(::JU_THETA, 22.5, 24.0, 3.14159265);
		glide(::JU_RHO, 24.5, 26.0, 0.97);
		glide(::JU_THETA, 25.5, 28.0, 3.8832);
		glide(::JU_RHO, 26.5, 28.0, 1.0);
		// the riser carries it up the quarter bulb to an eight-armed star; the bar 8 hit dives in
		glide(::JU_THETA, 28.5, 30.3, 4.7124);
		glide(::JU_RHO, 30.3, 32.0, 1.2679);
		glide(::JU_THETA, 31.0, 32.0, 4.6648);
		glide(::JU_DIVE, 29.0, 32.0, 0.6);
		land(::JU_DIVE, 32.0, 35.5, 3.6);
		land(::JU_SPIN, 32.0, 39.0, 6.2831853);
		// bar 9 pulls back and the star fattens to the four-eared set, which holds the frame to the end
		glide(::JU_DIVE, 36.0, 38.0, 0.0);
		glide(::JU_THETA, 37.0, 39.0, 4.7124);
		glide(::JU_RHO, 37.0, 39.0, 1.0625);
	}

	function update(t, dt) {
		tt = t;
	}

	function render(grid) {
		local t = tt;
		local beats = t / ::MUSIC_BEAT;
		local kick = cue.kick_env(t);
		local snare = cue.snare_env(t);
		local hitv = cue.hit(t);
		local track = [0.92, 0.9, 0.0, 0.0];
		local n = key_from.len();
		for (local i = 0; i < n; ++i) {
			local from = key_from[i];
			if (beats <= from) { continue; }
			local k = beats >= key_to[i] ? 1.0 : (beats - from) / (key_to[i] - from);
			local back = 1.0 - k;
			k = key_lands[i] ? 1.0 - back * back * back : k * k * (3.0 - 2.0 * k);
			local which = key_track[i];
			track[which] += (key_value[i] - track[which]) * k;
		}
		local rho = track[::JU_RHO];
		local theta = track[::JU_THETA];
		local dive = track[::JU_DIVE];
		local spin = track[::JU_SPIN] + 0.07 * ::sin(t * 0.9);
		local lr = rho * ::cos(theta);
		local li = rho * ::sin(theta);
		::JU_FX = lr * 0.5;
		::JU_FY = li * 0.5;
		::JU_CA = lr * 0.5 - (lr * lr - li * li) * 0.25;
		::JU_CB = li * 0.5 - lr * li * 0.5;
		// near alpha the map is e -> lambda e + e^2, a contraction while |e| < 1 - rho
		local home = rho < 1.0 ? (1.0 - rho) * 0.6 : 0.0;
		::JU_TRAP = home * home;
		::JU_DRIFT = rho < 1.0 ? 3.4 * ::log2(rho) : 0.0;
		// beta = 1 - alpha is the set's far tip: the set spans beta to -beta
		local bx = 1.0 - ::JU_FX;
		local by = 0.0 - ::JU_FY;
		local reach = ::sqrt(bx * bx + by * by);
		local half_width = reach * 1.2;
		half_width = half_width < 1.4 ? 1.4 : half_width;
		local scale = half_width * 2.0 / w * (1.0 - kick * 0.05) / ::pow(2.0, dive);
		local turn = ::atan2(by, bx) + spin - 0.6 * ::sin(theta);
		local cs = ::cos(turn);
		local sn = ::sin(turn);
		::JU_UX = cs * scale;
		::JU_UY = sn * scale;
		::JU_VX = 0.0 - sn * scale;
		::JU_VY = cs * scale;
		::JU_PIXELS = 1.0 / scale;
		// the critical orbit's mean stretch tells a set with an inside (it shrinks) from a dendrite (it grows)
		local ox = ::JU_CA;
		local oy = ::JU_CB;
		local stretch = 0.0;
		local steps = 0;
		while (steps < 48 && ox * ox + oy * oy < 64.0) {
			stretch += ::log(4.0 * (ox * ox + oy * oy) + 0.000000000001);
			local nx = ox * ox - oy * oy + ::JU_CA;
			oy = 2.0 * ox * oy + ::JU_CB;
			ox = nx;
			++steps;
		}
		local thin = steps > 0 ? stretch * 0.5 / steps - 0.06 : 1.0;
		thin = thin < 0.0 ? 0.0 : (thin > 0.25 ? 0.25 : thin);
		::JU_UNRESOLVED = ::JU_PIXELS * ::JU_PIXELS * ::pow(2.0, 10.0 - 400.0 * thin);
		::JU_LX = -0.6 * cs + 0.8 * sn;
		::JU_LY = -0.6 * sn - 0.8 * cs;
		::JU_PHASE = 4096.0 + t * 9.0 + cue.chord(t) * 5.0;
		::JU_INPHASE = t * 4.0;
		::JU_LIFT = kick * 0.1 + snare * 0.06 + hitv * 0.14;
		::JU_RIM = 1.1 + kick * 0.5 + hitv * 1.5;
		::JU_W = w;
		::JU_H = h;
		local rows_drawn = (h + 1) / 2;
		if (::JU_ROWS.len() != rows_drawn) {
			::JU_ROWS = [];
			for (local r = 0; r < rows_drawn; ++r) { ::JU_ROWS.push(r); }
		}
		// parallel_transform: the same row calls in order, serially
		local rows = ::JU_ROWS.map(::julia_row);
		local ww = w;
		local last = ww * h - 1;
		local idx = 0;
		for (local y = 0; y < rows_drawn; ++y) {
			local row = rows[y];
			for (local x = 0; x < ww; ++x) {
				local v = row[x];
				grid[idx] = v & 1023;
				grid[last - idx] = v >> 10;
				++idx;
			}
		}
	}
}

::SCENES[2] <- ::Julia;
