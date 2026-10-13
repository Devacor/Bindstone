-- SPONGEWORKS: a raymarched 3D Menger sponge: per-pixel SDF sphere tracing with
-- the KIFS fold, an exact slab test, one tracked re-fold for the face normal, a
-- distance-probe AO, and a 4-phase Bayer temporal update into a persistent buffer.
-- The JaiScript rows run on parallel_transform and return the row; here each row
-- updates its own slice of the persistent buffers in place (the same values).

local sin, cos, sqrt = math.sin, math.cos, math.sqrt

local SP_PIX, SP_T, SP_STAR, SP_FBR = {}, {}, {}, {}
local SP_W, SP_H, SP_PHASE, SP_STEPS = 0, 0, 0, 20
local SP_SBASE, SP_STARP, SP_EDGEP = 0, 0, 0
local SP_FL = 1.85
local SP_FX, SP_FY, SP_FZ = 0.0, 0.0, -1.0
local SP_RX, SP_RZ = 1.0, 0.0
local SP_UX, SP_UY, SP_UZ = 0.0, 1.0, 0.0
local SP_EX, SP_EY, SP_EZ = 0.0, 0.0, 3.3

local function sponge_row(py)
	local rww = SP_W
	local rhh = SP_H
	local phase = SP_PHASE
	local ms = SP_STEPS
	local sb = SP_SBASE
	local stp_ = SP_STARP
	local edp = SP_EDGEP
	local fl_ = SP_FL
	local lfx = SP_FX
	local lfy = SP_FY
	local lfz = SP_FZ
	local lrx = SP_RX
	local lrz = SP_RZ
	local lux = SP_UX
	local luy = SP_UY
	local luz = SP_UZ
	local lex = SP_EX
	local ley = SP_EY
	local lez = SP_EZ
	local pix, tbuf, star, fbr = SP_PIX, SP_T, SP_STAR, SP_FBR
	local du = 2.0 / rww
	local uc = rww * 0.5 - 0.5
	local vc = rhh * 0.5 - 0.5
	local base = py * rww + 1
	local v = (vc - py) * du
	for px = 0, rww - 1 do
		if ((py & 1) * 2 + (px & 1)) == phase then
			local u = (px - uc) * du
			local rdx = lfx * fl_ + lrx * u + lux * v
			local rdy = lfy * fl_ + luy * v
			local rdz = lfz * fl_ + lrz * u + luz * v
			local il = 1.0 / sqrt(rdx * rdx + rdy * rdy + rdz * rdz)
			rdx = rdx * il
			rdy = rdy * il
			rdz = rdz * il
			-- slab test against the bounding box [-1.15, 1.15]^3
			local sdx = (rdx < 0.000001 and rdx > -0.000001) and 0.000001 or rdx
			local sdy = (rdy < 0.000001 and rdy > -0.000001) and 0.000001 or rdy
			local sdz = (rdz < 0.000001 and rdz > -0.000001) and 0.000001 or rdz
			local ix = 1.0 / sdx
			local iy = 1.0 / sdy
			local izl = 1.0 / sdz
			local ta = (-1.15 - lex) * ix
			local tb = (1.15 - lex) * ix
			local tn1 = ta < tb and ta or tb
			local tf1 = ta < tb and tb or ta
			local tc = (-1.15 - ley) * iy
			local td = (1.15 - ley) * iy
			local tn2 = tc < td and tc or td
			local tf2 = tc < td and td or tc
			local te = (-1.15 - lez) * izl
			local tg = (1.15 - lez) * izl
			local tn3 = te < tg and te or tg
			local tf3 = te < tg and tg or te
			local tn = tn1 > tn2 and tn1 or tn2
			tn = tn3 > tn and tn3 or tn
			local tf = tf1 < tf2 and tf1 or tf2
			tf = tf3 < tf and tf3 or tf
			local at = base + px
			if tf < tn or tf < 0.0 then
				pix[at] = star[at] == 1 and stp_ or py
			else
				local tray = tn > 0.0 and tn + 0.002 or 0.002
				-- temporal reprojection: the last hit distance of this pixel is a
				-- near-perfect march start under a slow orbit (back up 15%)
				local tc0 = tbuf[at]
				tc0 = tc0 * 0.85
				tray = tc0 > tray and tc0 or tray
				local fit = 3
				local fsc = 0.037037037
				local hit = 0
				local stp = 0
				local sxp, syp, szp = 0.0, 0.0, 0.0
				local d = 0.0
				while stp < ms do
					sxp = lex + rdx * tray
					syp = ley + rdy * tray
					szp = lez + rdz * tray
					-- Menger KIFS fold, 3 iterations, inlined
					local qx = sxp
					local qy = syp
					local qz = szp
					for it = 1, fit do
						qx = qx < 0.0 and -qx or qx
						qy = qy < 0.0 and -qy or qy
						qz = qz < 0.0 and -qz or qz
						if qx < qy then qx, qy = qy, qx end
						if qx < qz then qx, qz = qz, qx end
						if qy < qz then qy, qz = qz, qy end
						qx = qx * 3.0 - 2.0
						qy = qy * 3.0 - 2.0
						qz = qz * 3.0 - 2.0
						if qz < -1.0 then qz = qz + 2.0 end
					end
					local aq = qx < 0.0 and -qx or qx
					local aq2 = qy < 0.0 and -qy or qy
					local aq3 = qz < 0.0 and -qz or qz
					local mq = aq > aq2 and aq or aq2
					mq = aq3 > mq and aq3 or mq
					d = (mq - 1.0) * fsc
					if d < 0.002 + tray * 0.0035 then hit = 1; break end
					tray = tray + d
					if tray > tf then break end
					stp = stp + 1
				end
				if hit == 1 then
					-- exact face normal from ONE tracked re-fold
					local qx2 = sxp
					local qy2 = syp
					local qz2 = szp
					local ax0, ax1, ax2 = 0, 2, 4
					local sg0, sg1, sg2 = 0, 0, 0
					for it2 = 1, fit do
						if qx2 < 0.0 then qx2 = -qx2; sg0 = 1 - sg0 end
						if qy2 < 0.0 then qy2 = -qy2; sg1 = 1 - sg1 end
						if qz2 < 0.0 then qz2 = -qz2; sg2 = 1 - sg2 end
						if qx2 < qy2 then
							qx2, qy2 = qy2, qx2
							ax0, ax1 = ax1, ax0
							sg0, sg1 = sg1, sg0
						end
						if qx2 < qz2 then
							qx2, qz2 = qz2, qx2
							ax0, ax2 = ax2, ax0
							sg0, sg2 = sg2, sg0
						end
						if qy2 < qz2 then
							qy2, qz2 = qz2, qy2
							ax1, ax2 = ax2, ax1
							sg1, sg2 = sg2, sg1
						end
						qx2 = qx2 * 3.0 - 2.0
						qy2 = qy2 * 3.0 - 2.0
						qz2 = qz2 * 3.0 - 2.0
						if qz2 < -1.0 then qz2 = qz2 + 2.0 end
					end
					local bq = qx2 < 0.0 and -qx2 or qx2
					local bq2 = qy2 < 0.0 and -qy2 or qy2
					local bq3 = qz2 < 0.0 and -qz2 or qz2
					local face = 0
					if bq >= bq2 and bq >= bq3 then
						face = ax0 + ((qx2 > 0.0 and 0 or 1) + sg0) % 2
					elseif bq2 >= bq3 then
						face = ax1 + ((qy2 > 0.0 and 0 or 1) + sg1) % 2
					else
						face = ax2 + ((qz2 > 0.0 and 0 or 1) + sg2) % 2
					end
					-- AO from one SDF probe 0.07 along the face normal
					local aopx = sxp
					local aopy = syp
					local aopz = szp
					local wax = face // 2
					local wsg = (face & 1) == 0 and 0.07 or -0.07
					aopx = wax == 0 and aopx + wsg or aopx
					aopy = wax == 1 and aopy + wsg or aopy
					aopz = wax == 2 and aopz + wsg or aopz
					for it3 = 1, 3 do
						aopx = aopx < 0.0 and -aopx or aopx
						aopy = aopy < 0.0 and -aopy or aopy
						aopz = aopz < 0.0 and -aopz or aopz
						if aopx < aopy then aopx, aopy = aopy, aopx end
						if aopx < aopz then aopx, aopz = aopz, aopx end
						if aopy < aopz then aopy, aopz = aopz, aopy end
						aopx = aopx * 3.0 - 2.0
						aopy = aopy * 3.0 - 2.0
						aopz = aopz * 3.0 - 2.0
						if aopz < -1.0 then aopz = aopz + 2.0 end
					end
					local cq = aopx < 0.0 and -aopx or aopx
					local cq2 = aopy < 0.0 and -aopy or aopy
					local cq3 = aopz < 0.0 and -aopz or aopz
					local cm = cq > cq2 and cq or cq2
					cm = cq3 > cm and cq3 or cm
					local ao = (cm - 1.0) * 0.037037037 * 15.0
					ao = ao > 1.0 and 1.0 or ao
					ao = ao < 0.0 and 0.0 or ao
					local fbv = fbr[face + 1]
					local fog = 1.0 - (tray - 1.0) * 0.09
					fog = fog > 1.0 and 1.0 or fog
					fog = fog < 0.45 and 0.45 or fog
					local li = fbv * (0.40 + 0.60 * ao) * fog
					li = li < 0.0 and 0.0 or li
					li = li > 0.999 and 0.999 or li
					local lvl = itrunc(li * 16.0)
					pix[at] = sb + face * 16 + lvl
					tbuf[at] = tray
				elseif tray > tf then
					-- clean exit through a hole: background
					pix[at] = star[at] == 1 and stp_ or py
					tbuf[at] = 0.0
				else
					-- step budget spent deep in a crevice: free contact shadow
					pix[at] = edp
					tbuf[at] = tray
				end
			end
		end
	end
end

SpongeWorks = class(Scene)

function SpongeWorks:init()
	Scene.init(self)
	self.rw = 0
	self.rh = 0
	self.starp = 0
	self.edgep = 0
	self.sbase = 0
	self.rframe = 0
	self.msteps = 20
	self.fl = 1.85
	self.ex = 0.0
	self.ey = 0.0
	self.ez = 3.3
	self.fx = 0.0
	self.fy = 0.0
	self.fz = -1.0
	self.rgx = 1.0
	self.rgz = 0.0
	self.upx = 0.0
	self.upy = 1.0
	self.upz = 0.0
	self.tt = 0.0
end

function SpongeWorks:build()
	self.name = "SPONGEWORKS"
	self.handle = "menger dive . signed distance, folded by hand"
	local pal = self.pal
	self.rw = self.w
	self.rh = self.h
	local rw, rh = self.rw, self.rh
	SP_PIX = make_grid(rw * rh)
	SP_T = make_fgrid(rw * rh)
	SP_STAR = {}
	-- background: deep violet gradient rows (entries 0..rh-1), plus one star entry
	for py = 0, rh - 1 do
		local f = py / (rh - 1.0)
		pal:add(8.0 + 12.0 * f, 6.0 + 9.0 * f, 24.0 + 28.0 * f)
	end
	self.starp = pal:add(196, 205, 240)
	self.edgep = pal:add(7, 6, 16)
	self.sbase = self.edgep + 1
	-- six face ramps, 16 brightness levels each, gamma-bent
	local famb = {{255.0, 194.0, 118.0}, {196.0, 98.0, 62.0}, {255.0, 233.0, 170.0},
		{104.0, 70.0, 96.0}, {112.0, 222.0, 206.0}, {128.0, 150.0, 194.0}}
	for fi = 1, 6 do
		local fc = famb[fi]
		local fr, fg, fb = fc[1], fc[2], fc[3]
		for lv = 0, 15 do
			local k = 0.06 + 0.94 * lv / 15.0
			local k2 = k * (0.35 + 0.65 * k)
			pal:add(fr * k2, fg * k2, fb * k2)
		end
	end
	SP_FBR = {0.95, 0.45, 1.0, 0.30, 0.42, 0.72}
	local sd = self.seed & 65535
	local npix = rw * rh
	for i = 0, npix - 1 do
		local hsh = ((i * 40503 + sd) * 2971) & 1048575
		SP_STAR[i + 1] = ((hsh // 128) & 255) < 3 and 1 or 0
	end
end

function SpongeWorks:update(t, dt)
	self.tt = t
	-- stately orbit, slow approach, then the dive toward the lattice
	local ang = t * 0.26 + 0.7
	local el = 0.38 + 0.22 * sin(t * 0.19 + 1.2)
	local rad = 3.9 - t * 0.05
	if t > 11.0 then rad = rad - (t - 11.0) * 0.22 end
	rad = rad < 1.7 and 1.7 or rad
	rad = rad + 0.06 * sin(t * 0.7)
	local cel = cos(el)
	local ex = rad * cel * cos(ang)
	local ey = rad * sin(el)
	local ez = rad * cel * sin(ang)
	local inr = 0.0 - 1.0 / rad
	local fx = ex * inr
	local fy = ey * inr
	local fz = ez * inr
	local rl = sqrt(fx * fx + fz * fz)
	rl = rl < 0.0001 and 0.0001 or rl
	local rgx = fz / rl
	local rgz = 0.0 - fx / rl
	self.ex, self.ey, self.ez = ex, ey, ez
	self.fx, self.fy, self.fz = fx, fy, fz
	self.rgx, self.rgz = rgx, rgz
	self.upx = fy * rgz
	self.upy = fz * rgx - fx * rgz
	self.upz = 0.0 - fy * rgx
end

function SpongeWorks:render(grid)
	self.rframe = self.rframe + 1
	SP_PHASE = self.rframe & 3
	SP_W = self.rw
	SP_H = self.rh
	SP_STEPS = self.msteps
	SP_SBASE = self.sbase
	SP_STARP = self.starp
	SP_EDGEP = self.edgep
	SP_FL = self.fl
	SP_FX = self.fx; SP_FY = self.fy; SP_FZ = self.fz
	SP_RX = self.rgx; SP_RZ = self.rgz
	SP_UX = self.upx; SP_UY = self.upy; SP_UZ = self.upz
	SP_EX = self.ex; SP_EY = self.ey; SP_EZ = self.ez
	local rh = self.rh
	for y = 0, rh - 1 do sponge_row(y) end
	local pix = SP_PIX
	for i = 1, self.rw * rh do grid[i] = pix[i] end
end
