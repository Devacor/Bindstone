-- BOIDS: a starling murmuration at sunset, and one hungry hawk. Each boid is an
-- object (the counterpoint to starfield's flat arrays), steering by the three
-- classic rules plus wander and fear, flying as silhouettes over a baked sky.

local sin, cos, sqrt = math.sin, math.cos, math.sqrt

Boid = class()

function Boid:init()
	self.x = 0.0
	self.y = 0.0
	self.vx = 0.0
	self.vy = 0.0
	self.px = 0.0
	self.py = 0.0
end

Boids = class(Scene)

function Boids:init()
	Scene.init(self)
	self.flock = {}
	self.trail = {}
	self.sky = {}
	self.nb = 80
	self.tones = 32
	self.hx = 10.0
	self.hy = 10.0
	self.hvx = 8.0
	self.hvy = 0.0
	self.tt = 0.0
end

function Boids:build()
	self.name = "BOIDS"
	self.handle = "murmuration . by the vm crew"
	local w, h, pal, rng = self.w, self.h, self.pal, self.rng
	self.trail = make_grid(w * h)
	self.sky = make_grid(w * h)
	local sky = self.sky
	local tones = self.tones
	local stops = {{10.0, 12.0, 40.0}, {48.0, 30.0, 90.0}, {150.0, 60.0, 120.0}, {240.0, 110.0, 90.0}, {255.0, 190.0, 110.0}, {255.0, 244.0, 200.0}}
	for t = 0, tones - 1 do
		local f = t * 5.0 / (tones - 1.0)
		local k = itrunc(f)
		k = k > 4 and 4 or k
		local u = f - k
		local a = stops[k + 1]
		local b = stops[k + 2]
		local ar, ag, ab = a[1], a[2], a[3]
		local cr = ar + (b[1] - ar) * u
		local cg = ag + (b[2] - ag) * u
		local cb = ab + (b[3] - ab) * u
		for d = 0, 7 do
			local k2 = 1.0 - d / 7.0 * 0.93
			pal:add(cr * k2 + 6.0 * (1.0 - k2), cg * k2 + 4.0 * (1.0 - k2), cb * k2 + 10.0 * (1.0 - k2))
		end
	end
	-- sky tones: height gradient + sun glow; the skyline is solid silhouette
	local sunx = w * 0.68
	local suny = h * 0.80
	local sr = h * 0.13
	for y = 0, h - 1 do
		for x = 0, w - 1 do
			local g = (y * 1.0 / h) * 9.0
			local dx = x - sunx
			local dy = (y - suny) * 1.3
			local d = sqrt(dx * dx + dy * dy)
			local glow = d < sr and 15.0 or 13.0 - (d - sr) * 0.22
			local tone = glow > g and glow or g
			local it = itrunc(tone * 2.0)
			sky[y * w + x + 1] = (it > 31 and 31 or it) * 8
		end
	end
	local hx0 = 0
	while hx0 < w do
		local bw = 4 + rng:next(9)
		local bh = h // 14 + rng:next(h // 7)
		local x2 = hx0
		while x2 < hx0 + bw and x2 < w do
			for y2 = h - bh, h - 1 do
				local lit = (x2 - hx0) % 3 == 1 and (y2 - (h - bh)) % 3 == 1 and rng:next(100) < 30
				sky[y2 * w + x2 + 1] = lit and 31 * 8 + 1 or 7
			end
			x2 = x2 + 1
		end
		hx0 = hx0 + bw + rng:next(3)
	end
	local flock = self.flock
	for i = 1, self.nb do
		local b2 = Boid.new()
		b2.x = w * 0.3 + rng:next(w * 4) * 0.1
		b2.y = h * 0.2 + rng:next(h * 4) * 0.1
		local a2 = rng:next(628) * 0.01
		b2.vx = cos(a2) * 12.0
		b2.vy = sin(a2) * 12.0
		b2.px = b2.x
		b2.py = b2.y
		flock[i] = b2
	end
end

function Boids:update(t, dt)
	self.tt = t
	if dt <= 0.0 then return end
	-- wander target keeps the flock touring the sky, above the rooftops
	local wx = self.w * (0.5 + 0.36 * sin(t * 0.37))
	local wy = self.h * (0.38 + 0.2 * sin(t * 0.53 + 1.7))
	self:hawk_update(dt)
	for i = 0, self.nb - 1 do
		self:steer_one(i, wx, wy, dt)
	end
end

function Boids:hawk_update(dt)
	local nb, flock, w, h = self.nb, self.flock, self.w, self.h
	local cx = 0.0
	local cy = 0.0
	for i = 1, nb do
		local b = flock[i]
		cx = cx + b.x
		cy = cy + b.y
	end
	cx = cx / nb
	cy = cy / nb
	local hx, hy, hvx, hvy = self.hx, self.hy, self.hvx, self.hvy
	hvx = hvx + (cx - hx) * 0.6 * dt
	hvy = hvy + (cy - hy) * 0.6 * dt
	local psp = sqrt(hvx * hvx + hvy * hvy)
	if psp > 15.0 then hvx = hvx * (15.0 / psp); hvy = hvy * (15.0 / psp) end
	hx = hx + hvx * dt
	hy = hy + hvy * dt
	if hx < 3.0 then hx = 3.0; hvx = 0.0 - hvx end
	if hx > w - 4.0 then hx = w - 4.0; hvx = 0.0 - hvx end
	if hy < 3.0 then hy = 3.0; hvy = 0.0 - hvy end
	if hy > h * 0.72 then hy = h * 0.72; hvy = 0.0 - hvy end
	self.hx, self.hy, self.hvx, self.hvy = hx, hy, hvx, hvy
end

-- classic three rules + wander + hawk fear, one boid per call
function Boids:steer_one(i, wx, wy, dt)
	local flock, nb, w, h = self.flock, self.nb, self.w, self.h
	local b = flock[i + 1]
	local bx = b.x
	local by = b.y
	local ax = 0.0
	local ay = 0.0
	local mx = 0.0
	local my = 0.0
	local vx_sum = 0.0
	local vy_sum = 0.0
	local near = 0
	for j = 0, nb - 1 do
		if j ~= i then
			local o = flock[j + 1]
			local dx = o.x - bx
			local dy = o.y - by
			local d2 = dx * dx + dy * dy
			if d2 <= 220.0 then
				near = near + 1
				mx = mx + o.x
				my = my + o.y
				vx_sum = vx_sum + o.vx
				vy_sum = vy_sum + o.vy
				if d2 < 10.0 and d2 > 0.0001 then
					ax = ax - dx / d2 * 26.0
					ay = ay - dy / d2 * 26.0
				end
			end
		end
	end
	if near > 0 then
		ax = ax + (mx / near - bx) * 1.1             -- cohesion
		ay = ay + (my / near - by) * 1.1
		ax = ax + (vx_sum / near - b.vx) * 1.7       -- alignment
		ay = ay + (vy_sum / near - b.vy) * 1.7
	end
	ax = ax + (wx - bx) * 0.45                       -- wander
	ay = ay + (wy - by) * 0.45
	if bx < 10.0 then ax = ax + (10.0 - bx) * 6.0 end   -- soft walls
	if bx > w - 10.0 then ax = ax - (bx - w + 10.0) * 6.0 end
	if by < 8.0 then ay = ay + (8.0 - by) * 6.0 end
	if by > h * 0.66 then ay = ay - (by - h * 0.66) * 6.0 end
	local fx = bx - self.hx                          -- flee the hawk
	local fy = by - self.hy
	local fd2 = fx * fx + fy * fy
	if fd2 < 260.0 and fd2 > 0.001 then
		ax = ax + fx / fd2 * 240.0
		ay = ay + fy / fd2 * 240.0
	end
	b.vx = b.vx + ax * dt
	b.vy = b.vy + ay * dt
	local sp = sqrt(b.vx * b.vx + b.vy * b.vy)
	if sp > 24.0 then b.vx = b.vx * (24.0 / sp); b.vy = b.vy * (24.0 / sp) end
	if sp < 7.0 and sp > 0.001 then b.vx = b.vx * (7.0 / sp); b.vy = b.vy * (7.0 / sp) end
	b.px = bx
	b.py = by
	b.x = b.x + b.vx * dt
	b.y = b.y + b.vy * dt
	if b.x < 0.0 then b.x = 0.0; b.vx = 0.0 - b.vx end
	if b.x > w - 1.0 then b.x = w - 1.0; b.vx = 0.0 - b.vx end
	if b.y < 0.0 then b.y = 0.0; b.vy = 0.0 - b.vy end
	if b.y > h * 0.82 then b.y = h * 0.82; b.vy = 0.0 - b.vy * 0.5 end
end

function Boids:mark(x, y, lv)
	local w = self.w
	if x < 0 or x >= w or y < 0 or y >= self.h then return end
	local i = y * w + x + 1
	local trail = self.trail
	if trail[i] < lv then trail[i] = lv end
end

function Boids:render(grid)
	local w, h = self.w, self.h
	local n = w * h
	local tr = self.trail
	local sk = self.sky
	local tt = self.tt
	local fade = (itrunc(tt * 30.0) & 1) == 0
	for i = 1, n do
		local tv = tr[i]
		if fade and tv > 0 then tv = tv - 1; tr[i] = tv end
		local s = sk[i]
		local dk = s % 8
		grid[i] = s + (tv > dk and tv - dk or 0)
	end
	-- wakes: each bird draws the segment it flew this frame into the trail
	local flock, nb = self.flock, self.nb
	for k = 1, nb do
		local b = flock[k]
		local ex = b.x
		local ey = b.y
		local sx = b.px
		local sy = b.py
		self:mark(itrunc(sx), itrunc(sy), 3)
		self:mark(itrunc((sx + ex) * 0.5), itrunc((sy + ey) * 0.5), 4)
		self:mark(itrunc(ex), itrunc(ey), 5)
	end
	-- the birds themselves, darkest, one pixel ahead along the heading
	for k2 = 1, nb do
		local b2 = flock[k2]
		local bx = itrunc(b2.x)
		local by = itrunc(b2.y)
		local sp = sqrt(b2.vx * b2.vx + b2.vy * b2.vy) + 0.001
		local nx = itrunc(b2.x + b2.vx / sp * 1.2)
		local ny = itrunc(b2.y + b2.vy / sp * 1.2)
		if bx >= 0 and bx < w and by >= 0 and by < h then
			local gi = by * w + bx + 1
			grid[gi] = sk[gi] - sk[gi] % 8 + 7
		end
		if nx >= 0 and nx < w and ny >= 0 and ny < h then
			local gi = ny * w + nx + 1
			grid[gi] = sk[gi] - sk[gi] % 8 + 6
		end
	end
	-- the hawk: a wide V with flapping wingtips, and a red eye
	local hxi = itrunc(self.hx)
	local hyi = itrunc(self.hy)
	local flap = (itrunc(tt * 7.0) % 4)
	local tip = flap == 0 and -2 or (flap == 2 and 1 or 0)
	for s2 = -4, 4 do
		local as = s2 < 0 and 0 - s2 or s2
		local yy = hyi + (as <= 1 and 0 or (as <= 3 and (tip < 0 and -1 or 0) or tip))
		local xx = hxi + s2
		if xx >= 0 and xx < w and yy >= 0 and yy < h then
			local gi = yy * w + xx + 1
			grid[gi] = sk[gi] - sk[gi] % 8 + 7
		end
	end
	if hyi + 1 < h and hxi >= 0 and hxi < w then
		local gb = (hyi + 1) * w + hxi + 1
		grid[gb] = sk[gb] - sk[gb] % 8 + 7
	end
end
