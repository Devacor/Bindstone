// LAVALAMP — metaballs, lit, in a lamp. The shot opens in macro on the wax and
// pulls back to show where it lives: a tapered glass vessel on a steel base under
// a steel cap, in a dark room its glow lights. Eight blobs rise from the heat
// at the bottom and sink as they cool, stretched into taffy by their speed, over a
// pool that never leaves the base. Per pixel one loop sums the inverse-square
// field, its gradient and a contribution-weighted hue, so merging blobs blend
// their colours into a bridge, and the wax is lit from a normal built out of the
// gradient (diffuse + a sharp specular), bulging out of the glass; between blobs
// the liquid glows with the wax's light, the glass carries a fresnel edge and a
// highlight stripe, the steel a studio reflection, the wall a halo of the lamp's
// colour. On the beat the light pulses and the wax burns through the rainbow
// before the camera pushes back into the glass. Rows run in parallel.

array<float> LV_BX = [];
array<float> LV_BY = [];
array<float> LV_R2 = [];
array<float> LV_ST = [];     // each blob's stretch: its speed pulls it into taffy
array<float> LV_HUE = [];
array<int> LV_ROWS = [];
int LV_W = 0;
int LV_H = 0;
int LV_GLOW = 0;
int LV_GLASS = 0;
int LV_STEEL = 0;
int LV_ROOM = 0;
float LV_CX = 0.0;           // the camera: the lamp-space point at the screen's centre
float LV_CY = 0.5;
float LV_SCALE = 0.004;      // and lamp units per pixel
float LV_LAMP = 1.0;         // how bright the lamp burns
float LV_HUE0 = 0.0;         // the room's tint: the wax's hue

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
	int n = LV_BX.size();
	float sc = LV_SCALE;
	float ly = LV_CY - (y + 0.5 - LV_H * 0.5) * sc;
	float hw = lamp_width(ly);
	float lux = -0.45;
	float luy = 0.55;
	float luz = 0.70;
	float hl = 1.0 / math::sqrt(lux * lux + luy * luy + (luz + 1.0) * (luz + 1.0));
	float hx = lux * hl;
	float hy = luy * hl;
	float hz = (luz + 1.0) * hl;
	float lamp = LV_LAMP;
	int room_hue = itrunc(LV_HUE0) % 24;
	for (int x = 0; x < ww; ++x) {
		float lx = LV_CX + (x + 0.5 - ww * 0.5) * sc;
		float across = lx / hw;
		float aa = across < 0.0 ? -across : across;
		if (ly >= 0.2 && ly <= 0.84 && aa < 1.0) {
			float f = 0.0;
			float gx = 0.0;
			float gy = 0.0;
			float hsum = 0.0;
			for (int b = 0; b < n; ++b) {
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
			int hq = itrunc(hsum / f + 0.5) % 24;
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
		// the room: the wall takes a halo of the lamp's colour; below the foot, the
		// table catches a pool of it
		float rx = lx;
		float ry = ly - 0.5;
		float halo = 0.0;
		if (ly < 0.0) {
			float pool = 1.0 / (1.0 + (rx * rx * 18.0 + ly * ly * 260.0));
			halo = pool * 1.1;
		} else {
			halo = 1.0 / (1.0 + (rx * rx * 9.0 + ry * ry * 3.0));
			halo = halo * halo;
		}
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
	int nb = 7;
	float tt = 0.0;
	float beat = 0.48387097;

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

	void update(float t, float dt) override {
		tt = t;
		// up off the heater, down as they cool, swaying to the glass; speed stretches them
		for (int i = 0; i < nb; ++i) {
			float a = t * 6.2831853 / period[i] + phase[i];
			float y = 0.27 + 0.5 * (0.5 - 0.5 * math::cos(a));
			float vy = math::sin(a);
			float w = lamp_width(y);
			by[i] = y;
			bx[i] = w * 0.5 * math::sin(t * 0.37 + sway[i]);
			st[i] = 1.0 + (vy < 0.0 ? -vy : vy) * 0.9;
		}
		// the hues: a warm family, burning through the rainbow at the peak
		float burn = smooth(7.0, 8.2, t) * (1.0 - smooth(10.2, 11.2, t));
		for (int j = 0; j < nb; ++j) {
			float base = 2.0 + j * 0.25;
			hue[j] = base + burn * (j * 2.3 + t * 9.0);
			while (hue[j] >= 24.0) { hue[j] -= 24.0; }
		}
	}

	void render(var& grid) override {
		float t = tt;
		// macro on the wax, the pull back to the whole lamp, a sway, the push back in
		float out_k = smooth(2.6, 5.4, t) * (1.0 - smooth(10.0, 11.8, t));
		float span = 0.3 + (1.12 - 0.3) * out_k;
		LV_SCALE = span / h;
		LV_CX = 0.02 * math::sin(t * 0.7) * out_k + 0.03 * (1.0 - out_k) * math::sin(t * 0.5);
		LV_CY = 0.5 + (0.48 - 0.5) * out_k + 0.06 * (1.0 - out_k) * math::sin(t * 0.43);
		float burn = smooth(7.0, 8.2, t) * (1.0 - smooth(10.2, 11.2, t));
		LV_LAMP = 0.85 + 0.15 * math::sin(t * 1.3) + pulse(t) * (0.12 + 0.35 * burn);
		LV_HUE0 = hue[0];
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
