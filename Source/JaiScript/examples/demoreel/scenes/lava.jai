// LAVALAMP — a wall of lava lamps, and a camera that will not sit still. It opens
// in macro on one lamp's wax, rolls and pulls back to the lamp, keeps pulling back
// until the lamp is one of dozens on a shelved wall turning through the frame, pans
// across them while a rainbow wave rolls over the wall and the zoom pumps on the
// beat, then spins and dives into another lamp's wax. Each pixel goes through the
// camera (a turn, a scale, a pan) into the lattice of lamps and is shaded by the
// one lamp whose cell it lands in, so the cost per pixel stays flat however many
// lamps are in view; each lamp takes one of three blob simulations, mirrored on
// alternate lamps, and its own colour family. A lamp is metaballs, lit: eight
// blobs rise off the heater and sink as they cool, stretched into taffy by their
// speed, over a pool on the base. One loop sums the inverse-square field, its
// gradient and a contribution-weighted hue, so merging blobs blend their colours,
// and the wax is lit from a normal built out of the gradient; between blobs the
// liquid glows with the wax's light, the glass carries a fresnel edge and a
// highlight stripe, the steel a studio reflection, the wall a halo of each lamp's
// colour. Rows run in parallel.

array<float> LV_BX = [];     // three blob sets, LV_N blobs each
array<float> LV_BY = [];
array<float> LV_R2 = [];
array<float> LV_ST = [];     // each blob's stretch: its speed pulls it into taffy
array<float> LV_HUE = [];
array<int> LV_ROWS = [];
int LV_N = 9;
int LV_W = 0;
int LV_H = 0;
int LV_GLOW = 0;
int LV_GLASS = 0;
int LV_STEEL = 0;
int LV_ROOM = 0;
float LV_CX = 0.0;           // the camera: the world point at the screen's centre,
float LV_CY = 0.5;
float LV_SCALE = 0.004;      // world units per pixel,
float LV_COS = 1.0;          // and its roll
float LV_SIN = 0.0;
float LV_LAMP = 1.0;         // how bright the lamps burn
float LV_BURN = 0.0;         // the rainbow wave's strength and clock
float LV_T = 0.0;
float LV_SPX = 0.55;         // the lattice: lamps across, shelves up
float LV_SPY = 1.25;

// the vessel's half-width at height y (lamp units: 0 the base's foot, 1 the cap's top)
function lamp_width(float y) -> float {
	if (y < 0.2) { return 0.16 - 0.35 * y; }
	float e = y - 0.42;
	if (y < 0.42) { return 0.13 - 0.85 * e * e; }
	if (y < 0.84) { return 0.13 - 0.363 * e * e; }
	return 0.066 - (y - 0.84) * 0.2;
}

// one row of the shot, on a parallel_transform worker: the blob arrays borrow
// zero-copy, the scalars are per-worker copies, only the returned row is written
function lava_row(int y) -> array<int> {
	array<int> out = [];
	int ww = LV_W;
	int nb = LV_N;
	float sc = LV_SCALE;
	float co = LV_COS;
	float si = LV_SIN;
	float spx = LV_SPX;
	float spy = LV_SPY;
	float py = (LV_H * 0.5 - y - 0.5) * sc;
	float lux = -0.45;
	float luy = 0.55;
	float luz = 0.70;
	float hl = 1.0 / math::sqrt(lux * lux + luy * luy + (luz + 1.0) * (luz + 1.0));
	float hx = lux * hl;
	float hy = luy * hl;
	float hz = (luz + 1.0) * hl;
	float lamp = LV_LAMP;
	float burn = LV_BURN;
	float clock = LV_T;
	for (int x = 0; x < ww; ++x) {
		float px = (x + 0.5 - ww * 0.5) * sc;
		float wx = LV_CX + px * co - py * si;
		float wy = LV_CY + px * si + py * co;
		int ci = math::ifloor(wx / spx + 0.5);
		int cj = math::ifloor(wy / spy);
		float lx = wx - ci * spx;
		float ly = wy - cj * spy;
		int id = ((ci * 7349 + cj * 1931) % 997 + 997) % 997;
		if ((id & 1) == 1) { lx = -lx; }
		int set = id % 3;
		int b0 = set * nb;
		float hue_off = (id % 6) * 4.0 + burn * ((ci + cj * 2) * 2.0 + clock * 9.0);
		float hw = lamp_width(ly);
		float across = lx / hw;
		float aa = across < 0.0 ? -across : across;
		if (ly >= 0.2 && ly <= 0.84 && aa < 1.0) {
			float f = 0.0;
			float gx = 0.0;
			float gy = 0.0;
			float hsum = 0.0;
			for (int b = b0; b < b0 + nb; ++b) {
				float dx = lx - LV_BX[b];
				float st = LV_ST[b];
				float dy = (ly - LV_BY[b]) / st;
				float d2 = dx * dx + dy * dy + 0.00002;
				float inv = LV_R2[b] / d2;
				f += inv;
				float q = inv / d2;
				gx += q * dx;
				gy += q * dy / st;
				hsum += inv * LV_HUE[b];
			}
			int hq = itrunc(hsum / f + hue_off + 0.5) % 24;
			if (f >= 1.0) {
				// the height of a blob under this field gives its normal, vertical at the rim
				float rim = 1.0 - 1.0 / f;
				rim = rim < 0.004 ? 0.004 : rim;
				float kz = 0.07 / (f * f * math::sqrt(rim));
				float nx = gx * kz;
				float ny = gy * kz;
				float il = 1.0 / math::sqrt(nx * nx + ny * ny + 1.0);
				nx *= il; ny *= il;
				float d = nx * lux + ny * luy + il * luz;
				d = d < 0.0 ? 0.0 : d;
				float s = nx * hx + ny * hy + il * hz;
				float s2 = s * s;
				float s4 = s2 * s2;
				float s8 = s4 * s4;
				float s16 = s8 * s8;
				int lv = itrunc((d * 0.72 * lamp + 0.12 + s16 * s16 * 0.45) * 15.99);
				out.push(hq * 16 + (lv > 15 ? 15 : lv));
				continue;
			}
			// the liquid: lit by the wax round it, the glass bright toward its edges
			float edge = aa * aa * aa * aa;
			int gl = itrunc((f * f * 0.85 + 0.12) * lamp * 7.99 + edge * 2.5);
			if (across > -0.62 && across < -0.5) {
				out.push(LV_GLASS + 5 + (aa > 0.56 ? 2 : 0));
				continue;
			}
			if (aa > 0.93) {
				out.push(LV_GLASS + 3 + itrunc((aa - 0.93) * 50.0));
				continue;
			}
			out.push(LV_GLOW + hq * 8 + (gl > 7 ? 7 : gl));
			continue;
		}
		if ((ly >= 0.0 && ly < 0.2 || ly > 0.84 && ly < 1.0) && aa < 1.0) {
			// steel: a studio reflection across the cone, a bright band and a dark core
			float band = 0.5 + 0.5 * math::cos(across * 3.4 - 0.9);
			float lip = ly < 0.2 ? (0.2 - ly) * 2.0 : (ly - 0.84) * 3.0;
			int sl = itrunc((band * 0.8 + 0.1 + (aa > 0.9 ? 0.25 : 0.0) - lip * 0.3) * 15.99);
			out.push(LV_STEEL + (sl < 0 ? 0 : (sl > 15 ? 15 : sl)));
			continue;
		}
		int room_hue = itrunc(hue_off + 2.5) % 24;
		// the shelf the lamp stands on: its front edge catches the lamp's light
		if (ly > spy - 0.045) {
			float lit = 1.0 / (1.0 + lx * lx * 30.0);
			int sh = itrunc((0.25 + lit * 0.7 * lamp) * 15.99);
			out.push(LV_ROOM + room_hue * 16 + (sh > 15 ? 15 : sh));
			continue;
		}
		// the wall behind: a halo of the lamp's colour, pooling on the shelf at its foot
		float ry = ly - 0.5;
		float halo = 1.0 / (1.0 + (lx * lx * 9.0 + ry * ry * 3.0));
		halo = halo * halo;
		if (ly < 0.06) { halo += 0.5 / (1.0 + lx * lx * 40.0); }
		int rl = itrunc(halo * lamp * 15.99);
		out.push(LV_ROOM + room_hue * 16 + (rl > 15 ? 15 : rl));
	}
	return out;
}

class Lava : Scene {
	array<float> bx = [];
	array<float> by = [];
	array<float> br2 = [];
	array<float> st = [];
	array<float> hue = [];
	array<float> period = [];
	array<float> phase = [];
	array<float> sway = [];
	int nb = 7;          // rising blobs per set; each set adds the pool and the cap's drip
	int sets = 3;
	float tt = 0.0;
	float beat = 0.48387097;

	// the camera's keyframes: time, span (world units the screen's height shows), roll, centre
	array<float> key_t = [0.0, 2.5, 4.8, 7.3, 9.4, 11.6];
	array<float> key_span = [0.30, 0.34, 1.45, 3.9, 3.0, 0.27];
	array<float> key_roll = [-0.18, 0.12, 0.5, 1.95, 3.3, 5.6];
	array<float> key_x = [0.0, 0.02, 0.0, 0.5, 1.9, 2.2];
	array<float> key_y = [0.52, 0.5, 0.52, 1.1, 2.55, 3.02];

	void build() override {
		name = "LAVALAMP";
		handle = "wax and gradient . by operator overload";
		// 24 hues x 16 shades (wax), then 24 hues x 8 glow levels (the liquid)
		for (int hq = 0; hq < 24; ++hq) {
			var c = gfx.rainbow_rgb(hq * 0.26 + 0.42);
			float cr = c[0]; float cg = c[1]; float cb = c[2];
			for (int s = 0; s < 16; ++s) {
				float f = s / 15.0;
				float lit = f < 0.75 ? 0.10 + f * 1.2 : 1.0;
				float wh = f < 0.75 ? 0.0 : (f - 0.75) * 3.6;
				wh = wh > 0.9 ? 0.9 : wh;
				float r = cr * lit;
				float g = cg * lit;
				float b = cb * lit;
				pal.add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh);
			}
		}
		LV_GLOW = pal.size();
		for (int hq2 = 0; hq2 < 24; ++hq2) {
			var c2 = gfx.rainbow_rgb(hq2 * 0.26 + 0.42);
			for (int g2 = 0; g2 < 8; ++g2) {
				float k = g2 / 7.0;
				k = k * k * 0.6;
				pal.add(18.0 + c2[0] * k, 8.0 + c2[1] * k, 36.0 + c2[2] * k);
			}
		}
		LV_GLASS = pal.ramp(8, 60.0, 50.0, 90.0, 245.0, 240.0, 255.0);
		LV_STEEL = pal.gradient(16, [[10.0, 10.0, 14.0], [60.0, 62.0, 72.0], [150.0, 154.0, 168.0], [235.0, 238.0, 250.0]]);
		// the room: each wax hue's halo on a dark wall, 16 levels
		LV_ROOM = pal.size();
		for (int hq3 = 0; hq3 < 24; ++hq3) {
			var c3 = gfx.rainbow_rgb(hq3 * 0.26 + 0.42);
			for (int r3 = 0; r3 < 16; ++r3) {
				float k3 = r3 / 15.0 * 0.42;
				pal.add(7.0 + c3[0] * k3, 6.0 + c3[1] * k3, 10.0 + c3[2] * k3);
			}
		}
		for (int s2 = 0; s2 < sets; ++s2) {
			for (int i = 0; i < nb; ++i) {
				float r = 0.024 + rng.next(40) * 0.0004;
				br2.push(r * r);
				hue.push(2.0 + rng.next(60) * 0.06);
				period.push(5.5 + rng.next(100) * 0.05);
				phase.push(rng.next(628) * 0.01);
				sway.push(rng.next(628) * 0.01);
				bx.push(0.0);
				by.push(0.0);
				st.push(1.0);
			}
			// the pool on the heater and the wax hanging under the cap
			br2.push(0.06 * 0.06);
			hue.push(3.5);
			bx.push(0.0);
			by.push(0.17);
			st.push(1.0);
			br2.push(0.022 * 0.022);
			hue.push(4.5);
			bx.push(0.0);
			by.push(0.87);
			st.push(1.0);
		}
		LV_N = nb + 2;
	}

	float smooth(float a, float b, float t) {
		float k = (t - a) / (b - a);
		k = k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
		return k * k * (3.0 - 2.0 * k);
	}

	float pulse(float t) {
		float ph = t / beat;
		float f = ph - math::floor(ph);
		return (1.0 - f) * (1.0 - f);
	}

	// a Catmull-Rom spline through one keyframe track, so the camera flows through
	// its marks instead of stopping at each
	float track(var& keys, float t) {
		int last = key_t.size() - 1;
		int k = 0;
		while (k < last - 1 && t >= key_t[k + 1]) { ++k; }
		float u = (t - key_t[k]) / (key_t[k + 1] - key_t[k]);
		u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
		float p0 = keys[k > 0 ? k - 1 : 0];
		float p1 = keys[k];
		float p2 = keys[k + 1];
		float p3 = keys[k + 2 <= last ? k + 2 : last];
		float u2 = u * u;
		float u3 = u2 * u;
		return 0.5 * (2.0 * p1 + (p2 - p0) * u + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * u2 + (3.0 * p1 - p0 - 3.0 * p2 + p3) * u3);
	}

	void update(float t, float dt) override {
		tt = t;
		// up off the heater, down as they cool, swaying to the glass; speed stretches them
		for (int s = 0; s < sets; ++s) {
			for (int i = 0; i < nb; ++i) {
				int k = s * nb + i;
				int slot = s * LV_N + i;
				float a = t * 6.2831853 / period[k] + phase[k];
				float y = 0.27 + 0.5 * (0.5 - 0.5 * math::cos(a));
				float vy = math::sin(a);
				float w = lamp_width(y);
				by[slot] = y;
				bx[slot] = w * 0.5 * math::sin(t * 0.37 + sway[k]);
				st[slot] = 1.0 + (vy < 0.0 ? -vy : vy) * 0.9;
				hue[slot] = 2.0 + i * 0.25;
			}
		}
	}

	void render(var& grid) override {
		float t = tt;
		// the zoom flies in log space, so pulling back feels steady at every scale
		var log_span = [];
		for (int k = 0; k < key_span.size(); ++k) { log_span.push(math::log2(key_span[k])); }
		float span = math::pow(2.0, track(log_span, t));
		float drive = smooth(4.8, 5.8, t) * (1.0 - smooth(9.4, 10.2, t));
		span *= 1.0 - pulse(t) * 0.07 * drive;
		float roll = track(key_roll, t) + 0.03 * math::sin(t * 7.1) * drive;
		LV_SCALE = span / h;
		LV_COS = math::cos(roll);
		LV_SIN = math::sin(roll);
		LV_CX = track(key_x, t);
		LV_CY = track(key_y, t);
		LV_BURN = smooth(6.9, 7.7, t) * (1.0 - smooth(9.6, 10.6, t));
		LV_T = t;
		LV_LAMP = 0.85 + 0.15 * math::sin(t * 1.3) + pulse(t) * (0.12 + 0.35 * LV_BURN);
		LV_BX = bx;
		LV_BY = by;
		LV_R2 = br2;
		LV_ST = st;
		LV_HUE = hue;
		LV_W = w;
		LV_H = h;
		if (LV_ROWS.size() != h) {
			LV_ROWS = [];
			for (int r = 0; r < h; ++r) { LV_ROWS.push(r); }
		}
		var rows = parallel_transform(LV_ROWS, lava_row);
		int idx = 0;
		for (int y = 0; y < h; ++y) {
			var& row = rows[y];
			for (int x = 0; x < w; ++x) {
				grid[idx] = row[x];
				++idx;
			}
		}
	}
}
