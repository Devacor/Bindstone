-- PIPEDREAM: the 3D Pipes screensaver homage. Thick round pipes as chains of lit
-- sphere impostors, glossy elbow balls, an occupancy grid, a camera that holds
-- still per generation, a persistent projected frame (only new voxels splat).

local sin, cos, sqrt = math.sin, math.cos, math.sqrt

Pipes = class(Scene)

function Pipes:init()
	Scene.init(self)
	self.cells = {}
	self.pgrid = {}
	self.pz = {}
	self.pending = {}
	self.eggs = {}
	self.hx = {}
	self.hy = {}
	self.hz = {}
	self.hd = {}
	self.DX = {1, -1, 0, 0, 0, 0}
	self.DY = {0, 0, 1, -1, 0, 0}
	self.DZ = {0, 0, 0, 0, 1, -1}
	self.gw = 16
	self.gh = 12
	self.gd = 16
	self.npipes = 4
	self.pipe_base = 0
	self.egg_base = 0
	self.filled = 0
	self.cap = 0
	self.phase = 0
	self.phase_t = 0.0
	self.last_wipe = 0
	self.step_acc = 0.0
	self.tt = 0.0
	self.gen_ang = 0.0
	self.gen_tilt = 0.34
	self.f_ca = 1.0
	self.f_sa = 0.0
	self.f_ct = 1.0
	self.f_st = 0.0
	self.flick = 0
end

function Pipes:build()
	self.name = "PIPEDREAM"
	self.handle = "screensaver seance . watch for the jackodile"
	local w, h, pal, rng = self.w, self.h, self.pal, self.rng
	self.cells = make_grid(self.gw * self.gh * self.gd)
	self.pgrid = make_grid(w * h)
	self.pz = make_fgrid(w * h)
	self.cap = self.gw * self.gh * self.gd * 34 // 100
	-- the office monitor at 2am: one entry per row
	for y = 0, h - 1 do
		local f = y / (h - 1.0)
		pal:add(3.0 + 6.0 * f, 4.0 + 5.0 * f, 12.0 + 14.0 * f)
	end
	local pgrid = self.pgrid
	for q = 0, w * h - 1 do pgrid[q + 1] = q // w end
	self.pipe_base = h
	local hues = {{70.0, 225.0, 195.0}, {255.0, 178.0, 58.0}, {232.0, 88.0, 212.0}, {128.0, 158.0, 255.0}}
	for p = 1, self.npipes do
		local hue = hues[p]
		self:shade_ramp(hue[1], hue[2], hue[3])
	end
	self.egg_base = pal:size()
	for e = 0, 5 do
		local c = gfx:rainbow_rgb(e * 1.0471975512)
		self:shade_ramp(c[1], c[2], c[3])
	end
	self:set_view(rng:next(628) * 0.01, 0.26 + rng:next(20) * 0.01)
	for p2 = 0, self.npipes - 1 do
		self.hx[p2 + 1] = 0
		self.hy[p2 + 1] = 0
		self.hz[p2 + 1] = 0
		self.hd[p2 + 1] = 0
		self:respawn(p2)
	end
end

-- 16 levels: shadow -> full color -> white-hot highlight
function Pipes:shade_ramp(r, g, b)
	local pal = self.pal
	for s = 0, 15 do
		local f = s / 15.0
		local k = f < 0.8 and 0.06 + f * 1.18 or 1.0
		local wh = f < 0.8 and 0.0 or (f - 0.8) * 4.0
		pal:add(r * k + (255.0 - r * k) * wh, g * k + (255.0 - g * k) * wh, b * k + (255.0 - b * k) * wh)
	end
end

function Pipes:set_view(ang, tilt)
	self.gen_ang = ang
	self.gen_tilt = tilt
	self.f_ca = cos(ang)
	self.f_sa = sin(ang)
	self.f_ct = cos(tilt)
	self.f_st = sin(tilt)
end

function Pipes:cell_idx(x, y, z)
	return (y * self.gd + z) * self.gw + x
end

function Pipes:is_free(x, y, z)
	if x < 0 or x >= self.gw or y < 0 or y >= self.gh or z < 0 or z >= self.gd then return false end
	return self.cells[self:cell_idx(x, y, z) + 1] == 0
end

function Pipes:advance(p, nx, ny, nz)
	self.hx[p + 1] = nx
	self.hy[p + 1] = ny
	self.hz[p + 1] = nz
	local ci = self:cell_idx(nx, ny, nz)
	self.cells[ci + 1] = 1 + p * 3
	self.pending[#self.pending + 1] = ci
	self.filled = self.filled + 1
end

function Pipes:respawn(p)
	local rng, cells = self.rng, self.cells
	for tr = 0, 29 do
		local x = rng:next(self.gw)
		local y = rng:next(self.gh)
		local z = rng:next(self.gd)
		local ci = self:cell_idx(x, y, z)
		if cells[ci + 1] == 0 then
			self.hx[p + 1] = x
			self.hy[p + 1] = y
			self.hz[p + 1] = z
			self.hd[p + 1] = rng:next(6)
			cells[ci + 1] = 1 + p * 3
			self.pending[#self.pending + 1] = ci
			self.filled = self.filled + 1
			return
		end
	end
end

function Pipes:step_one(p)
	local DX, DY, DZ, rng = self.DX, self.DY, self.DZ, self.rng
	local x = self.hx[p + 1]
	local y = self.hy[p + 1]
	local z = self.hz[p + 1]
	local d = self.hd[p + 1]
	local straight_ok = self:is_free(x + DX[d + 1], y + DY[d + 1], z + DZ[d + 1])
	if straight_ok and rng:next(100) < 72 then
		self:advance(p, x + DX[d + 1], y + DY[d + 1], z + DZ[d + 1])
		return
	end
	local nd = -1
	for tr = 0, 7 do
		local cand = rng:next(6)
		if not (cand == d or cand == (d ~ 1)) then
			if self:is_free(x + DX[cand + 1], y + DY[cand + 1], z + DZ[cand + 1]) then nd = cand; break end
		end
	end
	if nd < 0 and straight_ok then
		self:advance(p, x + DX[d + 1], y + DY[d + 1], z + DZ[d + 1])
		return
	end
	if nd < 0 then
		for cand2 = 0, 5 do
			if cand2 ~= (d ~ 1) then
				if self:is_free(x + DX[cand2 + 1], y + DY[cand2 + 1], z + DZ[cand2 + 1]) then nd = cand2; break end
			end
		end
	end
	if nd < 0 then
		self:respawn(p)   -- terminated: a fresh pipe takes over elsewhere
		return
	end
	-- a turn: the head cell becomes an elbow ball, or, rarely, the eye
	local here = self:cell_idx(x, y, z)
	if rng:chance(2) then
		self.cells[here + 1] = 3 + p * 3
		self.eggs[#self.eggs + 1] = here
	else
		self.cells[here + 1] = 2 + p * 3
	end
	self.pending[#self.pending + 1] = here
	self.hd[p + 1] = nd
	self:advance(p, x + DX[nd + 1], y + DY[nd + 1], z + DZ[nd + 1])
end

function Pipes:update(t, dt)
	self.tt = t
	if self.phase == 0 then
		self.step_acc = self.step_acc + dt
		while self.phase == 0 and self.step_acc >= 0.03 do
			self.step_acc = self.step_acc - 0.03
			for p = 0, self.npipes - 1 do self:step_one(p) end
			if self.filled >= self.cap then self.phase = 1; self.phase_t = 0.0; self.step_acc = 0.0 end
		end
	elseif self.phase == 1 then
		self.phase_t = self.phase_t + dt
		if self.phase_t > 1.8 then self.phase = 2; self.phase_t = 0.0 end
	else
		self.phase_t = self.phase_t + dt
		if self.phase_t > 1.5 then self:reset_volume() end
	end
end

function Pipes:reset_volume()
	local cells = self.cells
	for i = 1, #cells do cells[i] = 0 end
	local w = self.w
	local nn = w * self.h
	local pgrid, pz = self.pgrid, self.pz
	for q = 0, nn - 1 do pgrid[q + 1] = q // w; pz[q + 1] = 0.0 end
	self.pending = {}
	self.eggs = {}
	self.filled = 0
	self.phase = 0
	self.phase_t = 0.0
	self.last_wipe = 0
	self.step_acc = 0.0
	local rng = self.rng
	self:set_view(rng:next(628) * 0.01, 0.26 + rng:next(20) * 0.01)   -- the canon beat: a new angle
	for p = 0, self.npipes - 1 do self:respawn(p) end
end

-- one lit sphere impostor into the persistent frame
function Pipes:splat_at(fx, fy, fz, c)
	local w, h = self.w, self.h
	local f_ca, f_sa, f_ct, f_st = self.f_ca, self.f_sa, self.f_ct, self.f_st
	local rx = fx * f_ca + fz * f_sa
	local rz = fz * f_ca - fx * f_sa
	local ry = fy * f_ct - rz * f_st
	local zcam = fy * f_st + rz * f_ct + 30.0
	local ooz = 1.0 / zcam
	local kind = (c - 1) % 3
	local p = (c - 1) // 3
	local scale = (w < h * 1.3 and w or h * 1.3) * 1.25 * ooz
	local rad = scale * (kind == 0 and 0.40 or 0.58)
	rad = rad < 0.7 and 0.7 or rad
	local sx = w * 0.5 + rx * scale
	local sy = h * 0.52 - ry * scale
	local fog = 1.25 - zcam * 0.018
	fog = fog > 1.0 and 1.0 or (fog < 0.35 and 0.35 or fog)
	local base = kind == 2 and self.egg_base + ((self.flick + p + itrunc(fx * 3.0 + fz + 40.0)) % 6) * 16 or self.pipe_base + p * 16
	local x0 = itrunc(sx - rad)
	local x1 = itrunc(sx + rad) + 1
	local y0 = itrunc(sy - rad)
	local y1 = itrunc(sy + rad) + 1
	local ir = 1.0 / rad
	local r2 = rad * rad
	local dz_gain = ooz * 0.02
	local pg = self.pgrid
	local zz = self.pz
	for yy = y0, y1 do
		if yy >= 0 and yy < h then
			local dy = yy + 0.5 - sy
			local rowb = yy * w + 1
			for xx = x0, x1 do
				if xx >= 0 and xx < w then
					local dx = xx + 0.5 - sx
					local q = r2 - dx * dx - dy * dy
					if q >= 0.0 then
						local nz = sqrt(q) * ir
						local depth = ooz + nz * dz_gain
						if depth >= zz[rowb + xx] then
							local nx = dx * ir
							local ny = dy * ir
							local d = nx * -0.48 + ny * -0.56 + nz * 0.67
							d = d < 0.0 and 0.0 or d
							local s = nx * -0.25 + ny * -0.29 + nz * 0.92
							s = s < 0.0 and 0.0 or s
							local s2 = s * s
							local s4 = s2 * s2
							local s8 = s4 * s4
							local lit = (0.10 + d * 0.78) * fog + s8 * s8 * (kind == 0 and 0.5 or 0.8)
							local lv = itrunc(lit * 15.99)
							zz[rowb + xx] = depth
							pg[rowb + xx] = base + (lv > 15 and 15 or lv)
						end
					end
				end
			end
		end
	end
end

-- splat one voxel plus midpoints toward every same-pipe neighbor
function Pipes:splat_voxel(vidx)
	local cells = self.cells
	local c = cells[vidx + 1]
	if c == 0 then return end
	local gw, gh, gd = self.gw, self.gh, self.gd
	local ix = vidx % gw
	local rest = vidx // gw
	local iz = rest % gd
	local iy = rest // gd
	local fx = ix - (gw - 1) * 0.5
	local fy = iy - (gh - 1) * 0.5
	local fz = iz - (gd - 1) * 0.5
	self:splat_at(fx, fy, fz, c)
	local pp = (c - 1) // 3
	local seg = 1 + pp * 3
	if ix > 0 then
		local n0 = cells[vidx - 1 + 1]
		if n0 ~= 0 and (n0 - 1) // 3 == pp then self:splat_at(fx - 0.25, fy, fz, seg); self:splat_at(fx - 0.5, fy, fz, seg) end
	end
	if ix < gw - 1 then
		local n1 = cells[vidx + 1 + 1]
		if n1 ~= 0 and (n1 - 1) // 3 == pp then self:splat_at(fx + 0.25, fy, fz, seg); self:splat_at(fx + 0.5, fy, fz, seg) end
	end
	if iz > 0 then
		local n2 = cells[vidx - gw + 1]
		if n2 ~= 0 and (n2 - 1) // 3 == pp then self:splat_at(fx, fy, fz - 0.25, seg); self:splat_at(fx, fy, fz - 0.5, seg) end
	end
	if iz < gd - 1 then
		local n3 = cells[vidx + gw + 1]
		if n3 ~= 0 and (n3 - 1) // 3 == pp then self:splat_at(fx, fy, fz + 0.25, seg); self:splat_at(fx, fy, fz + 0.5, seg) end
	end
	if iy > 0 then
		local n4 = cells[vidx - gw * gd + 1]
		if n4 ~= 0 and (n4 - 1) // 3 == pp then self:splat_at(fx, fy - 0.25, fz, seg); self:splat_at(fx, fy - 0.5, fz, seg) end
	end
	if iy < gh - 1 then
		local n5 = cells[vidx + gw * gd + 1]
		if n5 ~= 0 and (n5 - 1) // 3 == pp then self:splat_at(fx, fy + 0.25, fz, seg); self:splat_at(fx, fy + 0.5, fz, seg) end
	end
end

function Pipes:render(grid)
	local w, h = self.w, self.h
	local nn = w * h
	local pgrid, pz = self.pgrid, self.pz
	if self.phase == 2 then
		-- generation over: the monitor wipes to black, top down
		local target = itrunc(self.phase_t * h / 1.3)
		target = target > h and h or target
		local stop = target * w
		for q = self.last_wipe * w, stop - 1 do pgrid[q + 1] = q // w; pz[q + 1] = 0.0 end
		self.last_wipe = target
	else
		self.flick = itrunc(self.tt * 8.0)
		local pending = self.pending
		for i = 1, #pending do self:splat_voxel(pending[i]) end
		self.pending = {}
		local eggs = self.eggs
		for i = 1, #eggs do self:splat_voxel(eggs[i]) end   -- the eyes shimmer every frame
	end
	for i = 1, nn do grid[i] = pgrid[i] end
end
