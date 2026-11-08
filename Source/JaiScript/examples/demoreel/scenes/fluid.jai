// MAELSTROM — ink in water, simulated: Jos Stam's stable fluids. The flow lives
// on a grid of half the pixel resolution: each frame the nozzles stir their jets
// in, vorticity confinement hands back the curl the grid smears away (a force
// along the gradient of the curl's magnitude), warm-started SOR projects the
// velocity divergence-free, and the velocity advects itself semi-Lagrangian
// (trace back, sample bilinear). The ink lives on the pixels: it traces back
// through the upsampled flow, so its filaments stay finer than the grid that
// moves them. The dye is a density plus its hue as a premultiplied (cos, sin)
// vector, so two inks blend into the hue between them and opposite hues wash out
// to pearl. The density is drawn as a height field, lit diffuse and Blinn
// specular from its gradient; motes ride the current. Jets cross, a ring of
// nozzles winds the tank up, the tank drains, and eight inks bloom into a
// rainbow wheel that a whirlpool twists. The ink and the light run their rows
// in parallel.
//
// Graded for the screen: caustic light ripples across the bare water, the jets
// pump their ink on the beat, a starburst of jets fires out from the middle of
// the tank, the frame darkens toward its corners, and at the
// end the camera pushes in on the whirlpool.
//
// Scored to its cue: the jets pump on the kick, the crossing jets' inks turn with
// each chord, the snare flares the ink's light and the bass the caustics, the
// energy feeds the curl, the ring's nozzles race through the bar before the burst,
// and the starburst (bar 4) and the bloom (bar 6) land as hits.

// Globals on purpose: the parallel row bodies read them (arrays borrow zero-copy).
array<float> MS_U = [];      // the flow, cells per second, on the half-size grid
array<float> MS_V = [];
array<float> MS_D = [];      // the ink per pixel: density,
array<float> MS_A = [];      // density * cos(hue),
array<float> MS_B = [];      // density * sin(hue)
array<float> MS_ZEROS = [];  // one ink row's result, cleared
array<int> MS_INNER = [];    // the rows the ink moves on (the walls stay dry)
array<int> MS_ALL = [];
int MS_LIFT = 0;             // the snare's flare on the ink, in shades
int MS_GLOW = 0;             // the bass's lift on the caustics
int MS_W = 0;
int MS_H = 0;
int MS_CW = 0;
int MS_CH = 0;
int MS_INK = 0;
int MS_BG = 0;               // 16 water tones x 4 caustic levels
array<float> MS_COS = [];    // a 256-entry cosine table for the caustics
array<float> MS_CCOL = [];   // per column: this frame's caustic phase
array<int> MS_VIG = [];      // per pixel: how far the vignette darkens it
float MS_T = 0.0;
float MS_RUN = 0.0;          // a frame's step, in pixels per (cell per second)
float MS_FADE = 1.0;

// one row of ink, on a parallel_transform worker: each pixel takes the flow at
// its centre (bilinear over the half-size grid), traces back one step and blends
// the four pixels it lands among; returns the row's density, then a, then b
function maelstrom_ink_row(int y) -> array<float> {
	int ww = MS_W;
	int gw = MS_CW;
	float gxmax = gw - 1.001;
	float gymax = MS_CH - 1.001;
	float xmax = ww - 1.001;
	float ymax = MS_H - 1.001;
	float run = MS_RUN;
	float fade = MS_FADE;
	array<float> out = MS_ZEROS;
	float qy = y * 0.5 - 0.25;
	qy = qy > gymax ? gymax : qy;
	int gy = qy;
	float ty = qy - gy;
	for (int x = 1; x < ww - 1; ++x) {
		float qx = x * 0.5 - 0.25;
		qx = qx > gxmax ? gxmax : qx;
		int gx = qx;
		float tx = qx - gx;
		int g = gy * gw + gx;
		float ut = MS_U[g] + (MS_U[g + 1] - MS_U[g]) * tx;
		float ub = MS_U[g + gw] + (MS_U[g + gw + 1] - MS_U[g + gw]) * tx;
		float vt = MS_V[g] + (MS_V[g + 1] - MS_V[g]) * tx;
		float vb = MS_V[g + gw] + (MS_V[g + gw + 1] - MS_V[g + gw]) * tx;
		float px = x - run * (ut + (ub - ut) * ty);
		float py = y - run * (vt + (vb - vt) * ty);
		px = px < 0.0 ? 0.0 : (px > xmax ? xmax : px);
		py = py < 0.0 ? 0.0 : (py > ymax ? ymax : py);
		int ix = px;
		int iy = py;
		float sx = px - ix;
		float sy = py - iy;
		int k = iy * ww + ix;
		float w00 = (1.0 - sx) * (1.0 - sy);
		float w10 = sx * (1.0 - sy);
		float w01 = (1.0 - sx) * sy;
		float w11 = sx * sy;
		out[x] = (w00 * MS_D[k] + w10 * MS_D[k + 1] + w01 * MS_D[k + ww] + w11 * MS_D[k + ww + 1]) * fade;
		out[ww + x] = (w00 * MS_A[k] + w10 * MS_A[k + 1] + w01 * MS_A[k + ww] + w11 * MS_A[k + ww + 1]) * fade;
		out[ww + ww + x] = (w00 * MS_B[k] + w10 * MS_B[k + 1] + w01 * MS_B[k + ww] + w11 * MS_B[k + ww + 1]) * fade;
	}
	return out;
}

// one row of the picture: the ink as a lit height field. Its gradient makes the
// normal, the light comes from the upper left, the hue is the angle of the ink's
// hue vector (pearl where opposite inks cancelled), specular glints top the crests
function maelstrom_row(int y) -> array<int> {
	int ww = MS_W;
	int hh = MS_H;
	int base = MS_INK;
	// caustics: two crossing waves, bright where their sum passes zero
	float rowp = 2.0 * math::sin(y * 0.17 + MS_T * 1.1);
	float yq = y * 0.23;
	int up = y > 0 ? -ww : 0;
	int down = y < hh - 1 ? ww : 0;
	float lx = -0.48;
	float ly = -0.56;
	float lz = 0.675;
	float hl = 1.0 / math::sqrt(lx * lx + ly * ly + (lz + 1.0) * (lz + 1.0));
	float hx = lx * hl;
	float hy = ly * hl;
	float hz = (lz + 1.0) * hl;
	array<int> out = [];
	int i = y * ww;
	for (int x = 0; x < ww; ++x) {
		float den = MS_D[i];
		int vig = MS_VIG[i];
		if (den < 0.03) {
			float cv = MS_COS[itrunc((x * 0.21 + rowp) * 40.743666) & 255] + MS_COS[itrunc((yq + MS_CCOL[x]) * 40.743666) & 255];
			cv = cv < 0.0 ? -cv : cv;
			int glow = cv < 0.24 ? itrunc((0.24 - cv) * 12.5) + 1 + MS_GLOW : 0;
			int tone = (y * 16 / hh) + vig * 2;
			out.push(MS_BG + (tone > 15 ? 15 : tone) * 4 + (glow > 3 ? 3 : glow));
			++i;
			continue;
		}
		float gx = MS_D[x < ww - 1 ? i + 1 : i] - MS_D[x > 0 ? i - 1 : i];
		float gy = MS_D[i + down] - MS_D[i + up];
		float nx = -gx * 2.6;
		float ny = -gy * 2.6;
		float il = 1.0 / math::sqrt(nx * nx + ny * ny + 1.0);
		float diff = (nx * lx + ny * ly + lz) * il;
		diff = diff < 0.0 ? 0.0 : diff;
		float s = (nx * hx + ny * hy + hz) * il;
		float s2 = s * s;
		float s4 = s2 * s2;
		float s8 = s4 * s4;
		float s32 = s8 * s8 * s8 * s8;
		float cover = den / (den + 0.3);
		int lv = cover * (0.42 + 0.78 * diff) * 16.0 - vig * 2 + MS_LIFT;
		lv = lv > 15 ? 15 : (lv < 0 ? 0 : lv);
		if (s32 > 0.4 && cover > 0.4) {
			int sp = 16 + (s32 - 0.4) * 6.6;
			lv = sp > 19 ? 19 : sp;
		}
		float av = MS_A[i];
		float bv = MS_B[i];
		int row = 24;
		if (av * av + bv * bv > den * den * 0.04) {
			int hq = math::atan2(bv, av) * 3.8197186 + 24.5;
			row = hq % 24;
		}
		out.push(base + row * 20 + lv);
		++i;
	}
	return out;
}

class Maelstrom : Scene {
	int cw = 0;                // the flow grid: cw x ch cells, two pixels to a cell
	int ch = 0;
	array<float> u1 = [];      // the flow advected
	array<float> v1 = [];
	array<float> pr = [];      // pressure, kept: each solve starts from the last one
	array<float> dv = [];      // divergence
	array<float> cu = [];      // curl
	array<float> mx = [];      // motes, in pixels
	array<float> my = [];
	int motes = 48;
	int ink = 0;               // 25 rows (24 hues, then pearl) x 20 levels (16 lit, 4 specular)
	int bg = 0;                // 16 water tones, top to bottom
	int mote = 0;
	int sor_iters = 20;
	float beat = 0.48387097;   // the soundtrack's 124 bpm
	array<int> col_of = [];    // the push-in: which column each screen column reads
	float vorticity = 14.0;
	float tt = 0.0;

	void build() override {
		name = "MAELSTROM";
		handle = "stable fluids . pressure, curl and ink";
		replay = 24.0;   // the water is its whole history
		cw = (w + 1) / 2;
		ch = (h + 1) / 2;
		int cn = cw * ch;
		MS_U = gfx.make_fgrid(cn);
		MS_V = gfx.make_fgrid(cn);
		u1 = gfx.make_fgrid(cn);
		v1 = gfx.make_fgrid(cn);
		pr = gfx.make_fgrid(cn);
		dv = gfx.make_fgrid(cn);
		cu = gfx.make_fgrid(cn);
		int n = w * h;
		MS_D = gfx.make_fgrid(n);
		MS_A = gfx.make_fgrid(n);
		MS_B = gfx.make_fgrid(n);
		MS_ZEROS = gfx.make_fgrid(w * 3);
		MS_INNER = [];
		for (int r = 1; r < h - 1; ++r) { MS_INNER.push(r); }
		MS_ALL = [];
		for (int r2 = 0; r2 < h; ++r2) { MS_ALL.push(r2); }
		ink = pal.size();
		for (int r3 = 0; r3 < 25; ++r3) {
			var c = r3 < 24 ? ink_rgb(r3 * 0.2617994) : [205.0, 214.0, 236.0];
			float cr = c[0]; float cg = c[1]; float cb = c[2];
			for (int l = 0; l < 16; ++l) {
				float f = (l + 0.5) / 16.0;
				f = f * (1.2 - 0.2 * f);
				pal.add(7.0 + (cr - 7.0) * f, 8.0 + (cg - 8.0) * f, 20.0 + (cb - 20.0) * f);
			}
			for (int s = 0; s < 4; ++s) {
				float k = 0.22 + s * 0.2;
				pal.add(cr + (255.0 - cr) * k, cg + (255.0 - cg) * k, cb + (255.0 - cb) * k);
			}
		}
		bg = pal.size();
		for (int t2 = 0; t2 < 16; ++t2) {
			float f2 = t2 / 15.0;
			float wr = 14.0 + (3.0 - 14.0) * f2;
			float wg = 20.0 + (4.0 - 20.0) * f2;
			float wb = 44.0 + (13.0 - 44.0) * f2;
			for (int c4 = 0; c4 < 4; ++c4) {
				float lift = c4 * 0.11 * (1.0 - f2 * 0.6);
				pal.add(wr + (90.0 - wr) * lift, wg + (170.0 - wg) * lift, wb + (230.0 - wb) * lift);
			}
		}
		MS_COS = [];
		for (int q = 0; q < 256; ++q) { MS_COS.push(math::cos(q * 0.024543693)); }
		MS_CCOL = gfx.make_fgrid(w);
		MS_VIG = gfx.make_grid(w * h);
		for (int vy = 0; vy < h; ++vy) {
			for (int vx = 0; vx < w; ++vx) {
				float ex = (vx + 0.5) / w * 2.0 - 1.0;
				float ey = (vy + 0.5) / h * 2.0 - 1.0;
				float r = ex * ex + ey * ey;
				MS_VIG[vy * w + vx] = r < 0.55 ? 0 : (r < 0.95 ? 1 : (r < 1.4 ? 2 : 3));
			}
		}
		col_of = gfx.make_grid(w);
		mote = pal.add(92, 116, 160);
		for (int k2 = 0; k2 < motes; ++k2) {
			mx.push(2.0 + rng.nextf() * (w - 4));
			my.push(2.0 + rng.nextf() * (h - 4));
		}
	}

	// a saturated ink of hue angle a (radians, red at 0)
	var ink_rgb(float a) {
		float h6 = a * 0.9549297;
		int sector = h6;
		float f = h6 - sector;
		float lo = 255.0 * 0.12;
		float up = lo + (255.0 - lo) * f;
		float dn = 255.0 - (255.0 - lo) * f;
		switch (sector % 6) {
			case 0: return [255.0, up, lo];
			case 1: return [dn, 255.0, lo];
			case 2: return [lo, 255.0, up];
			case 3: return [lo, dn, 255.0];
			case 4: return [up, lo, 255.0];
			default: return [255.0, lo, dn];
		}
	}

	array<float> hit_times() override { return [MUSIC_BAR * 4.0, MUSIC_BAR * 6.0]; }
	array<int> hit_levels() override { return [2, 2]; }

	void update(float t, float dt) override {
		tt = t;
		if (dt <= 0.0) { return; }
		vorticity = 10.0 + 8.0 * (cue == null ? 0.6 : cue.energy(t));
		float step = dt > 0.05 ? 0.05 : dt;
		stir(t, step);
		confine(step);
		project();
		advect_flow(step);
		advect_ink(step);
		drift_motes(step);
	}

	// the choreography, in pixels: crossing jets, then a ring of nozzles winding
	// the tank up; the tank drains, and eight inks bloom inward into a wheel the
	// whirlpool twists
	void stir(float t, float step) {
		float cx = w * 0.5;
		float cy = h * 0.5;
		// the jets pump on the kick; the crossing jets' inks turn with the chord
		float hit = cue == null ? 0.0 : cue.kick_env(t);
		float pump = 0.45 + 1.3 * hit;
		float tint = (cue == null ? 0 : cue.chord(t)) * 0.5235988;
		float rush = cue == null ? 0.0 : cue.riser(t);
		if (t < MUSIC_BAR * 3.0) {
			float pulse = (0.8 + 0.2 * math::sin(t * 2.7)) * (0.85 + 0.3 * hit);
			jet(3.0, h * 0.30, 1.0, 0.10, 70.0 * pulse, 5.3 + tint + 0.3 * math::sin(t * 0.9), 24.0 * pump, 3.0 * h / 120.0, step);
			jet(w - 4.0, h * 0.70, -1.0, -0.10, 70.0 * pulse, 2.9 + tint + 0.3 * math::sin(t * 1.1), 24.0 * pump, 3.0 * h / 120.0, step);
		}
		if (t > MUSIC_BAR * 2.5 && t < 10.177) {
			float ring = h * 0.38;
			float spin = t * 0.6 + rush * rush * 1.4;
			for (int k = 0; k < 4; ++k) {
				float a = spin + k * 1.5707963;
				float ca = math::cos(a);
				float sa = math::sin(a);
				jet(cx + ca * ring * 1.2, cy + sa * ring, -sa - ca * 0.35, ca - sa * 0.35, 56.0,
					k * 1.5707963 + t * 0.45, 9.0 * pump, 2.4 * h / 120.0, step);
			}
		}
		// the burst: twelve jets fire outward from the middle, a rainbow starburst
		if (t > MUSIC_BAR * 4.0 && t < MUSIC_BAR * 4.0 + 0.7) {
			for (int k3 = 0; k3 < 12; ++k3) {
				float a3 = k3 * 0.5235988 + 0.2;
				float c3 = math::cos(a3);
				float s3 = math::sin(a3);
				jet(cx + c3 * h * 0.1, cy + s3 * h * 0.1, c3, s3, 170.0, a3, 30.0 * pump, 3.6 * h / 120.0, step);
			}
		}
		if (t > MUSIC_BAR * 5.0) { vortex(cx, cy, swirl(t), step); }
		if (t > MUSIC_BAR * 6.0 && t < MUSIC_BAR * 6.0 + 1.0) {
			for (int k2 = 0; k2 < 8; ++k2) {
				float a2 = k2 * 0.7853982 + 0.3;
				float c2 = math::cos(a2);
				float s2 = math::sin(a2);
				jet(cx + c2 * h * 0.34, cy + s2 * h * 0.3, -c2, -s2, 84.0, a2, 22.0, 2.6 * h / 120.0, step);
			}
		}
	}

	// the whirlpool's strength: it winds up with the ring, eases off while the
	// bloom fires, then takes over
	float swirl(float t) {
		if (t < 11.577) { return (t - MUSIC_BAR * 5.0) * 0.3; }
		if (t < 12.377) { return 0.25; }
		float k = 0.25 + (t - 12.377) * 0.6;
		return k > 1.0 ? 1.0 : k;
	}

	// a nozzle at pixel (x, y) shooting `speed` pixels a second along (dx, dy):
	// it pulls the flow in a disc toward its jet and adds ink of hue angle `hue`,
	// densest at the centre
	void jet(float x, float y, float dx, float dy, float speed, float hue, float rate, float rad, float step) {
		var& uu = MS_U;
		var& vv = MS_V;
		float il = 0.5 / math::sqrt(dx * dx + dy * dy);
		float jx = dx * il * speed;
		float jy = dy * il * speed;
		float qx = x * 0.5 - 0.25;
		float qy = y * 0.5 - 0.25;
		float qr = rad * 0.5 + 0.75;
		float qr2 = qr * qr;
		int a0 = math::max(1, itrunc(qx - qr));
		int a1 = math::min(cw - 2, itrunc(qx + qr) + 1);
		int b0 = math::max(1, itrunc(qy - qr));
		int b1 = math::min(ch - 2, itrunc(qy + qr) + 1);
		for (int gy = b0; gy <= b1; ++gy) {
			float ey = gy - qy;
			for (int gx = a0; gx <= a1; ++gx) {
				float ex = gx - qx;
				float q = ex * ex + ey * ey;
				if (q >= qr2) { continue; }
				float f = (1.0 - q / qr2) * 0.6;
				int i = gy * cw + gx;
				uu[i] += (jx - uu[i]) * f;
				vv[i] += (jy - vv[i]) * f;
			}
		}
		var& d = MS_D;
		var& a = MS_A;
		var& b = MS_B;
		int ww = w;
		float hc = math::cos(hue);
		float hs = math::sin(hue);
		float r2 = rad * rad;
		int x0 = math::max(1, itrunc(x - rad));
		int x1 = math::min(ww - 2, itrunc(x + rad) + 1);
		int y0 = math::max(1, itrunc(y - rad));
		int y1 = math::min(h - 2, itrunc(y + rad) + 1);
		for (int py = y0; py <= y1; ++py) {
			float ey2 = py - y;
			for (int px = x0; px <= x1; ++px) {
				float ex2 = px - x;
				float q2 = ex2 * ex2 + ey2 * ey2;
				if (q2 >= r2) { continue; }
				float add = rate * (1.0 - q2 / r2) * step;
				int j = py * ww + px;
				float nd = d[j] + add;
				float na = a[j] + add * hc;
				float nb = b[j] + add * hs;
				float k = nd > 3.0 ? 3.0 / nd : 1.0;
				d[j] = nd * k;
				a[j] = na * k;
				b[j] = nb * k;
			}
		}
	}

	// a whirlpool at pixel (x, y): spin about it, falling off with distance
	void vortex(float x, float y, float amount, float step) {
		var& uu = MS_U;
		var& vv = MS_V;
		int gw = cw;
		float qx = x * 0.5 - 0.25;
		float qy = y * 0.5 - 0.25;
		float s2 = ch * ch * 0.09;
		float k = amount * step * 46.0 / s2;
		for (int gy = 1; gy < ch - 1; ++gy) {
			float ey = gy - qy;
			int i = gy * gw + 1;
			for (int gx = 1; gx < gw - 1; ++gx) {
				float ex = gx - qx;
				float f = k / (1.0 + (ex * ex + ey * ey) / s2);
				uu[i] -= ey * f;
				vv[i] += ex * f;
				++i;
			}
		}
	}

	// vorticity confinement: push along N x curl, N the unit gradient of |curl|
	void confine(float step) {
		var& uu = MS_U;
		var& vv = MS_V;
		var& c = cu;
		int gw = cw;
		int gh = ch;
		for (int y = 1; y < gh - 1; ++y) {
			int i = y * gw + 1;
			for (int x = 1; x < gw - 1; ++x) {
				c[i] = 0.5 * (vv[i + 1] - vv[i - 1] - uu[i + gw] + uu[i - gw]);
				++i;
			}
		}
		float eps = vorticity * step;
		for (int y2 = 2; y2 < gh - 2; ++y2) {
			int j = y2 * gw + 2;
			for (int x2 = 2; x2 < gw - 2; ++x2) {
				float cr = c[j + 1];
				float cl = c[j - 1];
				float cd = c[j + gw];
				float cn = c[j - gw];
				float gx = (cr < 0.0 ? -cr : cr) - (cl < 0.0 ? -cl : cl);
				float gy = (cd < 0.0 ? -cd : cd) - (cn < 0.0 ? -cn : cn);
				float f = eps * c[j] / (math::sqrt(gx * gx + gy * gy) + 0.00001);
				uu[j] += gy * f;
				vv[j] -= gx * f;
				++j;
			}
		}
	}

	// make the flow divergence-free: solve laplace(p) = div(u) by successive
	// over-relaxation from last frame's pressure (walls are Neumann: a border cell
	// copies its inner neighbour), then subtract grad(p)
	void project() {
		var& uu = MS_U;
		var& vv = MS_V;
		var& p = pr;
		var& dvg = dv;
		int gw = cw;
		int gh = ch;
		for (int y = 1; y < gh - 1; ++y) {
			int i = y * gw + 1;
			for (int x = 1; x < gw - 1; ++x) {
				dvg[i] = 0.5 * (uu[i + 1] - uu[i - 1] + vv[i + gw] - vv[i - gw]);
				++i;
			}
		}
		int last = (gh - 1) * gw;
		for (int it = 0; it < sor_iters; ++it) {
			for (int y2 = 1; y2 < gh - 1; ++y2) {
				int j = y2 * gw + 1;
				for (int x2 = 1; x2 < gw - 1; ++x2) {
					float g = (p[j - 1] + p[j + 1] + p[j - gw] + p[j + gw] - dvg[j]) * 0.25;
					p[j] += 1.7 * (g - p[j]);
					++j;
				}
			}
			for (int x3 = 0; x3 < gw; ++x3) {
				p[x3] = p[x3 + gw];
				p[last + x3] = p[last - gw + x3];
			}
			for (int y3 = 0; y3 < gh; ++y3) {
				int r = y3 * gw;
				p[r] = p[r + 1];
				p[r + gw - 1] = p[r + gw - 2];
			}
		}
		for (int y4 = 1; y4 < gh - 1; ++y4) {
			int k = y4 * gw + 1;
			for (int x4 = 1; x4 < gw - 1; ++x4) {
				uu[k] -= 0.5 * (p[k + 1] - p[k - 1]);
				vv[k] -= 0.5 * (p[k + gw] - p[k - gw]);
				++k;
			}
		}
	}

	// semi-Lagrangian: each cell traces its velocity back one step and takes the
	// bilinear blend of the four cells it lands among; the walls hold still and
	// the water slowly calms
	void advect_flow(float step) {
		var& uu = MS_U;
		var& vv = MS_V;
		var& un = u1;
		var& vn = v1;
		int gw = cw;
		int gh = ch;
		float xmax = gw - 1.001;
		float ymax = gh - 1.001;
		float keep = 1.0 - 0.15 * step;
		for (int y = 1; y < gh - 1; ++y) {
			int i = y * gw + 1;
			for (int x = 1; x < gw - 1; ++x) {
				float px = x - step * uu[i];
				float py = y - step * vv[i];
				px = px < 0.0 ? 0.0 : (px > xmax ? xmax : px);
				py = py < 0.0 ? 0.0 : (py > ymax ? ymax : py);
				int ix = px;
				int iy = py;
				float sx = px - ix;
				float sy = py - iy;
				int k = iy * gw + ix;
				float top = uu[k] + (uu[k + 1] - uu[k]) * sx;
				float bot = uu[k + gw] + (uu[k + gw + 1] - uu[k + gw]) * sx;
				un[i] = (top + (bot - top) * sy) * keep;
				float top2 = vv[k] + (vv[k + 1] - vv[k]) * sx;
				float bot2 = vv[k + gw] + (vv[k + gw + 1] - vv[k + gw]) * sx;
				vn[i] = (top2 + (bot2 - top2) * sy) * keep;
				++i;
			}
		}
		for (int y2 = 1; y2 < gh - 1; ++y2) {
			int j = y2 * gw + 1;
			for (int x2 = 1; x2 < gw - 1; ++x2) {
				uu[j] = un[j];
				vv[j] = vn[j];
				++j;
			}
		}
	}

	// the ink rows in parallel, written back once every row has read the old ink;
	// the tank drains faster before the bloom
	void advect_ink(float step) {
		MS_W = w;
		MS_H = h;
		MS_CW = cw;
		MS_CH = ch;
		MS_RUN = step * 2.0;
		MS_FADE = 1.0 - (tt > 10.177 && tt < 11.977 ? 0.9 : 0.22) * step;
		var rows = parallel_transform(MS_INNER, maelstrom_ink_row);
		var& d = MS_D;
		var& a = MS_A;
		var& b = MS_B;
		int ww = w;
		for (int r = 0; r < rows.size(); ++r) {
			var& row = rows[r];
			int i = (r + 1) * ww;
			for (int x = 1; x < ww - 1; ++x) {
				d[i + x] = row[x];
				a[i + x] = row[ww + x];
				b[i + x] = row[ww + ww + x];
			}
		}
	}

	// motes ride the current; one that leaves the water, or now and then any
	// one, comes back somewhere new
	void drift_motes(float step) {
		var& uu = MS_U;
		var& vv = MS_V;
		int gw = cw;
		float run = step * 2.0;
		for (int k = 0; k < motes; ++k) {
			float x = mx[k];
			float y = my[k];
			int g = itrunc(y * 0.5) * gw + itrunc(x * 0.5);
			x += run * uu[g];
			y += run * vv[g];
			if (x < 1.5 || x > w - 2.5 || y < 1.5 || y > h - 2.5 || rng.next(240) == 0) {
				x = 2.0 + rng.nextf() * (w - 4);
				y = 2.0 + rng.nextf() * (h - 4);
			}
			mx[k] = x;
			my[k] = y;
		}
	}

	void render(var& grid) override {
		MS_W = w;
		MS_H = h;
		MS_INK = ink;
		MS_BG = bg;
		MS_T = tt;
		float snare = cue == null ? 0.0 : cue.snare_env(tt);
		float flash = cue == null ? 0.0 : cue.hit(tt);
		MS_LIFT = itrunc(snare * 2.5 + flash * 3.0);
		MS_GLOW = itrunc((cue == null ? 0.0 : cue.bass_env(tt)) * 1.5);
		var& cc = MS_CCOL;
		for (int c = 0; c < w; ++c) { cc[c] = 2.0 * math::sin(c * 0.13 - tt * 0.9); }
		var rows = parallel_transform(MS_ALL, maelstrom_row);
		// the push-in on the whirlpool over the last seconds
		float push = tt > 11.977 ? (tt - 11.977) / 3.2 : 0.0;
		push = push > 1.0 ? 1.0 : push * push * (3.0 - 2.0 * push);
		float zoom = 1.0 + push * 0.45;
		int ww = w;
		var& cols = col_of;
		for (int x0 = 0; x0 < ww; ++x0) {
			int sx = itrunc(ww * 0.5 + (x0 + 0.5 - ww * 0.5) / zoom);
			cols[x0] = sx < 0 ? 0 : (sx >= ww ? ww - 1 : sx);
		}
		int idx = 0;
		for (int y = 0; y < h; ++y) {
			int sy = itrunc(h * 0.5 + (y + 0.5 - h * 0.5) / zoom);
			var& row = rows[sy < 0 ? 0 : (sy >= h ? h - 1 : sy)];
			for (int x = 0; x < ww; ++x) {
				grid[idx] = row[cols[x]];
				++idx;
			}
		}
		for (int k = 0; k < motes; ++k) {
			int px = itrunc(ww * 0.5 + (mx[k] - ww * 0.5) * zoom);
			int py = itrunc(h * 0.5 + (my[k] - h * 0.5) * zoom);
			if (px >= 0 && px < ww && py >= 0 && py < h) { grid[py * ww + px] = mote; }
		}
	}
}
