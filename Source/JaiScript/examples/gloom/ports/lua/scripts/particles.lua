-- Particle pool: 288 fixed slots of {kind, x, y, z, vx, vy, vz, life},
-- round-robin cursor overwrite, stepped in slot order by gloom_particle.

PART_MAX = 288

ParticlePool = {}
ParticlePool.__index = ParticlePool

function ParticlePool.new()
	local self = setmetatable({pool = {}, cursor = 0}, ParticlePool)
	self:reset()
	return self
end

function ParticlePool:reset()
	local pool = {}
	for i = 1, PART_MAX do
		pool[i] = {0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0}
	end
	self.pool = pool
	self.cursor = 0
end

function ParticlePool:spawn(kind, x, y, z, vx, vy, vz, life)
	local p = self.pool[self.cursor + 1]
	p[1] = kind; p[2] = x; p[3] = y; p[4] = z
	p[5] = vx; p[6] = vy; p[7] = vz; p[8] = life
	self.cursor = (self.cursor + 1) % PART_MAX
end

function ParticlePool:update()
	local pool = self.pool
	local step = gloom_particle
	for i = 1, PART_MAX do step(pool[i]) end
end

-- ------------------------------------------------------------ burst kit -----
-- All velocity spreads come from RNG (the seeded host rng): deterministic,
-- and CALL ORDER is part of the conformance contract.
function rnd_spread(scale)
	return (RNG:nextf() - 0.5) * 2.0 * scale
end

function fx_muzzle(x, y, dirx, diry)
	local fx = G.fx
	local rng = RNG
	local mx = x + dirx * 0.5
	local my = y + diry * 0.5
	fx:spawn(5, mx, my, 0.52, dirx * 2.0, diry * 2.0, 0.2, 3)
	for i = 1, 3 do
		fx:spawn(1, mx, my, 0.5, dirx * 3.0 + rnd_spread(1.6), diry * 3.0 + rnd_spread(1.6), 0.6 + rng:nextf(), 6)
	end
	fx:spawn(4, mx, my, 0.55, dirx * 0.6, diry * 0.6, 0.3, 16)
end

function fx_wall_hit(x, y)
	local fx = G.fx
	local rng = RNG
	for i = 1, 5 do
		fx:spawn(1, x, y, 0.45 + rng:nextf() * 0.2, rnd_spread(2.2), rnd_spread(2.2), 0.8 + rng:nextf() * 1.4, 8)
	end
	fx:spawn(4, x, y, 0.5, rnd_spread(0.3), rnd_spread(0.3), 0.4, 14)
end

function fx_blood(x, y, z, n)
	local fx = G.fx
	local rng = RNG
	for i = 1, n do
		fx:spawn(2, x, y, z + rng:nextf() * 0.25, rnd_spread(1.8), rnd_spread(1.8), 0.5 + rng:nextf() * 1.6, 12)
	end
end

-- death burst: meat + smoke; big enemies gib harder
function fx_gibs(x, y, n)
	local fx = G.fx
	local rng = RNG
	for i = 1, n do
		fx:spawn(3, x, y, 0.35 + rng:nextf() * 0.3, rnd_spread(2.6), rnd_spread(2.6), 1.2 + rng:nextf() * 2.4, 26)
	end
	for i = 1, 3 do
		fx:spawn(4, x, y, 0.4, rnd_spread(0.5), rnd_spread(0.5), 0.5, 20)
	end
end

function fx_explosion(x, y)
	local fx = G.fx
	local rng = RNG
	local cos, sin = math.cos, math.sin
	fx:spawn(5, x, y, 0.5, 0.0, 0.0, 0.0, 5)
	for i = 1, 10 do
		local a = rng:nextf() * 6.2831853
		local sp = 1.5 + rng:nextf() * 3.0
		fx:spawn(1, x, y, 0.3 + rng:nextf() * 0.5, cos(a) * sp, sin(a) * sp, 1.0 + rng:nextf() * 2.0, 11)
	end
	for i = 1, 5 do
		fx:spawn(4, x, y, 0.4, rnd_spread(0.9), rnd_spread(0.9), 0.6 + rng:nextf(), 24)
	end
	for i = 1, 6 do
		fx:spawn(6, x, y, 0.2 + rng:nextf() * 0.6, rnd_spread(1.4), rnd_spread(1.4), 0.5, 20)
	end
end

function fx_door_puff(x, y)
	local fx = G.fx
	local rng = RNG
	for i = 1, 4 do
		fx:spawn(4, x, y, 0.3 + rng:nextf() * 0.5, rnd_spread(0.7), rnd_spread(0.7), 0.3, 18)
	end
end

function fx_secret_glitter(x, y)
	local fx = G.fx
	local rng = RNG
	for i = 1, 8 do
		fx:spawn(6, x, y, 0.2 + rng:nextf() * 0.8, rnd_spread(1.2), rnd_spread(1.2), 0.6, 24)
	end
end

-- ------------------------------------------------------------- colors -------
-- per-kind life-phase ramps (render reads PART_COLS[kind * 4 + phase + 1])
PART_COLS = {}

function build_particle_colors()
	local ramps = {
		{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}},                          -- 0 dead (unused)
		{{255, 240, 160}, {255, 200, 90}, {230, 130, 50}, {140, 70, 30}},      -- 1 spark
		{{210, 40, 30}, {180, 34, 26}, {140, 26, 20}, {95, 18, 14}},           -- 2 blood
		{{200, 60, 44}, {170, 46, 34}, {130, 34, 26}, {90, 24, 18}},           -- 3 gib
		{{130, 126, 122}, {104, 102, 100}, {80, 79, 78}, {56, 56, 57}},        -- 4 smoke
		{{255, 252, 220}, {255, 236, 160}, {255, 190, 90}, {200, 120, 50}},    -- 5 flash
		{{150, 255, 170}, {110, 235, 200}, {90, 190, 250}, {80, 120, 220}}     -- 6 hexmote
	}
	PART_COLS = {}
	local n = 0
	for r = 1, #ramps do
		local ramp = ramps[r]
		for p = 1, 4 do
			local rgb = ramp[p]
			n = n + 1
			PART_COLS[n] = rgb_idx(rgb[1], rgb[2], rgb[3])
		end
	end
end
