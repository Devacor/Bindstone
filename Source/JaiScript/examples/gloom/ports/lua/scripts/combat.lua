-- Combat: line of sight, hitscan weapons, projectiles, explosions, damage.

local sqrt, cos, sin, floor = math.sqrt, math.cos, math.sin, math.floor

-- stepped-ray LOS on the tile grid (deterministic float march)
function los_clear(ax, ay, bx, by)
	local dx = bx - ax
	local dy = by - ay
	local len = sqrt(dx * dx + dy * dy)
	if len < 0.001 then return true end
	local inv = 1.0 / len
	dx = dx * inv
	dy = dy * inv
	local g = G
	local mw, mh = g.mw, g.mh
	local tiles = g.tiles
	local t = 0.15
	while t < len do
		local x = floor(ax + dx * t)
		local y = floor(ay + dy * t)
		if x < 0 or y < 0 or x >= mw or y >= mh then return false end
		if tiles[y * mw + x + 1] ~= 0 then return false end
		t = t + 0.15
	end
	return true
end

-- wall distance along an arbitrary (unit) ray — the SAME DDA the renderer uses
function wall_dist(ax, ay, dx, dy)
	local pd = gloom_ray(ax, ay, dx, dy)
	return pd
end

-- nearest live enemy along a unit ray, cross-checked against the wall
function hitscan(ax, ay, dx, dy, lo, hi, from_player)
	local g = G
	local wd = wall_dist(ax, ay, dx, dy)
	local best = 0
	local best_t = 999.0
	local enemies = g.enemies
	for i = 1, #enemies do
		local e = enemies[i]
		if e.alive then
			local rx = e.x - ax
			local ry = e.y - ay
			local t = rx * dx + ry * dy
			if t >= 0.2 and t <= wd + e.radius and t < best_t then
				local perp = rx * dy - ry * dx
				if perp < 0.0 then perp = 0.0 - perp end
				if perp < e.radius + 0.12 then
					best = i
					best_t = t
				end
			end
		end
	end
	if best > 0 then
		enemies[best]:hurt(RNG:roll(lo, hi))
		return true
	end
	-- wall impact garnish just short of the surface
	fx_wall_hit(ax + dx * (wd - 0.08), ay + dy * (wd - 0.08))
	return false
end

-- turret bolt: instant, accuracy falls off with range
function turret_shot(tx, ty)
	local g = G
	local dx = g.px - tx
	local dy = g.py - ty
	local d = sqrt(dx * dx + dy * dy)
	if d < 0.001 then return end
	fx_muzzle(tx, ty, dx / d, dy / d)
	local hit_pct = imax(12, 66 - trunc(d * 5.0))
	if RNG:chance(hit_pct) and los_clear(tx, ty, g.px, g.py) then
		damage_player(RNG:roll(2, 6), "turret fire")
	else
		-- tracer smacks the wall behind you
		local wd = wall_dist(tx, ty, dx / d, dy / d) - 0.1
		local t = d + 1.5
		if t > wd then t = wd end
		fx_wall_hit(tx + dx / d * t, ty + dy / d * t)
	end
end

-- ------------------------------------------------------------ projectiles ---
-- value records: {kind, x, y, vx, vy, ttl}
--   kind 1 spitter gob | 2 warden hollow fire | 3 player hex bolt
function spawn_shot(kind, ax, ay, tx, ty, speed)
	local dx = tx - ax
	local dy = ty - ay
	local len = sqrt(dx * dx + dy * dy)
	if len < 0.001 then return end
	local shots = G.shots
	shots[#shots + 1] = {kind, ax + dx / len * 0.4, ay + dy / len * 0.4,
		dx / len * speed, dy / len * speed, 150}
end

function spawn_shot_dir(kind, ax, ay, dx, dy, speed)
	local shots = G.shots
	shots[#shots + 1] = {kind, ax + dx * 0.45, ay + dy * 0.45, dx * speed, dy * speed, 150}
end

function update_shots()
	local g = G
	local shots = g.shots
	local kept = {}
	local nk = 0
	for i = 1, #shots do
		local s = shots[i]
		local kind = s[1]
		local x, y = s[2], s[3]
		local vx, vy = s[4], s[5]
		local ttl = s[6] - 1
		local dead = ttl <= 0
		-- 3 substeps so fast bolts don't tunnel tile corners
		local sub = 0
		while sub < 3 and not dead do
			x = x + vx * TICK * 0.333333333
			y = y + vy * TICK * 0.333333333
			if tile_solid(tile_at(floor(x), floor(y))) then
				if kind == 3 then explode_hex(x - vx * 0.02, y - vy * 0.02)
				elseif kind == 2 then explode_fire(x - vx * 0.02, y - vy * 0.02)
				else fx_wall_hit(x - vx * 0.02, y - vy * 0.02) end
				dead = true
				break
			end
			if kind == 3 then
				-- player bolt vs enemies
				local enemies = g.enemies
				for j = 1, #enemies do
					local e = enemies[j]
					if e.alive then
						local r = e.radius + 0.2
						if dist2(x, y, e.x, e.y) < r * r then
							explode_hex(x, y)
							dead = true
							break
						end
					end
				end
			else
				-- enemy shot vs player
				if dist2(x, y, g.px, g.py) < 0.14 then
					if kind == 2 then explode_fire(x, y)
					else
						damage_player(RNG:roll(4, 9), "a caustic gob")
						fx_blood(x, y, 0.5, 3)
					end
					dead = true
					break
				end
			end
			sub = sub + 1
		end
		if not dead then
			nk = nk + 1
			kept[nk] = {kind, x, y, vx, vy, ttl}
		end
	end
	g.shots = kept
	-- glow trail (shot index is 0-based in the (tick + i) % 2 phase rule)
	local fx = g.fx
	for i = 1, nk do
		local s2 = kept[i]
		if (g.tick + i - 1) % 2 == 0 then
			local k2 = s2[1]
			fx:spawn(k2 == 1 and 6 or (k2 == 2 and 1 or 6), s2[2], s2[3], 0.5, 0.0, 0.0, 0.0, 4)
		end
	end
end

function explode_hex(x, y)
	local g = G
	fx_explosion(x, y)
	g.noise = 20
	local enemies = g.enemies
	for i = 1, #enemies do
		local e = enemies[i]
		if e.alive then
			local d2 = dist2(x, y, e.x, e.y)
			if d2 < 3.24 then                              -- radius 1.8
				local dmg = trunc(42.0 - sqrt(d2) * 14.0)
				if dmg > 0 then e:hurt(dmg) end
			end
		end
	end
	-- standing in your own spell is a choice
	local pd2 = dist2(x, y, g.px, g.py)
	if pd2 < 2.25 then
		local self_dmg = trunc(20.0 - sqrt(pd2) * 10.0)
		if self_dmg > 0 then damage_player(self_dmg, "your own hex") end
	end
end

function explode_fire(x, y)
	local g = G
	fx_explosion(x, y)
	local pd2 = dist2(x, y, g.px, g.py)
	if pd2 < 2.89 then                                 -- radius 1.7
		local dmg = trunc(26.0 - sqrt(pd2) * 9.0)
		if dmg > 0 then damage_player(dmg, "hollow fire") end
	end
end

-- ------------------------------------------------------------ the player ----
function damage_player(dmg, src)
	local g = G
	if g.mode ~= 1 or dmg <= 0 then return end
	if HOST_GOD then return end
	local absorbed = imin(g.armor, dmg * 2 // 3)
	g.armor = g.armor - absorbed
	local taken = dmg - absorbed
	g.hp = g.hp - taken
	g.face_pain = 12
	g.hurt_flash = 4
	show_msg("-" .. taken .. " hp (" .. src .. ")", "91")
	if g.hp <= 0 then
		g.hp = 0
		g.mode = 3
		g.mode_t = 0
		g.death_cause = src
		show_msg("you died", "91")
	end
end

function player_fire()
	local g = G
	if g.cooldown > 0 then return end
	local w = WEAPONS[g.weapon + 1]
	local ammo_type = w.ammo
	if g.ammo[ammo_type + 1] <= 0 then
		show_msg("out of " .. AMMO_NAMES[ammo_type + 1] .. "S", "93")
		g.cooldown = 8
		return
	end
	g.ammo[ammo_type + 1] = g.ammo[ammo_type + 1] - 1
	g.cooldown = w.cd
	g.muzzle = 3
	g.noise = 24
	g.gun_kick = 4
	local dirx = cos(g.pang)
	local diry = sin(g.pang)
	fx_muzzle(g.px, g.py, dirx, diry)
	if w.kind == 1 then
		spawn_shot_dir(3, g.px, g.py, dirx, diry, 9.0)
		return
	end
	local pellets = w.pellets
	local spread = w.spread
	for i = 1, pellets do
		local a = g.pang + (RNG:nextf() - 0.5) * 2.0 * spread
		hitscan(g.px, g.py, cos(a), sin(a), w.lo, w.hi, true)
	end
end
