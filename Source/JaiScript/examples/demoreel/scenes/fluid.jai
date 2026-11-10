// MAELSTROM — neon ink in black water, seen from above and simulated: Jos Stam's
// stable fluids. The flow lives on a grid a third of the pixel resolution: each
// frame the forces stir it, vorticity confinement hands back the curl the grid
// smears away (a force along the gradient of the curl's magnitude), warm-started
// SOR projects the velocity divergence-free, and the velocity advects itself
// semi-Lagrangian (trace back, sample bilinear). The ink is one number a cell on a
// grid twice as fine, carried by the same flow. The picture reads it as marbling
// reads a tray: the amount of ink picks a place along a row of eight neon tubes
// with dark water between them, so wherever the ink changes it draws glowing
// contours, the flow folds them, and no amount of stirring blends them to mud.
// The ink also settles, slowly, toward a ramp across the tank, which is what
// keeps every hue in the water however long it is stirred. The camera samples
// the ink bilinear, rolled, zoomed and panned, so a contour stays a clean curve
// at any zoom.
//
// The tank opens banded like Jupiter: opposing jets whose shear rolls the bands
// up into billows. A counter-rotating pair of vortices folds them while six
// nozzles orbit the tank and a rod drags a figure-eight through it; the pair
// gives way to one whirlpool whose ramp turns with it, so the bands sweep round
// as arms the faster middle winds ahead of; the whirlpool reverses, and a drain
// opens that swallows the tank into its eye.
//
// Scored to its cue: three nozzles pour on the kick and three draw ink out on the
// snare, the kick surges the jets, makes the tubes throb and punches the camera
// in, the energy feeds the curl, each kick over the whirlpool sends a ring of
// light out from its eye, and the hits land as rings that shove the ink outward:
// the pair (bar 2), the whirlpool (bar 4, a shock whose wake lays the ink back
// in clean bands, the picture flashing negative), the reversal (bar 6, every
// tube trading its colour) and the drain (bar 8).

// Globals on purpose: the parallel row bodies read them (arrays borrow zero-copy).
array<float> MS_FU = [];     // the flow that carries the ink, flow cells per second
array<float> MS_FV = [];
array<float> MS_INK = [];    // each ink cell's ink, in palette entries
array<float> MS_HOME_X = []; // the ink a cell settles back to: its column's share
array<float> MS_HOME_Y = []; // plus its row's,
array<float> MS_SETTLE = []; // and by whole ink cells of radius from the tank's middle, the share of the way it goes in a frame
array<int> MS_RADIUS = [];   // per ink cell: that radius
array<int> MS_SHADE = [];    // by a quarter of the squared radius: the brightness level's first palette entry
array<int> MS_INK_ROWS = [];
array<int> MS_ROWS = [];
int MS_W = 0;
int MS_DW = 0;               // the ink grid
int MS_DH = 0;
int MS_CW = 0;               // the flow grid
int MS_CH = 0;
float MS_RUN = 0.0;          // a frame's step, in ink cells per (flow cell per second)
float MS_CX = 0.0;           // the tank's middle, in ink cells
float MS_CY = 0.0;
float MS_OX = 0.0;           // the camera: pixel (x, y) shows ink cell (OX + AX x + BX y, OY + AY x + BY y)
float MS_OY = 0.0;
float MS_AX = 1.0;
float MS_AY = 0.0;
float MS_BX = 0.0;
float MS_BY = 1.0;
int MS_TUBES = 0;            // the first palette entry of the tubes as drawn this frame
int MS_PHASE = 0;            // how far along the tubes every cell's ink is shifted

// one row of ink, on a parallel_transform worker: each cell takes the flow at its
// centre (bilinear over the flow grid, stepped along the row), traces back one
// step, blends the four cells it lands among and settles toward home
function maelstrom_ink_row(int y) -> array<float> {
	int dw = MS_DW;
	array<float> out = [];
	out.resize(dw, 0.0);
	if (y < 1 || y >= MS_DH - 1) { return out; }
	int gw = MS_CW;
	float gymax = MS_CH - 1.001;
	float xmax = dw - 2.001;
	float ymax = MS_DH - 2.001;
	float run = MS_RUN;
	float home_y = MS_HOME_Y[y];
	float qy = y * 0.5 - 0.25;
	qy = qy > gymax ? gymax : qy;
	int gy = cast<int>(qy);
	float ty = qy - gy;
	int g = gy * gw;
	float ua = MS_FU[g] + (MS_FU[g + gw] - MS_FU[g]) * ty;
	float va = MS_FV[g] + (MS_FV[g + gw] - MS_FV[g]) * ty;
	int x = 1;
	int radius_at = y * dw;
	for (int gx = 1; gx < gw; ++gx) {
		++g;
		float ub = MS_FU[g] + (MS_FU[g + gw] - MS_FU[g]) * ty;
		float vb = MS_FV[g] + (MS_FV[g + gw] - MS_FV[g]) * ty;
		float du = (ub - ua) * 0.5;
		float dv = (vb - va) * 0.5;
		float u = ua + du * 0.5;
		float v = va + dv * 0.5;
		int xe = x + 2;
		while (x < xe) {
			float px = x - run * u;
			float py = y - run * v;
			px = px < 1.0 ? 1.0 : (px > xmax ? xmax : px);
			py = py < 1.0 ? 1.0 : (py > ymax ? ymax : py);
			int ix = cast<int>(px);
			int iy = cast<int>(py);
			float sx = px - ix;
			int k = iy * dw + ix;
			int k2 = k + dw;
			float a = MS_INK[k];
			float b = MS_INK[k2];
			float top = a + (MS_INK[k + 1] - a) * sx;
			float bottom = b + (MS_INK[k2 + 1] - b) * sx;
			float ink = top + (bottom - top) * (py - iy);
			out[x] = ink + (home_y + MS_HOME_X[x] - ink) * MS_SETTLE[MS_RADIUS[radius_at + x]];
			u += du;
			v += dv;
			++x;
		}
		ua = ub;
		va = vb;
	}
	return out;
}

// one row of the picture, on a parallel_transform worker: the camera's line across
// the ink, clipped to the tank; each pixel blends the four ink cells around it and
// takes that much ink's place along the tubes, at the shade of its radius
function maelstrom_row(int y) -> array<int> {
	int ww = MS_W;
	int tubes = MS_TUBES;
	int phase = MS_PHASE;
	array<int> out = [];
	out.resize(ww, tubes);
	float ax = MS_AX;
	float ay = MS_AY;
	float fx = MS_OX + MS_BX * y;
	float fy = MS_OY + MS_BY * y;
	float xhi = MS_DW - 2.01;
	float yhi = MS_DH - 2.01;
	float from = 0.0;
	float to = ww - 1.0;
	if (ax > 0.00001) {
		from = math::max(from, (1.0 - fx) / ax);
		to = math::min(to, (xhi - fx) / ax);
	} else if (ax < -0.00001) {
		from = math::max(from, (xhi - fx) / ax);
		to = math::min(to, (1.0 - fx) / ax);
	} else if (fx < 1.0 || fx > xhi) {
		return out;
	}
	if (ay > 0.00001) {
		from = math::max(from, (1.0 - fy) / ay);
		to = math::min(to, (yhi - fy) / ay);
	} else if (ay < -0.00001) {
		from = math::max(from, (yhi - fy) / ay);
		to = math::min(to, (1.0 - fy) / ay);
	} else if (fy < 1.0 || fy > yhi) {
		return out;
	}
	if (to < from) { return out; }
	int x0 = cast<int>(from);
	if (x0 < from) { ++x0; }
	int x1 = cast<int>(to);
	fx += ax * x0;
	fy += ay * x0;
	int dw = MS_DW;
	float ex = fx - MS_CX;
	float ey = fy - MS_CY;
	for (int x = x0; x <= x1; ++x) {
		int ix = cast<int>(fx);
		int iy = cast<int>(fy);
		float sx = fx - ix;
		int k = iy * dw + ix;
		int k2 = k + dw;
		float a = MS_INK[k];
		float b = MS_INK[k2];
		float top = a + (MS_INK[k + 1] - a) * sx;
		float bottom = b + (MS_INK[k2 + 1] - b) * sx;
		int ink = cast<int>(top + (bottom - top) * (fy - iy));
		int far = cast<int>((ex * ex + ey * ey) * 0.25);
		out[x] = tubes + MS_SHADE[far] + ((ink + phase) & 127);
		ex += ax;
		ey += ay;
		fx += ax;
		fy += ay;
	}
	return out;
}

class Maelstrom : Scene {
	int cw = 0;                  // the flow grid: cw x ch cells, three pixels to a cell
	int ch = 0;
	int dw = 0;                  // the ink grid: two cells to a flow cell
	int dh = 0;
	float unit = 1.0;            // flow cells to one of the reference tank's (40 rows)
	array<float> u = [];         // the flow, flow cells per second,
	array<float> v = [];
	array<float> u1 = [];        // and advected
	array<float> v1 = [];
	array<float> pr = [];        // pressure, kept: each solve starts from the last one
	array<float> dv = [];        // divergence
	array<float> cu = [];        // curl
	array<float> away_x = [];    // per flow cell: the unit vector away from the tank's middle,
	array<float> away_y = [];
	array<int> away_r = [];      // and its radius in whole ink cells
	array<float> radial = [];    // by that radius: the speed outward the rings and the drain give the ink
	array<float> ring_born = [];
	array<float> ring_power = [];
	array<float> ring_lays = [];     // 1 for a shock whose wake lays the ink back in its bands
	int ring_next = 0;
	array<float> level = [];         // by that radius: the brightness level, before rounding
	array<float> shade_radius = [];  // the radius each of MS_SHADE's entries stands for
	array<float> neon = [        // the tubes: red, green and blue light each
		1.00, 0.08, 0.55,
		0.00, 0.80, 1.00,
		1.00, 0.84, 0.04,
		0.58, 0.20, 1.00,
		0.55, 1.00, 0.00,
		1.00, 0.38, 0.00,
		0.10, 0.35, 1.00,
		0.00, 1.00, 0.50
	];
	int tubes = 0;               // the palette's 128 entries along the tubes, at 10 brightness levels, then all of it negative
	int jets = 5;
	int sor_iters = 8;
	float vorticity = 12.0;
	float jet_speed = 14.0;      // the opening's jets
	float settling = 0.18;       // the share of the way home the ink settles in a second,
	float pool_settling = 0.6;   // and in the whirlpool, where home turns with the water
	float home_rate = 1.4;       // this many radians a second
	float home_turned = 0.0;
	float pair_speed = 16.0;     // how fast each vortex turns at its core's edge, flow cells a second
	float pool_speed = 18.0;
	float drain_speed = 14.0;
	float tt = 0.0;
	float turn = 1.0;            // the way the whirlpool winds
	float orbit = 0.0;           // where the nozzles' ring has turned to
	int traded = 0;              // the tubes' colours, moved along by the reversal
	float kick_before = 0.0;

	float t_pair = MUSIC_BAR * 2.0;
	float t_pool = MUSIC_BAR * 4.0;
	float t_flip = MUSIC_BAR * 6.0;
	float t_drain = MUSIC_BAR * 8.0;

	// the reel picks the grid before build runs, so the budget is set at construction
	Maelstrom() { pixel_budget = 30000; }

	int length_bars() override { return 10; }
	array<float> hit_times() override { return [t_pair, t_pool, t_flip, t_drain]; }
	array<int> hit_levels() override { return [1, 2, 1, 2]; }

	void build() override {
		name = "MAELSTROM";
		handle = "stable fluids . pressure, curl and neon ink";
		replay = 20.0;   // the water is its whole history
		cw = (w + 2) / 3;
		ch = (h + 2) / 3;
		dw = cw * 2;
		dh = ch * 2;
		unit = ch / 40.0;
		int cn = cw * ch;
		u = gfx.make_fgrid(cn);
		v = gfx.make_fgrid(cn);
		u1 = gfx.make_fgrid(cn);
		v1 = gfx.make_fgrid(cn);
		pr = gfx.make_fgrid(cn);
		dv = gfx.make_fgrid(cn);
		cu = gfx.make_fgrid(cn);
		MS_W = w;
		MS_DW = dw;
		MS_DH = dh;
		MS_CW = cw;
		MS_CH = ch;
		MS_CX = dw * 0.5 - 0.5;
		MS_CY = dh * 0.5 - 0.5;
		MS_INK = gfx.make_fgrid(dw * dh);
		MS_HOME_X = gfx.make_fgrid(dw);
		MS_HOME_Y = gfx.make_fgrid(dh);
		MS_INK_ROWS = [];
		for (int r = 0; r < dh; ++r) { MS_INK_ROWS.push(r); }
		MS_ROWS = [];
		for (int r2 = 0; r2 < h; ++r2) { MS_ROWS.push(r2); }
		mix_tubes();
		int reach = math::itrunc(math::sqrt(dw * dw + dh * dh * 1.0) * 0.5) + 3;
		MS_SETTLE = gfx.make_fgrid(reach);
		radial = gfx.make_fgrid(reach);
		level = gfx.make_fgrid(reach);
		int shades = (dw * dw + dh * dh) / 16 + 2;
		MS_SHADE = gfx.make_grid(shades);
		for (int q = 0; q < shades; ++q) { shade_radius.push(math::sqrt(q * 4.0 + 2.0)); }
		MS_RADIUS = gfx.make_grid(dw * dh);
		for (int y = 0; y < dh; ++y) {
			float ey = y + 0.5 - dh * 0.5;
			for (int x = 0; x < dw; ++x) {
				float ex = x + 0.5 - dw * 0.5;
				MS_RADIUS[y * dw + x] = math::itrunc(math::sqrt(ex * ex + ey * ey));
			}
		}
		away_x = gfx.make_fgrid(cn);
		away_y = gfx.make_fgrid(cn);
		away_r = gfx.make_grid(cn);
		for (int gy = 0; gy < ch; ++gy) {
			float fy = gy - (ch - 1) * 0.5;
			for (int gx = 0; gx < cw; ++gx) {
				float fx = gx - (cw - 1) * 0.5;
				float far = math::sqrt(fx * fx + fy * fy);
				int i = gy * cw + gx;
				away_x[i] = fx / (far + 0.0001);
				away_y[i] = fy / (far + 0.0001);
				away_r[i] = math::itrunc(far * 2.0);
			}
		}
		for (int k = 0; k < 8; ++k) {
			ring_born.push(-100.0);
			ring_power.push(0.0);
			ring_lays.push(0.0);
		}
		lay_bands();
		for (int settle = 0; settle < 12; ++settle) { project(); }
		carry_ink(0.0, 0.0);
	}

	// the palette: eight tubes of sixteen entries each, a tube's light swelling from the
	// dark water at its edges to a white-hot core; ten levels, from black through full
	// light to the tubes burning white; then the negative of every entry
	void mix_tubes() {
		tubes = pal.size();
		array<float> gain = [0.0, 0.12, 0.28, 0.48, 0.72, 1.0, 1.0, 1.0, 1.0, 1.0];
		array<float> glare = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.2, 0.4, 0.6, 0.8];
		for (int negative = 0; negative < 2; ++negative) {
			for (int shade = 0; shade < 10; ++shade) {
				for (int e = 0; e < 128; ++e) {
					int hue = (e / 16) * 3;
					float off = ((e % 16) + 0.5) / 16.0 - 0.5;
					float glow = 1.0 - off * off / 0.1156;
					glow = glow < 0.0 ? 0.0 : glow;
					float core = glow > 0.8 ? (glow - 0.8) * 2.2 : 0.0;
					float light = 0.05 + 0.95 * glow;
					float white = core + (1.0 - core) * glare[shade] * (0.12 + 0.88 * glow);
					float r = 4.0 + 251.0 * neon[hue] * light;
					float g = 5.0 + 250.0 * neon[hue + 1] * light;
					float b = 16.0 + 239.0 * neon[hue + 2] * light;
					r = (r + (255.0 - r) * white) * gain[shade];
					g = (g + (255.0 - g) * white) * gain[shade];
					b = (b + (255.0 - b) * white) * gain[shade];
					if (negative == 1) {
						pal.add(255.0 - r, 255.0 - g, 255.0 - b);
					} else {
						pal.add(r, g, b);
					}
				}
			}
		}
	}

	// the ramp the ink settles back to: seven tubes to the tank's height, deepening along the unit vector (across, down)
	void lay_home(float across, float down) {
		float tube = 112.0 / dh;
		for (int x = 0; x < dw; ++x) { MS_HOME_X[x] = (x - MS_CX) * across * tube; }
		for (int y = 0; y < dh; ++y) { MS_HOME_Y[y] = 1080.0 + (y - MS_CY) * down * tube; }
	}

	// the opening: the ink in its bands, across jets that run against their neighbours,
	// their seams already rippling so the shear rolls them up
	void lay_bands() {
		lay_home(0.0, 1.0);
		var& ink = MS_INK;
		for (int y = 0; y < dh; ++y) {
			int i = y * dw;
			for (int x = 0; x < dw; ++x) {
				ink[i] = MS_HOME_Y[y];
				++i;
			}
		}
		array<float> phase = [];
		for (int s = 0; s <= jets; ++s) {
			phase.push(rng.nextf() * 6.2831853);
			phase.push(rng.nextf() * 6.2831853);
		}
		for (int gy = 1; gy < ch - 1; ++gy) {
			float across = (gy + 0.5) * jets / ch;
			int seam_at = math::itrunc(across + 0.5);
			float ripple = math::cos(3.14159265 * across);
			ripple = seam_at == 0 || seam_at == jets ? 0.0 : ripple * ripple * ripple * ripple;
			for (int gx = 1; gx < cw - 1; ++gx) {
				int j = gy * cw + gx;
				u[j] = band_jet(gy);
				float along = gx * 6.2831853 / cw;
				v[j] = jet_speed * unit * ripple * (0.3 * math::sin(along * 5.0 + phase[seam_at * 2]) + 0.2 * math::sin(along * 3.0 + phase[seam_at * 2 + 1]));
			}
		}
	}

	// a flow row's place in the opening's jets: alternate ways, shearing at the seams
	float band_jet(int gy) {
		float wave = math::sin(3.14159265 * (gy + 0.5) * jets / ch) * 3.0;
		float steep = math::exp(wave);
		return jet_speed * unit * (steep - 1.0 / steep) / (steep + 1.0 / steep);
	}

	float smooth01(float x) {
		float c = x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x);
		return c * c * (3.0 - 2.0 * c);
	}

	float kick_at(float t) { return cue == null ? 0.0 : cue.kick_env(t); }
	float snare_at(float t) { return cue == null ? 0.0 : cue.snare_env(t); }
	float hit_at(float t) { return cue == null ? 0.0 : cue.hit(t); }

	void update(float t, float dt) override {
		float before = tt;
		tt = t;
		if (dt <= 0.0) { return; }
		vorticity = 9.0 + 9.0 * (cue == null ? 0.6 : cue.energy(t));
		float step = dt > 0.05 ? 0.05 : dt;
		strike(before, t);
		stir(t, step);
		confine(step);
		project();
		advect_flow(step);
		carry_ink(t, step);
	}

	// the hits, once each as the clock crosses them: a ring of light for the pair, and a
	// shock for the whirlpool, the reversal (the whole flow turning back on itself, every
	// tube trading its colour for the next) and the drain
	void strike(float before, float t) {
		if (before < t_pair && t >= t_pair) { ring(t_pair, 0.8, 0.0); }
		if (before < t_pool && t >= t_pool) { ring(t_pool, 1.0, 1.0); }
		if (before < t_flip && t >= t_flip) {
			ring(t_flip, 1.0, 1.0);
			turn = 0.0 - turn;
			traded += 16;
			int cn = cw * ch;
			for (int i = 0; i < cn; ++i) {
				u[i] = 0.0 - u[i];
				v[i] = 0.0 - v[i];
				pr[i] = 0.0 - pr[i];
			}
		}
		if (before < t_drain && t >= t_drain) { ring(t_drain, 1.0, 1.0); }
		float kick = kick_at(t);
		if (t > t_pool && kick > kick_before + 0.3) { ring(t, 0.3 * kick, 0.0); }
		kick_before = kick;
	}

	void ring(float born, float power, float lays) {
		ring_born[ring_next] = born;
		ring_power[ring_next] = power;
		ring_lays[ring_next] = lays;
		ring_next = (ring_next + 1) % ring_born.size();
	}

	// the choreography, in ink cells: the opening's jets, then a counter-rotating pair
	// with a rod stirring between them, then one whirlpool (home turning with it) that
	// the drain tightens, six nozzles orbiting from the pair on
	void stir(float t, float step) {
		float cx = MS_CX;
		float cy = MS_CY;
		float kick = kick_at(t);
		float snare = snare_at(t);
		if (t < t_pair) {
			drive_jets((0.5 + 1.6 * kick) * step);
			return;
		}
		float drain = smooth01((t - t_drain) / (MUSIC_BAR * 1.5));
		if (t >= t_pool) {
			float pool = smooth01((t - t_pool) / MUSIC_BEAT);
			home_turned += turn * home_rate * (1.0 + drain) * pool * step;
			lay_home(0.0 - math::sin(home_turned), math::cos(home_turned));
			whirl(cx, cy, turn * unit * (pool_speed + drain_speed * drain) * pool, ch * (0.26 - 0.1 * drain), (4.0 + 2.0 * drain) * step);
		} else {
			float pair = smooth01((t - t_pair) / MUSIC_BEAT);
			whirl(cx - dw * 0.23, cy, 0.0 - pair_speed * unit * pair, ch * 0.2, 3.5 * step);
			whirl(cx + dw * 0.23, cy, pair_speed * unit * pair, ch * 0.2, 3.5 * step);
			float a = t * 1.3;
			drag(cx + dw * 0.3 * math::sin(a), cy + dh * 0.27 * math::sin(a * 2.0),
				dw * 0.195 * math::cos(a), dh * 0.351 * math::cos(a * 2.0), 4.0 * unit);
		}
		float rush = cue == null ? 0.0 : cue.riser(t);
		orbit += turn * (0.55 + 1.2 * rush * rush + 1.4 * drain) * step;
		float reach = 1.0 - 0.5 * drain;
		for (int k = 0; k < 6; ++k) {
			float a2 = orbit + k * 1.0471976;
			float ca = math::cos(a2);
			float sa = math::sin(a2);
			bool pours = k % 2 == 0;
			float beat = pours ? kick : snare;
			float aim = pours ? 0.55 : 1.4;
			float speed = (5.0 + 22.0 * beat) * unit / math::sqrt(1.0 + aim * aim);
			float x = cx + ca * dw * 0.37 * reach;
			float y = cy + sa * dh * 0.37 * reach;
			drag(x, y, (0.0 - sa * turn - ca * aim) * speed, (ca * turn - sa * aim) * speed, 3.5 * unit);
			pour(x, y, (pours ? 1.0 : -1.0) * (12.0 + 80.0 * beat) * step, 4.5 * unit);
		}
	}

	// the opening's jets pull the flow back toward their shear
	void drive_jets(float grip) {
		for (int gy = 1; gy < ch - 1; ++gy) {
			float jet = band_jet(gy);
			int i = gy * cw + 1;
			for (int gx = 1; gx < cw - 1; ++gx) {
				u[i] += (jet - u[i]) * grip;
				++i;
			}
		}
	}

	// a disc of `rad` ink cells about ink cell (x, y) pulls the flow toward (vx, vy), hardest at its centre
	void drag(float x, float y, float vx, float vy, float rad) {
		float qx = x * 0.5 - 0.25;
		float qy = y * 0.5 - 0.25;
		float qr = rad * 0.5 + 0.75;
		float qr2 = qr * qr;
		int x0 = math::max(1, math::itrunc(qx - qr));
		int x1 = math::min(cw - 2, math::itrunc(qx + qr) + 1);
		int y0 = math::max(1, math::itrunc(qy - qr));
		int y1 = math::min(ch - 2, math::itrunc(qy + qr) + 1);
		for (int gy = y0; gy <= y1; ++gy) {
			float ey = gy - qy;
			for (int gx = x0; gx <= x1; ++gx) {
				float ex = gx - qx;
				float q = ex * ex + ey * ey;
				if (q >= qr2) { continue; }
				float f = (1.0 - q / qr2) * 0.6;
				int i = gy * cw + gx;
				u[i] += (vx - u[i]) * f;
				v[i] += (vy - v[i]) * f;
			}
		}
	}

	// ink poured into (or, negative, drawn out of) a disc about ink cell (x, y), most at its centre
	void pour(float x, float y, float amount, float rad) {
		var& ink = MS_INK;
		float r2 = rad * rad;
		int x0 = math::max(1, math::itrunc(x - rad));
		int x1 = math::min(dw - 2, math::itrunc(x + rad) + 1);
		int y0 = math::max(1, math::itrunc(y - rad));
		int y1 = math::min(dh - 2, math::itrunc(y + rad) + 1);
		for (int py = y0; py <= y1; ++py) {
			float ey = py - y;
			for (int px = x0; px <= x1; ++px) {
				float ex = px - x;
				float q = ex * ex + ey * ey;
				if (q >= r2) { continue; }
				float fall = 1.0 - q / r2;
				ink[py * dw + px] += amount * fall * fall;
			}
		}
	}

	// a vortex at ink cell (x, y): the flow is pulled toward a swirl about it that turns
	// `speed` flow cells a second at `core` cells out and falls away beyond
	void whirl(float x, float y, float speed, float core, float grip) {
		float qx = x * 0.5 - 0.25;
		float qy = y * 0.5 - 0.25;
		float c2 = core * core;
		float k = 2.0 * speed / core;
		for (int gy = 1; gy < ch - 1; ++gy) {
			float ey = gy - qy;
			int i = gy * cw + 1;
			for (int gx = 1; gx < cw - 1; ++gx) {
				float ex = gx - qx;
				float fall = 1.0 / (1.0 + (ex * ex + ey * ey) / c2);
				float pull = grip * fall;
				u[i] += (0.0 - ey * k * fall - u[i]) * pull;
				v[i] += (ex * k * fall - v[i]) * pull;
				++i;
			}
		}
	}

	// vorticity confinement: push along N x curl, N the unit gradient of |curl|
	void confine(float step) {
		var& uu = u;
		var& vv = v;
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
		var& uu = u;
		var& vv = v;
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
		var& uu = u;
		var& vv = v;
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
				int ix = cast<int>(px);
				int iy = cast<int>(py);
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
		u = u1;
		v = v1;
	}

	// what the tank's middle sends out, by radius: every ring's lit crest, dark wake and
	// shove outward (a shock's wake also lays the ink back in its bands), the drain's
	// pull inward, its white eye and the dark closing in on it
	void radiate(float t, float step) {
		var& settle = MS_SETTLE;
		var& push = radial;
		var& lit = level;
		int n = lit.size();
		float drain = smooth01((t - t_drain) / MUSIC_BAR);
		float closing = smooth01((t - t_drain - MUSIC_BAR * 0.5) / (MUSIC_BAR * 1.5));
		float dark_at = n * (1.0 - closing) + 11.0 * unit * closing;
		float dusk = 0.25 / unit;
		float eye = 3.5 * unit;
		float mouth = 9.0 * unit;
		float pull = 26.0 * unit * drain;
		float calm = (settling + (pool_settling - settling) * smooth01((t - t_pool) / MUSIC_BEAT)) * step;
		float full = 5.0 + 2.5 * hit_at(t);
		for (int r = 0; r < n; ++r) {
			float m = r / mouth;
			float q = r / eye;
			push[r] = 0.0 - pull * m / (1.0 + m * m);
			lit[r] = full + 4.0 * drain / (1.0 + q * q * q * q);
			settle[r] = calm;
		}
		float wide = 3.5 * unit;
		for (int k = 0; k < ring_born.size(); ++k) {
			float age = t - ring_born[k];
			if (age < 0.0 || age > 2.2) { continue; }
			float amp = ring_power[k] * math::exp(0.0 - age * 1.5);
			float at = (3.0 + age * 48.0) * unit;
			int r0 = math::max(0, math::itrunc(at - wide * 2.6));
			int r1 = math::min(n - 1, math::itrunc(at + wide) + 1);
			for (int r2 = r0; r2 <= r1; ++r2) {
				float e = (r2 - at) / wide;
				if (e > -1.0) {
					float crest = 1.0 - e * e;
					if (crest > 0.0) {
						lit[r2] += 3.4 * amp * crest;
						push[r2] += 24.0 * unit * amp * crest;
					}
				} else {
					float wake = 1.0 - (e + 1.8) * (e + 1.8) / 0.64;
					if (wake > 0.0) {
						lit[r2] -= 3.0 * amp * wake;
						settle[r2] += ring_lays[k] * wake;
					}
				}
			}
		}
		for (int r3 = 0; r3 < n; ++r3) {
			if (settle[r3] > 1.0) { settle[r3] = 1.0; }
			float left = (dark_at - r3) * dusk;
			if (left < 1.0) { lit[r3] *= left < 0.0 ? 0.0 : left; }
		}
		var& shade = MS_SHADE;
		int shades = shade.size();
		for (int q2 = 0; q2 < shades; ++q2) {
			float far = shade_radius[q2];
			int whole_far = cast<int>(far);
			float l = lit[whole_far] + (lit[whole_far + 1] - lit[whole_far]) * (far - whole_far);
			int whole = cast<int>(l + 0.5);
			shade[q2] = (whole < 0 ? 0 : (whole > 9 ? 9 : whole)) * 128;
		}
	}

	// the ink rides the flow plus what the rings and the drain add (a push the projection
	// would cancel, so the flow itself never takes it); its rows run in parallel and
	// rebuild the ink once every row has read the old one
	void carry_ink(float t, float step) {
		radiate(t, step);
		MS_FU = u;
		MS_FV = v;
		var& fu = MS_FU;
		var& fv = MS_FV;
		int cn = cw * ch;
		for (int i = 0; i < cn; ++i) {
			float out = radial[away_r[i]];
			fu[i] += away_x[i] * out;
			fv[i] += away_y[i] * out;
		}
		MS_RUN = step * 2.0;
		var rows = parallel_transform(MS_INK_ROWS, maelstrom_ink_row);
		var& ink = MS_INK;
		ink.clear();
		for (int r = 0; r < rows.size(); ++r) { ink.append(rows[r]); }
	}

	// the camera hangs over the tank: it sways and rolls, punches in on the kick and
	// the hits, and over the drain dives into the eye, turning with the spiral
	void place_camera() {
		float t = tt;
		float flash = hit_at(t);
		float drain = smooth01((t - t_drain) / (MUSIC_BAR * 2.0));
		float roll = 0.08 * math::sin(t * 0.47) + math::sin(t * 53.0) * flash * flash * 0.02;
		roll += turn * drain * drain * 2.4;
		float zoom = (1.2 + 0.06 * math::sin(t * 0.31) + 0.9 * drain) * (1.0 + 0.035 * kick_at(t) + 0.1 * flash);
		float k = dw / (w * zoom);
		float ca = math::cos(roll) * k;
		float sa = math::sin(roll) * k;
		float sway = (1.0 - drain) * dw * 0.025;
		MS_AX = ca;
		MS_AY = sa;
		MS_BX = 0.0 - sa;
		MS_BY = ca;
		MS_OX = MS_CX + sway * math::sin(t * 0.37) + (0.5 - w * 0.5) * ca - (0.5 - h * 0.5) * sa;
		MS_OY = MS_CY + sway * 0.6 * math::sin(t * 0.53) + (0.5 - w * 0.5) * sa + (0.5 - h * 0.5) * ca;
	}

	void render(var& grid) override {
		place_camera();
		// the shocks flash the picture negative
		float since_pool = tt - t_pool;
		float since_drain = tt - t_drain;
		bool negative = (since_pool >= 0.0 && since_pool < 0.1) || (since_drain >= 0.0 && since_drain < 0.1);
		MS_W = w;
		MS_TUBES = negative ? tubes + 1280 : tubes;
		MS_PHASE = traded + math::itrunc(3.5 * kick_at(tt));
		var rows = parallel_transform(MS_ROWS, maelstrom_row);
		grid.clear();
		for (int y = 0; y < h; ++y) { grid.append(rows[y]); }
	}
}
