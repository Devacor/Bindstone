-- OSSUARY (Lua 5.4 port of scenes/skull.jai): a hunt in twelve bars. A horned, three-eyed skull on a spider's body
-- hangs asleep from its dragline in a ribbed vault, wakes on bar 2, casts an orb web and runs its capture spiral; a
-- moth strikes on bar 6's downbeat, the spider runs it down on eight IK legs and wraps it in a cocoon by bar 8, then
-- turns on the viewer, stalks through bar 9 and lunges at the lens on bar 10. The creature, the moth and its cocoon
-- are distance fields in one march of about 6400 rays whatever the frame's size; the silk is drawn over the trace,
-- depth-tested.
-- The scene's globals are file locals (upvalues): with them all the chunk stays under Lua's 200 locals.

local sqrt, sin, cos, atan2, floor, exp, abs = math.sqrt, math.sin, math.cos, math.atan, math.floor, math.exp, math.abs
local itrunc = itrunc

local SK_T = 0.0
local SK_EX, SK_EY, SK_EZ = 0.0, 0.0, 0.0
local SK_FX, SK_FY, SK_FZ = 0.0, 0.0, 1.0
local SK_RX, SK_RY, SK_RZ = 1.0, 0.0, 0.0
local SK_UX, SK_UY, SK_UZ = 0.0, 1.0, 0.0
local SK_BCX, SK_BCY, SK_BCZ, SK_BR = 0.0, 0.0, 0.0, 1.0
local SK_AX, SK_AY, SK_AZ, SK_AR = 0.0, 0.0, 0.0, 0.24
local SK_TX, SK_TY, SK_TZ, SK_TR = 0.0, 0.0, 0.0, 0.14
local SK_HX, SK_HY, SK_HZ, SK_HR = 0.0, 0.0, 0.0, 0.24
local SK_NX, SK_NY, SK_NZ = 0.0, 0.0, 0.0
local SK_JX, SK_JY, SK_JZ = 0.0, 0.0, 0.0
local SK_PFX, SK_PFY, SK_PFZ = 0.0, 0.0, 1.0
local SK_PUX, SK_PUY, SK_PUZ = 0.0, 1.0, 0.0
local SK_PRX, SK_PRY, SK_PRZ = 1.0, 0.0, 0.0
local SK_QFX, SK_QFY, SK_QFZ = 0.0, 0.0, 1.0
local SK_QUX, SK_QUY, SK_QUZ = 0.0, 1.0, 0.0
local SK_LX, SK_LY, SK_LZ = 0.0, 0.0, 0.0
local SK_FLICK = 0.0
local SK_KAGE = 9.0        -- seconds since the cue's last kick
local SK_KICK = 0.0        -- and its envelope
local SK_ZOOM = 0.5        -- the lens: half the view's height at unit distance, punching in on the hits
local SK_PUNCH = 0.0       -- how far it has punched in
local SK_WAKE = 1.0        -- 0 while the creature sleeps
local SK_GLARE = 0.0       -- the hunter's glare at the viewer, which kindles its eyes and lights its face
local SK_CAX = {}
local SK_CAY = {}
local SK_CAZ = {}
local SK_CDX = {}
local SK_CDY = {}
local SK_CDZ = {}
local SK_CID = {}
local SK_CR = {}
local SK_CMX = {}
local SK_CMY = {}
local SK_CMZ = {}
local SK_CB = {}
local SK_HGX, SK_HGY, SK_HGZ = 0.0, 0.0, 0.0
local SK_BGX, SK_BGY, SK_BGZ = 0.0, 0.0, 0.0
local SK_NC = 0
local SK_NBONE = 5
local SK_CROWN0 = 0
local SK_CRX, SK_CRY, SK_CRZ = 0.0, 0.0, 0.0
local SK_SKB = 0.4
local SK_CRB = 0.4
local SK_GMX, SK_GMY, SK_GMZ = 0.0, 0.0, 0.0
local SK_GAX, SK_GAY, SK_GAZ = 1.0, 0.0, 0.0
local SK_GBX, SK_GBY, SK_GBZ = 0.0, 0.0, 1.0
local SK_GX = {}
local SK_GY = {}
local SK_GZ = {}
local SK_GR = {}
local SK_NG = 0
local SK_NSOCK = 3
local SK_W = 0
local SK_H = 0
local SK_BONE = 0
local SK_CHIT = 0
local SK_EYE = 0
local SK_MARK = 0
local SK_SPARK = 0
local SK_GEM = 0
local SK_ROWS = {}
local SK_BAYER = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5}
local SK_KX, SK_KY, SK_KZ = 0.0, 1.0, 0.0
local SK_MOTH = false
local SK_MX, SK_MY, SK_MZ, SK_MB = 0.0, 0.0, 0.0, 0.6
local SK_MFX, SK_MFY, SK_MFZ = 0.0, 1.0, 0.0
local SK_MUX, SK_MUY, SK_MUZ = 0.0, 0.0, 1.0
local SK_MRX, SK_MRY, SK_MRZ = 1.0, 0.0, 0.0
local SK_WSC = 1.0
local SK_COC = 0.0
local SK_SPIN = 0.0
local SK_WOX, SK_WOY, SK_WOZ = {0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0}
local SK_WSX, SK_WSY, SK_WSZ = {0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0}
local SK_WNX, SK_WNY, SK_WNZ = {0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0}, {0.0, 0.0, 0.0, 0.0}
local SK_WCA = {0.22, 0.22, 0.15, 0.15}
local SK_WCB = {0.0, 0.0, -0.075, -0.075}
local SK_WSA = {0.22, 0.22, 0.15, 0.15}
local SK_WSB = {0.105, 0.105, 0.09, 0.09}
local SK_WSW = {-0.25, -0.25, -0.35, -0.35}
local SK_WING = 0
local SK_FUR = 0
local SK_COCOON = 0
local SK_SILK = 0
local SK_BACK = 0
local SK_BACK_HUE = 0
local SK_BACK_COLS = 0
local SK_BACK_LIGHT = {}
local SK_BACK_WARM = {}

-- array.resize(n, fill): grows with fill, shrinks from the end, keeps the rest
local function sk_resize(a, n, fill)
	for i = #a + 1, n do a[i] = fill end
	for i = #a, n + 1, -1 do a[i] = nil end
end

-- a moth's wing k (0, 1 fore, 2, 3 hind): an ellipse swept back along the body, in a plane through its root
local function sk_wing(x, y, z, k)
	local k1 = k + 1
	local px = x - SK_WOX[k1]; local py = y - SK_WOY[k1]; local pz = z - SK_WOZ[k1]
	local a = px * SK_WSX[k1] + py * SK_WSY[k1] + pz * SK_WSZ[k1]
	local b = px * SK_MFX + py * SK_MFY + pz * SK_MFZ
	local c = px * SK_WNX[k1] + py * SK_WNY[k1] + pz * SK_WNZ[k1]
	local u = (a - SK_WCA[k1]) / SK_WSA[k1]
	local v = (b - SK_WCB[k1] - a * SK_WSW[k1]) / SK_WSB[k1]
	local d = (sqrt(u * u + v * v) - 1.0) * SK_WSB[k1]
	local dc = abs(c) - 0.007
	return d > dc and d or dc
end

-- the wing's pattern: 0 gold, 1 umber vein or band, 2 black ring, 3 iris, 4 pupil, 5 violet margin
local function sk_wing_tone(x, y, z, k)
	local k1 = k + 1
	local px = x - SK_WOX[k1]; local py = y - SK_WOY[k1]; local pz = z - SK_WOZ[k1]
	local a = px * SK_WSX[k1] + py * SK_WSY[k1] + pz * SK_WSZ[k1]
	local b = px * SK_MFX + py * SK_MFY + pz * SK_MFZ
	local u = (a - SK_WCA[k1]) / SK_WSA[k1]
	local v = (b - SK_WCB[k1] - a * SK_WSW[k1]) / SK_WSB[k1]
	local r = sqrt(u * u + v * v)
	if r > 0.82 then return 5 end
	local fore = k < 2
	local eu = u - (fore and 0.3 or 0.05)
	local ev = v - (fore and -0.08 or 0.05)
	local e = sqrt(eu * eu + ev * ev) / (fore and 0.3 or 0.45)
	if e < 0.34 then return 4 end
	if e < 0.74 then return 3 end
	if e < 1.0 then return 2 end
	local band = u * 0.8 + v * 0.5 + (fore and 0.45 or -0.2)
	if band > -0.1 and band < 0.1 then return 1 end
	local vein = sin(atan2(v, u + 1.05) * 11.0)
	return vein > 0.9 and 1 or 0
end

-- the moth: a furred body and head, four wings, and the cocoon the spider wraps around it
local function sk_moth(x, y, z)
	local ox = x - SK_MX; local oy = y - SK_MY; local oz = z - SK_MZ
	local f = ox * SK_MFX + oy * SK_MFY + oz * SK_MFZ
	local fc = f < -0.25 and -0.25 or (f > 0.09 and 0.09 or f)
	local qx = ox - SK_MFX * fc; local qy = oy - SK_MFY * fc; local qz = oz - SK_MFZ * fc
	local d = sqrt(qx * qx + qy * qy + qz * qz) - (0.066 + (fc < 0.0 and fc * 0.12 or 0.0))
	local hx = ox - SK_MFX * 0.15; local hy = oy - SK_MFY * 0.15; local hz = oz - SK_MFZ * 0.15
	local dh = sqrt(hx * hx + hy * hy + hz * hz) - 0.058
	d = d < dh and d or dh
	if SK_WSC > 0.05 then
		for k = 0, 3 do
			local dw = sk_wing(x, y, z, k)
			d = d < dw and d or dw
		end
	end
	if SK_COC > 0.0 then
		local cf = f + 0.07
		local rr = ox * ox + oy * oy + oz * oz - f * f
		rr = rr < 0.0 and 0.0 or sqrt(rr)
		local ca = 0.18 + 0.22 * SK_COC
		local cb = 0.05 + 0.14 * SK_COC
		local dc = (sqrt(cf * cf / (ca * ca) + rr * rr / (cb * cb)) - 1.0) * cb
		d = d < dc and d or dc
	end
	return d
end

-- which part of the moth is nearest: -1 body, -2 cocoon, else the wing's index
local function sk_moth_part(x, y, z)
	local ox = x - SK_MX; local oy = y - SK_MY; local oz = z - SK_MZ
	local f = ox * SK_MFX + oy * SK_MFY + oz * SK_MFZ
	local fc = f < -0.25 and -0.25 or (f > 0.09 and 0.09 or f)
	local qx = ox - SK_MFX * fc; local qy = oy - SK_MFY * fc; local qz = oz - SK_MFZ * fc
	local best = sqrt(qx * qx + qy * qy + qz * qz) - (0.066 + (fc < 0.0 and fc * 0.12 or 0.0))
	local hx = ox - SK_MFX * 0.15; local hy = oy - SK_MFY * 0.15; local hz = oz - SK_MFZ * 0.15
	local dh = sqrt(hx * hx + hy * hy + hz * hz) - 0.058
	best = best < dh and best or dh
	local result = -1
	if SK_WSC > 0.05 then
		for k = 0, 3 do
			local dw = sk_wing(x, y, z, k)
			if dw < best then best = dw; result = k end
		end
	end
	if SK_COC > 0.0 then
		local cf = f + 0.07
		local rr = ox * ox + oy * oy + oz * oz - f * f
		rr = rr < 0.0 and 0.0 or sqrt(rr)
		local ca = 0.18 + 0.22 * SK_COC
		local cb = 0.05 + 0.14 * SK_COC
		local dc = (sqrt(cf * cf / (ca * ca) + rr * rr / (cb * cb)) - 1.0) * cb
		if dc < best then result = -2 end
	end
	return result
end

-- capsules [first, last) against best: a capsule whose bounding sphere cannot beat best is skipped without a root
local function sk_capsules(x, y, z, first, last, best)
	for i = first + 1, last do
		local mx = x - SK_CMX[i]
		local my = y - SK_CMY[i]
		local mz = z - SK_CMZ[i]
		local reach = best + SK_CB[i]
		if not (mx * mx + my * my + mz * mz > reach * reach) then
			local pax = x - SK_CAX[i]
			local pay = y - SK_CAY[i]
			local paz = z - SK_CAZ[i]
			local dx = SK_CDX[i]
			local dy = SK_CDY[i]
			local dz = SK_CDZ[i]
			local hh = (pax * dx + pay * dy + paz * dz) * SK_CID[i]
			hh = hh < 0.0 and 0.0 or (hh > 1.0 and 1.0 or hh)
			local ex = pax - dx * hh
			local ey = pay - dy * hh
			local ez = paz - dz * hh
			local d = sqrt(ex * ex + ey * ey + ez * ez) - SK_CR[i]
			best = d < best and d or best
		end
	end
	return best
end

local function sk_glows(x, y, z)
	local best = 99.0
	for i = 1, SK_NG do
		local dx = x - SK_GX[i]
		local dy = y - SK_GY[i]
		local dz = z - SK_GZ[i]
		local d = sqrt(dx * dx + dy * dy + dz * dz) - SK_GR[i]
		best = d < best and d or best
	end
	return best
end

-- the skull against best: an ellipsoid (up 1.18, forward sqrt(0.92)) with its jaw, eye sockets and nose cut in. It is no
-- nearer than sqrt(0.92) of the distance to its center less SK_SKB (the jaw's reach), so it is skipped when that cannot beat best
local function sk_skull(x, y, z, best)
	local hx = x - SK_HX
	local hy = y - SK_HY
	local hz = z - SK_HZ
	if sqrt(hx * hx + hy * hy + hz * hz) * 0.959 - SK_SKB >= best then return best end
	local cu = hx * SK_PUX + hy * SK_PUY + hz * SK_PUZ
	local cf = hx * SK_PFX + hy * SK_PFY + hz * SK_PFZ
	local cr = hx * SK_PRX + hy * SK_PRY + hz * SK_PRZ
	cu = cu * 1.18
	local d = sqrt(cu * cu + cf * cf * 0.92 + cr * cr) - SK_HR
	local jx = x - SK_JX
	local jy = y - SK_JY
	local jz = z - SK_JZ
	local jr = jx * SK_PRX + jy * SK_PRY + jz * SK_PRZ
	jr = jr < -0.085 and -0.085 or (jr > 0.085 and 0.085 or jr)
	local qx = jx - SK_PRX * jr
	local qy = jy - SK_PRY * jr
	local qz = jz - SK_PRZ * jr
	local dj = sqrt(qx * qx + qy * qy + qz * qz) - 0.09
	d = d < dj and d or dj
	for i = 1, SK_NSOCK do
		local sx = x - SK_GX[i]
		local sy = y - SK_GY[i]
		local sz = z - SK_GZ[i]
		local c = SK_GR[i] * 2.5 - sqrt(sx * sx + sy * sy + sz * sz)
		d = d > c and d or c
	end
	local nx = x - SK_NX; local ny = y - SK_NY; local nz = z - SK_NZ
	local cn = 0.042 - sqrt(nx * nx + ny * ny + nz * nz)
	d = d > cn and d or cn
	return d < best and d or best
end

local function sk_body(x, y, z)
	local ax = x - SK_AX; local ay = y - SK_AY; local az = z - SK_AZ
	local af = ax * SK_QFX + ay * SK_QFY + az * SK_QFZ
	local au = ax * SK_QUX + ay * SK_QUY + az * SK_QUZ
	local ar = ax * SK_PRX + ay * SK_PRY + az * SK_PRZ
	af = af * 0.74
	au = au * 1.04
	local da = (sqrt(af * af + au * au + ar * ar) - SK_AR) * 0.74
	local tx = x - SK_TX; local ty = y - SK_TY; local tz = z - SK_TZ
	local dt = sqrt(tx * tx + ty * ty + tz * tz) - SK_TR
	local k = 0.09
	local hh = 0.5 + 0.5 * (dt - da) / k
	hh = hh < 0.0 and 0.0 or (hh > 1.0 and 1.0 or hh)
	return dt + (da - dt) * hh - k * hh * (1.0 - hh)
end

-- the crown's ring; its flames are the capsules [SK_CROWN0, SK_NC)
local function sk_ring(x, y, z)
	local ox = x - SK_CRX; local oy = y - SK_CRY; local oz = z - SK_CRZ
	local cu = ox * SK_PUX + oy * SK_PUY + oz * SK_PUZ
	local px = ox - SK_PUX * cu; local py = oy - SK_PUY * cu; local pz = oz - SK_PUZ * cu
	local ring = sqrt(px * px + py * py + pz * pz) - 0.2
	return sqrt(ring * ring + cu * cu) - 0.028
end

local function sk_gem(x, y, z)
	local ox = x - SK_GMX; local oy = y - SK_GMY; local oz = z - SK_GMZ
	local a = abs(ox * SK_GAX + oy * SK_GAY + oz * SK_GAZ)
	local b = abs(ox * SK_PUX + oy * SK_PUY + oz * SK_PUZ)
	local c = abs(ox * SK_GBX + oy * SK_GBY + oz * SK_GBZ)
	return (a + b * 0.62 + c - 0.075) * 0.54
end

-- the creature along one ray: only the parts the ray passes near (groups: 1 body, 2 head and its eyes, 4 moth; cand: capsule
-- indices, glows as 100 + index).
-- A part the ray never enters changes the step lengths, never a hit.
local function sk_spider(x, y, z, groups, cand, nc)
	local d = 99.0
	if (groups & 1) ~= 0 then d = sk_body(x, y, z) end
	if (groups & 2) ~= 0 then
		d = sk_skull(x, y, z, d)
		-- the crown's ring and flames lie within SK_CRB of its center
		local ox = x - SK_CRX; local oy = y - SK_CRY; local oz = z - SK_CRZ
		if sqrt(ox * ox + oy * oy + oz * oz) - SK_CRB < d then
			local dr = sk_ring(x, y, z)
			d = d < dr and d or dr
			d = sk_capsules(x, y, z, SK_CROWN0, SK_NC, d)
		end
		local dg = sk_gem(x, y, z)
		d = d < dg and d or dg
		for e = 1, SK_NSOCK do
			local ex = x - SK_GX[e]
			local ey = y - SK_GY[e]
			local ez = z - SK_GZ[e]
			local de = sqrt(ex * ex + ey * ey + ez * ez) - SK_GR[e]
			d = d < de and d or de
		end
	end
	if (groups & 4) ~= 0 then
		local dm = sk_moth(x, y, z)
		d = d < dm and d or dm
	end
	for k = 1, nc do
		local c = cand[k]
		if c >= 100 then
			local g = c - 99
			local gx = x - SK_GX[g]
			local gy = y - SK_GY[g]
			local gz = z - SK_GZ[g]
			local dg2 = sqrt(gx * gx + gy * gy + gz * gz) - SK_GR[g]
			d = d < dg2 and d or dg2
		else
			c = c + 1
			local mx = x - SK_CMX[c]
			local my = y - SK_CMY[c]
			local mz = z - SK_CMZ[c]
			local reach = d + SK_CB[c]
			if not (mx * mx + my * my + mz * mz > reach * reach) then
				local pax = x - SK_CAX[c]
				local pay = y - SK_CAY[c]
				local paz = z - SK_CAZ[c]
				local cdx = SK_CDX[c]
				local cdy = SK_CDY[c]
				local cdz = SK_CDZ[c]
				local hh = (pax * cdx + pay * cdy + paz * cdz) * SK_CID[c]
				hh = hh < 0.0 and 0.0 or (hh > 1.0 and 1.0 or hh)
				local ex = pax - cdx * hh
				local ey = pay - cdy * hh
				local ez = paz - cdz * hh
				local dc = sqrt(ex * ex + ey * ey + ez * ez) - SK_CR[c]
				d = d < dc and d or dc
			end
		end
	end
	return d
end

-- the ray's candidate parts cand[first, last) against best: capsule indices in ascending order, then glows as 100 + index
local function sk_candidates(x, y, z, cand, first, last, d)
	for k = first + 1, last do
		local c = cand[k]
		if c >= 100 then
			local g = c - 99
			local gx = x - SK_GX[g]
			local gy = y - SK_GY[g]
			local gz = z - SK_GZ[g]
			local dg2 = sqrt(gx * gx + gy * gy + gz * gz) - SK_GR[g]
			d = d < dg2 and d or dg2
		else
			c = c + 1
			local mx = x - SK_CMX[c]
			local my = y - SK_CMY[c]
			local mz = z - SK_CMZ[c]
			local reach = d + SK_CB[c]
			if not (mx * mx + my * my + mz * mz > reach * reach) then
				local pax = x - SK_CAX[c]
				local pay = y - SK_CAY[c]
				local paz = z - SK_CAZ[c]
				local cdx = SK_CDX[c]
				local cdy = SK_CDY[c]
				local cdz = SK_CDZ[c]
				local hh = (pax * cdx + pay * cdy + paz * cdz) * SK_CID[c]
				hh = hh < 0.0 and 0.0 or (hh > 1.0 and 1.0 or hh)
				local ex = pax - cdx * hh
				local ey = pay - cdy * hh
				local ez = paz - cdz * hh
				local dc = sqrt(ex * ex + ey * ey + ez * ez) - SK_CR[c]
				d = d < dc and d or dc
			end
		end
	end
	return d
end

-- does the ray (origin o, unit direction d) pass within r of c ahead of the origin
local function sk_ray_near(ox, oy, oz, dx, dy, dz, cx, cy, cz, r)
	local vx = cx - ox
	local vy = cy - oy
	local vz = cz - oz
	local tc = vx * dx + vy * dy + vz * dz
	if tc < -r then return false end
	return vx * vx + vy * vy + vz * vz - tc * tc < r * r
end

-- 1 bone, 2 chitin, 3 glow, 4 marking, 5 ruby, 6 moth at a point the march stopped on: the nearest wins, ties to the earlier.
-- A capsule the ray passed no nearer than 0.01 to is farther than the march's reach from every point on it, so only the ray's
-- candidates are tried (cand[0, nbone) bone, then chitin up to ncap)
local function sk_material(x, y, z, cand, nbone, ncap)
	local best = 99.0
	local m = 0
	local bone = sk_candidates(x, y, z, cand, 0, nbone, sk_skull(x, y, z, best))
	if bone < best then best = bone; m = 1 end
	local db = sk_body(x, y, z)
	local chit = sk_candidates(x, y, z, cand, nbone, ncap, db < best and db or best)
	if chit < best then best = chit; m = 2 end
	local de = sk_glows(x, y, z)
	de = de < best and de or best
	local ox = x - SK_CRX; local oy = y - SK_CRY; local oz = z - SK_CRZ
	if sqrt(ox * ox + oy * oy + oz * oz) - SK_CRB < de then
		local dr = sk_ring(x, y, z)
		de = de < dr and de or dr
		de = sk_capsules(x, y, z, SK_CROWN0, SK_NC, de)
	end
	if de < best then best = de; m = 3 end
	local dgm = sk_gem(x, y, z)
	if dgm < best then best = dgm; m = 5 end
	if SK_MOTH then
		local mx = x - SK_MX; local my = y - SK_MY; local mz = z - SK_MZ
		if sqrt(mx * mx + my * my + mz * mz) - SK_MB < best then
			local dm = sk_moth(x, y, z)
			if dm < best then m = 6 end
		end
	end
	if m == 2 and chit == db then
		-- chevrons crawl down the abdomen's back, a stripe runs along its spine
		local ax = x - SK_AX; local ay = y - SK_AY; local az = z - SK_AZ
		local au = ax * SK_QUX + ay * SK_QUY + az * SK_QUZ
		local ar = ax * SK_PRX + ay * SK_PRY + az * SK_PRZ
		local af = ax * SK_QFX + ay * SK_QFY + az * SK_QFZ
		local ar2 = ar < 0.0 and -ar or ar
		if au > 0.07 and af > -0.33 and af < 0.2 then
			local v = af * 7.0 - ar2 * 9.0 + SK_T * 1.6
			if v - floor(v) < 0.3 or ar2 < 0.018 then m = 4 end
		end
	end
	return m
end

-- where the ray (origin o, unit direction d) is inside the sphere (c, r): widens [t_near, t_far] to hold that stretch
local function sk_ray_span(ox, oy, oz, dx, dy, dz, cx, cy, cz, r, t_near, t_far)
	local vx = cx - ox
	local vy = cy - oy
	local vz = cz - oz
	local tc = vx * dx + vy * dy + vz * dz
	local h = r * r - (vx * vx + vy * vy + vz * vz - tc * tc)
	if h <= 0.0 then return t_near, t_far end
	h = sqrt(h)
	t_near = tc - h < t_near and tc - h or t_near
	t_far = tc + h > t_far and tc + h or t_far
	return t_near, t_far
end

-- a pixel's palette index plus its depth (0..254) above bit 10, for the silk and the embers drawn over it, or -1 where the ray
-- meets neither the creature nor the moth; row: the row's candidate parts (see sk_row)
local function sk_trace(dx, dy, dz, px, py, cand, row, nrow)
	local ox = SK_EX
	local oy = SK_EY
	local oz = SK_EZ
	local groups = 0
	local nc = 0
	local nbone = 0
	local ncap = 0
	if sk_ray_near(ox, oy, oz, dx, dy, dz, SK_BGX, SK_BGY, SK_BGZ, 0.7) then groups = 1 end
	if sk_ray_near(ox, oy, oz, dx, dy, dz, SK_HGX, SK_HGY, SK_HGZ, 0.74) then groups = groups + 2 end
	if SK_MOTH and sk_ray_near(ox, oy, oz, dx, dy, dz, SK_MX, SK_MY, SK_MZ, SK_MB) then groups = groups + 4 end
	for k = 1, nrow do
		local c = row[k]
		if c < 100 then
			if sk_ray_near(ox, oy, oz, dx, dy, dz, SK_CMX[c + 1], SK_CMY[c + 1], SK_CMZ[c + 1], SK_CB[c + 1] + 0.01) then
				nc = nc + 1
				cand[nc] = c
				ncap = nc
				if c < SK_NBONE then nbone = nc end
			end
		else
			local g = c - 99
			if sk_ray_near(ox, oy, oz, dx, dy, dz, SK_GX[g], SK_GY[g], SK_GZ[g], SK_GR[g] + 0.01) then
				nc = nc + 1
				cand[nc] = c
			end
		end
	end
	if groups == 0 and nc == 0 then return -1 end
	local t = 99.0
	local t_far = 0.0
	if (groups & 3) ~= 0 or nc ~= 0 then t, t_far = sk_ray_span(ox, oy, oz, dx, dy, dz, SK_BCX, SK_BCY, SK_BCZ, SK_BR, t, t_far) end
	if (groups & 4) ~= 0 then t, t_far = sk_ray_span(ox, oy, oz, dx, dy, dz, SK_MX, SK_MY, SK_MZ, SK_MB, t, t_far) end
	t = t < 0.04 and 0.04 or t
	local hit = false
	local i = 0
	while i < 48 and t < t_far do
		local d = sk_spider(ox + dx * t, oy + dy * t, oz + dz * t, groups, cand, nc)
		local reach = 0.001 + t * 0.0015
		if d < (reach < 0.0085 and reach or 0.0085) then
			hit = true
			break
		end
		t = t + d * 0.85
		i = i + 1
	end
	if not hit then return -1 end
	local dith = SK_BAYER[(py % 4) * 4 + px % 4 + 1] / 16.0 - 0.47
	local depth = itrunc(t * 18.0)
	depth = (depth > 254 and 254 or depth) * 1024
	local x = ox + dx * t
	local y = oy + dy * t
	local z = oz + dz * t
	local m = sk_material(x, y, z, cand, nbone, ncap)
	local fog = 1.0 - t / 14.0
	fog = fog < 0.0 and 0.0 or fog
	local lx = SK_LX - x
	local ly = SK_LY - y
	local lz = SK_LZ - z
	local ld2 = lx * lx + ly * ly + lz * lz
	local lil = 1.0 / sqrt(ld2 + 0.0001)
	lx = lx * lil; ly = ly * lil; lz = lz * lil
	if m == 3 then
		local e = itrunc((5.4 + SK_FLICK) * SK_WAKE + dith)
		for k = 1, SK_NSOCK do
			local ex = x - SK_GX[k]; local ey = y - SK_GY[k]; local ez = z - SK_GZ[k]
			local el = sqrt(ex * ex + ey * ey + ez * ez)
			if el < SK_GR[k] + 0.012 then
				-- an eye burns red at its rim and yellow where it faces the viewer, with a
				-- wet glint where its sphere mirrors the key light
				local facing = -(ex * dx + ey * dy + ez * dz) / (el + 0.0000001)
				e = itrunc((2.6 + 3.2 * facing * facing + SK_FLICK) * SK_WAKE + dith)
				local hx = SK_KX - dx; local hy = SK_KY - dy; local hz = SK_KZ - dz
				local hl = sqrt(hx * hx + hy * hy + hz * hz) * el + 0.0000001
				if (ex * hx + ey * hy + ez * hz) / hl > 0.93 and SK_WAKE > 0.5 then e = 7 end
			end
		end
		return SK_EYE + (e < 0 and 0 or (e > 7 and 7 or e)) + depth
	end
	if m == 5 then
		local ox = x - SK_GMX; local oy = y - SK_GMY; local oz = z - SK_GMZ
		local a = ox * SK_GAX + oy * SK_GAY + oz * SK_GAZ
		local b = ox * SK_PUX + oy * SK_PUY + oz * SK_PUZ
		local c = ox * SK_GBX + oy * SK_GBY + oz * SK_GBZ
		local sa = a < 0.0 and -1.0 or 1.0
		local sb = b < 0.0 and -1.0 or 1.0
		local sc = c < 0.0 and -1.0 or 1.0
		local gnx = SK_GAX * sa + SK_PUX * sb * 0.62 + SK_GBX * sc
		local gny = SK_GAY * sa + SK_PUY * sb * 0.62 + SK_GBY * sc
		local gnz = SK_GAZ * sa + SK_PUZ * sb * 0.62 + SK_GBZ * sc
		local gl = 1.0 / sqrt(gnx * gnx + gny * gny + gnz * gnz)
		local face = -(gnx * dx + gny * dy + gnz * dz) * gl
		face = face < 0.0 and 0.0 or face
		local facet = (sa > 0.0 and 1 or 0) + (sb > 0.0 and 2 or 0) + (sc > 0.0 and 4 or 0)
		local twinkle = sin(SK_T * 9.0 + facet * 2.3)
		local lit = 0.25 + face * face * 0.6 + (twinkle > 0.85 and 0.6 or 0.0)
		local r = itrunc(lit * 7.0 + dith + 0.5)
		return SK_GEM + (r < 0 and 0 or (r > 7 and 7 or r)) + depth
	end
	local hstep = 0.0015
	local n0 = sk_spider(x + hstep, y - hstep, z - hstep, groups, cand, nc)
	local n1 = sk_spider(x - hstep, y - hstep, z + hstep, groups, cand, nc)
	local n2 = sk_spider(x - hstep, y + hstep, z - hstep, groups, cand, nc)
	local n3 = sk_spider(x + hstep, y + hstep, z + hstep, groups, cand, nc)
	local nx = n0 - n1 - n2 + n3
	local ny = -n0 - n1 + n2 + n3
	local nz = -n0 + n1 - n2 + n3
	local nl = 1.0 / sqrt(nx * nx + ny * ny + nz * nz + 0.0000001)
	nx = nx * nl; ny = ny * nl; nz = nz * nl
	local vd = -(nx * dx + ny * dy + nz * dz)
	vd = vd < 0.0 and 0.0 or vd
	local up = nx * SK_PUX + ny * SK_PUY + nz * SK_PUZ
	up = up < 0.0 and 0.0 or up
	local rim = 1.0 - vd
	rim = rim * rim * rim
	local eyel = nx * lx + ny * ly + nz * lz
	eyel = eyel < 0.0 and 0.0 or eyel / (1.0 + ld2 * 6.0)
	local key = nx * SK_KX + ny * SK_KY + nz * SK_KZ
	if m == 6 then
		local part = sk_moth_part(x, y, z)
		if part >= 0 then
			-- the wings glow through: lit from either side
			local tl = 0.5 + 0.55 * (key < 0.0 and -key or key) + eyel * 0.6
			local tone = sk_wing_tone(x, y, z, part)
			if tone == 3 and part >= 2 then tone = 6 end
			local l = itrunc(tl * fog * 7.0 + dith + 0.5)
			return SK_WING + tone * 8 + (l < 0 and 0 or (l > 7 and 7 or l)) + depth
		end
		if part == -2 then
			-- the cocoon: silk wound in bands that turn as the spider spins it
			local ox2 = x - SK_MX; local oy2 = y - SK_MY; local oz2 = z - SK_MZ
			local f = ox2 * SK_MFX + oy2 * SK_MFY + oz2 * SK_MFZ
			local ang = atan2(ox2 * SK_MUX + oy2 * SK_MUY + oz2 * SK_MUZ, ox2 * SK_MRX + oy2 * SK_MRY + oz2 * SK_MRZ)
			local band = sin(f * 70.0 + ang * 2.0 + SK_SPIN * 2.0)
			local cl = (0.2 + vd * 0.45 + rim * 0.35 + (key > 0.0 and key * 0.25 or 0.0)) * (0.72 + 0.28 * band)
			local l = itrunc(cl * fog * 9.0 + dith + 0.5)
			return SK_COCOON + (l < 0 and 0 or (l > 9 and 9 or l)) + depth
		end
		local fl = 0.12 + vd * 0.55 + rim * 0.25 + eyel * 0.5
		local l = itrunc(fl * fog * 7.0 + dith * 1.6 + 0.5)
		return SK_FUR + (l < 0 and 0 or (l > 7 and 7 or l)) + depth
	end
	if m == 1 then
		local lit = 0.1 + vd * (0.55 + SK_GLARE * 0.4) + up * 0.22 + rim * 0.25
		local so = 99.0
		for k = 1, SK_NSOCK do
			local ox2 = x - SK_GX[k]; local oy2 = y - SK_GY[k]; local oz2 = z - SK_GZ[k]
			local q = sqrt(ox2 * ox2 + oy2 * oy2 + oz2 * oz2) - (SK_GR[k] < 0.03 and SK_GR[k] or 0.03) * 2.5
			so = q < so and q or so
		end
		if so < 0.012 then lit = lit * 0.15 + 0.07 * (1.0 + SK_FLICK) end
		local nsx = x - SK_NX; local nsy = y - SK_NY; local nsz = z - SK_NZ
		if nsx * nsx + nsy * nsy + nsz * nsz < 0.0025 then lit = lit * 0.2 end
		local jx = x - SK_JX; local jy = y - SK_JY; local jz = z - SK_JZ
		local jf = jx * SK_PFX + jy * SK_PFY + jz * SK_PFZ
		local ju = jx * SK_PUX + jy * SK_PUY + jz * SK_PUZ
		if jf > 0.045 and ju > -0.05 and ju < 0.042 then
			local tr = (jx * SK_PRX + jy * SK_PRY + jz * SK_PRZ) * 44.0
			if tr - floor(tr) < 0.24 or (ju > -0.008 and ju < 0.006) then lit = lit * 0.22 end
		end
		local b = itrunc(lit * fog * 11.0 + dith + 0.5)
		return SK_BONE + (b < 0 and 0 or (b > 11 and 11 or b)) + depth
	end
	-- a glossy highlight of the key light
	local hx = SK_KX - dx; local hy = SK_KY - dy; local hz = SK_KZ - dz
	local hl = 1.0 / sqrt(hx * hx + hy * hy + hz * hz + 0.0000001)
	local spec = (nx * hx + ny * hy + nz * hz) * hl
	spec = spec < 0.0 and 0.0 or spec
	spec = spec * spec; spec = spec * spec; spec = spec * spec; spec = spec * spec
	if m == 4 then
		local k = itrunc((0.45 + vd * 0.45 + 0.3 * sin(SK_T * 4.0) + spec * 0.6) * 5.0 + dith + 0.5)
		return SK_MARK + (k < 0 and 0 or (k > 5 and 5 or k)) + depth
	end
	-- beetle iridescence: the shell turns from wine through violet to teal toward its rim
	local lit = 0.1 + vd * 0.42 + rim * 0.45 + eyel * 0.9 + (key > 0.0 and key * 0.3 or 0.0) + spec * 1.1
	local band = vd > 0.68 and 0 or (vd > 0.36 and 1 or 2)
	local c = itrunc(lit * fog * 11.0 + dith + 0.5)
	return SK_CHIT + band * 12 + (c < 0 and 0 or (c > 11 and 11 or c)) + depth
end

-- the vault behind the web at every fourth traced pixel, as light and the creature's warm aura: a ribbed dome about the hub
-- with a pale oculus at its crown, a ring of pillars nearer the eye, mist drifting across, a faint ring leaving on each kick
local function sk_backdrop(dim, flash)
	local cols = SK_W // 4 + 2
	local rows = SK_H // 4 + 2
	SK_BACK_COLS = cols
	sk_resize(SK_BACK_LIGHT, cols * rows, 0.0)
	sk_resize(SK_BACK_WARM, cols * rows, 0.0)
	local back_light, back_warm = SK_BACK_LIGHT, SK_BACK_WARM
	local zoom = SK_ZOOM / (SK_H * 0.5)
	local ee = SK_EX * SK_EX + SK_EY * SK_EY + SK_EZ * SK_EZ
	local exz = SK_EX * SK_EX + SK_EZ * SK_EZ - 21.16
	local vx = SK_BCX - SK_EX
	local vy = SK_BCY - SK_EY
	local vz = SK_BCZ - SK_EZ
	local vv = vx * vx + vy * vy + vz * vz
	local ring = SK_KAGE * 1.8
	local ring_gain = SK_KAGE < 0.7 and (0.7 - SK_KAGE) * 0.16 * SK_WAKE or 0.0
	local aura_gain = SK_WAKE * (0.75 + 0.25 * SK_KICK)
	for j = 0, rows - 1 do
		local v = (SK_H * 0.5 - j * 4 - 0.5) * zoom
		for i = 0, cols - 1 do
			local u = (i * 4 + 0.5 - SK_W * 0.5) * zoom
			local dx = SK_FX + SK_RX * u + SK_UX * v
			local dy = SK_FY + SK_RY * u + SK_UY * v
			local dz = SK_FZ + SK_RZ * u + SK_UZ * v
			local il = 1.0 / sqrt(dx * dx + dy * dy + dz * dz)
			dx = dx * il; dy = dy * il; dz = dz * il
			local ed = SK_EX * dx + SK_EY * dy + SK_EZ * dz
			local t = (sqrt(ed * ed - ee + 49.0) - ed) / 7.0
			local qx = SK_EX / 7.0 + dx * t
			local qy = SK_EY / 7.0 + dy * t
			local qz = SK_EZ / 7.0 + dz * t
			local phi = atan2(qx, -qz)
			local rib = abs(sin(phi * 4.0))
			rib = rib < 0.25 and 1.0 - rib * 4.0 or 0.0
			local light = 0.2 + 0.14 * qy + rib * (0.06 + 0.06 * qy)
			local oculus = (qy - 0.8) * 6.0
			light = light + (oculus < 0.0 and 0.0 or (oculus > 1.0 and 1.0 or oculus)) * 0.3
			light = light + 0.05 * sin(phi * 3.0 + qy * 4.0 + SK_T * 0.25) * sin(qy * 6.0 - SK_T * 0.17 + phi)
			local off = abs(sqrt(qx * qx + qy * qy) - ring)
			if off < 0.12 then light = light + (1.0 - off / 0.12) * ring_gain end
			local a = dx * dx + dz * dz
			local b = SK_EX * dx + SK_EZ * dz
			local disc = b * b - a * exz
			if disc > 0.0 and a > 0.000001 then
				local tp = (sqrt(disc) - b) / a
				local gap = abs(cos(atan2(SK_EX + dx * tp, -SK_EZ - dz * tp) * 4.0))
				if gap < 0.5 then light = light * (0.45 + gap * 1.1) end
			end
			light = light * dim + flash
			local tc = vx * dx + vy * dy + vz * dz
			local perp = vv - tc * tc
			local warm = 1.3 - sqrt(perp < 0.0 and 0.0 or perp)
			warm = warm < 0.0 and 0.0 or (warm > 1.0 and 1.0 or warm)
			back_light[j * cols + i + 1] = light < 0.0 and 0.0 or (light > 1.0 and 1.0 or light)
			back_warm[j * cols + i + 1] = warm * warm * aura_gain
		end
	end
end

-- one traced row of the vault, from the grid sk_backdrop fills
local function sk_backdrop_row(y, out)
	local back_light, back_warm, bayer_t = SK_BACK_LIGHT, SK_BACK_WARM, SK_BAYER
	local j = y // 4
	local fy = (y - j * 4) * 0.25
	local upper = j * SK_BACK_COLS + 1
	local lower = upper + SK_BACK_COLS
	local bayer = (y % 4) * 4 + 1
	local base = SK_BACK + SK_BACK_HUE + 255 * 1024
	local ww = SK_W
	local x = 0
	local i = 0
	while x < ww do
		local l0 = back_light[upper + i]
		local l1 = back_light[upper + i + 1]
		local w0 = back_warm[upper + i]
		local w1 = back_warm[upper + i + 1]
		l0 = l0 + (back_light[lower + i] - l0) * fy
		l1 = l1 + (back_light[lower + i + 1] - l1) * fy
		w0 = w0 + (back_warm[lower + i] - w0) * fy
		w1 = w1 + (back_warm[lower + i + 1] - w1) * fy
		for k = 0, 3 do
			if x >= ww then break end
			local dith = bayer_t[bayer + k] / 16.0
			local level = itrunc((l0 + (l1 - l0) * k * 0.25) * 15.0 + dith)
			local warm = itrunc((w0 + (w1 - w0) * k * 0.25) * 4.0 + dith)
			x = x + 1
			out[x] = base + (warm > 4 and 4 or warm) * 16 + (level > 15 and 15 or level)
		end
		i = i + 1
	end
end

-- the pixels [first, last) of a row whose rays (A + R u, A = F + U v) pass within r of c: widens the span to hold them
local function sk_row_span(ax, ay, az, cx, cy, cz, r, zoom, first, last)
	local vx = cx - SK_EX
	local vy = cy - SK_EY
	local vz = cz - SK_EZ
	local q = vx * vx + vy * vy + vz * vz - r * r
	local a = vx * ax + vy * ay + vz * az
	local b = vx * SK_RX + vy * SK_RY + vz * SK_RZ
	local k = q - b * b
	if k < 0.0001 then return 0, SK_W end
	local disc = a * a * b * b - k * (q * (ax * ax + ay * ay + az * az) - a * a)
	if disc <= 0.0 or a <= 0.0 then return first, last end
	disc = sqrt(disc)
	local lo = floor((a * b - disc) / (k * zoom) + SK_W * 0.5) - 1
	local hi = floor((a * b + disc) / (k * zoom) + SK_W * 0.5) + 2
	first = lo < first and lo or first
	last = hi > last and hi or last
	return first, last
end

local function sk_row(y)
	local out = {}
	for i = 1, SK_W do out[i] = 0 end
	sk_backdrop_row(y, out)
	local ww = SK_W
	local half_h = SK_H * 0.5
	local zoom = SK_ZOOM / half_h
	local v = (half_h - y - 0.5) * zoom
	local ax = SK_FX + SK_UX * v
	local ay = SK_FY + SK_UY * v
	local az = SK_FZ + SK_UZ * v
	local first = ww
	local last = 0
	first, last = sk_row_span(ax, ay, az, SK_BCX, SK_BCY, SK_BCZ, SK_BR, zoom, first, last)
	if SK_MOTH then first, last = sk_row_span(ax, ay, az, SK_MX, SK_MY, SK_MZ, SK_MB, zoom, first, last) end
	first = first < 0 and 0 or first
	last = last > ww and ww or last
	if first >= last then return out end
	-- the row's rays all lie in the plane through the eye spanned by R and F + U v: a capsule (0..SK_CROWN0) or a leg glow
	-- (100 + g) farther from that plane than its reach passes near none of them, so the pixels test only the rest
	local nx = SK_RY * az - SK_RZ * ay
	local ny = SK_RZ * ax - SK_RX * az
	local nz = SK_RX * ay - SK_RY * ax
	local nl = 1.0 / sqrt(nx * nx + ny * ny + nz * nz)
	local row = {}
	local nrow = 0
	for c = 1, SK_CROWN0 do
		local off = ((SK_CMX[c] - SK_EX) * nx + (SK_CMY[c] - SK_EY) * ny + (SK_CMZ[c] - SK_EZ) * nz) * nl
		if abs(off) < SK_CB[c] + 0.02 then
			nrow = nrow + 1
			row[nrow] = c - 1
		end
	end
	for g = 4, SK_NG do
		local off = ((SK_GX[g] - SK_EX) * nx + (SK_GY[g] - SK_EY) * ny + (SK_GZ[g] - SK_EZ) * nz) * nl
		if abs(off) < SK_GR[g] + 0.02 then
			nrow = nrow + 1
			row[nrow] = 99 + g
		end
	end
	local cand = {}
	for i = 1, 64 do cand[i] = 0 end
	local rx, ry, rz = SK_RX, SK_RY, SK_RZ
	for x = first, last - 1 do
		local u = (x + 0.5 - ww * 0.5) * zoom
		local dx = ax + rx * u
		local dy = ay + ry * u
		local dz = az + rz * u
		local il = 1.0 / sqrt(dx * dx + dy * dy + dz * dz)
		local traced = sk_trace(dx * il, dy * il, dz * il, x, y, cand, row, nrow)
		if traced >= 0 then out[x + 1] = traced end
	end
	return out
end

Ossuary = setmetatable({}, {__index = Scene})
Ossuary.__index = Ossuary

function Ossuary.new()
	local self = Scene.init(setmetatable({}, Ossuary))
	self.bx = 0.0; self.by = 0.0; self.bz = 0.0
	self.fx = 0.0; self.fy = 1.0; self.fz = 0.0
	self.ux = 0.0; self.uy = 0.0; self.uz = 1.0
	self.rx = 1.0; self.ry = 0.0; self.rz = 0.0
	self.ex = 0.0; self.ey = 0.0; self.ez = 0.0
	self.cux = 0.0; self.cuy = 1.0; self.cuz = 0.0
	self.lkx = 0.0; self.lky = 0.0; self.lkz = 0.0
	self.placed = false
	self.footx = {}
	self.footy = {}
	self.footz = {}
	self.fromx = {}
	self.fromy = {}
	self.fromz = {}
	self.tox = {}
	self.toy = {}
	self.toz = {}
	self.stept = {}
	self.legfo = {0.3, 0.1, -0.1, -0.3}
	self.legreach = {0.42, 0.14, -0.08, -0.36}
	self.leglift = {1.25, 0.8, 0.25, 0.0}
	self.ppx = {}
	self.ppy = {}
	self.ppz = {}
	self.pvx = {}
	self.pvy = {}
	self.pvz = {}
	self.plife = {}
	self.prate = {}
	self.depth = {}
	self.np = 120
	self.pnext = 0
	self.l1 = 0.5
	self.l2 = 0.52
	self.tt = 0.0
	self.spawn_acc = 0.0
	self.stance = 0.5

	-- the story on the cue's bars: asleep at the hub (0-1), awake on bar 2, the spokes thrown and the spiral run by bar 4,
	-- the moth strikes on bar 6, the cocoon closes on bar 8, the hunter stalks the viewer through bar 9 and lunges on bar 10
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
	self.spoke_ang = {}
	self.spa = {}
	self.spb = {}
	self.lay_turn = {}
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
	return self
end

function Ossuary:build()
	self.name = "OSSUARY"
	self.handle = "eight legs of inverse kinematics . an orb web . a moth that came too close"
	self.replay = 23.5
	local pal = self.pal
	-- the vault: three chord hues, each five steps of the creature's warm aura by sixteen levels of light
	SK_BACK = pal:size()
	local vault = {52.0, 60.0, 112.0, 72.0, 52.0, 110.0, 40.0, 72.0, 104.0}
	for hue = 0, 2 do
		for warm = 0, 4 do
			for level = 0, 15 do
				local f = level / 15.0
				pal:add(4.0 + vault[hue * 3 + 1] * f + warm * 16.0, 4.0 + vault[hue * 3 + 2] * f + warm * 2.0, 9.0 + vault[hue * 3 + 3] * f + warm * 4.0)
			end
		end
	end
	SK_BONE = pal:ramp(12, 26.0, 20.0, 16.0, 250.0, 238.0, 206.0)
	SK_CHIT = pal:size()
	local shell = {235.0, 40.0, 90.0, 150.0, 70.0, 255.0, 40.0, 230.0, 210.0}
	for band = 0, 2 do
		for k = 0, 11 do
			local f = k / 11.0
			local c = f * f * 0.85 + f * 0.15
			local wh = f > 0.8 and (f - 0.8) * 3.5 or 0.0
			local r = 5.0 + shell[band * 3 + 1] * c
			local g = 2.0 + shell[band * 3 + 2] * c
			local b = 7.0 + shell[band * 3 + 3] * c
			pal:add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh)
		end
	end
	-- the eyes and the crown: three steps of dead ember, four of fire, then white
	SK_EYE = pal:size()
	pal:add(34.0, 12.0, 10.0)
	pal:add(96.0, 16.0, 8.0)
	pal:add(160.0, 22.0, 8.0)
	pal:ramp(4, 220.0, 30.0, 10.0, 255.0, 250.0, 190.0)
	pal:add(255.0, 255.0, 255.0)
	SK_MARK = pal:ramp(6, 90.0, 0.0, 8.0, 255.0, 200.0, 60.0)
	SK_SPARK = pal:size()
	for k = 0, 7 do
		local f = k / 7.0
		pal:add(90.0 + 165.0 * f, 8.0 + 120.0 * f * f + 120.0 * f * f * f * f, 4.0 + 40.0 * f * f * f + 180.0 * f * f * f * f * f * f)
	end
	SK_GEM = pal:size()
	for k = 0, 7 do
		local f = k / 7.0
		local wh = f > 0.75 and (f - 0.75) * 3.2 or 0.0
		local r = 40.0 + 215.0 * f
		local g = 0.0 + 30.0 * f * f
		local b = 12.0 + 50.0 * f * f
		pal:add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh)
	end
	-- the moth's wing tones: gold, umber, the eyespot's black ring, cyan iris, pupil, violet margin, magenta iris
	SK_WING = pal:size()
	local tones = {255.0, 186.0, 64.0, 120.0, 58.0, 28.0, 34.0, 10.0, 34.0, 60.0, 235.0, 255.0, 255.0, 245.0, 255.0, 160.0, 70.0, 230.0, 255.0, 50.0, 200.0}
	for tone = 0, 6 do
		for l = 0, 7 do
			local f = (l + 1) / 8.0
			local k = 0.1 + 0.9 * f
			pal:add(tones[tone * 3 + 1] * k, tones[tone * 3 + 2] * k, tones[tone * 3 + 3] * k)
		end
	end
	SK_FUR = pal:ramp(8, 22.0, 12.0, 8.0, 236.0, 196.0, 150.0)
	SK_COCOON = pal:gradient(10, {{16.0, 12.0, 28.0}, {110.0, 100.0, 150.0}, {230.0, 226.0, 248.0}, {255.0, 255.0, 255.0}})
	-- silk: eight iridescent hues, six levels each
	SK_SILK = pal:size()
	for hq = 0, 7 do
		local c = gfx:rainbow_rgb(hq * 0.7854 + 0.3)
		local cr = c[1] * 0.12 + 196.0
		local cg = c[2] * 0.12 + 204.0
		local cb = c[3] * 0.12 + 222.0
		for l = 0, 5 do
			local f = 0.14 + 0.86 * (l + 1) / 6.0
			pal:add(cr * f, cg * f, cb * f)
		end
	end
	for i = 1, 8 do
		self.footx[i] = 0.0; self.footy[i] = 0.0; self.footz[i] = 0.0
		self.fromx[i] = 0.0; self.fromy[i] = 0.0; self.fromz[i] = 0.0
		self.tox[i] = 0.0; self.toy[i] = 0.0; self.toz[i] = 0.0
		self.stept[i] = -1.0
	end
	for i = 1, self.np do
		self.ppx[i] = 0.0; self.ppy[i] = 0.0; self.ppz[i] = 0.0
		self.pvx[i] = 0.0; self.pvy[i] = 0.0; self.pvz[i] = 0.0
		self.plife[i] = 0.0; self.prate[i] = 1.0
	end
	self:plan_web()
end

function Ossuary:length_bars() return 12 end

function Ossuary:hit_times() return {3.871, 7.742, 11.613, 15.484, 19.355} end
function Ossuary:hit_levels() return {1, 1, 2, 1, 2} end

-- 0 until the hunter turns from the cocoon to the viewer, then 1
function Ossuary:facing(t) return self:smooth((t - self.t_face) / 1.2) end
-- 0 before the stalk, rising through bar 9 to 1 at the lunge
function Ossuary:stalk(t) return self:smooth((t - self.t_stalk) / (self.t_lunge - self.t_stalk)) end
-- the lunge itself, 1 from just after bar 10's downbeat on
function Ossuary:lunge(t) return self:smooth((t - self.t_lunge + 0.06) / 0.22) end
-- the flare of the waking: 1 on bar 2's downbeat, dying away
function Ossuary:flare(t) return t < self.t_wake and 0.0 or exp((self.t_wake - t) * 2.5) end
-- how wide the jaw gapes: a yawn on waking, the lunge, and a snap on every kick of the close-up
function Ossuary:gape(t)
	local result = 0.6 * self:flare(t)
	local s = t - self.t_lunge
	if s > -0.1 then result = exp(0.0 - (s > 0.0 and s or 0.0) * 2.2) + 0.35 * SK_KICK end
	return result > 1.2 and 1.2 or result
end

-- the music the scene answers: the last kick's age and envelope, the chord's hue in the vault, the hits in the lens
function Ossuary:hear(t)
	local cue = self.cue
	if cue == nil then return end
	local k = cue.kick:last(t / MUSIC_STEP + 0.0001)
	SK_KAGE = k < 1 and 9.0 or t - cue.kick.at[k] * MUSIC_STEP
	SK_KICK = cue:kick_env(t)
	SK_BACK_HUE = cue:chord(t) % 3 * 80
	local punch = cue:hit(t)
	SK_PUNCH = punch * punch
	SK_ZOOM = 0.5 * (1.0 - SK_PUNCH * 0.12 - SK_KICK * 0.015 * SK_WAKE)
end

function Ossuary:smooth(u)
	local c = u < 0.0 and 0.0 or (u > 1.0 and 1.0 or u)
	return c * c * (3.0 - 2.0 * c)
end

function Ossuary:turn_toward(from, to, w)
	local d = to - from
	d = d - 6.2831853 * floor((d + 3.1415927) / 6.2831853)
	return from + d * w
end

-- where a foot wants to stand: on the web about the body, the front pairs raised off it at the viewer in the stalk and the leap
function Ossuary:rest_foot(i, lead)
	local side = i < 4 and 1.0 or -1.0
	local j = i % 4
	local reach = (self.legreach[j + 1] + lead) * self.stance
	local span = side * 0.78 * self.stance
	self.tox[i + 1] = self.bx + self.fx * reach + self.rx * span
	self.toy[i + 1] = self.by + self.fy * reach + self.ry * span
	self.toz[i + 1] = self.bz * self.leglift[j + 1] + self:stalk(self.tt) * (j == 0 and 0.3 or 0.0)
end

function Ossuary:jitter(n) return (self.rng:next(n * 2 + 1) - n) / (n * 1.0) end

-- the web stands in the plane z = 0 about the origin, facing +z: the spokes get their angles and the capture spiral its
-- polygon, one vertex where it crosses each spoke, winding in from the rim; lay_turn is how far round from lay_from,
-- in turns, the edge into each vertex begins
function Ossuary:plan_web()
	local nspoke = self.nspoke
	local spoke_ang, spa, spb, lay_turn = self.spoke_ang, self.spa, self.spb, self.lay_turn
	for k = 0, nspoke - 1 do
		local jit = k == nspoke // 2 and 0.0 or self:jitter(100) * 0.09
		spoke_ang[k + 1] = -1.5707963 + k * 6.2831853 / nspoke + jit
	end
	local n = 4 * nspoke + nspoke // 2 + 1
	local web_rim = self.web_rim
	for i = 0, n - 1 do
		local r = web_rim - (web_rim - 0.5) * i / (n - 1.0)
		local a = spoke_ang[i % nspoke + 1] + 6.2831853 * (i // nspoke)
		spa[i + 1] = r * cos(a)
		spb[i + 1] = r * sin(a)
		local turn = (spoke_ang[(i + nspoke - 1) % nspoke + 1] - self.lay_from) / 6.2831853
		lay_turn[i + 1] = turn - floor(turn)
	end
end

-- how far round the web the spiral is laid at time t, in turns: every winding grows at once behind the running
-- spider, and the lap closes, a spoke past full circle, on bar 4
function Ossuary:laid(t) return (t - self.t_spiral0) / (self.t_spiral1 - self.t_spiral0) * (1.0 + 1.0 / self.nspoke) end

-- the spiral's run at time t, into qa, qb: out from the hub, round with the spinnerets trailing over the laid edge, home
function Ossuary:lap_point(t)
	local r = self:smooth((t - self.t_out) / 0.5) * (1.0 - self:smooth((t - self.t_spiral1) / (self.t_home - self.t_spiral1)))
	local a = self.lay_from + self.lay_lead + 6.2831853 * self:laid(t)
	self.qa = r * cos(a)
	self.qb = r * sin(a)
end

function Ossuary:lap_heading(t)
	self:lap_point(t - 0.04)
	local a = self.qa
	local b = self.qb
	self:lap_point(t + 0.04)
	return atan2(self.qb - b, self.qa - a)
end

-- where it is on the web (pa, pb) and which way it faces (psi), at time t
function Ossuary:plane_path(t)
	if t < self.t_out then
		self.pa = 0.0; self.pb = 0.0
		self.psi = 1.5707963
		return
	end
	if t < self.t_home then
		self.psi = self:turn_toward(1.5707963, self:lap_heading(t), self:smooth((t - self.t_out) / 0.15))
		self:lap_point(t)
		self.pa = self.qa; self.pb = self.qb
		return
	end
	local moth_a, moth_b = self.moth_a, self.moth_b
	local ml = sqrt(moth_a * moth_a + moth_b * moth_b)
	local da = moth_a / ml; local db = moth_b / ml
	local toward = atan2(db, da)
	if t < self.t_rush then
		self.pa = 0.0; self.pb = 0.0
		self.psi = self:turn_toward(self:lap_heading(self.t_home - 0.05), 1.5707963, self:smooth((t - self.t_home) / 0.6))
		if t > self.t_impact then self.psi = self:turn_toward(self.psi, toward, self:smooth((t - self.t_impact) / (self.t_rush - self.t_impact))) end
		return
	end
	local reach = ml - 0.85
	if t < self.t_reach then
		reach = reach * self:smooth((t - self.t_rush) / (self.t_reach - self.t_rush))
		self.psi = toward
	elseif t < self.t_turn then
		local e = self:smooth((t - self.t_reach) / (self.t_turn - self.t_reach))
		reach = reach - 0.11 * e
		self.psi = toward + 3.1415927 * e
	else
		-- it works the cocoon, then turns head-down and prowls across the hub at the viewer below
		local busy = 1.0 - self:smooth((t - self.t_wrap1 + 0.3) / 0.3)
		local prowl = self:smooth((t - self.t_face - 0.3) / (self.t_lunge - self.t_face - 0.3))
		reach = reach - 0.11 + 0.05 * sin(t * 8.0) * busy
		self.psi = self:turn_toward(toward + 3.1415927 + 0.08 * sin(t * 5.0) * busy, -1.5707963, self:facing(t))
		self.pa = da * reach * (1.0 - prowl)
		self.pb = db * reach * (1.0 - prowl) - 0.35 * prowl
		return
	end
	self.pa = da * reach; self.pb = db * reach
end

function Ossuary:web_motion(t, dt)
	local opa = self.pa; local opb = self.pb
	self:plane_path(t)
	local psi = self.psi
	self.fx = cos(psi); self.fy = sin(psi); self.fz = 0.0
	self.rx = -self.fy; self.ry = self.fx; self.rz = 0.0
	self.bx = self.pa; self.by = self.pb; self.bz = 0.0
	local ma = self.pa - opa; local mb = self.pb - opb
	self.spd = dt > 0.0 and sqrt(ma * ma + mb * mb) / dt or 0.0
end

-- how bright the strike's ring makes the silk at (a, b): a front running out from the moth through the threads
function Ossuary:ring_light(a, b)
	local s = self.tt - self.t_impact
	if s < 0.0 or s > 1.6 then return 0.0 end
	local ma = a - self.moth_a; local mb = b - self.moth_b
	local behind = s * self.ring_speed - sqrt(ma * ma + mb * mb)
	if behind < 0.0 or behind > 0.7 then return 0.0 end
	return (1.0 - behind / 0.7) * 1.3 / (1.0 + s * 1.2)
end

-- how far a point of the web (a, b) stands off its plane: the spider's weight, a breeze, the moth's strike
-- ringing out through the threads and its struggle
function Ossuary:sag(a, b)
	local tt = self.tt
	local da = a - self.pa; local db = b - self.pb
	local result = -0.12 / (1.0 + (da * da + db * db) * 3.0)
	result = result + 0.035 * sin(a * 0.8 + tt * 1.1) * sin(b * 0.7 - tt * 0.9 + 0.5) * (a * a + b * b) * 0.12
	if tt > self.t_impact then
		local ma = a - self.moth_a; local mb = b - self.moth_b
		local dist = sqrt(ma * ma + mb * mb)
		local s = tt - self.t_impact
		local front = s * self.ring_speed - dist
		if front > 0.0 then result = result + 0.34 / (1.0 + s * s * 1.6) * sin(front * 4.5) / (1.0 + dist * 0.6) end
		if tt < self.t_wrap1 then
			local fight = 0.07 * (1.0 - self.coc) * (0.55 + 0.45 * sin(tt * 2.3))
			result = result + fight * sin(tt * 21.0 - dist * 4.0) / (1.0 + dist * 2.5)
		end
	end
	return result
end

-- a point of the web in the world, into wx, wy, wz
function Ossuary:web_point(a, b)
	self.wx = a
	self.wy = b
	self.wz = self:sag(a, b)
end

function Ossuary:bezier(from, via, to, u)
	return from * (1.0 - u) * (1.0 - u) + via * 2.0 * u * (1.0 - u) + to * u * u
end

-- the moth at time t: it flutters in along a curve from off the frame's left, strikes the web and sticks, beats its
-- wings against the silk, then folds them as the cocoon closes and spins
function Ossuary:moth_pose(t)
	local moth_a, moth_b = self.moth_a, self.moth_b
	local mfx, mfy, mfz
	if t < self.t_impact then
		local u = (t - self.t_moth) / (self.t_impact - self.t_moth)
		u = u < 0.0 and 0.0 or u
		local a = self:bezier(-2.7, -0.9, moth_a, u)
		local b = self:bezier(0.6, 1.15, moth_b, u)
		local n = self:bezier(1.4, 0.9, 0.06, u)
		local va = self:bezier(-2.7, -0.9, moth_a, u + 0.02) - a
		local vb = self:bezier(0.6, 1.15, moth_b, u + 0.02) - b
		local vn = self:bezier(1.4, 0.9, 0.06, u + 0.02) - n
		local flutter = 1.0 - u
		self.mox = a + flutter * 0.16 * sin(t * 6.3)
		self.moy = b + flutter * 0.18 * sin(t * 8.9 + 1.0)
		self.moz = n + flutter * 0.14 * sin(t * 5.2 + 2.0)
		local vl = sqrt(va * va + vb * vb + vn * vn) + 0.000001
		mfx = va / vl; mfy = vb / vl + 0.5; mfz = vn / vl
		self.flap = 0.95 * sin(t * 31.4)
		self.wsc = 1.0; self.coc = 0.0; self.spin = 0.0
	else
		self:web_point(moth_a, moth_b)
		self.mox = self.wx; self.moy = self.wy; self.moz = self.wz + 0.09
		mfx = 0.34; mfy = 0.94; mfz = 0.0
		local coc = self:smooth((t - self.t_wrap0) / (self.t_wrap1 - 0.15 - self.t_wrap0))
		self.coc = coc
		self.spin = t > self.t_wrap0 and ((t < self.t_wrap1 and t or self.t_wrap1) - self.t_wrap0) * 6.0 or 0.0
		local beat = sin(t * 15.0)
		self.flap = (1.0 - coc) * 0.55 * (beat > 0.0 and beat or 0.0) * (0.6 + 0.4 * sin(t * 2.3)) + coc * 1.3
		self.wsc = 1.0 - 0.7 * coc
	end
	local ml = 1.0 / sqrt(mfx * mfx + mfy * mfy + mfz * mfz)
	mfx = mfx * ml; mfy = mfy * ml; mfz = mfz * ml
	local mux = 0.0 - mfx * mfz; local muy = 0.0 - mfy * mfz; local muz = 1.0 - mfz * mfz
	local ul = 1.0 / sqrt(mux * mux + muy * muy + muz * muz + 0.0000001)
	mux = mux * ul; muy = muy * ul; muz = muz * ul
	local mrx = muy * mfz - muz * mfy
	local mry = muz * mfx - mux * mfz
	local mrz = mux * mfy - muy * mfx
	if t > self.t_impact then
		-- the struggle jerks it about its spine; the wrapping rolls it
		local roll = self.spin * 0.12 + 0.3 * (1.0 - self.coc) * sin(t * 9.0)
		local c = cos(roll); local s = sin(roll)
		local nux = mux * c + mrx * s; local nuy = muy * c + mry * s; local nuz = muz * c + mrz * s
		mrx = mrx * c - mux * s; mry = mry * c - muy * s; mrz = mrz * c - muz * s
		mux = nux; muy = nuy; muz = nuz
	end
	self.mfx = mfx; self.mfy = mfy; self.mfz = mfz
	self.mux = mux; self.muy = muy; self.muz = muz
	self.mrx = mrx; self.mry = mry; self.mrz = mrz
end

function Ossuary:spawn(x, y, z, vx, vy, vz, life)
	local i = self.pnext + 1
	self.pnext = (self.pnext + 1) % self.np
	self.ppx[i] = x; self.ppy[i] = y; self.ppz[i] = z
	self.pvx[i] = vx; self.pvy[i] = vy; self.pvz[i] = vz
	self.plife[i] = 1.0
	self.prate[i] = 1.0 / life
end

-- an ember off the ring of the crown, thrown outward at the given speed
function Ossuary:crown_ember(speed)
	local bx, by, bz, fx, fy, rx, ry = self.bx, self.by, self.bz, self.fx, self.fy, self.rx, self.ry
	local a = self.rng:next(628) * 0.01
	local ca = cos(a) * 0.2
	local sa = sin(a) * 0.2
	local cx = bx + fx * (0.01 + ca * 0.66) + rx * sa
	local cy = by + fy * (0.01 + ca * 0.66) + ry * sa
	self:spawn(cx, cy, bz + 0.6 + ca * 0.75, (fx * ca * 0.66 + rx * sa) * speed, 0.5 + (fy * ca * 0.66 + ry * sa) * speed, 0.3 + ca * 0.75 * speed, 0.45 + self.rng:next(40) * 0.01)
end

function Ossuary:reached(mark, dt) return self.tt >= mark and self.tt - dt < mark end

-- embers leave the crown once it burns, more on the kick and many when the jaw gapes; the waking and the lunge throw
-- a burst of them, the strike a puff of scales off the moth
function Ossuary:update_particles(dt)
	self.spawn_acc = self.spawn_acc + dt * 5.0 * SK_WAKE * (0.3 + SK_KICK + self:gape(self.tt) * 8.0)
	while self.spawn_acc >= 1.0 do
		self.spawn_acc = self.spawn_acc - 1.0
		self:crown_ember(1.0)
	end
	if self:reached(self.t_wake, dt) or self:reached(self.t_lunge, dt) then
		for k = 0, 25 do self:crown_ember(5.0) end
	end
	if self:reached(self.t_impact, dt) then
		local rng = self.rng
		for k = 0, 21 do
			local a = rng:next(628) * 0.01
			local speed = 0.5 + rng:next(100) * 0.012
			self:spawn(self.moth_a, self.moth_b, 0.12, cos(a) * speed, sin(a) * speed, 0.3 + self:jitter(100) * 0.3, 0.5 + rng:next(50) * 0.01)
		end
	end
	local drag = 1.0 - dt * 1.4
	local ppx, ppy, ppz, pvx, pvy, pvz, plife, prate = self.ppx, self.ppy, self.ppz, self.pvx, self.pvy, self.pvz, self.plife, self.prate
	for i = 1, self.np do
		if plife[i] > 0.0 then
			pvx[i] = pvx[i] * drag
			pvy[i] = (pvy[i] + 0.7 * dt) * drag
			pvz[i] = pvz[i] * drag
			ppx[i] = ppx[i] + pvx[i] * dt
			ppy[i] = ppy[i] + pvy[i] * dt
			ppz[i] = ppz[i] + pvz[i] * dt
			plife[i] = plife[i] - dt * prate[i]
		end
	end
end

function Ossuary:update(t, dt)
	self.tt = t
	SK_T = t
	SK_WAKE = self:smooth((t - self.t_wake + 0.05) / 0.35)
	self:hear(t)
	self.stance = 0.5 + 0.5 * self:smooth((t - self.t_wake) / 0.3)
	self:web_motion(t, dt)
	local spring = self:lunge(t)
	if spring > 0.0 and self.placed then
		local lx = self.ex - self.bx; local ly = self.ey - self.by; local lz = self.ez - self.bz
		local ll = 0.75 * spring / sqrt(lx * lx + ly * ly + lz * lz + 0.000001)
		self.bx = self.bx + lx * ll; self.by = self.by + ly * ll; self.bz = self.bz + lz * ll
	end
	if not self.placed then
		for i = 0, 7 do
			self:rest_foot(i, 0.0)
			self.footx[i + 1] = self.tox[i + 1]; self.footy[i + 1] = self.toy[i + 1]; self.footz[i + 1] = self.toz[i + 1]
		end
	end
	self:walk(dt)
	self:update_particles(dt)
	if t > self.t_moth then self:moth_pose(t) end
	self:aim_camera(t, dt)
	self.placed = true
end

-- the tetrapod gait: a foot steps when its rest spot has run off, one group at a time; at speed it scuttles, with
-- quicker steps that lead the body and both groups free
function Ossuary:walk(dt)
	local footx, footy, footz, fromx, fromy, fromz = self.footx, self.footy, self.footz, self.fromx, self.fromy, self.fromz
	local tox, toy, toz, stept = self.tox, self.toy, self.toz, self.stept
	local scuttle = self.spd > 1.5
	local step_time = scuttle and 0.08 or 0.13
	local lead = self.spd * 0.1
	lead = lead > 0.45 and 0.45 or lead
	local stepping_a = false
	local stepping_b = false
	for i = 0, 7 do
		if stept[i + 1] >= 0.0 then
			if (i + i // 4) % 2 == 0 then stepping_a = true else stepping_b = true end
		end
	end
	for i = 0, 7 do
		local i1 = i + 1
		local group_a = (i + i // 4) % 2 == 0
		if stept[i1] >= 0.0 then
			stept[i1] = stept[i1] + dt / step_time
			self:rest_foot(i, lead)
			if stept[i1] >= 1.0 then
				stept[i1] = -1.0
				footx[i1] = tox[i1]; footy[i1] = toy[i1]; footz[i1] = toz[i1]
			else
				local e = stept[i1]
				footx[i1] = fromx[i1] + (tox[i1] - fromx[i1]) * e
				footy[i1] = fromy[i1] + (toy[i1] - fromy[i1]) * e
				footz[i1] = fromz[i1] + (toz[i1] - fromz[i1]) * e + sin(e * 3.14159265) * 0.16
			end
		else
			self:rest_foot(i, 0.0)
			local ddx = footx[i1] - tox[i1]
			local ddy = footy[i1] - toy[i1]
			local ddz = footz[i1] - toz[i1]
			local other_moving = stepping_a
			if group_a then other_moving = stepping_b end
			if (scuttle or not other_moving) and ddx * ddx + ddy * ddy + ddz * ddz > 0.05 then
				stept[i1] = 0.0
				fromx[i1] = footx[i1]; fromy[i1] = footy[i1]; fromz[i1] = footz[i1]
				if group_a then stepping_a = true else stepping_b = true end
			end
		end
	end
end

-- the camera: close on the sleeper, back for the whole web while it is thrown and the moth flies in, in on the hunter
-- and its catch, then round in front of the skull, below the line of its gaze (the face is reared 0.85 radians off
-- the body, the camera stands 0.51 up); the lunge lifts the aim up the face, which keeps the crown in the frame
function Ossuary:aim_camera(t, dt)
	local fx, fy = self.fx, self.fy
	local placed = self.placed
	local hx = self.bx; local hy = self.by; local hz = self.bz + 0.2
	local back = self:smooth((t - self.t_wake - 0.3) / 0.9)
	local near = self:smooth((t - self.t_impact - 0.25) / (self.t_reach - self.t_impact - 0.25)) * (1.0 - self:facing(t))
	local creep = self:stalk(t)
	local onto = self:facing(t) * 0.6 + creep * 0.4
	local tgx = hx * (1.0 - 0.75 * back)
	local tgy = hy * (1.0 - 0.75 * back)
	local tgz = hz * (1.0 - back)
	if t > self.t_moth then
		tgx = tgx + ((hx + self.mox) * 0.5 - tgx) * near
		tgy = tgy + ((hy + self.moy) * 0.5 - tgy) * near
		tgz = tgz + ((hz + self.moz) * 0.5 - tgz) * near
	end
	tgx = tgx + (hx + fx * 0.34 - tgx) * onto
	tgy = tgy + (hy + fy * 0.34 - tgy) * onto
	tgz = tgz + (hz - tgz) * onto
	local rise = 0.12 * self:lunge(t)
	tgx = tgx - fx * 0.7513 * rise
	tgy = tgy - fy * 0.7513 * rise
	tgz = tgz + 0.66 * rise
	local dist = 2.3 - 0.3 * (t < self.t_wake and t / self.t_wake or 1.0)
	dist = dist + (3.5 - dist) * back
	dist = dist + (2.1 - dist) * near
	dist = dist + (2.75 - dist) * self:facing(t)
	dist = dist - creep * 0.53 - self:lunge(t) * 0.53
	local oa = 0.1 + 0.12 * sin(t * 0.19)
	local dx = sin(oa)
	local dy = 0.12 + 0.05 * sin(t * 0.31)
	local dz = cos(oa)
	local dn = sqrt(dx * dx + dy * dy + dz * dz)
	dx = dx + (fx * 0.8712 * dn - dx) * onto
	dy = dy + (fy * 0.8712 * dn - dy) * onto
	dz = dz + (0.4908 * dn - dz) * onto
	local dl = dist / sqrt(dx * dx + dy * dy + dz * dz)
	local kc = placed and dt * (2.8 + 9.0 * self:lunge(t) * (1.0 - self:smooth((t - self.t_lunge - 0.5) / 0.5))) or 1.0
	kc = kc > 1.0 and 1.0 or kc
	self.ex = self.ex + (tgx + dx * dl - self.ex) * kc; self.ey = self.ey + (tgy + dy * dl - self.ey) * kc; self.ez = self.ez + (tgz + dz * dl - self.ez) * kc
	local kl = placed and dt * 4.0 or 1.0
	kl = kl > 1.0 and 1.0 or kl
	self.lkx = self.lkx + (tgx - self.lkx) * kl; self.lky = self.lky + (tgy - self.lky) * kl; self.lkz = self.lkz + (tgz - self.lkz) * kl
	local ku = placed and dt * 2.2 or 1.0
	ku = ku > 1.0 and 1.0 or ku
	self.cux = self.cux + (0.0 - fx * 0.7513 * onto - self.cux) * ku
	self.cuy = self.cuy + (1.0 - onto - fy * 0.7513 * onto - self.cuy) * ku
	self.cuz = self.cuz + (0.66 * onto - self.cuz) * ku
end

function Ossuary:capsule(i, ax, ay, az, bx2, by2, bz2, r)
	local dx = bx2 - ax
	local dy = by2 - ay
	local dz = bz2 - az
	i = i + 1
	SK_CAX[i] = ax; SK_CAY[i] = ay; SK_CAZ[i] = az
	SK_CDX[i] = dx; SK_CDY[i] = dy; SK_CDZ[i] = dz
	SK_CID[i] = 1.0 / (dx * dx + dy * dy + dz * dz + 0.000001)
	SK_CR[i] = r
	SK_CMX[i] = ax + dx * 0.5; SK_CMY[i] = ay + dy * 0.5; SK_CMZ[i] = az + dz * 0.5
	SK_CB[i] = sqrt(dx * dx + dy * dy + dz * dz) * 0.5 + r
end

function Ossuary:glow(i, x, y, z, r)
	i = i + 1
	SK_GX[i] = x; SK_GY[i] = y; SK_GZ[i] = z; SK_GR[i] = r
end

-- a curved horn: base, a bend out and back, a tip up and back
function Ossuary:horn(i, side)
	local bx0 = SK_HX + SK_PUX * 0.16 + SK_PRX * side * 0.12 - SK_PFX * 0.04
	local by0 = SK_HY + SK_PUY * 0.16 + SK_PRY * side * 0.12 - SK_PFY * 0.04
	local bz0 = SK_HZ + SK_PUZ * 0.16 + SK_PRZ * side * 0.12 - SK_PFZ * 0.04
	local mx = bx0 + SK_PRX * side * 0.2 + SK_PUX * 0.12 - SK_PFX * 0.12
	local my = by0 + SK_PRY * side * 0.2 + SK_PUY * 0.12 - SK_PFY * 0.12
	local mz = bz0 + SK_PRZ * side * 0.2 + SK_PUZ * 0.12 - SK_PFZ * 0.12
	local tx = mx + SK_PRX * side * 0.04 + SK_PUX * 0.26 - SK_PFX * 0.2
	local ty = my + SK_PRY * side * 0.04 + SK_PUY * 0.26 - SK_PFY * 0.2
	local tz = mz + SK_PRZ * side * 0.04 + SK_PUZ * 0.26 - SK_PFZ * 0.2
	self:capsule(i, bx0, by0, bz0, mx, my, mz, 0.05)
	self:capsule(i + 1, mx, my, mz, tx, ty, tz, 0.028)
	return i + 2
end

-- a leg from hip to foot: femur and tibia by two-bone IK to an ankle held above the foot, then the tarsus
function Ossuary:leg(ci, i, hx, hy, hz, side)
	local ux, uy, uz, rx, ry, rz = self.ux, self.uy, self.uz, self.rx, self.ry, self.rz
	local l1, l2 = self.l1, self.l2
	local px = self.footx[i + 1]; local py = self.footy[i + 1]; local pz = self.footz[i + 1]
	local hdx = px - hx; local hdy = py - hy; local hdz = pz - hz
	local hu = hdx * ux + hdy * uy + hdz * uz
	hdx = hdx - ux * hu; hdy = hdy - uy * hu; hdz = hdz - uz * hu
	local hl = 1.0 / sqrt(hdx * hdx + hdy * hdy + hdz * hdz + 0.000001)
	hdx = hdx * hl; hdy = hdy * hl; hdz = hdz * hl
	local ax = px + ux * 0.13 - hdx * 0.06
	local ay = py + uy * 0.13 - hdy * 0.06
	local az = pz + uz * 0.13 - hdz * 0.06
	local dx = ax - hx
	local dy = ay - hy
	local dz = az - hz
	local d = sqrt(dx * dx + dy * dy + dz * dz)
	local dn = d < 0.0001 and 0.0001 or d
	dx = dx / dn; dy = dy / dn; dz = dz / dn
	local dmax = (l1 + l2) * 0.995
	local dc = d > dmax and dmax or d
	dc = dc < 0.08 and 0.08 or dc
	local a = (l1 * l1 - l2 * l2 + dc * dc) / (2.0 * dc)
	local hh = l1 * l1 - a * a
	hh = hh < 0.0 and 0.0 or sqrt(hh)
	local bux = ux + rx * side * 0.35
	local buy = uy + ry * side * 0.35
	local buz = uz + rz * side * 0.35
	local bd = bux * dx + buy * dy + buz * dz
	bux = bux - dx * bd; buy = buy - dy * bd; buz = buz - dz * bd
	local bl = 1.0 / sqrt(bux * bux + buy * buy + buz * buz + 0.0000001)
	bux = bux * bl; buy = buy * bl; buz = buz * bl
	local kx = hx + dx * a + bux * hh
	local ky = hy + dy * a + buy * hh
	local kz = hz + dz * a + buz * hh
	local tx = hx + dx * dc
	local ty = hy + dy * dc
	local tz = hz + dz * dc
	self:capsule(ci, hx, hy, hz, kx, ky, kz, 0.042)
	self:capsule(ci + 1, kx, ky, kz, tx, ty, tz, 0.03)
	self:capsule(ci + 2, tx, ty, tz, px, py, pz, 0.018)
	self:glow(3 + i, kx, ky, kz, 0.027 + 0.006 * sin(self.tt * 7.0 + i))
	return ci + 3
end

function Ossuary:render(grid)
	-- the rays trace on a grid coarser than the picture's and the trace scales up to it, coarser still where the
	-- creature fills the frame and every ray is dear; the silk and the embers draw over it at full size
	local w, h, tt = self.w, self.h, self.tt
	local fx, fy, fz, ux, uy, uz, rx, ry, rz = self.fx, self.fy, self.fz, self.ux, self.uy, self.uz, self.rx, self.ry, self.rz
	local glare = self:facing(tt) * 0.3 + self:stalk(tt) * 0.7
	local close = 1.0 - self:smooth((tt - self.t_wake - 0.3) / 0.9)
	local caught = self:smooth((tt - self.t_impact - 0.25) / 0.5)
	close = close > caught and close or caught
	local trace_k = sqrt((6400.0 - close * 1800.0) * (1.0 - SK_PUNCH * 0.25) / (w * h))
	trace_k = trace_k > 1.0 and 1.0 or trace_k
	local tw = itrunc(w * trace_k)
	local th = itrunc(h * trace_k)
	SK_W = tw
	SK_H = th
	SK_FLICK = 0.7 * sin(tt * 17.0) * sin(tt * 5.3) + 1.5 * self:flare(tt)
	if tt > self.t_wrap1 then SK_FLICK = SK_FLICK * 0.4 + 0.9 + glare * 1.4 end
	local bob = 0.2 + 0.012 * sin(tt * 1.6) * (1.0 - SK_WAKE) + 0.018 * sin(tt * 11.0) * SK_WAKE
	local cx = self.bx + ux * bob
	local cy = self.by + uy * bob
	local cz = self.bz + uz * bob
	-- the sleeper's skull hangs level with its body; awake it rears back to face the viewer
	local pitch = 0.3 + 0.55 * self:smooth((tt - self.t_wake) / 0.4)
	local cpt = cos(pitch); local spt = sin(pitch)
	local hfx = fx * cpt + ux * spt; local hfy = fy * cpt + uy * spt; local hfz = fz * cpt + uz * spt
	local hux = ux * cpt - fx * spt; local huy = uy * cpt - fy * spt; local huz = uz * cpt - fz * spt
	SK_PFX = hfx; SK_PFY = hfy; SK_PFZ = hfz
	SK_PUX = hux; SK_PUY = huy; SK_PUZ = huz
	SK_PRX = rx; SK_PRY = ry; SK_PRZ = rz
	SK_QFX = fx; SK_QFY = fy; SK_QFZ = fz
	SK_QUX = ux; SK_QUY = uy; SK_QUZ = uz
	SK_TX = cx; SK_TY = cy; SK_TZ = cz
	SK_AR = 0.27
	SK_AX = cx - fx * 0.45 + ux * 0.12; SK_AY = cy - fy * 0.45 + uy * 0.12; SK_AZ = cz - fz * 0.45 + uz * 0.12
	local nod = 0.03 * sin(tt * 2.1)
	SK_HX = cx + fx * 0.34 + ux * (0.09 + nod); SK_HY = cy + fy * 0.34 + uy * (0.09 + nod); SK_HZ = cz + fz * 0.34 + uz * (0.09 + nod)
	SK_NX = SK_HX + hfx * 0.23 - hux * 0.06; SK_NY = SK_HY + hfy * 0.23 - huy * 0.06; SK_NZ = SK_HZ + hfz * 0.23 - huz * 0.06
	local jaw = 0.012 * sin(tt * 6.0) + 0.1 * self:gape(tt)
	SK_JX = SK_HX + hfx * 0.1 - hux * (0.14 + jaw); SK_JY = SK_HY + hfy * 0.1 - huy * (0.14 + jaw); SK_JZ = SK_HZ + hfz * 0.1 - huz * (0.14 + jaw)
	SK_BCX = cx; SK_BCY = cy; SK_BCZ = cz
	SK_BR = 1.6
	local ncap = 5 + 2 + 24 + 4 + 2 + 7
	if #SK_CAX ~= ncap then
		SK_CAX = {}; SK_CAY = {}; SK_CAZ = {}; SK_CDX = {}; SK_CDY = {}; SK_CDZ = {}; SK_CID = {}; SK_CR = {}; SK_CMX = {}; SK_CMY = {}; SK_CMZ = {}; SK_CB = {}
		for i = 1, ncap do
			SK_CAX[i] = 0.0; SK_CAY[i] = 0.0; SK_CAZ[i] = 0.0; SK_CDX[i] = 0.0; SK_CDY[i] = 0.0; SK_CDZ[i] = 0.0; SK_CID[i] = 1.0; SK_CR[i] = 0.1; SK_CMX[i] = 0.0; SK_CMY[i] = 0.0; SK_CMZ[i] = 0.0; SK_CB[i] = 1.0
		end
	end
	local nglow = 3 + 8
	if #SK_GX ~= nglow then
		SK_GX = {}; SK_GY = {}; SK_GZ = {}; SK_GR = {}
		for i = 1, nglow do SK_GX[i] = 0.0; SK_GY[i] = 0.0; SK_GZ[i] = 0.0; SK_GR[i] = 0.02 end
	end
	SK_NC = ncap
	SK_CROWN0 = ncap - 7
	SK_NG = nglow
	SK_NBONE = 5
	local eye = 0.03 * (1.0 + glare * 0.6)
	SK_GLARE = glare
	self:glow(0, SK_HX + hfx * 0.175 + hux * 0.03 + rx * 0.088, SK_HY + hfy * 0.175 + huy * 0.03 + ry * 0.088, SK_HZ + hfz * 0.175 + huz * 0.03 + rz * 0.088, eye)
	self:glow(1, SK_HX + hfx * 0.175 + hux * 0.03 - rx * 0.088, SK_HY + hfy * 0.175 + huy * 0.03 - ry * 0.088, SK_HZ + hfz * 0.175 + huz * 0.03 - rz * 0.088, eye)
	local third = (0.024 + 0.006 * sin(tt * 3.0)) * (1.0 + glare * 0.5)
	self:glow(2, SK_HX + hfx * 0.17 + hux * 0.15, SK_HY + hfy * 0.17 + huy * 0.15, SK_HZ + hfz * 0.17 + huz * 0.15, third)
	SK_LX = SK_GX[3] * 0.4 + (SK_GX[1] + SK_GX[2]) * 0.3 + hfx * 0.1
	SK_LY = SK_GY[3] * 0.4 + (SK_GY[1] + SK_GY[2]) * 0.3 + hfy * 0.1
	SK_LZ = SK_GZ[3] * 0.4 + (SK_GZ[1] + SK_GZ[2]) * 0.3 + hfz * 0.1
	self:capsule(0, SK_JX - rx * 0.08, SK_JY - ry * 0.08, SK_JZ - rz * 0.08, SK_JX + rx * 0.08, SK_JY + ry * 0.08, SK_JZ + rz * 0.08, 0.05)
	local ci = self:horn(1, 1.0)
	ci = self:horn(ci, -1.0)
	local fang = 0.02 * sin(tt * 6.0 + 1.0) + 0.05 * self:gape(tt)
	for s = 0, 1 do
		local side = s == 0 and 1.0 or -1.0
		local ax = SK_JX + hfx * 0.05 + rx * side * 0.035
		local ay = SK_JY + hfy * 0.05 + ry * side * 0.035
		local az = SK_JZ + hfz * 0.05 + rz * side * 0.035
		self:capsule(ci, ax, ay, az, ax + hfx * 0.07 - hux * 0.12 - rx * side * (0.02 + fang), ay + hfy * 0.07 - huy * 0.12 - ry * side * (0.02 + fang), az + hfz * 0.07 - huz * 0.12 - rz * side * (0.02 + fang), 0.016)
		ci = ci + 1
	end
	local legfo = self.legfo
	for i = 0, 7 do
		local side = i < 4 and 1.0 or -1.0
		local fo = legfo[i % 4 + 1]
		local hx = cx + fx * fo * 0.45 + rx * side * 0.1 + ux * 0.03
		local hy = cy + fy * fo * 0.45 + ry * side * 0.1 + uy * 0.03
		local hz = cz + fz * fo * 0.45 + rz * side * 0.1 + uz * 0.03
		ci = self:leg(ci, i, hx, hy, hz, side)
	end
	-- pedipalps feel the air in front of the jaw
	for s = 0, 1 do
		local side = s == 0 and 1.0 or -1.0
		local twitch = 0.03 * sin(tt * 7.3 + s * 2.0)
		local px = SK_HX + hfx * 0.2 - hux * 0.1 + rx * side * 0.08
		local py = SK_HY + hfy * 0.2 - huy * 0.1 + ry * side * 0.08
		local pz = SK_HZ + hfz * 0.2 - huz * 0.1 + rz * side * 0.08
		local mx = px + hfx * 0.11 + hux * (0.05 + twitch) + rx * side * 0.05
		local my = py + hfy * 0.11 + huy * (0.05 + twitch) + ry * side * 0.05
		local mz = pz + hfz * 0.11 + huz * (0.05 + twitch) + rz * side * 0.05
		self:capsule(ci, px, py, pz, mx, my, mz, 0.02)
		self:capsule(ci + 1, mx, my, mz, mx + hfx * 0.08 - hux * 0.09, my + hfy * 0.08 - huy * 0.09, mz + hfz * 0.08 - huz * 0.09, 0.017)
		ci = ci + 2
	end
	-- the spinnerets at the abdomen's tip
	for s = 0, 1 do
		local side = s == 0 and 1.0 or -1.0
		local sx = SK_AX - fx * 0.32 + rx * side * 0.03 - ux * 0.02
		local sy = SK_AY - fy * 0.32 + ry * side * 0.03 - uy * 0.02
		local sz = SK_AZ - fz * 0.32 + rz * side * 0.03 - uz * 0.02
		self:capsule(ci, sx, sy, sz, sx - fx * 0.08 - ux * 0.03, sy - fy * 0.08 - uy * 0.03, sz - fz * 0.08 - uz * 0.03, 0.02)
		ci = ci + 1
	end
	local hover = 0.03 * sin(tt * 1.7)
	SK_HGX = SK_HX + hux * 0.3; SK_HGY = SK_HY + huy * 0.3; SK_HGZ = SK_HZ + huz * 0.3
	SK_BGX = (SK_AX + SK_TX) * 0.5; SK_BGY = (SK_AY + SK_TY) * 0.5; SK_BGZ = (SK_AZ + SK_TZ) * 0.5
	SK_CRX = SK_HX + hux * (0.44 + hover); SK_CRY = SK_HY + huy * (0.44 + hover); SK_CRZ = SK_HZ + huz * (0.44 + hover)
	for k = 0, 6 do
		local a = k * 0.8976 + tt * 0.6
		local ca = cos(a) * 0.2
		local sa = sin(a) * 0.2
		local flame = (0.02 + 0.08 * SK_WAKE) + (0.05 * sin(tt * 9.0 + k * 1.9) + 0.03 * sin(tt * 23.0 + k)) * SK_WAKE + 0.03 * SK_KICK * SK_WAKE + (0.07 + 0.05 * SK_KICK) * glare
		local bxk = SK_CRX + hfx * ca + rx * sa
		local byk = SK_CRY + hfy * ca + ry * sa
		local bzk = SK_CRZ + hfz * ca + rz * sa
		self:capsule(SK_CROWN0 + k, bxk, byk, bzk, bxk + hux * flame + hfx * ca * 0.25 + rx * sa * 0.25, byk + huy * flame + hfy * ca * 0.25 + ry * sa * 0.25, bzk + huz * flame + hfz * ca * 0.25 + rz * sa * 0.25, 0.018)
	end
	-- the bounding radii the distance skips on: the skull's jaw about its center, the ring and flames about the crown's
	local jhx = SK_JX - SK_HX; local jhy = SK_JY - SK_HY; local jhz = SK_JZ - SK_HZ
	local jaw_reach = sqrt(jhx * jhx + jhy * jhy + jhz * jhz) + 0.085 + 0.09
	SK_SKB = (jaw_reach > SK_HR and jaw_reach or SK_HR) + 0.001
	local crown_reach = 0.2 + 0.028
	for k = SK_CROWN0 + 1, SK_NC do
		local mx = SK_CMX[k] - SK_CRX; local my = SK_CMY[k] - SK_CRY; local mz = SK_CMZ[k] - SK_CRZ
		local r = sqrt(mx * mx + my * my + mz * mz) + SK_CB[k]
		crown_reach = r > crown_reach and r or crown_reach
	end
	SK_CRB = crown_reach + 0.001
	local spin_gem = tt * 2.2
	local gl2 = 0.3 + 0.02 * sin(tt * 2.5)
	SK_GMX = SK_CRX + hux * gl2; SK_GMY = SK_CRY + huy * gl2; SK_GMZ = SK_CRZ + huz * gl2
	SK_GAX = rx * cos(spin_gem) + hfx * sin(spin_gem); SK_GAY = ry * cos(spin_gem) + hfy * sin(spin_gem); SK_GAZ = rz * cos(spin_gem) + hfz * sin(spin_gem)
	SK_GBX = hfx * cos(spin_gem) - rx * sin(spin_gem); SK_GBY = hfy * cos(spin_gem) - ry * sin(spin_gem); SK_GBZ = hfz * cos(spin_gem) - rz * sin(spin_gem)
	self:pose_moth()
	local ex, ey, ez = self.ex, self.ey, self.ez
	SK_EX = ex; SK_EY = ey; SK_EZ = ez
	local lx = self.lkx - ex
	local ly = self.lky - ey
	local lz = self.lkz - ez
	local ll = 1.0 / sqrt(lx * lx + ly * ly + lz * lz)
	lx = lx * ll; ly = ly * ll; lz = lz * ll
	local cux, cuy, cuz = self.cux, self.cuy, self.cuz
	local d = cux * lx + cuy * ly + cuz * lz
	local upx = cux - lx * d
	local upy = cuy - ly * d
	local upz = cuz - lz * d
	local ul = 1.0 / sqrt(upx * upx + upy * upy + upz * upz + 0.0000001)
	upx = upx * ul; upy = upy * ul; upz = upz * ul
	SK_FX = lx; SK_FY = ly; SK_FZ = lz
	SK_UX = upx; SK_UY = upy; SK_UZ = upz
	SK_RX = ly * upz - lz * upy
	SK_RY = lz * upx - lx * upz
	SK_RZ = lx * upy - ly * upx
	-- the key light: above the camera, a little to its right, toward the scene
	local kx = upx * 0.75 - lx * 0.45 + SK_RX * 0.35
	local ky = upy * 0.75 - ly * 0.45 + SK_RY * 0.35
	local kz = upz * 0.75 - lz * 0.45 + SK_RZ * 0.35
	local kl = 1.0 / sqrt(kx * kx + ky * ky + kz * kz)
	SK_KX = kx * kl; SK_KY = ky * kl; SK_KZ = kz * kl
	if #SK_ROWS ~= th then
		SK_ROWS = {}
		for r = 0, th - 1 do SK_ROWS[r + 1] = r end
	end
	local strike = tt > self.t_impact and exp((self.t_impact - tt) * 5.0) or 0.0
	local leap = tt > self.t_lunge and exp((self.t_lunge - tt) * 4.0) or 0.0
	sk_backdrop((0.55 + 0.45 * SK_WAKE) * (1.0 - 0.5 * glare), 0.1 * self:flare(tt) + 0.14 * strike + 0.16 * leap)
	local rows = parallel_transform(SK_ROWS, sk_row)
	if #self.depth ~= w * h then
		self.depth = {}
		for q = 1, w * h do self.depth[q] = 0 end
	end
	local depth = self.depth
	local idx = 1
	for y = 0, h - 1 do
		local row = rows[y * th // h + 1]
		for x = 0, w - 1 do
			local v = row[x * tw // w + 1]
			grid[idx] = v % 1024
			depth[idx] = v // 1024
			idx = idx + 1
		end
	end
	self:draw_web(grid)
	self:draw_particles(grid)
	self:strike_shake(grid)
end

-- the strike and the lunge shake the frame for half a second
function Ossuary:strike_shake(grid)
	local w, h, tt = self.w, self.h, self.tt
	local s = tt - (tt >= self.t_lunge and self.t_lunge or self.t_impact)
	if s < 0.0 or s > 0.5 then return end
	local amp = (0.5 - s) * w * 0.05
	local row = {}
	for x = 1, w do row[x] = 0 end
	for y = 0, h - 1 do
		local off = itrunc(sin(y * 0.7 + tt * 61.0) * amp)
		if off ~= 0 then
			local base = y * w
			for x2 = 1, w do row[x2] = grid[base + x2] end
			for x3 = 0, w - 1 do
				local from = x3 - off
				from = from < 0 and 0 or (from >= w and w - 1 or from)
				grid[base + x3 + 1] = row[from + 1]
			end
		end
	end
end

function Ossuary:pose_moth()
	SK_MOTH = self.tt > self.t_moth
	if not SK_MOTH then return end
	self:moth_pose(self.tt)
	local mox, moy, moz, mfx, mfy, mfz = self.mox, self.moy, self.moz, self.mfx, self.mfy, self.mfz
	local mux, muy, muz, mrx, mry, mrz = self.mux, self.muy, self.muz, self.mrx, self.mry, self.mrz
	local wsc, coc = self.wsc, self.coc
	SK_MX = mox; SK_MY = moy; SK_MZ = moz
	SK_MFX = mfx; SK_MFY = mfy; SK_MFZ = mfz
	SK_MUX = mux; SK_MUY = muy; SK_MUZ = muz
	SK_MRX = mrx; SK_MRY = mry; SK_MRZ = mrz
	SK_WSC = wsc
	SK_COC = coc
	SK_SPIN = self.spin
	SK_MB = 0.68 * wsc + 0.12 + 0.28 * coc + 0.06
	SK_MB = SK_MB < 0.4 and 0.4 or SK_MB
	local c = cos(self.flap); local s = sin(self.flap)
	for k = 0, 3 do
		local k1 = k + 1
		local side = k % 2 == 0 and 1.0 or -1.0
		local fore = k < 2
		local off = fore and 0.045 or -0.07
		SK_WOX[k1] = mox + mfx * off + mrx * side * 0.05
		SK_WOY[k1] = moy + mfy * off + mry * side * 0.05
		SK_WOZ[k1] = moz + mfz * off + mrz * side * 0.05
		SK_WSX[k1] = mrx * side * c + mux * s; SK_WSY[k1] = mry * side * c + muy * s; SK_WSZ[k1] = mrz * side * c + muz * s
		SK_WNX[k1] = -mrx * side * s + mux * c; SK_WNY[k1] = -mry * side * s + muy * c; SK_WNZ[k1] = -mrz * side * s + muz * c
		SK_WCA[k1] = (fore and 0.33 or 0.23) * wsc
		SK_WSA[k1] = (fore and 0.33 or 0.23) * wsc
		SK_WCB[k1] = (fore and 0.0 or -0.11) * wsc
		SK_WSB[k1] = (fore and 0.16 or 0.135) * wsc
	end
end

-- a world point onto the screen, into qsx, qsy and its depth qzc; false behind the camera
function Ossuary:project(x, y, z)
	local ox = x - self.ex; local oy = y - self.ey; local oz = z - self.ez
	local zc = ox * SK_FX + oy * SK_FY + oz * SK_FZ
	if zc < 0.08 then return false end
	local half_h = self.h * 0.5
	local scale = half_h / (SK_ZOOM * zc)
	self.qsx = (ox * SK_RX + oy * SK_RY + oz * SK_RZ) * scale + self.w * 0.5
	self.qsy = half_h - (ox * SK_UX + oy * SK_UY + oz * SK_UZ) * scale
	self.qzc = zc
	return true
end

-- a silk thread from (x0, y0, z0) to (x1, y1, z1), depth-tested against the traced scene; its hue turns with its
-- angle on the screen and with time, the way silk breaks light into colors
function Ossuary:thread(grid, x0, y0, z0, x1, y1, z1, bright)
	if not self:project(x0, y0, z0) then return end
	local sx0 = self.qsx; local sy0 = self.qsy; local z0c = self.qzc
	if not self:project(x1, y1, z1) then return end
	local ddx = self.qsx - sx0; local ddy = self.qsy - sy0
	local adx = ddx < 0.0 and -ddx or ddx
	local ady = ddy < 0.0 and -ddy or ddy
	local steps = itrunc((adx > ady and adx or ady) + 1.0)
	if steps > 400 then return end
	local hue = atan2(ddy, ddx) * 0.4 + self.tt * 0.15 + (sx0 + sy0) * 0.006
	hue = hue - floor(hue / 8.0) * 8.0
	local hq = itrunc(hue)
	hq = hq > 7 and 7 or hq
	local ink = SK_SILK + hq * 6
	local ix = ddx / steps; local iy = ddy / steps; local iz = (self.qzc - z0c) / steps
	local w, h, depth, bayer = self.w, self.h, self.depth, SK_BAYER
	for s = 0, steps do
		-- math.floor is math::ifloor (an integer), the original's itrunc(math::floor(v))
		local px = floor(sx0 + ix * s)
		local py = floor(sy0 + iy * s)
		if px < 0 or py < 0 or px >= w or py >= h then goto continue end
		do
			local at = py * w + px + 1
			local zc = z0c + iz * s
			if depth[at] <= itrunc(zc * 18.0) then goto continue end
			local fog = 1.0 - zc / 11.0
			local l = itrunc(fog * bright * 5.5 + bayer[(py % 4) * 4 + px % 4 + 1] / 16.0 - 0.3)
			if l < 0 then goto continue end
			grid[at] = ink + (l > 5 and 5 or l)
		end
		::continue::
	end
end

function Ossuary:glint(grid, x, y, z)
	if not self:project(x, y, z) then return end
	local w, h = self.w, self.h
	local px = floor(self.qsx)
	local py = floor(self.qsy)
	if px < 0 or py < 0 or px >= w or py >= h then return end
	local at = py * w + px + 1
	if self.depth[at] <= itrunc(self.qzc * 18.0) then return end
	grid[at] = SK_EYE + 7
end

-- the silk, drawn over the trace: the dragline the sleeper hangs from and the spokes cast after it, a spark at each
-- growing tip; the hub's mesh; the capture spiral, every winding growing at once behind the running spider on a fan
-- of fresh thread from its spinnerets; the strands that wind the cocoon. The strike's ring lights the threads it
-- passes, and the whole web flashes with dew as the spiral closes and again as the cocoon is sealed
function Ossuary:draw_web(grid)
	local tt, nspoke = self.tt, self.nspoke
	local shimmer = tt >= self.t_spiral1 and exp((self.t_spiral1 - tt) * 4.0) or 0.0
	local sealed = tt - self.t_wrap1
	if sealed >= 0.0 then shimmer = exp(0.0 - sealed * 5.0) end
	local spoke_ang = self.spoke_ang
	for k = 0, nspoke - 1 do
		local order = k < nspoke // 2 and k or k - 1
		local grow = k == nspoke // 2 and 1.0 or (tt - self.t_spokes - order * self.t_spoke_gap) / 0.25
		if grow > 0.0 then
			grow = grow > 1.0 and 1.0 or grow
			local ca = cos(spoke_ang[k + 1]) * self.spoke_reach * grow / 10.0
			local sa = sin(spoke_ang[k + 1]) * self.spoke_reach * grow / 10.0
			self:web_point(0.0, 0.0)
			local px = self.wx; local py = self.wy; local pz = self.wz
			for s = 1, 10 do
				self:web_point(ca * s, sa * s)
				self:thread(grid, px, py, pz, self.wx, self.wy, self.wz, 0.78 + shimmer * 0.4 + self:ring_light(ca * s, sa * s))
				px = self.wx; py = self.wy; pz = self.wz
			end
			if grow < 1.0 then self:glint(grid, px, py, pz) end
		end
	end
	if tt > self.t_out then
		for ring = 1, 2 do
			local rr = ring * 0.16
			self:web_point(rr, 0.0)
			local px = self.wx; local py = self.wy; local pz = self.wz
			for s = 1, 12 do
				local a = s * 0.5235988
				self:web_point(rr * cos(a), rr * sin(a))
				self:thread(grid, px, py, pz, self.wx, self.wy, self.wz, 0.8 + shimmer * 0.4)
				px = self.wx; py = self.wy; pz = self.wz
			end
		end
	end
	local fx, fy, fz = self.fx, self.fy, self.fz
	local sx = SK_AX - fx * 0.4; local sy = SK_AY - fy * 0.4; local sz = SK_AZ - fz * 0.4
	if tt > self.t_spiral0 then
		local sweep = self:laid(tt)
		local spa, spb, lay_turn = self.spa, self.spb, self.lay_turn
		local last = #spa - 1
		for i = 1, last do
			local u = (sweep - lay_turn[i + 1]) * nspoke
			if u > 0.0 then
				self:web_point(spa[i], spb[i])
				local px = self.wx; local py = self.wy; local pz = self.wz
				if u < 1.0 then
					self:web_point(spa[i] + (spa[i + 1] - spa[i]) * u, spb[i] + (spb[i + 1] - spb[i]) * u)
					self:thread(grid, px, py, pz, self.wx, self.wy, self.wz, 1.0)
					self:thread(grid, self.wx, self.wy, self.wz, sx, sy, sz, 1.2)
					self:glint(grid, self.wx, self.wy, self.wz)
				else
					self:web_point(spa[i + 1], spb[i + 1])
					self:thread(grid, px, py, pz, self.wx, self.wy, self.wz, 0.88 + shimmer * 0.4 + self:ring_light(spa[i + 1], spb[i + 1]))
					if sin(tt * 1.3 + i * 1.7) > 0.93 - shimmer * 0.9 then self:glint(grid, self.wx, self.wy, self.wz) end
				end
			end
		end
	end
	if tt > self.t_wrap0 and tt < self.t_wrap1 then
		local spin, mox, moy, moz, mfx, mfy, mfz = self.spin, self.mox, self.moy, self.moz, self.mfx, self.mfy, self.mfz
		local mrx, mry, mrz, mux, muy, muz = self.mrx, self.mry, self.mrz, self.mux, self.muy, self.muz
		local rad = 0.07 + 0.14 * self.coc
		for j = 0, 3 do
			local a = spin * 1.6 + j * 1.5708
			local along = -0.2 + j * 0.1 + 0.07 * sin(spin + j)
			local tx = mox + mfx * along + (mrx * cos(a) + mux * sin(a)) * rad
			local ty = moy + mfy * along + (mry * cos(a) + muy * sin(a)) * rad
			local tz = moz + mfz * along + (mrz * cos(a) + muz * sin(a)) * rad
			self:thread(grid, sx, sy, sz, tx, ty, tz, 1.25)
		end
	end
	if sealed >= 0.0 and sealed < 0.4 then
		for k = 0, 11 do
			local a = k * 0.5235988 + 0.2
			local r = 0.24 + sealed * 1.3
			self:glint(grid, self.mox + cos(a) * r, self.moy + sin(a) * r, self.moz + 0.05)
		end
	end
end

function Ossuary:plot(grid, x, y, dq, level)
	local w = self.w
	if x < 0 or y < 0 or x >= w or y >= self.h then return end
	local at = y * w + x + 1
	if self.depth[at] <= dq then return end
	local cur = grid[at]
	if cur >= SK_SPARK and cur < SK_SPARK + 8 and cur - SK_SPARK >= level then return end
	grid[at] = SK_SPARK + level
end

function Ossuary:splat(grid, x, y, z, level, size)
	if not self:project(x, y, z) then return end
	local px = floor(self.qsx)
	local py = floor(self.qsy)
	local dq = itrunc(self.qzc * 18.0)
	self:plot(grid, px, py, dq, level)
	if size > 0 then
		local halo = level > 2 and level - 3 or 0
		self:plot(grid, px + 1, py, dq, halo)
		self:plot(grid, px - 1, py, dq, halo)
		self:plot(grid, px, py + 1, dq, halo)
		self:plot(grid, px, py - 1, dq, halo)
	end
	if size > 1 then
		self:plot(grid, px + 1, py + 1, dq, level > 1 and level - 1 or 0)
		self:plot(grid, px + 1, py, dq, level)
		self:plot(grid, px, py + 1, dq, level)
	end
end

function Ossuary:draw_particles(grid)
	local plife, ppx, ppy, ppz = self.plife, self.ppx, self.ppy, self.ppz
	for i = 1, self.np do
		local life = plife[i]
		if life > 0.0 then
			local level = itrunc(life * 7.99)
			self:splat(grid, ppx[i], ppy[i], ppz[i], level, life > 0.55 and 1 or 0)
		end
	end
	if SK_WAKE < 0.5 then return end
	local tt, ux, uy, uz = self.tt, self.ux, self.uy, self.uz
	for s = 0, 5 do
		local ph = tt * 4.0 + s * 1.047
		local tw = sin(tt * 11.0 + s * 2.7)
		if tw >= 0.2 then
			local rr = 0.1 + 0.05 * sin(tt * 3.0 + s)
			local ca = cos(ph) * rr
			local sa = sin(ph) * rr
			local lift = 0.06 * sin(tt * 5.0 + s * 1.3)
			self:splat(grid, SK_GMX + SK_GAX * ca + SK_GBX * sa + ux * lift, SK_GMY + SK_GAY * ca + SK_GBY * sa + uy * lift, SK_GMZ + SK_GAZ * ca + SK_GBZ * sa + uz * lift, tw > 0.7 and 7 or 5, 1)
		end
	end
end

SCENES[9] = Ossuary
