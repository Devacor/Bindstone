// OSSUARY (Squirrel port of scenes/skull.jai): a horned, three-eyed skull on a spider's body wakes in its vault,
// casts an orb web, runs down a moth, cocoons it and lunges at the lens. The creature, the moth and its cocoon are
// distance fields in one march; the silk and the embers are drawn over the trace, depth-tested.

::SK_T <- 0.0;
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
::SK_QFX <- 0.0; ::SK_QFY <- 0.0; ::SK_QFZ <- 1.0;
::SK_QUX <- 0.0; ::SK_QUY <- 1.0; ::SK_QUZ <- 0.0;
::SK_LX <- 0.0; ::SK_LY <- 0.0; ::SK_LZ <- 0.0;
::SK_FLICK <- 0.0;
::SK_KAGE <- 9.0;
::SK_KICK <- 0.0;
::SK_ZOOM <- 0.5;
::SK_PUNCH <- 0.0;
::SK_WAKE <- 1.0;
::SK_GLARE <- 0.0;
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
::SK_SKB <- 0.4;
::SK_CRB <- 0.4;
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
::SK_BONE <- 0;
::SK_CHIT <- 0;
::SK_EYE <- 0;
::SK_MARK <- 0;
::SK_SPARK <- 0;
::SK_GEM <- 0;
::SK_ROWS <- [];
::SK_BAYER <- [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];
::SK_KX <- 0.0; ::SK_KY <- 1.0; ::SK_KZ <- 0.0;
::SK_MOTH <- false;
::SK_MX <- 0.0; ::SK_MY <- 0.0; ::SK_MZ <- 0.0; ::SK_MB <- 0.6;
::SK_MFX <- 0.0; ::SK_MFY <- 1.0; ::SK_MFZ <- 0.0;
::SK_MUX <- 0.0; ::SK_MUY <- 0.0; ::SK_MUZ <- 1.0;
::SK_MRX <- 1.0; ::SK_MRY <- 0.0; ::SK_MRZ <- 0.0;
::SK_WSC <- 1.0;
::SK_COC <- 0.0;
::SK_SPIN <- 0.0;
::SK_WOX <- [0.0, 0.0, 0.0, 0.0]; ::SK_WOY <- [0.0, 0.0, 0.0, 0.0]; ::SK_WOZ <- [0.0, 0.0, 0.0, 0.0];
::SK_WSX <- [0.0, 0.0, 0.0, 0.0]; ::SK_WSY <- [0.0, 0.0, 0.0, 0.0]; ::SK_WSZ <- [0.0, 0.0, 0.0, 0.0];
::SK_WNX <- [0.0, 0.0, 0.0, 0.0]; ::SK_WNY <- [0.0, 0.0, 0.0, 0.0]; ::SK_WNZ <- [0.0, 0.0, 0.0, 0.0];
::SK_WCA <- [0.22, 0.22, 0.15, 0.15];
::SK_WCB <- [0.0, 0.0, -0.075, -0.075];
::SK_WSA <- [0.22, 0.22, 0.15, 0.15];
::SK_WSB <- [0.105, 0.105, 0.09, 0.09];
::SK_WSW <- [-0.25, -0.25, -0.35, -0.35];
::SK_WING <- 0;
::SK_FUR <- 0;
::SK_COCOON <- 0;
::SK_SILK <- 0;
::SK_BACK <- 0;
::SK_BACK_HUE <- 0;
::SK_BACK_COLS <- 0;
::SK_BACK_LIGHT <- [];
::SK_BACK_WARM <- [];

// a moth's wing k (0, 1 fore, 2, 3 hind): an ellipse swept back along the body, in a plane through its root
function sk_wing(x, y, z, k) {
	local px = x - ::SK_WOX[k]; local py = y - ::SK_WOY[k]; local pz = z - ::SK_WOZ[k];
	local a = px * ::SK_WSX[k] + py * ::SK_WSY[k] + pz * ::SK_WSZ[k];
	local b = px * ::SK_MFX + py * ::SK_MFY + pz * ::SK_MFZ;
	local c = px * ::SK_WNX[k] + py * ::SK_WNY[k] + pz * ::SK_WNZ[k];
	local sb = ::SK_WSB[k];
	local u = (a - ::SK_WCA[k]) / ::SK_WSA[k];
	local v = (b - ::SK_WCB[k] - a * ::SK_WSW[k]) / sb;
	local d = (::sqrt(u * u + v * v) - 1.0) * sb;
	local dc = ::fabs(c) - 0.007;
	return d > dc ? d : dc;
}

// the wing's pattern: 0 gold, 1 umber vein or band, 2 black ring, 3 iris, 4 pupil, 5 violet margin
function sk_wing_tone(x, y, z, k) {
	local sqrt = ::sqrt;
	local px = x - ::SK_WOX[k]; local py = y - ::SK_WOY[k]; local pz = z - ::SK_WOZ[k];
	local a = px * ::SK_WSX[k] + py * ::SK_WSY[k] + pz * ::SK_WSZ[k];
	local b = px * ::SK_MFX + py * ::SK_MFY + pz * ::SK_MFZ;
	local u = (a - ::SK_WCA[k]) / ::SK_WSA[k];
	local v = (b - ::SK_WCB[k] - a * ::SK_WSW[k]) / ::SK_WSB[k];
	local r = sqrt(u * u + v * v);
	if (r > 0.82) { return 5; }
	local fore = k < 2;
	local eu = u - (fore ? 0.3 : 0.05);
	local ev = v - (fore ? -0.08 : 0.05);
	local e = sqrt(eu * eu + ev * ev) / (fore ? 0.3 : 0.45);
	if (e < 0.34) { return 4; }
	if (e < 0.74) { return 3; }
	if (e < 1.0) { return 2; }
	local band = u * 0.8 + v * 0.5 + (fore ? 0.45 : -0.2);
	if (band > -0.1 && band < 0.1) { return 1; }
	local vein = ::sin(::atan2(v, u + 1.05) * 11.0);
	return vein > 0.9 ? 1 : 0;
}

// the moth: a furred body and head, four wings, and the cocoon the spider wraps around it
function sk_moth(x, y, z) {
	local sqrt = ::sqrt;
	local mfx = ::SK_MFX; local mfy = ::SK_MFY; local mfz = ::SK_MFZ;
	local ox = x - ::SK_MX; local oy = y - ::SK_MY; local oz = z - ::SK_MZ;
	local f = ox * mfx + oy * mfy + oz * mfz;
	local fc = f < -0.25 ? -0.25 : (f > 0.09 ? 0.09 : f);
	local qx = ox - mfx * fc; local qy = oy - mfy * fc; local qz = oz - mfz * fc;
	local d = sqrt(qx * qx + qy * qy + qz * qz) - (0.066 + (fc < 0.0 ? fc * 0.12 : 0.0));
	local hx = ox - mfx * 0.15; local hy = oy - mfy * 0.15; local hz = oz - mfz * 0.15;
	local dh = sqrt(hx * hx + hy * hy + hz * hz) - 0.058;
	d = d < dh ? d : dh;
	if (::SK_WSC > 0.05) {
		local wing = ::sk_wing;
		for (local k = 0; k < 4; ++k) {
			local dw = wing(x, y, z, k);
			d = d < dw ? d : dw;
		}
	}
	local coc = ::SK_COC;
	if (coc > 0.0) {
		local cf = f + 0.07;
		local rr = ox * ox + oy * oy + oz * oz - f * f;
		rr = rr < 0.0 ? 0.0 : sqrt(rr);
		local ca = 0.18 + 0.22 * coc;
		local cb = 0.05 + 0.14 * coc;
		local dc = (sqrt(cf * cf / (ca * ca) + rr * rr / (cb * cb)) - 1.0) * cb;
		d = d < dc ? d : dc;
	}
	return d;
}

// which part of the moth is nearest: -1 body, -2 cocoon, else the wing's index
function sk_moth_part(x, y, z) {
	local sqrt = ::sqrt;
	local mfx = ::SK_MFX; local mfy = ::SK_MFY; local mfz = ::SK_MFZ;
	local ox = x - ::SK_MX; local oy = y - ::SK_MY; local oz = z - ::SK_MZ;
	local f = ox * mfx + oy * mfy + oz * mfz;
	local fc = f < -0.25 ? -0.25 : (f > 0.09 ? 0.09 : f);
	local qx = ox - mfx * fc; local qy = oy - mfy * fc; local qz = oz - mfz * fc;
	local best = sqrt(qx * qx + qy * qy + qz * qz) - (0.066 + (fc < 0.0 ? fc * 0.12 : 0.0));
	local hx = ox - mfx * 0.15; local hy = oy - mfy * 0.15; local hz = oz - mfz * 0.15;
	local dh = sqrt(hx * hx + hy * hy + hz * hz) - 0.058;
	best = best < dh ? best : dh;
	local result = -1;
	if (::SK_WSC > 0.05) {
		for (local k = 0; k < 4; ++k) {
			local dw = ::sk_wing(x, y, z, k);
			if (dw < best) { best = dw; result = k; }
		}
	}
	local coc = ::SK_COC;
	if (coc > 0.0) {
		local cf = f + 0.07;
		local rr = ox * ox + oy * oy + oz * oz - f * f;
		rr = rr < 0.0 ? 0.0 : sqrt(rr);
		local ca = 0.18 + 0.22 * coc;
		local cb = 0.05 + 0.14 * coc;
		local dc = (sqrt(cf * cf / (ca * ca) + rr * rr / (cb * cb)) - 1.0) * cb;
		if (dc < best) { result = -2; }
	}
	return result;
}

// capsules [first, last) against best: a capsule whose bounding sphere cannot beat best is skipped without a root
function sk_capsules(x, y, z, first, last, best) {
	local sqrt = ::sqrt;
	local cmx = ::SK_CMX; local cmy = ::SK_CMY; local cmz = ::SK_CMZ; local cbr = ::SK_CB;
	local cax = ::SK_CAX; local cay = ::SK_CAY; local caz = ::SK_CAZ;
	local cdx = ::SK_CDX; local cdy = ::SK_CDY; local cdz = ::SK_CDZ;
	local cid = ::SK_CID; local cr = ::SK_CR;
	for (local i = first; i < last; ++i) {
		local mx = x - cmx[i];
		local my = y - cmy[i];
		local mz = z - cmz[i];
		local reach = best + cbr[i];
		if (mx * mx + my * my + mz * mz > reach * reach) { continue; }
		local pax = x - cax[i];
		local pay = y - cay[i];
		local paz = z - caz[i];
		local dx = cdx[i];
		local dy = cdy[i];
		local dz = cdz[i];
		local hh = (pax * dx + pay * dy + paz * dz) * cid[i];
		hh = hh < 0.0 ? 0.0 : (hh > 1.0 ? 1.0 : hh);
		local ex = pax - dx * hh;
		local ey = pay - dy * hh;
		local ez = paz - dz * hh;
		local d = sqrt(ex * ex + ey * ey + ez * ez) - cr[i];
		best = d < best ? d : best;
	}
	return best;
}

function sk_glows(x, y, z) {
	local sqrt = ::sqrt;
	local gx = ::SK_GX; local gy = ::SK_GY; local gz = ::SK_GZ; local gr = ::SK_GR;
	local best = 99.0;
	local n = ::SK_NG;
	for (local i = 0; i < n; ++i) {
		local dx = x - gx[i];
		local dy = y - gy[i];
		local dz = z - gz[i];
		local d = sqrt(dx * dx + dy * dy + dz * dz) - gr[i];
		best = d < best ? d : best;
	}
	return best;
}

// the skull against best: an ellipsoid (up 1.18, forward sqrt(0.92)) with its jaw, eye sockets and nose cut in. It is no
// nearer than sqrt(0.92) of the distance to its center less SK_SKB (the jaw's reach), so it is skipped when that cannot beat best
function sk_skull(x, y, z, best) {
	local sqrt = ::sqrt;
	local hx = x - ::SK_HX;
	local hy = y - ::SK_HY;
	local hz = z - ::SK_HZ;
	if (sqrt(hx * hx + hy * hy + hz * hz) * 0.959 - ::SK_SKB >= best) { return best; }
	local prx = ::SK_PRX; local pry = ::SK_PRY; local prz = ::SK_PRZ;
	local cu = hx * ::SK_PUX + hy * ::SK_PUY + hz * ::SK_PUZ;
	local cf = hx * ::SK_PFX + hy * ::SK_PFY + hz * ::SK_PFZ;
	local cr = hx * prx + hy * pry + hz * prz;
	cu = cu * 1.18;
	local d = sqrt(cu * cu + cf * cf * 0.92 + cr * cr) - ::SK_HR;
	local jx = x - ::SK_JX;
	local jy = y - ::SK_JY;
	local jz = z - ::SK_JZ;
	local jr = jx * prx + jy * pry + jz * prz;
	jr = jr < -0.085 ? -0.085 : (jr > 0.085 ? 0.085 : jr);
	local qx = jx - prx * jr;
	local qy = jy - pry * jr;
	local qz = jz - prz * jr;
	local dj = sqrt(qx * qx + qy * qy + qz * qz) - 0.09;
	d = d < dj ? d : dj;
	local gx = ::SK_GX; local gy = ::SK_GY; local gz = ::SK_GZ; local gr = ::SK_GR;
	local nsock = ::SK_NSOCK;
	for (local i = 0; i < nsock; ++i) {
		local sx = x - gx[i];
		local sy = y - gy[i];
		local sz = z - gz[i];
		local c = gr[i] * 2.5 - sqrt(sx * sx + sy * sy + sz * sz);
		d = d > c ? d : c;
	}
	local nx = x - ::SK_NX; local ny = y - ::SK_NY; local nz = z - ::SK_NZ;
	local cn = 0.042 - sqrt(nx * nx + ny * ny + nz * nz);
	d = d > cn ? d : cn;
	return d < best ? d : best;
}

function sk_body(x, y, z) {
	local sqrt = ::sqrt;
	local ax = x - ::SK_AX; local ay = y - ::SK_AY; local az = z - ::SK_AZ;
	local af = ax * ::SK_QFX + ay * ::SK_QFY + az * ::SK_QFZ;
	local au = ax * ::SK_QUX + ay * ::SK_QUY + az * ::SK_QUZ;
	local ar = ax * ::SK_PRX + ay * ::SK_PRY + az * ::SK_PRZ;
	af = af * 0.74;
	au = au * 1.04;
	local da = (sqrt(af * af + au * au + ar * ar) - ::SK_AR) * 0.74;
	local tx = x - ::SK_TX; local ty = y - ::SK_TY; local tz = z - ::SK_TZ;
	local dt = sqrt(tx * tx + ty * ty + tz * tz) - ::SK_TR;
	local k = 0.09;
	local hh = 0.5 + 0.5 * (dt - da) / k;
	hh = hh < 0.0 ? 0.0 : (hh > 1.0 ? 1.0 : hh);
	return dt + (da - dt) * hh - k * hh * (1.0 - hh);
}

// the crown's ring; its flames are the capsules [SK_CROWN0, SK_NC)
function sk_ring(x, y, z) {
	local pux = ::SK_PUX; local puy = ::SK_PUY; local puz = ::SK_PUZ;
	local ox = x - ::SK_CRX; local oy = y - ::SK_CRY; local oz = z - ::SK_CRZ;
	local cu = ox * pux + oy * puy + oz * puz;
	local px = ox - pux * cu; local py = oy - puy * cu; local pz = oz - puz * cu;
	local ring = ::sqrt(px * px + py * py + pz * pz) - 0.2;
	return ::sqrt(ring * ring + cu * cu) - 0.028;
}

function sk_gem(x, y, z) {
	local fabs = ::fabs;
	local ox = x - ::SK_GMX; local oy = y - ::SK_GMY; local oz = z - ::SK_GMZ;
	local a = fabs(ox * ::SK_GAX + oy * ::SK_GAY + oz * ::SK_GAZ);
	local b = fabs(ox * ::SK_PUX + oy * ::SK_PUY + oz * ::SK_PUZ);
	local c = fabs(ox * ::SK_GBX + oy * ::SK_GBY + oz * ::SK_GBZ);
	return (a + b * 0.62 + c - 0.075) * 0.54;
}

// the creature along one ray: only the parts the ray passes near (groups: 1 body, 2 head and its eyes, 4 moth; cand: capsule
// indices, glows as 100 + index).
// A part the ray never enters changes the step lengths, never a hit.
function sk_spider(x, y, z, groups, cand, nc) {
	local sqrt = ::sqrt;
	local d = 99.0;
	if ((groups & 1) != 0) { d = ::sk_body(x, y, z); }
	if ((groups & 2) != 0) {
		d = ::sk_skull(x, y, z, d);
		// the crown's ring and flames lie within SK_CRB of its center
		local ox = x - ::SK_CRX; local oy = y - ::SK_CRY; local oz = z - ::SK_CRZ;
		if (sqrt(ox * ox + oy * oy + oz * oz) - ::SK_CRB < d) {
			local dr = ::sk_ring(x, y, z);
			d = d < dr ? d : dr;
			d = ::sk_capsules(x, y, z, ::SK_CROWN0, ::SK_NC, d);
		}
		local dg = ::sk_gem(x, y, z);
		d = d < dg ? d : dg;
		local gx = ::SK_GX; local gy = ::SK_GY; local gz = ::SK_GZ; local gr = ::SK_GR;
		local nsock = ::SK_NSOCK;
		for (local e = 0; e < nsock; ++e) {
			local ex = x - gx[e];
			local ey = y - gy[e];
			local ez = z - gz[e];
			local de = sqrt(ex * ex + ey * ey + ez * ez) - gr[e];
			d = d < de ? d : de;
		}
	}
	if ((groups & 4) != 0) {
		local dm = ::sk_moth(x, y, z);
		d = d < dm ? d : dm;
	}
	if (nc > 0) {
		local gx = ::SK_GX; local gy = ::SK_GY; local gz = ::SK_GZ; local gr = ::SK_GR;
		local cmx = ::SK_CMX; local cmy = ::SK_CMY; local cmz = ::SK_CMZ; local cbr = ::SK_CB;
		local cax = ::SK_CAX; local cay = ::SK_CAY; local caz = ::SK_CAZ;
		local cdxs = ::SK_CDX; local cdys = ::SK_CDY; local cdzs = ::SK_CDZ;
		local cid = ::SK_CID; local cr = ::SK_CR;
		for (local k = 0; k < nc; ++k) {
			local c = cand[k];
			if (c >= 100) {
				local g = c - 100;
				local gdx = x - gx[g];
				local gdy = y - gy[g];
				local gdz = z - gz[g];
				local dg2 = sqrt(gdx * gdx + gdy * gdy + gdz * gdz) - gr[g];
				d = d < dg2 ? d : dg2;
			} else {
				local mx = x - cmx[c];
				local my = y - cmy[c];
				local mz = z - cmz[c];
				local reach = d + cbr[c];
				if (mx * mx + my * my + mz * mz > reach * reach) { continue; }
				local pax = x - cax[c];
				local pay = y - cay[c];
				local paz = z - caz[c];
				local cdx = cdxs[c];
				local cdy = cdys[c];
				local cdz = cdzs[c];
				local hh = (pax * cdx + pay * cdy + paz * cdz) * cid[c];
				hh = hh < 0.0 ? 0.0 : (hh > 1.0 ? 1.0 : hh);
				local ex = pax - cdx * hh;
				local ey = pay - cdy * hh;
				local ez = paz - cdz * hh;
				local dc = sqrt(ex * ex + ey * ey + ez * ez) - cr[c];
				d = d < dc ? d : dc;
			}
		}
	}
	return d;
}

// the ray's candidate parts cand[first, last) against best: capsule indices in ascending order, then glows as 100 + index
function sk_candidates(x, y, z, cand, first, last, d) {
	local sqrt = ::sqrt;
	local gx = ::SK_GX; local gy = ::SK_GY; local gz = ::SK_GZ; local gr = ::SK_GR;
	local cmx = ::SK_CMX; local cmy = ::SK_CMY; local cmz = ::SK_CMZ; local cbr = ::SK_CB;
	local cax = ::SK_CAX; local cay = ::SK_CAY; local caz = ::SK_CAZ;
	local cdxs = ::SK_CDX; local cdys = ::SK_CDY; local cdzs = ::SK_CDZ;
	local cid = ::SK_CID; local cr = ::SK_CR;
	for (local k = first; k < last; ++k) {
		local c = cand[k];
		if (c >= 100) {
			local g = c - 100;
			local gdx = x - gx[g];
			local gdy = y - gy[g];
			local gdz = z - gz[g];
			local dg2 = sqrt(gdx * gdx + gdy * gdy + gdz * gdz) - gr[g];
			d = d < dg2 ? d : dg2;
		} else {
			local mx = x - cmx[c];
			local my = y - cmy[c];
			local mz = z - cmz[c];
			local reach = d + cbr[c];
			if (mx * mx + my * my + mz * mz > reach * reach) { continue; }
			local pax = x - cax[c];
			local pay = y - cay[c];
			local paz = z - caz[c];
			local cdx = cdxs[c];
			local cdy = cdys[c];
			local cdz = cdzs[c];
			local hh = (pax * cdx + pay * cdy + paz * cdz) * cid[c];
			hh = hh < 0.0 ? 0.0 : (hh > 1.0 ? 1.0 : hh);
			local ex = pax - cdx * hh;
			local ey = pay - cdy * hh;
			local ez = paz - cdz * hh;
			local dc = sqrt(ex * ex + ey * ey + ez * ez) - cr[c];
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

// 1 bone, 2 chitin, 3 glow, 4 marking, 5 ruby, 6 moth at a point the march stopped on: the nearest wins, ties to the earlier.
// A capsule the ray passed no nearer than 0.01 to is farther than the march's reach from every point on it, so only the ray's
// candidates are tried (cand[0, nbone) bone, then chitin up to ncap)
function sk_material(x, y, z, cand, nbone, ncap) {
	local sqrt = ::sqrt;
	local best = 99.0;
	local m = 0;
	local bone = ::sk_candidates(x, y, z, cand, 0, nbone, ::sk_skull(x, y, z, best));
	if (bone < best) { best = bone; m = 1; }
	local db = ::sk_body(x, y, z);
	local chit = ::sk_candidates(x, y, z, cand, nbone, ncap, db < best ? db : best);
	if (chit < best) { best = chit; m = 2; }
	local de = ::sk_glows(x, y, z);
	de = de < best ? de : best;
	local ox = x - ::SK_CRX; local oy = y - ::SK_CRY; local oz = z - ::SK_CRZ;
	if (sqrt(ox * ox + oy * oy + oz * oz) - ::SK_CRB < de) {
		local dr = ::sk_ring(x, y, z);
		de = de < dr ? de : dr;
		de = ::sk_capsules(x, y, z, ::SK_CROWN0, ::SK_NC, de);
	}
	if (de < best) { best = de; m = 3; }
	local dgm = ::sk_gem(x, y, z);
	if (dgm < best) { best = dgm; m = 5; }
	if (::SK_MOTH) {
		local mx = x - ::SK_MX; local my = y - ::SK_MY; local mz = z - ::SK_MZ;
		if (sqrt(mx * mx + my * my + mz * mz) - ::SK_MB < best) {
			local dm = ::sk_moth(x, y, z);
			if (dm < best) { m = 6; }
		}
	}
	if (m == 2 && chit == db) {
		// chevrons crawl down the abdomen's back, a stripe runs along its spine
		local ax = x - ::SK_AX; local ay = y - ::SK_AY; local az = z - ::SK_AZ;
		local au = ax * ::SK_QUX + ay * ::SK_QUY + az * ::SK_QUZ;
		local ar = ax * ::SK_PRX + ay * ::SK_PRY + az * ::SK_PRZ;
		local af = ax * ::SK_QFX + ay * ::SK_QFY + az * ::SK_QFZ;
		local ar2 = ar < 0.0 ? -ar : ar;
		if (au > 0.07 && af > -0.33 && af < 0.2) {
			local v = af * 7.0 - ar2 * 9.0 + ::SK_T * 1.6;
			if (v - ::floor(v) < 0.3 || ar2 < 0.018) { m = 4; }
		}
	}
	return m;
}

// where the ray (origin o, unit direction d) is inside the sphere (c, r): widens span = [t_near, t_far] to hold that stretch
function sk_ray_span(ox, oy, oz, dx, dy, dz, cx, cy, cz, r, span) {
	local vx = cx - ox;
	local vy = cy - oy;
	local vz = cz - oz;
	local tc = vx * dx + vy * dy + vz * dz;
	local h = r * r - (vx * vx + vy * vy + vz * vz - tc * tc);
	if (h <= 0.0) { return; }
	h = ::sqrt(h);
	local t_near = span[0];
	local t_far = span[1];
	span[0] = tc - h < t_near ? tc - h : t_near;
	span[1] = tc + h > t_far ? tc + h : t_far;
}

// a pixel's palette index plus its depth (0..254) above bit 10, for the silk and the embers drawn over it, or -1 where the ray
// meets neither the creature nor the moth; row: the row's candidate parts (see sk_row)
function sk_trace(dx, dy, dz, px, py, cand, row, nrow) {
	local sqrt = ::sqrt;
	local near = ::sk_ray_near;
	local ox = ::SK_EX;
	local oy = ::SK_EY;
	local oz = ::SK_EZ;
	local groups = 0;
	local nc = 0;
	local nbone = 0;
	local ncap = 0;
	if (near(ox, oy, oz, dx, dy, dz, ::SK_BGX, ::SK_BGY, ::SK_BGZ, 0.7)) { groups = 1; }
	if (near(ox, oy, oz, dx, dy, dz, ::SK_HGX, ::SK_HGY, ::SK_HGZ, 0.74)) { groups = groups + 2; }
	if (::SK_MOTH && near(ox, oy, oz, dx, dy, dz, ::SK_MX, ::SK_MY, ::SK_MZ, ::SK_MB)) { groups = groups + 4; }
	local gx = ::SK_GX; local gy = ::SK_GY; local gz = ::SK_GZ; local gr = ::SK_GR;
	if (nrow > 0) {
		local cmx = ::SK_CMX; local cmy = ::SK_CMY; local cmz = ::SK_CMZ; local cbr = ::SK_CB;
		local nbones = ::SK_NBONE;
		for (local k = 0; k < nrow; ++k) {
			local c = row[k];
			if (c < 100) {
				if (near(ox, oy, oz, dx, dy, dz, cmx[c], cmy[c], cmz[c], cbr[c] + 0.01)) {
					cand[nc] = c;
					nc = nc + 1;
					ncap = nc;
					if (c < nbones) { nbone = nc; }
				}
			} else {
				local g = c - 100;
				if (near(ox, oy, oz, dx, dy, dz, gx[g], gy[g], gz[g], gr[g] + 0.01)) {
					cand[nc] = c;
					nc = nc + 1;
				}
			}
		}
	}
	if (groups == 0 && nc == 0) { return -1; }
	local span = [99.0, 0.0];
	if ((groups & 3) != 0 || nc != 0) { ::sk_ray_span(ox, oy, oz, dx, dy, dz, ::SK_BCX, ::SK_BCY, ::SK_BCZ, ::SK_BR, span); }
	if ((groups & 4) != 0) { ::sk_ray_span(ox, oy, oz, dx, dy, dz, ::SK_MX, ::SK_MY, ::SK_MZ, ::SK_MB, span); }
	local t = span[0];
	local t_far = span[1];
	t = t < 0.04 ? 0.04 : t;
	local hit = false;
	local i = 0;
	local spider = ::sk_spider;
	while (i < 48 && t < t_far) {
		local d = spider(ox + dx * t, oy + dy * t, oz + dz * t, groups, cand, nc);
		local reach = 0.001 + t * 0.0015;
		if (d < (reach < 0.0085 ? reach : 0.0085)) {
			hit = true;
			break;
		}
		t += d * 0.85;
		i = i + 1;
	}
	if (!hit) { return -1; }
	local dith = ::SK_BAYER[(py % 4) * 4 + px % 4] / 16.0 - 0.47;
	local depth = (t * 18.0).tointeger();
	depth = (depth > 254 ? 254 : depth) * 1024;
	local x = ox + dx * t;
	local y = oy + dy * t;
	local z = oz + dz * t;
	local m = ::sk_material(x, y, z, cand, nbone, ncap);
	local fog = 1.0 - t / 14.0;
	fog = fog < 0.0 ? 0.0 : fog;
	local lx = ::SK_LX - x;
	local ly = ::SK_LY - y;
	local lz = ::SK_LZ - z;
	local ld2 = lx * lx + ly * ly + lz * lz;
	local lil = 1.0 / sqrt(ld2 + 0.0001);
	lx *= lil; ly *= lil; lz *= lil;
	local flick = ::SK_FLICK;
	local wake = ::SK_WAKE;
	local kx = ::SK_KX; local ky = ::SK_KY; local kz = ::SK_KZ;
	if (m == 3) {
		local e = ((5.4 + flick) * wake + dith).tointeger();
		local nsock = ::SK_NSOCK;
		for (local k = 0; k < nsock; ++k) {
			local ex = x - gx[k]; local ey = y - gy[k]; local ez = z - gz[k];
			local el = sqrt(ex * ex + ey * ey + ez * ez);
			if (el < gr[k] + 0.012) {
				// an eye burns red at its rim and yellow where it faces the viewer, with a
				// wet glint where its sphere mirrors the key light
				local facing = -(ex * dx + ey * dy + ez * dz) / (el + 0.0000001);
				e = ((2.6 + 3.2 * facing * facing + flick) * wake + dith).tointeger();
				local hx = kx - dx; local hy = ky - dy; local hz = kz - dz;
				local hl = sqrt(hx * hx + hy * hy + hz * hz) * el + 0.0000001;
				if ((ex * hx + ey * hy + ez * hz) / hl > 0.93 && wake > 0.5) { e = 7; }
			}
		}
		return ::SK_EYE + (e < 0 ? 0 : (e > 7 ? 7 : e)) + depth;
	}
	local pux = ::SK_PUX; local puy = ::SK_PUY; local puz = ::SK_PUZ;
	if (m == 5) {
		local gax = ::SK_GAX; local gay = ::SK_GAY; local gaz = ::SK_GAZ;
		local gbx = ::SK_GBX; local gby = ::SK_GBY; local gbz = ::SK_GBZ;
		local gox = x - ::SK_GMX; local goy = y - ::SK_GMY; local goz = z - ::SK_GMZ;
		local a = gox * gax + goy * gay + goz * gaz;
		local b = gox * pux + goy * puy + goz * puz;
		local c = gox * gbx + goy * gby + goz * gbz;
		local sa = a < 0.0 ? -1.0 : 1.0;
		local sb = b < 0.0 ? -1.0 : 1.0;
		local sc = c < 0.0 ? -1.0 : 1.0;
		local gnx = gax * sa + pux * sb * 0.62 + gbx * sc;
		local gny = gay * sa + puy * sb * 0.62 + gby * sc;
		local gnz = gaz * sa + puz * sb * 0.62 + gbz * sc;
		local gl = 1.0 / sqrt(gnx * gnx + gny * gny + gnz * gnz);
		local face = -(gnx * dx + gny * dy + gnz * dz) * gl;
		face = face < 0.0 ? 0.0 : face;
		local facet = (sa > 0.0 ? 1 : 0) + (sb > 0.0 ? 2 : 0) + (sc > 0.0 ? 4 : 0);
		local twinkle = ::sin(::SK_T * 9.0 + facet * 2.3);
		local lit = 0.25 + face * face * 0.6 + (twinkle > 0.85 ? 0.6 : 0.0);
		local r = (lit * 7.0 + dith + 0.5).tointeger();
		return ::SK_GEM + (r < 0 ? 0 : (r > 7 ? 7 : r)) + depth;
	}
	local hstep = 0.0015;
	local n0 = spider(x + hstep, y - hstep, z - hstep, groups, cand, nc);
	local n1 = spider(x - hstep, y - hstep, z + hstep, groups, cand, nc);
	local n2 = spider(x - hstep, y + hstep, z - hstep, groups, cand, nc);
	local n3 = spider(x + hstep, y + hstep, z + hstep, groups, cand, nc);
	local nx = n0 - n1 - n2 + n3;
	local ny = -n0 - n1 + n2 + n3;
	local nz = -n0 + n1 - n2 + n3;
	local nl = 1.0 / sqrt(nx * nx + ny * ny + nz * nz + 0.0000001);
	nx *= nl; ny *= nl; nz *= nl;
	local vd = -(nx * dx + ny * dy + nz * dz);
	vd = vd < 0.0 ? 0.0 : vd;
	local up = nx * pux + ny * puy + nz * puz;
	up = up < 0.0 ? 0.0 : up;
	local rim = 1.0 - vd;
	rim = rim * rim * rim;
	local eyel = nx * lx + ny * ly + nz * lz;
	eyel = eyel < 0.0 ? 0.0 : eyel / (1.0 + ld2 * 6.0);
	local key = nx * kx + ny * ky + nz * kz;
	if (m == 6) {
		local part = ::sk_moth_part(x, y, z);
		if (part >= 0) {
			// the wings glow through: lit from either side
			local tl = 0.5 + 0.55 * (key < 0.0 ? -key : key) + eyel * 0.6;
			local tone = ::sk_wing_tone(x, y, z, part);
			if (tone == 3 && part >= 2) { tone = 6; }
			local l = (tl * fog * 7.0 + dith + 0.5).tointeger();
			return ::SK_WING + tone * 8 + (l < 0 ? 0 : (l > 7 ? 7 : l)) + depth;
		}
		if (part == -2) {
			// the cocoon: silk wound in bands that turn as the spider spins it
			local ox2 = x - ::SK_MX; local oy2 = y - ::SK_MY; local oz2 = z - ::SK_MZ;
			local f = ox2 * ::SK_MFX + oy2 * ::SK_MFY + oz2 * ::SK_MFZ;
			local ang = ::atan2(ox2 * ::SK_MUX + oy2 * ::SK_MUY + oz2 * ::SK_MUZ, ox2 * ::SK_MRX + oy2 * ::SK_MRY + oz2 * ::SK_MRZ);
			local band = ::sin(f * 70.0 + ang * 2.0 + ::SK_SPIN * 2.0);
			local cl = (0.2 + vd * 0.45 + rim * 0.35 + (key > 0.0 ? key * 0.25 : 0.0)) * (0.72 + 0.28 * band);
			local l = (cl * fog * 9.0 + dith + 0.5).tointeger();
			return ::SK_COCOON + (l < 0 ? 0 : (l > 9 ? 9 : l)) + depth;
		}
		local fl = 0.12 + vd * 0.55 + rim * 0.25 + eyel * 0.5;
		local l = (fl * fog * 7.0 + dith * 1.6 + 0.5).tointeger();
		return ::SK_FUR + (l < 0 ? 0 : (l > 7 ? 7 : l)) + depth;
	}
	if (m == 1) {
		local lit = 0.1 + vd * (0.55 + ::SK_GLARE * 0.4) + up * 0.22 + rim * 0.25;
		local so = 99.0;
		local nsock = ::SK_NSOCK;
		for (local k = 0; k < nsock; ++k) {
			local ox2 = x - gx[k]; local oy2 = y - gy[k]; local oz2 = z - gz[k];
			local q = sqrt(ox2 * ox2 + oy2 * oy2 + oz2 * oz2) - (gr[k] < 0.03 ? gr[k] : 0.03) * 2.5;
			so = q < so ? q : so;
		}
		if (so < 0.012) { lit = lit * 0.15 + 0.07 * (1.0 + flick); }
		local nsx = x - ::SK_NX; local nsy = y - ::SK_NY; local nsz = z - ::SK_NZ;
		if (nsx * nsx + nsy * nsy + nsz * nsz < 0.0025) { lit = lit * 0.2; }
		local jx = x - ::SK_JX; local jy = y - ::SK_JY; local jz = z - ::SK_JZ;
		local jf = jx * ::SK_PFX + jy * ::SK_PFY + jz * ::SK_PFZ;
		local ju = jx * pux + jy * puy + jz * puz;
		if (jf > 0.045 && ju > -0.05 && ju < 0.042) {
			local tr = (jx * ::SK_PRX + jy * ::SK_PRY + jz * ::SK_PRZ) * 44.0;
			if (tr - ::floor(tr) < 0.24 || (ju > -0.008 && ju < 0.006)) { lit = lit * 0.22; }
		}
		local b = (lit * fog * 11.0 + dith + 0.5).tointeger();
		return ::SK_BONE + (b < 0 ? 0 : (b > 11 ? 11 : b)) + depth;
	}
	// a glossy highlight of the key light
	local hx = kx - dx; local hy = ky - dy; local hz = kz - dz;
	local hl = 1.0 / sqrt(hx * hx + hy * hy + hz * hz + 0.0000001);
	local spec = (nx * hx + ny * hy + nz * hz) * hl;
	spec = spec < 0.0 ? 0.0 : spec;
	spec = spec * spec; spec = spec * spec; spec = spec * spec; spec = spec * spec;
	if (m == 4) {
		local k = ((0.45 + vd * 0.45 + 0.3 * ::sin(::SK_T * 4.0) + spec * 0.6) * 5.0 + dith + 0.5).tointeger();
		return ::SK_MARK + (k < 0 ? 0 : (k > 5 ? 5 : k)) + depth;
	}
	// beetle iridescence: the shell turns from wine through violet to teal toward its rim
	local lit = 0.1 + vd * 0.42 + rim * 0.45 + eyel * 0.9 + (key > 0.0 ? key * 0.3 : 0.0) + spec * 1.1;
	local band = vd > 0.68 ? 0 : (vd > 0.36 ? 1 : 2);
	local c = (lit * fog * 11.0 + dith + 0.5).tointeger();
	return ::SK_CHIT + band * 12 + (c < 0 ? 0 : (c > 11 ? 11 : c)) + depth;
}

// the vault behind the web at every fourth traced pixel, as light and the creature's warm aura: a ribbed dome about the hub
// with a pale oculus at its crown, a ring of pillars nearer the eye, mist drifting across, a faint ring leaving on each kick
function sk_backdrop(dim, flash) {
	local sqrt = ::sqrt;
	local sin = ::sin;
	local cos = ::cos;
	local atan2 = ::atan2;
	local fabs = ::fabs;
	local sw = ::SK_W;
	local sh = ::SK_H;
	local cols = sw / 4 + 2;
	local rows = sh / 4 + 2;
	::SK_BACK_COLS = cols;
	local back_light = ::SK_BACK_LIGHT;
	local back_warm = ::SK_BACK_WARM;
	back_light.resize(cols * rows, 0.0);
	back_warm.resize(cols * rows, 0.0);
	local ex = ::SK_EX; local ey = ::SK_EY; local ez = ::SK_EZ;
	local fx = ::SK_FX; local fy = ::SK_FY; local fz = ::SK_FZ;
	local rx = ::SK_RX; local ry = ::SK_RY; local rz = ::SK_RZ;
	local ux = ::SK_UX; local uy = ::SK_UY; local uz = ::SK_UZ;
	local st = ::SK_T;
	local kage = ::SK_KAGE;
	local wake = ::SK_WAKE;
	local zoom = ::SK_ZOOM / (sh * 0.5);
	local ee = ex * ex + ey * ey + ez * ez;
	local exz = ex * ex + ez * ez - 21.16;
	local vx = ::SK_BCX - ex;
	local vy = ::SK_BCY - ey;
	local vz = ::SK_BCZ - ez;
	local vv = vx * vx + vy * vy + vz * vz;
	local ring = kage * 1.8;
	local ring_gain = kage < 0.7 ? (0.7 - kage) * 0.16 * wake : 0.0;
	local aura_gain = wake * (0.75 + 0.25 * ::SK_KICK);
	for (local j = 0; j < rows; ++j) {
		local v = (sh * 0.5 - j * 4 - 0.5) * zoom;
		for (local i = 0; i < cols; ++i) {
			local u = (i * 4 + 0.5 - sw * 0.5) * zoom;
			local dx = fx + rx * u + ux * v;
			local dy = fy + ry * u + uy * v;
			local dz = fz + rz * u + uz * v;
			local il = 1.0 / sqrt(dx * dx + dy * dy + dz * dz);
			dx *= il; dy *= il; dz *= il;
			local ed = ex * dx + ey * dy + ez * dz;
			local t = (sqrt(ed * ed - ee + 49.0) - ed) / 7.0;
			local qx = ex / 7.0 + dx * t;
			local qy = ey / 7.0 + dy * t;
			local qz = ez / 7.0 + dz * t;
			local phi = atan2(qx, -qz);
			local rib = fabs(sin(phi * 4.0));
			rib = rib < 0.25 ? 1.0 - rib * 4.0 : 0.0;
			local light = 0.2 + 0.14 * qy + rib * (0.06 + 0.06 * qy);
			local oculus = (qy - 0.8) * 6.0;
			light += (oculus < 0.0 ? 0.0 : (oculus > 1.0 ? 1.0 : oculus)) * 0.3;
			light += 0.05 * sin(phi * 3.0 + qy * 4.0 + st * 0.25) * sin(qy * 6.0 - st * 0.17 + phi);
			local off = fabs(sqrt(qx * qx + qy * qy) - ring);
			if (off < 0.12) { light += (1.0 - off / 0.12) * ring_gain; }
			local a = dx * dx + dz * dz;
			local b = ex * dx + ez * dz;
			local disc = b * b - a * exz;
			if (disc > 0.0 && a > 0.000001) {
				local tp = (sqrt(disc) - b) / a;
				local gap = fabs(cos(atan2(ex + dx * tp, -ez - dz * tp) * 4.0));
				if (gap < 0.5) { light *= 0.45 + gap * 1.1; }
			}
			light = light * dim + flash;
			local tc = vx * dx + vy * dy + vz * dz;
			local perp = vv - tc * tc;
			local warm = 1.3 - sqrt(perp < 0.0 ? 0.0 : perp);
			warm = warm < 0.0 ? 0.0 : (warm > 1.0 ? 1.0 : warm);
			back_light[j * cols + i] = light < 0.0 ? 0.0 : (light > 1.0 ? 1.0 : light);
			back_warm[j * cols + i] = warm * warm * aura_gain;
		}
	}
}

// one traced row of the vault, from the grid sk_backdrop fills
function sk_backdrop_row(y, out) {
	local back_light = ::SK_BACK_LIGHT;
	local back_warm = ::SK_BACK_WARM;
	local bayers = ::SK_BAYER;
	local j = y / 4;
	local fy = (y - j * 4) * 0.25;
	local upper = j * ::SK_BACK_COLS;
	local lower = upper + ::SK_BACK_COLS;
	local bayer = (y % 4) * 4;
	// the original's "base": a keyword in Squirrel
	local base_ink = ::SK_BACK + ::SK_BACK_HUE + 255 * 1024;
	local ww = ::SK_W;
	local x = 0;
	local i = 0;
	while (x < ww) {
		local l0 = back_light[upper + i];
		local l1 = back_light[upper + i + 1];
		local w0 = back_warm[upper + i];
		local w1 = back_warm[upper + i + 1];
		l0 += (back_light[lower + i] - l0) * fy;
		l1 += (back_light[lower + i + 1] - l1) * fy;
		w0 += (back_warm[lower + i] - w0) * fy;
		w1 += (back_warm[lower + i + 1] - w1) * fy;
		for (local k = 0; k < 4 && x < ww; ++k) {
			local dith = bayers[bayer + k] / 16.0;
			local level = ((l0 + (l1 - l0) * k * 0.25) * 15.0 + dith).tointeger();
			local warm = ((w0 + (w1 - w0) * k * 0.25) * 4.0 + dith).tointeger();
			out[x] = base_ink + (warm > 4 ? 4 : warm) * 16 + (level > 15 ? 15 : level);
			x = x + 1;
		}
		i = i + 1;
	}
}

// the pixels span = [first, last) of a row whose rays (A + R u, A = F + U v) pass within r of c: widens the span to hold them
function sk_row_span(ax, ay, az, cx, cy, cz, r, zoom, span) {
	local vx = cx - ::SK_EX;
	local vy = cy - ::SK_EY;
	local vz = cz - ::SK_EZ;
	local q = vx * vx + vy * vy + vz * vz - r * r;
	local a = vx * ax + vy * ay + vz * az;
	local b = vx * ::SK_RX + vy * ::SK_RY + vz * ::SK_RZ;
	local k = q - b * b;
	if (k < 0.0001) {
		span[0] = 0;
		span[1] = ::SK_W;
		return;
	}
	local disc = a * a * b * b - k * (q * (ax * ax + ay * ay + az * az) - a * a);
	if (disc <= 0.0 || a <= 0.0) { return; }
	disc = ::sqrt(disc);
	local lo = ::floor((a * b - disc) / (k * zoom) + ::SK_W * 0.5).tointeger() - 1;
	local hi = ::floor((a * b + disc) / (k * zoom) + ::SK_W * 0.5).tointeger() + 2;
	local first = span[0];
	local last = span[1];
	span[0] = lo < first ? lo : first;
	span[1] = hi > last ? hi : last;
}

function sk_row(y) {
	local sqrt = ::sqrt;
	local fabs = ::fabs;
	local out = ::array(::SK_W, 0);
	::sk_backdrop_row(y, out);
	local ww = ::SK_W;
	local half_h = ::SK_H * 0.5;
	local zoom = ::SK_ZOOM / half_h;
	local v = (half_h - y - 0.5) * zoom;
	local ax = ::SK_FX + ::SK_UX * v;
	local ay = ::SK_FY + ::SK_UY * v;
	local az = ::SK_FZ + ::SK_UZ * v;
	local span = [ww, 0];
	::sk_row_span(ax, ay, az, ::SK_BCX, ::SK_BCY, ::SK_BCZ, ::SK_BR, zoom, span);
	if (::SK_MOTH) { ::sk_row_span(ax, ay, az, ::SK_MX, ::SK_MY, ::SK_MZ, ::SK_MB, zoom, span); }
	local first = span[0];
	local last = span[1];
	first = first < 0 ? 0 : first;
	last = last > ww ? ww : last;
	if (first >= last) { return out; }
	// the row's rays all lie in the plane through the eye spanned by R and F + U v: a capsule (0..SK_CROWN0) or a leg glow
	// (100 + g) farther from that plane than its reach passes near none of them, so the pixels test only the rest
	local rx = ::SK_RX; local ry = ::SK_RY; local rz = ::SK_RZ;
	local ex = ::SK_EX; local ey = ::SK_EY; local ez = ::SK_EZ;
	local nx = ry * az - rz * ay;
	local ny = rz * ax - rx * az;
	local nz = rx * ay - ry * ax;
	local nl = 1.0 / sqrt(nx * nx + ny * ny + nz * nz);
	local row = [];
	local cmx = ::SK_CMX; local cmy = ::SK_CMY; local cmz = ::SK_CMZ; local cbr = ::SK_CB;
	local crown0 = ::SK_CROWN0;
	for (local c = 0; c < crown0; ++c) {
		local off = ((cmx[c] - ex) * nx + (cmy[c] - ey) * ny + (cmz[c] - ez) * nz) * nl;
		if (fabs(off) < cbr[c] + 0.02) { row.push(c); }
	}
	local gx = ::SK_GX; local gy = ::SK_GY; local gz = ::SK_GZ; local gr = ::SK_GR;
	local ng = ::SK_NG;
	for (local g = 3; g < ng; ++g) {
		local off = ((gx[g] - ex) * nx + (gy[g] - ey) * ny + (gz[g] - ez) * nz) * nl;
		if (fabs(off) < gr[g] + 0.02) { row.push(100 + g); }
	}
	local nrow = row.len();
	local cand = ::array(64, 0);
	local trace = ::sk_trace;
	for (local x = first; x < last; ++x) {
		local u = (x + 0.5 - ww * 0.5) * zoom;
		local dx = ax + rx * u;
		local dy = ay + ry * u;
		local dz = az + rz * u;
		local il = 1.0 / sqrt(dx * dx + dy * dy + dz * dz);
		local traced = trace(dx * il, dy * il, dz * il, x, y, cand, row, nrow);
		if (traced >= 0) { out[x] = traced; }
	}
	return out;
}

class Ossuary extends ::Scene {
	bx = 0.0; by = 0.0; bz = 0.0;
	fx = 0.0; fy = 1.0; fz = 0.0;
	ux = 0.0; uy = 0.0; uz = 1.0;
	rx = 1.0; ry = 0.0; rz = 0.0;
	ex = 0.0; ey = 0.0; ez = 0.0;
	cux = 0.0; cuy = 1.0; cuz = 0.0;
	lkx = 0.0; lky = 0.0; lkz = 0.0;
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
	leglift = null;
	ppx = null;
	ppy = null;
	ppz = null;
	pvx = null;
	pvy = null;
	pvz = null;
	plife = null;
	prate = null;
	depth = null;
	np = 120;
	pnext = 0;
	l1 = 0.5;
	l2 = 0.52;
	tt = 0.0;
	spawn_acc = 0.0;
	stance = 0.5;

	// the story on the cue's bars: asleep at the hub (0-1), awake on bar 2, the spokes thrown and the spiral run by bar 4,
	// the moth strikes on bar 6, the cocoon closes on bar 8, the hunter stalks the viewer through bar 9 and lunges on bar 10
	t_wake = 3.871;
	t_spokes = 4.355;
	t_spoke_gap = 0.0806;
	t_out = 5.5;
	t_spiral0 = 5.806;
	t_spiral1 = 7.742;
	t_home = 8.25;
	t_moth = 8.7;
	t_impact = 11.613;
	t_rush = 11.85;
	t_reach = 12.5;
	t_turn = 12.85;
	t_wrap0 = 12.95;
	t_wrap1 = 15.484;
	t_face = 15.7;
	t_stalk = 17.419;
	t_lunge = 19.355;
	nspoke = 14;
	spoke_reach = 4.4;
	web_rim = 1.55;
	lay_from = 2.24;
	lay_lead = 0.7;
	ring_speed = 3.0;
	spoke_ang = null;
	spa = null;
	spb = null;
	lay_turn = null;
	pa = 0.0; pb = 0.0; psi = 1.5707963;
	spd = 0.0;
	moth_a = 1.2; moth_b = 0.7;
	qa = 0.0; qb = 0.0;
	wx = 0.0; wy = 0.0; wz = 0.0;
	qsx = 0.0; qsy = 0.0; qzc = 0.0;
	mox = 0.0; moy = 0.0; moz = 0.0;
	mfx = 0.0; mfy = 1.0; mfz = 0.0;
	mux = 0.0; muy = 0.0; muz = 1.0;
	mrx = 1.0; mry = 0.0; mrz = 0.0;
	flap = 0.0; wsc = 1.0; coc = 0.0; spin = 0.0;

	constructor() {
		pixel_budget = 16000;
		footx = [];
		footy = [];
		footz = [];
		fromx = [];
		fromy = [];
		fromz = [];
		tox = [];
		toy = [];
		toz = [];
		stept = [];
		legfo = [0.3, 0.1, -0.1, -0.3];
		legreach = [0.42, 0.14, -0.08, -0.36];
		leglift = [1.25, 0.8, 0.25, 0.0];
		ppx = [];
		ppy = [];
		ppz = [];
		pvx = [];
		pvy = [];
		pvz = [];
		plife = [];
		prate = [];
		depth = [];
		spoke_ang = [];
		spa = [];
		spb = [];
		lay_turn = [];
	}

	function build() {
		name = "OSSUARY";
		handle = "eight legs of inverse kinematics . an orb web . a moth that came too close";
		replay = 23.5;
		// the vault: three chord hues, each five steps of the creature's warm aura by sixteen levels of light
		::SK_BACK = pal.size();
		local vault = [52.0, 60.0, 112.0, 72.0, 52.0, 110.0, 40.0, 72.0, 104.0];
		for (local hue = 0; hue < 3; ++hue) {
			for (local warm = 0; warm < 5; ++warm) {
				for (local level = 0; level < 16; ++level) {
					local f = level / 15.0;
					pal.add(4.0 + vault[hue * 3] * f + warm * 16.0, 4.0 + vault[hue * 3 + 1] * f + warm * 2.0, 9.0 + vault[hue * 3 + 2] * f + warm * 4.0);
				}
			}
		}
		::SK_BONE = pal.ramp(12, 26.0, 20.0, 16.0, 250.0, 238.0, 206.0);
		::SK_CHIT = pal.size();
		local shell = [235.0, 40.0, 90.0, 150.0, 70.0, 255.0, 40.0, 230.0, 210.0];
		for (local band = 0; band < 3; ++band) {
			for (local k = 0; k < 12; ++k) {
				local f = k / 11.0;
				local c = f * f * 0.85 + f * 0.15;
				local wh = f > 0.8 ? (f - 0.8) * 3.5 : 0.0;
				local r = 5.0 + shell[band * 3] * c;
				local g = 2.0 + shell[band * 3 + 1] * c;
				local b = 7.0 + shell[band * 3 + 2] * c;
				pal.add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh);
			}
		}
		// the eyes and the crown: three steps of dead ember, four of fire, then white
		::SK_EYE = pal.size();
		pal.add(34.0, 12.0, 10.0);
		pal.add(96.0, 16.0, 8.0);
		pal.add(160.0, 22.0, 8.0);
		pal.ramp(4, 220.0, 30.0, 10.0, 255.0, 250.0, 190.0);
		pal.add(255.0, 255.0, 255.0);
		::SK_MARK = pal.ramp(6, 90.0, 0.0, 8.0, 255.0, 200.0, 60.0);
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
		// the moth's wing tones: gold, umber, the eyespot's black ring, cyan iris, pupil, violet margin, magenta iris
		::SK_WING = pal.size();
		local tones = [255.0, 186.0, 64.0, 120.0, 58.0, 28.0, 34.0, 10.0, 34.0, 60.0, 235.0, 255.0, 255.0, 245.0, 255.0, 160.0, 70.0, 230.0, 255.0, 50.0, 200.0];
		for (local tone = 0; tone < 7; ++tone) {
			for (local l = 0; l < 8; ++l) {
				local f = (l + 1) / 8.0;
				local k = 0.1 + 0.9 * f;
				pal.add(tones[tone * 3] * k, tones[tone * 3 + 1] * k, tones[tone * 3 + 2] * k);
			}
		}
		::SK_FUR = pal.ramp(8, 22.0, 12.0, 8.0, 236.0, 196.0, 150.0);
		::SK_COCOON = pal.gradient(10, [[16.0, 12.0, 28.0], [110.0, 100.0, 150.0], [230.0, 226.0, 248.0], [255.0, 255.0, 255.0]]);
		// silk: eight iridescent hues, six levels each
		::SK_SILK = pal.size();
		for (local hq = 0; hq < 8; ++hq) {
			local c = ::gfx.rainbow_rgb(hq * 0.7854 + 0.3);
			local cr = c[0] * 0.12 + 196.0;
			local cg = c[1] * 0.12 + 204.0;
			local cb = c[2] * 0.12 + 222.0;
			for (local l = 0; l < 6; ++l) {
				local f = 0.14 + 0.86 * (l + 1) / 6.0;
				pal.add(cr * f, cg * f, cb * f);
			}
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
		plan_web();
	}

	function length_bars() { return 12; }

	function hit_times() { return [3.871, 7.742, 11.613, 15.484, 19.355]; }
	function hit_levels() { return [1, 1, 2, 1, 2]; }

	// 0 until the hunter turns from the cocoon to the viewer, then 1
	function facing(t) { return smooth((t - t_face) / 1.2); }
	// 0 before the stalk, rising through bar 9 to 1 at the lunge
	function stalk(t) { return smooth((t - t_stalk) / (t_lunge - t_stalk)); }
	// the lunge itself, 1 from just after bar 10's downbeat on
	function lunge(t) { return smooth((t - t_lunge + 0.06) / 0.22); }
	// the flare of the waking: 1 on bar 2's downbeat, dying away
	function flare(t) { return t < t_wake ? 0.0 : ::exp((t_wake - t) * 2.5); }
	// how wide the jaw gapes: a yawn on waking, the lunge, and a snap on every kick of the close-up
	function gape(t) {
		local result = 0.6 * flare(t);
		local s = t - t_lunge;
		if (s > -0.1) { result = ::exp(0.0 - (s > 0.0 ? s : 0.0) * 2.2) + 0.35 * ::SK_KICK; }
		return result > 1.2 ? 1.2 : result;
	}

	// the music the scene answers: the last kick's age and envelope, the chord's hue in the vault, the hits in the lens
	function hear(t) {
		if (cue == null) { return; }
		local k = cue.kick.last(t / ::MUSIC_STEP + 0.0001);
		::SK_KAGE = k < 0 ? 9.0 : t - cue.kick.at[k] * ::MUSIC_STEP;
		::SK_KICK = cue.kick_env(t);
		::SK_BACK_HUE = cue.chord(t) % 3 * 80;
		local punch = cue.hit(t);
		::SK_PUNCH = punch * punch;
		::SK_ZOOM = 0.5 * (1.0 - ::SK_PUNCH * 0.12 - ::SK_KICK * 0.015 * ::SK_WAKE);
	}

	function smooth(u) {
		local c = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
		return c * c * (3.0 - 2.0 * c);
	}

	function turn_toward(from, to, w) {
		local d = to - from;
		d = d - 6.2831853 * ::floor((d + 3.1415927) / 6.2831853);
		return from + d * w;
	}

	// where a foot wants to stand: on the web about the body, the front pairs raised off it at the viewer in the stalk and the leap
	function rest_foot(i, lead) {
		local side = i < 4 ? 1.0 : -1.0;
		local j = i % 4;
		local reach = (legreach[j] + lead) * stance;
		local span = side * 0.78 * stance;
		tox[i] = bx + fx * reach + rx * span;
		toy[i] = by + fy * reach + ry * span;
		toz[i] = bz * leglift[j] + stalk(tt) * (j == 0 ? 0.3 : 0.0);
	}

	function jitter(n) { return (rng.next(n * 2 + 1) - n) / (n * 1.0); }

	// the web stands in the plane z = 0 about the origin, facing +z: the spokes get their angles and the capture spiral its
	// polygon, one vertex where it crosses each spoke, winding in from the rim; lay_turn is how far round from lay_from,
	// in turns, the edge into each vertex begins
	function plan_web() {
		for (local k = 0; k < nspoke; ++k) {
			local jit = k == nspoke / 2 ? 0.0 : jitter(100) * 0.09;
			spoke_ang.push(-1.5707963 + k * 6.2831853 / nspoke + jit);
		}
		local n = 4 * nspoke + nspoke / 2 + 1;
		for (local i = 0; i < n; ++i) {
			local r = web_rim - (web_rim - 0.5) * i / (n - 1.0);
			local a = spoke_ang[i % nspoke] + 6.2831853 * (i / nspoke);
			spa.push(r * ::cos(a));
			spb.push(r * ::sin(a));
			local turn = (spoke_ang[(i + nspoke - 1) % nspoke] - lay_from) / 6.2831853;
			lay_turn.push(turn - ::floor(turn));
		}
	}

	// how far round the web the spiral is laid at time t, in turns: every winding grows at once behind the running
	// spider, and the lap closes, a spoke past full circle, on bar 4
	function laid(t) { return (t - t_spiral0) / (t_spiral1 - t_spiral0) * (1.0 + 1.0 / nspoke); }

	// the spiral's run at time t, into qa, qb: out from the hub, round with the spinnerets trailing over the laid edge, home
	function lap_point(t) {
		local r = smooth((t - t_out) / 0.5) * (1.0 - smooth((t - t_spiral1) / (t_home - t_spiral1)));
		local a = lay_from + lay_lead + 6.2831853 * laid(t);
		qa = r * ::cos(a);
		qb = r * ::sin(a);
	}

	function lap_heading(t) {
		lap_point(t - 0.04);
		local a = qa;
		local b = qb;
		lap_point(t + 0.04);
		return ::atan2(qb - b, qa - a);
	}

	// where it is on the web (pa, pb) and which way it faces (psi), at time t
	function plane_path(t) {
		if (t < t_out) {
			pa = 0.0; pb = 0.0;
			psi = 1.5707963;
			return;
		}
		if (t < t_home) {
			psi = turn_toward(1.5707963, lap_heading(t), smooth((t - t_out) / 0.15));
			lap_point(t);
			pa = qa; pb = qb;
			return;
		}
		local ml = ::sqrt(moth_a * moth_a + moth_b * moth_b);
		local da = moth_a / ml; local db = moth_b / ml;
		local toward = ::atan2(db, da);
		if (t < t_rush) {
			pa = 0.0; pb = 0.0;
			psi = turn_toward(lap_heading(t_home - 0.05), 1.5707963, smooth((t - t_home) / 0.6));
			if (t > t_impact) { psi = turn_toward(psi, toward, smooth((t - t_impact) / (t_rush - t_impact))); }
			return;
		}
		local reach = ml - 0.85;
		if (t < t_reach) {
			reach = reach * smooth((t - t_rush) / (t_reach - t_rush));
			psi = toward;
		} else if (t < t_turn) {
			local e = smooth((t - t_reach) / (t_turn - t_reach));
			reach = reach - 0.11 * e;
			psi = toward + 3.1415927 * e;
		} else {
			// it works the cocoon, then turns head-down and prowls across the hub at the viewer below
			local busy = 1.0 - smooth((t - t_wrap1 + 0.3) / 0.3);
			local prowl = smooth((t - t_face - 0.3) / (t_lunge - t_face - 0.3));
			reach = reach - 0.11 + 0.05 * ::sin(t * 8.0) * busy;
			psi = turn_toward(toward + 3.1415927 + 0.08 * ::sin(t * 5.0) * busy, -1.5707963, facing(t));
			pa = da * reach * (1.0 - prowl);
			pb = db * reach * (1.0 - prowl) - 0.35 * prowl;
			return;
		}
		pa = da * reach; pb = db * reach;
	}

	function web_motion(t, dt) {
		local opa = pa; local opb = pb;
		plane_path(t);
		fx = ::cos(psi); fy = ::sin(psi); fz = 0.0;
		rx = -fy; ry = fx; rz = 0.0;
		bx = pa; by = pb; bz = 0.0;
		local ma = pa - opa; local mb = pb - opb;
		spd = dt > 0.0 ? ::sqrt(ma * ma + mb * mb) / dt : 0.0;
	}

	// how bright the strike's ring makes the silk at (a, b): a front running out from the moth through the threads
	function ring_light(a, b) {
		local s = tt - t_impact;
		if (s < 0.0 || s > 1.6) { return 0.0; }
		local ma = a - moth_a; local mb = b - moth_b;
		local behind = s * ring_speed - ::sqrt(ma * ma + mb * mb);
		if (behind < 0.0 || behind > 0.7) { return 0.0; }
		return (1.0 - behind / 0.7) * 1.3 / (1.0 + s * 1.2);
	}

	// how far a point of the web (a, b) stands off its plane: the spider's weight, a breeze, the moth's strike
	// ringing out through the threads and its struggle
	function sag(a, b) {
		local sin = ::sin;
		local da = a - pa; local db = b - pb;
		local result = -0.12 / (1.0 + (da * da + db * db) * 3.0);
		result += 0.035 * sin(a * 0.8 + tt * 1.1) * sin(b * 0.7 - tt * 0.9 + 0.5) * (a * a + b * b) * 0.12;
		if (tt > t_impact) {
			local ma = a - moth_a; local mb = b - moth_b;
			local dist = ::sqrt(ma * ma + mb * mb);
			local s = tt - t_impact;
			local front = s * ring_speed - dist;
			if (front > 0.0) { result += 0.34 / (1.0 + s * s * 1.6) * sin(front * 4.5) / (1.0 + dist * 0.6); }
			if (tt < t_wrap1) {
				local fight = 0.07 * (1.0 - coc) * (0.55 + 0.45 * sin(tt * 2.3));
				result += fight * sin(tt * 21.0 - dist * 4.0) / (1.0 + dist * 2.5);
			}
		}
		return result;
	}

	// a point of the web in the world, into wx, wy, wz
	function web_point(a, b) {
		wx = a;
		wy = b;
		wz = sag(a, b);
	}

	function bezier(from, via, to, u) {
		return from * (1.0 - u) * (1.0 - u) + via * 2.0 * u * (1.0 - u) + to * u * u;
	}

	// the moth at time t: it flutters in along a curve from off the frame's left, strikes the web and sticks, beats its
	// wings against the silk, then folds them as the cocoon closes and spins
	function moth_pose(t) {
		local sin = ::sin;
		if (t < t_impact) {
			local u = (t - t_moth) / (t_impact - t_moth);
			u = u < 0.0 ? 0.0 : u;
			local a = bezier(-2.7, -0.9, moth_a, u);
			local b = bezier(0.6, 1.15, moth_b, u);
			local n = bezier(1.4, 0.9, 0.06, u);
			local va = bezier(-2.7, -0.9, moth_a, u + 0.02) - a;
			local vb = bezier(0.6, 1.15, moth_b, u + 0.02) - b;
			local vn = bezier(1.4, 0.9, 0.06, u + 0.02) - n;
			local flutter = 1.0 - u;
			mox = a + flutter * 0.16 * sin(t * 6.3);
			moy = b + flutter * 0.18 * sin(t * 8.9 + 1.0);
			moz = n + flutter * 0.14 * sin(t * 5.2 + 2.0);
			local vl = ::sqrt(va * va + vb * vb + vn * vn) + 0.000001;
			mfx = va / vl; mfy = vb / vl + 0.5; mfz = vn / vl;
			flap = 0.95 * sin(t * 31.4);
			wsc = 1.0; coc = 0.0; spin = 0.0;
		} else {
			web_point(moth_a, moth_b);
			mox = wx; moy = wy; moz = wz + 0.09;
			mfx = 0.34; mfy = 0.94; mfz = 0.0;
			coc = smooth((t - t_wrap0) / (t_wrap1 - 0.15 - t_wrap0));
			spin = t > t_wrap0 ? ((t < t_wrap1 ? t : t_wrap1) - t_wrap0) * 6.0 : 0.0;
			local beat = sin(t * 15.0);
			flap = (1.0 - coc) * 0.55 * (beat > 0.0 ? beat : 0.0) * (0.6 + 0.4 * sin(t * 2.3)) + coc * 1.3;
			wsc = 1.0 - 0.7 * coc;
		}
		local ml = 1.0 / ::sqrt(mfx * mfx + mfy * mfy + mfz * mfz);
		mfx *= ml; mfy *= ml; mfz *= ml;
		mux = 0.0 - mfx * mfz; muy = 0.0 - mfy * mfz; muz = 1.0 - mfz * mfz;
		local ul = 1.0 / ::sqrt(mux * mux + muy * muy + muz * muz + 0.0000001);
		mux *= ul; muy *= ul; muz *= ul;
		mrx = muy * mfz - muz * mfy;
		mry = muz * mfx - mux * mfz;
		mrz = mux * mfy - muy * mfx;
		if (t > t_impact) {
			// the struggle jerks it about its spine; the wrapping rolls it
			local roll = spin * 0.12 + 0.3 * (1.0 - coc) * sin(t * 9.0);
			local c = ::cos(roll); local s = sin(roll);
			local nux = mux * c + mrx * s; local nuy = muy * c + mry * s; local nuz = muz * c + mrz * s;
			mrx = mrx * c - mux * s; mry = mry * c - muy * s; mrz = mrz * c - muz * s;
			mux = nux; muy = nuy; muz = nuz;
		}
	}

	function spawn(x, y, z, vx, vy, vz, life) {
		local i = pnext;
		pnext = (pnext + 1) % np;
		ppx[i] = x; ppy[i] = y; ppz[i] = z;
		pvx[i] = vx; pvy[i] = vy; pvz[i] = vz;
		plife[i] = 1.0;
		prate[i] = 1.0 / life;
	}

	// an ember off the ring of the crown, thrown outward at the given speed
	function crown_ember(speed) {
		local a = rng.next(628) * 0.01;
		local ca = ::cos(a) * 0.2;
		local sa = ::sin(a) * 0.2;
		local cx = bx + fx * (0.01 + ca * 0.66) + rx * sa;
		local cy = by + fy * (0.01 + ca * 0.66) + ry * sa;
		spawn(cx, cy, bz + 0.6 + ca * 0.75, (fx * ca * 0.66 + rx * sa) * speed, 0.5 + (fy * ca * 0.66 + ry * sa) * speed, 0.3 + ca * 0.75 * speed, 0.45 + rng.next(40) * 0.01);
	}

	function reached(mark, dt) { return tt >= mark && tt - dt < mark; }

	// embers leave the crown once it burns, more on the kick and many when the jaw gapes; the waking and the lunge throw
	// a burst of them, the strike a puff of scales off the moth
	function update_particles(dt) {
		spawn_acc += dt * 5.0 * ::SK_WAKE * (0.3 + ::SK_KICK + gape(tt) * 8.0);
		while (spawn_acc >= 1.0) {
			spawn_acc -= 1.0;
			crown_ember(1.0);
		}
		if (reached(t_wake, dt) || reached(t_lunge, dt)) {
			for (local k = 0; k < 26; ++k) { crown_ember(5.0); }
		}
		if (reached(t_impact, dt)) {
			for (local k = 0; k < 22; ++k) {
				local a = rng.next(628) * 0.01;
				local speed = 0.5 + rng.next(100) * 0.012;
				spawn(moth_a, moth_b, 0.12, ::cos(a) * speed, ::sin(a) * speed, 0.3 + jitter(100) * 0.3, 0.5 + rng.next(50) * 0.01);
			}
		}
		local drag = 1.0 - dt * 1.4;
		for (local i = 0; i < np; ++i) {
			if (plife[i] <= 0.0) { continue; }
			pvx[i] = pvx[i] * drag;
			pvy[i] = (pvy[i] + 0.7 * dt) * drag;
			pvz[i] = pvz[i] * drag;
			ppx[i] = ppx[i] + pvx[i] * dt;
			ppy[i] = ppy[i] + pvy[i] * dt;
			ppz[i] = ppz[i] + pvz[i] * dt;
			plife[i] = plife[i] - dt * prate[i];
		}
	}

	function update(t, dt) {
		tt = t;
		::SK_T = t;
		::SK_WAKE = smooth((t - t_wake + 0.05) / 0.35);
		hear(t);
		stance = 0.5 + 0.5 * smooth((t - t_wake) / 0.3);
		web_motion(t, dt);
		local spring = lunge(t);
		if (spring > 0.0 && placed) {
			local lx = ex - bx; local ly = ey - by; local lz = ez - bz;
			local ll = 0.75 * spring / ::sqrt(lx * lx + ly * ly + lz * lz + 0.000001);
			bx += lx * ll; by += ly * ll; bz += lz * ll;
		}
		if (!placed) {
			for (local i = 0; i < 8; ++i) {
				rest_foot(i, 0.0);
				footx[i] = tox[i]; footy[i] = toy[i]; footz[i] = toz[i];
			}
		}
		walk(dt);
		update_particles(dt);
		if (t > t_moth) { moth_pose(t); }
		aim_camera(t, dt);
		placed = true;
	}

	// the tetrapod gait: a foot steps when its rest spot has run off, one group at a time; at speed it scuttles, with
	// quicker steps that lead the body and both groups free
	function walk(dt) {
		local scuttle = spd > 1.5;
		local step_time = scuttle ? 0.08 : 0.13;
		local lead = spd * 0.1;
		lead = lead > 0.45 ? 0.45 : lead;
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
				stept[i] = stept[i] + dt / step_time;
				rest_foot(i, lead);
				if (stept[i] >= 1.0) {
					stept[i] = -1.0;
					footx[i] = tox[i]; footy[i] = toy[i]; footz[i] = toz[i];
				} else {
					local e = stept[i];
					footx[i] = fromx[i] + (tox[i] - fromx[i]) * e;
					footy[i] = fromy[i] + (toy[i] - fromy[i]) * e;
					footz[i] = fromz[i] + (toz[i] - fromz[i]) * e + ::sin(e * 3.14159265) * 0.16;
				}
			} else {
				rest_foot(i, 0.0);
				local ddx = footx[i] - tox[i];
				local ddy = footy[i] - toy[i];
				local ddz = footz[i] - toz[i];
				local other_moving = group_a ? stepping_b : stepping_a;
				if ((scuttle || !other_moving) && ddx * ddx + ddy * ddy + ddz * ddz > 0.05) {
					stept[i] = 0.0;
					fromx[i] = footx[i]; fromy[i] = footy[i]; fromz[i] = footz[i];
					if (group_a) { stepping_a = true; } else { stepping_b = true; }
				}
			}
		}
	}

	// the camera: close on the sleeper, back for the whole web while it is thrown and the moth flies in, in on the hunter
	// and its catch, then round in front of the skull, below the line of its gaze (the face is reared 0.85 radians off
	// the body, the camera stands 0.51 up); the lunge lifts the aim up the face, which keeps the crown in the frame
	function aim_camera(t, dt) {
		local hx = bx; local hy = by; local hz = bz + 0.2;
		local back = smooth((t - t_wake - 0.3) / 0.9);
		local near = smooth((t - t_impact - 0.25) / (t_reach - t_impact - 0.25)) * (1.0 - facing(t));
		local creep = stalk(t);
		local onto = facing(t) * 0.6 + creep * 0.4;
		local tgx = hx * (1.0 - 0.75 * back);
		local tgy = hy * (1.0 - 0.75 * back);
		local tgz = hz * (1.0 - back);
		if (t > t_moth) {
			tgx += ((hx + mox) * 0.5 - tgx) * near;
			tgy += ((hy + moy) * 0.5 - tgy) * near;
			tgz += ((hz + moz) * 0.5 - tgz) * near;
		}
		tgx += (hx + fx * 0.34 - tgx) * onto;
		tgy += (hy + fy * 0.34 - tgy) * onto;
		tgz += (hz - tgz) * onto;
		local rise = 0.12 * lunge(t);
		tgx -= fx * 0.7513 * rise;
		tgy -= fy * 0.7513 * rise;
		tgz += 0.66 * rise;
		local dist = 2.3 - 0.3 * (t < t_wake ? t / t_wake : 1.0);
		dist += (3.5 - dist) * back;
		dist += (2.1 - dist) * near;
		dist += (2.75 - dist) * facing(t);
		dist = dist - creep * 0.53 - lunge(t) * 0.53;
		local oa = 0.1 + 0.12 * ::sin(t * 0.19);
		local dx = ::sin(oa);
		local dy = 0.12 + 0.05 * ::sin(t * 0.31);
		local dz = ::cos(oa);
		local dn = ::sqrt(dx * dx + dy * dy + dz * dz);
		dx += (fx * 0.8712 * dn - dx) * onto;
		dy += (fy * 0.8712 * dn - dy) * onto;
		dz += (0.4908 * dn - dz) * onto;
		local dl = dist / ::sqrt(dx * dx + dy * dy + dz * dz);
		local kc = placed ? dt * (2.8 + 9.0 * lunge(t) * (1.0 - smooth((t - t_lunge - 0.5) / 0.5))) : 1.0;
		kc = kc > 1.0 ? 1.0 : kc;
		ex += (tgx + dx * dl - ex) * kc; ey += (tgy + dy * dl - ey) * kc; ez += (tgz + dz * dl - ez) * kc;
		local kl = placed ? dt * 4.0 : 1.0;
		kl = kl > 1.0 ? 1.0 : kl;
		lkx += (tgx - lkx) * kl; lky += (tgy - lky) * kl; lkz += (tgz - lkz) * kl;
		local ku = placed ? dt * 2.2 : 1.0;
		ku = ku > 1.0 ? 1.0 : ku;
		cux += (0.0 - fx * 0.7513 * onto - cux) * ku;
		cuy += (1.0 - onto - fy * 0.7513 * onto - cuy) * ku;
		cuz += (0.66 * onto - cuz) * ku;
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
		local pux = ::SK_PUX; local puy = ::SK_PUY; local puz = ::SK_PUZ;
		local prx = ::SK_PRX; local pry = ::SK_PRY; local prz = ::SK_PRZ;
		local pfx = ::SK_PFX; local pfy = ::SK_PFY; local pfz = ::SK_PFZ;
		local bx0 = ::SK_HX + pux * 0.16 + prx * side * 0.12 - pfx * 0.04;
		local by0 = ::SK_HY + puy * 0.16 + pry * side * 0.12 - pfy * 0.04;
		local bz0 = ::SK_HZ + puz * 0.16 + prz * side * 0.12 - pfz * 0.04;
		local mx = bx0 + prx * side * 0.2 + pux * 0.12 - pfx * 0.12;
		local my = by0 + pry * side * 0.2 + puy * 0.12 - pfy * 0.12;
		local mz = bz0 + prz * side * 0.2 + puz * 0.12 - pfz * 0.12;
		local tx = mx + prx * side * 0.04 + pux * 0.26 - pfx * 0.2;
		local ty = my + pry * side * 0.04 + puy * 0.26 - pfy * 0.2;
		local tz = mz + prz * side * 0.04 + puz * 0.26 - pfz * 0.2;
		capsule(i, bx0, by0, bz0, mx, my, mz, 0.05);
		capsule(i + 1, mx, my, mz, tx, ty, tz, 0.028);
		return i + 2;
	}

	// a leg from hip to foot: femur and tibia by two-bone IK to an ankle held above the foot, then the tarsus
	function leg(ci, i, hx, hy, hz, side) {
		local sqrt = ::sqrt;
		local px = footx[i]; local py = footy[i]; local pz = footz[i];
		local hdx = px - hx; local hdy = py - hy; local hdz = pz - hz;
		local hu = hdx * ux + hdy * uy + hdz * uz;
		hdx -= ux * hu; hdy -= uy * hu; hdz -= uz * hu;
		local hl = 1.0 / sqrt(hdx * hdx + hdy * hdy + hdz * hdz + 0.000001);
		hdx *= hl; hdy *= hl; hdz *= hl;
		local ax = px + ux * 0.13 - hdx * 0.06;
		local ay = py + uy * 0.13 - hdy * 0.06;
		local az = pz + uz * 0.13 - hdz * 0.06;
		local dx = ax - hx;
		local dy = ay - hy;
		local dz = az - hz;
		local d = sqrt(dx * dx + dy * dy + dz * dz);
		local dn = d < 0.0001 ? 0.0001 : d;
		dx /= dn; dy /= dn; dz /= dn;
		local dmax = (l1 + l2) * 0.995;
		local dc = d > dmax ? dmax : d;
		dc = dc < 0.08 ? 0.08 : dc;
		local a = (l1 * l1 - l2 * l2 + dc * dc) / (2.0 * dc);
		local hh = l1 * l1 - a * a;
		hh = hh < 0.0 ? 0.0 : sqrt(hh);
		local bux = ux + rx * side * 0.35;
		local buy = uy + ry * side * 0.35;
		local buz = uz + rz * side * 0.35;
		local bd = bux * dx + buy * dy + buz * dz;
		bux -= dx * bd; buy -= dy * bd; buz -= dz * bd;
		local bl = 1.0 / sqrt(bux * bux + buy * buy + buz * buz + 0.0000001);
		bux *= bl; buy *= bl; buz *= bl;
		local kx = hx + dx * a + bux * hh;
		local ky = hy + dy * a + buy * hh;
		local kz = hz + dz * a + buz * hh;
		local tx = hx + dx * dc;
		local ty = hy + dy * dc;
		local tz = hz + dz * dc;
		capsule(ci, hx, hy, hz, kx, ky, kz, 0.042);
		capsule(ci + 1, kx, ky, kz, tx, ty, tz, 0.03);
		capsule(ci + 2, tx, ty, tz, px, py, pz, 0.018);
		glow(3 + i, kx, ky, kz, 0.027 + 0.006 * ::sin(tt * 7.0 + i));
		return ci + 3;
	}

	function render(grid) {
		local sin = ::sin;
		local cos = ::cos;
		local sqrt = ::sqrt;
		// the rays trace on a grid coarser than the picture's and the trace scales up to it, coarser still where the
		// creature fills the frame and every ray is dear; the silk and the embers draw over it at full size
		local glare = facing(tt) * 0.3 + stalk(tt) * 0.7;
		local close = 1.0 - smooth((tt - t_wake - 0.3) / 0.9);
		local caught = smooth((tt - t_impact - 0.25) / 0.5);
		close = close > caught ? close : caught;
		local trace_k = sqrt((6400.0 - close * 1800.0) * (1.0 - ::SK_PUNCH * 0.25) / (w * h));
		trace_k = trace_k > 1.0 ? 1.0 : trace_k;
		local tw = (w * trace_k).tointeger();
		local th = (h * trace_k).tointeger();
		::SK_W = tw;
		::SK_H = th;
		::SK_FLICK = 0.7 * sin(tt * 17.0) * sin(tt * 5.3) + 1.5 * flare(tt);
		if (tt > t_wrap1) { ::SK_FLICK = ::SK_FLICK * 0.4 + 0.9 + glare * 1.4; }
		local bob = 0.2 + 0.012 * sin(tt * 1.6) * (1.0 - ::SK_WAKE) + 0.018 * sin(tt * 11.0) * ::SK_WAKE;
		local cx = bx + ux * bob;
		local cy = by + uy * bob;
		local cz = bz + uz * bob;
		// the sleeper's skull hangs level with its body; awake it rears back to face the viewer
		local pitch = 0.3 + 0.55 * smooth((tt - t_wake) / 0.4);
		local cpt = cos(pitch); local spt = sin(pitch);
		local hfx = fx * cpt + ux * spt; local hfy = fy * cpt + uy * spt; local hfz = fz * cpt + uz * spt;
		local hux = ux * cpt - fx * spt; local huy = uy * cpt - fy * spt; local huz = uz * cpt - fz * spt;
		::SK_PFX = hfx; ::SK_PFY = hfy; ::SK_PFZ = hfz;
		::SK_PUX = hux; ::SK_PUY = huy; ::SK_PUZ = huz;
		::SK_PRX = rx; ::SK_PRY = ry; ::SK_PRZ = rz;
		::SK_QFX = fx; ::SK_QFY = fy; ::SK_QFZ = fz;
		::SK_QUX = ux; ::SK_QUY = uy; ::SK_QUZ = uz;
		::SK_TX = cx; ::SK_TY = cy; ::SK_TZ = cz;
		::SK_AR = 0.27;
		::SK_AX = cx - fx * 0.45 + ux * 0.12; ::SK_AY = cy - fy * 0.45 + uy * 0.12; ::SK_AZ = cz - fz * 0.45 + uz * 0.12;
		local nod = 0.03 * sin(tt * 2.1);
		::SK_HX = cx + fx * 0.34 + ux * (0.09 + nod); ::SK_HY = cy + fy * 0.34 + uy * (0.09 + nod); ::SK_HZ = cz + fz * 0.34 + uz * (0.09 + nod);
		local shx = ::SK_HX; local shy = ::SK_HY; local shz = ::SK_HZ;
		::SK_NX = shx + hfx * 0.23 - hux * 0.06; ::SK_NY = shy + hfy * 0.23 - huy * 0.06; ::SK_NZ = shz + hfz * 0.23 - huz * 0.06;
		local jaw = 0.012 * sin(tt * 6.0) + 0.1 * gape(tt);
		::SK_JX = shx + hfx * 0.1 - hux * (0.14 + jaw); ::SK_JY = shy + hfy * 0.1 - huy * (0.14 + jaw); ::SK_JZ = shz + hfz * 0.1 - huz * (0.14 + jaw);
		local sjx = ::SK_JX; local sjy = ::SK_JY; local sjz = ::SK_JZ;
		::SK_BCX = cx; ::SK_BCY = cy; ::SK_BCZ = cz;
		::SK_BR = 1.6;
		local ncap = 5 + 2 + 24 + 4 + 2 + 7;
		if (::SK_CAX.len() != ncap) {
			::SK_CAX = []; ::SK_CAY = []; ::SK_CAZ = []; ::SK_CDX = []; ::SK_CDY = []; ::SK_CDZ = []; ::SK_CID = []; ::SK_CR = []; ::SK_CMX = []; ::SK_CMY = []; ::SK_CMZ = []; ::SK_CB = [];
			for (local i = 0; i < ncap; ++i) {
				::SK_CAX.push(0.0); ::SK_CAY.push(0.0); ::SK_CAZ.push(0.0); ::SK_CDX.push(0.0); ::SK_CDY.push(0.0); ::SK_CDZ.push(0.0); ::SK_CID.push(1.0); ::SK_CR.push(0.1); ::SK_CMX.push(0.0); ::SK_CMY.push(0.0); ::SK_CMZ.push(0.0); ::SK_CB.push(1.0);
			}
		}
		local nglow = 3 + 8;
		if (::SK_GX.len() != nglow) {
			::SK_GX = []; ::SK_GY = []; ::SK_GZ = []; ::SK_GR = [];
			for (local i = 0; i < nglow; ++i) { ::SK_GX.push(0.0); ::SK_GY.push(0.0); ::SK_GZ.push(0.0); ::SK_GR.push(0.02); }
		}
		::SK_NC = ncap;
		::SK_CROWN0 = ncap - 7;
		::SK_NG = nglow;
		::SK_NBONE = 5;
		local eye = 0.03 * (1.0 + glare * 0.6);
		::SK_GLARE = glare;
		glow(0, shx + hfx * 0.175 + hux * 0.03 + rx * 0.088, shy + hfy * 0.175 + huy * 0.03 + ry * 0.088, shz + hfz * 0.175 + huz * 0.03 + rz * 0.088, eye);
		glow(1, shx + hfx * 0.175 + hux * 0.03 - rx * 0.088, shy + hfy * 0.175 + huy * 0.03 - ry * 0.088, shz + hfz * 0.175 + huz * 0.03 - rz * 0.088, eye);
		local third = (0.024 + 0.006 * sin(tt * 3.0)) * (1.0 + glare * 0.5);
		glow(2, shx + hfx * 0.17 + hux * 0.15, shy + hfy * 0.17 + huy * 0.15, shz + hfz * 0.17 + huz * 0.15, third);
		local sgx = ::SK_GX; local sgy = ::SK_GY; local sgz = ::SK_GZ;
		::SK_LX = sgx[2] * 0.4 + (sgx[0] + sgx[1]) * 0.3 + hfx * 0.1;
		::SK_LY = sgy[2] * 0.4 + (sgy[0] + sgy[1]) * 0.3 + hfy * 0.1;
		::SK_LZ = sgz[2] * 0.4 + (sgz[0] + sgz[1]) * 0.3 + hfz * 0.1;
		capsule(0, sjx - rx * 0.08, sjy - ry * 0.08, sjz - rz * 0.08, sjx + rx * 0.08, sjy + ry * 0.08, sjz + rz * 0.08, 0.05);
		local ci = horn(1, 1.0);
		ci = horn(ci, -1.0);
		local fang = 0.02 * sin(tt * 6.0 + 1.0) + 0.05 * gape(tt);
		for (local s = 0; s < 2; ++s) {
			local side = s == 0 ? 1.0 : -1.0;
			local ax = sjx + hfx * 0.05 + rx * side * 0.035;
			local ay = sjy + hfy * 0.05 + ry * side * 0.035;
			local az = sjz + hfz * 0.05 + rz * side * 0.035;
			capsule(ci, ax, ay, az, ax + hfx * 0.07 - hux * 0.12 - rx * side * (0.02 + fang), ay + hfy * 0.07 - huy * 0.12 - ry * side * (0.02 + fang), az + hfz * 0.07 - huz * 0.12 - rz * side * (0.02 + fang), 0.016);
			ci = ci + 1;
		}
		for (local i = 0; i < 8; ++i) {
			local side = i < 4 ? 1.0 : -1.0;
			local fo = legfo[i % 4];
			local hx = cx + fx * fo * 0.45 + rx * side * 0.1 + ux * 0.03;
			local hy = cy + fy * fo * 0.45 + ry * side * 0.1 + uy * 0.03;
			local hz = cz + fz * fo * 0.45 + rz * side * 0.1 + uz * 0.03;
			ci = leg(ci, i, hx, hy, hz, side);
		}
		// pedipalps feel the air in front of the jaw
		for (local s = 0; s < 2; ++s) {
			local side = s == 0 ? 1.0 : -1.0;
			local twitch = 0.03 * sin(tt * 7.3 + s * 2.0);
			local px = shx + hfx * 0.2 - hux * 0.1 + rx * side * 0.08;
			local py = shy + hfy * 0.2 - huy * 0.1 + ry * side * 0.08;
			local pz = shz + hfz * 0.2 - huz * 0.1 + rz * side * 0.08;
			local mx = px + hfx * 0.11 + hux * (0.05 + twitch) + rx * side * 0.05;
			local my = py + hfy * 0.11 + huy * (0.05 + twitch) + ry * side * 0.05;
			local mz = pz + hfz * 0.11 + huz * (0.05 + twitch) + rz * side * 0.05;
			capsule(ci, px, py, pz, mx, my, mz, 0.02);
			capsule(ci + 1, mx, my, mz, mx + hfx * 0.08 - hux * 0.09, my + hfy * 0.08 - huy * 0.09, mz + hfz * 0.08 - huz * 0.09, 0.017);
			ci = ci + 2;
		}
		// the spinnerets at the abdomen's tip
		for (local s = 0; s < 2; ++s) {
			local side = s == 0 ? 1.0 : -1.0;
			local sx = ::SK_AX - fx * 0.32 + rx * side * 0.03 - ux * 0.02;
			local sy = ::SK_AY - fy * 0.32 + ry * side * 0.03 - uy * 0.02;
			local sz = ::SK_AZ - fz * 0.32 + rz * side * 0.03 - uz * 0.02;
			capsule(ci, sx, sy, sz, sx - fx * 0.08 - ux * 0.03, sy - fy * 0.08 - uy * 0.03, sz - fz * 0.08 - uz * 0.03, 0.02);
			ci = ci + 1;
		}
		local hover = 0.03 * sin(tt * 1.7);
		::SK_HGX = shx + hux * 0.3; ::SK_HGY = shy + huy * 0.3; ::SK_HGZ = shz + huz * 0.3;
		::SK_BGX = (::SK_AX + ::SK_TX) * 0.5; ::SK_BGY = (::SK_AY + ::SK_TY) * 0.5; ::SK_BGZ = (::SK_AZ + ::SK_TZ) * 0.5;
		::SK_CRX = shx + hux * (0.44 + hover); ::SK_CRY = shy + huy * (0.44 + hover); ::SK_CRZ = shz + huz * (0.44 + hover);
		local crx = ::SK_CRX; local cry = ::SK_CRY; local crz = ::SK_CRZ;
		local wake = ::SK_WAKE;
		local kick = ::SK_KICK;
		local crown0 = ::SK_CROWN0;
		for (local k = 0; k < 7; ++k) {
			local a = k * 0.8976 + tt * 0.6;
			local ca = cos(a) * 0.2;
			local sa = sin(a) * 0.2;
			local flame = (0.02 + 0.08 * wake) + (0.05 * sin(tt * 9.0 + k * 1.9) + 0.03 * sin(tt * 23.0 + k)) * wake + 0.03 * kick * wake + (0.07 + 0.05 * kick) * glare;
			local bxk = crx + hfx * ca + rx * sa;
			local byk = cry + hfy * ca + ry * sa;
			local bzk = crz + hfz * ca + rz * sa;
			capsule(crown0 + k, bxk, byk, bzk, bxk + hux * flame + hfx * ca * 0.25 + rx * sa * 0.25, byk + huy * flame + hfy * ca * 0.25 + ry * sa * 0.25, bzk + huz * flame + hfz * ca * 0.25 + rz * sa * 0.25, 0.018);
		}
		// the bounding radii the distance skips on: the skull's jaw about its center, the ring and flames about the crown's
		local jhx = sjx - shx; local jhy = sjy - shy; local jhz = sjz - shz;
		local jaw_reach = sqrt(jhx * jhx + jhy * jhy + jhz * jhz) + 0.085 + 0.09;
		::SK_SKB = (jaw_reach > ::SK_HR ? jaw_reach : ::SK_HR) + 0.001;
		local crown_reach = 0.2 + 0.028;
		local cmx = ::SK_CMX; local cmy = ::SK_CMY; local cmz = ::SK_CMZ; local cbr = ::SK_CB;
		local nc = ::SK_NC;
		for (local k = crown0; k < nc; ++k) {
			local mx = cmx[k] - crx; local my = cmy[k] - cry; local mz = cmz[k] - crz;
			local r = sqrt(mx * mx + my * my + mz * mz) + cbr[k];
			crown_reach = r > crown_reach ? r : crown_reach;
		}
		::SK_CRB = crown_reach + 0.001;
		local spin_gem = tt * 2.2;
		local gl2 = 0.3 + 0.02 * sin(tt * 2.5);
		::SK_GMX = crx + hux * gl2; ::SK_GMY = cry + huy * gl2; ::SK_GMZ = crz + huz * gl2;
		::SK_GAX = rx * cos(spin_gem) + hfx * sin(spin_gem); ::SK_GAY = ry * cos(spin_gem) + hfy * sin(spin_gem); ::SK_GAZ = rz * cos(spin_gem) + hfz * sin(spin_gem);
		::SK_GBX = hfx * cos(spin_gem) - rx * sin(spin_gem); ::SK_GBY = hfy * cos(spin_gem) - ry * sin(spin_gem); ::SK_GBZ = hfz * cos(spin_gem) - rz * sin(spin_gem);
		pose_moth();
		::SK_EX = ex; ::SK_EY = ey; ::SK_EZ = ez;
		local lx = lkx - ex;
		local ly = lky - ey;
		local lz = lkz - ez;
		local ll = 1.0 / sqrt(lx * lx + ly * ly + lz * lz);
		lx *= ll; ly *= ll; lz *= ll;
		local d = cux * lx + cuy * ly + cuz * lz;
		local upx = cux - lx * d;
		local upy = cuy - ly * d;
		local upz = cuz - lz * d;
		local ul = 1.0 / sqrt(upx * upx + upy * upy + upz * upz + 0.0000001);
		upx *= ul; upy *= ul; upz *= ul;
		::SK_FX = lx; ::SK_FY = ly; ::SK_FZ = lz;
		::SK_UX = upx; ::SK_UY = upy; ::SK_UZ = upz;
		::SK_RX = ly * upz - lz * upy;
		::SK_RY = lz * upx - lx * upz;
		::SK_RZ = lx * upy - ly * upx;
		// the key light: above the camera, a little to its right, toward the scene
		local kx = upx * 0.75 - lx * 0.45 + ::SK_RX * 0.35;
		local ky = upy * 0.75 - ly * 0.45 + ::SK_RY * 0.35;
		local kz = upz * 0.75 - lz * 0.45 + ::SK_RZ * 0.35;
		local kl = 1.0 / sqrt(kx * kx + ky * ky + kz * kz);
		::SK_KX = kx * kl; ::SK_KY = ky * kl; ::SK_KZ = kz * kl;
		if (::SK_ROWS.len() != th) {
			::SK_ROWS = [];
			for (local r = 0; r < th; ++r) { ::SK_ROWS.push(r); }
		}
		local strike = tt > t_impact ? ::exp((t_impact - tt) * 5.0) : 0.0;
		local leap = tt > t_lunge ? ::exp((t_lunge - tt) * 4.0) : 0.0;
		::sk_backdrop((0.55 + 0.45 * ::SK_WAKE) * (1.0 - 0.5 * glare), 0.1 * flare(tt) + 0.14 * strike + 0.16 * leap);
		// parallel_transform: the same row calls in order, serially
		local rows = ::SK_ROWS.map(::sk_row);
		if (depth.len() != w * h) {
			depth = [];
			for (local q = 0; q < w * h; ++q) { depth.push(0); }
		}
		local dep = depth;
		local ww = w;
		local hh = h;
		local idx = 0;
		for (local y = 0; y < hh; ++y) {
			local row = rows[y * th / hh];
			for (local x = 0; x < ww; ++x) {
				local v = row[x * tw / ww];
				grid[idx] = v % 1024;
				dep[idx] = v / 1024;
				idx = idx + 1;
			}
		}
		draw_web(grid);
		draw_particles(grid);
		strike_shake(grid);
	}

	// the strike and the lunge shake the frame for half a second
	function strike_shake(grid) {
		local s = tt - (tt >= t_lunge ? t_lunge : t_impact);
		if (s < 0.0 || s > 0.5) { return; }
		local amp = (0.5 - s) * w * 0.05;
		local row = [];
		for (local x = 0; x < w; ++x) { row.push(0); }
		for (local y = 0; y < h; ++y) {
			local off = (::sin(y * 0.7 + tt * 61.0) * amp).tointeger();
			if (off == 0) { continue; }
			local row0 = y * w;
			for (local x2 = 0; x2 < w; ++x2) { row[x2] = grid[row0 + x2]; }
			for (local x3 = 0; x3 < w; ++x3) {
				local from = x3 - off;
				from = from < 0 ? 0 : (from >= w ? w - 1 : from);
				grid[row0 + x3] = row[from];
			}
		}
	}

	function pose_moth() {
		::SK_MOTH = tt > t_moth;
		if (!::SK_MOTH) { return; }
		moth_pose(tt);
		::SK_MX = mox; ::SK_MY = moy; ::SK_MZ = moz;
		::SK_MFX = mfx; ::SK_MFY = mfy; ::SK_MFZ = mfz;
		::SK_MUX = mux; ::SK_MUY = muy; ::SK_MUZ = muz;
		::SK_MRX = mrx; ::SK_MRY = mry; ::SK_MRZ = mrz;
		::SK_WSC = wsc;
		::SK_COC = coc;
		::SK_SPIN = spin;
		::SK_MB = 0.68 * wsc + 0.12 + 0.28 * coc + 0.06;
		::SK_MB = ::SK_MB < 0.4 ? 0.4 : ::SK_MB;
		local c = ::cos(flap); local s = ::sin(flap);
		for (local k = 0; k < 4; ++k) {
			local side = k % 2 == 0 ? 1.0 : -1.0;
			local fore = k < 2;
			local off = fore ? 0.045 : -0.07;
			::SK_WOX[k] = mox + mfx * off + mrx * side * 0.05;
			::SK_WOY[k] = moy + mfy * off + mry * side * 0.05;
			::SK_WOZ[k] = moz + mfz * off + mrz * side * 0.05;
			::SK_WSX[k] = mrx * side * c + mux * s; ::SK_WSY[k] = mry * side * c + muy * s; ::SK_WSZ[k] = mrz * side * c + muz * s;
			::SK_WNX[k] = -mrx * side * s + mux * c; ::SK_WNY[k] = -mry * side * s + muy * c; ::SK_WNZ[k] = -mrz * side * s + muz * c;
			::SK_WCA[k] = (fore ? 0.33 : 0.23) * wsc;
			::SK_WSA[k] = (fore ? 0.33 : 0.23) * wsc;
			::SK_WCB[k] = (fore ? 0.0 : -0.11) * wsc;
			::SK_WSB[k] = (fore ? 0.16 : 0.135) * wsc;
		}
	}

	// a world point onto the screen, into qsx, qsy and its depth qzc; false behind the camera
	function project(x, y, z) {
		local ox = x - ex; local oy = y - ey; local oz = z - ez;
		local zc = ox * ::SK_FX + oy * ::SK_FY + oz * ::SK_FZ;
		if (zc < 0.08) { return false; }
		local half_h = h * 0.5;
		local scale = half_h / (::SK_ZOOM * zc);
		qsx = (ox * ::SK_RX + oy * ::SK_RY + oz * ::SK_RZ) * scale + w * 0.5;
		qsy = half_h - (ox * ::SK_UX + oy * ::SK_UY + oz * ::SK_UZ) * scale;
		qzc = zc;
		return true;
	}

	// a silk thread from (x0, y0, z0) to (x1, y1, z1), depth-tested against the traced scene; its hue turns with its
	// angle on the screen and with time, the way silk breaks light into colors
	function thread(grid, x0, y0, z0, x1, y1, z1, bright) {
		if (!project(x0, y0, z0)) { return; }
		local sx0 = qsx; local sy0 = qsy; local z0c = qzc;
		if (!project(x1, y1, z1)) { return; }
		local ddx = qsx - sx0; local ddy = qsy - sy0;
		local adx = ddx < 0.0 ? -ddx : ddx;
		local ady = ddy < 0.0 ? -ddy : ddy;
		local steps = ((adx > ady ? adx : ady) + 1.0).tointeger();
		if (steps > 400) { return; }
		local floor = ::floor;
		local hue = ::atan2(ddy, ddx) * 0.4 + tt * 0.15 + (sx0 + sy0) * 0.006;
		hue = hue - floor(hue / 8.0) * 8.0;
		local hq = hue.tointeger();
		hq = hq > 7 ? 7 : hq;
		local ink = ::SK_SILK + hq * 6;
		local ix = ddx / steps; local iy = ddy / steps; local iz = (qzc - z0c) / steps;
		local ww = w;
		local hh = h;
		local dep = depth;
		local bayers = ::SK_BAYER;
		for (local s = 0; s <= steps; ++s) {
			local px = floor(sx0 + ix * s).tointeger();
			local py = floor(sy0 + iy * s).tointeger();
			if (px < 0 || py < 0 || px >= ww || py >= hh) { continue; }
			local at = py * ww + px;
			local zc = z0c + iz * s;
			if (dep[at] <= (zc * 18.0).tointeger()) { continue; }
			local fog = 1.0 - zc / 11.0;
			local l = (fog * bright * 5.5 + bayers[(py % 4) * 4 + px % 4] / 16.0 - 0.3).tointeger();
			if (l < 0) { continue; }
			grid[at] = ink + (l > 5 ? 5 : l);
		}
	}

	function glint(grid, x, y, z) {
		if (!project(x, y, z)) { return; }
		local px = ::floor(qsx).tointeger();
		local py = ::floor(qsy).tointeger();
		if (px < 0 || py < 0 || px >= w || py >= h) { return; }
		local at = py * w + px;
		if (depth[at] <= (qzc * 18.0).tointeger()) { return; }
		grid[at] = ::SK_EYE + 7;
	}

	// the silk, drawn over the trace: the dragline the sleeper hangs from and the spokes cast after it, a spark at each
	// growing tip; the hub's mesh; the capture spiral, every winding growing at once behind the running spider on a fan
	// of fresh thread from its spinnerets; the strands that wind the cocoon. The strike's ring lights the threads it
	// passes, and the whole web flashes with dew as the spiral closes and again as the cocoon is sealed
	function draw_web(grid) {
		local sin = ::sin;
		local cos = ::cos;
		local shimmer = tt >= t_spiral1 ? ::exp((t_spiral1 - tt) * 4.0) : 0.0;
		local sealed = tt - t_wrap1;
		if (sealed >= 0.0) { shimmer = ::exp(0.0 - sealed * 5.0); }
		for (local k = 0; k < nspoke; ++k) {
			local order = k < nspoke / 2 ? k : k - 1;
			local grow = k == nspoke / 2 ? 1.0 : (tt - t_spokes - order * t_spoke_gap) / 0.25;
			if (grow <= 0.0) { continue; }
			grow = grow > 1.0 ? 1.0 : grow;
			local ca = cos(spoke_ang[k]) * spoke_reach * grow / 10.0;
			local sa = sin(spoke_ang[k]) * spoke_reach * grow / 10.0;
			web_point(0.0, 0.0);
			local px = wx; local py = wy; local pz = wz;
			for (local s = 1; s <= 10; ++s) {
				web_point(ca * s, sa * s);
				thread(grid, px, py, pz, wx, wy, wz, 0.78 + shimmer * 0.4 + ring_light(ca * s, sa * s));
				px = wx; py = wy; pz = wz;
			}
			if (grow < 1.0) { glint(grid, px, py, pz); }
		}
		if (tt > t_out) {
			for (local ring = 1; ring <= 2; ++ring) {
				local rr = ring * 0.16;
				web_point(rr, 0.0);
				local px = wx; local py = wy; local pz = wz;
				for (local s = 1; s <= 12; ++s) {
					local a = s * 0.5235988;
					web_point(rr * cos(a), rr * sin(a));
					thread(grid, px, py, pz, wx, wy, wz, 0.8 + shimmer * 0.4);
					px = wx; py = wy; pz = wz;
				}
			}
		}
		local sx = ::SK_AX - fx * 0.4; local sy = ::SK_AY - fy * 0.4; local sz = ::SK_AZ - fz * 0.4;
		if (tt > t_spiral0) {
			local sweep = laid(tt);
			local last = spa.len() - 1;
			for (local i = 1; i <= last; ++i) {
				local u = (sweep - lay_turn[i]) * nspoke;
				if (u <= 0.0) { continue; }
				web_point(spa[i - 1], spb[i - 1]);
				local px = wx; local py = wy; local pz = wz;
				if (u < 1.0) {
					web_point(spa[i - 1] + (spa[i] - spa[i - 1]) * u, spb[i - 1] + (spb[i] - spb[i - 1]) * u);
					thread(grid, px, py, pz, wx, wy, wz, 1.0);
					thread(grid, wx, wy, wz, sx, sy, sz, 1.2);
					glint(grid, wx, wy, wz);
					continue;
				}
				web_point(spa[i], spb[i]);
				thread(grid, px, py, pz, wx, wy, wz, 0.88 + shimmer * 0.4 + ring_light(spa[i], spb[i]));
				if (sin(tt * 1.3 + i * 1.7) > 0.93 - shimmer * 0.9) { glint(grid, wx, wy, wz); }
			}
		}
		if (tt > t_wrap0 && tt < t_wrap1) {
			local rad = 0.07 + 0.14 * coc;
			for (local j = 0; j < 4; ++j) {
				local a = spin * 1.6 + j * 1.5708;
				local along = -0.2 + j * 0.1 + 0.07 * sin(spin + j);
				local tx = mox + mfx * along + (mrx * cos(a) + mux * sin(a)) * rad;
				local ty = moy + mfy * along + (mry * cos(a) + muy * sin(a)) * rad;
				local tz = moz + mfz * along + (mrz * cos(a) + muz * sin(a)) * rad;
				thread(grid, sx, sy, sz, tx, ty, tz, 1.25);
			}
		}
		if (sealed >= 0.0 && sealed < 0.4) {
			for (local k = 0; k < 12; ++k) {
				local a = k * 0.5235988 + 0.2;
				local r = 0.24 + sealed * 1.3;
				glint(grid, mox + cos(a) * r, moy + sin(a) * r, moz + 0.05);
			}
		}
	}

	function plot(grid, x, y, dq, level) {
		if (x < 0 || y < 0 || x >= w || y >= h) { return; }
		local at = y * w + x;
		if (depth[at] <= dq) { return; }
		local cur = grid[at];
		local spark = ::SK_SPARK;
		if (cur >= spark && cur < spark + 8 && cur - spark >= level) { return; }
		grid[at] = spark + level;
	}

	function splat(grid, x, y, z, level, size) {
		if (!project(x, y, z)) { return; }
		local px = ::floor(qsx).tointeger();
		local py = ::floor(qsy).tointeger();
		local dq = (qzc * 18.0).tointeger();
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
		local sin = ::sin;
		local cos = ::cos;
		for (local i = 0; i < np; ++i) {
			local life = plife[i];
			if (life <= 0.0) { continue; }
			local level = (life * 7.99).tointeger();
			splat(grid, ppx[i], ppy[i], ppz[i], level, life > 0.55 ? 1 : 0);
		}
		if (::SK_WAKE < 0.5) { return; }
		for (local s = 0; s < 6; ++s) {
			local ph = tt * 4.0 + s * 1.047;
			local tw = sin(tt * 11.0 + s * 2.7);
			if (tw < 0.2) { continue; }
			local rr = 0.1 + 0.05 * sin(tt * 3.0 + s);
			local ca = cos(ph) * rr;
			local sa = sin(ph) * rr;
			local lift = 0.06 * sin(tt * 5.0 + s * 1.3);
			splat(grid, ::SK_GMX + ::SK_GAX * ca + ::SK_GBX * sa + ux * lift, ::SK_GMY + ::SK_GAY * ca + ::SK_GBY * sa + uy * lift, ::SK_GMZ + ::SK_GAZ * ca + ::SK_GBZ * sa + uz * lift, tw > 0.7 ? 7 : 5, 1);
		}
	}
}

::SCENES[9] <- ::Ossuary;
