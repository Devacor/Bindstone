-- Enemies: metatable class, one instance table per body in G.enemies, and
-- each enemy's BRAIN is a plain Lua coroutine stored in its own field —
-- kind-specific methods run inside coroutine.create, the per-tick resume
-- drives multi-phase behavior, and all phase state lives in the coroutine
-- frame. Pain interrupts by discarding the handle; tick() re-mints it.

ENEMY_STATE_DORMANT = 0
ENEMY_STATE_HUNT = 1
ENEMY_STATE_ATTACK = 2
ENEMY_STATE_DYING = 3
ENEMY_STATE_DEAD = 4

local sqrt = math.sqrt
local floor = math.floor
local create, resume, status, yield =
	coroutine.create, coroutine.resume, coroutine.status, coroutine.yield

Enemy = {}
Enemy.__index = Enemy

function Enemy.new(kind, x, y)
	local def = ENEMY_DEFS[kind + 1]
	return setmetatable({
		kind = kind,
		x = x,
		y = y,
		hp = def.hp,
		alive = true,
		state = 0,          -- ENEMY_STATE_*
		anim = 0,           -- ticks in current state (drives art frames)
		walk = 0,           -- walk cycle counter (advances only when moving)
		flash = 0,          -- damage flash ticks
		stun = 0,           -- pain stun ticks (brain dropped + re-minted)
		radius = def.radius,
		speed = def.speed,
		brain = nil,        -- coroutine handle in a field: the whole point
	}, Enemy)
end

-- one sim tick: corpse cools, stun blocks, otherwise the brain runs
function Enemy:tick()
	self.anim = self.anim + 1
	if self.flash > 0 then self.flash = self.flash - 1 end
	if not self.alive then return end
	if self.stun > 0 then
		self.stun = self.stun - 1
		return
	end
	local br = self.brain
	if br == nil or status(br) == "dead" then
		local k = self.kind
		local fn
		if k == 0 then fn = Enemy.brain_grunt
		elseif k == 1 then fn = Enemy.brain_spitter
		elseif k == 2 then fn = Enemy.brain_turret
		else fn = Enemy.brain_warden end
		br = create(fn)
		self.brain = br
	end
	local ok, err = resume(br, self)
	if not ok then error(err, 0) end
end

function Enemy:player_dist()
	local g = G
	return sqrt(dist2(self.x, self.y, g.px, g.py))
end

function Enemy:sees_player()
	if self:player_dist() > 13.0 then return false end
	local g = G
	return los_clear(self.x, self.y, g.px, g.py)
end

function Enemy:hears_player()
	return G.noise > 0 and self:player_dist() < 12.0
end

-- slide movement against the tile grid (axes resolved separately)
function Enemy:move_toward(tx, ty)
	local x, y = self.x, self.y
	local dx = tx - x
	local dy = ty - y
	local len = sqrt(dx * dx + dy * dy)
	if len < 0.0001 then return end
	local step = self.speed * TICK
	local nx = x + dx / len * step
	local ny = y + dy / len * step
	local r = self.radius
	if spot_free(nx, y, r) then self.x = nx; self.walk = self.walk + 1 end
	if spot_free(self.x, ny, r) then self.y = ny; self.walk = self.walk + 1 end
end

function Enemy:hurt(dmg)
	if not self.alive then return end
	self.hp = self.hp - dmg
	self.flash = 4
	fx_blood(self.x, self.y, 0.5, imin(6, 2 + dmg // 6))
	if self.hp <= 0 then
		self:die()
		return
	end
	-- pain: drop the brain mid-phase; re-minted next tick = re-telegraph
	if RNG:chance(ENEMY_DEFS[self.kind + 1].pain) then
		self.stun = 5 + RNG:next(5)
		self.brain = nil
		self.state = ENEMY_STATE_HUNT
		self.anim = 0
	end
end

function Enemy:die()
	self.alive = false
	self.state = ENEMY_STATE_DYING
	self.anim = 0
	self.brain = nil
	local g = G
	g.kills = g.kills + 1
	fx_gibs(self.x, self.y, self.kind == 3 and 14 or 7)
	if self.kind == 3 then
		fx_explosion(self.x, self.y)
		g.warden_down = true
	end
	show_msg(ENEMY_DEFS[self.kind + 1].name .. " destroyed", "92")
end

-- --------------------------------------------------------- brains -------
-- One yield = one tick. Timers are plain loop counters living in the
-- coroutine frame; pain interrupts by discarding the handle (tick()).

-- GRUNT: doze -> roar -> relentless zigzag chase -> lunge bite
function Enemy:brain_grunt()
	local g = G
	while self.state == ENEMY_STATE_DORMANT do
		if (self:sees_player() and self:player_dist() < 11.0) or self:hears_player() then break end
		yield()
	end
	self.state = ENEMY_STATE_HUNT
	self.anim = 0
	for i = 1, 7 do yield() end   -- the roar (it commits)
	local zig = RNG:next(2) == 0 and 1 or -1
	while true do
		local d = self:player_dist()
		if d < 1.3 then
			self.state = ENEMY_STATE_ATTACK
			self.anim = 0
			for i = 1, 6 do yield() end     -- lunge windup
			if self:player_dist() < 1.6 and los_clear(self.x, self.y, g.px, g.py) then
				damage_player(RNG:roll(ENEMY_DEFS[1].melee_lo, ENEMY_DEFS[1].melee_hi), "a grunt's claws")
			end
			self.state = ENEMY_STATE_HUNT
			self.anim = 0
			for i = 1, 5 do yield() end     -- recover
		else
			-- zigzag pursuit: aim past the player's flank, swapping sides
			if self.anim % 20 == 19 then zig = -zig end
			local fx = g.py - self.y
			local fy = self.x - g.px
			local fl = sqrt(fx * fx + fy * fy)
			if fl < 0.001 then fl = 1.0 end
			local lean = d > 3.0 and 0.9 or 0.2
			self:move_toward(g.px + fx / fl * lean * zig, g.py + fy / fl * lean * zig)
			yield()
		end
	end
end

-- SPITTER: keeps its range band, strafes, telegraphs, spits
function Enemy:brain_spitter()
	local g = G
	while self.state == ENEMY_STATE_DORMANT do
		if (self:sees_player() and self:player_dist() < 12.0) or self:hears_player() then break end
		yield()
	end
	self.state = ENEMY_STATE_HUNT
	self.anim = 0
	local orbit = RNG:next(2) == 0 and 1 or -1
	while true do
		local d = self:player_dist()
		local los = los_clear(self.x, self.y, g.px, g.py)
		if los and d < 9.5 and d > 2.0 then
			-- telegraph glow, then the gob
			self.state = ENEMY_STATE_ATTACK
			self.anim = 0
			for i = 1, 9 do yield() end
			if los_clear(self.x, self.y, g.px, g.py) then
				spawn_shot(1, self.x, self.y, g.px, g.py, 6.5)
			end
			self.state = ENEMY_STATE_HUNT
			self.anim = 0
			-- cooldown spent orbiting sideways
			for i = 1, 16 do
				local ox = g.py - self.y
				local oy = self.x - g.px
				local ol = sqrt(ox * ox + oy * oy)
				if ol < 0.001 then ol = 1.0 end
				self:move_toward(self.x + ox / ol * orbit, self.y + oy / ol * orbit)
				yield()
			end
			if RNG:chance(40) then orbit = -orbit end
		elseif d <= 2.0 then
			-- too close: shove and retreat
			if d < 1.2 and RNG:chance(30) then
				damage_player(RNG:roll(ENEMY_DEFS[2].melee_lo, ENEMY_DEFS[2].melee_hi), "a spitter's talons")
			end
			self:move_toward(self.x + (self.x - g.px), self.y + (self.y - g.py))
			yield()
		else
			self:move_toward(g.px, g.py)
			yield()
		end
	end
end

-- TURRET: dormant metal until it has line of sight; then 3-round bursts.
function Enemy:brain_turret()
	while true do
		while not (self:sees_player() and self:player_dist() < 11.0) do
			self.state = ENEMY_STATE_DORMANT
			yield()
		end
		self.state = ENEMY_STATE_HUNT     -- waking whir
		self.anim = 0
		for i = 1, 8 do yield() end
		while self:sees_player() and self:player_dist() < 12.0 do
			self.state = ENEMY_STATE_ATTACK
			self.anim = 0
			for burst = 1, 3 do
				turret_shot(self.x, self.y)
				yield()
				yield()
			end
			self.state = ENEMY_STATE_HUNT
			self.anim = 0
			for i = 1, 13 do yield() end
		end
	end
end

-- WARDEN: the landlord. Stalks and lobs hollow fire; under half health he
-- goes double-volley and calls the family, exactly once.
function Enemy:brain_warden()
	local g = G
	local enraged = false
	local summoned = false
	while self.state == ENEMY_STATE_DORMANT do
		if (self:sees_player() and self:player_dist() < 13.0) or self:hears_player() then break end
		yield()
	end
	self.state = ENEMY_STATE_HUNT
	self.anim = 0
	show_msg("THE WARDEN HAS SEEN YOU", "91")
	for i = 1, 10 do yield() end
	while true do
		if not enraged and self.hp * 2 < ENEMY_DEFS[4].hp then
			enraged = true
			self.speed = self.speed * 1.5
			show_msg("The Warden's wounds glow like coals", "91")
		end
		if enraged and not summoned then
			summoned = true
			summon_adds(self.x, self.y)
			show_msg("The Warden howls for his pack", "91")
		end
		local d = self:player_dist()
		local los = los_clear(self.x, self.y, g.px, g.py)
		if los and d > 2.4 then
			self.state = ENEMY_STATE_ATTACK
			self.anim = 0
			for i = 1, 8 do yield() end
			local volleys = enraged and 2 or 1
			for v = 1, volleys do
				if los_clear(self.x, self.y, g.px, g.py) then
					spawn_shot(2, self.x, self.y, g.px, g.py, 5.5)
				end
				for i = 1, 4 do yield() end
			end
			self.state = ENEMY_STATE_HUNT
			self.anim = 0
			local rest = enraged and 6 or 12
			for i = 1, rest do
				self:move_toward(g.px, g.py)
				yield()
			end
		elseif d < 1.7 then
			self.state = ENEMY_STATE_ATTACK
			self.anim = 0
			for i = 1, 5 do yield() end
			if self:player_dist() < 2.0 then
				damage_player(RNG:roll(ENEMY_DEFS[4].melee_lo, ENEMY_DEFS[4].melee_hi), "the Warden's fist")
			end
			self.state = ENEMY_STATE_HUNT
			self.anim = 0
			for i = 1, 4 do yield() end
		else
			self:move_toward(g.px, g.py)
			yield()
		end
	end
end

function spawn_enemy(kind, tx, ty)
	local g = G
	g.enemies[#g.enemies + 1] = Enemy.new(kind, tx + 0.5, ty + 0.5)
	g.kill_total = g.kill_total + 1
end

-- warden's reinforcements: grunts at the nearest free tiles around him.
function summon_adds(wx, wy)
	local offs = {{2, 0}, {-2, 0}, {0, 2}, {0, -2}}
	local placed = 0
	for i = 1, 4 do
		if placed >= 2 then break end
		local o = offs[i]
		local tx = floor(wx) + o[1]
		local ty = floor(wy) + o[2]
		if walkable(tx, ty) then
			spawn_enemy(0, tx, ty)
			fx_explosion(tx + 0.5, ty + 0.5)
			placed = placed + 1
		end
	end
end

-- which art a body renders as, given its state machinery
local ART_BASE = {"grunt", "spit", "tur", "war"}

function enemy_art(e)
	local name = ART_BASE[e.kind + 1]
	if e.kind == 2 then
		-- turret has its own frame set
		if e.alive then
			if e.state == ENEMY_STATE_ATTACK then return "tur_fire" end
			return "tur_idle"
		end
		if e.anim < 6 then return "tur_d0" end
		if e.anim < 12 then return "tur_d1" end
		return "tur_dead"
	end
	if e.alive then
		if e.state == ENEMY_STATE_ATTACK then return name .. "_at" end
		if (e.walk // 6) % 2 == 0 then return name .. "_w0" end
		return name .. "_w1"
	end
	if e.anim < 6 then return name .. "_d0" end
	if e.anim < 12 then return name .. "_d1" end
	return name .. "_dead"
end
