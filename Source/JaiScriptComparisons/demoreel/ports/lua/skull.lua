-- OSSUARY: a horned, three-eyed skull on a spider's body crawls a landscape that
-- will not hold still: one raymarched field morphing between gyroid, Schwarz P
-- and Schwarz D under a flowing domain warp, eight two-bone IK legs, tendrils, a
-- flame crown under a spinning ruby, depth-tested embers. Rows serial.

local sin, cos, sqrt, floor = math.sin, math.cos, math.sqrt, math.floor

local SK_T, SK_S, SK_K, SK_TW, SK_TH = 0.0, 0.8, 0.0, 0.0, 0.16
local SK_MG, SK_MP, SK_MD, SK_WA = 1.0, 0.0, 0.0, 0.0
local SK_SHELL = 1.68
local SK_CX, SK_CY, SK_CZ = 0.0, 0.0, 0.0
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
local SK_LX, SK_LY, SK_LZ = 0.0, 0.0, 0.0
local SK_FLICK = 0.0
local SK_CAX, SK_CAY, SK_CAZ, SK_CDX, SK_CDY, SK_CDZ = {}, {}, {}, {}, {}, {}
local SK_CID, SK_CR, SK_CMX, SK_CMY, SK_CMZ, SK_CB = {}, {}, {}, {}, {}, {}
local SK_HGX, SK_HGY, SK_HGZ = 0.0, 0.0, 0.0
local SK_BGX, SK_BGY, SK_BGZ = 0.0, 0.0, 0.0
local SK_NC, SK_NBONE, SK_CROWN0 = 0, 5, 0
local SK_CRX, SK_CRY, SK_CRZ = 0.0, 0.0, 0.0
local SK_SKB, SK_CRB = 0.4, 0.4
local SK_GMX, SK_GMY, SK_GMZ = 0.0, 0.0, 0.0
local SK_GAX, SK_GAY, SK_GAZ = 1.0, 0.0, 0.0
local SK_GBX, SK_GBY, SK_GBZ = 0.0, 0.0, 1.0
local SK_GX, SK_GY, SK_GZ, SK_GR = {}, {}, {}, {}
local SK_NG, SK_NSOCK = 0, 3
local SK_EYE_INSIDE = false
local SK_W, SK_H = 0, 0
local SK_WORLD, SK_EMBER, SK_BONE, SK_CHIT, SK_EYE, SK_MARK, SK_FOG, SK_SPARK, SK_GEM = 0, 0, 0, 0, 0, 0, 0, 0, 0
local SK_BAYER = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5}

-- the landscape's level function: three surfaces blended under a flowing warp, twisted about the creature's axis
local function sk_field(x, y, z)
	local a = (z - SK_CZ) * SK_TW
	local ca = cos(a)
	local sa = sin(a)
	local px = x - SK_CX
	local py = y - SK_CY
	local s = SK_S
	local qx = (px * ca - py * sa + SK_CX) * s
	local qy = (px * sa + py * ca + SK_CY) * s
	local qz = z * s
	local wa = SK_WA
	local wx = qx + wa * sin(qy * 1.3 + SK_T * 0.7)
	local wy = qy + wa * sin(qz * 1.3 + SK_T * 0.6)
	local wz = qz + wa * sin(qx * 1.3 + SK_T * 0.8)
	local sx = sin(wx); local cx = cos(wx)
	local sy = sin(wy); local cy = cos(wy)
	local sz = sin(wz); local cz = cos(wz)
	local g = sx * cy + sy * cz + sz * cx
	local p = cx + cy + cz
	local d = sx * sy * sz + sx * cy * cz + cx * sy * cz + cx * cy * sz
	return g * SK_MG + p * SK_MP + d * SK_MD * 1.4 - SK_K
end

local function sk_world(x, y, z)
	local g = sk_field(x, y, z)
	return ((g < 0.0 and -g or g) - SK_TH) / SK_SHELL
end

-- capsules [first, last) against best: a capsule whose bounding sphere cannot beat best is skipped without a root
local function sk_capsules(x, y, z, first, last, best)
	for i = first + 1, last do
		local mx = x - SK_CMX[i]
		local my = y - SK_CMY[i]
		local mz = z - SK_CMZ[i]
		local reach = best + SK_CB[i]
		if mx * mx + my * my + mz * mz <= reach * reach then
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

-- the skull against best: no nearer than sqrt(0.92) of the distance to its center less SK_SKB (the jaw's reach),
-- so it is skipped when that cannot beat best
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
	local af = ax * SK_PFX + ay * SK_PFY + az * SK_PFZ
	local au = ax * SK_PUX + ay * SK_PUY + az * SK_PUZ
	local ar = ax * SK_PRX + ay * SK_PRY + az * SK_PRZ
	af = af * 0.74
	au = au * 1.12
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
	local a = ox * SK_GAX + oy * SK_GAY + oz * SK_GAZ
	local b = ox * SK_PUX + oy * SK_PUY + oz * SK_PUZ
	local c = ox * SK_GBX + oy * SK_GBY + oz * SK_GBZ
	a = a < 0.0 and -a or a
	b = b < 0.0 and -b or b
	c = c < 0.0 and -c or c
	return (a + b * 0.62 + c - 0.075) * 0.54
end

-- the creature along one ray: only the parts the ray passes near (groups: 1 body, 2 head;
-- cand: capsule indices, glows as 100 + index). A part the ray never enters changes the
-- step lengths, never a hit.
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
			local ci = c + 1
			local mx = x - SK_CMX[ci]
			local my = y - SK_CMY[ci]
			local mz = z - SK_CMZ[ci]
			local reach = d + SK_CB[ci]
			if mx * mx + my * my + mz * mz <= reach * reach then
				local pax = x - SK_CAX[ci]
				local pay = y - SK_CAY[ci]
				local paz = z - SK_CAZ[ci]
				local cdx = SK_CDX[ci]
				local cdy = SK_CDY[ci]
				local cdz = SK_CDZ[ci]
				local hh = (pax * cdx + pay * cdy + paz * cdz) * SK_CID[ci]
				hh = hh < 0.0 and 0.0 or (hh > 1.0 and 1.0 or hh)
				local ex = pax - cdx * hh
				local ey = pay - cdy * hh
				local ez = paz - cdz * hh
				local dc = sqrt(ex * ex + ey * ey + ez * ez) - SK_CR[ci]
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

-- the ray's candidate capsules cand[first, last) (counted from 0) against best
local function sk_candidates(x, y, z, cand, first, last, d)
	for k = first + 1, last do
		local ci = cand[k] + 1
		local mx = x - SK_CMX[ci]
		local my = y - SK_CMY[ci]
		local mz = z - SK_CMZ[ci]
		local reach = d + SK_CB[ci]
		if mx * mx + my * my + mz * mz <= reach * reach then
			local pax = x - SK_CAX[ci]
			local pay = y - SK_CAY[ci]
			local paz = z - SK_CAZ[ci]
			local cdx = SK_CDX[ci]
			local cdy = SK_CDY[ci]
			local cdz = SK_CDZ[ci]
			local hh = (pax * cdx + pay * cdy + paz * cdz) * SK_CID[ci]
			hh = hh < 0.0 and 0.0 or (hh > 1.0 and 1.0 or hh)
			local ex = pax - cdx * hh
			local ey = pay - cdy * hh
			local ez = paz - cdz * hh
			local dc = sqrt(ex * ex + ey * ey + ez * ez) - SK_CR[ci]
			d = d < dc and d or dc
		end
	end
	return d
end

-- 0 world, 1 bone, 2 chitin, 3 glow, 4 marking, 5 ruby at a point on the ray the wall is dw from: the nearest wins, ties
-- to the earlier. Each capsule run starts from the best so far, so a capsule that cannot win costs only its bounding
-- test; and a capsule the ray passed no nearer than 0.01 to (not among its candidates: cand[0, nbone) bone, then chitin
-- up to ncap) is that far from every point on it, so where the wall is nearer only the candidates can win
local function sk_material(x, y, z, dw, cand, nbone, ncap)
	local best = dw
	local m = 0
	local candidates_only = dw < 0.009
	local bone = sk_skull(x, y, z, best)
	if candidates_only then bone = sk_candidates(x, y, z, cand, 0, nbone, bone) else bone = sk_capsules(x, y, z, 0, SK_NBONE, bone) end
	if bone < best then best = bone; m = 1 end
	local db = sk_body(x, y, z)
	local chit = db < best and db or best
	if candidates_only then chit = sk_candidates(x, y, z, cand, nbone, ncap, chit) else chit = sk_capsules(x, y, z, SK_NBONE, SK_CROWN0, chit) end
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
	if m == 2 and chit == db then
		local ax = x - SK_AX; local ay = y - SK_AY; local az = z - SK_AZ
		local au = ax * SK_PUX + ay * SK_PUY + az * SK_PUZ
		local ar = ax * SK_PRX + ay * SK_PRY + az * SK_PRZ
		local af = ax * SK_PFX + ay * SK_PFY + az * SK_PFZ
		local ar2 = ar < 0.0 and -ar or ar
		local af2 = af < 0.0 and -af or af
		if au > 0.09 and ar2 < 0.03 + af2 * 0.2 and af > -0.28 and af < 0.18 then m = 4 end
	end
	return m
end

-- a pixel's palette index plus its depth (0..255) above bit 10, for the particle pass
local function sk_trace(dx, dy, dz, px, py, cand, row, nrow)
	local ox = SK_EX
	local oy = SK_EY
	local oz = SK_EZ
	local groups = 0
	local nc = 0
	local nbone = 0
	local ncap = 0
	if sk_ray_near(ox, oy, oz, dx, dy, dz, SK_BGX, SK_BGY, SK_BGZ, 0.62) then groups = 1 end
	if sk_ray_near(ox, oy, oz, dx, dy, dz, SK_HGX, SK_HGY, SK_HGZ, 0.74) then groups = groups + 2 end
	for k = 1, nrow do
		local c = row[k]
		if c < 100 then
			local ci = c + 1
			if sk_ray_near(ox, oy, oz, dx, dy, dz, SK_CMX[ci], SK_CMY[ci], SK_CMZ[ci], SK_CB[ci] + 0.01) then
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
	local near = groups ~= 0 or nc ~= 0
	local t = 0.04
	local glow = 9.0
	if not near then
		local vx = SK_BCX - ox
		local vy = SK_BCY - oy
		local vz = SK_BCZ - oz
		local tc = vx * dx + vy * dy + vz * dz
		local perp = vx * vx + vy * vy + vz * vz - tc * tc
		glow = tc < 0.0 and 9.0 or sqrt(perp < 0.0 and 0.0 or perp) - 0.55
	end
	local hit = false
	local inside = SK_EYE_INSIDE
	-- the creature's distance is Lipschitz: 1.18 where the skull's long axis is among the ray's parts, else 1
	local lip = (groups & 2) ~= 0 and 1.2 or 1.0
	local ds_last = 0.0
	local t_last = 0.0
	local wall = 0.0
	local i = 0
	while i < 64 do
		local x = ox + dx * t
		local y = oy + dy * t
		local z = oz + dz * t
		local dw = sk_world(x, y, z)
		local cut = false
		if inside then
			-- the camera sits inside a wall: the first slab along the ray is cut away
			if dw < 0.0 then
				t = t + (0.01 - dw)
				i = i + 1
				cut = true
			else
				inside = false
			end
		end
		if not cut then
			local d = dw
			if near then
				-- skipped while its last distance, less what the ray has closed since, stays above the wall and above
				-- the glow so far or the aura's reach (0.5)
				local bound = ds_last - (t - t_last) * lip - 0.001
				if bound < dw or (bound < glow and bound < 0.5) then
					local ds = sk_spider(x, y, z, groups, cand, nc)
					glow = ds < glow and ds or glow
					d = dw < ds and dw or ds
					ds_last = ds
					t_last = t
				end
			end
			if d < 0.001 + t * 0.0015 then
				hit = true
				wall = dw
				break
			end
			t = t + d * 0.85
			if t > 14.0 then break end
			i = i + 1
		end
	end
	local dith = SK_BAYER[(py % 4) * 4 + px % 4 + 1] / 16.0 - 0.47
	if not hit then
		local aura = 1.0 - glow * 2.0
		aura = aura < 0.0 and 0.0 or aura
		local f = itrunc(aura * aura * 9.0 + dith + 0.5 + i * 0.015)
		return SK_FOG + (f < 0 and 0 or (f > 9 and 9 or f)) + 255 * 1024
	end
	local depth = itrunc(t * 18.0)
	depth = (depth > 254 and 254 or depth) * 1024
	local x = ox + dx * t
	local y = oy + dy * t
	local z = oz + dz * t
	local m = sk_material(x, y, z, wall, cand, nbone, ncap)
	local fog = 1.0 - t / 14.0
	fog = fog < 0.0 and 0.0 or fog
	local lx = SK_LX - x
	local ly = SK_LY - y
	local lz = SK_LZ - z
	local ld2 = lx * lx + ly * ly + lz * lz
	local lil = 1.0 / sqrt(ld2 + 0.0001)
	lx = lx * lil; ly = ly * lil; lz = lz * lil
	if m == 0 then
		local hs = 0.004
		local n0 = sk_field(x + hs, y - hs, z - hs)
		local n1 = sk_field(x - hs, y - hs, z + hs)
		local n2 = sk_field(x - hs, y + hs, z - hs)
		local n3 = sk_field(x + hs, y + hs, z + hs)
		local g = (n0 + n1 + n2 + n3) * 0.25
		local nx = n0 - n1 - n2 + n3
		local ny = -n0 - n1 + n2 + n3
		local nz = -n0 + n1 - n2 + n3
		local sg = g < 0.0 and -1.0 or 1.0
		local nl = sg / sqrt(nx * nx + ny * ny + nz * nz + 0.0000001)
		nx = nx * nl; ny = ny * nl; nz = nz * nl
		local head = -(nx * dx + ny * dy + nz * dz)
		head = head < 0.0 and 0.0 or head
		local ember = nx * lx + ny * ly + nz * lz
		ember = ember < 0.0 and 0.0 or ember * 1.6 / (1.0 + ld2 * 3.0)
		local s = SK_S * 3.1
		local vx = x * s
		local vy = y * s
		local vz = z * s
		local vein = sin(vx) * cos(vy) + sin(vy) * cos(vz) + sin(vz) * cos(vx)
		local va = vein < 0.0 and -vein or vein
		local pulse = 0.5 + 0.5 * sin(vein * 5.0 - SK_T * 3.4 + vz * 0.25)
		local ao = 1.0 - i * 0.011
		ao = ao < 0.3 and 0.3 or ao
		local lit = (0.06 + head * 0.7) * fog * ao
		if va < 0.3 then
			local edge = 1.0 - va / 0.3
			local glowv = pulse * pulse * edge * 1.15 * (0.35 + fog * 0.65)
			lit = glowv > lit and glowv or lit * 0.4
		end
		if ember * fog > lit * 1.15 + (va < 0.3 and 0.25 or 0.0) then
			local e = itrunc((ember * fog * 0.85 + lit * 0.3) * 11.0 + dith + 0.5)
			return SK_EMBER + (e < 0 and 0 or (e > 11 and 11 or e)) + depth
		end
		local hue = (vx * 0.11 + vy * 0.07 - vz * 0.05) * 0.3 + g * 0.35 + SK_T * 0.05 + t * 0.035 + (va < 0.3 and 0.33 or 0.0)
		hue = hue - floor(hue)
		local hq = itrunc(hue * 12.0)
		hq = hq > 11 and 11 or hq
		local l = itrunc(lit * 11.0 + dith + 0.5)
		l = l < 0 and 0 or (l > 11 and 11 or l)
		return SK_WORLD + hq * 12 + l + depth
	end
	if m == 3 then
		local e = itrunc(2.4 + SK_FLICK + dith)
		return SK_EYE + (e < 0 and 0 or (e > 3 and 3 or e)) + depth
	end
	if m == 5 then
		local gox = x - SK_GMX; local goy = y - SK_GMY; local goz = z - SK_GMZ
		local a = gox * SK_GAX + goy * SK_GAY + goz * SK_GAZ
		local b = gox * SK_PUX + goy * SK_PUY + goz * SK_PUZ
		local c = gox * SK_GBX + goy * SK_GBY + goz * SK_GBZ
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
	if m == 1 then
		local lit = 0.1 + vd * 0.55 + up * 0.22 + rim * 0.25
		local so = 99.0
		for k = 1, SK_NSOCK do
			local ox2 = x - SK_GX[k]; local oy2 = y - SK_GY[k]; local oz2 = z - SK_GZ[k]
			local q = sqrt(ox2 * ox2 + oy2 * oy2 + oz2 * oz2) - SK_GR[k] * 2.5
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
	if m == 4 then
		local k = itrunc((0.45 + vd * 0.45 + 0.3 * sin(SK_T * 4.0)) * 5.0 + dith + 0.5)
		return SK_MARK + (k < 0 and 0 or (k > 5 and 5 or k)) + depth
	end
	local lit = 0.03 + vd * 0.16 + rim * 0.62 + eyel * 0.9
	local c = itrunc(lit * fog * 11.0 + dith + 0.5)
	return SK_CHIT + (c < 0 and 0 or (c > 11 and 11 or c)) + depth
end

local function sk_row(y, out, cand)
	local ww = SK_W
	local half_h = SK_H * 0.5
	local zoom = 0.6 / half_h
	local v = (half_h - y - 0.5) * zoom
	local fx, fy, fz, rx, ry, rz, ux, uy, uz = SK_FX, SK_FY, SK_FZ, SK_RX, SK_RY, SK_RZ, SK_UX, SK_UY, SK_UZ
	-- the row's rays all lie in the plane through the eye spanned by R and F + U v: a capsule or a leg glow farther from
	-- that plane than its reach passes near none of them, so the pixels test only the rest
	local ax = fx + ux * v; local ay = fy + uy * v; local az = fz + uz * v
	local nx = ry * az - rz * ay
	local ny = rz * ax - rx * az
	local nz = rx * ay - ry * ax
	local nl = 1.0 / sqrt(nx * nx + ny * ny + nz * nz)
	local row, nrow = {}, 0
	for c = 1, SK_CROWN0 do
		local off = ((SK_CMX[c] - SK_EX) * nx + (SK_CMY[c] - SK_EY) * ny + (SK_CMZ[c] - SK_EZ) * nz) * nl
		if math.abs(off) < SK_CB[c] + 0.02 then nrow = nrow + 1; row[nrow] = c - 1 end
	end
	for g = 4, SK_NG do
		local off = ((SK_GX[g] - SK_EX) * nx + (SK_GY[g] - SK_EY) * ny + (SK_GZ[g] - SK_EZ) * nz) * nl
		if math.abs(off) < SK_GR[g] + 0.02 then nrow = nrow + 1; row[nrow] = 99 + g end
	end
	local base = y * ww
	for x = 0, ww - 1 do
		local u = (x + 0.5 - ww * 0.5) * zoom
		local dx = fx + rx * u + ux * v
		local dy = fy + ry * u + uy * v
		local dz = fz + rz * u + uz * v
		local il = 1.0 / sqrt(dx * dx + dy * dy + dz * dz)
		out[base + x + 1] = sk_trace(dx * il, dy * il, dz * il, x, y, cand, row, nrow)
	end
end

-- the sheet's surface on the + side: Newton steps onto sk_field == SK_TH
local function sk_project(x, y, z)
	for k = 0, 4 do
		local e = 0.004
		local g = sk_field(x, y, z) - SK_TH
		local gx = (sk_field(x + e, y, z) - sk_field(x - e, y, z)) / (2.0 * e)
		local gy = (sk_field(x, y + e, z) - sk_field(x, y - e, z)) / (2.0 * e)
		local gz = (sk_field(x, y, z + e) - sk_field(x, y, z - e)) / (2.0 * e)
		local q = g / (gx * gx + gy * gy + gz * gz + 0.000001)
		x = x - gx * q
		y = y - gy * q
		z = z - gz * q
	end
	return x, y, z
end

Ossuary = class(Scene)

function Ossuary:init()
	Scene.init(self)
	self.bx = 0.4; self.by = 0.3; self.bz = 0.2
	self.fx = 0.0; self.fy = 0.0; self.fz = 1.0
	self.ux = 0.0; self.uy = 1.0; self.uz = 0.0
	self.rx = 1.0; self.ry = 0.0; self.rz = 0.0
	self.cxs = 0.0; self.cys = 0.0; self.czs = 0.0
	self.ex = 0.0; self.ey = 0.0; self.ez = 0.0
	self.cux = 0.0; self.cuy = 1.0; self.cuz = 0.0
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
	self.ppx = {}
	self.ppy = {}
	self.ppz = {}
	self.pvx = {}
	self.pvy = {}
	self.pvz = {}
	self.plife = {}
	self.prate = {}
	self.depth = {}
	self.rows = {}
	self.cand = {}
	self.np = 240
	self.pnext = 0
	self.l1 = 0.5
	self.l2 = 0.66
	self.tt = 0.0
	self.wander = 0.0
	self.spawn_acc = 0.0
end

function Ossuary:build()
	self.name = "OSSUARY"
	self.handle = "eight legs of inverse kinematics . a landscape that will not hold still"
	local pal = self.pal
	SK_WORLD = pal:size()
	for hq = 0, 11 do
		local c = gfx:rainbow_rgb(hq * 0.5236 + 2.2)
		for s = 0, 11 do
			local f = s / 11.0
			local k = f * f * 0.9 + f * 0.25
			local wh = f > 0.82 and (f - 0.82) * 2.2 or 0.0
			local r = 5.0 + c[1] * k * 0.9
			local g = 2.0 + c[2] * k * 0.9
			local b = 10.0 + c[3] * k * 0.9
			pal:add(r + (255.0 - r) * wh, g + (255.0 - g) * wh, b + (255.0 - b) * wh)
		end
	end
	SK_EMBER = pal:size()
	for e = 0, 11 do
		local f = e / 11.0
		pal:add(18.0 + 237.0 * f, 2.0 + 150.0 * f * f * f, 6.0 + 60.0 * f * f * f * f)
	end
	SK_BONE = pal:ramp(12, 26.0, 20.0, 16.0, 250.0, 238.0, 206.0)
	SK_CHIT = pal:size()
	for k = 0, 11 do
		local f = k / 11.0
		pal:add(6.0 + 200.0 * f * f + 49.0 * f * f * f * f, 3.0 + 20.0 * f * f * f + 150.0 * f * f * f * f * f * f, 8.0 + 30.0 * f * f + 60.0 * f * f * f * f * f * f)
	end
	SK_EYE = pal:ramp(4, 220.0, 30.0, 10.0, 255.0, 250.0, 190.0)
	SK_MARK = pal:ramp(6, 90.0, 0.0, 8.0, 255.0, 60.0, 30.0)
	SK_FOG = pal:size()
	for g = 0, 9 do
		local f = g / 9.0
		pal:add(2.0 + 110.0 * f * f, 1.0 + 12.0 * f * f * f, 5.0 + 22.0 * f)
	end
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
	for k = 1, 64 do self.cand[k] = 0 end
	self.wander = self.rng:next(628) * 0.01
end

function Ossuary:set_world(t)
	SK_T = t
	SK_S = 0.8 + 0.07 * sin(t * 0.37)
	SK_K = 0.45 * sin(t * 0.21 + 0.6)
	SK_TW = 0.11 * sin(t * 0.29 + 1.3)
	SK_WA = 0.18 + 0.14 * sin(t * 0.33 + 2.0)
	SK_SHELL = SK_S * (2.1 + SK_WA * 2.4)
	local ph = t * 0.19
	local a = 0.5 + 0.5 * cos(ph)
	local b = 0.5 + 0.5 * cos(ph - 2.0944)
	local c = 0.5 + 0.5 * cos(ph + 2.0944)
	a = a * a * a; b = b * b * b; c = c * c * c
	local sum = a + b + c
	SK_MG = a / sum
	SK_MP = b / sum
	SK_MD = c / sum
	SK_CX = self.cxs; SK_CY = self.cys; SK_CZ = self.czs
end

function Ossuary:frame_from_normal()
	local bx, by, bz = self.bx, self.by, self.bz
	local e = 0.004
	local gx = sk_field(bx + e, by, bz) - sk_field(bx - e, by, bz)
	local gy = sk_field(bx, by + e, bz) - sk_field(bx, by - e, bz)
	local gz = sk_field(bx, by, bz + e) - sk_field(bx, by, bz - e)
	local gl = 1.0 / sqrt(gx * gx + gy * gy + gz * gz + 0.0000001)
	local ux, uy, uz = gx * gl, gy * gl, gz * gl
	local fx, fy, fz = self.fx, self.fy, self.fz
	local d = fx * ux + fy * uy + fz * uz
	fx = fx - ux * d; fy = fy - uy * d; fz = fz - uz * d
	local fl = sqrt(fx * fx + fy * fy + fz * fz)
	if fl < 0.001 then
		fx = uy; fy = uz; fz = ux
		d = fx * ux + fy * uy + fz * uz
		fx = fx - ux * d; fy = fy - uy * d; fz = fz - uz * d
		fl = sqrt(fx * fx + fy * fy + fz * fz)
	end
	fx = fx / fl; fy = fy / fl; fz = fz / fl
	self.ux, self.uy, self.uz = ux, uy, uz
	self.fx, self.fy, self.fz = fx, fy, fz
	self.rx = uy * fz - uz * fy
	self.ry = uz * fx - ux * fz
	self.rz = ux * fy - uy * fx
end

function Ossuary:rest_foot(i, lead)
	local side = i < 4 and 1.0 or -1.0
	local j = i % 4
	local reach = self.legreach[j + 1] + lead
	local x = self.bx + self.fx * reach + self.rx * side * 0.78
	local y = self.by + self.fy * reach + self.ry * side * 0.78
	local z = self.bz + self.fz * reach + self.rz * side * 0.78
	x, y, z = sk_project(x, y, z)
	self.tox[i + 1] = x; self.toy[i + 1] = y; self.toz[i + 1] = z
end

function Ossuary:jitter(n) return (self.rng:next(n * 2 + 1) - n) / (n * 1.0) end

function Ossuary:spawn(x, y, z, vx, vy, vz, life)
	local i = self.pnext + 1
	self.pnext = (self.pnext + 1) % self.np
	self.ppx[i] = x; self.ppy[i] = y; self.ppz[i] = z
	self.pvx[i] = vx; self.pvy[i] = vy; self.pvz[i] = vz
	self.plife[i] = 1.0
	self.prate[i] = 1.0 / life
end

function Ossuary:update_particles(dt)
	local rng = self.rng
	local bx, by, bz = self.bx, self.by, self.bz
	local fx, fy, fz = self.fx, self.fy, self.fz
	local ux, uy, uz = self.ux, self.uy, self.uz
	local rx, ry, rz = self.rx, self.ry, self.rz
	local hx = bx + ux * 0.2
	local hy = by + uy * 0.2
	local hz = bz + uz * 0.2
	self.spawn_acc = self.spawn_acc + dt * 110.0
	while self.spawn_acc >= 1.0 do
		self.spawn_acc = self.spawn_acc - 1.0
		local kind = rng:next(14)
		if kind >= 10 then
			local ca2 = rng:next(628) * 0.01
			local cxr = cos(ca2) * 0.2
			local sxr = sin(ca2) * 0.2
			local crx = hx + fx * 0.34 + ux * 0.55
			local cry = hy + fy * 0.34 + uy * 0.55
			local crz = hz + fz * 0.34 + uz * 0.55
			self:spawn(crx + fx * cxr + rx * sxr, cry + fy * cxr + ry * sxr, crz + fz * cxr + rz * sxr, ux * 0.9 + fx * cxr + rx * sxr, uy * 0.9 + fy * cxr + ry * sxr, uz * 0.9 + fz * cxr + rz * sxr, 0.45 + rng:next(40) * 0.01)
		elseif kind < 4 then
			local ex2 = hx + fx * 0.52 + ux * 0.14 + rx * self:jitter(100) * 0.09
			local ey2 = hy + fy * 0.52 + uy * 0.14 + ry * self:jitter(100) * 0.09
			local ez2 = hz + fz * 0.52 + uz * 0.14 + rz * self:jitter(100) * 0.09
			self:spawn(ex2, ey2, ez2, fx * 0.5 + ux * 0.35 + rx * self:jitter(100) * 0.3, fy * 0.5 + uy * 0.35 + ry * self:jitter(100) * 0.3, fz * 0.5 + uz * 0.35 + rz * self:jitter(100) * 0.3, 1.1 + rng:next(80) * 0.01)
		elseif kind < 6 then
			self:spawn(hx - fx * 0.55, hy - fy * 0.55, hz - fz * 0.55, -fx * 0.3 + ux * 0.4 + self:jitter(100) * 0.2, -fy * 0.3 + uy * 0.4 + self:jitter(100) * 0.2, -fz * 0.3 + uz * 0.4 + self:jitter(100) * 0.2, 1.4 + rng:next(80) * 0.01)
		else
			local r = 0.6 + rng:next(140) * 0.01
			local a = rng:next(628) * 0.01
			local ca = cos(a) * r
			local sa = sin(a) * r
			self:spawn(hx + fx * ca + rx * sa - ux * 0.1, hy + fy * ca + ry * sa - uy * 0.1, hz + fz * ca + rz * sa - uz * 0.1, ux * 0.25, uy * 0.25, uz * 0.25, 2.0 + rng:next(100) * 0.01)
		end
	end
	local drag = 1.0 - dt * 0.9
	local ppx, ppy, ppz, pvx, pvy, pvz, plife, prate = self.ppx, self.ppy, self.ppz, self.pvx, self.pvy, self.pvz, self.plife, self.prate
	for i = 1, self.np do
		if plife[i] > 0.0 then
			local ox = ppx[i] - hx
			local oy = ppy[i] - hy
			local oz = ppz[i] - hz
			local sx = uy * oz - uz * oy
			local sy = uz * ox - ux * oz
			local sz = ux * oy - uy * ox
			pvx[i] = (pvx[i] + (ux * 0.9 + sx * 1.3 - ox * 0.25) * dt) * drag
			pvy[i] = (pvy[i] + (uy * 0.9 + sy * 1.3 - oy * 0.25) * dt) * drag
			pvz[i] = (pvz[i] + (uz * 0.9 + sz * 1.3 - oz * 0.25) * dt) * drag
			ppx[i] = ppx[i] + pvx[i] * dt
			ppy[i] = ppy[i] + pvy[i] * dt
			ppz[i] = ppz[i] + pvz[i] * dt
			plife[i] = plife[i] - dt * prate[i]
		end
	end
end

function Ossuary:update(t, dt)
	self.tt = t
	if not self.placed then
		self.cxs = self.bx; self.cys = self.by; self.czs = self.bz
	end
	self:set_world(t)
	local wander = self.wander
	local turn = 0.75 * sin(t * 0.53 + wander) + 0.45 * sin(t * 1.37 + wander * 2.0) + 0.2 * sin(t * 2.9)
	local ang = turn * dt * 1.6
	local c = cos(ang)
	local s = sin(ang)
	local nfx = self.fx * c + self.rx * s
	local nfy = self.fy * c + self.ry * s
	local nfz = self.fz * c + self.rz * s
	self.fx = nfx; self.fy = nfy; self.fz = nfz
	local speed = 0.55 + 0.2 * sin(t * 0.8)
	self.bx = self.bx + nfx * speed * dt
	self.by = self.by + nfy * speed * dt
	self.bz = self.bz + nfz * speed * dt
	self.bx, self.by, self.bz = sk_project(self.bx, self.by, self.bz)
	self:frame_from_normal()
	local k = dt * 0.6
	k = k > 1.0 and 1.0 or k
	self.cxs = self.cxs + (self.bx - self.cxs) * k
	self.cys = self.cys + (self.by - self.cys) * k
	self.czs = self.czs + (self.bz - self.czs) * k
	SK_CX = self.cxs; SK_CY = self.cys; SK_CZ = self.czs
	local footx, footy, footz = self.footx, self.footy, self.footz
	local tox, toy, toz = self.tox, self.toy, self.toz
	local fromx, fromy, fromz, stept = self.fromx, self.fromy, self.fromz, self.stept
	if not self.placed then
		for i = 0, 7 do
			self:rest_foot(i, 0.0)
			footx[i + 1] = tox[i + 1]; footy[i + 1] = toy[i + 1]; footz[i + 1] = toz[i + 1]
		end
	end
	local stepping_a = false
	local stepping_b = false
	for i = 0, 7 do
		if stept[i + 1] >= 0.0 then
			if (i + i // 4) % 2 == 0 then stepping_a = true else stepping_b = true end
		end
	end
	for i = 0, 7 do
		local j = i + 1
		local group_a = (i + i // 4) % 2 == 0
		if stept[j] >= 0.0 then
			stept[j] = stept[j] + dt / 0.17
			self:rest_foot(i, 0.2)
			if stept[j] >= 1.0 then
				stept[j] = -1.0
				footx[j] = tox[j]; footy[j] = toy[j]; footz[j] = toz[j]
			else
				local e = stept[j]
				local lift = sin(e * 3.14159265) * 0.22
				footx[j] = fromx[j] + (tox[j] - fromx[j]) * e + self.ux * lift
				footy[j] = fromy[j] + (toy[j] - fromy[j]) * e + self.uy * lift
				footz[j] = fromz[j] + (toz[j] - fromz[j]) * e + self.uz * lift
			end
		else
			local px, py, pz = sk_project(footx[j], footy[j], footz[j])
			footx[j] = px; footy[j] = py; footz[j] = pz
			self:rest_foot(i, 0.0)
			local ddx = footx[j] - tox[j]
			local ddy = footy[j] - toy[j]
			local ddz = footz[j] - toz[j]
			local other_moving = group_a and stepping_b or (not group_a and stepping_a)
			if not other_moving and ddx * ddx + ddy * ddy + ddz * ddz > 0.1 then
				stept[j] = 0.0
				fromx[j] = footx[j]; fromy[j] = footy[j]; fromz[j] = footz[j]
				if group_a then stepping_a = true else stepping_b = true end
			end
		end
	end
	self:update_particles(dt)
	local ux, uy, uz = self.ux, self.uy, self.uz
	local fx, fy, fz = self.fx, self.fy, self.fz
	local rx, ry, rz = self.rx, self.ry, self.rz
	local bob = 0.2 + 0.018 * sin(t * 11.0)
	local hx = self.bx + ux * bob
	local hy = self.by + uy * bob
	local hz = self.bz + uz * bob
	local orbit = 2.4 + t * 0.33 + 0.5 * sin(t * 0.17)
	local oc = cos(orbit) * 2.1
	local os = sin(orbit) * 2.1
	local dex = hx + fx * oc + rx * os + ux * 1.0
	local dey = hy + fy * oc + ry * os + uy * 1.0
	local dez = hz + fz * oc + rz * os + uz * 1.0
	local cdx = dex - hx
	local cdy = dey - hy
	local cdz = dez - hz
	local want = sqrt(cdx * cdx + cdy * cdy + cdz * cdz)
	cdx = cdx / want; cdy = cdy / want; cdz = cdz / want
	local reach = 1.5
	while reach < want do
		local d = sk_world(hx + cdx * reach, hy + cdy * reach, hz + cdz * reach)
		if d < 0.14 then break end
		reach = reach + d * 0.8
	end
	reach = reach > want and want or reach
	reach = reach - 0.1
	reach = reach < 1.5 and 1.5 or reach
	dex = hx + cdx * reach
	dey = hy + cdy * reach
	dez = hz + cdz * reach
	local kc = self.placed and dt * 2.8 or 1.0
	kc = kc > 1.0 and 1.0 or kc
	self.ex = self.ex + (dex - self.ex) * kc
	self.ey = self.ey + (dey - self.ey) * kc
	self.ez = self.ez + (dez - self.ez) * kc
	local ku = self.placed and dt * 2.2 or 1.0
	ku = ku > 1.0 and 1.0 or ku
	self.cux = self.cux + (ux - self.cux) * ku
	self.cuy = self.cuy + (uy - self.cuy) * ku
	self.cuz = self.cuz + (uz - self.cuz) * ku
	self.placed = true
end

local function capsule(i, ax, ay, az, bx2, by2, bz2, r)
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

local function glow(i, x, y, z, r)
	i = i + 1
	SK_GX[i] = x; SK_GY[i] = y; SK_GZ[i] = z; SK_GR[i] = r
end

-- a curved horn: base, a bend out and back, a tip up and back
local function horn(i, side)
	local bx0 = SK_HX + SK_PUX * 0.16 + SK_PRX * side * 0.12 - SK_PFX * 0.04
	local by0 = SK_HY + SK_PUY * 0.16 + SK_PRY * side * 0.12 - SK_PFY * 0.04
	local bz0 = SK_HZ + SK_PUZ * 0.16 + SK_PRZ * side * 0.12 - SK_PFZ * 0.04
	local mx = bx0 + SK_PRX * side * 0.2 + SK_PUX * 0.12 - SK_PFX * 0.12
	local my = by0 + SK_PRY * side * 0.2 + SK_PUY * 0.12 - SK_PFY * 0.12
	local mz = bz0 + SK_PRZ * side * 0.2 + SK_PUZ * 0.12 - SK_PFZ * 0.12
	local tx = mx + SK_PRX * side * 0.04 + SK_PUX * 0.26 - SK_PFX * 0.2
	local ty = my + SK_PRY * side * 0.04 + SK_PUY * 0.26 - SK_PFY * 0.2
	local tz = mz + SK_PRZ * side * 0.04 + SK_PUZ * 0.26 - SK_PFZ * 0.2
	capsule(i, bx0, by0, bz0, mx, my, mz, 0.05)
	capsule(i + 1, mx, my, mz, tx, ty, tz, 0.028)
	return i + 2
end

function Ossuary:render(grid)
	local tt = self.tt
	local w, h = self.w, self.h
	self:set_world(tt)
	SK_W = w
	SK_H = h
	SK_FLICK = 0.7 * sin(tt * 17.0) * sin(tt * 5.3)
	local fx, fy, fz = self.fx, self.fy, self.fz
	local ux, uy, uz = self.ux, self.uy, self.uz
	local rx, ry, rz = self.rx, self.ry, self.rz
	local bob = 0.2 + 0.018 * sin(tt * 11.0)
	local cx = self.bx + ux * bob
	local cy = self.by + uy * bob
	local cz = self.bz + uz * bob
	SK_PFX = fx; SK_PFY = fy; SK_PFZ = fz
	SK_PUX = ux; SK_PUY = uy; SK_PUZ = uz
	SK_PRX = rx; SK_PRY = ry; SK_PRZ = rz
	SK_TX = cx; SK_TY = cy; SK_TZ = cz
	SK_AX = cx - fx * 0.4 + ux * 0.1; SK_AY = cy - fy * 0.4 + uy * 0.1; SK_AZ = cz - fz * 0.4 + uz * 0.1
	local nod = 0.03 * sin(tt * 2.1)
	SK_HX = cx + fx * 0.34 + ux * (0.09 + nod); SK_HY = cy + fy * 0.34 + uy * (0.09 + nod); SK_HZ = cz + fz * 0.34 + uz * (0.09 + nod)
	SK_NX = SK_HX + fx * 0.23 - ux * 0.06; SK_NY = SK_HY + fy * 0.23 - uy * 0.06; SK_NZ = SK_HZ + fz * 0.23 - uz * 0.06
	local jaw = 0.012 * sin(tt * 6.0)
	SK_JX = SK_HX + fx * 0.1 - ux * (0.14 + jaw); SK_JY = SK_HY + fy * 0.1 - uy * (0.14 + jaw); SK_JZ = SK_HZ + fz * 0.1 - uz * (0.14 + jaw)
	SK_BCX = cx; SK_BCY = cy; SK_BCZ = cz
	SK_BR = 1.6
	local ncap = 5 + 2 + 16 + 12 + 7
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
	glow(0, SK_HX + fx * 0.175 + ux * 0.03 + rx * 0.088, SK_HY + fy * 0.175 + uy * 0.03 + ry * 0.088, SK_HZ + fz * 0.175 + uz * 0.03 + rz * 0.088, 0.03)
	glow(1, SK_HX + fx * 0.175 + ux * 0.03 - rx * 0.088, SK_HY + fy * 0.175 + uy * 0.03 - ry * 0.088, SK_HZ + fz * 0.175 + uz * 0.03 - rz * 0.088, 0.03)
	local third = 0.024 + 0.006 * sin(tt * 3.0)
	glow(2, SK_HX + fx * 0.17 + ux * 0.15, SK_HY + fy * 0.17 + uy * 0.15, SK_HZ + fz * 0.17 + uz * 0.15, third)
	SK_LX = SK_GX[3] * 0.4 + (SK_GX[1] + SK_GX[2]) * 0.3 + fx * 0.1
	SK_LY = SK_GY[3] * 0.4 + (SK_GY[1] + SK_GY[2]) * 0.3 + fy * 0.1
	SK_LZ = SK_GZ[3] * 0.4 + (SK_GZ[1] + SK_GZ[2]) * 0.3 + fz * 0.1
	capsule(0, SK_JX - rx * 0.08, SK_JY - ry * 0.08, SK_JZ - rz * 0.08, SK_JX + rx * 0.08, SK_JY + ry * 0.08, SK_JZ + rz * 0.08, 0.05)
	local ci = horn(1, 1.0)
	ci = horn(ci, -1.0)
	local fang = 0.02 * sin(tt * 6.0 + 1.0)
	for s = 0, 1 do
		local side = s == 0 and 1.0 or -1.0
		local ax = SK_JX + fx * 0.05 + rx * side * 0.035
		local ay = SK_JY + fy * 0.05 + ry * side * 0.035
		local az = SK_JZ + fz * 0.05 + rz * side * 0.035
		capsule(ci, ax, ay, az, ax + fx * 0.07 - ux * 0.12 - rx * side * (0.02 + fang), ay + fy * 0.07 - uy * 0.12 - ry * side * (0.02 + fang), az + fz * 0.07 - uz * 0.12 - rz * side * (0.02 + fang), 0.016)
		ci = ci + 1
	end
	local l1, l2 = self.l1, self.l2
	local footx, footy, footz, legfo = self.footx, self.footy, self.footz, self.legfo
	for i = 0, 7 do
		local side = i < 4 and 1.0 or -1.0
		local j = i % 4
		local fo = legfo[j + 1]
		local hx = cx + fx * fo * 0.45 + rx * side * 0.1 + ux * 0.03
		local hy = cy + fy * fo * 0.45 + ry * side * 0.1 + uy * 0.03
		local hz = cz + fz * fo * 0.45 + rz * side * 0.1 + uz * 0.03
		local dx = footx[i + 1] - hx
		local dy = footy[i + 1] - hy
		local dz = footz[i + 1] - hz
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
		local tipx = hx + dx * dc
		local tipy = hy + dy * dc
		local tipz = hz + dz * dc
		capsule(ci, hx, hy, hz, kx, ky, kz, 0.034)
		capsule(ci + 1, kx, ky, kz, tipx, tipy, tipz, 0.02)
		ci = ci + 2
		glow(3 + i, kx, ky, kz, 0.036 + 0.008 * sin(tt * 7.0 + i))
	end
	for k = 0, 2 do
		local side = k - 1.0
		local px = SK_AX - fx * 0.3 + rx * side * 0.07
		local py = SK_AY - fy * 0.3 + ry * side * 0.07
		local pz = SK_AZ - fz * 0.3 + rz * side * 0.07
		for seg = 0, 3 do
			local ph = tt * 3.1 + seg * 0.9 + k * 2.1
			local sw = sin(ph) * (0.1 + seg * 0.05)
			local lift = cos(ph * 0.7 + k) * 0.08 + 0.05
			local len = 0.2 - seg * 0.025
			local qx = px - fx * len + rx * (sw + side * 0.05) + ux * lift
			local qy = py - fy * len + ry * (sw + side * 0.05) + uy * lift
			local qz = pz - fz * len + rz * (sw + side * 0.05) + uz * lift
			capsule(ci, px, py, pz, qx, qy, qz, 0.03 - seg * 0.006)
			ci = ci + 1
			px = qx; py = qy; pz = qz
		end
	end
	local hover = 0.03 * sin(tt * 1.7)
	SK_HGX = SK_HX + ux * 0.3; SK_HGY = SK_HY + uy * 0.3; SK_HGZ = SK_HZ + uz * 0.3
	SK_BGX = (SK_AX + SK_TX) * 0.5; SK_BGY = (SK_AY + SK_TY) * 0.5; SK_BGZ = (SK_AZ + SK_TZ) * 0.5
	SK_CRX = SK_HX + ux * (0.44 + hover); SK_CRY = SK_HY + uy * (0.44 + hover); SK_CRZ = SK_HZ + uz * (0.44 + hover)
	for k = 0, 6 do
		local a = k * 0.8976 + tt * 0.6
		local ca = cos(a) * 0.2
		local sa = sin(a) * 0.2
		local flame = 0.1 + 0.05 * sin(tt * 9.0 + k * 1.9) + 0.03 * sin(tt * 23.0 + k)
		local bxk = SK_CRX + fx * ca + rx * sa
		local byk = SK_CRY + fy * ca + ry * sa
		local bzk = SK_CRZ + fz * ca + rz * sa
		capsule(SK_CROWN0 + k, bxk, byk, bzk, bxk + ux * flame + fx * ca * 0.25 + rx * sa * 0.25, byk + uy * flame + fy * ca * 0.25 + ry * sa * 0.25, bzk + uz * flame + fz * ca * 0.25 + rz * sa * 0.25, 0.018)
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
	local spin = tt * 2.2
	local gl2 = 0.3 + 0.02 * sin(tt * 2.5)
	SK_GMX = SK_CRX + ux * gl2; SK_GMY = SK_CRY + uy * gl2; SK_GMZ = SK_CRZ + uz * gl2
	SK_GAX = rx * cos(spin) + fx * sin(spin); SK_GAY = ry * cos(spin) + fy * sin(spin); SK_GAZ = rz * cos(spin) + fz * sin(spin)
	SK_GBX = fx * cos(spin) - rx * sin(spin); SK_GBY = fy * cos(spin) - ry * sin(spin); SK_GBZ = fz * cos(spin) - rz * sin(spin)
	local ex, ey, ez = self.ex, self.ey, self.ez
	SK_EX = ex; SK_EY = ey; SK_EZ = ez
	SK_EYE_INSIDE = sk_world(ex, ey, ez) < 0.0
	local lx = cx + fx * 0.3 + ux * 0.25 - ex
	local ly = cy + fy * 0.3 + uy * 0.25 - ey
	local lz = cz + fz * 0.3 + uz * 0.25 - ez
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
	local rows = self.rows
	local cand = self.cand
	for y = 0, h - 1 do sk_row(y, rows, cand) end
	local depth = self.depth
	local n = w * h
	for q = 1, n do
		local v = rows[q]
		grid[q] = v % 1024
		depth[q] = v // 1024
	end
	self:draw_particles(grid)
end

function Ossuary:plot(grid, x, y, dq, level)
	local w = self.w
	if x < 0 or y < 0 or x >= w or y >= self.h then return end
	local at = y * w + x + 1
	if self.depth[at] <= dq then return end
	local cur = grid[at]
	if cur >= SK_SPARK and cur - SK_SPARK >= level then return end
	grid[at] = SK_SPARK + level
end

function Ossuary:splat(grid, x, y, z, level, size)
	local ox = x - self.ex
	local oy = y - self.ey
	local oz = z - self.ez
	local zc = ox * SK_FX + oy * SK_FY + oz * SK_FZ
	if zc < 0.08 then return end
	local half_h = self.h * 0.5
	local scale = half_h / (0.6 * zc)
	local sx = (ox * SK_RX + oy * SK_RY + oz * SK_RZ) * scale + self.w * 0.5
	local sy = half_h - (ox * SK_UX + oy * SK_UY + oz * SK_UZ) * scale
	local px = floor(sx)
	local py = floor(sy)
	local dq = itrunc(zc * 18.0)
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
	local tt = self.tt
	local plife, ppx, ppy, ppz = self.plife, self.ppx, self.ppy, self.ppz
	for i = 1, self.np do
		local life = plife[i]
		if life > 0.0 then
			local level = itrunc(life * 7.99)
			self:splat(grid, ppx[i], ppy[i], ppz[i], level, life > 0.55 and 1 or 0)
		end
	end
	local fx, fy, fz = self.fx, self.fy, self.fz
	local ux, uy, uz = self.ux, self.uy, self.uz
	local rx, ry, rz = self.rx, self.ry, self.rz
	local hx = self.bx + ux * 0.25
	local hy = self.by + uy * 0.25
	local hz = self.bz + uz * 0.25
	for g = 0, 6 do
		local a = tt * (0.9 + g * 0.07) + g * 0.8976
		local r = 1.05 + 0.2 * sin(tt * 1.3 + g)
		local ca = cos(a) * r
		local sa = sin(a) * r
		local lift = 0.35 + 0.25 * sin(tt * 2.0 + g * 1.7)
		local pulse = itrunc(5.5 + 2.0 * sin(tt * 6.0 + g))
		self:splat(grid, hx + fx * ca + rx * sa + ux * lift, hy + fy * ca + ry * sa + uy * lift, hz + fz * ca + rz * sa + uz * lift, pulse > 7 and 7 or pulse, 2)
	end
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
