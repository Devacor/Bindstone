-- CANYONRUN: the Comanche (1992) VoxelSpace algorithm. A diamond-square
-- heightmap with a carved river canyon, a packed height+color map, and the
-- classic front-to-back per-depth-line march with a per-column y-buffer in
-- 16.16 fixed point.

local sin, sqrt, atan, floor = math.sin, math.sqrt, math.atan, math.floor

CanyonRun = class(Scene)

function CanyonRun:init()
	Scene.init(self)
	self.hmap = {}
	self.cmap = {}
	self.ybuf = {}
	self.rolloff = {}
	self.zs = {}
	self.invz = {}
	self.hazes = {}
	self.msz = 128
	self.mmask = 127
	self.tbase = 0
	self.nsteps = 72
	self.nhaze = 10
	self.sunb = 0
	self.posx = 64.0
	self.posy = 0.0
	self.dirx = 0.0
	self.diry = 1.0
	self.planex = 0.0
	self.planey = 0.0
	self.pdx = 0.0
	self.pdy = 1.0
	self.camhf = 210.0
	self.roll = 0.0
	self.ph1 = 0.0
	self.ph2 = 0.0
	self.camh = 210
	self.horizon = 12
	self.tt = 0.0
end

function CanyonRun:build()
	self.name = "CANYONRUN"
	self.handle = "river meander . a heightmap, a y-buffer, 1992"
	local w, h, pal, rng = self.w, self.h, self.pal, self.rng
	-- sky: entries 0..h-1, deep dusk blue up top warming toward the horizon
	for py = 0, h - 1 do
		local f = py / (h - 1.0)
		local f2 = f * f
		pal:add(16.0 + 150.0 * f2 + 30.0 * f, 24.0 + 138.0 * f2 + 18.0 * f, 64.0 + 130.0 * f)
	end
	self.tbase = h
	local nhaze = self.nhaze
	-- 8 height bands x 4 light shades x 10 haze banks (haze lerps toward the sky)
	local bands = {{16.0, 48.0, 112.0}, {30.0, 94.0, 158.0}, {152.0, 134.0, 94.0}, {98.0, 128.0, 58.0},
		{154.0, 88.0, 54.0}, {190.0, 124.0, 78.0}, {146.0, 142.0, 136.0}, {238.0, 242.0, 248.0}}
	for b = 1, 8 do
		local bc = bands[b]
		local br, bg, bb = bc[1], bc[2], bc[3]
		for sh = 0, 3 do
			local k = 0.52 + sh * 0.20
			local cr = br * k
			local cg = bg * k
			local cb = bb * k
			for hq = 0, nhaze - 1 do
				local hf = hq * 0.093
				pal:add(cr + (176.0 - cr) * hf, cg + (176.0 - cg) * hf, cb + (206.0 - cb) * hf)
			end
		end
	end
	-- diamond-square with wraparound; displacement from an inline integer hash
	local msz, mmask = self.msz, self.mmask
	local hmap = make_grid(msz * msz)
	self.hmap = hmap
	local sd = self.seed & 65535
	hmap[1] = 110 + (sd & 63)
	local step = msz
	local amp = 92
	while step > 1 do
		local half = step // 2
		for dy = half, msz - 1, step do
			local rw0 = ((dy - half) & mmask) * msz + 1
			local rw1 = ((dy + half) & mmask) * msz + 1
			local rwc = dy * msz + 1
			for dx = half, msz - 1, step do
				local x0 = dx - half
				local x1 = (dx + half) & mmask
				local a1 = hmap[rw0 + x0]
				local a2 = hmap[rw0 + x1]
				local a3 = hmap[rw1 + x0]
				local a4 = hmap[rw1 + x1]
				local hsh = (dx * 73856093 + dy * 19349663 + step * 83492791 + sd * 2971) & 1048575
				local d = (((hsh * 1274126177) // 4096) & 255) - 128
				hmap[rwc + dx] = idiv(a1 + a2 + a3 + a4, 4) + idiv(d * amp, 128)
			end
		end
		for sy = 0, msz - 1, half do
			local xoff = (sy % step) == 0 and half or 0
			local rws = sy * msz + 1
			local rwu = ((sy - half) & mmask) * msz + 1
			local rwd = ((sy + half) & mmask) * msz + 1
			for sx = xoff, msz - 1, step do
				local xl = (sx - half) & mmask
				local xr = (sx + half) & mmask
				local b1 = hmap[rws + xl]
				local b2 = hmap[rws + xr]
				local b3 = hmap[rwu + sx]
				local b4 = hmap[rwd + sx]
				local hsh2 = (sx * 19349663 + sy * 73856093 + step * 297121507 + sd * 7919) & 1048575
				local d2 = (((hsh2 * 1274126177) // 4096) & 255) - 128
				hmap[rws + sx] = idiv(b1 + b2 + b3 + b4, 4) + idiv(d2 * amp, 128)
			end
		end
		step = half
		amp = amp * 11 // 20
		amp = amp < 3 and 3 or amp
	end
	-- range scan, then normalize + carve the river canyon in one integer pass
	local hmin = 999999
	local hmax = -999999
	local nm = msz * msz
	for q = 1, nm do
		local hv0 = hmap[q]
		hmin = hv0 < hmin and hv0 or hmin
		hmax = hv0 > hmax and hv0 or hmax
	end
	local range = hmax - hmin
	range = range < 1 and 1 or range
	self.ph1 = rng:nextf() * 6.283185307
	self.ph2 = rng:nextf() * 6.283185307
	local ph1, ph2 = self.ph1, self.ph2
	local rivc = {}
	for ry = 0, msz - 1 do
		rivc[ry + 1] = itrunc(64.0 + 26.0 * sin(ry * 0.04908738521 + ph1) + 11.0 * sin(ry * 0.1472621556 + ph2))
	end
	local falltab = {}   -- smoothstep carve profile in 1/256ths, by distance from the river axis
	for fd = 0, 15 do
		local ff = fd / 15.0
		falltab[fd + 1] = itrunc(ff * ff * (3.0 - 2.0 * ff) * 256.0)
	end
	for cy = 0, msz - 1 do
		local rc = rivc[cy + 1]
		local rowc = cy * msz + 1
		for cx = 0, msz - 1 do
			local hraw = hmap[rowc + cx]
			local hv = (hraw - hmin) * 255 // range
			local dxr = cx - rc
			dxr = dxr > 64 and dxr - 128 or dxr
			dxr = dxr < -64 and dxr + 128 or dxr
			local ad = dxr < 0 and 0 - dxr or dxr
			if ad < 16 then
				local f256 = falltab[(ad > 15 and 15 or ad) + 1]
				hv = 50 + idiv((hv - 50) * f256, 256)
			end
			hv = hv < 0 and 0 or hv
			hv = hv > 255 and 255 or hv
			hmap[rowc + cx] = hv
		end
	end
	-- colormap, then the color index is PACKED into the height cell (hv*32 + ci)
	local cmap = make_grid(nm)
	self.cmap = cmap
	for my = 0, msz - 1 do
		local rowm = my * msz + 1
		local rown = ((my + 1) & mmask) * msz + 1
		local rc2 = rivc[my + 1]
		for mx = 0, msz - 1 do
			local hv2 = hmap[rowm + mx]
			local hn = hmap[rown + mx]
			local dxr2 = mx - rc2
			dxr2 = dxr2 > 64 and dxr2 - 128 or dxr2
			dxr2 = dxr2 < -64 and dxr2 + 128 or dxr2
			local ad2 = dxr2 < 0 and 0 - dxr2 or dxr2
			local band = 7
			if ad2 < 3 then band = 0
			elseif ad2 < 6 then band = 1
			elseif hv2 <= 80 then band = 2
			elseif hv2 <= 118 then band = 3
			elseif hv2 <= 165 then band = 4
			elseif hv2 <= 205 then band = 5
			elseif hv2 <= 235 then band = 6 end
			-- sun from the south-west: lit when the ground falls away toward it
			local he = hmap[rowm + ((mx + 1) & mmask)]
			local hs = hmap[((my - 1) & mmask) * msz + mx + 1]
			local hw = hmap[rowm + ((mx - 1) & mmask)]
			local grain = ((((mx * 73856093 + my * 19349663) & 1048575) * 1274126177) >> 12) & 3
			local lit = 6 + (hw - he) + (hs - hn) + grain - 1
			local shade = band < 2 and 1 + grain // 2 or (lit < 3 and 0 or (lit < 7 and 1 or (lit < 11 and 2 or 3)))
			cmap[rowm + mx] = band * 4 + shade
		end
	end
	for q2 = 1, nm do cmap[q2] = hmap[q2] * 32 + cmap[q2] end
	-- depth tables: geometric steps, projection scale and haze bank per step
	local z = 2.4
	local hsc = h * 0.085
	local nsteps = self.nsteps
	for i = 0, nsteps - 1 do
		self.zs[i + 1] = z
		self.invz[i + 1] = itrunc(hsc * 65536.0 / z)
		local zr = i / (nsteps - 1.0)
		local hzv = itrunc(zr * zr * (nhaze + 0.6))
		self.hazes[i + 1] = hzv > nhaze - 1 and nhaze - 1 or hzv
		z = z * 1.058
	end
	-- the sun: a glow ramp toward white, drawn only where sky shows through
	self.sunb = pal:gradient(6, {{255.0, 170.0, 110.0}, {255.0, 210.0, 150.0}, {255.0, 244.0, 214.0}})
	self.ybuf = make_grid(w)
	self.rolloff = make_grid(w)
end

function CanyonRun:update(t, dt)
	self.tt = t
	local ph1, ph2 = self.ph1, self.ph2
	local sp = 8.2
	local u = t * sp
	local posy = u
	local wv = 13.0 * sin(t * 0.5 + 0.8)
	local posx = 64.0 + 26.0 * sin(u * 0.04908738521 + ph1) + 11.0 * sin(u * 0.1472621556 + ph2) + wv
	self.posx, self.posy = posx, posy
	-- heading from a look-ahead point on the same path: the turns steer themselves
	local u2 = u + 7.5
	local wv2 = 13.0 * sin((t + 7.5 / sp) * 0.5 + 0.8)
	local lx = 64.0 + 26.0 * sin(u2 * 0.04908738521 + ph1) + 11.0 * sin(u2 * 0.1472621556 + ph2) + wv2
	local ddx = lx - posx
	local dl = sqrt(ddx * ddx + 56.25)
	local dirx = ddx / dl
	local diry = 7.5 / dl
	self.dirx, self.diry = dirx, diry
	self.planex = 0.0 - diry * 0.92
	self.planey = dirx * 0.92
	-- bank into the turn: roll target from the heading's turn rate
	if dt > 0.0001 then
		local cr = (self.pdx * diry - self.pdy * dirx) / dt
		local rt = cr * 1.05
		rt = rt > 0.55 and 0.55 or rt
		rt = rt < -0.55 and -0.55 or rt
		local mixr = dt * 3.2
		mixr = mixr > 1.0 and 1.0 or mixr
		self.roll = self.roll + (rt - self.roll) * mixr
	end
	self.pdx = dirx
	self.pdy = diry
	-- altitude: slow dive/climb wave, floored by terrain clearance ahead
	local hmap, mmask, msz = self.hmap, self.mmask, self.msz
	local g0 = hmap[((floor(posy) & mmask) * msz) + (floor(posx) & mmask) + 1]
	local g1 = hmap[((floor(posy + diry * 6.0) & mmask) * msz) + (floor(posx + dirx * 6.0) & mmask) + 1]
	local g2 = hmap[((floor(posy + diry * 14.0) & mmask) * msz) + (floor(posx + dirx * 14.0) & mmask) + 1]
	local gm = g0 > g1 and g0 or g1
	gm = g2 > gm and g2 or gm
	local base = 118.0 + 74.0 * sin(t * 0.31 + 4.4)
	local floorh = gm + 30.0
	local targ = floorh > base and floorh or base
	if dt > 0.0001 then
		local mixa = dt * 1.9
		mixa = mixa > 1.0 and 1.0 or mixa
		self.camhf = self.camhf + (targ - self.camhf) * mixa
	end
	local camhf = self.camhf
	self.camh = itrunc(camhf)
	-- pitch: nose down when high over the terrain ahead, plus a gentle bob
	local pit = (camhf - (gm + 70.0)) * 0.0020 + 0.06 * sin(t * 0.23)
	pit = pit > 0.20 and 0.20 or pit
	pit = pit < -0.14 and -0.14 or pit
	self.horizon = itrunc(self.h * (0.30 + pit))
end

function CanyonRun:render(grid)
	local ww = self.w
	local hh = self.h
	local mm = self.mmask
	local ms = self.msz
	local tb = self.tbase
	local cam = self.camh
	local horiz = self.horizon
	local ns = self.nsteps
	local nh = self.nhaze
	local dx_ = self.dirx
	local dy_ = self.diry
	local px_ = self.planex
	local py_ = self.planey
	local tm = self.cmap
	local yb = self.ybuf
	local ro = self.rolloff
	local zt = self.zs
	local izt = self.invz
	local hzt = self.hazes
	local w2 = ww // 2
	local rollfp = itrunc(self.roll * 65536.0)
	for x = 0, ww - 1 do
		yb[x + 1] = hh
		ro[x + 1] = horiz + idiv(rollfp * (x - w2), 65536)   -- horizon folded in
	end
	-- +4096 keeps the fixed-point coords positive so /65536 floors exactly
	local bigx = self.posx + 4096.0
	local bigy = self.posy + 4096.0
	for s = 1, ns do
		local z = zt[s]
		local iz = izt[s]
		local hz = hzt[s]
		local plx = bigx + (dx_ - px_) * z
		local ply = bigy + (dy_ - py_) * z
		local prx = bigx + (dx_ + px_) * z
		local pry = bigy + (dy_ + py_) * z
		local rxf = itrunc(plx * 65536.0)
		local ryf = itrunc(ply * 65536.0)
		local dxf = itrunc((prx - plx) * 65536.0 / ww)
		local dyf = itrunc((pry - ply) * 65536.0 / ww)
		for x2 = 1, ww do
			local ybv = yb[x2]
			if ybv > 0 then
				local mi = ((ryf >> 16) & mm) * ms + ((rxf >> 16) & mm) + 1   -- coords kept positive: shift == floor
				local tv = tm[mi]
				local sy = idiv((cam - (tv >> 5)) * iz, 65536) + ro[x2]
				if sy < ybv then
					sy = sy < 0 and 0 or sy
					local pi = tb + (tv & 31) * nh + hz
					local gi = sy * ww + x2
					for yy = sy, ybv - 1 do grid[gi] = pi; gi = gi + ww end
					yb[x2] = sy
				end
			end
			rxf = rxf + dxf
			ryf = ryf + dyf
		end
	end
	-- whatever the terrain never reached is sky (row-gradient entries 0..h-1)
	for x3 = 1, ww do
		local yb2 = yb[x3]
		local gi2 = x3
		for yy2 = 0, yb2 - 1 do grid[gi2] = yy2; gi2 = gi2 + ww end
	end
	-- the sun sits at a fixed bearing: its screen column follows the heading
	local sunb = self.sunb
	local rel = atan(dx_, dy_) - 0.55
	local sxf = ww * 0.5 - rel * ww * 0.55
	local syf = horiz - hh * 0.16
	local sr = hh * 0.07
	local sx0 = itrunc(sxf - sr * 2.2)
	local sx1 = itrunc(sxf + sr * 2.2)
	local sy0 = itrunc(syf - sr * 2.2)
	local sy1 = itrunc(syf + sr * 2.2)
	for sy = sy0, sy1 do
		if sy >= 0 and sy < hh then
			for sx = sx0, sx1 do
				if not (sx < 0 or sx >= ww or sy >= yb[sx + 1]) then
					local ddx = sx + 0.5 - sxf
					local ddy = sy + 0.5 - syf
					local d = sqrt(ddx * ddx + ddy * ddy) / sr
					if d < 1.0 then grid[sy * ww + sx + 1] = sunb + 5
					elseif d < 2.2 then
						local gl = itrunc((2.2 - d) * 4.2)
						if gl > 0 then grid[sy * ww + sx + 1] = sunb + (gl > 4 and 4 or gl - 1) end
					end
				end
			end
		end
	end
end
