// SPONGEWORKS — a raymarched 3D Menger sponge: per-pixel signed-distance-field
// sphere tracing, in a terminal, in script. The SDF is the KIFS fold (abs,
// component sort, x3 scale about the corner, z re-fold — 3 iterations), which
// needs NO sqrt, NO mod, NO host calls: pure ternaries and multiplies, inlined
// at every use because a script method call costs tens of microseconds (see
// README perf lesson). Rays first run an exact slab test against the bounding
// box so the empty screen is nearly free; hits shade with axis-quantized
// normals (a Menger sponge's faces are ALL axis-aligned, so three forward
// difference taps pick the face axis exactly — six face palettes, no
// normalization), step-count ambient occlusion, and distance fog; rays that
// exhaust their step budget inside the box become dark crevice cells, which
// reads as free contact shadow. The normal costs ONE extra SDF eval, not
// three: the KIFS fold is abs + permutation + affine, so re-running it once
// while tracking which world axis each folded slot came from (and its sign)
// maps the folded box's gradient axis back to the exact world-space face.
// Budget honesty: computed at half resolution into a persistent buffer (each
// cell doubled 2x2, like JULIA's columns) with a 4-phase Bayer temporal
// update — a quarter of the rays per frame, converged in four (the orbit is
// slow, staleness stays under a cell).
// The camera orbits on a slow spiral and dives toward the lattice as the scene
// runs. This is the reel's floating-point stress: the VM showcase.

class SpongeWorks : Scene {
	var pix = [];        // rw x rh persistent half-res buffer of palette indices
	var tcache = [];     // per-pixel last hit distance (temporal reprojection)
	var starmap = [];
	var fbr = [];        // per-face base brightness (fixed world light)
	int rw = 0;
	int rh = 0;
	int starp = 0;
	int edgep = 0;
	int sbase = 0;       // face ramps: 6 faces x 10 levels
	int rframe = 0;
	int msteps = 14;
	float fl = 1.85;     // focal length
	float ex = 0.0;
	float ey = 0.0;
	float ez = 3.3;
	float fx = 0.0;
	float fy = 0.0;
	float fz = -1.0;
	float rgx = 1.0;
	float rgz = 0.0;
	float upx = 0.0;
	float upy = 1.0;
	float upz = 0.0;
	float tt = 0.0;

	void build() override {
		name = "SPONGEWORKS";
		handle = "menger dive . signed distance, folded by hand";
		rw = w / 2;
		rh = h / 2;
		pix = gfx.make_grid(rw * rh);
		tcache = gfx.make_grid(rw * rh);
		// background: deep violet gradient rows (entries 0..rh-1), plus one star entry
		for (int py = 0; py < rh; ++py) {
			float f = py / (rh - 1.0);
			pal.add_bg(8.0 + 12.0 * f, 6.0 + 9.0 * f, 24.0 + 28.0 * f);
		}
		starp = pal.add_fg_bg(196, 205, 240, 10, 8, 34, ".");
		edgep = pal.add_bg(7, 6, 16);
		sbase = edgep + 1;
		// six face ramps: +x amber, -x rust, +y gold (sun side), -y shadowed violet,
		// +z teal, -z steel — 10 brightness levels each, gamma-bent
		var famb = [[255.0, 194.0, 118.0], [196.0, 98.0, 62.0], [255.0, 233.0, 170.0],
			[104.0, 70.0, 96.0], [112.0, 222.0, 206.0], [128.0, 150.0, 194.0]];
		for (int fi = 0; fi < 6; ++fi) {
			var fc = famb[fi];
			float fr = fc[0];
			float fg = fc[1];
			float fb = fc[2];
			for (int lv = 0; lv < 10; ++lv) {
				float k = 0.06 + 0.94 * lv / 9.0;
				float k2 = k * (0.35 + 0.65 * k);
				pal.add_bg(fr * k2, fg * k2, fb * k2);
			}
		}
		fbr = [0.95, 0.45, 1.0, 0.30, 0.42, 0.72];
		int sd = seed & 65535;
		int npix = rw * rh;
		for (int i = 0; i < npix; ++i) {
			int hsh = ((i * 40503 + sd) * 2971) & 1048575;
			starmap.push(((hsh / 128) & 255) < 5 ? 1 : 0);
		}
	}

	void update(float t, float dt) override {
		tt = t;
		// stately orbit, slow approach, then the dive toward the lattice
		float ang = t * 0.26 + 0.7;
		float el = 0.38 + 0.22 * sin(t * 0.19 + 1.2);
		float rad = 3.9 - t * 0.05;
		if (t > 11.0) { rad -= (t - 11.0) * 0.22; }
		rad = rad < 1.7 ? 1.7 : rad;
		rad += 0.06 * sin(t * 0.7);
		float cel = cos(el);
		ex = rad * cel * cos(ang);
		ey = rad * sin(el);
		ez = rad * cel * sin(ang);
		float inr = 0.0 - 1.0 / rad;
		fx = ex * inr;
		fy = ey * inr;
		fz = ez * inr;
		float rl = sqrt(fx * fx + fz * fz);
		rl = rl < 0.0001 ? 0.0001 : rl;
		rgx = fz / rl;
		rgz = 0.0 - fx / rl;
		upx = fy * rgz;
		upy = fz * rgx - fx * rgz;
		upz = 0.0 - fy * rgx;
	}

	// every array the ray loop touches is bound via var& (plain assignment
	// would deep-copy — and the pix/tcache buffers must PERSIST), every hot
	// scalar copied to a local: field access costs ~2x a local slot
	void render(var& grid) override {
		rframe = rframe + 1;
		int phase = rframe & 3;
		int rww = rw;
		int rhh = rh;
		int ms = msteps;
		int sb = sbase;
		int stp_ = starp;
		int edp = edgep;
		float fl_ = fl;
		float lfx = fx;
		float lfy = fy;
		float lfz = fz;
		float lrx = rgx;
		float lrz = rgz;
		float lux = upx;
		float luy = upy;
		float luz = upz;
		var& pxb = pix;
		var& tch = tcache;
		var& smb = starmap;
		var& fbb = fbr;
		float du = 2.0 / rww;
		float uc = rww * 0.5 - 0.5;
		float vc = rhh * 0.5 - 0.5;
		float lex = ex;
		float ley = ey;
		float lez = ez;
		int pp = 0;
		for (int py = 0; py < rhh; ++py) {
			float v = (vc - py) * du * 2.0;   // console cells are ~1:2, blocks are 2x2
			for (int px = 0; px < rww; ++px) {
				if (((py & 1) * 2 + (px & 1)) != phase) { pp = pp + 1; continue; }
				float u = (px - uc) * du;
				float rdx = lfx * fl_ + lrx * u + lux * v;
				float rdy = lfy * fl_ + luy * v;
				float rdz = lfz * fl_ + lrz * u + luz * v;
				float il = 1.0 / sqrt(rdx * rdx + rdy * rdy + rdz * rdz);
				rdx *= il;
				rdy *= il;
				rdz *= il;
				// slab test against the bounding box [-1.15, 1.15]^3
				float sdx = (rdx < 0.000001 && rdx > -0.000001) ? 0.000001 : rdx;
				float sdy = (rdy < 0.000001 && rdy > -0.000001) ? 0.000001 : rdy;
				float sdz = (rdz < 0.000001 && rdz > -0.000001) ? 0.000001 : rdz;
				float ix = 1.0 / sdx;
				float iy = 1.0 / sdy;
				float izl = 1.0 / sdz;
				float ta = (-1.15 - lex) * ix;
				float tb = (1.15 - lex) * ix;
				float tn1 = ta < tb ? ta : tb;
				float tf1 = ta < tb ? tb : ta;
				float tc = (-1.15 - ley) * iy;
				float td = (1.15 - ley) * iy;
				float tn2 = tc < td ? tc : td;
				float tf2 = tc < td ? td : tc;
				float te = (-1.15 - lez) * izl;
				float tg = (1.15 - lez) * izl;
				float tn3 = te < tg ? te : tg;
				float tf3 = te < tg ? tg : te;
				float tn = tn1 > tn2 ? tn1 : tn2;
				tn = tn3 > tn ? tn3 : tn;
				float tf = tf1 < tf2 ? tf1 : tf2;
				tf = tf3 < tf ? tf3 : tf;
				if (tf < tn || tf < 0.0) {
					int sv = smb[pp];
					pxb[pp] = sv == 1 ? stp_ : py;
					pp = pp + 1;
					continue;
				}
				float tray = tn > 0.0 ? tn + 0.002 : 0.002;
				// temporal reprojection: the last hit distance of this pixel
				// (refreshed every 4th frame) is a near-perfect march start under
				// a slow orbit - back up 15% and surface rays converge in a few steps
				float tc0 = tch[pp];
				tc0 = tc0 * 0.85;
				tray = tc0 > tray ? tc0 : tray;
				int fit = 3;
				float fsc = 0.037037037;
				int hit = 0;
				int stp = 0;
				float sxp = 0.0;
				float syp = 0.0;
				float szp = 0.0;
				float d = 0.0;
				while (stp < ms) {
					sxp = lex + rdx * tray;
					syp = ley + rdy * tray;
					szp = lez + rdz * tray;
					// Menger KIFS fold, 3 iterations, inlined
					float qx = sxp;
					float qy = syp;
					float qz = szp;
					for (int it = 0; it < fit; ++it) {
						qx = qx < 0.0 ? -qx : qx;
						qy = qy < 0.0 ? -qy : qy;
						qz = qz < 0.0 ? -qz : qz;
						if (qx < qy) { float t1 = qx; qx = qy; qy = t1; }
						if (qx < qz) { float t2 = qx; qx = qz; qz = t2; }
						if (qy < qz) { float t3 = qy; qy = qz; qz = t3; }
						qx = qx * 3.0 - 2.0;
						qy = qy * 3.0 - 2.0;
						qz = qz * 3.0 - 2.0;
						if (qz < -1.0) { qz = qz + 2.0; }
					}
					float aq = qx < 0.0 ? -qx : qx;
					float aq2 = qy < 0.0 ? -qy : qy;
					float aq3 = qz < 0.0 ? -qz : qz;
					float mq = aq > aq2 ? aq : aq2;
					mq = aq3 > mq ? aq3 : mq;
					d = (mq - 1.0) * fsc;
					if (d < 0.002 + tray * 0.0035) { hit = 1; break; }
					tray = tray + d;
					if (tray > tf) { break; }
					stp = stp + 1;
				}
				if (hit == 1) {
					// exact face normal from ONE tracked re-fold: carry which world
					// axis (and sign) feeds each folded slot through the sorts/abs,
					// then the folded box's max-|component| slot IS the face
					float qx2 = sxp;
					float qy2 = syp;
					float qz2 = szp;
					int ax0 = 0;
					int ax1 = 2;
					int ax2 = 4;
					int sg0 = 0;
					int sg1 = 0;
					int sg2 = 0;
					for (int it2 = 0; it2 < fit; ++it2) {
						if (qx2 < 0.0) { qx2 = -qx2; sg0 = 1 - sg0; }
						if (qy2 < 0.0) { qy2 = -qy2; sg1 = 1 - sg1; }
						if (qz2 < 0.0) { qz2 = -qz2; sg2 = 1 - sg2; }
						if (qx2 < qy2) {
							float t4 = qx2; qx2 = qy2; qy2 = t4;
							int a4 = ax0; ax0 = ax1; ax1 = a4;
							int s4 = sg0; sg0 = sg1; sg1 = s4;
						}
						if (qx2 < qz2) {
							float t5 = qx2; qx2 = qz2; qz2 = t5;
							int a5 = ax0; ax0 = ax2; ax2 = a5;
							int s5 = sg0; sg0 = sg2; sg2 = s5;
						}
						if (qy2 < qz2) {
							float t6 = qy2; qy2 = qz2; qz2 = t6;
							int a6 = ax1; ax1 = ax2; ax2 = a6;
							int s6 = sg1; sg1 = sg2; sg2 = s6;
						}
						qx2 = qx2 * 3.0 - 2.0;
						qy2 = qy2 * 3.0 - 2.0;
						qz2 = qz2 * 3.0 - 2.0;
						if (qz2 < -1.0) { qz2 = qz2 + 2.0; }
					}
					float bq = qx2 < 0.0 ? -qx2 : qx2;
					float bq2 = qy2 < 0.0 ? -qy2 : qy2;
					float bq3 = qz2 < 0.0 ? -qz2 : qz2;
					int face = 0;
					if (bq >= bq2 && bq >= bq3) {
						face = ax0 + ((qx2 > 0.0 ? 0 : 1) + sg0) % 2;
					} else if (bq2 >= bq3) {
						face = ax1 + ((qy2 > 0.0 ? 0 : 1) + sg1) % 2;
					} else {
						face = ax2 + ((qz2 > 0.0 ? 0 : 1) + sg2) % 2;
					}
					// AO from one SDF probe 0.07 along the face normal — step-count
					// AO speckles under reprojection warm starts (step counts no
					// longer correlate with occlusion), a distance probe doesn't
					float aopx = sxp;
					float aopy = syp;
					float aopz = szp;
					int wax = face / 2;
					float wsg = (face & 1) == 0 ? 0.07 : -0.07;
					aopx = wax == 0 ? aopx + wsg : aopx;
					aopy = wax == 1 ? aopy + wsg : aopy;
					aopz = wax == 2 ? aopz + wsg : aopz;
					for (int it3 = 0; it3 < 3; ++it3) {
						aopx = aopx < 0.0 ? -aopx : aopx;
						aopy = aopy < 0.0 ? -aopy : aopy;
						aopz = aopz < 0.0 ? -aopz : aopz;
						if (aopx < aopy) { float t7 = aopx; aopx = aopy; aopy = t7; }
						if (aopx < aopz) { float t8 = aopx; aopx = aopz; aopz = t8; }
						if (aopy < aopz) { float t9 = aopy; aopy = aopz; aopz = t9; }
						aopx = aopx * 3.0 - 2.0;
						aopy = aopy * 3.0 - 2.0;
						aopz = aopz * 3.0 - 2.0;
						if (aopz < -1.0) { aopz = aopz + 2.0; }
					}
					float cq = aopx < 0.0 ? -aopx : aopx;
					float cq2 = aopy < 0.0 ? -aopy : aopy;
					float cq3 = aopz < 0.0 ? -aopz : aopz;
					float cm = cq > cq2 ? cq : cq2;
					cm = cq3 > cm ? cq3 : cm;
					float ao = (cm - 1.0) * 0.037037037 * 15.0;
					ao = ao > 1.0 ? 1.0 : ao;
					ao = ao < 0.0 ? 0.0 : ao;
					float fbv = fbb[face];
					float fog = 1.0 - (tray - 1.0) * 0.09;
					fog = fog > 1.0 ? 1.0 : fog;
					fog = fog < 0.45 ? 0.45 : fog;
					float li = fbv * (0.40 + 0.60 * ao) * fog;
					li = li < 0.0 ? 0.0 : li;
					li = li > 0.999 ? 0.999 : li;
					int lvl = itrunc(li * 10.0);
					pxb[pp] = sb + face * 10 + lvl;
					tch[pp] = tray;
				} else if (tray > tf) {
					// clean exit through a hole: background
					int sv2 = smb[pp];
					pxb[pp] = sv2 == 1 ? stp_ : py;
					tch[pp] = 0.0;
				} else {
					// step budget spent deep in a crevice: free contact shadow
					pxb[pp] = edp;
					tch[pp] = tray;
				}
				pp = pp + 1;
			}
		}
		// blit the half-res buffer as 2x2 blocks
		int ww = w;
		for (int by = 0; by < rhh; ++by) {
			int r0 = by * 2 * ww;
			int r1 = r0 + ww;
			int bp = by * rww;
			for (int bx = 0; bx < rww; ++bx) {
				int pv = pxb[bp + bx];
				int c0 = r0 + bx * 2;
				grid[c0] = pv;
				grid[c0 + 1] = pv;
				int c1 = r1 + bx * 2;
				grid[c1] = pv;
				grid[c1 + 1] = pv;
			}
		}
		// odd console sizes: fill the spare column/row with background
		if ((ww & 1) == 1) {
			for (int oy = 0; oy < h; ++oy) {
				int bgr = oy / 2;
				grid[oy * ww + ww - 1] = bgr > rhh - 1 ? rhh - 1 : bgr;
			}
		}
		if ((h & 1) == 1) {
			int rl2 = (h - 1) * ww;
			for (int ox = 0; ox < ww; ++ox) { grid[rl2 + ox] = rhh - 1; }
		}
	}
}
