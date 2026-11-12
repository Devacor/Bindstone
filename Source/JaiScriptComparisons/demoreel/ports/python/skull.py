"""OSSUARY (scenes/skull.jai): a hunt in twelve bars. A horned, three-eyed skull on a spider's body
hangs asleep from its dragline in a ribbed vault. On bar 2 its crown of flame catches and it wakes;
it casts the spokes of an orb web and runs the capture spiral, home by bar 4. A moth flutters in
and strikes on bar 6's downbeat, the spider runs it down on eight IK legs and wraps it in a cocoon
that closes on bar 8. Then the hunter turns on the viewer, stalks through bar 9 and lunges on bar
10. The creature, the moth and its cocoon are distance fields in one march of about 6400 rays
whatever the frame's size; the silk is drawn over the trace, depth-tested.
"""

from math import sqrt, sin, cos, atan2, floor, exp

from music import MUSIC_STEP
from reel import Scene, SCENES
from util import gfx

SK_T = 0.0
SK_EX = 0.0; SK_EY = 0.0; SK_EZ = 0.0
SK_FX = 0.0; SK_FY = 0.0; SK_FZ = 1.0
SK_RX = 1.0; SK_RY = 0.0; SK_RZ = 0.0
SK_UX = 0.0; SK_UY = 1.0; SK_UZ = 0.0
SK_BCX = 0.0; SK_BCY = 0.0; SK_BCZ = 0.0; SK_BR = 1.0
SK_AX = 0.0; SK_AY = 0.0; SK_AZ = 0.0; SK_AR = 0.24
SK_TX = 0.0; SK_TY = 0.0; SK_TZ = 0.0; SK_TR = 0.14
SK_HX = 0.0; SK_HY = 0.0; SK_HZ = 0.0; SK_HR = 0.24
SK_NX = 0.0; SK_NY = 0.0; SK_NZ = 0.0
SK_JX = 0.0; SK_JY = 0.0; SK_JZ = 0.0
SK_PFX = 0.0; SK_PFY = 0.0; SK_PFZ = 1.0
SK_PUX = 0.0; SK_PUY = 1.0; SK_PUZ = 0.0
SK_PRX = 1.0; SK_PRY = 0.0; SK_PRZ = 0.0
SK_QFX = 0.0; SK_QFY = 0.0; SK_QFZ = 1.0
SK_QUX = 0.0; SK_QUY = 1.0; SK_QUZ = 0.0
SK_LX = 0.0; SK_LY = 0.0; SK_LZ = 0.0
SK_FLICK = 0.0
SK_KAGE = 9.0         # seconds since the cue's last kick
SK_KICK = 0.0         # and its envelope
SK_ZOOM = 0.5         # the lens: half the view's height at unit distance, punching in on the hits
SK_PUNCH = 0.0        # how far it has punched in
SK_WAKE = 1.0         # 0 while the creature sleeps
SK_GLARE = 0.0        # the hunter's glare at the viewer, which kindles its eyes and lights its face
SK_CAX = []
SK_CAY = []
SK_CAZ = []
SK_CDX = []
SK_CDY = []
SK_CDZ = []
SK_CID = []
SK_CR = []
SK_CMX = []
SK_CMY = []
SK_CMZ = []
SK_CB = []
SK_HGX = 0.0; SK_HGY = 0.0; SK_HGZ = 0.0
SK_BGX = 0.0; SK_BGY = 0.0; SK_BGZ = 0.0
SK_NC = 0
SK_NBONE = 5
SK_CROWN0 = 0
SK_CRX = 0.0; SK_CRY = 0.0; SK_CRZ = 0.0
SK_SKB = 0.4
SK_CRB = 0.4
SK_GMX = 0.0; SK_GMY = 0.0; SK_GMZ = 0.0
SK_GAX = 1.0; SK_GAY = 0.0; SK_GAZ = 0.0
SK_GBX = 0.0; SK_GBY = 0.0; SK_GBZ = 1.0
SK_GX = []
SK_GY = []
SK_GZ = []
SK_GR = []
SK_NG = 0
SK_NSOCK = 3
SK_W = 0
SK_H = 0
SK_BONE = 0
SK_CHIT = 0
SK_EYE = 0
SK_MARK = 0
SK_SPARK = 0
SK_GEM = 0
SK_ROWS = []
SK_BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]
SK_KX = 0.0; SK_KY = 1.0; SK_KZ = 0.0
SK_MOTH = False
SK_MX = 0.0; SK_MY = 0.0; SK_MZ = 0.0; SK_MB = 0.6
SK_MFX = 0.0; SK_MFY = 1.0; SK_MFZ = 0.0
SK_MUX = 0.0; SK_MUY = 0.0; SK_MUZ = 1.0
SK_MRX = 1.0; SK_MRY = 0.0; SK_MRZ = 0.0
SK_WSC = 1.0
SK_COC = 0.0
SK_SPIN = 0.0
SK_WOX = [0.0, 0.0, 0.0, 0.0]; SK_WOY = [0.0, 0.0, 0.0, 0.0]; SK_WOZ = [0.0, 0.0, 0.0, 0.0]
SK_WSX = [0.0, 0.0, 0.0, 0.0]; SK_WSY = [0.0, 0.0, 0.0, 0.0]; SK_WSZ = [0.0, 0.0, 0.0, 0.0]
SK_WNX = [0.0, 0.0, 0.0, 0.0]; SK_WNY = [0.0, 0.0, 0.0, 0.0]; SK_WNZ = [0.0, 0.0, 0.0, 0.0]
SK_WCA = [0.22, 0.22, 0.15, 0.15]
SK_WCB = [0.0, 0.0, -0.075, -0.075]
SK_WSA = [0.22, 0.22, 0.15, 0.15]
SK_WSB = [0.105, 0.105, 0.09, 0.09]
SK_WSW = [-0.25, -0.25, -0.35, -0.35]
SK_WING = 0
SK_FUR = 0
SK_COCOON = 0
SK_SILK = 0
SK_BACK = 0
SK_BACK_HUE = 0
SK_BACK_COLS = 0
SK_BACK_LIGHT = []
SK_BACK_WARM = []


# a moth's wing k (0, 1 fore, 2, 3 hind): an ellipse swept back along the body, in a plane through its root
def sk_wing(x, y, z, k):
	px = x - SK_WOX[k]; py = y - SK_WOY[k]; pz = z - SK_WOZ[k]
	a = px * SK_WSX[k] + py * SK_WSY[k] + pz * SK_WSZ[k]
	b = px * SK_MFX + py * SK_MFY + pz * SK_MFZ
	c = px * SK_WNX[k] + py * SK_WNY[k] + pz * SK_WNZ[k]
	u = (a - SK_WCA[k]) / SK_WSA[k]
	v = (b - SK_WCB[k] - a * SK_WSW[k]) / SK_WSB[k]
	d = (sqrt(u * u + v * v) - 1.0) * SK_WSB[k]
	dc = abs(c) - 0.007
	return d if d > dc else dc


# the wing's pattern: 0 gold, 1 umber vein or band, 2 black ring, 3 iris, 4 pupil, 5 violet margin
def sk_wing_tone(x, y, z, k):
	px = x - SK_WOX[k]; py = y - SK_WOY[k]; pz = z - SK_WOZ[k]
	a = px * SK_WSX[k] + py * SK_WSY[k] + pz * SK_WSZ[k]
	b = px * SK_MFX + py * SK_MFY + pz * SK_MFZ
	u = (a - SK_WCA[k]) / SK_WSA[k]
	v = (b - SK_WCB[k] - a * SK_WSW[k]) / SK_WSB[k]
	r = sqrt(u * u + v * v)
	if r > 0.82:
		return 5
	fore = k < 2
	eu = u - (0.3 if fore else 0.05)
	ev = v - (-0.08 if fore else 0.05)
	e = sqrt(eu * eu + ev * ev) / (0.3 if fore else 0.45)
	if e < 0.34:
		return 4
	if e < 0.74:
		return 3
	if e < 1.0:
		return 2
	band = u * 0.8 + v * 0.5 + (0.45 if fore else -0.2)
	if band > -0.1 and band < 0.1:
		return 1
	vein = sin(atan2(v, u + 1.05) * 11.0)
	return 1 if vein > 0.9 else 0


# the moth: a furred body and head, four wings, and the cocoon the spider wraps around it
def sk_moth(x, y, z):
	ox = x - SK_MX; oy = y - SK_MY; oz = z - SK_MZ
	f = ox * SK_MFX + oy * SK_MFY + oz * SK_MFZ
	fc = -0.25 if f < -0.25 else (0.09 if f > 0.09 else f)
	qx = ox - SK_MFX * fc; qy = oy - SK_MFY * fc; qz = oz - SK_MFZ * fc
	d = sqrt(qx * qx + qy * qy + qz * qz) - (0.066 + (fc * 0.12 if fc < 0.0 else 0.0))
	hx = ox - SK_MFX * 0.15; hy = oy - SK_MFY * 0.15; hz = oz - SK_MFZ * 0.15
	dh = sqrt(hx * hx + hy * hy + hz * hz) - 0.058
	d = d if d < dh else dh
	if SK_WSC > 0.05:
		for k in range(4):
			dw = sk_wing(x, y, z, k)
			d = d if d < dw else dw
	if SK_COC > 0.0:
		cf = f + 0.07
		rr = ox * ox + oy * oy + oz * oz - f * f
		rr = 0.0 if rr < 0.0 else sqrt(rr)
		ca = 0.18 + 0.22 * SK_COC
		cb = 0.05 + 0.14 * SK_COC
		dc = (sqrt(cf * cf / (ca * ca) + rr * rr / (cb * cb)) - 1.0) * cb
		d = d if d < dc else dc
	return d


# which part of the moth is nearest: -1 body, -2 cocoon, else the wing's index
def sk_moth_part(x, y, z):
	ox = x - SK_MX; oy = y - SK_MY; oz = z - SK_MZ
	f = ox * SK_MFX + oy * SK_MFY + oz * SK_MFZ
	fc = -0.25 if f < -0.25 else (0.09 if f > 0.09 else f)
	qx = ox - SK_MFX * fc; qy = oy - SK_MFY * fc; qz = oz - SK_MFZ * fc
	best = sqrt(qx * qx + qy * qy + qz * qz) - (0.066 + (fc * 0.12 if fc < 0.0 else 0.0))
	hx = ox - SK_MFX * 0.15; hy = oy - SK_MFY * 0.15; hz = oz - SK_MFZ * 0.15
	dh = sqrt(hx * hx + hy * hy + hz * hz) - 0.058
	best = best if best < dh else dh
	result = -1
	if SK_WSC > 0.05:
		for k in range(4):
			dw = sk_wing(x, y, z, k)
			if dw < best:
				best = dw
				result = k
	if SK_COC > 0.0:
		cf = f + 0.07
		rr = ox * ox + oy * oy + oz * oz - f * f
		rr = 0.0 if rr < 0.0 else sqrt(rr)
		ca = 0.18 + 0.22 * SK_COC
		cb = 0.05 + 0.14 * SK_COC
		dc = (sqrt(cf * cf / (ca * ca) + rr * rr / (cb * cb)) - 1.0) * cb
		if dc < best:
			result = -2
	return result


# capsules [first, last) against best: a capsule whose bounding sphere cannot beat best is skipped without a root
def sk_capsules(x, y, z, first, last, best):
	cmx = SK_CMX; cmy = SK_CMY; cmz = SK_CMZ; cbr = SK_CB
	cax = SK_CAX; cay = SK_CAY; caz = SK_CAZ
	cdx = SK_CDX; cdy = SK_CDY; cdz = SK_CDZ; cid = SK_CID; cr = SK_CR
	for i in range(first, last):
		mx = x - cmx[i]
		my = y - cmy[i]
		mz = z - cmz[i]
		reach = best + cbr[i]
		if mx * mx + my * my + mz * mz > reach * reach:
			continue
		pax = x - cax[i]
		pay = y - cay[i]
		paz = z - caz[i]
		dx = cdx[i]
		dy = cdy[i]
		dz = cdz[i]
		hh = (pax * dx + pay * dy + paz * dz) * cid[i]
		hh = 0.0 if hh < 0.0 else (1.0 if hh > 1.0 else hh)
		ex = pax - dx * hh
		ey = pay - dy * hh
		ez = paz - dz * hh
		d = sqrt(ex * ex + ey * ey + ez * ez) - cr[i]
		best = d if d < best else best
	return best


def sk_glows(x, y, z):
	best = 99.0
	gx = SK_GX; gy = SK_GY; gz = SK_GZ; gr = SK_GR
	for i in range(SK_NG):
		dx = x - gx[i]
		dy = y - gy[i]
		dz = z - gz[i]
		d = sqrt(dx * dx + dy * dy + dz * dz) - gr[i]
		best = d if d < best else best
	return best


# the skull against best: an ellipsoid (up 1.18, forward sqrt(0.92)) with its jaw, eye sockets and nose cut in. It is no
# nearer than sqrt(0.92) of the distance to its center less SK_SKB (the jaw's reach), so it is skipped when that cannot beat best
def sk_skull(x, y, z, best):
	hx = x - SK_HX
	hy = y - SK_HY
	hz = z - SK_HZ
	if sqrt(hx * hx + hy * hy + hz * hz) * 0.959 - SK_SKB >= best:
		return best
	cu = hx * SK_PUX + hy * SK_PUY + hz * SK_PUZ
	cf = hx * SK_PFX + hy * SK_PFY + hz * SK_PFZ
	cr = hx * SK_PRX + hy * SK_PRY + hz * SK_PRZ
	cu = cu * 1.18
	d = sqrt(cu * cu + cf * cf * 0.92 + cr * cr) - SK_HR
	jx = x - SK_JX
	jy = y - SK_JY
	jz = z - SK_JZ
	jr = jx * SK_PRX + jy * SK_PRY + jz * SK_PRZ
	jr = -0.085 if jr < -0.085 else (0.085 if jr > 0.085 else jr)
	qx = jx - SK_PRX * jr
	qy = jy - SK_PRY * jr
	qz = jz - SK_PRZ * jr
	dj = sqrt(qx * qx + qy * qy + qz * qz) - 0.09
	d = d if d < dj else dj
	gx = SK_GX; gy = SK_GY; gz = SK_GZ; gr = SK_GR
	for i in range(SK_NSOCK):
		sx = x - gx[i]
		sy = y - gy[i]
		sz = z - gz[i]
		c = gr[i] * 2.5 - sqrt(sx * sx + sy * sy + sz * sz)
		d = d if d > c else c
	nx = x - SK_NX; ny = y - SK_NY; nz = z - SK_NZ
	cn = 0.042 - sqrt(nx * nx + ny * ny + nz * nz)
	d = d if d > cn else cn
	return d if d < best else best


def sk_body(x, y, z):
	ax = x - SK_AX; ay = y - SK_AY; az = z - SK_AZ
	af = ax * SK_QFX + ay * SK_QFY + az * SK_QFZ
	au = ax * SK_QUX + ay * SK_QUY + az * SK_QUZ
	ar = ax * SK_PRX + ay * SK_PRY + az * SK_PRZ
	af = af * 0.74
	au = au * 1.04
	da = (sqrt(af * af + au * au + ar * ar) - SK_AR) * 0.74
	tx = x - SK_TX; ty = y - SK_TY; tz = z - SK_TZ
	dt = sqrt(tx * tx + ty * ty + tz * tz) - SK_TR
	k = 0.09
	hh = 0.5 + 0.5 * (dt - da) / k
	hh = 0.0 if hh < 0.0 else (1.0 if hh > 1.0 else hh)
	return dt + (da - dt) * hh - k * hh * (1.0 - hh)


# the crown's ring; its flames are the capsules [SK_CROWN0, SK_NC)
def sk_ring(x, y, z):
	ox = x - SK_CRX; oy = y - SK_CRY; oz = z - SK_CRZ
	cu = ox * SK_PUX + oy * SK_PUY + oz * SK_PUZ
	px = ox - SK_PUX * cu; py = oy - SK_PUY * cu; pz = oz - SK_PUZ * cu
	ring = sqrt(px * px + py * py + pz * pz) - 0.2
	return sqrt(ring * ring + cu * cu) - 0.028


def sk_gem(x, y, z):
	ox = x - SK_GMX; oy = y - SK_GMY; oz = z - SK_GMZ
	a = abs(ox * SK_GAX + oy * SK_GAY + oz * SK_GAZ)
	b = abs(ox * SK_PUX + oy * SK_PUY + oz * SK_PUZ)
	c = abs(ox * SK_GBX + oy * SK_GBY + oz * SK_GBZ)
	return (a + b * 0.62 + c - 0.075) * 0.54


# the creature along one ray: only the parts the ray passes near (groups: 1 body, 2 head and its eyes, 4 moth; cand: capsule
# indices, glows as 100 + index).
# A part the ray never enters changes the step lengths, never a hit.
def sk_spider(x, y, z, groups, cand, nc):
	d = 99.0
	if (groups & 1) != 0:
		d = sk_body(x, y, z)
	if (groups & 2) != 0:
		d = sk_skull(x, y, z, d)
		# the crown's ring and flames lie within SK_CRB of its center
		ox = x - SK_CRX; oy = y - SK_CRY; oz = z - SK_CRZ
		if sqrt(ox * ox + oy * oy + oz * oz) - SK_CRB < d:
			dr = sk_ring(x, y, z)
			d = d if d < dr else dr
			d = sk_capsules(x, y, z, SK_CROWN0, SK_NC, d)
		dg = sk_gem(x, y, z)
		d = d if d < dg else dg
		gx = SK_GX; gy = SK_GY; gz = SK_GZ; gr = SK_GR
		for e in range(SK_NSOCK):
			ex = x - gx[e]
			ey = y - gy[e]
			ez = z - gz[e]
			de = sqrt(ex * ex + ey * ey + ez * ez) - gr[e]
			d = d if d < de else de
	if (groups & 4) != 0:
		dm = sk_moth(x, y, z)
		d = d if d < dm else dm
	if nc > 0:
		gx = SK_GX; gy = SK_GY; gz = SK_GZ; gr = SK_GR
		cmx = SK_CMX; cmy = SK_CMY; cmz = SK_CMZ; cbr = SK_CB
		cax = SK_CAX; cay = SK_CAY; caz = SK_CAZ
		cdxa = SK_CDX; cdya = SK_CDY; cdza = SK_CDZ; cid = SK_CID; cr = SK_CR
		for k in range(nc):
			c = cand[k]
			if c >= 100:
				g = c - 100
				gx2 = x - gx[g]
				gy2 = y - gy[g]
				gz2 = z - gz[g]
				dg2 = sqrt(gx2 * gx2 + gy2 * gy2 + gz2 * gz2) - gr[g]
				d = d if d < dg2 else dg2
			else:
				mx = x - cmx[c]
				my = y - cmy[c]
				mz = z - cmz[c]
				reach = d + cbr[c]
				if mx * mx + my * my + mz * mz > reach * reach:
					continue
				pax = x - cax[c]
				pay = y - cay[c]
				paz = z - caz[c]
				cdx = cdxa[c]
				cdy = cdya[c]
				cdz = cdza[c]
				hh = (pax * cdx + pay * cdy + paz * cdz) * cid[c]
				hh = 0.0 if hh < 0.0 else (1.0 if hh > 1.0 else hh)
				ex = pax - cdx * hh
				ey = pay - cdy * hh
				ez = paz - cdz * hh
				dc = sqrt(ex * ex + ey * ey + ez * ez) - cr[c]
				d = d if d < dc else dc
	return d


# the ray's candidate parts cand[first, last) against best: capsule indices in ascending order, then glows as 100 + index
def sk_candidates(x, y, z, cand, first, last, d):
	gx = SK_GX; gy = SK_GY; gz = SK_GZ; gr = SK_GR
	cmx = SK_CMX; cmy = SK_CMY; cmz = SK_CMZ; cbr = SK_CB
	cax = SK_CAX; cay = SK_CAY; caz = SK_CAZ
	cdxa = SK_CDX; cdya = SK_CDY; cdza = SK_CDZ; cid = SK_CID; cr = SK_CR
	for k in range(first, last):
		c = cand[k]
		if c >= 100:
			g = c - 100
			gx2 = x - gx[g]
			gy2 = y - gy[g]
			gz2 = z - gz[g]
			dg2 = sqrt(gx2 * gx2 + gy2 * gy2 + gz2 * gz2) - gr[g]
			d = d if d < dg2 else dg2
		else:
			mx = x - cmx[c]
			my = y - cmy[c]
			mz = z - cmz[c]
			reach = d + cbr[c]
			if mx * mx + my * my + mz * mz > reach * reach:
				continue
			pax = x - cax[c]
			pay = y - cay[c]
			paz = z - caz[c]
			cdx = cdxa[c]
			cdy = cdya[c]
			cdz = cdza[c]
			hh = (pax * cdx + pay * cdy + paz * cdz) * cid[c]
			hh = 0.0 if hh < 0.0 else (1.0 if hh > 1.0 else hh)
			ex = pax - cdx * hh
			ey = pay - cdy * hh
			ez = paz - cdz * hh
			dc = sqrt(ex * ex + ey * ey + ez * ez) - cr[c]
			d = d if d < dc else dc
	return d


# does the ray (origin o, unit direction d) pass within r of c ahead of the origin
def sk_ray_near(ox, oy, oz, dx, dy, dz, cx, cy, cz, r):
	vx = cx - ox
	vy = cy - oy
	vz = cz - oz
	tc = vx * dx + vy * dy + vz * dz
	if tc < -r:
		return False
	return vx * vx + vy * vy + vz * vz - tc * tc < r * r


# 1 bone, 2 chitin, 3 glow, 4 marking, 5 ruby, 6 moth at a point the march stopped on: the nearest wins, ties to the earlier.
# A capsule the ray passed no nearer than 0.01 to is farther than the march's reach from every point on it, so only the ray's
# candidates are tried (cand[0, nbone) bone, then chitin up to ncap)
def sk_material(x, y, z, cand, nbone, ncap):
	best = 99.0
	m = 0
	bone = sk_candidates(x, y, z, cand, 0, nbone, sk_skull(x, y, z, best))
	if bone < best:
		best = bone
		m = 1
	db = sk_body(x, y, z)
	chit = sk_candidates(x, y, z, cand, nbone, ncap, db if db < best else best)
	if chit < best:
		best = chit
		m = 2
	de = sk_glows(x, y, z)
	de = de if de < best else best
	ox = x - SK_CRX; oy = y - SK_CRY; oz = z - SK_CRZ
	if sqrt(ox * ox + oy * oy + oz * oz) - SK_CRB < de:
		dr = sk_ring(x, y, z)
		de = de if de < dr else dr
		de = sk_capsules(x, y, z, SK_CROWN0, SK_NC, de)
	if de < best:
		best = de
		m = 3
	dgm = sk_gem(x, y, z)
	if dgm < best:
		best = dgm
		m = 5
	if SK_MOTH:
		mx = x - SK_MX; my = y - SK_MY; mz = z - SK_MZ
		if sqrt(mx * mx + my * my + mz * mz) - SK_MB < best:
			dm = sk_moth(x, y, z)
			if dm < best:
				m = 6
	if m == 2 and chit == db:
		# chevrons crawl down the abdomen's back, a stripe runs along its spine
		ax = x - SK_AX; ay = y - SK_AY; az = z - SK_AZ
		au = ax * SK_QUX + ay * SK_QUY + az * SK_QUZ
		ar = ax * SK_PRX + ay * SK_PRY + az * SK_PRZ
		af = ax * SK_QFX + ay * SK_QFY + az * SK_QFZ
		ar2 = -ar if ar < 0.0 else ar
		if au > 0.07 and af > -0.33 and af < 0.2:
			v = af * 7.0 - ar2 * 9.0 + SK_T * 1.6
			if v - floor(v) < 0.3 or ar2 < 0.018:
				m = 4
	return m


# where the ray (origin o, unit direction d) is inside the sphere (c, r): widens [t_near, t_far] to hold that stretch;
# the reference's float& pair comes back as a tuple
def sk_ray_span(ox, oy, oz, dx, dy, dz, cx, cy, cz, r, t_near, t_far):
	vx = cx - ox
	vy = cy - oy
	vz = cz - oz
	tc = vx * dx + vy * dy + vz * dz
	h = r * r - (vx * vx + vy * vy + vz * vz - tc * tc)
	if h <= 0.0:
		return t_near, t_far
	h = sqrt(h)
	t_near = tc - h if tc - h < t_near else t_near
	t_far = tc + h if tc + h > t_far else t_far
	return t_near, t_far


# a pixel's palette index plus its depth (0..254) above bit 10, for the silk and the embers drawn over it, or -1 where the ray
# meets neither the creature nor the moth; row: the row's candidate parts (see sk_row)
def sk_trace(dx, dy, dz, px, py, cand, row, nrow):
	ox = SK_EX
	oy = SK_EY
	oz = SK_EZ
	groups = 0
	nc = 0
	nbone = 0
	ncap = 0
	if sk_ray_near(ox, oy, oz, dx, dy, dz, SK_BGX, SK_BGY, SK_BGZ, 0.7):
		groups = 1
	if sk_ray_near(ox, oy, oz, dx, dy, dz, SK_HGX, SK_HGY, SK_HGZ, 0.74):
		groups = groups + 2
	if SK_MOTH and sk_ray_near(ox, oy, oz, dx, dy, dz, SK_MX, SK_MY, SK_MZ, SK_MB):
		groups = groups + 4
	if nrow > 0:
		cmx = SK_CMX; cmy = SK_CMY; cmz = SK_CMZ; cbr = SK_CB
		gx = SK_GX; gy = SK_GY; gz = SK_GZ; gr = SK_GR
		nbone_caps = SK_NBONE
		for k in range(nrow):
			c = row[k]
			if c < 100:
				if sk_ray_near(ox, oy, oz, dx, dy, dz, cmx[c], cmy[c], cmz[c], cbr[c] + 0.01):
					cand[nc] = c
					nc = nc + 1
					ncap = nc
					if c < nbone_caps:
						nbone = nc
			else:
				g = c - 100
				if sk_ray_near(ox, oy, oz, dx, dy, dz, gx[g], gy[g], gz[g], gr[g] + 0.01):
					cand[nc] = c
					nc = nc + 1
	if groups == 0 and nc == 0:
		return -1
	t = 99.0
	t_far = 0.0
	if (groups & 3) != 0 or nc != 0:
		t, t_far = sk_ray_span(ox, oy, oz, dx, dy, dz, SK_BCX, SK_BCY, SK_BCZ, SK_BR, t, t_far)
	if (groups & 4) != 0:
		t, t_far = sk_ray_span(ox, oy, oz, dx, dy, dz, SK_MX, SK_MY, SK_MZ, SK_MB, t, t_far)
	t = 0.04 if t < 0.04 else t
	hit = False
	i = 0
	while i < 48 and t < t_far:
		d = sk_spider(ox + dx * t, oy + dy * t, oz + dz * t, groups, cand, nc)
		reach = 0.001 + t * 0.0015
		if d < (reach if reach < 0.0085 else 0.0085):
			hit = True
			break
		t += d * 0.85
		i = i + 1
	if not hit:
		return -1
	dith = SK_BAYER[(py % 4) * 4 + px % 4] / 16.0 - 0.47
	depth = int(t * 18.0)
	depth = (254 if depth > 254 else depth) * 1024
	x = ox + dx * t
	y = oy + dy * t
	z = oz + dz * t
	m = sk_material(x, y, z, cand, nbone, ncap)
	fog = 1.0 - t / 14.0
	fog = 0.0 if fog < 0.0 else fog
	lx = SK_LX - x
	ly = SK_LY - y
	lz = SK_LZ - z
	ld2 = lx * lx + ly * ly + lz * lz
	lil = 1.0 / sqrt(ld2 + 0.0001)
	lx *= lil
	ly *= lil
	lz *= lil
	if m == 3:
		e = int((5.4 + SK_FLICK) * SK_WAKE + dith)
		for k in range(SK_NSOCK):
			ex = x - SK_GX[k]; ey = y - SK_GY[k]; ez = z - SK_GZ[k]
			el = sqrt(ex * ex + ey * ey + ez * ez)
			if el < SK_GR[k] + 0.012:
				# an eye burns red at its rim and yellow where it faces the viewer, with a
				# wet glint where its sphere mirrors the key light
				facing = -(ex * dx + ey * dy + ez * dz) / (el + 0.0000001)
				e = int((2.6 + 3.2 * facing * facing + SK_FLICK) * SK_WAKE + dith)
				hx = SK_KX - dx; hy = SK_KY - dy; hz = SK_KZ - dz
				hl = sqrt(hx * hx + hy * hy + hz * hz) * el + 0.0000001
				if (ex * hx + ey * hy + ez * hz) / hl > 0.93 and SK_WAKE > 0.5:
					e = 7
		return SK_EYE + (0 if e < 0 else (7 if e > 7 else e)) + depth
	if m == 5:
		ox = x - SK_GMX; oy = y - SK_GMY; oz = z - SK_GMZ
		a = ox * SK_GAX + oy * SK_GAY + oz * SK_GAZ
		b = ox * SK_PUX + oy * SK_PUY + oz * SK_PUZ
		c = ox * SK_GBX + oy * SK_GBY + oz * SK_GBZ
		sa = -1.0 if a < 0.0 else 1.0
		sb = -1.0 if b < 0.0 else 1.0
		sc = -1.0 if c < 0.0 else 1.0
		gnx = SK_GAX * sa + SK_PUX * sb * 0.62 + SK_GBX * sc
		gny = SK_GAY * sa + SK_PUY * sb * 0.62 + SK_GBY * sc
		gnz = SK_GAZ * sa + SK_PUZ * sb * 0.62 + SK_GBZ * sc
		gl = 1.0 / sqrt(gnx * gnx + gny * gny + gnz * gnz)
		face = -(gnx * dx + gny * dy + gnz * dz) * gl
		face = 0.0 if face < 0.0 else face
		facet = (1 if sa > 0.0 else 0) + (2 if sb > 0.0 else 0) + (4 if sc > 0.0 else 0)
		twinkle = sin(SK_T * 9.0 + facet * 2.3)
		lit = 0.25 + face * face * 0.6 + (0.6 if twinkle > 0.85 else 0.0)
		r = int(lit * 7.0 + dith + 0.5)
		return SK_GEM + (0 if r < 0 else (7 if r > 7 else r)) + depth
	hstep = 0.0015
	n0 = sk_spider(x + hstep, y - hstep, z - hstep, groups, cand, nc)
	n1 = sk_spider(x - hstep, y - hstep, z + hstep, groups, cand, nc)
	n2 = sk_spider(x - hstep, y + hstep, z - hstep, groups, cand, nc)
	n3 = sk_spider(x + hstep, y + hstep, z + hstep, groups, cand, nc)
	nx = n0 - n1 - n2 + n3
	ny = -n0 - n1 + n2 + n3
	nz = -n0 + n1 - n2 + n3
	nl = 1.0 / sqrt(nx * nx + ny * ny + nz * nz + 0.0000001)
	nx *= nl
	ny *= nl
	nz *= nl
	vd = -(nx * dx + ny * dy + nz * dz)
	vd = 0.0 if vd < 0.0 else vd
	up = nx * SK_PUX + ny * SK_PUY + nz * SK_PUZ
	up = 0.0 if up < 0.0 else up
	rim = 1.0 - vd
	rim = rim * rim * rim
	eyel = nx * lx + ny * ly + nz * lz
	eyel = 0.0 if eyel < 0.0 else eyel / (1.0 + ld2 * 6.0)
	key = nx * SK_KX + ny * SK_KY + nz * SK_KZ
	if m == 6:
		part = sk_moth_part(x, y, z)
		if part >= 0:
			# the wings glow through: lit from either side
			tl = 0.5 + 0.55 * (-key if key < 0.0 else key) + eyel * 0.6
			tone = sk_wing_tone(x, y, z, part)
			if tone == 3 and part >= 2:
				tone = 6
			l = int(tl * fog * 7.0 + dith + 0.5)
			return SK_WING + tone * 8 + (0 if l < 0 else (7 if l > 7 else l)) + depth
		if part == -2:
			# the cocoon: silk wound in bands that turn as the spider spins it
			ox2 = x - SK_MX; oy2 = y - SK_MY; oz2 = z - SK_MZ
			f = ox2 * SK_MFX + oy2 * SK_MFY + oz2 * SK_MFZ
			ang = atan2(ox2 * SK_MUX + oy2 * SK_MUY + oz2 * SK_MUZ, ox2 * SK_MRX + oy2 * SK_MRY + oz2 * SK_MRZ)
			band = sin(f * 70.0 + ang * 2.0 + SK_SPIN * 2.0)
			cl = (0.2 + vd * 0.45 + rim * 0.35 + (key * 0.25 if key > 0.0 else 0.0)) * (0.72 + 0.28 * band)
			l = int(cl * fog * 9.0 + dith + 0.5)
			return SK_COCOON + (0 if l < 0 else (9 if l > 9 else l)) + depth
		fl = 0.12 + vd * 0.55 + rim * 0.25 + eyel * 0.5
		l = int(fl * fog * 7.0 + dith * 1.6 + 0.5)
		return SK_FUR + (0 if l < 0 else (7 if l > 7 else l)) + depth
	if m == 1:
		lit = 0.1 + vd * (0.55 + SK_GLARE * 0.4) + up * 0.22 + rim * 0.25
		so = 99.0
		for k in range(SK_NSOCK):
			ox2 = x - SK_GX[k]; oy2 = y - SK_GY[k]; oz2 = z - SK_GZ[k]
			q = sqrt(ox2 * ox2 + oy2 * oy2 + oz2 * oz2) - (SK_GR[k] if SK_GR[k] < 0.03 else 0.03) * 2.5
			so = q if q < so else so
		if so < 0.012:
			lit = lit * 0.15 + 0.07 * (1.0 + SK_FLICK)
		nsx = x - SK_NX; nsy = y - SK_NY; nsz = z - SK_NZ
		if nsx * nsx + nsy * nsy + nsz * nsz < 0.0025:
			lit = lit * 0.2
		jx = x - SK_JX; jy = y - SK_JY; jz = z - SK_JZ
		jf = jx * SK_PFX + jy * SK_PFY + jz * SK_PFZ
		ju = jx * SK_PUX + jy * SK_PUY + jz * SK_PUZ
		if jf > 0.045 and ju > -0.05 and ju < 0.042:
			tr = (jx * SK_PRX + jy * SK_PRY + jz * SK_PRZ) * 44.0
			if tr - floor(tr) < 0.24 or (ju > -0.008 and ju < 0.006):
				lit = lit * 0.22
		b = int(lit * fog * 11.0 + dith + 0.5)
		return SK_BONE + (0 if b < 0 else (11 if b > 11 else b)) + depth
	# a glossy highlight of the key light
	hx = SK_KX - dx; hy = SK_KY - dy; hz = SK_KZ - dz
	hl = 1.0 / sqrt(hx * hx + hy * hy + hz * hz + 0.0000001)
	spec = (nx * hx + ny * hy + nz * hz) * hl
	spec = 0.0 if spec < 0.0 else spec
	spec = spec * spec
	spec = spec * spec
	spec = spec * spec
	spec = spec * spec
	if m == 4:
		k = int((0.45 + vd * 0.45 + 0.3 * sin(SK_T * 4.0) + spec * 0.6) * 5.0 + dith + 0.5)
		return SK_MARK + (0 if k < 0 else (5 if k > 5 else k)) + depth
	# beetle iridescence: the shell turns from wine through violet to teal toward its rim
	lit = 0.1 + vd * 0.42 + rim * 0.45 + eyel * 0.9 + (key * 0.3 if key > 0.0 else 0.0) + spec * 1.1
	band = 0 if vd > 0.68 else (1 if vd > 0.36 else 2)
	c = int(lit * fog * 11.0 + dith + 0.5)
	return SK_CHIT + band * 12 + (0 if c < 0 else (11 if c > 11 else c)) + depth


# the vault behind the web at every fourth traced pixel, as light and the creature's warm aura: a ribbed dome about the hub
# with a pale oculus at its crown, a ring of pillars nearer the eye, mist drifting across, a faint ring leaving on each kick
def sk_backdrop(dim, flash):
	global SK_BACK_COLS
	cols = SK_W // 4 + 2
	rows = SK_H // 4 + 2
	SK_BACK_COLS = cols
	back_light = SK_BACK_LIGHT
	back_warm = SK_BACK_WARM
	n = cols * rows
	# array::resize keeps the head and fills the tail with 0.0
	for arr in (back_light, back_warm):
		if len(arr) < n:
			arr.extend([0.0] * (n - len(arr)))
		else:
			del arr[n:]
	ex = SK_EX; ey = SK_EY; ez = SK_EZ
	fx = SK_FX; fy = SK_FY; fz = SK_FZ
	rx = SK_RX; ry = SK_RY; rz = SK_RZ
	ux = SK_UX; uy = SK_UY; uz = SK_UZ
	sk_t = SK_T
	sk_w = SK_W
	sk_h = SK_H
	zoom = SK_ZOOM / (sk_h * 0.5)
	ee = ex * ex + ey * ey + ez * ez
	exz = ex * ex + ez * ez - 21.16
	vx = SK_BCX - ex
	vy = SK_BCY - ey
	vz = SK_BCZ - ez
	vv = vx * vx + vy * vy + vz * vz
	ring = SK_KAGE * 1.8
	ring_gain = (0.7 - SK_KAGE) * 0.16 * SK_WAKE if SK_KAGE < 0.7 else 0.0
	aura_gain = SK_WAKE * (0.75 + 0.25 * SK_KICK)
	for j in range(rows):
		v = (sk_h * 0.5 - j * 4 - 0.5) * zoom
		for i in range(cols):
			u = (i * 4 + 0.5 - sk_w * 0.5) * zoom
			dx = fx + rx * u + ux * v
			dy = fy + ry * u + uy * v
			dz = fz + rz * u + uz * v
			il = 1.0 / sqrt(dx * dx + dy * dy + dz * dz)
			dx *= il
			dy *= il
			dz *= il
			ed = ex * dx + ey * dy + ez * dz
			t = (sqrt(ed * ed - ee + 49.0) - ed) / 7.0
			qx = ex / 7.0 + dx * t
			qy = ey / 7.0 + dy * t
			qz = ez / 7.0 + dz * t
			phi = atan2(qx, -qz)
			rib = abs(sin(phi * 4.0))
			rib = 1.0 - rib * 4.0 if rib < 0.25 else 0.0
			light = 0.2 + 0.14 * qy + rib * (0.06 + 0.06 * qy)
			oculus = (qy - 0.8) * 6.0
			light += (0.0 if oculus < 0.0 else (1.0 if oculus > 1.0 else oculus)) * 0.3
			light += 0.05 * sin(phi * 3.0 + qy * 4.0 + sk_t * 0.25) * sin(qy * 6.0 - sk_t * 0.17 + phi)
			off = abs(sqrt(qx * qx + qy * qy) - ring)
			if off < 0.12:
				light += (1.0 - off / 0.12) * ring_gain
			a = dx * dx + dz * dz
			b = ex * dx + ez * dz
			disc = b * b - a * exz
			if disc > 0.0 and a > 0.000001:
				tp = (sqrt(disc) - b) / a
				gap = abs(cos(atan2(ex + dx * tp, -ez - dz * tp) * 4.0))
				if gap < 0.5:
					light *= 0.45 + gap * 1.1
			light = light * dim + flash
			tc = vx * dx + vy * dy + vz * dz
			perp = vv - tc * tc
			warm = 1.3 - sqrt(0.0 if perp < 0.0 else perp)
			warm = 0.0 if warm < 0.0 else (1.0 if warm > 1.0 else warm)
			back_light[j * cols + i] = 0.0 if light < 0.0 else (1.0 if light > 1.0 else light)
			back_warm[j * cols + i] = warm * warm * aura_gain


# one traced row of the vault, from the grid sk_backdrop fills
def sk_backdrop_row(y, out):
	back_light = SK_BACK_LIGHT
	back_warm = SK_BACK_WARM
	bayer_t = SK_BAYER
	j = y // 4
	fy = (y - j * 4) * 0.25
	upper = j * SK_BACK_COLS
	lower = upper + SK_BACK_COLS
	bayer = (y % 4) * 4
	base = SK_BACK + SK_BACK_HUE + 255 * 1024
	ww = SK_W
	x = 0
	i = 0
	while x < ww:
		l0 = back_light[upper + i]
		l1 = back_light[upper + i + 1]
		w0 = back_warm[upper + i]
		w1 = back_warm[upper + i + 1]
		l0 += (back_light[lower + i] - l0) * fy
		l1 += (back_light[lower + i + 1] - l1) * fy
		w0 += (back_warm[lower + i] - w0) * fy
		w1 += (back_warm[lower + i + 1] - w1) * fy
		k = 0
		while k < 4 and x < ww:
			dith = bayer_t[bayer + k] / 16.0
			level = int((l0 + (l1 - l0) * k * 0.25) * 15.0 + dith)
			warm = int((w0 + (w1 - w0) * k * 0.25) * 4.0 + dith)
			out[x] = base + (4 if warm > 4 else warm) * 16 + (15 if level > 15 else level)
			x = x + 1
			k += 1
		i = i + 1


# the pixels [first, last) of a row whose rays (A + R u, A = F + U v) pass within r of c: widens the span to hold them;
# the reference's int& pair comes back as a tuple
def sk_row_span(ax, ay, az, cx, cy, cz, r, zoom, first, last):
	vx = cx - SK_EX
	vy = cy - SK_EY
	vz = cz - SK_EZ
	q = vx * vx + vy * vy + vz * vz - r * r
	a = vx * ax + vy * ay + vz * az
	b = vx * SK_RX + vy * SK_RY + vz * SK_RZ
	k = q - b * b
	if k < 0.0001:
		return 0, SK_W
	disc = a * a * b * b - k * (q * (ax * ax + ay * ay + az * az) - a * a)
	if disc <= 0.0 or a <= 0.0:
		return first, last
	disc = sqrt(disc)
	lo = floor((a * b - disc) / (k * zoom) + SK_W * 0.5) - 1
	hi = floor((a * b + disc) / (k * zoom) + SK_W * 0.5) + 2
	first = lo if lo < first else first
	last = hi if hi > last else last
	return first, last


def sk_row(y):
	out = [0] * SK_W
	sk_backdrop_row(y, out)
	ww = SK_W
	half_h = SK_H * 0.5
	zoom = SK_ZOOM / half_h
	v = (half_h - y - 0.5) * zoom
	ax = SK_FX + SK_UX * v
	ay = SK_FY + SK_UY * v
	az = SK_FZ + SK_UZ * v
	first = ww
	last = 0
	first, last = sk_row_span(ax, ay, az, SK_BCX, SK_BCY, SK_BCZ, SK_BR, zoom, first, last)
	if SK_MOTH:
		first, last = sk_row_span(ax, ay, az, SK_MX, SK_MY, SK_MZ, SK_MB, zoom, first, last)
	first = 0 if first < 0 else first
	last = ww if last > ww else last
	if first >= last:
		return out
	# the row's rays all lie in the plane through the eye spanned by R and F + U v: a capsule (0..SK_CROWN0) or a leg glow
	# (100 + g) farther from that plane than its reach passes near none of them, so the pixels test only the rest
	rx = SK_RX; ry = SK_RY; rz = SK_RZ
	ex = SK_EX; ey = SK_EY; ez = SK_EZ
	nx = ry * az - rz * ay
	ny = rz * ax - rx * az
	nz = rx * ay - ry * ax
	nl = 1.0 / sqrt(nx * nx + ny * ny + nz * nz)
	row = []
	cmx = SK_CMX; cmy = SK_CMY; cmz = SK_CMZ; cbr = SK_CB
	for c in range(SK_CROWN0):
		off = ((cmx[c] - ex) * nx + (cmy[c] - ey) * ny + (cmz[c] - ez) * nz) * nl
		if abs(off) < cbr[c] + 0.02:
			row.append(c)
	gx = SK_GX; gy = SK_GY; gz = SK_GZ; gr = SK_GR
	for g in range(3, SK_NG):
		off = ((gx[g] - ex) * nx + (gy[g] - ey) * ny + (gz[g] - ez) * nz) * nl
		if abs(off) < gr[g] + 0.02:
			row.append(100 + g)
	nrow = len(row)
	cand = [0] * 64
	for x in range(first, last):
		u = (x + 0.5 - ww * 0.5) * zoom
		dx = ax + rx * u
		dy = ay + ry * u
		dz = az + rz * u
		il = 1.0 / sqrt(dx * dx + dy * dy + dz * dz)
		traced = sk_trace(dx * il, dy * il, dz * il, x, y, cand, row, nrow)
		if traced >= 0:
			out[x] = traced
	return out


class Ossuary(Scene):
	def __init__(self):
		Scene.__init__(self)
		self.bx = 0.0; self.by = 0.0; self.bz = 0.0
		self.fx = 0.0; self.fy = 1.0; self.fz = 0.0
		self.ux = 0.0; self.uy = 0.0; self.uz = 1.0
		self.rx = 1.0; self.ry = 0.0; self.rz = 0.0
		self.ex = 0.0; self.ey = 0.0; self.ez = 0.0
		self.cux = 0.0; self.cuy = 1.0; self.cuz = 0.0
		self.lkx = 0.0; self.lky = 0.0; self.lkz = 0.0
		self.placed = False
		self.footx = []
		self.footy = []
		self.footz = []
		self.fromx = []
		self.fromy = []
		self.fromz = []
		self.tox = []
		self.toy = []
		self.toz = []
		self.stept = []
		self.legfo = [0.3, 0.1, -0.1, -0.3]
		self.legreach = [0.42, 0.14, -0.08, -0.36]
		self.leglift = [1.25, 0.8, 0.25, 0.0]
		self.ppx = []
		self.ppy = []
		self.ppz = []
		self.pvx = []
		self.pvy = []
		self.pvz = []
		self.plife = []
		self.prate = []
		self.depth = []
		self.np = 120
		self.pnext = 0
		self.l1 = 0.5
		self.l2 = 0.52
		self.tt = 0.0
		self.spawn_acc = 0.0
		self.stance = 0.5
		# the story on the cue's bars: asleep at the hub (0-1), awake on bar 2, the spokes thrown and the spiral run by bar 4,
		# the moth strikes on bar 6, the cocoon closes on bar 8, the hunter stalks the viewer through bar 9 and lunges on bar 10
		self.t_wake = 3.871
		self.t_spokes = 4.355
		self.t_spoke_gap = 0.0806
		self.t_out = 5.5
		self.t_spiral0 = 5.806
		self.t_spiral1 = 7.742
		self.t_home = 8.25
		self.t_moth = 8.7
		self.t_impact = 11.613
		self.t_rush = 11.85
		self.t_reach = 12.5
		self.t_turn = 12.85
		self.t_wrap0 = 12.95
		self.t_wrap1 = 15.484
		self.t_face = 15.7
		self.t_stalk = 17.419
		self.t_lunge = 19.355
		self.nspoke = 14
		self.spoke_reach = 4.4
		self.web_rim = 1.55
		self.lay_from = 2.24
		self.lay_lead = 0.7
		self.ring_speed = 3.0
		self.spoke_ang = []
		self.spa = []
		self.spb = []
		self.lay_turn = []
		self.pa = 0.0; self.pb = 0.0; self.psi = 1.5707963
		self.spd = 0.0
		self.moth_a = 1.2; self.moth_b = 0.7
		self.qa = 0.0; self.qb = 0.0
		self.wx = 0.0; self.wy = 0.0; self.wz = 0.0
		self.qsx = 0.0; self.qsy = 0.0; self.qzc = 0.0
		self.mox = 0.0; self.moy = 0.0; self.moz = 0.0
		self.mfx = 0.0; self.mfy = 1.0; self.mfz = 0.0
		self.mux = 0.0; self.muy = 0.0; self.muz = 1.0
		self.mrx = 1.0; self.mry = 0.0; self.mrz = 0.0
		self.flap = 0.0; self.wsc = 1.0; self.coc = 0.0; self.spin = 0.0
		self.pixel_budget = 16000

	def build(self):
		global SK_BACK, SK_BONE, SK_CHIT, SK_EYE, SK_MARK, SK_SPARK, SK_GEM, SK_WING, SK_FUR, SK_COCOON, SK_SILK
		self.name = "OSSUARY"
		self.handle = "eight legs of inverse kinematics . an orb web . a moth that came too close"
		self.replay = 23.5
		pal = self.pal
		# the vault: three chord hues, each five steps of the creature's warm aura by sixteen levels of light
		SK_BACK = pal.size()
		vault = [52.0, 60.0, 112.0, 72.0, 52.0, 110.0, 40.0, 72.0, 104.0]
		for hue in range(3):
			for warm in range(5):
				for level in range(16):
					f = level / 15.0
					pal.add(4.0 + vault[hue * 3] * f + warm * 16.0, 4.0 + vault[hue * 3 + 1] * f + warm * 2.0, 9.0 + vault[hue * 3 + 2] * f + warm * 4.0)
		SK_BONE = pal.ramp(12, 26.0, 20.0, 16.0, 250.0, 238.0, 206.0)
		SK_CHIT = pal.size()
		shell = [235.0, 40.0, 90.0, 150.0, 70.0, 255.0, 40.0, 230.0, 210.0]
		for band in range(3):
			for k in range(12):
				f = k / 11.0
				c = f * f * 0.85 + f * 0.15
				wh = (f - 0.8) * 3.5 if f > 0.8 else 0.0
				r = 5.0 + shell[band * 3] * c
				g = 2.0 + shell[band * 3 + 1] * c
				b = 7.0 + shell[band * 3 + 2] * c
				pal.add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh)
		# the eyes and the crown: three steps of dead ember, four of fire, then white
		SK_EYE = pal.size()
		pal.add(34.0, 12.0, 10.0)
		pal.add(96.0, 16.0, 8.0)
		pal.add(160.0, 22.0, 8.0)
		pal.ramp(4, 220.0, 30.0, 10.0, 255.0, 250.0, 190.0)
		pal.add(255.0, 255.0, 255.0)
		SK_MARK = pal.ramp(6, 90.0, 0.0, 8.0, 255.0, 200.0, 60.0)
		SK_SPARK = pal.size()
		for k in range(8):
			f = k / 7.0
			pal.add(90.0 + 165.0 * f, 8.0 + 120.0 * f * f + 120.0 * f * f * f * f, 4.0 + 40.0 * f * f * f + 180.0 * f * f * f * f * f * f)
		SK_GEM = pal.size()
		for k in range(8):
			f = k / 7.0
			wh = (f - 0.75) * 3.2 if f > 0.75 else 0.0
			r = 40.0 + 215.0 * f
			g = 0.0 + 30.0 * f * f
			b = 12.0 + 50.0 * f * f
			pal.add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh)
		# the moth's wing tones: gold, umber, the eyespot's black ring, cyan iris, pupil, violet margin, magenta iris
		SK_WING = pal.size()
		tones = [255.0, 186.0, 64.0, 120.0, 58.0, 28.0, 34.0, 10.0, 34.0, 60.0, 235.0, 255.0, 255.0, 245.0, 255.0, 160.0, 70.0, 230.0, 255.0, 50.0, 200.0]
		for tone in range(7):
			for l in range(8):
				f = (l + 1) / 8.0
				k = 0.1 + 0.9 * f
				pal.add(tones[tone * 3] * k, tones[tone * 3 + 1] * k, tones[tone * 3 + 2] * k)
		SK_FUR = pal.ramp(8, 22.0, 12.0, 8.0, 236.0, 196.0, 150.0)
		SK_COCOON = pal.gradient(10, [[16.0, 12.0, 28.0], [110.0, 100.0, 150.0], [230.0, 226.0, 248.0], [255.0, 255.0, 255.0]])
		# silk: eight iridescent hues, six levels each
		SK_SILK = pal.size()
		for hq in range(8):
			c = gfx.rainbow_rgb(hq * 0.7854 + 0.3)
			cr = c[0] * 0.12 + 196.0
			cg = c[1] * 0.12 + 204.0
			cb = c[2] * 0.12 + 222.0
			for l in range(6):
				f = 0.14 + 0.86 * (l + 1) / 6.0
				pal.add(cr * f, cg * f, cb * f)
		for i in range(8):
			self.footx.append(0.0); self.footy.append(0.0); self.footz.append(0.0)
			self.fromx.append(0.0); self.fromy.append(0.0); self.fromz.append(0.0)
			self.tox.append(0.0); self.toy.append(0.0); self.toz.append(0.0)
			self.stept.append(-1.0)
		for i in range(self.np):
			self.ppx.append(0.0); self.ppy.append(0.0); self.ppz.append(0.0)
			self.pvx.append(0.0); self.pvy.append(0.0); self.pvz.append(0.0)
			self.plife.append(0.0); self.prate.append(1.0)
		self.plan_web()

	def length_bars(self):
		return 12

	def hit_times(self):
		return [3.871, 7.742, 11.613, 15.484, 19.355]

	def hit_levels(self):
		return [1, 1, 2, 1, 2]

	# 0 until the hunter turns from the cocoon to the viewer, then 1
	def facing(self, t):
		return self.smooth((t - self.t_face) / 1.2)

	# 0 before the stalk, rising through bar 9 to 1 at the lunge
	def stalk(self, t):
		return self.smooth((t - self.t_stalk) / (self.t_lunge - self.t_stalk))

	# the lunge itself, 1 from just after bar 10's downbeat on
	def lunge(self, t):
		return self.smooth((t - self.t_lunge + 0.06) / 0.22)

	# the flare of the waking: 1 on bar 2's downbeat, dying away
	def flare(self, t):
		return 0.0 if t < self.t_wake else exp((self.t_wake - t) * 2.5)

	# how wide the jaw gapes: a yawn on waking, the lunge, and a snap on every kick of the close-up
	def gape(self, t):
		result = 0.6 * self.flare(t)
		s = t - self.t_lunge
		if s > -0.1:
			result = exp(0.0 - (s if s > 0.0 else 0.0) * 2.2) + 0.35 * SK_KICK
		return 1.2 if result > 1.2 else result

	# the music the scene answers: the last kick's age and envelope, the chord's hue in the vault, the hits in the lens
	def hear(self, t):
		global SK_KAGE, SK_KICK, SK_BACK_HUE, SK_PUNCH, SK_ZOOM
		cue = self.cue
		if cue is None:
			return
		k = cue.kick.last(t / MUSIC_STEP + 0.0001)
		SK_KAGE = 9.0 if k < 0 else t - cue.kick.at[k] * MUSIC_STEP
		SK_KICK = cue.kick_env(t)
		SK_BACK_HUE = cue.chord(t) % 3 * 80
		punch = cue.hit(t)
		SK_PUNCH = punch * punch
		SK_ZOOM = 0.5 * (1.0 - SK_PUNCH * 0.12 - SK_KICK * 0.015 * SK_WAKE)

	def smooth(self, u):
		c = 0.0 if u < 0.0 else (1.0 if u > 1.0 else u)
		return c * c * (3.0 - 2.0 * c)

	def turn_toward(self, from_, to, w):
		d = to - from_
		d = d - 6.2831853 * floor((d + 3.1415927) / 6.2831853)
		return from_ + d * w

	# where a foot wants to stand: on the web about the body, the front pairs raised off it at the viewer in the stalk and the leap
	def rest_foot(self, i, lead):
		side = 1.0 if i < 4 else -1.0
		j = i % 4
		reach = (self.legreach[j] + lead) * self.stance
		span = side * 0.78 * self.stance
		self.tox[i] = self.bx + self.fx * reach + self.rx * span
		self.toy[i] = self.by + self.fy * reach + self.ry * span
		self.toz[i] = self.bz * self.leglift[j] + self.stalk(self.tt) * (0.3 if j == 0 else 0.0)

	def jitter(self, n):
		return (self.rng.next(n * 2 + 1) - n) / (n * 1.0)

	# the web stands in the plane z = 0 about the origin, facing +z: the spokes get their angles and the capture spiral its
	# polygon, one vertex where it crosses each spoke, winding in from the rim; lay_turn is how far round from lay_from,
	# in turns, the edge into each vertex begins
	def plan_web(self):
		nspoke = self.nspoke
		spoke_ang = self.spoke_ang
		for k in range(nspoke):
			jit = 0.0 if k == nspoke // 2 else self.jitter(100) * 0.09
			spoke_ang.append(-1.5707963 + k * 6.2831853 / nspoke + jit)
		n = 4 * nspoke + nspoke // 2 + 1
		web_rim = self.web_rim
		for i in range(n):
			r = web_rim - (web_rim - 0.5) * i / (n - 1.0)
			a = spoke_ang[i % nspoke] + 6.2831853 * (i // nspoke)
			self.spa.append(r * cos(a))
			self.spb.append(r * sin(a))
			turn = (spoke_ang[(i + nspoke - 1) % nspoke] - self.lay_from) / 6.2831853
			self.lay_turn.append(turn - floor(turn))

	# how far round the web the spiral is laid at time t, in turns: every winding grows at once behind the running
	# spider, and the lap closes, a spoke past full circle, on bar 4
	def laid(self, t):
		return (t - self.t_spiral0) / (self.t_spiral1 - self.t_spiral0) * (1.0 + 1.0 / self.nspoke)

	# the spiral's run at time t, into qa, qb: out from the hub, round with the spinnerets trailing over the laid edge, home
	def lap_point(self, t):
		r = self.smooth((t - self.t_out) / 0.5) * (1.0 - self.smooth((t - self.t_spiral1) / (self.t_home - self.t_spiral1)))
		a = self.lay_from + self.lay_lead + 6.2831853 * self.laid(t)
		self.qa = r * cos(a)
		self.qb = r * sin(a)

	def lap_heading(self, t):
		self.lap_point(t - 0.04)
		a = self.qa
		b = self.qb
		self.lap_point(t + 0.04)
		return atan2(self.qb - b, self.qa - a)

	# where it is on the web (pa, pb) and which way it faces (psi), at time t
	def plane_path(self, t):
		if t < self.t_out:
			self.pa = 0.0
			self.pb = 0.0
			self.psi = 1.5707963
			return
		if t < self.t_home:
			self.psi = self.turn_toward(1.5707963, self.lap_heading(t), self.smooth((t - self.t_out) / 0.15))
			self.lap_point(t)
			self.pa = self.qa
			self.pb = self.qb
			return
		ml = sqrt(self.moth_a * self.moth_a + self.moth_b * self.moth_b)
		da = self.moth_a / ml
		db = self.moth_b / ml
		toward = atan2(db, da)
		if t < self.t_rush:
			self.pa = 0.0
			self.pb = 0.0
			self.psi = self.turn_toward(self.lap_heading(self.t_home - 0.05), 1.5707963, self.smooth((t - self.t_home) / 0.6))
			if t > self.t_impact:
				self.psi = self.turn_toward(self.psi, toward, self.smooth((t - self.t_impact) / (self.t_rush - self.t_impact)))
			return
		reach = ml - 0.85
		if t < self.t_reach:
			reach = reach * self.smooth((t - self.t_rush) / (self.t_reach - self.t_rush))
			self.psi = toward
		elif t < self.t_turn:
			e = self.smooth((t - self.t_reach) / (self.t_turn - self.t_reach))
			reach = reach - 0.11 * e
			self.psi = toward + 3.1415927 * e
		else:
			# it works the cocoon, then turns head-down and prowls across the hub at the viewer below
			busy = 1.0 - self.smooth((t - self.t_wrap1 + 0.3) / 0.3)
			prowl = self.smooth((t - self.t_face - 0.3) / (self.t_lunge - self.t_face - 0.3))
			reach = reach - 0.11 + 0.05 * sin(t * 8.0) * busy
			self.psi = self.turn_toward(toward + 3.1415927 + 0.08 * sin(t * 5.0) * busy, -1.5707963, self.facing(t))
			self.pa = da * reach * (1.0 - prowl)
			self.pb = db * reach * (1.0 - prowl) - 0.35 * prowl
			return
		self.pa = da * reach
		self.pb = db * reach

	def web_motion(self, t, dt):
		opa = self.pa
		opb = self.pb
		self.plane_path(t)
		self.fx = cos(self.psi)
		self.fy = sin(self.psi)
		self.fz = 0.0
		self.rx = -self.fy
		self.ry = self.fx
		self.rz = 0.0
		self.bx = self.pa
		self.by = self.pb
		self.bz = 0.0
		ma = self.pa - opa
		mb = self.pb - opb
		self.spd = sqrt(ma * ma + mb * mb) / dt if dt > 0.0 else 0.0

	# how bright the strike's ring makes the silk at (a, b): a front running out from the moth through the threads
	def ring_light(self, a, b):
		s = self.tt - self.t_impact
		if s < 0.0 or s > 1.6:
			return 0.0
		ma = a - self.moth_a
		mb = b - self.moth_b
		behind = s * self.ring_speed - sqrt(ma * ma + mb * mb)
		if behind < 0.0 or behind > 0.7:
			return 0.0
		return (1.0 - behind / 0.7) * 1.3 / (1.0 + s * 1.2)

	# how far a point of the web (a, b) stands off its plane: the spider's weight, a breeze, the moth's strike
	# ringing out through the threads and its struggle
	def sag(self, a, b):
		tt = self.tt
		da = a - self.pa
		db = b - self.pb
		result = -0.12 / (1.0 + (da * da + db * db) * 3.0)
		result += 0.035 * sin(a * 0.8 + tt * 1.1) * sin(b * 0.7 - tt * 0.9 + 0.5) * (a * a + b * b) * 0.12
		if tt > self.t_impact:
			ma = a - self.moth_a
			mb = b - self.moth_b
			dist = sqrt(ma * ma + mb * mb)
			s = tt - self.t_impact
			front = s * self.ring_speed - dist
			if front > 0.0:
				result += 0.34 / (1.0 + s * s * 1.6) * sin(front * 4.5) / (1.0 + dist * 0.6)
			if tt < self.t_wrap1:
				fight = 0.07 * (1.0 - self.coc) * (0.55 + 0.45 * sin(tt * 2.3))
				result += fight * sin(tt * 21.0 - dist * 4.0) / (1.0 + dist * 2.5)
		return result

	# a point of the web in the world, into wx, wy, wz
	def web_point(self, a, b):
		self.wx = a
		self.wy = b
		self.wz = self.sag(a, b)

	def bezier(self, from_, via, to, u):
		return from_ * (1.0 - u) * (1.0 - u) + via * 2.0 * u * (1.0 - u) + to * u * u

	# the moth at time t: it flutters in along a curve from off the frame's left, strikes the web and sticks, beats its
	# wings against the silk, then folds them as the cocoon closes and spins
	def moth_pose(self, t):
		bezier = self.bezier
		if t < self.t_impact:
			u = (t - self.t_moth) / (self.t_impact - self.t_moth)
			u = 0.0 if u < 0.0 else u
			a = bezier(-2.7, -0.9, self.moth_a, u)
			b = bezier(0.6, 1.15, self.moth_b, u)
			n = bezier(1.4, 0.9, 0.06, u)
			va = bezier(-2.7, -0.9, self.moth_a, u + 0.02) - a
			vb = bezier(0.6, 1.15, self.moth_b, u + 0.02) - b
			vn = bezier(1.4, 0.9, 0.06, u + 0.02) - n
			flutter = 1.0 - u
			self.mox = a + flutter * 0.16 * sin(t * 6.3)
			self.moy = b + flutter * 0.18 * sin(t * 8.9 + 1.0)
			self.moz = n + flutter * 0.14 * sin(t * 5.2 + 2.0)
			vl = sqrt(va * va + vb * vb + vn * vn) + 0.000001
			self.mfx = va / vl
			self.mfy = vb / vl + 0.5
			self.mfz = vn / vl
			self.flap = 0.95 * sin(t * 31.4)
			self.wsc = 1.0
			self.coc = 0.0
			self.spin = 0.0
		else:
			self.web_point(self.moth_a, self.moth_b)
			self.mox = self.wx
			self.moy = self.wy
			self.moz = self.wz + 0.09
			self.mfx = 0.34
			self.mfy = 0.94
			self.mfz = 0.0
			self.coc = self.smooth((t - self.t_wrap0) / (self.t_wrap1 - 0.15 - self.t_wrap0))
			self.spin = ((t if t < self.t_wrap1 else self.t_wrap1) - self.t_wrap0) * 6.0 if t > self.t_wrap0 else 0.0
			beat = sin(t * 15.0)
			self.flap = (1.0 - self.coc) * 0.55 * (beat if beat > 0.0 else 0.0) * (0.6 + 0.4 * sin(t * 2.3)) + self.coc * 1.3
			self.wsc = 1.0 - 0.7 * self.coc
		mfx = self.mfx
		mfy = self.mfy
		mfz = self.mfz
		ml = 1.0 / sqrt(mfx * mfx + mfy * mfy + mfz * mfz)
		mfx *= ml
		mfy *= ml
		mfz *= ml
		mux = 0.0 - mfx * mfz
		muy = 0.0 - mfy * mfz
		muz = 1.0 - mfz * mfz
		ul = 1.0 / sqrt(mux * mux + muy * muy + muz * muz + 0.0000001)
		mux *= ul
		muy *= ul
		muz *= ul
		mrx = muy * mfz - muz * mfy
		mry = muz * mfx - mux * mfz
		mrz = mux * mfy - muy * mfx
		if t > self.t_impact:
			# the struggle jerks it about its spine; the wrapping rolls it
			roll = self.spin * 0.12 + 0.3 * (1.0 - self.coc) * sin(t * 9.0)
			c = cos(roll)
			s = sin(roll)
			nux = mux * c + mrx * s
			nuy = muy * c + mry * s
			nuz = muz * c + mrz * s
			mrx = mrx * c - mux * s
			mry = mry * c - muy * s
			mrz = mrz * c - muz * s
			mux = nux
			muy = nuy
			muz = nuz
		self.mfx = mfx; self.mfy = mfy; self.mfz = mfz
		self.mux = mux; self.muy = muy; self.muz = muz
		self.mrx = mrx; self.mry = mry; self.mrz = mrz

	def spawn(self, x, y, z, vx, vy, vz, life):
		i = self.pnext
		self.pnext = (self.pnext + 1) % self.np
		self.ppx[i] = x; self.ppy[i] = y; self.ppz[i] = z
		self.pvx[i] = vx; self.pvy[i] = vy; self.pvz[i] = vz
		self.plife[i] = 1.0
		self.prate[i] = 1.0 / life

	# an ember off the ring of the crown, thrown outward at the given speed
	def crown_ember(self, speed):
		rng = self.rng
		fx = self.fx; fy = self.fy
		rx = self.rx; ry = self.ry
		a = rng.next(628) * 0.01
		ca = cos(a) * 0.2
		sa = sin(a) * 0.2
		cx = self.bx + fx * (0.01 + ca * 0.66) + rx * sa
		cy = self.by + fy * (0.01 + ca * 0.66) + ry * sa
		self.spawn(cx, cy, self.bz + 0.6 + ca * 0.75, (fx * ca * 0.66 + rx * sa) * speed, 0.5 + (fy * ca * 0.66 + ry * sa) * speed, 0.3 + ca * 0.75 * speed, 0.45 + rng.next(40) * 0.01)

	def reached(self, mark, dt):
		return self.tt >= mark and self.tt - dt < mark

	# embers leave the crown once it burns, more on the kick and many when the jaw gapes; the waking and the lunge throw
	# a burst of them, the strike a puff of scales off the moth
	def update_particles(self, dt):
		self.spawn_acc += dt * 5.0 * SK_WAKE * (0.3 + SK_KICK + self.gape(self.tt) * 8.0)
		while self.spawn_acc >= 1.0:
			self.spawn_acc -= 1.0
			self.crown_ember(1.0)
		if self.reached(self.t_wake, dt) or self.reached(self.t_lunge, dt):
			for k in range(26):
				self.crown_ember(5.0)
		if self.reached(self.t_impact, dt):
			rng = self.rng
			for k in range(22):
				a = rng.next(628) * 0.01
				speed = 0.5 + rng.next(100) * 0.012
				self.spawn(self.moth_a, self.moth_b, 0.12, cos(a) * speed, sin(a) * speed, 0.3 + self.jitter(100) * 0.3, 0.5 + rng.next(50) * 0.01)
		drag = 1.0 - dt * 1.4
		ppx = self.ppx; ppy = self.ppy; ppz = self.ppz
		pvx = self.pvx; pvy = self.pvy; pvz = self.pvz
		plife = self.plife; prate = self.prate
		for i in range(self.np):
			if plife[i] <= 0.0:
				continue
			pvx[i] = pvx[i] * drag
			pvy[i] = (pvy[i] + 0.7 * dt) * drag
			pvz[i] = pvz[i] * drag
			ppx[i] = ppx[i] + pvx[i] * dt
			ppy[i] = ppy[i] + pvy[i] * dt
			ppz[i] = ppz[i] + pvz[i] * dt
			plife[i] = plife[i] - dt * prate[i]

	def update(self, t, dt):
		global SK_T, SK_WAKE
		self.tt = t
		SK_T = t
		SK_WAKE = self.smooth((t - self.t_wake + 0.05) / 0.35)
		self.hear(t)
		self.stance = 0.5 + 0.5 * self.smooth((t - self.t_wake) / 0.3)
		self.web_motion(t, dt)
		spring = self.lunge(t)
		if spring > 0.0 and self.placed:
			lx = self.ex - self.bx
			ly = self.ey - self.by
			lz = self.ez - self.bz
			ll = 0.75 * spring / sqrt(lx * lx + ly * ly + lz * lz + 0.000001)
			self.bx += lx * ll
			self.by += ly * ll
			self.bz += lz * ll
		if not self.placed:
			for i in range(8):
				self.rest_foot(i, 0.0)
				self.footx[i] = self.tox[i]
				self.footy[i] = self.toy[i]
				self.footz[i] = self.toz[i]
		self.walk(dt)
		self.update_particles(dt)
		if t > self.t_moth:
			self.moth_pose(t)
		self.aim_camera(t, dt)
		self.placed = True

	# the tetrapod gait: a foot steps when its rest spot has run off, one group at a time; at speed it scuttles, with
	# quicker steps that lead the body and both groups free
	def walk(self, dt):
		scuttle = self.spd > 1.5
		step_time = 0.08 if scuttle else 0.13
		lead = self.spd * 0.1
		lead = 0.45 if lead > 0.45 else lead
		stept = self.stept
		footx = self.footx; footy = self.footy; footz = self.footz
		fromx = self.fromx; fromy = self.fromy; fromz = self.fromz
		tox = self.tox; toy = self.toy; toz = self.toz
		stepping_a = False
		stepping_b = False
		for i in range(8):
			if stept[i] >= 0.0:
				if (i + i // 4) % 2 == 0:
					stepping_a = True
				else:
					stepping_b = True
		for i in range(8):
			group_a = (i + i // 4) % 2 == 0
			if stept[i] >= 0.0:
				stept[i] = stept[i] + dt / step_time
				self.rest_foot(i, lead)
				if stept[i] >= 1.0:
					stept[i] = -1.0
					footx[i] = tox[i]
					footy[i] = toy[i]
					footz[i] = toz[i]
				else:
					e = stept[i]
					footx[i] = fromx[i] + (tox[i] - fromx[i]) * e
					footy[i] = fromy[i] + (toy[i] - fromy[i]) * e
					footz[i] = fromz[i] + (toz[i] - fromz[i]) * e + sin(e * 3.14159265) * 0.16
			else:
				self.rest_foot(i, 0.0)
				ddx = footx[i] - tox[i]
				ddy = footy[i] - toy[i]
				ddz = footz[i] - toz[i]
				other_moving = stepping_b if group_a else stepping_a
				if (scuttle or not other_moving) and ddx * ddx + ddy * ddy + ddz * ddz > 0.05:
					stept[i] = 0.0
					fromx[i] = footx[i]
					fromy[i] = footy[i]
					fromz[i] = footz[i]
					if group_a:
						stepping_a = True
					else:
						stepping_b = True

	# the camera: close on the sleeper, back for the whole web while it is thrown and the moth flies in, in on the hunter
	# and its catch, then round in front of the skull, below the line of its gaze (the face is reared 0.85 radians off
	# the body, the camera stands 0.51 up); the lunge lifts the aim up the face, which keeps the crown in the frame
	def aim_camera(self, t, dt):
		smooth = self.smooth
		fx = self.fx; fy = self.fy
		hx = self.bx
		hy = self.by
		hz = self.bz + 0.2
		back = smooth((t - self.t_wake - 0.3) / 0.9)
		near = smooth((t - self.t_impact - 0.25) / (self.t_reach - self.t_impact - 0.25)) * (1.0 - self.facing(t))
		creep = self.stalk(t)
		onto = self.facing(t) * 0.6 + creep * 0.4
		tgx = hx * (1.0 - 0.75 * back)
		tgy = hy * (1.0 - 0.75 * back)
		tgz = hz * (1.0 - back)
		if t > self.t_moth:
			tgx += ((hx + self.mox) * 0.5 - tgx) * near
			tgy += ((hy + self.moy) * 0.5 - tgy) * near
			tgz += ((hz + self.moz) * 0.5 - tgz) * near
		tgx += (hx + fx * 0.34 - tgx) * onto
		tgy += (hy + fy * 0.34 - tgy) * onto
		tgz += (hz - tgz) * onto
		rise = 0.12 * self.lunge(t)
		tgx -= fx * 0.7513 * rise
		tgy -= fy * 0.7513 * rise
		tgz += 0.66 * rise
		dist = 2.3 - 0.3 * (t / self.t_wake if t < self.t_wake else 1.0)
		dist += (3.5 - dist) * back
		dist += (2.1 - dist) * near
		dist += (2.75 - dist) * self.facing(t)
		dist = dist - creep * 0.53 - self.lunge(t) * 0.53
		oa = 0.1 + 0.12 * sin(t * 0.19)
		dx = sin(oa)
		dy = 0.12 + 0.05 * sin(t * 0.31)
		dz = cos(oa)
		dn = sqrt(dx * dx + dy * dy + dz * dz)
		dx += (fx * 0.8712 * dn - dx) * onto
		dy += (fy * 0.8712 * dn - dy) * onto
		dz += (0.4908 * dn - dz) * onto
		dl = dist / sqrt(dx * dx + dy * dy + dz * dz)
		kc = dt * (2.8 + 9.0 * self.lunge(t) * (1.0 - smooth((t - self.t_lunge - 0.5) / 0.5))) if self.placed else 1.0
		kc = 1.0 if kc > 1.0 else kc
		self.ex += (tgx + dx * dl - self.ex) * kc
		self.ey += (tgy + dy * dl - self.ey) * kc
		self.ez += (tgz + dz * dl - self.ez) * kc
		kl = dt * 4.0 if self.placed else 1.0
		kl = 1.0 if kl > 1.0 else kl
		self.lkx += (tgx - self.lkx) * kl
		self.lky += (tgy - self.lky) * kl
		self.lkz += (tgz - self.lkz) * kl
		ku = dt * 2.2 if self.placed else 1.0
		ku = 1.0 if ku > 1.0 else ku
		self.cux += (0.0 - fx * 0.7513 * onto - self.cux) * ku
		self.cuy += (1.0 - onto - fy * 0.7513 * onto - self.cuy) * ku
		self.cuz += (0.66 * onto - self.cuz) * ku

	def capsule(self, i, ax, ay, az, bx2, by2, bz2, r):
		dx = bx2 - ax
		dy = by2 - ay
		dz = bz2 - az
		SK_CAX[i] = ax; SK_CAY[i] = ay; SK_CAZ[i] = az
		SK_CDX[i] = dx; SK_CDY[i] = dy; SK_CDZ[i] = dz
		SK_CID[i] = 1.0 / (dx * dx + dy * dy + dz * dz + 0.000001)
		SK_CR[i] = r
		SK_CMX[i] = ax + dx * 0.5; SK_CMY[i] = ay + dy * 0.5; SK_CMZ[i] = az + dz * 0.5
		SK_CB[i] = sqrt(dx * dx + dy * dy + dz * dz) * 0.5 + r

	def glow(self, i, x, y, z, r):
		SK_GX[i] = x; SK_GY[i] = y; SK_GZ[i] = z; SK_GR[i] = r

	# a curved horn: base, a bend out and back, a tip up and back
	def horn(self, i, side):
		bx0 = SK_HX + SK_PUX * 0.16 + SK_PRX * side * 0.12 - SK_PFX * 0.04
		by0 = SK_HY + SK_PUY * 0.16 + SK_PRY * side * 0.12 - SK_PFY * 0.04
		bz0 = SK_HZ + SK_PUZ * 0.16 + SK_PRZ * side * 0.12 - SK_PFZ * 0.04
		mx = bx0 + SK_PRX * side * 0.2 + SK_PUX * 0.12 - SK_PFX * 0.12
		my = by0 + SK_PRY * side * 0.2 + SK_PUY * 0.12 - SK_PFY * 0.12
		mz = bz0 + SK_PRZ * side * 0.2 + SK_PUZ * 0.12 - SK_PFZ * 0.12
		tx = mx + SK_PRX * side * 0.04 + SK_PUX * 0.26 - SK_PFX * 0.2
		ty = my + SK_PRY * side * 0.04 + SK_PUY * 0.26 - SK_PFY * 0.2
		tz = mz + SK_PRZ * side * 0.04 + SK_PUZ * 0.26 - SK_PFZ * 0.2
		self.capsule(i, bx0, by0, bz0, mx, my, mz, 0.05)
		self.capsule(i + 1, mx, my, mz, tx, ty, tz, 0.028)
		return i + 2

	# a leg from hip to foot: femur and tibia by two-bone IK to an ankle held above the foot, then the tarsus
	def leg(self, ci, i, hx, hy, hz, side):
		ux = self.ux; uy = self.uy; uz = self.uz
		rx = self.rx; ry = self.ry; rz = self.rz
		l1 = self.l1; l2 = self.l2
		px = self.footx[i]; py = self.footy[i]; pz = self.footz[i]
		hdx = px - hx; hdy = py - hy; hdz = pz - hz
		hu = hdx * ux + hdy * uy + hdz * uz
		hdx -= ux * hu
		hdy -= uy * hu
		hdz -= uz * hu
		hl = 1.0 / sqrt(hdx * hdx + hdy * hdy + hdz * hdz + 0.000001)
		hdx *= hl
		hdy *= hl
		hdz *= hl
		ax = px + ux * 0.13 - hdx * 0.06
		ay = py + uy * 0.13 - hdy * 0.06
		az = pz + uz * 0.13 - hdz * 0.06
		dx = ax - hx
		dy = ay - hy
		dz = az - hz
		d = sqrt(dx * dx + dy * dy + dz * dz)
		dn = 0.0001 if d < 0.0001 else d
		dx /= dn
		dy /= dn
		dz /= dn
		dmax = (l1 + l2) * 0.995
		dc = dmax if d > dmax else d
		dc = 0.08 if dc < 0.08 else dc
		a = (l1 * l1 - l2 * l2 + dc * dc) / (2.0 * dc)
		hh = l1 * l1 - a * a
		hh = 0.0 if hh < 0.0 else sqrt(hh)
		bux = ux + rx * side * 0.35
		buy = uy + ry * side * 0.35
		buz = uz + rz * side * 0.35
		bd = bux * dx + buy * dy + buz * dz
		bux -= dx * bd
		buy -= dy * bd
		buz -= dz * bd
		bl = 1.0 / sqrt(bux * bux + buy * buy + buz * buz + 0.0000001)
		bux *= bl
		buy *= bl
		buz *= bl
		kx = hx + dx * a + bux * hh
		ky = hy + dy * a + buy * hh
		kz = hz + dz * a + buz * hh
		tx = hx + dx * dc
		ty = hy + dy * dc
		tz = hz + dz * dc
		self.capsule(ci, hx, hy, hz, kx, ky, kz, 0.042)
		self.capsule(ci + 1, kx, ky, kz, tx, ty, tz, 0.03)
		self.capsule(ci + 2, tx, ty, tz, px, py, pz, 0.018)
		self.glow(3 + i, kx, ky, kz, 0.027 + 0.006 * sin(self.tt * 7.0 + i))
		return ci + 3

	def render(self, grid):
		global SK_W, SK_H, SK_FLICK, SK_PFX, SK_PFY, SK_PFZ, SK_PUX, SK_PUY, SK_PUZ, SK_PRX, SK_PRY, SK_PRZ
		global SK_QFX, SK_QFY, SK_QFZ, SK_QUX, SK_QUY, SK_QUZ, SK_TX, SK_TY, SK_TZ, SK_AR, SK_AX, SK_AY, SK_AZ
		global SK_HX, SK_HY, SK_HZ, SK_NX, SK_NY, SK_NZ, SK_JX, SK_JY, SK_JZ, SK_BCX, SK_BCY, SK_BCZ, SK_BR
		global SK_CAX, SK_CAY, SK_CAZ, SK_CDX, SK_CDY, SK_CDZ, SK_CID, SK_CR, SK_CMX, SK_CMY, SK_CMZ, SK_CB
		global SK_GX, SK_GY, SK_GZ, SK_GR, SK_NC, SK_CROWN0, SK_NG, SK_NBONE, SK_GLARE, SK_LX, SK_LY, SK_LZ
		global SK_HGX, SK_HGY, SK_HGZ, SK_BGX, SK_BGY, SK_BGZ, SK_CRX, SK_CRY, SK_CRZ, SK_SKB, SK_CRB
		global SK_GMX, SK_GMY, SK_GMZ, SK_GAX, SK_GAY, SK_GAZ, SK_GBX, SK_GBY, SK_GBZ
		global SK_EX, SK_EY, SK_EZ, SK_FX, SK_FY, SK_FZ, SK_UX, SK_UY, SK_UZ, SK_RX, SK_RY, SK_RZ
		global SK_KX, SK_KY, SK_KZ, SK_ROWS
		# the rays trace on a grid coarser than the picture's and the trace scales up to it, coarser still where the
		# creature fills the frame and every ray is dear; the silk and the embers draw over it at full size
		smooth = self.smooth
		tt = self.tt
		w = self.w
		h = self.h
		fx = self.fx; fy = self.fy; fz = self.fz
		ux = self.ux; uy = self.uy; uz = self.uz
		rx = self.rx; ry = self.ry; rz = self.rz
		glare = self.facing(tt) * 0.3 + self.stalk(tt) * 0.7
		close = 1.0 - smooth((tt - self.t_wake - 0.3) / 0.9)
		caught = smooth((tt - self.t_impact - 0.25) / 0.5)
		close = close if close > caught else caught
		trace_k = sqrt((6400.0 - close * 1800.0) * (1.0 - SK_PUNCH * 0.25) / (w * h))
		trace_k = 1.0 if trace_k > 1.0 else trace_k
		tw = int(w * trace_k)
		th = int(h * trace_k)
		SK_W = tw
		SK_H = th
		SK_FLICK = 0.7 * sin(tt * 17.0) * sin(tt * 5.3) + 1.5 * self.flare(tt)
		if tt > self.t_wrap1:
			SK_FLICK = SK_FLICK * 0.4 + 0.9 + glare * 1.4
		bob = 0.2 + 0.012 * sin(tt * 1.6) * (1.0 - SK_WAKE) + 0.018 * sin(tt * 11.0) * SK_WAKE
		cx = self.bx + ux * bob
		cy = self.by + uy * bob
		cz = self.bz + uz * bob
		# the sleeper's skull hangs level with its body; awake it rears back to face the viewer
		pitch = 0.3 + 0.55 * smooth((tt - self.t_wake) / 0.4)
		cpt = cos(pitch)
		spt = sin(pitch)
		hfx = fx * cpt + ux * spt; hfy = fy * cpt + uy * spt; hfz = fz * cpt + uz * spt
		hux = ux * cpt - fx * spt; huy = uy * cpt - fy * spt; huz = uz * cpt - fz * spt
		SK_PFX = hfx; SK_PFY = hfy; SK_PFZ = hfz
		SK_PUX = hux; SK_PUY = huy; SK_PUZ = huz
		SK_PRX = rx; SK_PRY = ry; SK_PRZ = rz
		SK_QFX = fx; SK_QFY = fy; SK_QFZ = fz
		SK_QUX = ux; SK_QUY = uy; SK_QUZ = uz
		SK_TX = cx; SK_TY = cy; SK_TZ = cz
		SK_AR = 0.27
		SK_AX = cx - fx * 0.45 + ux * 0.12; SK_AY = cy - fy * 0.45 + uy * 0.12; SK_AZ = cz - fz * 0.45 + uz * 0.12
		nod = 0.03 * sin(tt * 2.1)
		SK_HX = cx + fx * 0.34 + ux * (0.09 + nod); SK_HY = cy + fy * 0.34 + uy * (0.09 + nod); SK_HZ = cz + fz * 0.34 + uz * (0.09 + nod)
		SK_NX = SK_HX + hfx * 0.23 - hux * 0.06; SK_NY = SK_HY + hfy * 0.23 - huy * 0.06; SK_NZ = SK_HZ + hfz * 0.23 - huz * 0.06
		jaw = 0.012 * sin(tt * 6.0) + 0.1 * self.gape(tt)
		SK_JX = SK_HX + hfx * 0.1 - hux * (0.14 + jaw); SK_JY = SK_HY + hfy * 0.1 - huy * (0.14 + jaw); SK_JZ = SK_HZ + hfz * 0.1 - huz * (0.14 + jaw)
		SK_BCX = cx; SK_BCY = cy; SK_BCZ = cz
		SK_BR = 1.6
		ncap = 5 + 2 + 24 + 4 + 2 + 7
		if len(SK_CAX) != ncap:
			SK_CAX = []; SK_CAY = []; SK_CAZ = []; SK_CDX = []; SK_CDY = []; SK_CDZ = []; SK_CID = []; SK_CR = []; SK_CMX = []; SK_CMY = []; SK_CMZ = []; SK_CB = []
			for i in range(ncap):
				SK_CAX.append(0.0); SK_CAY.append(0.0); SK_CAZ.append(0.0); SK_CDX.append(0.0); SK_CDY.append(0.0); SK_CDZ.append(0.0); SK_CID.append(1.0); SK_CR.append(0.1); SK_CMX.append(0.0); SK_CMY.append(0.0); SK_CMZ.append(0.0); SK_CB.append(1.0)
		nglow = 3 + 8
		if len(SK_GX) != nglow:
			SK_GX = []; SK_GY = []; SK_GZ = []; SK_GR = []
			for i in range(nglow):
				SK_GX.append(0.0); SK_GY.append(0.0); SK_GZ.append(0.0); SK_GR.append(0.02)
		SK_NC = ncap
		SK_CROWN0 = ncap - 7
		SK_NG = nglow
		SK_NBONE = 5
		eye = 0.03 * (1.0 + glare * 0.6)
		SK_GLARE = glare
		glow = self.glow
		capsule = self.capsule
		glow(0, SK_HX + hfx * 0.175 + hux * 0.03 + rx * 0.088, SK_HY + hfy * 0.175 + huy * 0.03 + ry * 0.088, SK_HZ + hfz * 0.175 + huz * 0.03 + rz * 0.088, eye)
		glow(1, SK_HX + hfx * 0.175 + hux * 0.03 - rx * 0.088, SK_HY + hfy * 0.175 + huy * 0.03 - ry * 0.088, SK_HZ + hfz * 0.175 + huz * 0.03 - rz * 0.088, eye)
		third = (0.024 + 0.006 * sin(tt * 3.0)) * (1.0 + glare * 0.5)
		glow(2, SK_HX + hfx * 0.17 + hux * 0.15, SK_HY + hfy * 0.17 + huy * 0.15, SK_HZ + hfz * 0.17 + huz * 0.15, third)
		SK_LX = SK_GX[2] * 0.4 + (SK_GX[0] + SK_GX[1]) * 0.3 + hfx * 0.1
		SK_LY = SK_GY[2] * 0.4 + (SK_GY[0] + SK_GY[1]) * 0.3 + hfy * 0.1
		SK_LZ = SK_GZ[2] * 0.4 + (SK_GZ[0] + SK_GZ[1]) * 0.3 + hfz * 0.1
		capsule(0, SK_JX - rx * 0.08, SK_JY - ry * 0.08, SK_JZ - rz * 0.08, SK_JX + rx * 0.08, SK_JY + ry * 0.08, SK_JZ + rz * 0.08, 0.05)
		ci = self.horn(1, 1.0)
		ci = self.horn(ci, -1.0)
		fang = 0.02 * sin(tt * 6.0 + 1.0) + 0.05 * self.gape(tt)
		for s in range(2):
			side = 1.0 if s == 0 else -1.0
			ax = SK_JX + hfx * 0.05 + rx * side * 0.035
			ay = SK_JY + hfy * 0.05 + ry * side * 0.035
			az = SK_JZ + hfz * 0.05 + rz * side * 0.035
			capsule(ci, ax, ay, az, ax + hfx * 0.07 - hux * 0.12 - rx * side * (0.02 + fang), ay + hfy * 0.07 - huy * 0.12 - ry * side * (0.02 + fang), az + hfz * 0.07 - huz * 0.12 - rz * side * (0.02 + fang), 0.016)
			ci = ci + 1
		legfo = self.legfo
		for i in range(8):
			side = 1.0 if i < 4 else -1.0
			fo = legfo[i % 4]
			hx = cx + fx * fo * 0.45 + rx * side * 0.1 + ux * 0.03
			hy = cy + fy * fo * 0.45 + ry * side * 0.1 + uy * 0.03
			hz = cz + fz * fo * 0.45 + rz * side * 0.1 + uz * 0.03
			ci = self.leg(ci, i, hx, hy, hz, side)
		# pedipalps feel the air in front of the jaw
		for s in range(2):
			side = 1.0 if s == 0 else -1.0
			twitch = 0.03 * sin(tt * 7.3 + s * 2.0)
			px = SK_HX + hfx * 0.2 - hux * 0.1 + rx * side * 0.08
			py = SK_HY + hfy * 0.2 - huy * 0.1 + ry * side * 0.08
			pz = SK_HZ + hfz * 0.2 - huz * 0.1 + rz * side * 0.08
			mx = px + hfx * 0.11 + hux * (0.05 + twitch) + rx * side * 0.05
			my = py + hfy * 0.11 + huy * (0.05 + twitch) + ry * side * 0.05
			mz = pz + hfz * 0.11 + huz * (0.05 + twitch) + rz * side * 0.05
			capsule(ci, px, py, pz, mx, my, mz, 0.02)
			capsule(ci + 1, mx, my, mz, mx + hfx * 0.08 - hux * 0.09, my + hfy * 0.08 - huy * 0.09, mz + hfz * 0.08 - huz * 0.09, 0.017)
			ci = ci + 2
		# the spinnerets at the abdomen's tip
		for s in range(2):
			side = 1.0 if s == 0 else -1.0
			sx = SK_AX - fx * 0.32 + rx * side * 0.03 - ux * 0.02
			sy = SK_AY - fy * 0.32 + ry * side * 0.03 - uy * 0.02
			sz = SK_AZ - fz * 0.32 + rz * side * 0.03 - uz * 0.02
			capsule(ci, sx, sy, sz, sx - fx * 0.08 - ux * 0.03, sy - fy * 0.08 - uy * 0.03, sz - fz * 0.08 - uz * 0.03, 0.02)
			ci = ci + 1
		hover = 0.03 * sin(tt * 1.7)
		SK_HGX = SK_HX + hux * 0.3; SK_HGY = SK_HY + huy * 0.3; SK_HGZ = SK_HZ + huz * 0.3
		SK_BGX = (SK_AX + SK_TX) * 0.5; SK_BGY = (SK_AY + SK_TY) * 0.5; SK_BGZ = (SK_AZ + SK_TZ) * 0.5
		SK_CRX = SK_HX + hux * (0.44 + hover); SK_CRY = SK_HY + huy * (0.44 + hover); SK_CRZ = SK_HZ + huz * (0.44 + hover)
		for k in range(7):
			a = k * 0.8976 + tt * 0.6
			ca = cos(a) * 0.2
			sa = sin(a) * 0.2
			flame = (0.02 + 0.08 * SK_WAKE) + (0.05 * sin(tt * 9.0 + k * 1.9) + 0.03 * sin(tt * 23.0 + k)) * SK_WAKE + 0.03 * SK_KICK * SK_WAKE + (0.07 + 0.05 * SK_KICK) * glare
			bxk = SK_CRX + hfx * ca + rx * sa
			byk = SK_CRY + hfy * ca + ry * sa
			bzk = SK_CRZ + hfz * ca + rz * sa
			capsule(SK_CROWN0 + k, bxk, byk, bzk, bxk + hux * flame + hfx * ca * 0.25 + rx * sa * 0.25, byk + huy * flame + hfy * ca * 0.25 + ry * sa * 0.25, bzk + huz * flame + hfz * ca * 0.25 + rz * sa * 0.25, 0.018)
		# the bounding radii the distance skips on: the skull's jaw about its center, the ring and flames about the crown's
		jhx = SK_JX - SK_HX; jhy = SK_JY - SK_HY; jhz = SK_JZ - SK_HZ
		jaw_reach = sqrt(jhx * jhx + jhy * jhy + jhz * jhz) + 0.085 + 0.09
		SK_SKB = (jaw_reach if jaw_reach > SK_HR else SK_HR) + 0.001
		crown_reach = 0.2 + 0.028
		for k in range(SK_CROWN0, SK_NC):
			mx = SK_CMX[k] - SK_CRX; my = SK_CMY[k] - SK_CRY; mz = SK_CMZ[k] - SK_CRZ
			r = sqrt(mx * mx + my * my + mz * mz) + SK_CB[k]
			crown_reach = r if r > crown_reach else crown_reach
		SK_CRB = crown_reach + 0.001
		spin_gem = tt * 2.2
		gl2 = 0.3 + 0.02 * sin(tt * 2.5)
		SK_GMX = SK_CRX + hux * gl2; SK_GMY = SK_CRY + huy * gl2; SK_GMZ = SK_CRZ + huz * gl2
		SK_GAX = rx * cos(spin_gem) + hfx * sin(spin_gem); SK_GAY = ry * cos(spin_gem) + hfy * sin(spin_gem); SK_GAZ = rz * cos(spin_gem) + hfz * sin(spin_gem)
		SK_GBX = hfx * cos(spin_gem) - rx * sin(spin_gem); SK_GBY = hfy * cos(spin_gem) - ry * sin(spin_gem); SK_GBZ = hfz * cos(spin_gem) - rz * sin(spin_gem)
		self.pose_moth()
		ex = self.ex; ey = self.ey; ez = self.ez
		SK_EX = ex; SK_EY = ey; SK_EZ = ez
		lx = self.lkx - ex
		ly = self.lky - ey
		lz = self.lkz - ez
		ll = 1.0 / sqrt(lx * lx + ly * ly + lz * lz)
		lx *= ll
		ly *= ll
		lz *= ll
		cux = self.cux; cuy = self.cuy; cuz = self.cuz
		d = cux * lx + cuy * ly + cuz * lz
		upx = cux - lx * d
		upy = cuy - ly * d
		upz = cuz - lz * d
		ul = 1.0 / sqrt(upx * upx + upy * upy + upz * upz + 0.0000001)
		upx *= ul
		upy *= ul
		upz *= ul
		SK_FX = lx; SK_FY = ly; SK_FZ = lz
		SK_UX = upx; SK_UY = upy; SK_UZ = upz
		SK_RX = ly * upz - lz * upy
		SK_RY = lz * upx - lx * upz
		SK_RZ = lx * upy - ly * upx
		# the key light: above the camera, a little to its right, toward the scene
		kx = upx * 0.75 - lx * 0.45 + SK_RX * 0.35
		ky = upy * 0.75 - ly * 0.45 + SK_RY * 0.35
		kz = upz * 0.75 - lz * 0.45 + SK_RZ * 0.35
		kl = 1.0 / sqrt(kx * kx + ky * ky + kz * kz)
		SK_KX = kx * kl; SK_KY = ky * kl; SK_KZ = kz * kl
		if len(SK_ROWS) != th:
			SK_ROWS = []
			for r in range(th):
				SK_ROWS.append(r)
		strike = exp((self.t_impact - tt) * 5.0) if tt > self.t_impact else 0.0
		leap = exp((self.t_lunge - tt) * 4.0) if tt > self.t_lunge else 0.0
		sk_backdrop((0.55 + 0.45 * SK_WAKE) * (1.0 - 0.5 * glare), 0.1 * self.flare(tt) + 0.14 * strike + 0.16 * leap)
		rows = [sk_row(r) for r in SK_ROWS]
		if len(self.depth) != w * h:
			self.depth = [0] * (w * h)
		depth = self.depth
		idx = 0
		for y in range(h):
			row = rows[y * th // h]
			for x in range(w):
				v = row[x * tw // w]
				grid[idx] = v % 1024
				depth[idx] = v // 1024
				idx += 1
		self.draw_web(grid)
		self.draw_particles(grid)
		self.strike_shake(grid)

	# the strike and the lunge shake the frame for half a second
	def strike_shake(self, grid):
		tt = self.tt
		s = tt - (self.t_lunge if tt >= self.t_lunge else self.t_impact)
		if s < 0.0 or s > 0.5:
			return
		w = self.w
		amp = (0.5 - s) * w * 0.05
		row = [0] * w
		for y in range(self.h):
			off = int(sin(y * 0.7 + tt * 61.0) * amp)
			if off == 0:
				continue
			base = y * w
			for x2 in range(w):
				row[x2] = grid[base + x2]
			for x3 in range(w):
				from_ = x3 - off
				from_ = 0 if from_ < 0 else (w - 1 if from_ >= w else from_)
				grid[base + x3] = row[from_]

	def pose_moth(self):
		global SK_MOTH, SK_MX, SK_MY, SK_MZ, SK_MFX, SK_MFY, SK_MFZ, SK_MUX, SK_MUY, SK_MUZ, SK_MRX, SK_MRY, SK_MRZ
		global SK_WSC, SK_COC, SK_SPIN, SK_MB
		SK_MOTH = self.tt > self.t_moth
		if not SK_MOTH:
			return
		self.moth_pose(self.tt)
		mox = self.mox; moy = self.moy; moz = self.moz
		mfx = self.mfx; mfy = self.mfy; mfz = self.mfz
		mux = self.mux; muy = self.muy; muz = self.muz
		mrx = self.mrx; mry = self.mry; mrz = self.mrz
		wsc = self.wsc
		SK_MX = mox; SK_MY = moy; SK_MZ = moz
		SK_MFX = mfx; SK_MFY = mfy; SK_MFZ = mfz
		SK_MUX = mux; SK_MUY = muy; SK_MUZ = muz
		SK_MRX = mrx; SK_MRY = mry; SK_MRZ = mrz
		SK_WSC = wsc
		SK_COC = self.coc
		SK_SPIN = self.spin
		SK_MB = 0.68 * wsc + 0.12 + 0.28 * self.coc + 0.06
		SK_MB = 0.4 if SK_MB < 0.4 else SK_MB
		c = cos(self.flap)
		s = sin(self.flap)
		for k in range(4):
			side = 1.0 if k % 2 == 0 else -1.0
			fore = k < 2
			off = 0.045 if fore else -0.07
			SK_WOX[k] = mox + mfx * off + mrx * side * 0.05
			SK_WOY[k] = moy + mfy * off + mry * side * 0.05
			SK_WOZ[k] = moz + mfz * off + mrz * side * 0.05
			SK_WSX[k] = mrx * side * c + mux * s; SK_WSY[k] = mry * side * c + muy * s; SK_WSZ[k] = mrz * side * c + muz * s
			SK_WNX[k] = -mrx * side * s + mux * c; SK_WNY[k] = -mry * side * s + muy * c; SK_WNZ[k] = -mrz * side * s + muz * c
			SK_WCA[k] = (0.33 if fore else 0.23) * wsc
			SK_WSA[k] = (0.33 if fore else 0.23) * wsc
			SK_WCB[k] = (0.0 if fore else -0.11) * wsc
			SK_WSB[k] = (0.16 if fore else 0.135) * wsc

	# a world point onto the screen, into qsx, qsy and its depth qzc; false behind the camera
	def project(self, x, y, z):
		ox = x - self.ex; oy = y - self.ey; oz = z - self.ez
		zc = ox * SK_FX + oy * SK_FY + oz * SK_FZ
		if zc < 0.08:
			return False
		half_h = self.h * 0.5
		scale = half_h / (SK_ZOOM * zc)
		self.qsx = (ox * SK_RX + oy * SK_RY + oz * SK_RZ) * scale + self.w * 0.5
		self.qsy = half_h - (ox * SK_UX + oy * SK_UY + oz * SK_UZ) * scale
		self.qzc = zc
		return True

	# a silk thread from (x0, y0, z0) to (x1, y1, z1), depth-tested against the traced scene; its hue turns with its
	# angle on the screen and with time, the way silk breaks light into colors
	def thread(self, grid, x0, y0, z0, x1, y1, z1, bright):
		if not self.project(x0, y0, z0):
			return
		sx0 = self.qsx
		sy0 = self.qsy
		z0c = self.qzc
		if not self.project(x1, y1, z1):
			return
		ddx = self.qsx - sx0
		ddy = self.qsy - sy0
		adx = -ddx if ddx < 0.0 else ddx
		ady = -ddy if ddy < 0.0 else ddy
		steps = int((adx if adx > ady else ady) + 1.0)
		if steps > 400:
			return
		hue = atan2(ddy, ddx) * 0.4 + self.tt * 0.15 + (sx0 + sy0) * 0.006
		hue = hue - floor(hue / 8.0) * 8.0
		hq = int(hue)
		hq = 7 if hq > 7 else hq
		ink = SK_SILK + hq * 6
		ix = ddx / steps
		iy = ddy / steps
		iz = (self.qzc - z0c) / steps
		w = self.w
		h = self.h
		depth = self.depth
		bayer = SK_BAYER
		for s in range(steps + 1):
			px = floor(sx0 + ix * s)
			py = floor(sy0 + iy * s)
			if px < 0 or py < 0 or px >= w or py >= h:
				continue
			at = py * w + px
			zc = z0c + iz * s
			if depth[at] <= int(zc * 18.0):
				continue
			fog = 1.0 - zc / 11.0
			l = int(fog * bright * 5.5 + bayer[(py % 4) * 4 + px % 4] / 16.0 - 0.3)
			if l < 0:
				continue
			grid[at] = ink + (5 if l > 5 else l)

	def glint(self, grid, x, y, z):
		if not self.project(x, y, z):
			return
		px = floor(self.qsx)
		py = floor(self.qsy)
		w = self.w
		if px < 0 or py < 0 or px >= w or py >= self.h:
			return
		at = py * w + px
		if self.depth[at] <= int(self.qzc * 18.0):
			return
		grid[at] = SK_EYE + 7

	# the silk, drawn over the trace: the dragline the sleeper hangs from and the spokes cast after it, a spark at each
	# growing tip; the hub's mesh; the capture spiral, every winding growing at once behind the running spider on a fan
	# of fresh thread from its spinnerets; the strands that wind the cocoon. The strike's ring lights the threads it
	# passes, and the whole web flashes with dew as the spiral closes and again as the cocoon is sealed
	def draw_web(self, grid):
		tt = self.tt
		nspoke = self.nspoke
		web_point = self.web_point
		thread = self.thread
		glint = self.glint
		ring_light = self.ring_light
		shimmer = exp((self.t_spiral1 - tt) * 4.0) if tt >= self.t_spiral1 else 0.0
		sealed = tt - self.t_wrap1
		if sealed >= 0.0:
			shimmer = exp(0.0 - sealed * 5.0)
		spoke_ang = self.spoke_ang
		for k in range(nspoke):
			order = k if k < nspoke // 2 else k - 1
			grow = 1.0 if k == nspoke // 2 else (tt - self.t_spokes - order * self.t_spoke_gap) / 0.25
			if grow <= 0.0:
				continue
			grow = 1.0 if grow > 1.0 else grow
			ca = cos(spoke_ang[k]) * self.spoke_reach * grow / 10.0
			sa = sin(spoke_ang[k]) * self.spoke_reach * grow / 10.0
			web_point(0.0, 0.0)
			px = self.wx; py = self.wy; pz = self.wz
			for s in range(1, 11):
				web_point(ca * s, sa * s)
				thread(grid, px, py, pz, self.wx, self.wy, self.wz, 0.78 + shimmer * 0.4 + ring_light(ca * s, sa * s))
				px = self.wx; py = self.wy; pz = self.wz
			if grow < 1.0:
				glint(grid, px, py, pz)
		if tt > self.t_out:
			for ring in range(1, 3):
				rr = ring * 0.16
				web_point(rr, 0.0)
				px = self.wx; py = self.wy; pz = self.wz
				for s in range(1, 13):
					a = s * 0.5235988
					web_point(rr * cos(a), rr * sin(a))
					thread(grid, px, py, pz, self.wx, self.wy, self.wz, 0.8 + shimmer * 0.4)
					px = self.wx; py = self.wy; pz = self.wz
		sx = SK_AX - self.fx * 0.4; sy = SK_AY - self.fy * 0.4; sz = SK_AZ - self.fz * 0.4
		if tt > self.t_spiral0:
			sweep = self.laid(tt)
			spa = self.spa
			spb = self.spb
			lay_turn = self.lay_turn
			last = len(spa) - 1
			for i in range(1, last + 1):
				u = (sweep - lay_turn[i]) * nspoke
				if u <= 0.0:
					continue
				web_point(spa[i - 1], spb[i - 1])
				px = self.wx; py = self.wy; pz = self.wz
				if u < 1.0:
					web_point(spa[i - 1] + (spa[i] - spa[i - 1]) * u, spb[i - 1] + (spb[i] - spb[i - 1]) * u)
					thread(grid, px, py, pz, self.wx, self.wy, self.wz, 1.0)
					thread(grid, self.wx, self.wy, self.wz, sx, sy, sz, 1.2)
					glint(grid, self.wx, self.wy, self.wz)
					continue
				web_point(spa[i], spb[i])
				thread(grid, px, py, pz, self.wx, self.wy, self.wz, 0.88 + shimmer * 0.4 + ring_light(spa[i], spb[i]))
				if sin(tt * 1.3 + i * 1.7) > 0.93 - shimmer * 0.9:
					glint(grid, self.wx, self.wy, self.wz)
		if tt > self.t_wrap0 and tt < self.t_wrap1:
			rad = 0.07 + 0.14 * self.coc
			mox = self.mox; moy = self.moy; moz = self.moz
			mfx = self.mfx; mfy = self.mfy; mfz = self.mfz
			mrx = self.mrx; mry = self.mry; mrz = self.mrz
			mux = self.mux; muy = self.muy; muz = self.muz
			spin = self.spin
			for j in range(4):
				a = spin * 1.6 + j * 1.5708
				along = -0.2 + j * 0.1 + 0.07 * sin(spin + j)
				tx = mox + mfx * along + (mrx * cos(a) + mux * sin(a)) * rad
				ty = moy + mfy * along + (mry * cos(a) + muy * sin(a)) * rad
				tz = moz + mfz * along + (mrz * cos(a) + muz * sin(a)) * rad
				thread(grid, sx, sy, sz, tx, ty, tz, 1.25)
		if sealed >= 0.0 and sealed < 0.4:
			for k in range(12):
				a = k * 0.5235988 + 0.2
				r = 0.24 + sealed * 1.3
				glint(grid, self.mox + cos(a) * r, self.moy + sin(a) * r, self.moz + 0.05)

	def plot(self, grid, x, y, dq, level):
		w = self.w
		if x < 0 or y < 0 or x >= w or y >= self.h:
			return
		at = y * w + x
		if self.depth[at] <= dq:
			return
		cur = grid[at]
		if cur >= SK_SPARK and cur < SK_SPARK + 8 and cur - SK_SPARK >= level:
			return
		grid[at] = SK_SPARK + level

	def splat(self, grid, x, y, z, level, size):
		if not self.project(x, y, z):
			return
		plot = self.plot
		px = floor(self.qsx)
		py = floor(self.qsy)
		dq = int(self.qzc * 18.0)
		plot(grid, px, py, dq, level)
		if size > 0:
			halo = level - 3 if level > 2 else 0
			plot(grid, px + 1, py, dq, halo)
			plot(grid, px - 1, py, dq, halo)
			plot(grid, px, py + 1, dq, halo)
			plot(grid, px, py - 1, dq, halo)
		if size > 1:
			plot(grid, px + 1, py + 1, dq, level - 1 if level > 1 else 0)
			plot(grid, px + 1, py, dq, level)
			plot(grid, px, py + 1, dq, level)

	def draw_particles(self, grid):
		splat = self.splat
		ppx = self.ppx; ppy = self.ppy; ppz = self.ppz
		plife = self.plife
		for i in range(self.np):
			life = plife[i]
			if life <= 0.0:
				continue
			level = int(life * 7.99)
			splat(grid, ppx[i], ppy[i], ppz[i], level, 1 if life > 0.55 else 0)
		if SK_WAKE < 0.5:
			return
		tt = self.tt
		ux = self.ux; uy = self.uy; uz = self.uz
		for s in range(6):
			ph = tt * 4.0 + s * 1.047
			tw = sin(tt * 11.0 + s * 2.7)
			if tw < 0.2:
				continue
			rr = 0.1 + 0.05 * sin(tt * 3.0 + s)
			ca = cos(ph) * rr
			sa = sin(ph) * rr
			lift = 0.06 * sin(tt * 5.0 + s * 1.3)
			splat(grid, SK_GMX + SK_GAX * ca + SK_GBX * sa + ux * lift, SK_GMY + SK_GAY * ca + SK_GBY * sa + uy * lift, SK_GMZ + SK_GAZ * ca + SK_GBZ * sa + uz * lift, 7 if tw > 0.7 else 5, 1)


SCENES[9] = Ossuary
