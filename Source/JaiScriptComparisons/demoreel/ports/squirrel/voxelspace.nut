// CANYONRUN (Squirrel port of scenes/voxelspace.jai): the Comanche VoxelSpace
// march over a diamond-square heightmap with a carved river canyon.

class CanyonRun extends Scene {
	hmap = null;
	cmap = null;
	ybuf = null;
	rolloff = null;
	zs = null;
	invz = null;
	hazes = null;
	msz = 128;
	mmask = 127;
	tbase = 0;
	nsteps = 72;
	nhaze = 10;
	sunb = 0;
	posx = 64.0;
	posy = 0.0;
	dirx = 0.0;
	diry = 1.0;
	planex = 0.0;
	planey = 0.0;
	pdx = 0.0;
	pdy = 1.0;
	camhf = 210.0;
	roll = 0.0;
	ph1 = 0.0;
	ph2 = 0.0;
	camh = 210;
	horizon = 12;
	tt = 0.0;

	constructor() {
		hmap = [];
		cmap = [];
		ybuf = [];
		rolloff = [];
		zs = [];
		invz = [];
		hazes = [];
	}

	function build() {
		name = "CANYONRUN";
		handle = "river meander . a heightmap, a y-buffer, 1992";
		for (local py = 0; py < h; ++py) {
			local f = py / (h - 1.0);
			local f2 = f * f;
			pal.add(16.0 + 150.0 * f2 + 30.0 * f, 24.0 + 138.0 * f2 + 18.0 * f, 64.0 + 130.0 * f);
		}
		tbase = h;
		local bands = [[16.0, 48.0, 112.0], [30.0, 94.0, 158.0], [152.0, 134.0, 94.0], [98.0, 128.0, 58.0],
			[154.0, 88.0, 54.0], [190.0, 124.0, 78.0], [146.0, 142.0, 136.0], [238.0, 242.0, 248.0]];
		for (local b = 0; b < 8; ++b) {
			local bc = bands[b];
			local br = bc[0];
			local bg = bc[1];
			local bb = bc[2];
			for (local sh = 0; sh < 4; ++sh) {
				local k = 0.52 + sh * 0.20;
				local cr = br * k;
				local cg = bg * k;
				local cb = bb * k;
				for (local hq = 0; hq < nhaze; ++hq) {
					local hf = hq * 0.093;
					pal.add(cr + (176.0 - cr) * hf, cg + (176.0 - cg) * hf, cb + (206.0 - cb) * hf);
				}
			}
		}
		hmap = ::gfx.make_grid(msz * msz);
		local sd = seed & 65535;
		hmap[0] = 110 + (sd & 63);
		local step = msz;
		local amp = 92;
		while (step > 1) {
			local half = step / 2;
			for (local dy = half; dy < msz; dy += step) {
				local rw0 = ((dy - half) & mmask) * msz;
				local rw1 = ((dy + half) & mmask) * msz;
				local rwc = dy * msz;
				for (local dx = half; dx < msz; dx += step) {
					local x0 = dx - half;
					local x1 = (dx + half) & mmask;
					local a1 = hmap[rw0 + x0];
					local a2 = hmap[rw0 + x1];
					local a3 = hmap[rw1 + x0];
					local a4 = hmap[rw1 + x1];
					local hsh = (dx * 73856093 + dy * 19349663 + step * 83492791 + sd * 2971) & 1048575;
					local d = (((hsh * 1274126177) / 4096) & 255) - 128;
					hmap[rwc + dx] = (a1 + a2 + a3 + a4) / 4 + d * amp / 128;
				}
			}
			for (local sy = 0; sy < msz; sy += half) {
				local xoff = (sy % step) == 0 ? half : 0;
				local rws = sy * msz;
				local rwu = ((sy - half) & mmask) * msz;
				local rwd = ((sy + half) & mmask) * msz;
				for (local sx = xoff; sx < msz; sx += step) {
					local xl = (sx - half) & mmask;
					local xr = (sx + half) & mmask;
					local b1 = hmap[rws + xl];
					local b2 = hmap[rws + xr];
					local b3 = hmap[rwu + sx];
					local b4 = hmap[rwd + sx];
					local hsh2 = (sx * 19349663 + sy * 73856093 + step * 297121507 + sd * 7919) & 1048575;
					local d2 = (((hsh2 * 1274126177) / 4096) & 255) - 128;
					hmap[rws + sx] = (b1 + b2 + b3 + b4) / 4 + d2 * amp / 128;
				}
			}
			step = half;
			amp = amp * 11 / 20;
			amp = amp < 3 ? 3 : amp;
		}
		local hmin = 999999;
		local hmax = -999999;
		local nm = msz * msz;
		for (local q = 0; q < nm; ++q) {
			local hv0 = hmap[q];
			hmin = hv0 < hmin ? hv0 : hmin;
			hmax = hv0 > hmax ? hv0 : hmax;
		}
		local range = hmax - hmin;
		range = range < 1 ? 1 : range;
		ph1 = rng.nextf() * 6.283185307;
		ph2 = rng.nextf() * 6.283185307;
		local rivc = [];
		for (local ry = 0; ry < msz; ++ry) {
			rivc.push((64.0 + 26.0 * ::sin(ry * 0.04908738521 + ph1) + 11.0 * ::sin(ry * 0.1472621556 + ph2)).tointeger());
		}
		local falltab = [];
		for (local fd = 0; fd < 16; ++fd) {
			local ff = fd / 15.0;
			falltab.push((ff * ff * (3.0 - 2.0 * ff) * 256.0).tointeger());
		}
		for (local cy = 0; cy < msz; ++cy) {
			local rc = rivc[cy];
			local rowc = cy * msz;
			for (local cx = 0; cx < msz; ++cx) {
				local hraw = hmap[rowc + cx];
				local hv = (hraw - hmin) * 255 / range;
				local dxr = cx - rc;
				dxr = dxr > 64 ? dxr - 128 : dxr;
				dxr = dxr < -64 ? dxr + 128 : dxr;
				local ad = dxr < 0 ? 0 - dxr : dxr;
				if (ad < 16) {
					local f256 = falltab[ad > 15 ? 15 : ad];
					hv = 50 + (hv - 50) * f256 / 256;
				}
				hv = hv < 0 ? 0 : hv;
				hv = hv > 255 ? 255 : hv;
				hmap[rowc + cx] = hv;
			}
		}
		cmap = ::gfx.make_grid(nm);
		for (local my = 0; my < msz; ++my) {
			local rowm = my * msz;
			local rown = ((my + 1) & mmask) * msz;
			local rc2 = rivc[my];
			for (local mx = 0; mx < msz; ++mx) {
				local hv2 = hmap[rowm + mx];
				local hn = hmap[rown + mx];
				local dxr2 = mx - rc2;
				dxr2 = dxr2 > 64 ? dxr2 - 128 : dxr2;
				dxr2 = dxr2 < -64 ? dxr2 + 128 : dxr2;
				local ad2 = dxr2 < 0 ? 0 - dxr2 : dxr2;
				local band = 7;
				if (ad2 < 3) { band = 0; }
				else if (ad2 < 6) { band = 1; }
				else if (hv2 <= 80) { band = 2; }
				else if (hv2 <= 118) { band = 3; }
				else if (hv2 <= 165) { band = 4; }
				else if (hv2 <= 205) { band = 5; }
				else if (hv2 <= 235) { band = 6; }
				local he = hmap[rowm + ((mx + 1) & mmask)];
				local hs = hmap[((my - 1) & mmask) * msz + mx];
				local hw = hmap[rowm + ((mx - 1) & mmask)];
				local grain = (((mx * 73856093 + my * 19349663) & 1048575) * 1274126177 >> 12) & 3;
				local lit = 6 + (hw - he) + (hs - hn) + grain - 1;
				local shade = band < 2 ? 1 + grain / 2 : (lit < 3 ? 0 : (lit < 7 ? 1 : (lit < 11 ? 2 : 3)));
				cmap[rowm + mx] = band * 4 + shade;
			}
		}
		for (local q2 = 0; q2 < nm; ++q2) { cmap[q2] = hmap[q2] * 32 + cmap[q2]; }
		local z = 2.4;
		local hsc = h * 0.085;
		for (local i = 0; i < nsteps; ++i) {
			zs.push(z);
			invz.push((hsc * 65536.0 / z).tointeger());
			local zr = i / (nsteps - 1.0);
			local hzv = (zr * zr * (nhaze + 0.6)).tointeger();
			hazes.push(hzv > nhaze - 1 ? nhaze - 1 : hzv);
			z = z * 1.058;
		}
		sunb = pal.gradient(6, [[255.0, 170.0, 110.0], [255.0, 210.0, 150.0], [255.0, 244.0, 214.0]]);
		ybuf = ::gfx.make_grid(w);
		rolloff = ::gfx.make_grid(w);
	}

	function update(t, dt) {
		tt = t;
		local sp = 8.2;
		local u = t * sp;
		posy = u;
		local wv = 13.0 * ::sin(t * 0.5 + 0.8);
		posx = 64.0 + 26.0 * ::sin(u * 0.04908738521 + ph1) + 11.0 * ::sin(u * 0.1472621556 + ph2) + wv;
		local u2 = u + 7.5;
		local wv2 = 13.0 * ::sin((t + 7.5 / sp) * 0.5 + 0.8);
		local lx = 64.0 + 26.0 * ::sin(u2 * 0.04908738521 + ph1) + 11.0 * ::sin(u2 * 0.1472621556 + ph2) + wv2;
		local ddx = lx - posx;
		local dl = ::sqrt(ddx * ddx + 56.25);
		dirx = ddx / dl;
		diry = 7.5 / dl;
		planex = 0.0 - diry * 0.92;
		planey = dirx * 0.92;
		if (dt > 0.0001) {
			local cr = (pdx * diry - pdy * dirx) / dt;
			local rt = cr * 1.05;
			rt = rt > 0.55 ? 0.55 : rt;
			rt = rt < -0.55 ? -0.55 : rt;
			local mixr = dt * 3.2;
			mixr = mixr > 1.0 ? 1.0 : mixr;
			roll += (rt - roll) * mixr;
		}
		pdx = dirx;
		pdy = diry;
		local g0 = hmap[((::floor(posy).tointeger() & mmask) * msz) + (::floor(posx).tointeger() & mmask)];
		local g1 = hmap[((::floor(posy + diry * 6.0).tointeger() & mmask) * msz) + (::floor(posx + dirx * 6.0).tointeger() & mmask)];
		local g2 = hmap[((::floor(posy + diry * 14.0).tointeger() & mmask) * msz) + (::floor(posx + dirx * 14.0).tointeger() & mmask)];
		local gm = g0 > g1 ? g0 : g1;
		gm = g2 > gm ? g2 : gm;
		local base_ = 118.0 + 74.0 * ::sin(t * 0.31 + 4.4);
		local floorh = gm + 30.0;
		local targ = floorh > base_ ? floorh : base_;
		if (dt > 0.0001) {
			local mixa = dt * 1.9;
			mixa = mixa > 1.0 ? 1.0 : mixa;
			camhf += (targ - camhf) * mixa;
		}
		camh = camhf.tointeger();
		local pit = (camhf - (gm + 70.0)) * 0.0020 + 0.06 * ::sin(t * 0.23);
		pit = pit > 0.20 ? 0.20 : pit;
		pit = pit < -0.14 ? -0.14 : pit;
		horizon = (h * (0.30 + pit)).tointeger();
	}

	function render(grid) {
		local ww = w;
		local hh = h;
		local mm = mmask;
		local ms = msz;
		local tb = tbase;
		local cam = camh;
		local horiz = horizon;
		local ns = nsteps;
		local nh = nhaze;
		local dx_ = dirx;
		local dy_ = diry;
		local px_ = planex;
		local py_ = planey;
		local tm = cmap;
		local yb = ybuf;
		local ro = rolloff;
		local zt = zs;
		local izt = invz;
		local hzt = hazes;
		local w2 = ww / 2;
		local rollfp = (roll * 65536.0).tointeger();
		for (local x = 0; x < ww; ++x) {
			yb[x] = hh;
			ro[x] = horiz + rollfp * (x - w2) / 65536;
		}
		local bigx = posx + 4096.0;
		local bigy = posy + 4096.0;
		for (local s = 0; s < ns; ++s) {
			local z = zt[s];
			local iz = izt[s];
			local hz = hzt[s];
			local plx = bigx + (dx_ - px_) * z;
			local ply = bigy + (dy_ - py_) * z;
			local prx = bigx + (dx_ + px_) * z;
			local pry = bigy + (dy_ + py_) * z;
			local rxf = (plx * 65536.0).tointeger();
			local ryf = (ply * 65536.0).tointeger();
			local dxf = ((prx - plx) * 65536.0 / ww).tointeger();
			local dyf = ((pry - ply) * 65536.0 / ww).tointeger();
			for (local x2 = 0; x2 < ww; ++x2) {
				local ybv = yb[x2];
				if (ybv > 0) {
					local mi = ((ryf >> 16) & mm) * ms + ((rxf >> 16) & mm);
					local tv = tm[mi];
					local sy = (cam - (tv >> 5)) * iz / 65536 + ro[x2];
					if (sy < ybv) {
						sy = sy < 0 ? 0 : sy;
						local pi = tb + (tv & 31) * nh + hz;
						local gi = sy * ww + x2;
						local yy = sy;
						while (yy < ybv) { grid[gi] = pi; gi += ww; yy = yy + 1; }
						yb[x2] = sy;
					}
				}
				rxf += dxf;
				ryf += dyf;
			}
		}
		for (local x3 = 0; x3 < ww; ++x3) {
			local yb2 = yb[x3];
			local gi2 = x3;
			local yy2 = 0;
			while (yy2 < yb2) { grid[gi2] = yy2; gi2 += ww; yy2 = yy2 + 1; }
		}
		local rel = ::atan2(dx_, dy_) - 0.55;
		local sxf = ww * 0.5 - rel * ww * 0.55;
		local syf = horiz - hh * 0.16;
		local sr = hh * 0.07;
		local sx0 = (sxf - sr * 2.2).tointeger();
		local sx1 = (sxf + sr * 2.2).tointeger();
		local sy0 = (syf - sr * 2.2).tointeger();
		local sy1 = (syf + sr * 2.2).tointeger();
		for (local sy = sy0; sy <= sy1; ++sy) {
			if (sy < 0 || sy >= hh) { continue; }
			for (local sx = sx0; sx <= sx1; ++sx) {
				if (sx < 0 || sx >= ww || sy >= yb[sx]) { continue; }
				local ddx = sx + 0.5 - sxf;
				local ddy = sy + 0.5 - syf;
				local d = ::sqrt(ddx * ddx + ddy * ddy) / sr;
				if (d < 1.0) { grid[sy * ww + sx] = sunb + 5; }
				else if (d < 2.2) {
					local gl = ((2.2 - d) * 4.2).tointeger();
					if (gl > 0) { grid[sy * ww + sx] = sunb + (gl > 4 ? 4 : gl - 1); }
				}
			}
		}
	}
}
