// BANNERFALL — Verlet cloth, all script: a 28x11 point grid (structural +
// shear constraints) pinned along a pole, a rainbow weave bearing the JAI
// sigil, blowing in an animated wind field. The relaxation solver uses the
// classic Jakobsen sqrt-free approximation (rest2/(d2+rest2) - 0.5), so the
// hot loop is pure arithmetic with zero host calls — and because an array
// element op costs ~10x a local float op here, the constraint topology lives
// in INDEX ARITHMETIC (four family loops: horizontal, vertical, two shear
// diagonals), not in endpoint arrays; only the intact/torn bit is stored.
// Honest tuning: two relaxation passes over ~1.1k constraints (four passes of
// a denser grid ate the frame budget; a flag forgives the extra stretch).
// Lighting comes from real surface normals (edge cross products per quad)
// against a fixed light, with an alpha-max magnitude estimate instead of a
// normalize — no sqrt anywhere in the scene. The wind is a set of traveling
// waves sampled per row/column (a few dozen host sins a frame, never per
// point). Scene arc, deterministic and state-driven: calm ripple -> rising
// gale -> the hoist seam TEARS (constraints break for real when overstretched;
// the seam is woven weakest at the top, so it peels downward like a flag
// really fails — a hidden assist guarantees the cascade completes) -> the
// moment the last thread parts, the gust dies and the free banner tumbles ->
// it shreds into ember particles on a pre-shuffled tear order. Physics,
// lighting, and a small tragedy in 4,000 cells.

class BannerFall : Scene {
	int cw = 26;         // points along the banner
	int chh = 11;        // points down the pole
	var pxs = [];
	var pys = [];
	var pzs = [];
	var opx = [];
	var opy = [];
	var opz = [];
	var jit = [];        // per-point wind grip, from the seeded rng
	var con = [];        // 1 = intact; indexed h | v | shearA | shearB
	var shred = [];      // pre-shuffled constraint order for the final shredding
	var colf = [];       // per-column wind wave (rebuilt each update)
	var colz = [];
	var rowf = [];       // per-row wind modulation
	var sxs = [];        // per-point screen cells (rebuilt each render)
	var sys = [];
	var qmask = [];      // sigil mask per quad
	var hues = [];       // weave hue index per quad column
	int nh = 0;
	int nv = 0;
	int nsh = 0;
	int nc = 0;
	int npts = 0;
	int seamn = 0;       // intact hoist-seam constraints (banner detaches at 0)
	float free_t = -1.0;
	float next_cut = 9.6;
	float shred_t = -1.0;
	int shred_at = 0;
	int polex = 14;
	int poley = 7;
	int polebase = 0;
	int wbase = 0;
	int gbase = 0;
	int flashp = 0;
	int tearpt = -1;
	float tear_t = 0.0;
	float tt = 0.0;

	void build() override {
		name = "BANNERFALL";
		handle = "verlet gale . every thread gives eventually";
		npts = cw * chh;
		for (int j = 0; j < chh; ++j) {
			for (int i = 0; i < cw; ++i) {
				pxs.push(i * 1.0);
				pys.push(j * 1.0);
				pzs.push(0.0);
				opx.push(i * 1.0);
				opy.push(j * 1.0);
				opz.push(0.0);
				jit.push(0.85 + rng.nextf() * 0.3);
			}
		}
		// constraint ids: horizontal, then vertical, then the two shear diagonals
		nh = chh * (cw - 1);
		nv = (chh - 1) * cw;
		nsh = (chh - 1) * (cw - 1);
		nc = nh + nv + nsh + nsh;
		for (int c = 0; c < nc; ++c) {
			con.push(1);
			shred.push(c);
		}
		for (int c2 = nc - 1; c2 > 0; --c2) {   // Fisher-Yates via the host rng
			int k = rng.next(c2 + 1);
			int tmp = shred[c2];
			shred[c2] = shred[k];
			shred[k] = tmp;
		}
		seamn = chh;
		for (int wc = 0; wc < cw; ++wc) { colf.push(0.0); colz.push(0.0); }
		for (int wr = 0; wr < chh; ++wr) { rowf.push(0.0); }
		sxs = gfx.make_grid(npts);
		sys = gfx.make_grid(npts);
		// palette: bg gradient rows 0..h-1 (dusk), pole, 12 weave hues x 6 light
		// levels, gold sigil ramp, tear flash
		for (int py = 0; py < h; ++py) {
			float f = py / (h - 1.0);
			float f2 = f * f;
			pal.add_bg(14.0 + 96.0 * f2 + 24.0 * f, 12.0 + 52.0 * f2 + 10.0 * f, 34.0 + 66.0 * f);
		}
		polebase = h;
		pal.add_bg(88, 72, 54);
		pal.add_bg(122, 102, 76);
		pal.add_bg(255, 214, 110);
		wbase = polebase + 3;
		for (int hu = 0; hu < 12; ++hu) {
			var c3 = gfx.rainbow_rgb(hu * 0.5235987756);
			float hr = c3[0];
			float hg = c3[1];
			float hb = c3[2];
			for (int lv = 0; lv < 6; ++lv) {
				float k2 = 0.16 + 0.84 * lv / 5.0;
				pal.add_bg(hr * k2, hg * k2, hb * k2);
			}
		}
		gbase = wbase + 72;
		for (int lv2 = 0; lv2 < 6; ++lv2) {
			float k3 = 0.22 + 0.78 * lv2 / 5.0;
			pal.add_bg(255.0 * k3, 218.0 * k3, 96.0 * k3);
		}
		flashp = gbase + 6;
		pal.add_bg(255, 255, 255);
		for (int qc = 0; qc < cw - 1; ++qc) { hues.push(qc * 12 / (cw - 1)); }
		// the JAI sigil, woven per quad (5x7 letters, single-column gaps)
		qmask = gfx.make_grid((cw - 1) * (chh - 1));
		var glyphs = [
			["#####", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.."],
			[".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
			["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"]];
		int ox0 = (cw - 1 - 17) / 2;
		int oy0 = (chh - 1 - 7) / 2;
		for (int L = 0; L < 3; ++L) {
			var lrows = glyphs[L];
			for (int r = 0; r < 7; ++r) {
				string rs = lrows[r];
				for (int cc = 0; cc < 5; ++cc) {
					if (rs.substr(cc, 1) == "#") {
						qmask[(oy0 + r) * (cw - 1) + ox0 + L * 6 + cc] = 1;
					}
				}
			}
		}
	}

	void update(float t, float dt) override {
		tt = t;
		tear_t -= dt;
		if (dt < 0.0001) { return; }
		float sdt = dt > 0.05 ? 0.05 : dt;
		float dt2 = sdt * sdt;
		// wind program: steady breeze, then the gale ramps in with layered gusts
		float A = 9.0 + 2.4 * sin(t * 0.7);
		if (t > 5.0) {
			float g = (t - 5.0) * 0.4;
			g = g > 1.0 ? 1.0 : g;
			A = A + g * (14.0 + 6.0 * sin(t * 1.9) + 3.0 * sin(t * 3.7));
		}
		if (free_t >= 0.0) {   // the gust dies the moment the banner parts: the tumble stays framed
			float dk = (t - free_t) * 0.55;
			dk = dk > 0.88 ? 0.88 : dk;
			A = A * (1.0 - dk);
		}
		// bind every hot array via var& (plain assignment would deep-copy it —
		// value semantics — and the physics state must persist) and copy hot
		// scalars to locals: field access costs ~2x a local slot, and the
		// solver below touches arrays ~20k times a frame
		int cww = cw;
		int chn = chh;
		var& px = pxs;
		var& py = pys;
		var& pz = pzs;
		var& qx = opx;
		var& qy = opy;
		var& qz = opz;
		var& cn = con;
		var& jt = jit;
		var& cf_ = colf;
		var& cz_ = colz;
		var& rf_ = rowf;
		// traveling waves, sampled per column/row — never per point
		for (int i = 0; i < cww; ++i) {
			cf_[i] = 1.0 + 0.35 * sin(t * 4.2 - i * 0.42);
			cz_[i] = sin(t * 5.1 - i * 0.55) + 0.5 * sin(t * 2.3 - i * 0.21 + 1.7);
		}
		for (int j = 0; j < chn; ++j) { rf_[j] = 0.82 + 0.36 * sin(t * 1.6 + j * 0.5); }
		// gravity while flown; once the banner parts, the gust's updraft carries
		// the scraps so the burst stays framed
		float grav = free_t >= 0.0 ? 2.6 : 9.5;
		// verlet integration
		int p = 0;
		for (int j2 = 0; j2 < chn; ++j2) {
			float rf = rf_[j2];
			for (int i2 = 0; i2 < cww; ++i2) {
				float cf = cf_[i2];
				float jv = jt[p];
				float fxv = A * cf * rf * jv;
				float fyv = grav - A * 0.16 * cf;   // gravity minus wind lift
				float fzv = A * 0.42 * cz_[i2] * rf * jv;
				float cx = px[p];
				float cy = py[p];
				float cz = pz[p];
				float lox = qx[p];
				float loy = qy[p];
				float loz = qz[p];
				qx[p] = cx;
				qy[p] = cy;
				qz[p] = cz;
				px[p] = cx + (cx - lox) * 0.986 + fxv * dt2;
				py[p] = cy + (cy - loy) * 0.986 + fyv * dt2;
				pz[p] = cz + (cz - loz) * 0.984 + fzv * dt2;
				p = p + 1;
			}
		}
		// hidden assist: from 9.6s the topmost intact seam thread is cut every
		// 0.3s, so the peel completes even if the gale alone would not have
		if (t > next_cut && seamn > 0) {
			next_cut = t + 0.3;
			for (int js = 0; js < chn; ++js) {
				int sid = js * (cww - 1);
				if (cn[sid] == 1) {
					cn[sid] = 0;
					tearpt = js * cww;
					tear_t = 0.14;
					seamn = seamn - 1;
					break;
				}
			}
		}
		if (seamn <= 0 && free_t < 0.0) { free_t = t; }
		// one breath after the banner parts, it bursts into embers
		if (free_t >= 0.0 && t > free_t + 0.45 && shred_at < nc) {
			if (shred_t < 0.0) { shred_t = t; }
			int lim = shred_at + 24;
			lim = lim > nc ? nc : lim;
			while (shred_at < lim) {
				int cxx = shred[shred_at];
				cn[cxx] = 0;
				shred_at = shred_at + 1;
			}
		}
		// relaxation: Jakobsen sqrt-free correction; overstretch breaks for real.
		// topology is index arithmetic — the only constraint array is con[]
		bool brk = t > 8.0;   // the gale is up: threads may fail for real now
		int nhh = nh;
		int nvv = nv;
		int nss = nsh;
		for (int it = 0; it < 2; ++it) {
			// horizontal: a = j*cw+i <-> a+1, rest2 = 1; i==0 is the weak seam
			int cid = 0;
			for (int jh = 0; jh < chn; ++jh) {
				int a = jh * cww;
				for (int ih = 0; ih < cww - 1; ++ih) {
					if (cn[cid] == 1) {
						float ddx = px[a + 1] - px[a];
						float ddy = py[a + 1] - py[a];
						float ddz = pz[a + 1] - pz[a];
						float d2 = ddx * ddx + ddy * ddy + ddz * ddz;
						// the hoist seam (i==0) is woven weakest at the TOP: it peels downward
						if (brk && d2 > (ih == 0 ? 3.0 + jh * 0.4 : 4.6)) {
							cn[cid] = 0;
							tearpt = a;
							tear_t = 0.14;
							if (ih == 0) { seamn = seamn - 1; }
						} else {
							float k = 1.0 / (d2 + 1.0) - 0.5;
							px[a] -= ddx * k;
							py[a] -= ddy * k;
							pz[a] -= ddz * k;
							px[a + 1] += ddx * k;
							py[a + 1] += ddy * k;
							pz[a + 1] += ddz * k;
						}
					}
					cid = cid + 1;
					a = a + 1;
				}
			}
			// vertical: a = j*cw+i <-> a+cw, rest2 = 1; i==0 weak seam
			for (int jv = 0; jv < chn - 1; ++jv) {
				int a2 = jv * cww;
				for (int iv = 0; iv < cww; ++iv) {
					if (cn[cid] == 1) {
						float dvx = px[a2 + cww] - px[a2];
						float dvy = py[a2 + cww] - py[a2];
						float dvz = pz[a2 + cww] - pz[a2];
						float e2 = dvx * dvx + dvy * dvy + dvz * dvz;
						if (brk && iv > 0 && e2 > 4.6) {   // i==0 is the hoist itself: never breaks
							cn[cid] = 0;
							tearpt = a2;
							tear_t = 0.14;
						} else {
							float k2 = 1.0 / (e2 + 1.0) - 0.5;
							px[a2] -= dvx * k2;
							py[a2] -= dvy * k2;
							pz[a2] -= dvz * k2;
							px[a2 + cww] += dvx * k2;
							py[a2 + cww] += dvy * k2;
							pz[a2 + cww] += dvz * k2;
						}
					}
					cid = cid + 1;
					a2 = a2 + 1;
				}
			}
			// shear, first pass only (honest tuning: the second shear pass cost
			// more than its stiffness was worth at this cell budget)
			if (it == 0) {
			// shear A: a <-> a+cw+1, rest2 = 2
			for (int ja = 0; ja < chn - 1; ++ja) {
				int a3 = ja * cww;
				for (int ia = 0; ia < cww - 1; ++ia) {
					if (cn[cid] == 1) {
						float dax = px[a3 + cww + 1] - px[a3];
						float day = py[a3 + cww + 1] - py[a3];
						float daz = pz[a3 + cww + 1] - pz[a3];
						float f2 = dax * dax + day * day + daz * daz;
						if (brk && f2 > 9.2) {
							cn[cid] = 0;
							tearpt = a3;
							tear_t = 0.14;
						} else {
							float k3 = 2.0 / (f2 + 2.0) - 0.5;
							px[a3] -= dax * k3;
							py[a3] -= day * k3;
							pz[a3] -= daz * k3;
							px[a3 + cww + 1] += dax * k3;
							py[a3 + cww + 1] += day * k3;
							pz[a3 + cww + 1] += daz * k3;
						}
					}
					cid = cid + 1;
					a3 = a3 + 1;
				}
			}
			// shear B: a+1 <-> a+cw, rest2 = 2
			for (int jb = 0; jb < chn - 1; ++jb) {
				int a4 = jb * cww;
				for (int ib = 0; ib < cww - 1; ++ib) {
					if (cn[cid] == 1) {
						float dbx = px[a4 + cww] - px[a4 + 1];
						float dby = py[a4 + cww] - py[a4 + 1];
						float dbz = pz[a4 + cww] - pz[a4 + 1];
						float g2 = dbx * dbx + dby * dby + dbz * dbz;
						if (brk && g2 > 9.2) {
							cn[cid] = 0;
							tearpt = a4 + 1;
							tear_t = 0.14;
						} else {
							float k4 = 2.0 / (g2 + 2.0) - 0.5;
							px[a4 + 1] -= dbx * k4;
							py[a4 + 1] -= dby * k4;
							pz[a4 + 1] -= dbz * k4;
							px[a4 + cww] += dbx * k4;
							py[a4 + cww] += dby * k4;
							pz[a4 + cww] += dbz * k4;
						}
					}
					cid = cid + 1;
					a4 = a4 + 1;
				}
			}
			}
			// the hoist column is pinned to the pole for good
			for (int pj = 0; pj < chn; ++pj) {
				int pi = pj * cww;
				px[pi] = 0.0;
				py[pi] = pj * 1.0;
				pz[pi] = 0.0;
			}
		}
	}

	void render(var& grid) override {
		int ww = w;
		int hh = h;
		int cww = cw;
		var& px = pxs;
		var& py = pys;
		var& pz = pzs;
		var& sx_ = sxs;
		var& sy_ = sys;
		var& qm = qmask;
		var& hu = hues;
		// dusk gradient backdrop (row entries 0..h-1)
		int gi = 0;
		for (int y = 0; y < hh; ++y) {
			for (int x = 0; x < ww; ++x) { grid[gi] = y; gi = gi + 1; }
		}
		// the pole
		int pbot = poley + itrunc(chh * 1.5) + 3;
		pbot = pbot > h - 1 ? h - 1 : pbot;
		grid[(poley - 2) * ww + polex] = polebase + 2;
		for (int pr = poley - 1; pr <= pbot; ++pr) {
			grid[pr * ww + polex] = polebase + (pr & 1);
		}
		// project every point once (skewed ortho: z folds read as depth)
		int np = npts;
		int px0 = polex + 1;
		int py0 = poley;
		for (int p2 = 0; p2 < np; ++p2) {
			float X = px[p2];
			float Y = py[p2];
			float Z = pz[p2];
			sx_[p2] = px0 + itrunc(X * 1.65 + Z * 0.28);
			sy_[p2] = py0 + itrunc(Y * 1.5 - Z * 0.18);
		}
		// quads: bbox splat, lit by the edge cross product (alpha-max magnitude,
		// no normalize); a quad with any TORN bounding constraint is skipped, so
		// tears read as holes the moment they happen (huge-bbox quads too)
		var& cnr = con;
		int qw = cww - 1;
		int vbase = nh;
		for (int j = 0; j < chh - 1; ++j) {
			for (int i = 0; i < qw; ++i) {
				int a = j * cww + i;
				int ht = cnr[j * qw + i];
				int hb = cnr[(j + 1) * qw + i];
				int vl = cnr[vbase + j * cww + i];
				int vr = cnr[vbase + j * cww + i + 1];
				if (ht + hb + vl + vr < 4) { continue; }
				int x00 = sx_[a];
				int x01 = sx_[a + 1];
				int x10 = sx_[a + cww];
				int x11 = sx_[a + cww + 1];
				int xmin = x00 < x01 ? x00 : x01;
				xmin = x10 < xmin ? x10 : xmin;
				xmin = x11 < xmin ? x11 : xmin;
				int xmax = x00 > x01 ? x00 : x01;
				xmax = x10 > xmax ? x10 : xmax;
				xmax = x11 > xmax ? x11 : xmax;
				int y00 = sy_[a];
				int y01 = sy_[a + 1];
				int y10 = sy_[a + cww];
				int y11 = sy_[a + cww + 1];
				int ymin = y00 < y01 ? y00 : y01;
				ymin = y10 < ymin ? y10 : ymin;
				ymin = y11 < ymin ? y11 : ymin;
				int ymax = y00 > y01 ? y00 : y01;
				ymax = y10 > ymax ? y10 : ymax;
				ymax = y11 > ymax ? y11 : ymax;
				if (xmax - xmin > 6 || ymax - ymin > 4) { continue; }
				xmin = xmin < 0 ? 0 : xmin;
				ymin = ymin < 1 ? 1 : ymin;
				xmax = xmax > ww - 1 ? ww - 1 : xmax;
				ymax = ymax > hh - 1 ? hh - 1 : ymax;
				if (xmin > xmax || ymin > ymax) { continue; }
				float e1x = px[a + 1] - px[a];
				float e1y = py[a + 1] - py[a];
				float e1z = pz[a + 1] - pz[a];
				float e2x = px[a + cww] - px[a];
				float e2y = py[a + cww] - py[a];
				float e2z = pz[a + cww] - pz[a];
				float nx = e1y * e2z - e1z * e2y;
				float ny = e1z * e2x - e1x * e2z;
				float nz = e1x * e2y - e1y * e2x;
				float ndl = nx * 0.31 - ny * 0.25 + nz * 0.92;
				float anx = nx < 0.0 ? -nx : nx;
				float any = ny < 0.0 ? -ny : ny;
				float anz = nz < 0.0 ? -nz : nz;
				float mmx = anx > any ? anx : any;
				mmx = anz > mmx ? anz : mmx;
				float b = ndl / ((anx + any + anz) * 0.62 + mmx * 0.36 + 0.0001);
				b = b < 0.0 ? -b : b;   // the weave is two-sided
				float li = 0.18 + 0.82 * b;
				li = li > 0.999 ? 0.999 : li;
				int lvl = itrunc(li * 6.0);
				int m = qm[j * qw + i];
				int pi2 = m == 1 ? gbase + lvl : wbase + hu[i] * 6 + lvl;
				for (int yy = ymin; yy <= ymax; ++yy) {
					int rw2 = yy * ww;
					for (int xx = xmin; xx <= xmax; ++xx) { grid[rw2 + xx] = pi2; }
				}
			}
		}
		// once shredding starts the freed points fly as fading embers
		if (shred_at > 0) {
			int fl2 = itrunc(tt * 9.0);
			int fade = itrunc((tt - shred_t) * 0.55);
			int wb = wbase;
			for (int p3 = 0; p3 < npts; ++p3) {
				int sx2 = sx_[p3];
				int sy3 = sy_[p3];
				if (sx2 < 0 || sx2 >= ww || sy3 < 1 || sy3 >= hh) { continue; }
				int lv3 = 3 + ((p3 * 2971 + fl2 * 40503) & 3) - fade;
				lv3 = lv3 < 0 ? 0 : lv3;
				lv3 = lv3 > 5 ? 5 : lv3;
				int ii = p3 % cww;
				if (ii == 0) { continue; }   // the hoist column stays on the pole
				int hu2 = ii >= qw ? qw - 1 : ii;
				grid[sy3 * ww + sx2] = wb + hu[hu2] * 6 + lv3;
			}
		}
		// a thread snapped: one white flash cell at the tear
		if (tear_t > 0.0 && tearpt >= 0) {
			int tx = sx_[tearpt];
			int ty = sy_[tearpt];
			if (tx >= 0 && tx < ww && ty >= 1 && ty < hh) { grid[ty * ww + tx] = flashp; }
		}
	}
}
