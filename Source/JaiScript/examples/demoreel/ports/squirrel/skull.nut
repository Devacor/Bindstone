// OSSUARY (Squirrel port of scenes/skull.jai): a horned three-eyed skull
// spider crawls a raymarched landscape morphing between gyroid, Schwarz P and
// D. The reference's parallel_transform rows run as a serial loop.

::SK_T <- 0.0;
::SK_S <- 0.8;
::SK_K <- 0.0;
::SK_TW <- 0.0;
::SK_TH <- 0.16;
::SK_MG <- 1.0;
::SK_MP <- 0.0;
::SK_MD <- 0.0;
::SK_WA <- 0.0;
::SK_CX <- 0.0;
::SK_CY <- 0.0;
::SK_CZ <- 0.0;
::SK_EX <- 0.0; ::SK_EY <- 0.0; ::SK_EZ <- 0.0;
::SK_FX <- 0.0; ::SK_FY <- 0.0; ::SK_FZ <- 1.0;
::SK_RX <- 1.0; ::SK_RY <- 0.0; ::SK_RZ <- 0.0;
::SK_UX <- 0.0; ::SK_UY <- 1.0; ::SK_UZ <- 0.0;
::SK_BCX <- 0.0; ::SK_BCY <- 0.0; ::SK_BCZ <- 0.0; ::SK_BR <- 1.0;
::SK_AX <- 0.0; ::SK_AY <- 0.0; ::SK_AZ <- 0.0; ::SK_AR <- 0.24;
::SK_TX <- 0.0; ::SK_TY <- 0.0; ::SK_TZ <- 0.0; ::SK_TR <- 0.14;
::SK_HX <- 0.0; ::SK_HY <- 0.0; ::SK_HZ <- 0.0; ::SK_HR <- 0.24;
::SK_NX <- 0.0; ::SK_NY <- 0.0; ::SK_NZ <- 0.0;
::SK_JX <- 0.0; ::SK_JY <- 0.0; ::SK_JZ <- 0.0;
::SK_PFX <- 0.0; ::SK_PFY <- 0.0; ::SK_PFZ <- 1.0;
::SK_PUX <- 0.0; ::SK_PUY <- 1.0; ::SK_PUZ <- 0.0;
::SK_PRX <- 1.0; ::SK_PRY <- 0.0; ::SK_PRZ <- 0.0;
::SK_LX <- 0.0; ::SK_LY <- 0.0; ::SK_LZ <- 0.0;
::SK_FLICK <- 0.0;
::SK_CAX <- [];
::SK_CAY <- [];
::SK_CAZ <- [];
::SK_CDX <- [];
::SK_CDY <- [];
::SK_CDZ <- [];
::SK_CID <- [];
::SK_CR <- [];
::SK_CMX <- [];
::SK_CMY <- [];
::SK_CMZ <- [];
::SK_CB <- [];
::SK_HGX <- 0.0; ::SK_HGY <- 0.0; ::SK_HGZ <- 0.0;
::SK_BGX <- 0.0; ::SK_BGY <- 0.0; ::SK_BGZ <- 0.0;
::SK_NC <- 0;
::SK_NBONE <- 5;
::SK_CROWN0 <- 0;
::SK_CRX <- 0.0; ::SK_CRY <- 0.0; ::SK_CRZ <- 0.0;
::SK_GMX <- 0.0; ::SK_GMY <- 0.0; ::SK_GMZ <- 0.0;
::SK_GAX <- 1.0; ::SK_GAY <- 0.0; ::SK_GAZ <- 0.0;
::SK_GBX <- 0.0; ::SK_GBY <- 0.0; ::SK_GBZ <- 1.0;
::SK_GX <- [];
::SK_GY <- [];
::SK_GZ <- [];
::SK_GR <- [];
::SK_NG <- 0;
::SK_NSOCK <- 3;
::SK_W <- 0;
::SK_H <- 0;
::SK_WORLD <- 0;
::SK_EMBER <- 0;
::SK_BONE <- 0;
::SK_CHIT <- 0;
::SK_EYE <- 0;
::SK_MARK <- 0;
::SK_FOG <- 0;
::SK_SPARK <- 0;
::SK_GEM <- 0;
::SK_BAYER <- [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];

// the landscape's level function: three surfaces blended under a flowing warp, twisted about the creature's axis
function sk_field(x, y, z) {
	local a = (z - ::SK_CZ) * ::SK_TW;
	local ca = ::cos(a);
	local sa = ::sin(a);
	local cxc = ::SK_CX;
	local cyc = ::SK_CY;
	local px = x - cxc;
	local py = y - cyc;
	local s = ::SK_S;
	local qx = (px * ca - py * sa + cxc) * s;
	local qy = (px * sa + py * ca + cyc) * s;
	local qz = z * s;
	local wa = ::SK_WA;
	local t = ::SK_T;
	local wx = qx + wa * ::sin(qy * 1.3 + t * 0.7);
	local wy = qy + wa * ::sin(qz * 1.3 + t * 0.6);
	local wz = qz + wa * ::sin(qx * 1.3 + t * 0.8);
	local sx = ::sin(wx), cx = ::cos(wx);
	local sy = ::sin(wy), cy = ::cos(wy);
	local sz = ::sin(wz), cz = ::cos(wz);
	local g = sx * cy + sy * cz + sz * cx;
	local p = cx + cy + cz;
	local d = sx * sy * sz + sx * cy * cz + cx * sy * cz + cx * cy * sz;
	return g * ::SK_MG + p * ::SK_MP + d * ::SK_MD * 1.4 - ::SK_K;
}

function sk_world(x, y, z) {
	local g = ::sk_field(x, y, z);
	return ((g < 0.0 ? -g : g) - ::SK_TH) / (::SK_S * (2.1 + ::SK_WA * 2.4));
}

// capsules [first, last) against best: a capsule whose bounding sphere cannot beat best is skipped without a root
function sk_capsules(x, y, z, first, last, best) {
	local cmx = ::SK_CMX, cmy = ::SK_CMY, cmz = ::SK_CMZ, cb = ::SK_CB;
	for (local i = first; i < last; ++i) {
		local mx = x - cmx[i];
		local my = y - cmy[i];
		local mz = z - cmz[i];
		local reach = best + cb[i];
		if (mx * mx + my * my + mz * mz > reach * reach) { continue; }
		local pax = x - ::SK_CAX[i];
		local pay = y - ::SK_CAY[i];
		local paz = z - ::SK_CAZ[i];
		local dx = ::SK_CDX[i];
		local dy = ::SK_CDY[i];
		local dz = ::SK_CDZ[i];
		local hh = (pax * dx + pay * dy + paz * dz) * ::SK_CID[i];
		hh = hh < 0.0 ? 0.0 : (hh > 1.0 ? 1.0 : hh);
		local ex = pax - dx * hh;
		local ey = pay - dy * hh;
		local ez = paz - dz * hh;
		local d = ::sqrt(ex * ex + ey * ey + ez * ez) - ::SK_CR[i];
		best = d < best ? d : best;
	}
	return best;
}

function sk_glows(x, y, z) {
	local best = 99.0;
	local gx = ::SK_GX, gy = ::SK_GY, gz = ::SK_GZ, gr = ::SK_GR;
	local ng = ::SK_NG;
	for (local i = 0; i < ng; ++i) {
		local dx = x - gx[i];
		local dy = y - gy[i];
		local dz = z - gz[i];
		local d = ::sqrt(dx * dx + dy * dy + dz * dz) - gr[i];
		best = d < best ? d : best;
	}
	return best;
}

function sk_skull(x, y, z) {
	local hx = x - ::SK_HX;
	local hy = y - ::SK_HY;
	local hz = z - ::SK_HZ;
	local cu = hx * ::SK_PUX + hy * ::SK_PUY + hz * ::SK_PUZ;
	local cf = hx * ::SK_PFX + hy * ::SK_PFY + hz * ::SK_PFZ;
	local cr = hx * ::SK_PRX + hy * ::SK_PRY + hz * ::SK_PRZ;
	cu = cu * 1.18;
	local d = ::sqrt(cu * cu + cf * cf * 0.92 + cr * cr) - ::SK_HR;
	local jx = x - ::SK_JX;
	local jy = y - ::SK_JY;
	local jz = z - ::SK_JZ;
	local jr = jx * ::SK_PRX + jy * ::SK_PRY + jz * ::SK_PRZ;
	jr = jr < -0.085 ? -0.085 : (jr > 0.085 ? 0.085 : jr);
	local qx = jx - ::SK_PRX * jr;
	local qy = jy - ::SK_PRY * jr;
	local qz = jz - ::SK_PRZ * jr;
	local dj = ::sqrt(qx * qx + qy * qy + qz * qz) - 0.09;
	d = d < dj ? d : dj;
	local gx = ::SK_GX, gy = ::SK_GY, gz = ::SK_GZ, gr = ::SK_GR;
	local ns = ::SK_NSOCK;
	for (local i = 0; i < ns; ++i) {
		local sx = x - gx[i];
		local sy = y - gy[i];
		local sz = z - gz[i];
		local c = gr[i] * 2.5 - ::sqrt(sx * sx + sy * sy + sz * sz);
		d = d > c ? d : c;
	}
	local nx = x - ::SK_NX, ny = y - ::SK_NY, nz = z - ::SK_NZ;
	local cn = 0.042 - ::sqrt(nx * nx + ny * ny + nz * nz);
	return d > cn ? d : cn;
}

function sk_body(x, y, z) {
	local ax = x - ::SK_AX, ay = y - ::SK_AY, az = z - ::SK_AZ;
	local af = ax * ::SK_PFX + ay * ::SK_PFY + az * ::SK_PFZ;
	local au = ax * ::SK_PUX + ay * ::SK_PUY + az * ::SK_PUZ;
	local ar = ax * ::SK_PRX + ay * ::SK_PRY + az * ::SK_PRZ;
	af = af * 0.74;
	au = au * 1.12;
	local da = (::sqrt(af * af + au * au + ar * ar) - ::SK_AR) * 0.74;
	local tx = x - ::SK_TX, ty = y - ::SK_TY, tz = z - ::SK_TZ;
	local dt = ::sqrt(tx * tx + ty * ty + tz * tz) - ::SK_TR;
	local k = 0.09;
	local hh = 0.5 + 0.5 * (dt - da) / k;
	hh = hh < 0.0 ? 0.0 : (hh > 1.0 ? 1.0 : hh);
	return dt + (da - dt) * hh - k * hh * (1.0 - hh);
}

function sk_crown(x, y, z) {
	local ox = x - ::SK_CRX, oy = y - ::SK_CRY, oz = z - ::SK_CRZ;
	local cu = ox * ::SK_PUX + oy * ::SK_PUY + oz * ::SK_PUZ;
	local px = ox - ::SK_PUX * cu, py = oy - ::SK_PUY * cu, pz = oz - ::SK_PUZ * cu;
	local ring = ::sqrt(px * px + py * py + pz * pz) - 0.2;
	local d = ::sqrt(ring * ring + cu * cu) - 0.028;
	local ds = ::sk_capsules(x, y, z, ::SK_CROWN0, ::SK_NC, 99.0);
	return d < ds ? d : ds;
}

function sk_gem(x, y, z) {
	local ox = x - ::SK_GMX, oy = y - ::SK_GMY, oz = z - ::SK_GMZ;
	local a = ox * ::SK_GAX + oy * ::SK_GAY + oz * ::SK_GAZ;
	local b = ox * ::SK_PUX + oy * ::SK_PUY + oz * ::SK_PUZ;
	local c = ox * ::SK_GBX + oy * ::SK_GBY + oz * ::SK_GBZ;
	a = a < 0.0 ? -a : a;
	b = b < 0.0 ? -b : b;
	c = c < 0.0 ? -c : c;
	return (a + b * 0.62 + c - 0.075) * 0.54;
}

// the creature along one ray: only the parts the ray passes near (groups: 1 body, 2 head; cand: capsule indices, glows as 100 + index)
function sk_spider(x, y, z, groups, cand, nc) {
	local d = 99.0;
	if ((groups & 1) != 0) { d = ::sk_body(x, y, z); }
	if ((groups & 2) != 0) {
		local ds = ::sk_skull(x, y, z);
		d = d < ds ? d : ds;
		local dr = ::sk_crown(x, y, z);
		d = d < dr ? d : dr;
		local dg = ::sk_gem(x, y, z);
		d = d < dg ? d : dg;
	}
	for (local k = 0; k < nc; ++k) {
		local c = cand[k];
		if (c >= 100) {
			local g = c - 100;
			local gx = x - ::SK_GX[g];
			local gy = y - ::SK_GY[g];
			local gz = z - ::SK_GZ[g];
			local dg2 = ::sqrt(gx * gx + gy * gy + gz * gz) - ::SK_GR[g];
			d = d < dg2 ? d : dg2;
		} else {
			local pax = x - ::SK_CAX[c];
			local pay = y - ::SK_CAY[c];
			local paz = z - ::SK_CAZ[c];
			local cdx = ::SK_CDX[c];
			local cdy = ::SK_CDY[c];
			local cdz = ::SK_CDZ[c];
			local hh = (pax * cdx + pay * cdy + paz * cdz) * ::SK_CID[c];
			hh = hh < 0.0 ? 0.0 : (hh > 1.0 ? 1.0 : hh);
			local ex = pax - cdx * hh;
			local ey = pay - cdy * hh;
			local ez = paz - cdz * hh;
			local dc = ::sqrt(ex * ex + ey * ey + ez * ez) - ::SK_CR[c];
			d = d < dc ? d : dc;
		}
	}
	return d;
}

// does the ray (origin o, unit direction d) pass within r of c ahead of the origin
function sk_ray_near(ox, oy, oz, dx, dy, dz, cx, cy, cz, r) {
	local vx = cx - ox;
	local vy = cy - oy;
	local vz = cz - oz;
	local tc = vx * dx + vy * dy + vz * dz;
	if (tc < -r) { return false; }
	return vx * vx + vy * vy + vz * vz - tc * tc < r * r;
}

// 0 world, 1 bone, 2 chitin, 3 glow, 4 marking, 5 ruby
function sk_material(x, y, z) {
	local dw = ::sk_world(x, y, z);
	local ds = ::sk_skull(x, y, z);
	local dj = ::sk_capsules(x, y, z, 0, ::SK_NBONE, 99.0);
	local bone = ds < dj ? ds : dj;
	local de = ::sk_glows(x, y, z);
	local dcr = ::sk_crown(x, y, z);
	de = de < dcr ? de : dcr;
	local dgm = ::sk_gem(x, y, z);
	local db = ::sk_body(x, y, z);
	local dl = ::sk_capsules(x, y, z, ::SK_NBONE, ::SK_CROWN0, 99.0);
	local chit = db < dl ? db : dl;
	local m = 0;
	local best = dw;
	if (bone < best) { best = bone; m = 1; }
	if (chit < best) { best = chit; m = 2; }
	if (de < best) { best = de; m = 3; }
	if (dgm < best) { best = dgm; m = 5; }
	if (m == 2 && db <= dl) {
		local ax = x - ::SK_AX, ay = y - ::SK_AY, az = z - ::SK_AZ;
		local au = ax * ::SK_PUX + ay * ::SK_PUY + az * ::SK_PUZ;
		local ar = ax * ::SK_PRX + ay * ::SK_PRY + az * ::SK_PRZ;
		local af = ax * ::SK_PFX + ay * ::SK_PFY + az * ::SK_PFZ;
		local ar2 = ar < 0.0 ? -ar : ar;
		local af2 = af < 0.0 ? -af : af;
		if (au > 0.09 && ar2 < 0.03 + af2 * 0.2 && af > -0.28 && af < 0.18) { m = 4; }
	}
	return m;
}

// a pixel's palette index plus its depth (0..255) above bit 10, for the particle pass
function sk_trace(dx, dy, dz, px, py, cand) {
	local ox = ::SK_EX;
	local oy = ::SK_EY;
	local oz = ::SK_EZ;
	local groups = 0;
	local nc = 0;
	if (::sk_ray_near(ox, oy, oz, dx, dy, dz, ::SK_BGX, ::SK_BGY, ::SK_BGZ, 0.62)) { groups = 1; }
	if (::sk_ray_near(ox, oy, oz, dx, dy, dz, ::SK_HGX, ::SK_HGY, ::SK_HGZ, 0.74)) { groups = groups + 2; }
	local crown0 = ::SK_CROWN0;
	for (local c = 0; c < crown0; ++c) {
		if (::sk_ray_near(ox, oy, oz, dx, dy, dz, ::SK_CMX[c], ::SK_CMY[c], ::SK_CMZ[c], ::SK_CB[c] + 0.01)) {
			cand[nc] = c;
			nc = nc + 1;
		}
	}
	local ng = ::SK_NG;
	for (local g = 3; g < ng; ++g) {
		if (::sk_ray_near(ox, oy, oz, dx, dy, dz, ::SK_GX[g], ::SK_GY[g], ::SK_GZ[g], ::SK_GR[g] + 0.01)) {
			cand[nc] = 100 + g;
			nc = nc + 1;
		}
	}
	local near = groups != 0 || nc != 0;
	local t = 0.04;
	local glow = 9.0;
	if (!near) {
		local vx = ::SK_BCX - ox;
		local vy = ::SK_BCY - oy;
		local vz = ::SK_BCZ - oz;
		local tc = vx * dx + vy * dy + vz * dz;
		local perp = vx * vx + vy * vy + vz * vz - tc * tc;
		glow = tc < 0.0 ? 9.0 : ::sqrt(perp < 0.0 ? 0.0 : perp) - 0.55;
	}
	local hit = false;
	local inside = ::sk_world(ox, oy, oz) < 0.0;
	local i = 0;
	while (i < 64) {
		local x = ox + dx * t;
		local y = oy + dy * t;
		local z = oz + dz * t;
		local dw = ::sk_world(x, y, z);
		if (inside) {
			// the camera sits inside a wall: the first slab along the ray is cut away
			if (dw < 0.0) {
				t += 0.01 - dw;
				i = i + 1;
				continue;
			}
			inside = false;
		}
		local d = dw;
		if (near) {
			local ds = ::sk_spider(x, y, z, groups, cand, nc);
			glow = ds < glow ? ds : glow;
			d = dw < ds ? dw : ds;
		}
		if (d < 0.001 + t * 0.0015) {
			hit = true;
			break;
		}
		t += d * 0.85;
		if (t > 14.0) { break; }
		i = i + 1;
	}
	local dith = ::SK_BAYER[(py % 4) * 4 + px % 4] / 16.0 - 0.47;
	if (!hit) {
		local aura = 1.0 - glow * 2.0;
		aura = aura < 0.0 ? 0.0 : aura;
		local f = (aura * aura * 9.0 + dith + 0.5 + i * 0.015).tointeger();
		return ::SK_FOG + (f < 0 ? 0 : (f > 9 ? 9 : f)) + 255 * 1024;
	}
	local depth = (t * 18.0).tointeger();
	depth = (depth > 254 ? 254 : depth) * 1024;
	local x = ox + dx * t;
	local y = oy + dy * t;
	local z = oz + dz * t;
	local m = ::sk_material(x, y, z);
	local fog = 1.0 - t / 14.0;
	fog = fog < 0.0 ? 0.0 : fog;
	local lx = ::SK_LX - x;
	local ly = ::SK_LY - y;
	local lz = ::SK_LZ - z;
	local ld2 = lx * lx + ly * ly + lz * lz;
	local lil = 1.0 / ::sqrt(ld2 + 0.0001);
	lx *= lil; ly *= lil; lz *= lil;
	if (m == 0) {
		local hs = 0.004;
		local n0 = ::sk_field(x + hs, y - hs, z - hs);
		local n1 = ::sk_field(x - hs, y - hs, z + hs);
		local n2 = ::sk_field(x - hs, y + hs, z - hs);
		local n3 = ::sk_field(x + hs, y + hs, z + hs);
		local g = (n0 + n1 + n2 + n3) * 0.25;
		local nx = n0 - n1 - n2 + n3;
		local ny = -n0 - n1 + n2 + n3;
		local nz = -n0 + n1 - n2 + n3;
		local sg = g < 0.0 ? -1.0 : 1.0;
		local nl = sg / ::sqrt(nx * nx + ny * ny + nz * nz + 0.0000001);
		nx *= nl; ny *= nl; nz *= nl;
		local head = -(nx * dx + ny * dy + nz * dz);
		head = head < 0.0 ? 0.0 : head;
		local ember = nx * lx + ny * ly + nz * lz;
		ember = ember < 0.0 ? 0.0 : ember * 1.6 / (1.0 + ld2 * 3.0);
		local s = ::SK_S * 3.1;
		local vx = x * s;
		local vy = y * s;
		local vz = z * s;
		local vein = ::sin(vx) * ::cos(vy) + ::sin(vy) * ::cos(vz) + ::sin(vz) * ::cos(vx);
		local va = vein < 0.0 ? -vein : vein;
		local pulse = 0.5 + 0.5 * ::sin(vein * 5.0 - ::SK_T * 3.4 + vz * 0.25);
		local ao = 1.0 - i * 0.011;
		ao = ao < 0.3 ? 0.3 : ao;
		local lit = (0.06 + head * 0.7) * fog * ao;
		if (va < 0.3) {
			local edge = 1.0 - va / 0.3;
			local glowv = pulse * pulse * edge * 1.15 * (0.35 + fog * 0.65);
			lit = glowv > lit ? glowv : lit * 0.4;
		}
		if (ember * fog > lit * 1.15 + (va < 0.3 ? 0.25 : 0.0)) {
			local e = ((ember * fog * 0.85 + lit * 0.3) * 11.0 + dith + 0.5).tointeger();
			return ::SK_EMBER + (e < 0 ? 0 : (e > 11 ? 11 : e)) + depth;
		}
		local hue = (vx * 0.11 + vy * 0.07 - vz * 0.05) * 0.3 + g * 0.35 + ::SK_T * 0.05 + t * 0.035 + (va < 0.3 ? 0.33 : 0.0);
		hue = hue - ::floor(hue);
		local hq = (hue * 12.0).tointeger();
		hq = hq > 11 ? 11 : hq;
		local l = (lit * 11.0 + dith + 0.5).tointeger();
		l = l < 0 ? 0 : (l > 11 ? 11 : l);
		return ::SK_WORLD + hq * 12 + l + depth;
	}
	if (m == 3) {
		local e = (2.4 + ::SK_FLICK + dith).tointeger();
		return ::SK_EYE + (e < 0 ? 0 : (e > 3 ? 3 : e)) + depth;
	}
	if (m == 5) {
		local gox = x - ::SK_GMX, goy = y - ::SK_GMY, goz = z - ::SK_GMZ;
		local a = gox * ::SK_GAX + goy * ::SK_GAY + goz * ::SK_GAZ;
		local b = gox * ::SK_PUX + goy * ::SK_PUY + goz * ::SK_PUZ;
		local c = gox * ::SK_GBX + goy * ::SK_GBY + goz * ::SK_GBZ;
		local sa = a < 0.0 ? -1.0 : 1.0;
		local sb = b < 0.0 ? -1.0 : 1.0;
		local sc = c < 0.0 ? -1.0 : 1.0;
		local gnx = ::SK_GAX * sa + ::SK_PUX * sb * 0.62 + ::SK_GBX * sc;
		local gny = ::SK_GAY * sa + ::SK_PUY * sb * 0.62 + ::SK_GBY * sc;
		local gnz = ::SK_GAZ * sa + ::SK_PUZ * sb * 0.62 + ::SK_GBZ * sc;
		local gl = 1.0 / ::sqrt(gnx * gnx + gny * gny + gnz * gnz);
		local face = -(gnx * dx + gny * dy + gnz * dz) * gl;
		face = face < 0.0 ? 0.0 : face;
		local facet = (sa > 0.0 ? 1 : 0) + (sb > 0.0 ? 2 : 0) + (sc > 0.0 ? 4 : 0);
		local twinkle = ::sin(::SK_T * 9.0 + facet * 2.3);
		local glit = 0.25 + face * face * 0.6 + (twinkle > 0.85 ? 0.6 : 0.0);
		local r = (glit * 7.0 + dith + 0.5).tointeger();
		return ::SK_GEM + (r < 0 ? 0 : (r > 7 ? 7 : r)) + depth;
	}
	local hstep = 0.0015;
	local n0 = ::sk_spider(x + hstep, y - hstep, z - hstep, groups, cand, nc);
	local n1 = ::sk_spider(x - hstep, y - hstep, z + hstep, groups, cand, nc);
	local n2 = ::sk_spider(x - hstep, y + hstep, z - hstep, groups, cand, nc);
	local n3 = ::sk_spider(x + hstep, y + hstep, z + hstep, groups, cand, nc);
	local nx = n0 - n1 - n2 + n3;
	local ny = -n0 - n1 + n2 + n3;
	local nz = -n0 + n1 - n2 + n3;
	local nl = 1.0 / ::sqrt(nx * nx + ny * ny + nz * nz + 0.0000001);
	nx *= nl; ny *= nl; nz *= nl;
	local vd = -(nx * dx + ny * dy + nz * dz);
	vd = vd < 0.0 ? 0.0 : vd;
	local up = nx * ::SK_PUX + ny * ::SK_PUY + nz * ::SK_PUZ;
	up = up < 0.0 ? 0.0 : up;
	local rim = 1.0 - vd;
	rim = rim * rim * rim;
	local eyel = nx * lx + ny * ly + nz * lz;
	eyel = eyel < 0.0 ? 0.0 : eyel / (1.0 + ld2 * 6.0);
	if (m == 1) {
		local blit = 0.1 + vd * 0.55 + up * 0.22 + rim * 0.25;
		local so = 99.0;
		for (local k = 0; k < ::SK_NSOCK; ++k) {
			local ox2 = x - ::SK_GX[k], oy2 = y - ::SK_GY[k], oz2 = z - ::SK_GZ[k];
			local q = ::sqrt(ox2 * ox2 + oy2 * oy2 + oz2 * oz2) - ::SK_GR[k] * 2.5;
			so = q < so ? q : so;
		}
		if (so < 0.012) { blit = blit * 0.15 + 0.07 * (1.0 + ::SK_FLICK); }
		local nsx = x - ::SK_NX, nsy = y - ::SK_NY, nsz = z - ::SK_NZ;
		if (nsx * nsx + nsy * nsy + nsz * nsz < 0.0025) { blit = blit * 0.2; }
		local jx = x - ::SK_JX, jy = y - ::SK_JY, jz = z - ::SK_JZ;
		local jf = jx * ::SK_PFX + jy * ::SK_PFY + jz * ::SK_PFZ;
		local ju = jx * ::SK_PUX + jy * ::SK_PUY + jz * ::SK_PUZ;
		if (jf > 0.045 && ju > -0.05 && ju < 0.042) {
			local tr = (jx * ::SK_PRX + jy * ::SK_PRY + jz * ::SK_PRZ) * 44.0;
			if (tr - ::floor(tr) < 0.24 || (ju > -0.008 && ju < 0.006)) { blit = blit * 0.22; }
		}
		local b = (blit * fog * 11.0 + dith + 0.5).tointeger();
		return ::SK_BONE + (b < 0 ? 0 : (b > 11 ? 11 : b)) + depth;
	}
	if (m == 4) {
		local k = ((0.45 + vd * 0.45 + 0.3 * ::sin(::SK_T * 4.0)) * 5.0 + dith + 0.5).tointeger();
		return ::SK_MARK + (k < 0 ? 0 : (k > 5 ? 5 : k)) + depth;
	}
	local lit = 0.03 + vd * 0.16 + rim * 0.62 + eyel * 0.9;
	local cc = (lit * fog * 11.0 + dith + 0.5).tointeger();
	return ::SK_CHIT + (cc < 0 ? 0 : (cc > 11 ? 11 : cc)) + depth;
}

function sk_row(y) {
	local out = [];
	local cand = ::array(64, 0);
	local ww = ::SK_W;
	local half_h = ::SK_H * 0.5;
	local zoom = 0.6 / half_h;
	local v = (half_h - y - 0.5) * zoom;
	for (local x = 0; x < ww; ++x) {
		local u = (x + 0.5 - ww * 0.5) * zoom;
		local dx = ::SK_FX + ::SK_RX * u + ::SK_UX * v;
		local dy = ::SK_FY + ::SK_RY * u + ::SK_UY * v;
		local dz = ::SK_FZ + ::SK_RZ * u + ::SK_UZ * v;
		local il = 1.0 / ::sqrt(dx * dx + dy * dy + dz * dz);
		out.push(::sk_trace(dx * il, dy * il, dz * il, x, y, cand));
	}
	return out;
}

// the sheet's surface on the + side: Newton steps onto sk_field == SK_TH (the reference's float& params: returns [x, y, z])
function sk_project(x, y, z) {
	for (local k = 0; k < 5; ++k) {
		local e = 0.004;
		local g = ::sk_field(x, y, z) - ::SK_TH;
		local gx = (::sk_field(x + e, y, z) - ::sk_field(x - e, y, z)) / (2.0 * e);
		local gy = (::sk_field(x, y + e, z) - ::sk_field(x, y - e, z)) / (2.0 * e);
		local gz = (::sk_field(x, y, z + e) - ::sk_field(x, y, z - e)) / (2.0 * e);
		local q = g / (gx * gx + gy * gy + gz * gz + 0.000001);
		x -= gx * q;
		y -= gy * q;
		z -= gz * q;
	}
	return [x, y, z];
}

class Ossuary extends Scene {
	bx = 0.4; by = 0.3; bz = 0.2;
	fx = 0.0; fy = 0.0; fz = 1.0;
	ux = 0.0; uy = 1.0; uz = 0.0;
	rx = 1.0; ry = 0.0; rz = 0.0;
	cxs = 0.0; cys = 0.0; czs = 0.0;
	ex = 0.0; ey = 0.0; ez = 0.0;
	cux = 0.0; cuy = 1.0; cuz = 0.0;
	placed = false;
	footx = null;
	footy = null;
	footz = null;
	fromx = null;
	fromy = null;
	fromz = null;
	tox = null;
	toy = null;
	toz = null;
	stept = null;
	legfo = null;
	legreach = null;
	ppx = null;
	ppy = null;
	ppz = null;
	pvx = null;
	pvy = null;
	pvz = null;
	plife = null;
	prate = null;
	depth = null;
	np = 240;
	pnext = 0;
	l1 = 0.5;
	l2 = 0.66;
	tt = 0.0;
	wander = 0.0;
	spawn_acc = 0.0;

	constructor() {
		footx = []; footy = []; footz = [];
		fromx = []; fromy = []; fromz = [];
		tox = []; toy = []; toz = [];
		stept = [];
		legfo = [0.3, 0.1, -0.1, -0.3];
		legreach = [0.42, 0.14, -0.08, -0.36];
		ppx = []; ppy = []; ppz = [];
		pvx = []; pvy = []; pvz = [];
		plife = []; prate = [];
		depth = [];
	}

	function build() {
		name = "OSSUARY";
		handle = "eight legs of inverse kinematics . a landscape that will not hold still";
		::SK_WORLD = pal.size();
		for (local hq = 0; hq < 12; ++hq) {
			local c = ::gfx.rainbow_rgb(hq * 0.5236 + 2.2);
			for (local s = 0; s < 12; ++s) {
				local f = s / 11.0;
				local k = f * f * 0.9 + f * 0.25;
				local wh = f > 0.82 ? (f - 0.82) * 2.2 : 0.0;
				local r = 5.0 + c[0] * k * 0.9;
				local g = 2.0 + c[1] * k * 0.9;
				local b = 10.0 + c[2] * k * 0.9;
				pal.add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh);
			}
		}
		::SK_EMBER = pal.size();
		for (local e = 0; e < 12; ++e) {
			local f = e / 11.0;
			pal.add(18.0 + 237.0 * f, 2.0 + 150.0 * f * f * f, 6.0 + 60.0 * f * f * f * f);
		}
		::SK_BONE = pal.ramp(12, 26.0, 20.0, 16.0, 250.0, 238.0, 206.0);
		::SK_CHIT = pal.size();
		for (local k = 0; k < 12; ++k) {
			local f = k / 11.0;
			pal.add(6.0 + 200.0 * f * f + 49.0 * f * f * f * f, 3.0 + 20.0 * f * f * f + 150.0 * f * f * f * f * f * f, 8.0 + 30.0 * f * f + 60.0 * f * f * f * f * f * f);
		}
		::SK_EYE = pal.ramp(4, 220.0, 30.0, 10.0, 255.0, 250.0, 190.0);
		::SK_MARK = pal.ramp(6, 90.0, 0.0, 8.0, 255.0, 60.0, 30.0);
		::SK_FOG = pal.size();
		for (local g = 0; g < 10; ++g) {
			local f = g / 9.0;
			pal.add(2.0 + 110.0 * f * f, 1.0 + 12.0 * f * f * f, 5.0 + 22.0 * f);
		}
		::SK_SPARK = pal.size();
		for (local k = 0; k < 8; ++k) {
			local f = k / 7.0;
			pal.add(90.0 + 165.0 * f, 8.0 + 120.0 * f * f + 120.0 * f * f * f * f, 4.0 + 40.0 * f * f * f + 180.0 * f * f * f * f * f * f);
		}
		::SK_GEM = pal.size();
		for (local k = 0; k < 8; ++k) {
			local f = k / 7.0;
			local wh = f > 0.75 ? (f - 0.75) * 3.2 : 0.0;
			local r = 40.0 + 215.0 * f;
			local g = 0.0 + 30.0 * f * f;
			local b = 12.0 + 50.0 * f * f;
			pal.add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh);
		}
		for (local i = 0; i < 8; ++i) {
			footx.push(0.0); footy.push(0.0); footz.push(0.0);
			fromx.push(0.0); fromy.push(0.0); fromz.push(0.0);
			tox.push(0.0); toy.push(0.0); toz.push(0.0);
			stept.push(-1.0);
		}
		for (local i = 0; i < np; ++i) {
			ppx.push(0.0); ppy.push(0.0); ppz.push(0.0);
			pvx.push(0.0); pvy.push(0.0); pvz.push(0.0);
			plife.push(0.0); prate.push(1.0);
		}
		wander = rng.next(628) * 0.01;
	}

	function set_world(t) {
		::SK_T = t;
		::SK_S = 0.8 + 0.07 * ::sin(t * 0.37);
		::SK_K = 0.45 * ::sin(t * 0.21 + 0.6);
		::SK_TW = 0.11 * ::sin(t * 0.29 + 1.3);
		::SK_WA = 0.18 + 0.14 * ::sin(t * 0.33 + 2.0);
		local ph = t * 0.19;
		local a = 0.5 + 0.5 * ::cos(ph);
		local b = 0.5 + 0.5 * ::cos(ph - 2.0944);
		local c = 0.5 + 0.5 * ::cos(ph + 2.0944);
		a = a * a * a; b = b * b * b; c = c * c * c;
		local sum = a + b + c;
		::SK_MG = a / sum;
		::SK_MP = b / sum;
		::SK_MD = c / sum;
		::SK_CX = cxs; ::SK_CY = cys; ::SK_CZ = czs;
	}

	function frame_from_normal() {
		local e = 0.004;
		local gx = ::sk_field(bx + e, by, bz) - ::sk_field(bx - e, by, bz);
		local gy = ::sk_field(bx, by + e, bz) - ::sk_field(bx, by - e, bz);
		local gz = ::sk_field(bx, by, bz + e) - ::sk_field(bx, by, bz - e);
		local gl = 1.0 / ::sqrt(gx * gx + gy * gy + gz * gz + 0.0000001);
		ux = gx * gl; uy = gy * gl; uz = gz * gl;
		local d = fx * ux + fy * uy + fz * uz;
		fx -= ux * d; fy -= uy * d; fz -= uz * d;
		local fl = ::sqrt(fx * fx + fy * fy + fz * fz);
		if (fl < 0.001) {
			fx = uy; fy = uz; fz = ux;
			d = fx * ux + fy * uy + fz * uz;
			fx -= ux * d; fy -= uy * d; fz -= uz * d;
			fl = ::sqrt(fx * fx + fy * fy + fz * fz);
		}
		fx /= fl; fy /= fl; fz /= fl;
		rx = uy * fz - uz * fy;
		ry = uz * fx - ux * fz;
		rz = ux * fy - uy * fx;
	}

	function rest_foot(i, lead) {
		local side = i < 4 ? 1.0 : -1.0;
		local j = i % 4;
		local reach = legreach[j] + lead;
		local x = bx + fx * reach + rx * side * 0.78;
		local y = by + fy * reach + ry * side * 0.78;
		local z = bz + fz * reach + rz * side * 0.78;
		local p = ::sk_project(x, y, z);
		tox[i] = p[0]; toy[i] = p[1]; toz[i] = p[2];
	}

	function jitter(n) { return (rng.next(n * 2 + 1) - n) / (n * 1.0); }

	function spawn(x, y, z, vx, vy, vz, life) {
		local i = pnext;
		pnext = (pnext + 1) % np;
		ppx[i] = x; ppy[i] = y; ppz[i] = z;
		pvx[i] = vx; pvy[i] = vy; pvz[i] = vz;
		plife[i] = 1.0;
		prate[i] = 1.0 / life;
	}

	function update_particles(dt) {
		local hx = bx + ux * 0.2;
		local hy = by + uy * 0.2;
		local hz = bz + uz * 0.2;
		spawn_acc += dt * 110.0;
		while (spawn_acc >= 1.0) {
			spawn_acc -= 1.0;
			local kind = rng.next(14);
			if (kind >= 10) {
				local ca2 = rng.next(628) * 0.01;
				local cxr = ::cos(ca2) * 0.2;
				local sxr = ::sin(ca2) * 0.2;
				local crx = hx + fx * 0.34 + ux * 0.55;
				local cry = hy + fy * 0.34 + uy * 0.55;
				local crz = hz + fz * 0.34 + uz * 0.55;
				spawn(crx + fx * cxr + rx * sxr, cry + fy * cxr + ry * sxr, crz + fz * cxr + rz * sxr, ux * 0.9 + fx * cxr + rx * sxr, uy * 0.9 + fy * cxr + ry * sxr, uz * 0.9 + fz * cxr + rz * sxr, 0.45 + rng.next(40) * 0.01);
			} else if (kind < 4) {
				local ex2 = hx + fx * 0.52 + ux * 0.14 + rx * jitter(100) * 0.09;
				local ey2 = hy + fy * 0.52 + uy * 0.14 + ry * jitter(100) * 0.09;
				local ez2 = hz + fz * 0.52 + uz * 0.14 + rz * jitter(100) * 0.09;
				spawn(ex2, ey2, ez2, fx * 0.5 + ux * 0.35 + rx * jitter(100) * 0.3, fy * 0.5 + uy * 0.35 + ry * jitter(100) * 0.3, fz * 0.5 + uz * 0.35 + rz * jitter(100) * 0.3, 1.1 + rng.next(80) * 0.01);
			} else if (kind < 6) {
				spawn(hx - fx * 0.55, hy - fy * 0.55, hz - fz * 0.55, -fx * 0.3 + ux * 0.4 + jitter(100) * 0.2, -fy * 0.3 + uy * 0.4 + jitter(100) * 0.2, -fz * 0.3 + uz * 0.4 + jitter(100) * 0.2, 1.4 + rng.next(80) * 0.01);
			} else {
				local r = 0.6 + rng.next(140) * 0.01;
				local a = rng.next(628) * 0.01;
				local ca = ::cos(a) * r;
				local sa = ::sin(a) * r;
				spawn(hx + fx * ca + rx * sa - ux * 0.1, hy + fy * ca + ry * sa - uy * 0.1, hz + fz * ca + rz * sa - uz * 0.1, ux * 0.25, uy * 0.25, uz * 0.25, 2.0 + rng.next(100) * 0.01);
			}
		}
		local drag = 1.0 - dt * 0.9;
		for (local i = 0; i < np; ++i) {
			if (plife[i] <= 0.0) { continue; }
			local ox = ppx[i] - hx;
			local oy = ppy[i] - hy;
			local oz = ppz[i] - hz;
			local sx = uy * oz - uz * oy;
			local sy = uz * ox - ux * oz;
			local sz = ux * oy - uy * ox;
			pvx[i] = (pvx[i] + (ux * 0.9 + sx * 1.3 - ox * 0.25) * dt) * drag;
			pvy[i] = (pvy[i] + (uy * 0.9 + sy * 1.3 - oy * 0.25) * dt) * drag;
			pvz[i] = (pvz[i] + (uz * 0.9 + sz * 1.3 - oz * 0.25) * dt) * drag;
			ppx[i] = ppx[i] + pvx[i] * dt;
			ppy[i] = ppy[i] + pvy[i] * dt;
			ppz[i] = ppz[i] + pvz[i] * dt;
			plife[i] = plife[i] - dt * prate[i];
		}
	}

	function update(t, dt) {
		tt = t;
		if (!placed) {
			cxs = bx; cys = by; czs = bz;
		}
		set_world(t);
		local turn = 0.75 * ::sin(t * 0.53 + wander) + 0.45 * ::sin(t * 1.37 + wander * 2.0) + 0.2 * ::sin(t * 2.9);
		local ang = turn * dt * 1.6;
		local c = ::cos(ang);
		local s = ::sin(ang);
		local nfx = fx * c + rx * s;
		local nfy = fy * c + ry * s;
		local nfz = fz * c + rz * s;
		fx = nfx; fy = nfy; fz = nfz;
		local speed = 0.55 + 0.2 * ::sin(t * 0.8);
		bx += fx * speed * dt;
		by += fy * speed * dt;
		bz += fz * speed * dt;
		local pb = ::sk_project(bx, by, bz);
		bx = pb[0]; by = pb[1]; bz = pb[2];
		frame_from_normal();
		local k = dt * 0.6;
		k = k > 1.0 ? 1.0 : k;
		cxs += (bx - cxs) * k; cys += (by - cys) * k; czs += (bz - czs) * k;
		::SK_CX = cxs; ::SK_CY = cys; ::SK_CZ = czs;
		if (!placed) {
			for (local i = 0; i < 8; ++i) {
				rest_foot(i, 0.0);
				footx[i] = tox[i]; footy[i] = toy[i]; footz[i] = toz[i];
			}
		}
		local stepping_a = false;
		local stepping_b = false;
		for (local i = 0; i < 8; ++i) {
			if (stept[i] >= 0.0) {
				if ((i + i / 4) % 2 == 0) { stepping_a = true; } else { stepping_b = true; }
			}
		}
		for (local i = 0; i < 8; ++i) {
			local group_a = (i + i / 4) % 2 == 0;
			if (stept[i] >= 0.0) {
				stept[i] = stept[i] + dt / 0.17;
				rest_foot(i, 0.2);
				if (stept[i] >= 1.0) {
					stept[i] = -1.0;
					footx[i] = tox[i]; footy[i] = toy[i]; footz[i] = toz[i];
				} else {
					local e = stept[i];
					local lift = ::sin(e * 3.14159265) * 0.22;
					footx[i] = fromx[i] + (tox[i] - fromx[i]) * e + ux * lift;
					footy[i] = fromy[i] + (toy[i] - fromy[i]) * e + uy * lift;
					footz[i] = fromz[i] + (toz[i] - fromz[i]) * e + uz * lift;
				}
			} else {
				local pp = ::sk_project(footx[i], footy[i], footz[i]);
				footx[i] = pp[0]; footy[i] = pp[1]; footz[i] = pp[2];
				rest_foot(i, 0.0);
				local ddx = footx[i] - tox[i];
				local ddy = footy[i] - toy[i];
				local ddz = footz[i] - toz[i];
				local other_moving = group_a ? stepping_b : stepping_a;
				if (!other_moving && ddx * ddx + ddy * ddy + ddz * ddz > 0.1) {
					stept[i] = 0.0;
					fromx[i] = footx[i]; fromy[i] = footy[i]; fromz[i] = footz[i];
					if (group_a) { stepping_a = true; } else { stepping_b = true; }
				}
			}
		}
		update_particles(dt);
		local bob = 0.2 + 0.018 * ::sin(t * 11.0);
		local hx = bx + ux * bob;
		local hy = by + uy * bob;
		local hz = bz + uz * bob;
		local orbit = 2.4 + t * 0.33 + 0.5 * ::sin(t * 0.17);
		local oc = ::cos(orbit) * 2.1;
		local os = ::sin(orbit) * 2.1;
		local dex = hx + fx * oc + rx * os + ux * 1.0;
		local dey = hy + fy * oc + ry * os + uy * 1.0;
		local dez = hz + fz * oc + rz * os + uz * 1.0;
		local cdx = dex - hx;
		local cdy = dey - hy;
		local cdz = dez - hz;
		local want = ::sqrt(cdx * cdx + cdy * cdy + cdz * cdz);
		cdx /= want; cdy /= want; cdz /= want;
		local reach = 1.5;
		while (reach < want) {
			local d = ::sk_world(hx + cdx * reach, hy + cdy * reach, hz + cdz * reach);
			if (d < 0.14) { break; }
			reach += d * 0.8;
		}
		reach = reach > want ? want : reach;
		reach = reach - 0.1;
		reach = reach < 1.5 ? 1.5 : reach;
		dex = hx + cdx * reach;
		dey = hy + cdy * reach;
		dez = hz + cdz * reach;
		local kc = placed ? dt * 2.8 : 1.0;
		kc = kc > 1.0 ? 1.0 : kc;
		ex += (dex - ex) * kc; ey += (dey - ey) * kc; ez += (dez - ez) * kc;
		local ku = placed ? dt * 2.2 : 1.0;
		ku = ku > 1.0 ? 1.0 : ku;
		cux += (ux - cux) * ku; cuy += (uy - cuy) * ku; cuz += (uz - cuz) * ku;
		placed = true;
	}

	function capsule(i, ax, ay, az, bx2, by2, bz2, r) {
		local dx = bx2 - ax;
		local dy = by2 - ay;
		local dz = bz2 - az;
		::SK_CAX[i] = ax; ::SK_CAY[i] = ay; ::SK_CAZ[i] = az;
		::SK_CDX[i] = dx; ::SK_CDY[i] = dy; ::SK_CDZ[i] = dz;
		::SK_CID[i] = 1.0 / (dx * dx + dy * dy + dz * dz + 0.000001);
		::SK_CR[i] = r;
		::SK_CMX[i] = ax + dx * 0.5; ::SK_CMY[i] = ay + dy * 0.5; ::SK_CMZ[i] = az + dz * 0.5;
		::SK_CB[i] = ::sqrt(dx * dx + dy * dy + dz * dz) * 0.5 + r;
	}

	function glow(i, x, y, z, r) {
		::SK_GX[i] = x; ::SK_GY[i] = y; ::SK_GZ[i] = z; ::SK_GR[i] = r;
	}

	// a curved horn: base, a bend out and back, a tip up and back
	function horn(i, side) {
		local bx0 = ::SK_HX + ::SK_PUX * 0.16 + ::SK_PRX * side * 0.12 - ::SK_PFX * 0.04;
		local by0 = ::SK_HY + ::SK_PUY * 0.16 + ::SK_PRY * side * 0.12 - ::SK_PFY * 0.04;
		local bz0 = ::SK_HZ + ::SK_PUZ * 0.16 + ::SK_PRZ * side * 0.12 - ::SK_PFZ * 0.04;
		local mx = bx0 + ::SK_PRX * side * 0.2 + ::SK_PUX * 0.12 - ::SK_PFX * 0.12;
		local my = by0 + ::SK_PRY * side * 0.2 + ::SK_PUY * 0.12 - ::SK_PFY * 0.12;
		local mz = bz0 + ::SK_PRZ * side * 0.2 + ::SK_PUZ * 0.12 - ::SK_PFZ * 0.12;
		local tx = mx + ::SK_PRX * side * 0.04 + ::SK_PUX * 0.26 - ::SK_PFX * 0.2;
		local ty = my + ::SK_PRY * side * 0.04 + ::SK_PUY * 0.26 - ::SK_PFY * 0.2;
		local tz = mz + ::SK_PRZ * side * 0.04 + ::SK_PUZ * 0.26 - ::SK_PFZ * 0.2;
		capsule(i, bx0, by0, bz0, mx, my, mz, 0.05);
		capsule(i + 1, mx, my, mz, tx, ty, tz, 0.028);
		return i + 2;
	}

	function render(grid) {
		set_world(tt);
		::SK_W = w;
		::SK_H = h;
		::SK_FLICK = 0.7 * ::sin(tt * 17.0) * ::sin(tt * 5.3);
		local bob = 0.2 + 0.018 * ::sin(tt * 11.0);
		local cx = bx + ux * bob;
		local cy = by + uy * bob;
		local cz = bz + uz * bob;
		::SK_PFX = fx; ::SK_PFY = fy; ::SK_PFZ = fz;
		::SK_PUX = ux; ::SK_PUY = uy; ::SK_PUZ = uz;
		::SK_PRX = rx; ::SK_PRY = ry; ::SK_PRZ = rz;
		::SK_TX = cx; ::SK_TY = cy; ::SK_TZ = cz;
		::SK_AX = cx - fx * 0.4 + ux * 0.1; ::SK_AY = cy - fy * 0.4 + uy * 0.1; ::SK_AZ = cz - fz * 0.4 + uz * 0.1;
		local nod = 0.03 * ::sin(tt * 2.1);
		::SK_HX = cx + fx * 0.34 + ux * (0.09 + nod); ::SK_HY = cy + fy * 0.34 + uy * (0.09 + nod); ::SK_HZ = cz + fz * 0.34 + uz * (0.09 + nod);
		::SK_NX = ::SK_HX + fx * 0.23 - ux * 0.06; ::SK_NY = ::SK_HY + fy * 0.23 - uy * 0.06; ::SK_NZ = ::SK_HZ + fz * 0.23 - uz * 0.06;
		local jaw = 0.012 * ::sin(tt * 6.0);
		::SK_JX = ::SK_HX + fx * 0.1 - ux * (0.14 + jaw); ::SK_JY = ::SK_HY + fy * 0.1 - uy * (0.14 + jaw); ::SK_JZ = ::SK_HZ + fz * 0.1 - uz * (0.14 + jaw);
		::SK_BCX = cx; ::SK_BCY = cy; ::SK_BCZ = cz;
		::SK_BR = 1.6;
		local ncap = 5 + 2 + 16 + 12 + 7;
		if (::SK_CAX.len() != ncap) {
			::SK_CAX = ::array(ncap, 0.0); ::SK_CAY = ::array(ncap, 0.0); ::SK_CAZ = ::array(ncap, 0.0);
			::SK_CDX = ::array(ncap, 0.0); ::SK_CDY = ::array(ncap, 0.0); ::SK_CDZ = ::array(ncap, 0.0);
			::SK_CID = ::array(ncap, 1.0); ::SK_CR = ::array(ncap, 0.1);
			::SK_CMX = ::array(ncap, 0.0); ::SK_CMY = ::array(ncap, 0.0); ::SK_CMZ = ::array(ncap, 0.0);
			::SK_CB = ::array(ncap, 1.0);
		}
		local nglow = 3 + 8;
		if (::SK_GX.len() != nglow) {
			::SK_GX = ::array(nglow, 0.0); ::SK_GY = ::array(nglow, 0.0); ::SK_GZ = ::array(nglow, 0.0); ::SK_GR = ::array(nglow, 0.02);
		}
		::SK_NC = ncap;
		::SK_CROWN0 = ncap - 7;
		::SK_NG = nglow;
		::SK_NBONE = 5;
		local shx = ::SK_HX, shy = ::SK_HY, shz = ::SK_HZ;
		glow(0, shx + fx * 0.175 + ux * 0.03 + rx * 0.088, shy + fy * 0.175 + uy * 0.03 + ry * 0.088, shz + fz * 0.175 + uz * 0.03 + rz * 0.088, 0.03);
		glow(1, shx + fx * 0.175 + ux * 0.03 - rx * 0.088, shy + fy * 0.175 + uy * 0.03 - ry * 0.088, shz + fz * 0.175 + uz * 0.03 - rz * 0.088, 0.03);
		local third = 0.024 + 0.006 * ::sin(tt * 3.0);
		glow(2, shx + fx * 0.17 + ux * 0.15, shy + fy * 0.17 + uy * 0.15, shz + fz * 0.17 + uz * 0.15, third);
		::SK_LX = ::SK_GX[2] * 0.4 + (::SK_GX[0] + ::SK_GX[1]) * 0.3 + fx * 0.1;
		::SK_LY = ::SK_GY[2] * 0.4 + (::SK_GY[0] + ::SK_GY[1]) * 0.3 + fy * 0.1;
		::SK_LZ = ::SK_GZ[2] * 0.4 + (::SK_GZ[0] + ::SK_GZ[1]) * 0.3 + fz * 0.1;
		local sjx = ::SK_JX, sjy = ::SK_JY, sjz = ::SK_JZ;
		capsule(0, sjx - rx * 0.08, sjy - ry * 0.08, sjz - rz * 0.08, sjx + rx * 0.08, sjy + ry * 0.08, sjz + rz * 0.08, 0.05);
		local ci = horn(1, 1.0);
		ci = horn(ci, -1.0);
		local fang = 0.02 * ::sin(tt * 6.0 + 1.0);
		for (local s = 0; s < 2; ++s) {
			local side = s == 0 ? 1.0 : -1.0;
			local ax = sjx + fx * 0.05 + rx * side * 0.035;
			local ay = sjy + fy * 0.05 + ry * side * 0.035;
			local az = sjz + fz * 0.05 + rz * side * 0.035;
			capsule(ci, ax, ay, az, ax + fx * 0.07 - ux * 0.12 - rx * side * (0.02 + fang), ay + fy * 0.07 - uy * 0.12 - ry * side * (0.02 + fang), az + fz * 0.07 - uz * 0.12 - rz * side * (0.02 + fang), 0.016);
			ci = ci + 1;
		}
		for (local i = 0; i < 8; ++i) {
			local side = i < 4 ? 1.0 : -1.0;
			local j = i % 4;
			local fo = legfo[j];
			local hx = cx + fx * fo * 0.45 + rx * side * 0.1 + ux * 0.03;
			local hy = cy + fy * fo * 0.45 + ry * side * 0.1 + uy * 0.03;
			local hz = cz + fz * fo * 0.45 + rz * side * 0.1 + uz * 0.03;
			local dx = footx[i] - hx;
			local dy = footy[i] - hy;
			local dz = footz[i] - hz;
			local d = ::sqrt(dx * dx + dy * dy + dz * dz);
			local dn = d < 0.0001 ? 0.0001 : d;
			dx /= dn; dy /= dn; dz /= dn;
			local dmax = (l1 + l2) * 0.995;
			local dc = d > dmax ? dmax : d;
			dc = dc < 0.08 ? 0.08 : dc;
			local a = (l1 * l1 - l2 * l2 + dc * dc) / (2.0 * dc);
			local hh = l1 * l1 - a * a;
			hh = hh < 0.0 ? 0.0 : ::sqrt(hh);
			local bux = ux + rx * side * 0.35;
			local buy = uy + ry * side * 0.35;
			local buz = uz + rz * side * 0.35;
			local bd = bux * dx + buy * dy + buz * dz;
			bux -= dx * bd; buy -= dy * bd; buz -= dz * bd;
			local bl = 1.0 / ::sqrt(bux * bux + buy * buy + buz * buz + 0.0000001);
			bux *= bl; buy *= bl; buz *= bl;
			local kx = hx + dx * a + bux * hh;
			local ky = hy + dy * a + buy * hh;
			local kz = hz + dz * a + buz * hh;
			local tipx = hx + dx * dc;
			local tipy = hy + dy * dc;
			local tipz = hz + dz * dc;
			capsule(ci, hx, hy, hz, kx, ky, kz, 0.034);
			capsule(ci + 1, kx, ky, kz, tipx, tipy, tipz, 0.02);
			ci = ci + 2;
			glow(3 + i, kx, ky, kz, 0.036 + 0.008 * ::sin(tt * 7.0 + i));
		}
		for (local k = 0; k < 3; ++k) {
			local side = k - 1.0;
			local px = ::SK_AX - fx * 0.3 + rx * side * 0.07;
			local py = ::SK_AY - fy * 0.3 + ry * side * 0.07;
			local pz = ::SK_AZ - fz * 0.3 + rz * side * 0.07;
			for (local seg = 0; seg < 4; ++seg) {
				local ph = tt * 3.1 + seg * 0.9 + k * 2.1;
				local sw = ::sin(ph) * (0.1 + seg * 0.05);
				local lift = ::cos(ph * 0.7 + k) * 0.08 + 0.05;
				local len = 0.2 - seg * 0.025;
				local qx = px - fx * len + rx * (sw + side * 0.05) + ux * lift;
				local qy = py - fy * len + ry * (sw + side * 0.05) + uy * lift;
				local qz = pz - fz * len + rz * (sw + side * 0.05) + uz * lift;
				capsule(ci, px, py, pz, qx, qy, qz, 0.03 - seg * 0.006);
				ci = ci + 1;
				px = qx; py = qy; pz = qz;
			}
		}
		local hover = 0.03 * ::sin(tt * 1.7);
		::SK_HGX = shx + ux * 0.3; ::SK_HGY = shy + uy * 0.3; ::SK_HGZ = shz + uz * 0.3;
		::SK_BGX = (::SK_AX + ::SK_TX) * 0.5; ::SK_BGY = (::SK_AY + ::SK_TY) * 0.5; ::SK_BGZ = (::SK_AZ + ::SK_TZ) * 0.5;
		::SK_CRX = shx + ux * (0.44 + hover); ::SK_CRY = shy + uy * (0.44 + hover); ::SK_CRZ = shz + uz * (0.44 + hover);
		local crx = ::SK_CRX, cry = ::SK_CRY, crz = ::SK_CRZ;
		for (local k = 0; k < 7; ++k) {
			local a = k * 0.8976 + tt * 0.6;
			local ca = ::cos(a) * 0.2;
			local sa = ::sin(a) * 0.2;
			local flame = 0.1 + 0.05 * ::sin(tt * 9.0 + k * 1.9) + 0.03 * ::sin(tt * 23.0 + k);
			local bxk = crx + fx * ca + rx * sa;
			local byk = cry + fy * ca + ry * sa;
			local bzk = crz + fz * ca + rz * sa;
			capsule(::SK_CROWN0 + k, bxk, byk, bzk, bxk + ux * flame + fx * ca * 0.25 + rx * sa * 0.25, byk + uy * flame + fy * ca * 0.25 + ry * sa * 0.25, bzk + uz * flame + fz * ca * 0.25 + rz * sa * 0.25, 0.018);
		}
		local spin = tt * 2.2;
		local gl2 = 0.3 + 0.02 * ::sin(tt * 2.5);
		::SK_GMX = crx + ux * gl2; ::SK_GMY = cry + uy * gl2; ::SK_GMZ = crz + uz * gl2;
		::SK_GAX = rx * ::cos(spin) + fx * ::sin(spin); ::SK_GAY = ry * ::cos(spin) + fy * ::sin(spin); ::SK_GAZ = rz * ::cos(spin) + fz * ::sin(spin);
		::SK_GBX = fx * ::cos(spin) - rx * ::sin(spin); ::SK_GBY = fy * ::cos(spin) - ry * ::sin(spin); ::SK_GBZ = fz * ::cos(spin) - rz * ::sin(spin);
		::SK_EX = ex; ::SK_EY = ey; ::SK_EZ = ez;
		local lx = cx + fx * 0.3 + ux * 0.25 - ex;
		local ly = cy + fy * 0.3 + uy * 0.25 - ey;
		local lz = cz + fz * 0.3 + uz * 0.25 - ez;
		local ll = 1.0 / ::sqrt(lx * lx + ly * ly + lz * lz);
		lx *= ll; ly *= ll; lz *= ll;
		local d = cux * lx + cuy * ly + cuz * lz;
		local upx = cux - lx * d;
		local upy = cuy - ly * d;
		local upz = cuz - lz * d;
		local ul = 1.0 / ::sqrt(upx * upx + upy * upy + upz * upz + 0.0000001);
		upx *= ul; upy *= ul; upz *= ul;
		::SK_FX = lx; ::SK_FY = ly; ::SK_FZ = lz;
		::SK_UX = upx; ::SK_UY = upy; ::SK_UZ = upz;
		::SK_RX = ly * upz - lz * upy;
		::SK_RY = lz * upx - lx * upz;
		::SK_RZ = lx * upy - ly * upx;
		local rows = [];
		for (local r = 0; r < h; ++r) { rows.push(::sk_row(r)); }
		if (depth.len() != w * h) { depth = ::array(w * h, 0); }
		local dep = depth;
		local idx = 0;
		for (local y = 0; y < h; ++y) {
			local row = rows[y];
			for (local x = 0; x < w; ++x) {
				local v = row[x];
				grid[idx] = v % 1024;
				dep[idx] = v / 1024;
				idx = idx + 1;
			}
		}
		draw_particles(grid);
	}

	function plot(grid, x, y, dq, level) {
		if (x < 0 || y < 0 || x >= w || y >= h) { return; }
		local at = y * w + x;
		if (depth[at] <= dq) { return; }
		local cv = grid[at];
		if (cv >= ::SK_SPARK && cv - ::SK_SPARK >= level) { return; }
		grid[at] = ::SK_SPARK + level;
	}

	function splat(grid, x, y, z, level, size) {
		local ox = x - ex;
		local oy = y - ey;
		local oz = z - ez;
		local zc = ox * ::SK_FX + oy * ::SK_FY + oz * ::SK_FZ;
		if (zc < 0.08) { return; }
		local half_h = h * 0.5;
		local scale = half_h / (0.6 * zc);
		local sx = (ox * ::SK_RX + oy * ::SK_RY + oz * ::SK_RZ) * scale + w * 0.5;
		local sy = half_h - (ox * ::SK_UX + oy * ::SK_UY + oz * ::SK_UZ) * scale;
		local px = ::floor(sx).tointeger();
		local py = ::floor(sy).tointeger();
		local dq = (zc * 18.0).tointeger();
		plot(grid, px, py, dq, level);
		if (size > 0) {
			local halo = level > 2 ? level - 3 : 0;
			plot(grid, px + 1, py, dq, halo);
			plot(grid, px - 1, py, dq, halo);
			plot(grid, px, py + 1, dq, halo);
			plot(grid, px, py - 1, dq, halo);
		}
		if (size > 1) {
			plot(grid, px + 1, py + 1, dq, level > 1 ? level - 1 : 0);
			plot(grid, px + 1, py, dq, level);
			plot(grid, px, py + 1, dq, level);
		}
	}

	function draw_particles(grid) {
		for (local i = 0; i < np; ++i) {
			local life = plife[i];
			if (life <= 0.0) { continue; }
			local level = (life * 7.99).tointeger();
			splat(grid, ppx[i], ppy[i], ppz[i], level, life > 0.55 ? 1 : 0);
		}
		local hx = bx + ux * 0.25;
		local hy = by + uy * 0.25;
		local hz = bz + uz * 0.25;
		for (local g = 0; g < 7; ++g) {
			local a = tt * (0.9 + g * 0.07) + g * 0.8976;
			local r = 1.05 + 0.2 * ::sin(tt * 1.3 + g);
			local ca = ::cos(a) * r;
			local sa = ::sin(a) * r;
			local lift = 0.35 + 0.25 * ::sin(tt * 2.0 + g * 1.7);
			local pulse = (5.5 + 2.0 * ::sin(tt * 6.0 + g)).tointeger();
			splat(grid, hx + fx * ca + rx * sa + ux * lift, hy + fy * ca + ry * sa + uy * lift, hz + fz * ca + rz * sa + uz * lift, pulse > 7 ? 7 : pulse, 2);
		}
		for (local s = 0; s < 6; ++s) {
			local ph = tt * 4.0 + s * 1.047;
			local tw = ::sin(tt * 11.0 + s * 2.7);
			if (tw < 0.2) { continue; }
			local rr = 0.1 + 0.05 * ::sin(tt * 3.0 + s);
			local ca = ::cos(ph) * rr;
			local sa = ::sin(ph) * rr;
			local lift = 0.06 * ::sin(tt * 5.0 + s * 1.3);
			splat(grid, ::SK_GMX + ::SK_GAX * ca + ::SK_GBX * sa + ux * lift, ::SK_GMY + ::SK_GAY * ca + ::SK_GBY * sa + uy * lift, ::SK_GMZ + ::SK_GAZ * ca + ::SK_GBZ * sa + uz * lift, tw > 0.7 ? 7 : 5, 1);
		}
	}
}
