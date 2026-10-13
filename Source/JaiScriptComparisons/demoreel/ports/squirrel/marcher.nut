// SPONGEWORKS (Squirrel port of scenes/marcher.jai): a raymarched Menger sponge,
// KIFS fold SDF, slab-tested rays, one tracked re-fold for the face normal, a
// 4-phase Bayer temporal update. The reference's parallel_transform rows run serially.

::SP_PIX <- [];
::SP_T <- [];
::SP_STAR <- [];
::SP_FBR <- [];
::SP_W <- 0; ::SP_H <- 0; ::SP_PHASE <- 0; ::SP_STEPS <- 20;
::SP_SBASE <- 0; ::SP_STARP <- 0; ::SP_EDGEP <- 0;
::SP_FL <- 1.85;
::SP_FX <- 0.0; ::SP_FY <- 0.0; ::SP_FZ <- -1.0;
::SP_RX <- 1.0; ::SP_RZ <- 0.0;
::SP_UX <- 0.0; ::SP_UY <- 1.0; ::SP_UZ <- 0.0;
::SP_EX <- 0.0; ::SP_EY <- 0.0; ::SP_EZ <- 3.3;

// one row of rays: the row's new pixels followed by its new hit distances;
// pixels outside this frame's Bayer phase pass through unchanged
function sponge_row(py) {
	local rww = ::SP_W;
	local rhh = ::SP_H;
	local phase = ::SP_PHASE;
	local ms = ::SP_STEPS;
	local sb = ::SP_SBASE;
	local stp_ = ::SP_STARP;
	local edp = ::SP_EDGEP;
	local fl_ = ::SP_FL;
	local lfx = ::SP_FX;
	local lfy = ::SP_FY;
	local lfz = ::SP_FZ;
	local lrx = ::SP_RX;
	local lrz = ::SP_RZ;
	local lux = ::SP_UX;
	local luy = ::SP_UY;
	local luz = ::SP_UZ;
	local lex = ::SP_EX;
	local ley = ::SP_EY;
	local lez = ::SP_EZ;
	local spix = ::SP_PIX;
	local spt = ::SP_T;
	local star = ::SP_STAR;
	local fbr = ::SP_FBR;
	local du = 2.0 / rww;
	local uc = rww * 0.5 - 0.5;
	local vc = rhh * 0.5 - 0.5;
	local base_ = py * rww;
	local out = [];
	for (local c = 0; c < rww; ++c) { out.push(spix[base_ + c] * 1.0); }
	for (local c2 = 0; c2 < rww; ++c2) { out.push(spt[base_ + c2]); }
	local v = (vc - py) * du;
	for (local px = 0; px < rww; ++px) {
		if (((py & 1) * 2 + (px & 1)) != phase) { continue; }
		local u = (px - uc) * du;
		local rdx = lfx * fl_ + lrx * u + lux * v;
		local rdy = lfy * fl_ + luy * v;
		local rdz = lfz * fl_ + lrz * u + luz * v;
		local il = 1.0 / ::sqrt(rdx * rdx + rdy * rdy + rdz * rdz);
		rdx *= il;
		rdy *= il;
		rdz *= il;
		local sdx = (rdx < 0.000001 && rdx > -0.000001) ? 0.000001 : rdx;
		local sdy = (rdy < 0.000001 && rdy > -0.000001) ? 0.000001 : rdy;
		local sdz = (rdz < 0.000001 && rdz > -0.000001) ? 0.000001 : rdz;
		local ix = 1.0 / sdx;
		local iy = 1.0 / sdy;
		local izl = 1.0 / sdz;
		local ta = (-1.15 - lex) * ix;
		local tb = (1.15 - lex) * ix;
		local tn1 = ta < tb ? ta : tb;
		local tf1 = ta < tb ? tb : ta;
		local tc = (-1.15 - ley) * iy;
		local td = (1.15 - ley) * iy;
		local tn2 = tc < td ? tc : td;
		local tf2 = tc < td ? td : tc;
		local te = (-1.15 - lez) * izl;
		local tg = (1.15 - lez) * izl;
		local tn3 = te < tg ? te : tg;
		local tf3 = te < tg ? tg : te;
		local tn = tn1 > tn2 ? tn1 : tn2;
		tn = tn3 > tn ? tn3 : tn;
		local tf = tf1 < tf2 ? tf1 : tf2;
		tf = tf3 < tf ? tf3 : tf;
		if (tf < tn || tf < 0.0) {
			out[px] = star[base_ + px] == 1 ? stp_ : py;
			continue;
		}
		local tray = tn > 0.0 ? tn + 0.002 : 0.002;
		local tc0 = spt[base_ + px];
		tc0 = tc0 * 0.85;
		tray = tc0 > tray ? tc0 : tray;
		local fit = 3;
		local fsc = 0.037037037;
		local hit = 0;
		local stp = 0;
		local sxp = 0.0;
		local syp = 0.0;
		local szp = 0.0;
		local d = 0.0;
		while (stp < ms) {
			sxp = lex + rdx * tray;
			syp = ley + rdy * tray;
			szp = lez + rdz * tray;
			local qx = sxp;
			local qy = syp;
			local qz = szp;
			for (local it = 0; it < fit; ++it) {
				qx = qx < 0.0 ? -qx : qx;
				qy = qy < 0.0 ? -qy : qy;
				qz = qz < 0.0 ? -qz : qz;
				if (qx < qy) { local t1 = qx; qx = qy; qy = t1; }
				if (qx < qz) { local t2 = qx; qx = qz; qz = t2; }
				if (qy < qz) { local t3 = qy; qy = qz; qz = t3; }
				qx = qx * 3.0 - 2.0;
				qy = qy * 3.0 - 2.0;
				qz = qz * 3.0 - 2.0;
				if (qz < -1.0) { qz = qz + 2.0; }
			}
			local aq = qx < 0.0 ? -qx : qx;
			local aq2 = qy < 0.0 ? -qy : qy;
			local aq3 = qz < 0.0 ? -qz : qz;
			local mq = aq > aq2 ? aq : aq2;
			mq = aq3 > mq ? aq3 : mq;
			d = (mq - 1.0) * fsc;
			if (d < 0.002 + tray * 0.0035) { hit = 1; break; }
			tray = tray + d;
			if (tray > tf) { break; }
			stp = stp + 1;
		}
		if (hit == 1) {
			local qx2 = sxp;
			local qy2 = syp;
			local qz2 = szp;
			local ax0 = 0;
			local ax1 = 2;
			local ax2 = 4;
			local sg0 = 0;
			local sg1 = 0;
			local sg2 = 0;
			for (local it2 = 0; it2 < fit; ++it2) {
				if (qx2 < 0.0) { qx2 = -qx2; sg0 = 1 - sg0; }
				if (qy2 < 0.0) { qy2 = -qy2; sg1 = 1 - sg1; }
				if (qz2 < 0.0) { qz2 = -qz2; sg2 = 1 - sg2; }
				if (qx2 < qy2) {
					local t4 = qx2; qx2 = qy2; qy2 = t4;
					local a4 = ax0; ax0 = ax1; ax1 = a4;
					local s4 = sg0; sg0 = sg1; sg1 = s4;
				}
				if (qx2 < qz2) {
					local t5 = qx2; qx2 = qz2; qz2 = t5;
					local a5 = ax0; ax0 = ax2; ax2 = a5;
					local s5 = sg0; sg0 = sg2; sg2 = s5;
				}
				if (qy2 < qz2) {
					local t6 = qy2; qy2 = qz2; qz2 = t6;
					local a6 = ax1; ax1 = ax2; ax2 = a6;
					local s6 = sg1; sg1 = sg2; sg2 = s6;
				}
				qx2 = qx2 * 3.0 - 2.0;
				qy2 = qy2 * 3.0 - 2.0;
				qz2 = qz2 * 3.0 - 2.0;
				if (qz2 < -1.0) { qz2 = qz2 + 2.0; }
			}
			local bq = qx2 < 0.0 ? -qx2 : qx2;
			local bq2 = qy2 < 0.0 ? -qy2 : qy2;
			local bq3 = qz2 < 0.0 ? -qz2 : qz2;
			local face = 0;
			if (bq >= bq2 && bq >= bq3) {
				face = ax0 + ((qx2 > 0.0 ? 0 : 1) + sg0) % 2;
			} else if (bq2 >= bq3) {
				face = ax1 + ((qy2 > 0.0 ? 0 : 1) + sg1) % 2;
			} else {
				face = ax2 + ((qz2 > 0.0 ? 0 : 1) + sg2) % 2;
			}
			local aopx = sxp;
			local aopy = syp;
			local aopz = szp;
			local wax = face / 2;
			local wsg = (face & 1) == 0 ? 0.07 : -0.07;
			aopx = wax == 0 ? aopx + wsg : aopx;
			aopy = wax == 1 ? aopy + wsg : aopy;
			aopz = wax == 2 ? aopz + wsg : aopz;
			for (local it3 = 0; it3 < 3; ++it3) {
				aopx = aopx < 0.0 ? -aopx : aopx;
				aopy = aopy < 0.0 ? -aopy : aopy;
				aopz = aopz < 0.0 ? -aopz : aopz;
				if (aopx < aopy) { local t7 = aopx; aopx = aopy; aopy = t7; }
				if (aopx < aopz) { local t8 = aopx; aopx = aopz; aopz = t8; }
				if (aopy < aopz) { local t9 = aopy; aopy = aopz; aopz = t9; }
				aopx = aopx * 3.0 - 2.0;
				aopy = aopy * 3.0 - 2.0;
				aopz = aopz * 3.0 - 2.0;
				if (aopz < -1.0) { aopz = aopz + 2.0; }
			}
			local cq = aopx < 0.0 ? -aopx : aopx;
			local cq2 = aopy < 0.0 ? -aopy : aopy;
			local cq3 = aopz < 0.0 ? -aopz : aopz;
			local cm = cq > cq2 ? cq : cq2;
			cm = cq3 > cm ? cq3 : cm;
			local ao = (cm - 1.0) * 0.037037037 * 15.0;
			ao = ao > 1.0 ? 1.0 : ao;
			ao = ao < 0.0 ? 0.0 : ao;
			local fbv = fbr[face];
			local fog = 1.0 - (tray - 1.0) * 0.09;
			fog = fog > 1.0 ? 1.0 : fog;
			fog = fog < 0.45 ? 0.45 : fog;
			local li = fbv * (0.40 + 0.60 * ao) * fog;
			li = li < 0.0 ? 0.0 : li;
			li = li > 0.999 ? 0.999 : li;
			local lvl = (li * 16.0).tointeger();
			out[px] = sb + face * 16 + lvl;
			out[rww + px] = tray;
		} else if (tray > tf) {
			out[px] = star[base_ + px] == 1 ? stp_ : py;
			out[rww + px] = 0.0;
		} else {
			out[px] = edp;
			out[rww + px] = tray;
		}
	}
	return out;
}

class SpongeWorks extends Scene {
	rw = 0;
	rh = 0;
	starp = 0;
	edgep = 0;
	sbase = 0;
	rframe = 0;
	msteps = 20;
	fl = 1.85;
	ex = 0.0;
	ey = 0.0;
	ez = 3.3;
	fx = 0.0;
	fy = 0.0;
	fz = -1.0;
	rgx = 1.0;
	rgz = 0.0;
	upx = 0.0;
	upy = 1.0;
	upz = 0.0;
	tt = 0.0;

	function build() {
		name = "SPONGEWORKS";
		handle = "menger dive . signed distance, folded by hand";
		rw = w;
		rh = h;
		::SP_PIX = ::gfx.make_grid(rw * rh);
		::SP_T = ::gfx.make_fgrid(rw * rh);
		::SP_STAR = [];
		for (local py = 0; py < rh; ++py) {
			local f = py / (rh - 1.0);
			pal.add(8.0 + 12.0 * f, 6.0 + 9.0 * f, 24.0 + 28.0 * f);
		}
		starp = pal.add(196, 205, 240);
		edgep = pal.add(7, 6, 16);
		sbase = edgep + 1;
		local famb = [[255.0, 194.0, 118.0], [196.0, 98.0, 62.0], [255.0, 233.0, 170.0],
			[104.0, 70.0, 96.0], [112.0, 222.0, 206.0], [128.0, 150.0, 194.0]];
		for (local fi = 0; fi < 6; ++fi) {
			local fc = famb[fi];
			local fr = fc[0];
			local fg = fc[1];
			local fb = fc[2];
			for (local lv = 0; lv < 16; ++lv) {
				local k = 0.06 + 0.94 * lv / 15.0;
				local k2 = k * (0.35 + 0.65 * k);
				pal.add(fr * k2, fg * k2, fb * k2);
			}
		}
		::SP_FBR = [0.95, 0.45, 1.0, 0.30, 0.42, 0.72];
		local sd = seed & 65535;
		local npix = rw * rh;
		for (local i = 0; i < npix; ++i) {
			local hsh = ((i * 40503 + sd) * 2971) & 1048575;
			::SP_STAR.push(((hsh / 128) & 255) < 3 ? 1 : 0);
		}
	}

	function update(t, dt) {
		tt = t;
		local ang = t * 0.26 + 0.7;
		local el = 0.38 + 0.22 * ::sin(t * 0.19 + 1.2);
		local rad = 3.9 - t * 0.05;
		if (t > 11.0) { rad -= (t - 11.0) * 0.22; }
		rad = rad < 1.7 ? 1.7 : rad;
		rad += 0.06 * ::sin(t * 0.7);
		local cel = ::cos(el);
		ex = rad * cel * ::cos(ang);
		ey = rad * ::sin(el);
		ez = rad * cel * ::sin(ang);
		local inr = 0.0 - 1.0 / rad;
		fx = ex * inr;
		fy = ey * inr;
		fz = ez * inr;
		local rl = ::sqrt(fx * fx + fz * fz);
		rl = rl < 0.0001 ? 0.0001 : rl;
		rgx = fz / rl;
		rgz = 0.0 - fx / rl;
		upx = fy * rgz;
		upy = fz * rgx - fx * rgz;
		upz = 0.0 - fy * rgx;
	}

	function render(grid) {
		rframe = rframe + 1;
		::SP_PHASE = rframe & 3;
		::SP_W = rw;
		::SP_H = rh;
		::SP_STEPS = msteps;
		::SP_SBASE = sbase;
		::SP_STARP = starp;
		::SP_EDGEP = edgep;
		::SP_FL = fl;
		::SP_FX = fx; ::SP_FY = fy; ::SP_FZ = fz;
		::SP_RX = rgx; ::SP_RZ = rgz;
		::SP_UX = upx; ::SP_UY = upy; ::SP_UZ = upz;
		::SP_EX = ex; ::SP_EY = ey; ::SP_EZ = ez;
		local rows = [];
		for (local r = 0; r < rh; ++r) { rows.push(::sponge_row(r)); }
		local spix = ::SP_PIX;
		local spt = ::SP_T;
		local rww = rw;
		local idx = 0;
		for (local y = 0; y < rh; ++y) {
			local row = rows[y];
			for (local x = 0; x < rww; ++x) {
				local pv = row[x].tointeger();
				spix[idx] = pv;
				spt[idx] = row[rww + x];
				grid[idx] = pv;
				idx = idx + 1;
			}
		}
	}
}
