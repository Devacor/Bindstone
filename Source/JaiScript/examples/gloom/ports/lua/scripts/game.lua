-- Game state + the fixed-timestep tick + the deterministic autopilot.
-- G is the single world table (created once in gloom_boot); Lua tables are
-- reference-semantic, so no accidental copy can fork the world.

local sqrt, cos, sin, floor, atan = math.sqrt, math.cos, math.sin, math.floor, math.atan
local create, resume, status, yield =
	coroutine.create, coroutine.resume, coroutine.status, coroutine.yield

Game = {}
Game.__index = Game

function Game.new(w, h)
	local self = setmetatable({}, Game)
	-- console + view geometry (sub-cell pixels: vw = w*PIXW, vh = rows*PIXH)
	self.w = w
	self.h = h
	self.vw = w * PIXW
	self.vh = (h - 6) * PIXH

	-- flow
	self.mode = 0          -- 0 title, 1 play, 2 tally, 3 dead, 4 victory
	self.mode_t = 0
	self.tick = 0
	self.accum = 0.0
	self.quit = false
	self.autopilot = false
	self.frame_key = ""
	self.hash = 2166136261

	-- map
	self.map_i = 0
	self.map_name = ""
	self.par = 0
	self.mw = 0
	self.mh = 0
	self.tiles = {}
	self.mapkind = nil     -- per-cell render kinds (raycast snapshot)

	-- player
	self.px = 2.5
	self.py = 2.5
	self.pang = 0.0
	self.hp = 100
	self.armor = 0
	self.ammo = {48, 0, 0}
	self.have = {1, 0, 0}
	self.weapon = 0
	self.key_r = false
	self.key_b = false
	self.cooldown = 0
	self.bob = 0.0
	self.muzzle = 0
	self.gun_kick = 0
	self.death_cause = "the gloom"

	-- world
	self.enemies = {}
	self.items = {}        -- {kind, x, y, taken}
	self.shots = {}        -- {kind, x, y, vx, vy, ttl}
	self.fx = ParticlePool.new()
	self.noise = 0
	self.warden_down = false

	-- feedback
	self.msg = ""
	self.msg_color = "97"
	self.msg_t = 0
	self.face_pain = 0
	self.face_grin = 0
	self.hurt_flash = 0
	self.automap = false

	-- tallies
	self.kills = 0
	self.kill_total = 0
	self.secrets = 0
	self.secret_total = 0
	self.map_ticks = 0
	self.ep_kills = 0
	self.ep_kill_total = 0
	self.ep_secrets = 0
	self.ep_secret_total = 0
	self.ep_ticks = 0
	self.deaths = 0

	-- render scratch (allocated once; bg rebaked per map)
	local pix = {}
	for i = 1, self.vw * self.vh do pix[i] = 0 end
	self.pix = pix
	self.bg = {}
	local zbuf = {}
	for i = 1, self.vw do zbuf[i] = 99.0 end
	self.zbuf = zbuf
	self.rowparts = {}
	self.lutc = {}

	-- the autopilot: a coroutine handle living in a field
	self.pilot = nil
	self.input = {}
	return self
end

function Game:reset_player()
	self.hp = 100
	self.armor = 0
	self.ammo = {48, 0, 0}
	self.have = {1, 0, 0}
	self.weapon = 0
	self.cooldown = 0
	self.bob = 0.0
end

function Game:start_episode()
	self.map_i = 0
	self.ep_kills = 0
	self.ep_kill_total = 0
	self.ep_secrets = 0
	self.ep_secret_total = 0
	self.ep_ticks = 0
	self.deaths = 0
	self:reset_player()
	self:begin_map()
end

function Game:begin_map()
	self.key_r = false
	self.key_b = false
	self.shots = {}
	self.fx:reset()
	load_map(self.map_i)
	self.mode = 1
	self.mode_t = 0
	show_msg(self.map_name, "97")
end

function Game:finish_map()
	self.ep_kills = self.ep_kills + self.kills
	self.ep_kill_total = self.ep_kill_total + self.kill_total
	self.ep_secrets = self.ep_secrets + self.secrets
	self.ep_secret_total = self.ep_secret_total + self.secret_total
	self.ep_ticks = self.ep_ticks + self.map_ticks
	self.map_i = self.map_i + 1
	if self.map_i >= #MAPS then
		self.mode = 4
		self.mode_t = 0
	else
		self:begin_map()
	end
end

-- deterministic input for this tick, human or coroutine
function Game:gather_input()
	if self.autopilot then
		local pilot = self.pilot
		if pilot == nil or status(pilot) == "dead" then
			pilot = create(Game.pilot_brain)
			self.pilot = pilot
		end
		local ok, input = resume(pilot, self)
		if not ok then error(input, 0) end
		return input
	end
	local fk = self.frame_key
	local fire_edge = fk == " " or fk == "enter"
	local wsel = -1
	if fk == "1" then wsel = 0
	elseif fk == "2" then wsel = 1
	elseif fk == "3" then wsel = 2 end
	return {
		fwd = key_down("w") or key_down("up"),
		back = key_down("s") or key_down("down"),
		sl = key_down("a"),
		sr = key_down("d"),
		tl = key_down("left"),
		tr = key_down("right"),
		run = key_down("shift"),
		fire = key_down("space") or key_down("ctrl"),
		use = fk == "e",
		weapon = wsel,
		start = fire_edge or key_down("space") or key_down("ctrl"),
	}
end

function Game:run_tick()
	self.tick = self.tick + 1
	local input = self:gather_input()
	self.input = input
	local start_pressed = input.start
	local mode = self.mode
	if mode == 0 then
		self.mode_t = self.mode_t + 1
		if start_pressed and self.mode_t > 8 then self:start_episode() end
		self:accumulate_hash()
		return
	end
	if mode == 2 then
		self.mode_t = self.mode_t + 1
		if start_pressed and self.mode_t > 25 then self:finish_map() end
		self:accumulate_hash()
		return
	end
	if mode == 3 then
		self.mode_t = self.mode_t + 1
		if start_pressed and self.mode_t > 25 then
			self.deaths = self.deaths + 1
			self:reset_player()
			self:begin_map()       -- the floor repopulates; the Well remembers nothing
		end
		self:accumulate_hash()
		return
	end
	if mode == 4 then
		self.mode_t = self.mode_t + 1
		self:accumulate_hash()
		return
	end

	-- --- play ---
	local wsel = input.weapon
	if wsel >= 0 and self.have[wsel + 1] == 1 and wsel ~= self.weapon then
		self.weapon = wsel
		self.cooldown = 8
		show_msg(WEAPONS[wsel + 1].name .. " ready", "97")
	end
	if self.frame_key == "m" then self.automap = not self.automap end
	if input.use then player_use() end
	player_move(input)
	if input.fire then player_fire() end

	if self.cooldown > 0 then self.cooldown = self.cooldown - 1 end
	if self.muzzle > 0 then self.muzzle = self.muzzle - 1 end
	if self.gun_kick > 0 then self.gun_kick = self.gun_kick - 1 end
	if self.noise > 0 then self.noise = self.noise - 1 end
	if self.msg_t > 0 then self.msg_t = self.msg_t - 1 end
	if self.face_pain > 0 then self.face_pain = self.face_pain - 1 end
	if self.face_grin > 0 then self.face_grin = self.face_grin - 1 end
	if self.hurt_flash > 0 then self.hurt_flash = self.hurt_flash - 1 end

	-- live length each iteration: warden summons APPEND mid-loop and tick the
	-- same tick (the reference range-for re-checks size per iteration)
	local enemies = self.enemies
	local i = 1
	while i <= #enemies do
		enemies[i]:tick()
		i = i + 1
	end
	update_shots()
	self.fx:update()
	self.map_ticks = self.map_ticks + 1
	self:accumulate_hash()
end

function Game:accumulate_hash()
	local hh = self.hash
	hh = mix32(hh, self.tick)
	hh = mix32(hh, self.mode * 31 + self.map_i)
	hh = mix32(hh, trunc(self.px * 256.0) * 4096 + trunc(self.py * 256.0))
	hh = mix32(hh, trunc(self.pang * 1024.0))
	hh = mix32(hh, self.hp * 512 + self.armor)
	local ammo = self.ammo
	hh = mix32(hh, ammo[1] * 65536 + ammo[2] * 256 + ammo[3])
	hh = mix32(hh, self.weapon * 64 + (self.key_r and 2 or 0) + (self.key_b and 1 or 0))
	local live = 0
	local acc = 0
	local enemies = self.enemies
	for i = 1, #enemies do
		local e = enemies[i]
		if e.alive then
			live = live + 1
			acc = (acc + trunc(e.x * 64.0) * 977 + trunc(e.y * 64.0) * 331 + e.hp * 7) & 0xFFFFFFFF
		end
	end
	hh = mix32(hh, live)
	hh = mix32(hh, acc)
	local pacc = 0
	local pool = self.fx.pool
	for i = 1, PART_MAX do
		local p = pool[i]
		local pk = p[1]
		if pk ~= 0 then
			pacc = (pacc + pk * 131 + (trunc(p[2] * 16.0) * 61 + trunc(p[3] * 16.0)) * 17 + p[8]) & 0xFFFFFFFF
		end
	end
	hh = mix32(hh, pacc)
	hh = mix32(hh, #self.shots * 8191 + self.kills * 127 + self.secrets * 31)
	hh = mix32(hh, RNG:state())
	self.hash = hh
end

-- ------------------------------------------------------- autopilot ------
-- One coroutine, one yield per tick, every decision from game state + RNG.
function Game:pilot_brain()
	local dist_field = {}
	local field_age = 999
	local goal_x = -1
	local goal_y = -1
	local strafe_dir = 1
	local strafe_t = 0
	while true do
		if self.mode ~= 1 then
			yield(self:pilot_input(false, false, false, false, 0, false, false, -1, true))
		else

		-- --- combat: nearest visible enemy inside 12 tiles. Below 40 hp the
		-- pilot disengages unless cornered — navigation will chase medkits.
		local desperate = self.hp < 40
		local found = false
		local tgt_x = 0.0
		local tgt_y = 0.0
		local best_d = 12.0
		local enemies = self.enemies
		local px, py = self.px, self.py
		for i = 1, #enemies do
			local e = enemies[i]
			if e.alive then
				local d = sqrt(dist2(e.x, e.y, px, py))
				if d < best_d and los_clear(px, py, e.x, e.y) then
					best_d = d
					tgt_x = e.x
					tgt_y = e.y
					found = true
				end
			end
		end
		if found and desperate and best_d > 3.0 then found = false end
		if found then
			local want = atan(tgt_y - py, tgt_x - px)
			local da = ang_diff(self.pang, want)
			local turn = 0
			if da > 0.05 then turn = 1 end
			if da < -0.05 then turn = -1 end
			-- weapon sense: scatter close, hex for crowds/far, pistol default
			local wsel = -1
			local have, ammo = self.have, self.ammo
			if have[2] == 1 and ammo[2] > 0 and best_d < 6.0 then wsel = 1
			elseif have[3] == 1 and ammo[3] > 0 and best_d > 3.5 then wsel = 2
			elseif ammo[1] > 0 then wsel = 0
			elseif have[2] == 1 and ammo[2] > 0 then wsel = 1
			elseif have[3] == 1 and ammo[3] > 0 then wsel = 2 end
			if wsel == self.weapon then wsel = -1 end
			local aligned = da < 0.14 and da > -0.14
			local fire = aligned and self.cooldown == 0 and best_d > 1.2
			-- footwork: back off rushers, strafe under fire
			local back = best_d < 2.4
			local fwd = best_d > 8.5 and aligned
			strafe_t = strafe_t + 1
			if strafe_t > 18 then
				strafe_t = 0
				if RNG:chance(60) then strafe_dir = -strafe_dir end
			end
			yield(self:pilot_input(fwd, back, strafe_dir < 0, strafe_dir > 0, turn, fire, false, wsel, false))
		else

		-- --- navigate: pick a goal, BFS-from-goal field, walk downhill.
		-- COMMIT to the goal until arrival; the field alone refreshes on a
		-- timer so newly opened doors shorten the route.
		field_age = field_age + 1
		local goal_gone = goal_x < 0 or field_age > 240
		if not goal_gone then
			if tile_at(goal_x, goal_y) == 0 and floor(px) == goal_x and floor(py) == goal_y then
				goal_gone = true
			end
		end
		if goal_gone then
			local gx, gy = self:pick_goal()
			goal_x = gx
			goal_y = gy
			dist_field = self:bfs_field(goal_x, goal_y)
			field_age = 0
		elseif field_age % 45 == 0 then
			dist_field = self:bfs_field(goal_x, goal_y)
		end
		if goal_x < 0 then
			-- nothing to want: spin slowly (a lost pilot is still deterministic)
			yield(self:pilot_input(false, false, false, false, 1, false, false, -1, false))
		else
		local mw, mh = self.mw, self.mh
		local ptx = floor(px)
		local pty = floor(py)
		local here = dist_field[pty * mw + ptx + 1]
		if here < 0 then
			-- goal unreachable from here (shouldn't happen): drop it
			goal_x = -1
			yield(self:pilot_input(false, false, false, false, 0, false, false, -1, false))
		else
			-- pick the walkable-or-door neighbor closest to the goal
			local nx = ptx
			local ny = pty
			local bestv = here
			local cx, cy, v
			cx = ptx + 1; cy = pty
			if cx < mw then
				v = dist_field[cy * mw + cx + 1]
				if v >= 0 and v < bestv then bestv = v; nx = cx; ny = cy end
			end
			cx = ptx - 1
			if cx >= 0 then
				v = dist_field[cy * mw + cx + 1]
				if v >= 0 and v < bestv then bestv = v; nx = cx; ny = cy end
			end
			cx = ptx; cy = pty + 1
			if cy < mh then
				v = dist_field[cy * mw + cx + 1]
				if v >= 0 and v < bestv then bestv = v; nx = cx; ny = cy end
			end
			cy = pty - 1
			if cy >= 0 then
				v = dist_field[cy * mw + cx + 1]
				if v >= 0 and v < bestv then bestv = v; nx = cx; ny = cy end
			end
			local wx = nx + 0.5
			local wy = ny + 0.5
			local want2 = atan(wy - py, wx - px)
			local da2 = ang_diff(self.pang, want2)
			local turn2 = 0
			if da2 > 0.08 then turn2 = 1 end
			if da2 < -0.08 then turn2 = -1 end
			local fwd2 = da2 < 0.6 and da2 > -0.6
			-- a door/secret in the way? use it when close and mostly facing
			local front_t = tile_at(nx, ny)
			local use2 = false
			if front_t ~= 0 and dist2(px, py, wx, wy) < 2.1 and fwd2 then use2 = true end
			yield(self:pilot_input(fwd2, false, false, false, turn2, false, use2, -1, false))
		end
		end
		end
		end
	end
end

function Game:pilot_input(fwd, back, sl, sr, turn, fire, use, wsel, start)
	return {
		fwd = fwd, back = back, sl = sl, sr = sr,
		tl = turn < 0, tr = turn > 0,
		run = true, fire = fire, use = use,
		weapon = wsel, start = start,
	}
end

-- goal priority: medkits when hurting -> live enemies -> loot -> unfound
-- secrets -> the exit
local MED_KINDS = {0, 1, 10}
local ALL_KINDS = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}

function Game:pick_goal()
	if self.hp < 55 then
		local mx, my = self:nearest_item(MED_KINDS)
		if mx >= 0 then return mx, my end
	end
	local hunt_x = -1
	local hunt_y = -1
	local hd = 9999.0
	local enemies = self.enemies
	local px, py = self.px, self.py
	for i = 1, #enemies do
		local e = enemies[i]
		if e.alive then
			local d = dist2(e.x, e.y, px, py)
			if d < hd then
				hd = d
				hunt_x = floor(e.x)
				hunt_y = floor(e.y)
			end
		end
	end
	if hunt_x >= 0 then return hunt_x, hunt_y end
	local lx, ly = self:nearest_item(ALL_KINDS)
	if lx >= 0 then return lx, ly end
	if self.secrets < self.secret_total then
		local sx, sy = self:nearest_secret()
		if sx >= 0 then return sx, sy end
	end
	return self:exit_tile()
end

function Game:nearest_item(kinds)
	local bx = -1
	local by = -1
	local bd = 9999.0
	local items = self.items
	local nkinds = #kinds
	local px, py = self.px, self.py
	local hp, armor = self.hp, self.armor
	local ammo = self.ammo
	for i = 1, #items do
		local it = items[i]
		if it[4] ~= 1 then
			local k = it[1]
			local wanted = false
			for j = 1, nkinds do if kinds[j] == k then wanted = true end end
			-- skip anything the pickup code would refuse
			if wanted then
				if (k == 0 or k == 1) and hp >= 100 then wanted = false end
				if k == 10 and hp >= 150 then wanted = false end
				if k == 2 and armor >= 100 then wanted = false end
				if k == 3 and ammo[1] >= AMMO_MAX[1] then wanted = false end
				if k == 4 and ammo[2] >= AMMO_MAX[2] then wanted = false end
				if k == 5 and ammo[3] >= AMMO_MAX[3] then wanted = false end
				-- keys we already carry are not loot
				if k == 6 and self.key_r then wanted = false end
				if k == 7 and self.key_b then wanted = false end
			end
			if wanted then
				local d = dist2(it[2], it[3], px, py)
				if d < bd then
					bd = d
					bx = floor(it[2])
					by = floor(it[3])
				end
			end
		end
	end
	return bx, by
end

function Game:nearest_secret()
	local bx = -1
	local by = -1
	local bd = 9999.0
	local mw, mh = self.mw, self.mh
	local tiles = self.tiles
	local px, py = self.px, self.py
	for ty = 0, mh - 1 do
		for tx = 0, mw - 1 do
			if tiles[ty * mw + tx + 1] == 7 then
				local d = dist2(tx + 0.5, ty + 0.5, px, py)
				if d < bd then
					bd = d
					bx = tx
					by = ty
				end
			end
		end
	end
	return bx, by
end

function Game:exit_tile()
	local mw, mh = self.mw, self.mh
	local tiles = self.tiles
	for ty = 0, mh - 1 do
		for tx = 0, mw - 1 do
			if tiles[ty * mw + tx + 1] == 6 then return tx, ty end
		end
	end
	return -1, -1
end

-- BFS distances FROM the goal, over floor + doors this pilot can open
-- (+ the goal tile itself so walls like secrets/exit are approachable)
function Game:bfs_field(gx, gy)
	local mw, mh = self.mw, self.mh
	local field = {}
	local n = mw * mh
	for i = 1, n do field[i] = -1 end
	if gx < 0 then return field end
	local tiles = self.tiles
	local key_r, key_b = self.key_r, self.key_b
	local qx, qy = {gx}, {gy}
	field[gy * mw + gx + 1] = 0
	local head = 1
	while head <= #qx do
		local cx, cy = qx[head], qy[head]
		head = head + 1
		local cd = field[cy * mw + cx + 1]
		for di = 1, 4 do
			local nx, ny
			if di == 1 then nx = cx + 1; ny = cy
			elseif di == 2 then nx = cx - 1; ny = cy
			elseif di == 3 then nx = cx; ny = cy + 1
			else nx = cx; ny = cy - 1 end
			if nx >= 0 and ny >= 0 and nx < mw and ny < mh then
				local fi = ny * mw + nx + 1
				if field[fi] < 0 then
					local t = tiles[fi]
					local pass = t == 0 or t == 5
					if t == 8 and key_r then pass = true end
					if t == 9 and key_b then pass = true end
					if pass then
						field[fi] = cd + 1
						qx[#qx + 1] = nx
						qy[#qy + 1] = ny
					end
				end
			end
		end
	end
	return field
end
