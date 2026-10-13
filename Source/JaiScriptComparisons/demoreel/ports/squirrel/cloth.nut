// BANNERFALL (Squirrel port of scenes/cloth.jai): Verlet cloth pinned to a pole,
// Jakobsen sqrt-free relaxation over index-arithmetic constraint families,
// real tearing, edge-function triangle raster, the shred into embers.

class BannerFall extends Scene {
	cw = 26;
	chh = 11;
	pxs = null;
	pys = null;
	pzs = null;
	opx = null;
	opy = null;
	opz = null;
	jit = null;
	con = null;
	shred = null;
	colf = null;
	colz = null;
	rowf = null;
	sxs = null;
	sys = null;
	qmask = null;
	hues = null;
	nh = 0;
	nv = 0;
	nsh = 0;
	nc = 0;
	npts = 0;
	seamn = 0;
	free_t = -1.0;
	next_cut = 9.6;
	shred_t = -1.0;
	shred_at = 0;
	polex = 14;
	poley = 14;
	kx = 1.65;
	ky = 3.0;
	polebase = 0;
	wbase = 0;
	gbase = 0;
	flashp = 0;
	tearpt = -1;
	tear_t = 0.0;
	tt = 0.0;

	constructor() {
		pxs = []; pys = []; pzs = [];
		opx = []; opy = []; opz = [];
		jit = []; con = []; shred = [];
		colf = []; colz = []; rowf = [];
		sxs = []; sys = [];
		qmask = []; hues = [];
	}

	function build() {
		name = "BANNERFALL";
		handle = "verlet gale . every thread gives eventually";
		npts = cw * chh;
		for (local j = 0; j < chh; ++j) {
			for (local i = 0; i < cw; ++i) {
				pxs.push(i * 1.0);
				pys.push(j * 1.0);
				pzs.push(0.0);
				opx.push(i * 1.0);
				opy.push(j * 1.0);
				opz.push(0.0);
				jit.push(0.85 + rng.nextf() * 0.3);
			}
		}
		nh = chh * (cw - 1);
		nv = (chh - 1) * cw;
		nsh = (chh - 1) * (cw - 1);
		nc = nh + nv + nsh + nsh;
		for (local c = 0; c < nc; ++c) {
			con.push(1);
			shred.push(c);
		}
		for (local c2 = nc - 1; c2 > 0; --c2) {
			local k = rng.next(c2 + 1);
			local tmp = shred[c2];
			shred[c2] = shred[k];
			shred[k] = tmp;
		}
		seamn = chh;
		for (local wc = 0; wc < cw; ++wc) { colf.push(0.0); colz.push(0.0); }
		for (local wr = 0; wr < chh; ++wr) { rowf.push(0.0); }
		sxs = ::gfx.make_fgrid(npts);
		sys = ::gfx.make_fgrid(npts);
		kx = w * 0.024;
		ky = h * 0.034;
		polex = (w * 0.12).tointeger();
		poley = (h * 0.17).tointeger();
		for (local py = 0; py < h; ++py) {
			local f = py / (h - 1.0);
			local f2 = f * f;
			pal.add(14.0 + 96.0 * f2 + 24.0 * f, 12.0 + 52.0 * f2 + 10.0 * f, 34.0 + 66.0 * f);
		}
		polebase = h;
		pal.add(70, 56, 42);
		pal.add(122, 102, 76);
		pal.add(168, 144, 108);
		pal.add(255, 214, 110);
		pal.add(255, 250, 210);
		wbase = polebase + 5;
		for (local hu = 0; hu < 12; ++hu) {
			local c3 = ::gfx.rainbow_rgb(hu * 0.5235987756);
			local hr = c3[0];
			local hg = c3[1];
			local hb = c3[2];
			for (local lv = 0; lv < 10; ++lv) {
				local k2 = 0.14 + 0.86 * lv / 9.0;
				pal.add(hr * k2, hg * k2, hb * k2);
			}
		}
		gbase = wbase + 120;
		for (local lv2 = 0; lv2 < 10; ++lv2) {
			local k3 = 0.2 + 0.8 * lv2 / 9.0;
			local wh = lv2 >= 8 ? (lv2 - 7) * 0.25 : 0.0;
			pal.add(255.0 * k3 + (255.0 - 255.0 * k3) * wh, 218.0 * k3 + (255.0 - 218.0 * k3) * wh, 96.0 * k3 + (255.0 - 96.0 * k3) * wh);
		}
		flashp = gbase + 10;
		pal.add(255, 255, 255);
		for (local qc = 0; qc < cw - 1; ++qc) { hues.push(qc * 12 / (cw - 1)); }
		qmask = ::gfx.make_grid((cw - 1) * (chh - 1));
		local glyphs = [
			["#####", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.."],
			[".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"],
			["#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"]];
		local ox0 = (cw - 1 - 17) / 2;
		local oy0 = (chh - 1 - 7) / 2;
		for (local L = 0; L < 3; ++L) {
			local lrows = glyphs[L];
			for (local r = 0; r < 7; ++r) {
				local rs = lrows[r];
				for (local cc = 0; cc < 5; ++cc) {
					if (rs.slice(cc, cc + 1) == "#") {
						qmask[(oy0 + r) * (cw - 1) + ox0 + L * 6 + cc] = 1;
					}
				}
			}
		}
	}

	function update(t, dt) {
		tt = t;
		tear_t -= dt;
		if (dt < 0.0001) { return; }
		local sdt = dt > 0.05 ? 0.05 : dt;
		local dt2 = sdt * sdt;
		local A = 9.0 + 2.4 * ::sin(t * 0.7);
		if (t > 5.0) {
			local g = (t - 5.0) * 0.4;
			g = g > 1.0 ? 1.0 : g;
			A = A + g * (14.0 + 6.0 * ::sin(t * 1.9) + 3.0 * ::sin(t * 3.7));
		}
		if (free_t >= 0.0) {
			local dk = (t - free_t) * 0.55;
			dk = dk > 0.88 ? 0.88 : dk;
			A = A * (1.0 - dk);
		}
		local cww = cw;
		local chn = chh;
		local px = pxs;
		local py = pys;
		local pz = pzs;
		local qx = opx;
		local qy = opy;
		local qz = opz;
		local cn = con;
		local jt = jit;
		local cf_ = colf;
		local cz_ = colz;
		local rf_ = rowf;
		for (local i = 0; i < cww; ++i) {
			cf_[i] = 1.0 + 0.35 * ::sin(t * 4.2 - i * 0.42);
			cz_[i] = ::sin(t * 5.1 - i * 0.55) + 0.5 * ::sin(t * 2.3 - i * 0.21 + 1.7);
		}
		for (local j = 0; j < chn; ++j) { rf_[j] = 0.82 + 0.36 * ::sin(t * 1.6 + j * 0.5); }
		local grav = free_t >= 0.0 ? 2.6 : 9.5;
		local p = 0;
		for (local j2 = 0; j2 < chn; ++j2) {
			local rf = rf_[j2];
			for (local i2 = 0; i2 < cww; ++i2) {
				local cf = cf_[i2];
				local jv = jt[p];
				local fxv = A * cf * rf * jv;
				local fyv = grav - A * 0.16 * cf;
				local fzv = A * 0.42 * cz_[i2] * rf * jv;
				local cx = px[p];
				local cy = py[p];
				local cz = pz[p];
				local lox = qx[p];
				local loy = qy[p];
				local loz = qz[p];
				qx[p] = cx;
				qy[p] = cy;
				qz[p] = cz;
				px[p] = cx + (cx - lox) * 0.986 + fxv * dt2;
				py[p] = cy + (cy - loy) * 0.986 + fyv * dt2;
				pz[p] = cz + (cz - loz) * 0.984 + fzv * dt2;
				p = p + 1;
			}
		}
		// hidden assist: from 9.6s the topmost intact seam thread is cut every 0.3s
		if (t > next_cut && seamn > 0) {
			next_cut = t + 0.3;
			for (local js = 0; js < chn; ++js) {
				local sid = js * (cww - 1);
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
		if (free_t >= 0.0 && t > free_t + 0.45 && shred_at < nc) {
			if (shred_t < 0.0) { shred_t = t; }
			local lim = shred_at + 24;
			lim = lim > nc ? nc : lim;
			while (shred_at < lim) {
				local cxx = shred[shred_at];
				cn[cxx] = 0;
				shred_at = shred_at + 1;
			}
		}
		local brk = t > 8.0;
		for (local it = 0; it < 2; ++it) {
			local cid = 0;
			for (local jh = 0; jh < chn; ++jh) {
				local a = jh * cww;
				for (local ih = 0; ih < cww - 1; ++ih) {
					if (cn[cid] == 1) {
						local ddx = px[a + 1] - px[a];
						local ddy = py[a + 1] - py[a];
						local ddz = pz[a + 1] - pz[a];
						local d2 = ddx * ddx + ddy * ddy + ddz * ddz;
						if (brk && d2 > (ih == 0 ? 3.0 + jh * 0.4 : 4.6)) {
							cn[cid] = 0;
							tearpt = a;
							tear_t = 0.14;
							if (ih == 0) { seamn = seamn - 1; }
						} else {
							local k = 1.0 / (d2 + 1.0) - 0.5;
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
			for (local jv = 0; jv < chn - 1; ++jv) {
				local a2 = jv * cww;
				for (local iv = 0; iv < cww; ++iv) {
					if (cn[cid] == 1) {
						local dvx = px[a2 + cww] - px[a2];
						local dvy = py[a2 + cww] - py[a2];
						local dvz = pz[a2 + cww] - pz[a2];
						local e2 = dvx * dvx + dvy * dvy + dvz * dvz;
						if (brk && iv > 0 && e2 > 4.6) {
							cn[cid] = 0;
							tearpt = a2;
							tear_t = 0.14;
						} else {
							local k2 = 1.0 / (e2 + 1.0) - 0.5;
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
			if (it == 0) {
				for (local ja = 0; ja < chn - 1; ++ja) {
					local a3 = ja * cww;
					for (local ia = 0; ia < cww - 1; ++ia) {
						if (cn[cid] == 1) {
							local dax = px[a3 + cww + 1] - px[a3];
							local day = py[a3 + cww + 1] - py[a3];
							local daz = pz[a3 + cww + 1] - pz[a3];
							local f2 = dax * dax + day * day + daz * daz;
							if (brk && f2 > 9.2) {
								cn[cid] = 0;
								tearpt = a3;
								tear_t = 0.14;
							} else {
								local k3 = 2.0 / (f2 + 2.0) - 0.5;
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
				for (local jb = 0; jb < chn - 1; ++jb) {
					local a4 = jb * cww;
					for (local ib = 0; ib < cww - 1; ++ib) {
						if (cn[cid] == 1) {
							local dbx = px[a4 + cww] - px[a4 + 1];
							local dby = py[a4 + cww] - py[a4 + 1];
							local dbz = pz[a4 + cww] - pz[a4 + 1];
							local g2 = dbx * dbx + dby * dby + dbz * dbz;
							if (brk && g2 > 9.2) {
								cn[cid] = 0;
								tearpt = a4 + 1;
								tear_t = 0.14;
							} else {
								local k4 = 2.0 / (g2 + 2.0) - 0.5;
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
			for (local pj = 0; pj < chn; ++pj) {
				local pi = pj * cww;
				px[pi] = 0.0;
				py[pi] = pj * 1.0;
				pz[pi] = 0.0;
			}
		}
	}

	// fill one triangle by edge functions at pixel centers (either winding)
	function tri(grid, ax, ay, bx, by, cx, cy, pi) {
		local ww = w;
		local mnx = ax < bx ? ax : bx;
		mnx = cx < mnx ? cx : mnx;
		local mxx = ax > bx ? ax : bx;
		mxx = cx > mxx ? cx : mxx;
		local mny = ay < by ? ay : by;
		mny = cy < mny ? cy : mny;
		local mxy = ay > by ? ay : by;
		mxy = cy > mxy ? cy : mxy;
		local x0 = mnx.tointeger();
		local x1 = mxx.tointeger();
		local y0 = mny.tointeger();
		local y1 = mxy.tointeger();
		x0 = x0 < 0 ? 0 : x0;
		y0 = y0 < 1 ? 1 : y0;
		x1 = x1 > ww - 1 ? ww - 1 : x1;
		y1 = y1 > h - 1 ? h - 1 : y1;
		local area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
		if (area < 0.0001 && area > -0.0001) { return; }
		local sg = area > 0.0 ? 1.0 : -1.0;
		for (local y = y0; y <= y1; ++y) {
			local py = y + 0.5;
			local rb = y * ww;
			for (local x = x0; x <= x1; ++x) {
				local px = x + 0.5;
				local e0 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) * sg;
				local e1 = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) * sg;
				local e2 = ((ax - cx) * (py - cy) - (ay - cy) * (px - cx)) * sg;
				if (e0 >= 0.0 && e1 >= 0.0 && e2 >= 0.0) { grid[rb + x] = pi; }
			}
		}
	}

	function render(grid) {
		local ww = w;
		local hh = h;
		local cww = cw;
		local px = pxs;
		local py = pys;
		local pz = pzs;
		local sx_ = sxs;
		local sy_ = sys;
		local qm = qmask;
		local hu = hues;
		local gi = 0;
		for (local y = 0; y < hh; ++y) {
			for (local x = 0; x < ww; ++x) { grid[gi] = y; gi = gi + 1; }
		}
		local pbot = poley + (chh * ky).tointeger() + 10;
		pbot = pbot > h - 1 ? h - 1 : pbot;
		for (local pr = poley - 2; pr <= pbot; ++pr) {
			grid[pr * ww + polex - 1] = polebase;
			grid[pr * ww + polex] = polebase + 2;
			grid[pr * ww + polex + 1] = polebase + 1;
		}
		for (local fy = -2; fy <= 0; ++fy) {
			for (local fx = -1; fx <= 1; ++fx) {
				grid[(poley - 4 + fy) * ww + polex + fx] = (fx == -1 && fy == -2) ? polebase + 4 : polebase + 3;
			}
		}
		local np = npts;
		local px0 = polex + 1.5;
		local py0 = poley + 0.5;
		local kxx = kx;
		local kyy = ky;
		for (local p2 = 0; p2 < np; ++p2) {
			local X = px[p2];
			local Y = py[p2];
			local Z = pz[p2];
			sx_[p2] = px0 + X * kxx + Z * kxx * 0.17;
			sy_[p2] = py0 + Y * kyy - Z * kyy * 0.12;
		}
		local cnr = con;
		local qw = cww - 1;
		local vbase = nh;
		local maxw = kxx * 4.0;
		local maxh = kyy * 3.0;
		for (local j = 0; j < chh - 1; ++j) {
			for (local i = 0; i < qw; ++i) {
				local a = j * cww + i;
				local ht = cnr[j * qw + i];
				local hb = cnr[(j + 1) * qw + i];
				local vl = cnr[vbase + j * cww + i];
				local vr = cnr[vbase + j * cww + i + 1];
				if (ht + hb + vl + vr < 4) { continue; }
				local x00 = sx_[a];
				local x01 = sx_[a + 1];
				local x10 = sx_[a + cww];
				local x11 = sx_[a + cww + 1];
				local y00 = sy_[a];
				local y01 = sy_[a + 1];
				local y10 = sy_[a + cww];
				local y11 = sy_[a + cww + 1];
				local dxq = x11 - x00;
				local dyq = y11 - y00;
				local dxr = x01 - x10;
				local dyr = y01 - y10;
				if (dxq > maxw || dxq < 0.0 - maxw || dyq > maxh || dyq < 0.0 - maxh) { continue; }
				if (dxr > maxw || dxr < 0.0 - maxw || dyr > maxh || dyr < 0.0 - maxh) { continue; }
				local e1x = px[a + 1] - px[a];
				local e1y = py[a + 1] - py[a];
				local e1z = pz[a + 1] - pz[a];
				local e2x = px[a + cww] - px[a];
				local e2y = py[a + cww] - py[a];
				local e2z = pz[a + cww] - pz[a];
				local nx = e1y * e2z - e1z * e2y;
				local ny = e1z * e2x - e1x * e2z;
				local nz = e1x * e2y - e1y * e2x;
				local ndl = nx * 0.31 - ny * 0.25 + nz * 0.92;
				local anx = nx < 0.0 ? -nx : nx;
				local any = ny < 0.0 ? -ny : ny;
				local anz = nz < 0.0 ? -nz : nz;
				local mmx = anx > any ? anx : any;
				mmx = anz > mmx ? anz : mmx;
				local b = ndl / ((anx + any + anz) * 0.62 + mmx * 0.36 + 0.0001);
				b = b < 0.0 ? -b : b;
				local li = 0.12 + 0.88 * b;
				li = li > 0.999 ? 0.999 : li;
				local lvl = (li * 10.0).tointeger();
				local m = qm[j * qw + i];
				local pi2 = m == 1 ? gbase + lvl : wbase + hu[i] * 10 + lvl;
				tri(grid, x00, y00, x01, y01, x11, y11, pi2);
				tri(grid, x00, y00, x11, y11, x10, y10, pi2);
			}
		}
		if (shred_at > 0) {
			local fl2 = (tt * 9.0).tointeger();
			local fade = ((tt - shred_t) * 0.9).tointeger();
			local wb = wbase;
			for (local p3 = 0; p3 < npts; ++p3) {
				local sx2 = sx_[p3].tointeger();
				local sy3 = sy_[p3].tointeger();
				if (sx2 < 0 || sx2 >= ww - 1 || sy3 < 1 || sy3 >= hh - 1) { continue; }
				local lv3 = 6 + ((p3 * 2971 + fl2 * 40503) & 3) - fade;
				lv3 = lv3 < 0 ? 0 : lv3;
				lv3 = lv3 > 9 ? 9 : lv3;
				local ii = p3 % cww;
				if (ii == 0) { continue; }
				local hu2 = ii >= qw ? qw - 1 : ii;
				local ec = wb + hu[hu2] * 10 + lv3;
				grid[sy3 * ww + sx2] = ec;
				if (lv3 > 4) { grid[sy3 * ww + sx2 + 1] = ec; }
			}
		}
		if (tear_t > 0.0 && tearpt >= 0) {
			local tx = sx_[tearpt].tointeger();
			local ty = sy_[tearpt].tointeger();
			for (local q = -1; q <= 1; ++q) {
				if (tx + q >= 0 && tx + q < ww && ty >= 1 && ty < hh) { grid[ty * ww + tx + q] = flashp; }
				if (tx >= 0 && tx < ww && ty + q >= 1 && ty + q < hh) { grid[(ty + q) * ww + tx] = flashp; }
			}
		}
	}
}
