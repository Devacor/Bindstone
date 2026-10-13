-- Player simulation: movement with wall sliding, the 'use' action (doors,
-- secrets, the exit switch), pickups.

local sqrt, cos, sin, floor = math.sqrt, math.cos, math.sin, math.floor

-- circle-vs-grid: all four corners of the actor's square must be on floor
function spot_free(x, y, r)
	if tile_solid(tile_at(floor(x - r), floor(y - r))) then return false end
	if tile_solid(tile_at(floor(x + r), floor(y - r))) then return false end
	if tile_solid(tile_at(floor(x - r), floor(y + r))) then return false end
	if tile_solid(tile_at(floor(x + r), floor(y + r))) then return false end
	return true
end

function player_move(input)
	local g = G
	local turn = 0.0
	if input.tl then turn = turn - 1.0 end
	if input.tr then turn = turn + 1.0 end
	g.pang = g.pang + turn * 2.7 * TICK
	while g.pang > 6.2831853 do g.pang = g.pang - 6.2831853 end
	while g.pang < 0.0 do g.pang = g.pang + 6.2831853 end

	local dirx = cos(g.pang)
	local diry = sin(g.pang)
	local wx = 0.0
	local wy = 0.0
	if input.fwd then wx = wx + dirx; wy = wy + diry end
	if input.back then wx = wx - dirx; wy = wy - diry end
	if input.sl then wx = wx + diry; wy = wy - dirx end
	if input.sr then wx = wx - diry; wy = wy + dirx end
	local wl = sqrt(wx * wx + wy * wy)
	if wl > 0.001 then
		local run = input.run
		local speed = (run and 5.0 or 3.4) * TICK
		wx = wx / wl * speed
		wy = wy / wl * speed
		local nx = g.px + wx
		local ny = g.py + wy
		local moved = false
		if spot_free(nx, g.py, 0.26) then g.px = nx; moved = true end
		if spot_free(g.px, ny, 0.26) then g.py = ny; moved = true end
		if moved then
			g.bob = g.bob + (run and 0.30 or 0.22)
			check_pickups()
		end
	end
end

-- ------------------------------------------------------------ 'use' ---------
-- probe a few steps down the facing for the first non-floor tile
local USE_PROBES = {0.5, 0.9, 1.3}

function player_use()
	local g = G
	local dirx = cos(g.pang)
	local diry = sin(g.pang)
	for i = 1, 3 do
		local p = USE_PROBES[i]
		local tx = floor(g.px + dirx * p)
		local ty = floor(g.py + diry * p)
		local t = tile_at(tx, ty)
		if t ~= 0 then
			use_tile(tx, ty, t)
			return
		end
	end
	show_msg("nothing to use", "90")
end

function use_tile(tx, ty, t)
	local g = G
	if t == 5 then open_door(tx, ty, "the door grinds open"); return end
	if t == 8 then
		if g.key_r then open_door(tx, ty, "RED lock released")
		else show_msg("you need the RED keycard", "91") end
		return
	end
	if t == 9 then
		if g.key_b then open_door(tx, ty, "BLUE lock released")
		else show_msg("you need the BLUE keycard", "94") end
		return
	end
	if t == 7 then
		g.tiles[ty * g.mw + tx + 1] = 0
		rebuild_mapstr()
		g.secrets = g.secrets + 1
		fx_secret_glitter(tx + 0.5, ty + 0.5)
		show_msg("you found a secret!", "96")
		return
	end
	if t == 6 then
		g.mode = 2      -- tally
		g.mode_t = 0
		show_msg("level complete", "92")
		return
	end
	show_msg("solid rock. very solid.", "90")
end

function open_door(tx, ty, msg)
	local g = G
	g.tiles[ty * g.mw + tx + 1] = 0
	rebuild_mapstr()
	fx_door_puff(tx + 0.5, ty + 0.5)
	g.noise = imax(g.noise, 10)
	show_msg(msg, "97")
end

-- ------------------------------------------------------------ pickups -------
function give_ammo(t, n)
	local g = G
	local ammo = g.ammo
	if ammo[t + 1] >= AMMO_MAX[t + 1] then return false end
	ammo[t + 1] = imin(AMMO_MAX[t + 1], ammo[t + 1] + n)
	return true
end

function check_pickups()
	local g = G
	local items = g.items
	for i = 1, #items do
		local it = items[i]
		if it[4] ~= 1 then
			local kind = it[1]
			local ix, iy = it[2], it[3]
			if dist2(g.px, g.py, ix, iy) <= 0.36 then
				local got = false
				if kind == 0 then
					if g.hp < 100 then g.hp = imin(100, g.hp + 10); got = true end
				elseif kind == 1 then
					if g.hp < 100 then g.hp = imin(100, g.hp + 25); got = true end
				elseif kind == 2 then
					if g.armor < 100 then g.armor = imin(100, g.armor + 50); got = true end
				elseif kind == 3 then got = give_ammo(0, 20)
				elseif kind == 4 then got = give_ammo(1, 8)
				elseif kind == 5 then got = give_ammo(2, 30)
				elseif kind == 6 then g.key_r = true; got = true
				elseif kind == 7 then g.key_b = true; got = true
				elseif kind == 8 then
					g.have[2] = 1
					give_ammo(1, 8)
					g.weapon = 1
					g.cooldown = 8
					got = true
					g.face_grin = 30
				elseif kind == 9 then
					g.have[3] = 1
					give_ammo(2, 40)
					g.weapon = 2
					g.cooldown = 8
					got = true
					g.face_grin = 30
				elseif kind == 10 then
					g.hp = imin(150, g.hp + 40)
					got = true
					g.face_grin = 20
				end
				if got then
					it[4] = 1
					show_msg("picked up " .. ITEM_DEFS[kind + 1].name, "93")
				end
			end
		end
	end
end
