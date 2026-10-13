-- BANNERFALL: Verlet cloth: a 26x11 point grid (structural + shear constraints)
-- pinned along a pole, a rainbow weave bearing the JAI sigil, blowing in an
-- animated wind field, tearing, then shredding into embers. Constraint topology
-- lives in index arithmetic; only the intact/torn bit is stored.

local sin = math.sin

BannerFall = class(Scene)

function BannerFall:init()
	Scene.init(self)
	self.cw = 26
	self.chh = 11
	self.pxs = {}
	self.pys = {}
	self.pzs = {}
	self.opx = {}
	self.opy = {}
	self.opz = {}
	self.jit = {}
	self.con = {}
	self.shred = {}
	self.colf = {}
	self.colz = {}
	self.rowf = {}
	self.sxs = {}
	self.sys = {}
	self.qmask = {}
	self.hues = {}
	self.nh = 0
	self.nv = 0
	self.nsh = 0
	self.nc = 0
	self.npts = 0
	self.seamn = 0
	self.free_t = -1.0
	self.next_cut = 9.6
	self.shred_t = -1.0
	self.shred_at = 0
	self.polex = 14
	self.poley = 14
	self.kx = 1.65
	self.ky = 3.0
	self.polebase = 0
	self.wbase = 0
	self.gbase = 0
	self.flashp = 0
	self.tearpt = -1
	self.tear_t = 0.0
	self.tt = 0.0
end

function BannerFall:build()
	self.name = "BANNERFALL"
	self.handle = "verlet gale . every thread gives eventually"
	local w, h, pal, rng = self.w, self.h, self.pal, self.rng
	local cw, chh = self.cw, self.chh
	self.npts = cw * chh
	local n = 0
	for j = 0, chh - 1 do
		for i = 0, cw - 1 do
			n = n + 1
			self.pxs[n] = i * 1.0
			self.pys[n] = j * 1.0
			self.pzs[n] = 0.0
			self.opx[n] = i * 1.0
			self.opy[n] = j * 1.0
			self.opz[n] = 0.0
			self.jit[n] = 0.85 + rng:nextf() * 0.3
		end
	end
	-- constraint ids: horizontal, then vertical, then the two shear diagonals
	self.nh = chh * (cw - 1)
	self.nv = (chh - 1) * cw
	self.nsh = (chh - 1) * (cw - 1)
	local nc = self.nh + self.nv + self.nsh + self.nsh
	self.nc = nc
	local con, shred = self.con, self.shred
	for c = 1, nc do
		con[c] = 1
		shred[c] = c - 1
	end
	for c2 = nc - 1, 1, -1 do   -- Fisher-Yates via the host rng
		local k = rng:next(c2 + 1)
		local tmp = shred[c2 + 1]
		shred[c2 + 1] = shred[k + 1]
		shred[k + 1] = tmp
	end
	self.seamn = chh
	for wc = 1, cw do self.colf[wc] = 0.0; self.colz[wc] = 0.0 end
	for wr = 1, chh do self.rowf[wr] = 0.0 end
	self.sxs = make_fgrid(self.npts)
	self.sys = make_fgrid(self.npts)
	self.kx = w * 0.024
	self.ky = h * 0.034
	self.polex = itrunc(w * 0.12)
	self.poley = itrunc(h * 0.17)
	-- palette: bg gradient rows 0..h-1 (dusk), pole, 12 weave hues x 10 light
	-- levels, gold sigil ramp, tear flash
	for py = 0, h - 1 do
		local f = py / (h - 1.0)
		local f2 = f * f
		pal:add(14.0 + 96.0 * f2 + 24.0 * f, 12.0 + 52.0 * f2 + 10.0 * f, 34.0 + 66.0 * f)
	end
	self.polebase = h
	pal:add(70, 56, 42)
	pal:add(122, 102, 76)
	pal:add(168, 144, 108)
	pal:add(255, 214, 110)
	pal:add(255, 250, 210)
	self.wbase = self.polebase + 5
	for hu = 0, 11 do
		local c3 = gfx:rainbow_rgb(hu * 0.5235987756)
		local hr, hg, hb = c3[1], c3[2], c3[3]
		for lv = 0, 9 do
			local k2 = 0.14 + 0.86 * lv / 9.0
			pal:add(hr * k2, hg * k2, hb * k2)
		end
	end
	self.gbase = self.wbase + 120
	for lv2 = 0, 9 do
		local k3 = 0.2 + 0.8 * lv2 / 9.0
		local wh = lv2 >= 8 and (lv2 - 7) * 0.25 or 0.0
		pal:add(255.0 * k3 + (255.0 - 255.0 * k3) * wh, 218.0 * k3 + (255.0 - 218.0 * k3) * wh, 96.0 * k3 + (255.0 - 96.0 * k3) * wh)
	end
	self.flashp = self.gbase + 10
	pal:add(255, 255, 255)
	for qc = 0, cw - 2 do self.hues[qc + 1] = qc * 12 // (cw - 1) end
	-- the JAI sigil, woven per quad (5x7 letters, single-column gaps)
	self.qmask = make_grid((cw - 1) * (chh - 1))
	local glyphs = {
		{"#####", "...#.", "...#.", "...#.", "#..#.", "#..#.", ".##.."},
		{".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
		{"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "#####"}}
	local ox0 = (cw - 1 - 17) // 2
	local oy0 = (chh - 1 - 7) // 2
	for L = 0, 2 do
		local lrows = glyphs[L + 1]
		for r = 0, 6 do
			local rs = lrows[r + 1]
			for cc = 0, 4 do
				if rs:sub(cc + 1, cc + 1) == "#" then
					self.qmask[(oy0 + r) * (cw - 1) + ox0 + L * 6 + cc + 1] = 1
				end
			end
		end
	end
end

function BannerFall:update(t, dt)
	self.tt = t
	self.tear_t = self.tear_t - dt
	if dt < 0.0001 then return end
	local sdt = dt > 0.05 and 0.05 or dt
	local dt2 = sdt * sdt
	-- wind program: steady breeze, then the gale ramps in with layered gusts
	local A = 9.0 + 2.4 * sin(t * 0.7)
	if t > 5.0 then
		local g = (t - 5.0) * 0.4
		g = g > 1.0 and 1.0 or g
		A = A + g * (14.0 + 6.0 * sin(t * 1.9) + 3.0 * sin(t * 3.7))
	end
	if self.free_t >= 0.0 then   -- the gust dies the moment the banner parts
		local dk = (t - self.free_t) * 0.55
		dk = dk > 0.88 and 0.88 or dk
		A = A * (1.0 - dk)
	end
	local cww = self.cw
	local chn = self.chh
	local px = self.pxs
	local py = self.pys
	local pz = self.pzs
	local qx = self.opx
	local qy = self.opy
	local qz = self.opz
	local cn = self.con
	local jt = self.jit
	local cf_ = self.colf
	local cz_ = self.colz
	local rf_ = self.rowf
	-- traveling waves, sampled per column/row, never per point
	for i = 0, cww - 1 do
		cf_[i + 1] = 1.0 + 0.35 * sin(t * 4.2 - i * 0.42)
		cz_[i + 1] = sin(t * 5.1 - i * 0.55) + 0.5 * sin(t * 2.3 - i * 0.21 + 1.7)
	end
	for j = 0, chn - 1 do rf_[j + 1] = 0.82 + 0.36 * sin(t * 1.6 + j * 0.5) end
	-- gravity while flown; once the banner parts, the gust's updraft carries the scraps
	local grav = self.free_t >= 0.0 and 2.6 or 9.5
	-- verlet integration
	local p = 1
	for j2 = 1, chn do
		local rf = rf_[j2]
		for i2 = 1, cww do
			local cf = cf_[i2]
			local jv = jt[p]
			local fxv = A * cf * rf * jv
			local fyv = grav - A * 0.16 * cf   -- gravity minus wind lift
			local fzv = A * 0.42 * cz_[i2] * rf * jv
			local cx = px[p]
			local cy = py[p]
			local cz = pz[p]
			local lox = qx[p]
			local loy = qy[p]
			local loz = qz[p]
			qx[p] = cx
			qy[p] = cy
			qz[p] = cz
			px[p] = cx + (cx - lox) * 0.986 + fxv * dt2
			py[p] = cy + (cy - loy) * 0.986 + fyv * dt2
			pz[p] = cz + (cz - loz) * 0.984 + fzv * dt2
			p = p + 1
		end
	end
	-- hidden assist: from 9.6s the topmost intact seam thread is cut every 0.3s
	if t > self.next_cut and self.seamn > 0 then
		self.next_cut = t + 0.3
		for js = 0, chn - 1 do
			local sid = js * (cww - 1) + 1
			if cn[sid] == 1 then
				cn[sid] = 0
				self.tearpt = js * cww
				self.tear_t = 0.14
				self.seamn = self.seamn - 1
				break
			end
		end
	end
	if self.seamn <= 0 and self.free_t < 0.0 then self.free_t = t end
	-- one breath after the banner parts, it bursts into embers
	local nc = self.nc
	if self.free_t >= 0.0 and t > self.free_t + 0.45 and self.shred_at < nc then
		if self.shred_t < 0.0 then self.shred_t = t end
		local lim = self.shred_at + 24
		lim = lim > nc and nc or lim
		local shred = self.shred
		while self.shred_at < lim do
			local cxx = shred[self.shred_at + 1]
			cn[cxx + 1] = 0
			self.shred_at = self.shred_at + 1
		end
	end
	-- relaxation: Jakobsen sqrt-free correction; overstretch breaks for real
	local brk = t > 8.0   -- the gale is up: threads may fail for real now
	local tearpt, tear_t, seamn = self.tearpt, self.tear_t, self.seamn
	for it = 0, 1 do
		-- horizontal: a = j*cw+i <-> a+1, rest2 = 1; i==0 is the weak seam
		local cid = 1
		for jh = 0, chn - 1 do
			local a = jh * cww + 1
			for ih = 0, cww - 2 do
				if cn[cid] == 1 then
					local ddx = px[a + 1] - px[a]
					local ddy = py[a + 1] - py[a]
					local ddz = pz[a + 1] - pz[a]
					local d2 = ddx * ddx + ddy * ddy + ddz * ddz
					-- the hoist seam (i==0) is woven weakest at the TOP: it peels downward
					if brk and d2 > (ih == 0 and 3.0 + jh * 0.4 or 4.6) then
						cn[cid] = 0
						tearpt = a - 1
						tear_t = 0.14
						if ih == 0 then seamn = seamn - 1 end
					else
						local k = 1.0 / (d2 + 1.0) - 0.5
						px[a] = px[a] - ddx * k
						py[a] = py[a] - ddy * k
						pz[a] = pz[a] - ddz * k
						px[a + 1] = px[a + 1] + ddx * k
						py[a + 1] = py[a + 1] + ddy * k
						pz[a + 1] = pz[a + 1] + ddz * k
					end
				end
				cid = cid + 1
				a = a + 1
			end
		end
		-- vertical: a = j*cw+i <-> a+cw, rest2 = 1; i==0 weak seam
		for jv = 0, chn - 2 do
			local a2 = jv * cww + 1
			for iv = 0, cww - 1 do
				if cn[cid] == 1 then
					local dvx = px[a2 + cww] - px[a2]
					local dvy = py[a2 + cww] - py[a2]
					local dvz = pz[a2 + cww] - pz[a2]
					local e2 = dvx * dvx + dvy * dvy + dvz * dvz
					if brk and iv > 0 and e2 > 4.6 then   -- i==0 is the hoist itself: never breaks
						cn[cid] = 0
						tearpt = a2 - 1
						tear_t = 0.14
					else
						local k2 = 1.0 / (e2 + 1.0) - 0.5
						px[a2] = px[a2] - dvx * k2
						py[a2] = py[a2] - dvy * k2
						pz[a2] = pz[a2] - dvz * k2
						px[a2 + cww] = px[a2 + cww] + dvx * k2
						py[a2 + cww] = py[a2 + cww] + dvy * k2
						pz[a2 + cww] = pz[a2 + cww] + dvz * k2
					end
				end
				cid = cid + 1
				a2 = a2 + 1
			end
		end
		-- shear, first pass only
		if it == 0 then
			-- shear A: a <-> a+cw+1, rest2 = 2
			for ja = 0, chn - 2 do
				local a3 = ja * cww + 1
				for ia = 0, cww - 2 do
					if cn[cid] == 1 then
						local dax = px[a3 + cww + 1] - px[a3]
						local day = py[a3 + cww + 1] - py[a3]
						local daz = pz[a3 + cww + 1] - pz[a3]
						local f2 = dax * dax + day * day + daz * daz
						if brk and f2 > 9.2 then
							cn[cid] = 0
							tearpt = a3 - 1
							tear_t = 0.14
						else
							local k3 = 2.0 / (f2 + 2.0) - 0.5
							px[a3] = px[a3] - dax * k3
							py[a3] = py[a3] - day * k3
							pz[a3] = pz[a3] - daz * k3
							px[a3 + cww + 1] = px[a3 + cww + 1] + dax * k3
							py[a3 + cww + 1] = py[a3 + cww + 1] + day * k3
							pz[a3 + cww + 1] = pz[a3 + cww + 1] + daz * k3
						end
					end
					cid = cid + 1
					a3 = a3 + 1
				end
			end
			-- shear B: a+1 <-> a+cw, rest2 = 2
			for jb = 0, chn - 2 do
				local a4 = jb * cww + 1
				for ib = 0, cww - 2 do
					if cn[cid] == 1 then
						local dbx = px[a4 + cww] - px[a4 + 1]
						local dby = py[a4 + cww] - py[a4 + 1]
						local dbz = pz[a4 + cww] - pz[a4 + 1]
						local g2 = dbx * dbx + dby * dby + dbz * dbz
						if brk and g2 > 9.2 then
							cn[cid] = 0
							tearpt = a4
							tear_t = 0.14
						else
							local k4 = 2.0 / (g2 + 2.0) - 0.5
							px[a4 + 1] = px[a4 + 1] - dbx * k4
							py[a4 + 1] = py[a4 + 1] - dby * k4
							pz[a4 + 1] = pz[a4 + 1] - dbz * k4
							px[a4 + cww] = px[a4 + cww] + dbx * k4
							py[a4 + cww] = py[a4 + cww] + dby * k4
							pz[a4 + cww] = pz[a4 + cww] + dbz * k4
						end
					end
					cid = cid + 1
					a4 = a4 + 1
				end
			end
		end
		-- the hoist column is pinned to the pole for good
		for pj = 0, chn - 1 do
			local pi = pj * cww + 1
			px[pi] = 0.0
			py[pi] = pj * 1.0
			pz[pi] = 0.0
		end
	end
	self.tearpt, self.tear_t, self.seamn = tearpt, tear_t, seamn
end

-- fill one triangle by edge functions at pixel centers (either winding)
function BannerFall:tri(grid, ax, ay, bx, by, cx, cy, pi)
	local ww = self.w
	local h = self.h
	local mnx = ax < bx and ax or bx
	mnx = cx < mnx and cx or mnx
	local mxx = ax > bx and ax or bx
	mxx = cx > mxx and cx or mxx
	local mny = ay < by and ay or by
	mny = cy < mny and cy or mny
	local mxy = ay > by and ay or by
	mxy = cy > mxy and cy or mxy
	local x0 = itrunc(mnx)
	local x1 = itrunc(mxx)
	local y0 = itrunc(mny)
	local y1 = itrunc(mxy)
	x0 = x0 < 0 and 0 or x0
	y0 = y0 < 1 and 1 or y0
	x1 = x1 > ww - 1 and ww - 1 or x1
	y1 = y1 > h - 1 and h - 1 or y1
	local area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
	if area < 0.0001 and area > -0.0001 then return end
	local sg = area > 0.0 and 1.0 or -1.0
	for y = y0, y1 do
		local py = y + 0.5
		local rb = y * ww + 1
		for x = x0, x1 do
			local px = x + 0.5
			local e0 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) * sg
			local e1 = ((cx - bx) * (py - by) - (cy - by) * (px - bx)) * sg
			local e2 = ((ax - cx) * (py - cy) - (ay - cy) * (px - cx)) * sg
			if e0 >= 0.0 and e1 >= 0.0 and e2 >= 0.0 then grid[rb + x] = pi end
		end
	end
end

function BannerFall:render(grid)
	local ww = self.w
	local hh = self.h
	local cww = self.cw
	local chh = self.chh
	local px = self.pxs
	local py = self.pys
	local pz = self.pzs
	local sx_ = self.sxs
	local sy_ = self.sys
	local qm = self.qmask
	local hu = self.hues
	-- dusk gradient backdrop (row entries 0..h-1)
	local gi = 1
	for y = 0, hh - 1 do
		for x = 1, ww do grid[gi] = y; gi = gi + 1 end
	end
	-- the pole: a shaded round staff, a gold finial on top
	local polex, poley, polebase = self.polex, self.poley, self.polebase
	local pbot = poley + itrunc(chh * self.ky) + 10
	pbot = pbot > hh - 1 and hh - 1 or pbot
	for pr = poley - 2, pbot do
		grid[pr * ww + polex - 1 + 1] = polebase
		grid[pr * ww + polex + 1] = polebase + 2
		grid[pr * ww + polex + 1 + 1] = polebase + 1
	end
	for fy = -2, 0 do
		for fx = -1, 1 do
			grid[(poley - 4 + fy) * ww + polex + fx + 1] = (fx == -1 and fy == -2) and polebase + 4 or polebase + 3
		end
	end
	-- project every point once (skewed ortho: z folds read as depth)
	local np = self.npts
	local px0 = polex + 1.5
	local py0 = poley + 0.5
	local kxx = self.kx
	local kyy = self.ky
	for p2 = 1, np do
		local X = px[p2]
		local Y = py[p2]
		local Z = pz[p2]
		sx_[p2] = px0 + X * kxx + Z * kxx * 0.17
		sy_[p2] = py0 + Y * kyy - Z * kyy * 0.12
	end
	-- quads as two triangles, lit by the edge cross product; a quad with any TORN
	-- bounding constraint is skipped, so tears read as holes the moment they happen
	local cnr = self.con
	local qw = cww - 1
	local vbase = self.nh
	local maxw = kxx * 4.0
	local maxh = kyy * 3.0
	local gbase, wbase = self.gbase, self.wbase
	for j = 0, chh - 2 do
		for i = 0, qw - 1 do
			local a = j * cww + i + 1
			local ht = cnr[j * qw + i + 1]
			local hb = cnr[(j + 1) * qw + i + 1]
			local vl = cnr[vbase + j * cww + i + 1]
			local vr = cnr[vbase + j * cww + i + 2]
			if ht + hb + vl + vr >= 4 then
				local x00 = sx_[a]
				local x01 = sx_[a + 1]
				local x10 = sx_[a + cww]
				local x11 = sx_[a + cww + 1]
				local y00 = sy_[a]
				local y01 = sy_[a + 1]
				local y10 = sy_[a + cww]
				local y11 = sy_[a + cww + 1]
				local dxq = x11 - x00
				local dyq = y11 - y00
				local dxr = x01 - x10
				local dyr = y01 - y10
				if not (dxq > maxw or dxq < 0.0 - maxw or dyq > maxh or dyq < 0.0 - maxh)
					and not (dxr > maxw or dxr < 0.0 - maxw or dyr > maxh or dyr < 0.0 - maxh) then
					local e1x = px[a + 1] - px[a]
					local e1y = py[a + 1] - py[a]
					local e1z = pz[a + 1] - pz[a]
					local e2x = px[a + cww] - px[a]
					local e2y = py[a + cww] - py[a]
					local e2z = pz[a + cww] - pz[a]
					local nx = e1y * e2z - e1z * e2y
					local ny = e1z * e2x - e1x * e2z
					local nz = e1x * e2y - e1y * e2x
					local ndl = nx * 0.31 - ny * 0.25 + nz * 0.92
					local anx = nx < 0.0 and -nx or nx
					local any = ny < 0.0 and -ny or ny
					local anz = nz < 0.0 and -nz or nz
					local mmx = anx > any and anx or any
					mmx = anz > mmx and anz or mmx
					local b = ndl / ((anx + any + anz) * 0.62 + mmx * 0.36 + 0.0001)
					b = b < 0.0 and -b or b   -- the weave is two-sided
					local li = 0.12 + 0.88 * b
					li = li > 0.999 and 0.999 or li
					local lvl = itrunc(li * 10.0)
					local m = qm[j * qw + i + 1]
					local pi2 = m == 1 and gbase + lvl or wbase + hu[i + 1] * 10 + lvl
					self:tri(grid, x00, y00, x01, y01, x11, y11, pi2)
					self:tri(grid, x00, y00, x11, y11, x10, y10, pi2)
				end
			end
		end
	end
	-- once shredding starts the freed points fly as fading embers
	if self.shred_at > 0 then
		local tt = self.tt
		local fl2 = itrunc(tt * 9.0)
		local fade = itrunc((tt - self.shred_t) * 0.9)
		local wb = wbase
		for p3 = 0, np - 1 do
			local sx2 = itrunc(sx_[p3 + 1])
			local sy3 = itrunc(sy_[p3 + 1])
			if not (sx2 < 0 or sx2 >= ww - 1 or sy3 < 1 or sy3 >= hh - 1) then
				local lv3 = 6 + ((p3 * 2971 + fl2 * 40503) & 3) - fade
				lv3 = lv3 < 0 and 0 or lv3
				lv3 = lv3 > 9 and 9 or lv3
				local ii = p3 % cww
				if ii ~= 0 then   -- the hoist column stays on the pole
					local hu2 = ii >= qw and qw - 1 or ii
					local ec = wb + hu[hu2 + 1] * 10 + lv3
					grid[sy3 * ww + sx2 + 1] = ec
					if lv3 > 4 then grid[sy3 * ww + sx2 + 2] = ec end
				end
			end
		end
	end
	-- a thread snapped: a white flash burst at the tear
	local tearpt = self.tearpt
	if self.tear_t > 0.0 and tearpt >= 0 then
		local flashp = self.flashp
		local tx = itrunc(sx_[tearpt + 1])
		local ty = itrunc(sy_[tearpt + 1])
		for q = -1, 1 do
			if tx + q >= 0 and tx + q < ww and ty >= 1 and ty < hh then grid[ty * ww + tx + q + 1] = flashp end
			if tx >= 0 and tx < ww and ty + q >= 1 and ty + q < hh then grid[(ty + q) * ww + tx + 1] = flashp end
		end
	end
end
