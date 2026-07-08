-- The reference's PURE parallel bodies, as plain serial Lua (PORTING.md:
-- ports always run the serial-equivalent loops; outputs are defined
-- identical). The DDA math is a line-for-line transliteration so the float
-- stream is bit-exact; the renderer inlines its own copy of the same walk.

local floor = math.floor

-- One wall ray over the render-kind grid (the serial helper for hitscan and
-- tracer garnish). Returns pdist, side, kind, texcol (0..63).
function gloom_ray(posx, posy, rdx, rdy)
	local g = G
	local mw = g.mw
	local mk = g.mapkind
	local mx = floor(posx)                 -- coords stay positive in-map
	local my = floor(posy)
	local adx = rdx < 0.0 and -rdx or rdx
	local ady = rdy < 0.0 and -rdy or rdy
	local ddx = adx < 0.00000001 and 100000000.0 or 1.0 / adx
	local ddy = ady < 0.00000001 and 100000000.0 or 1.0 / ady
	local stepx, sdx
	if rdx < 0.0 then stepx = -1; sdx = (posx - mx) * ddx
	else stepx = 1; sdx = (mx + 1.0 - posx) * ddx end
	local stepy, sdy
	if rdy < 0.0 then stepy = -1; sdy = (posy - my) * ddy
	else stepy = 1; sdy = (my + 1.0 - posy) * ddy end
	local side = 0
	local kind = 1
	local guard = 0
	local hit = false
	while not hit and guard < 128 do
		if sdx < sdy then sdx = sdx + ddx; mx = mx + stepx; side = 0
		else sdy = sdy + ddy; my = my + stepy; side = 1 end
		local c = mk[my * mw + mx + 1]
		if c ~= 0 then hit = true; kind = c end
		guard = guard + 1
	end
	local pdist = side == 0 and sdx - ddx or sdy - ddy
	if pdist < 0.02 then pdist = 0.02 end
	local wallx
	if side == 0 then wallx = posy + pdist * rdy else wallx = posx + pdist * rdx end
	wallx = wallx - floor(wallx)
	local texcol = floor(wallx * 64.0)
	return pdist, side, kind, texcol
end

-- One particle step at the fixed timestep; mutates the slot record in place
-- (the reference rebuilds value records through parallel_transform — same
-- numbers, no allocation). Slot: {kind, x, y, z, vx, vy, vz, life}
function gloom_particle(p)
	local kind = p[1]
	if kind == 0 then return end
	local x, y, z = p[2], p[3], p[4]
	local vx, vy, vz = p[5], p[6], p[7]
	local life = p[8] - 1
	if life <= 0 then
		p[1] = 0; p[2] = 0.0; p[3] = 0.0; p[4] = 0.0
		p[5] = 0.0; p[6] = 0.0; p[7] = 0.0; p[8] = 0
		return
	end
	if kind == 1 then                      -- spark: fast, hard gravity
		vz = vz - 0.30
		vx = vx * 0.90
		vy = vy * 0.90
	elseif kind == 2 then                  -- blood: droops
		vz = vz - 0.22
		vx = vx * 0.94
		vy = vy * 0.94
	elseif kind == 3 then                  -- gib: ballistic, bounces once-ish
		vz = vz - 0.16
	elseif kind == 4 then                  -- smoke: rises, drifts
		vz = vz * 0.90 + 0.012
		vx = vx * 0.96
		vy = vy * 0.96
	elseif kind == 5 then                  -- muzzle/impact flash: hangs still
		vx = vx * 0.5
		vy = vy * 0.5
	else                                   -- hexmote: swirly float
		vz = vz * 0.98 + 0.004
		vx = vx * 0.97
		vy = vy * 0.97
	end
	x = x + vx * 0.033333333
	y = y + vy * 0.033333333
	z = z + vz * 0.033333333
	if z < 0.02 then
		z = 0.02
		if kind == 3 and (vz < -0.5 or vz > 0.5) then vz = 0.0 - vz * 0.45
		else vz = 0.0 end
		if kind == 1 or kind == 2 then
			if life >= 4 then life = 4 end
		end
	end
	p[2] = x; p[3] = y; p[4] = z
	p[5] = vx; p[6] = vy; p[7] = vz; p[8] = life
end
