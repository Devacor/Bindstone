-- MAELSTROM (Lua 5.4 port of scenes/fluid.jai): neon ink in a slab of black water, simulated
-- twice over and seen in three dimensions. Under the surface runs Jos Stam's stable fluids on a
-- grid a third of the pixel resolution: forces stir it, vorticity confinement hands back the curl
-- the grid smears away, warm-started SOR projects it divergence-free, and the velocity advects
-- itself semi-Lagrangian. The ink is one number a cell on a grid twice as fine, carried by the
-- same flow, picking a place along a row of eight neon tubes; it settles slowly toward a ramp.
--
-- The surface is a height field on the ink's grid obeying the wave equation, struck by rain,
-- blobs of ink on the kick, crowns of spray and the hits; the vortices dish it and the drain
-- pulls it into a funnel. A shift-lens camera casts one ray per column across the water, the
-- slab's cut face hangs from the waterline, and a neon grid floor lies below it all.

local floor, ceil, sqrt, sin, cos, exp, abs, max, min = math.floor, math.ceil, math.sqrt, math.sin, math.cos, math.exp, math.abs, math.max, math.min
local move = table.move

local MS_FU = {}           -- the flow that carries the ink, flow cells per second
local MS_FV = {}
local MS_INK = {}          -- each ink cell's ink, in palette entries
local MS_HOME_X = {}       -- the ink a cell settles back to: its column's share
local MS_HOME_Y = {}       -- plus its row's,
local MS_SETTLE = {}       -- and by whole ink cells of radius from the tank's middle, the share of the way it goes in a frame
local MS_RADIUS = {}       -- per ink cell: that radius
local MS_Z = {}            -- per ink cell: the surface's height above rest, in ink cells
local MS_LIT = {}          -- per ink cell: its brightness level's first palette entry
local MS_GLOW = {}         -- by radius: the brightness level of flat water,
local MS_DUSK = {}         -- and the share of its light the drain's dark leaves it
local MS_INK_ROWS = {}
local MS_COLS = {}
local MS_W = 0
local MS_H = 0
local MS_DW = 0            -- the ink grid
local MS_DH = 0
local MS_CW = 0            -- the flow grid
local MS_CH = 0
local MS_RUN = 0.0         -- a frame's step, in ink cells per (flow cell per second)
local MS_CX = 0.0          -- the tank's middle, in ink cells
local MS_CY = 0.0
local MS_CAM_X = 0.0       -- the camera, in ink cells: where it stands,
local MS_CAM_Y = 0.0
local MS_CAM_H = 1.0       -- how high,
local MS_FACE_X = 1.0      -- the unit vector it faces along the ground,
local MS_FACE_Y = 0.0
local MS_FOCAL = 1.0       -- pixels to a cell one cell away,
local MS_HORIZON = 0.0     -- and the row its eye level falls on
local MS_DEPTH = 1.0       -- the slab's depth
local MS_LIGHT_X = 0.0     -- brightness levels a unit of slope toward the key light takes away
local MS_LIGHT_Y = 0.0
local MS_GLINT_X = 0.0     -- the slope that glints,
local MS_GLINT_Y = 0.0
local MS_GLINT_NARROW = 1.0 -- one over the square of how far from it a slope may stray,
local MS_GLINT = 0.0       -- and the levels it adds
local MS_CREST = 0.0       -- brightness levels a cell of height adds
local MS_TUBES = 0         -- the first palette entry of the tubes as drawn this frame
local MS_PHASE = 0         -- how far along the tubes every cell's ink is shifted
local MS_FLOOR = 0         -- the floor's first palette entry: the dark, then its grid's 8 shades
local MS_GRID = 1.0        -- floor grid lines to an ink cell
local MS_GRID_GAIN = 0.0   -- a line's shade where the camera stands,
local MS_GRID_FADE = 0.0   -- and what a cell of distance takes off it

-- one row of ink: each cell takes the flow at its centre (bilinear over the flow grid, stepped
-- along the row), traces back one step, blends the four cells it lands among and settles toward home
local function maelstrom_ink_row(y)
	local dw = MS_DW
	local out = {}
	for i = 1, dw do out[i] = 0.0 end
	if y < 1 or y >= MS_DH - 1 then return out end
	local fu, fv, ink, home_x, settle, radius = MS_FU, MS_FV, MS_INK, MS_HOME_X, MS_SETTLE, MS_RADIUS
	local gw = MS_CW
	local gymax = MS_CH - 1.001
	local xmax = dw - 2.001
	local ymax = MS_DH - 2.001
	local run = MS_RUN
	local home_y = MS_HOME_Y[y + 1]
	local qy = y * 0.5 - 0.25
	qy = qy > gymax and gymax or qy
	local gy = floor(qy)
	local ty = qy - gy
	local g = gy * gw + 1
	local ua = fu[g] + (fu[g + gw] - fu[g]) * ty
	local va = fv[g] + (fv[g + gw] - fv[g]) * ty
	local x = 1
	local radius_at = y * dw + 1
	for gx = 1, gw - 1 do
		g = g + 1
		local ub = fu[g] + (fu[g + gw] - fu[g]) * ty
		local vb = fv[g] + (fv[g + gw] - fv[g]) * ty
		local du = (ub - ua) * 0.5
		local dv = (vb - va) * 0.5
		local u = ua + du * 0.5
		local v = va + dv * 0.5
		for xx = x, x + 1 do
			local px = xx - run * u
			local py = y - run * v
			px = px < 1.0 and 1.0 or (px > xmax and xmax or px)
			py = py < 1.0 and 1.0 or (py > ymax and ymax or py)
			local ix = floor(px)
			local iy = floor(py)
			local sx = px - ix
			local k = iy * dw + ix + 1
			local k2 = k + dw
			local a = ink[k]
			local b = ink[k2]
			local top = a + (ink[k + 1] - a) * sx
			local bottom = b + (ink[k2 + 1] - b) * sx
			local here = top + (bottom - top) * (py - iy)
			out[xx + 1] = here + (home_y + home_x[xx + 1] - here) * settle[radius[radius_at + xx] + 1]
			u = u + du
			v = v + dv
		end
		x = x + 2
		ua = ub
		va = vb
	end
	return out
end

-- one row of the surface's light: each cell's level from its slope (the key light's share, and
-- the glint) and its height, at the glow of its radius
local function maelstrom_light_row(y)
	local dw = MS_DW
	local out = {}
	for i = 1, dw do out[i] = 0 end
	local row = y < 1 and 1 or (y > MS_DH - 2 and MS_DH - 2 or y)
	local z, radius_of, glow, dusk = MS_Z, MS_RADIUS, MS_GLOW, MS_DUSK
	local light_x = MS_LIGHT_X
	local light_y = MS_LIGHT_Y
	local glint_x = MS_GLINT_X
	local glint_y = MS_GLINT_Y
	local narrow = MS_GLINT_NARROW
	local glint = MS_GLINT
	local crest = MS_CREST
	local k = row * dw + 2
	for x = 2, dw - 1 do
		local sx = z[k + 1] - z[k - 1]
		local sy = z[k + dw] - z[k - dw]
		local radius = radius_of[k] + 1
		local lift = z[k] * crest
		lift = lift < -2.5 and -2.5 or (lift > 3.0 and 3.0 or lift)
		local level = glow[radius] + lift - sx * light_x - sy * light_y
		local ex = sx - glint_x
		local ey = sy - glint_y
		local near = 1.0 - (ex * ex + ey * ey) * narrow
		if near > 0.0 then level = level + near * near * glint end
		if level < 2.0 then level = 2.0 end
		local whole = floor(level * dusk[radius])
		if whole > 15 then whole = 15 end
		out[x] = whole * 128
		k = k + 1
	end
	out[1] = out[2]
	out[dw] = out[dw - 1]
	return out
end

-- one column of the picture: its ray across the ground is clipped to the tank; where it enters,
-- the slab's cut face hangs from the waterline; then the ray walks the surface front to back, a
-- step a row, and each sample that rises above everything nearer fills the rows it adds. The rows
-- nothing covered show the floor and its grid.
local function maelstrom_column(x)
	local hh = MS_H
	local dw = MS_DW
	local tubes = MS_TUBES
	local phase = MS_PHASE
	local floor_at = MS_FLOOR
	local out = {}
	for i = 1, hh do out[i] = floor_at end
	local focal = MS_FOCAL
	local camx = MS_CAM_X
	local camy = MS_CAM_Y
	local camh = MS_CAM_H
	local hz = MS_HORIZON
	local aside = (x + 0.5 - MS_W * 0.5) / focal
	local dx = MS_FACE_X - aside * MS_FACE_Y
	local dy = MS_FACE_Y + aside * MS_FACE_X
	local xhi = dw - 2.01
	local yhi = MS_DH - 2.01
	local t0 = 1.0
	local t1 = 1000000.0
	local face = 7.0
	if dx > 0.00001 then
		t0 = max(t0, (1.0 - camx) / dx)
		t1 = min(t1, (xhi - camx) / dx)
	elseif dx < -0.00001 then
		t0 = max(t0, (xhi - camx) / dx)
		t1 = min(t1, (1.0 - camx) / dx)
	elseif camx < 1.0 or camx > xhi then
		t1 = 0.0
	end
	local across0 = 1.0
	if dy > 0.00001 then
		across0 = (1.0 - camy) / dy
		t1 = min(t1, (yhi - camy) / dy)
	elseif dy < -0.00001 then
		across0 = (yhi - camy) / dy
		t1 = min(t1, (1.0 - camy) / dy)
	elseif camy < 1.0 or camy > yhi then
		t1 = 0.0
	end
	if across0 > t0 then
		t0 = across0
		face = 5.0
	end
	local ytop = hh
	local ybase = hh
	if t1 > t0 then
		local z, ink, lit = MS_Z, MS_INK, MS_LIT
		local px0 = camx + t0 * dx
		local py0 = camy + t0 * dy
		px0 = px0 < 1.0 and 1.0 or (px0 > xhi and xhi or px0)
		py0 = py0 < 1.0 and 1.0 or (py0 > yhi and yhi or py0)
		local ix0 = floor(px0)
		local iy0 = floor(py0)
		local k0 = iy0 * dw + ix0 + 1
		local scale0 = focal / t0
		local waterline = hz + (camh - z[k0]) * scale0
		local foot = hz + (camh + MS_DEPTH) * scale0
		local edge_ink = ink[k0]
		edge_ink = edge_ink >= 0.0 and floor(edge_ink) or ceil(edge_ink)
		edge_ink = tubes + ((edge_ink + phase) & 127)
		local dusk = MS_DUSK[MS_RADIUS[k0] + 1]
		local level = face * dusk
		local fall = (level - dusk) / (foot - waterline + 0.001)
		local ya = 0
		if waterline > 0.0 then
			ya = floor(waterline)
		else
			level = level + fall * waterline
		end
		if ya > hh then ya = hh end
		if foot < hh then ybase = foot < 0.0 and 0 or floor(foot) end
		if ybase < ya then ybase = ya end
		for yf = ya + 1, ybase do
			local whole = level >= 0.0 and floor(level) or ceil(level)
			out[yf] = edge_ink + whole * 128
			level = level - fall
		end
		if ya < ybase then
			local meniscus = floor(12.0 * dusk)
			out[ya + 1] = edge_ink + meniscus * 128
		end
		ytop = ya
		local t = t0
		local row_step = 1.0 / (camh * focal)
		-- the clip keeps px and py at 1 or more, so floor is the truncation
		while t < t1 and ytop > 0 do
			local px = camx + t * dx
			local py = camy + t * dy
			local ix = floor(px)
			local iy = floor(py)
			local sx = px - ix
			local sy = py - iy
			local k = iy * dw + ix + 1
			local k2 = k + dw
			local za = z[k]
			local zb = z[k2]
			local ztop = za + (z[k + 1] - za) * sx
			local zbot = zb + (z[k2 + 1] - zb) * sx
			local row = hz + (camh - ztop - (zbot - ztop) * sy) * focal / t
			if row < ytop then
				local ys = row < 0.0 and 0 or floor(row)
				if ys < ytop then
					local a = ink[k]
					local b = ink[k2]
					local top = a + (ink[k + 1] - a) * sx
					local bottom = b + (ink[k2 + 1] - b) * sx
					local here = top + (bottom - top) * sy
					here = here >= 0.0 and floor(here) or ceil(here)
					local pix = tubes + lit[k] + ((here + phase) & 127)
					for yy = ys + 1, ytop do out[yy] = pix end
					ytop = ys
				end
			end
			local stride = t * t * row_step
			t = t + (stride < 0.35 and 0.35 or (stride > 1.0 and 1.0 or stride))
		end
	end
	local floor_k = (camh + MS_DEPTH) * focal
	local lines = MS_GRID
	local line_wide = lines * 1.2 / focal
	local gain = MS_GRID_GAIN
	local fade = MS_GRID_FADE
	local from = 0
	local to = ytop
	for pass = 0, 1 do
		for y = from, to - 1 do
			local under = y + 0.5 - hz
			if under >= 1.0 then
				local far = floor_k / under
				local shade = gain - far * fade
				if shade >= 1.0 then
					local wide = far * line_wide
					local gx = (camx + far * dx) * lines + 4096.0
					local gy = (camy + far * dy) * lines + 4096.0
					local wx = gx >= 0.0 and floor(gx) or ceil(gx)
					local wy = gy >= 0.0 and floor(gy) or ceil(gy)
					if gx - wx < wide or gy - wy < wide then
						local sh = floor(shade)
						out[y + 1] = floor_at + (sh > 8 and 8 or sh)
					end
				end
			end
		end
		from = ybase
		to = hh
	end
	return out
end

Maelstrom = setmetatable({}, {__index = Scene})
Maelstrom.__index = Maelstrom

function Maelstrom.new()
	local self = Scene.init(setmetatable({}, Maelstrom))
	self.cw = 0                -- the flow grid: cw x ch cells, three pixels to a cell
	self.ch = 0
	self.dw = 0                -- the ink grid: two cells to a flow cell
	self.dh = 0
	self.unit = 1.0            -- ink cells to one of the reference tank's (80 rows)
	self.u = {}                -- the flow, flow cells per second,
	self.v = {}
	self.u1 = {}               -- and advected
	self.v1 = {}
	self.pr = {}               -- pressure, kept: each solve starts from the last one
	self.dv = {}               -- divergence
	self.cu = {}               -- curl
	self.away_x = {}           -- per flow cell: the unit vector away from the tank's middle,
	self.away_y = {}
	self.away_r = {}           -- and its radius in whole ink cells
	self.radial = {}           -- by that radius: the speed outward the rings and the drain give the ink
	self.ring_born = {}
	self.ring_power = {}
	self.ring_lays = {}        -- 1 for a shock whose wake lays the ink back in its bands
	self.ring_next = 0
	self.hw = {}               -- the waves: each ink cell's height above rest,
	self.hv = {}               -- and how fast it is rising, ink cells a second
	self.dish_pair = {}        -- per ink cell: the share of a vortex's dimple it sits in, the pair's
	self.dish_pool = {}        -- and the whirlpool's
	self.drop_x = {}           -- what is in the air, ink cells and ink cells a second
	self.drop_y = {}
	self.drop_z = {}
	self.drop_vx = {}
	self.drop_vy = {}
	self.drop_vz = {}
	self.drop_size = {}        -- a blob's radius; spray and rain have none
	self.drop_kind = {}        -- -1 none, 0 spray, 1 rain, 2 a blob of ink
	self.drop_entry = {}       -- its colour, along the tubes
	self.drop_next = 0
	self.neon = {              -- the tubes: red, green and blue light each
		1.00, 0.08, 0.55,
		0.00, 0.80, 1.00,
		1.00, 0.84, 0.04,
		0.58, 0.20, 1.00,
		0.55, 1.00, 0.00,
		1.00, 0.38, 0.00,
		0.10, 0.35, 1.00,
		0.00, 1.00, 0.50
	}
	self.tubes = 0             -- the palette's 128 entries along the tubes, at 16 brightness levels, then all of it negative
	self.floor_at = 0          -- then the floor: the dark and 8 shades of grid line, and their negative
	self.jets = 5
	self.sor_iters = 8
	self.vorticity = 12.0
	self.jet_speed = 14.0      -- the opening's jets
	self.settling = 0.18       -- the share of the way home the ink settles in a second,
	self.pool_settling = 0.6   -- and in the whirlpool, where home turns with the water
	self.home_rate = 1.4       -- this many radians a second
	self.home_turned = 0.0
	self.pair_speed = 16.0     -- how fast each vortex turns at its core's edge, flow cells a second
	self.pool_speed = 18.0
	self.drain_speed = 14.0
	self.wave_speed = 1.0      -- ink cells a second: the tank's height in 2 s
	self.wave_drag = 1.0       -- ink cells squared a second: what smooths the chop out of the waves
	self.gravity = 1.0         -- ink cells a second each second
	self.fall_lead = 0.55      -- a blob is let go this long before the kick it lands on,
	self.fall_speed = 1.0      -- already falling this fast
	self.tt = 0.0
	self.turn = 1.0            -- the way the whirlpool winds
	self.orbit = 0.0           -- where the nozzles' ring has turned to
	self.cam_yaw = -0.6        -- where the camera's swing has turned to
	self.traded = 0            -- the tubes' colours, moved along by the reversal
	self.kick_before = 0.0
	self.kick_ahead_before = 0.0
	self.snare_before = 0.0
	self.rain_due = 0.0
	self.blob_tube = 0
	self.blob_turn = 0
	self.spout_turn = 0
	self.paddle_turn = 0
	self.t_pair = MUSIC_BAR * 2.0
	self.t_pool = MUSIC_BAR * 4.0
	self.t_flip = MUSIC_BAR * 6.0
	self.t_drain = MUSIC_BAR * 8.0
	-- the reel picks the grid before build runs, so the budget is set at construction
	self.pixel_budget = 30000
	return self
end

function Maelstrom:length_bars() return 10 end
function Maelstrom:hit_times() return {self.t_pair, self.t_pool, self.t_flip, self.t_drain} end
function Maelstrom:hit_levels() return {1, 2, 1, 2} end

function Maelstrom:build()
	self.name = "MAELSTROM"
	self.handle = "stable fluids under a wave tank . neon ink"
	self.replay = 20.0   -- the water is its whole history
	local w, h = self.w, self.h
	local cw = (w + 2) // 3
	local ch = (h + 2) // 3
	local dw = cw * 2
	local dh = ch * 2
	self.cw = cw
	self.ch = ch
	self.dw = dw
	self.dh = dh
	local unit = dh / 80.0
	self.unit = unit
	self.wave_speed = dh / 2.0
	self.wave_drag = 2.0 * unit * unit
	self.gravity = 120.0 * unit
	self.fall_speed = 30.0 * unit
	local cn = cw * ch
	local dn = dw * dh
	self.u = gfx:make_fgrid(cn)
	self.v = gfx:make_fgrid(cn)
	self.u1 = gfx:make_fgrid(cn)
	self.v1 = gfx:make_fgrid(cn)
	self.pr = gfx:make_fgrid(cn)
	self.dv = gfx:make_fgrid(cn)
	self.cu = gfx:make_fgrid(cn)
	self.hw = gfx:make_fgrid(dn)
	self.hv = gfx:make_fgrid(dn)
	MS_W = w
	MS_H = h
	MS_DW = dw
	MS_DH = dh
	MS_CW = cw
	MS_CH = ch
	MS_CX = dw * 0.5 - 0.5
	MS_CY = dh * 0.5 - 0.5
	MS_DEPTH = 10.0 * unit
	MS_GRID = 4.0 / dh
	MS_INK = gfx:make_fgrid(dn)
	MS_Z = gfx:make_fgrid(dn)
	MS_LIT = gfx:make_grid(dn)
	MS_HOME_X = gfx:make_fgrid(dw)
	MS_HOME_Y = gfx:make_fgrid(dh)
	MS_INK_ROWS = {}
	for r = 0, dh - 1 do MS_INK_ROWS[r + 1] = r end
	MS_COLS = {}
	for c = 0, w - 1 do MS_COLS[c + 1] = c end
	self:mix_tubes()
	local reach = itrunc(sqrt(dw * dw + dh * dh * 1.0) * 0.5) + 3
	MS_SETTLE = gfx:make_fgrid(reach)
	MS_GLOW = gfx:make_fgrid(reach)
	MS_DUSK = gfx:make_fgrid(reach)
	self.radial = gfx:make_fgrid(reach)
	MS_RADIUS = gfx:make_grid(dn)
	local dish_pair = gfx:make_fgrid(dn)
	local dish_pool = gfx:make_fgrid(dn)
	self.dish_pair = dish_pair
	self.dish_pool = dish_pool
	local pair_core = 5.0 * unit
	local pool_core = 8.0 * unit
	for y = 0, dh - 1 do
		local ey = y + 0.5 - dh * 0.5
		for x = 0, dw - 1 do
			local ex = x + 0.5 - dw * 0.5
			local left = ex + dw * 0.23
			local right = ex - dw * 0.23
			local at = y * dw + x + 1
			MS_RADIUS[at] = itrunc(sqrt(ex * ex + ey * ey))
			dish_pool[at] = 1.0 / (1.0 + (ex * ex + ey * ey) / (pool_core * pool_core))
			dish_pair[at] = 1.0 / (1.0 + (left * left + ey * ey) / (pair_core * pair_core))
				+ 1.0 / (1.0 + (right * right + ey * ey) / (pair_core * pair_core))
		end
	end
	local away_x = gfx:make_fgrid(cn)
	local away_y = gfx:make_fgrid(cn)
	local away_r = gfx:make_grid(cn)
	self.away_x = away_x
	self.away_y = away_y
	self.away_r = away_r
	for gy = 0, ch - 1 do
		local fy = gy - (ch - 1) * 0.5
		for gx = 0, cw - 1 do
			local fx = gx - (cw - 1) * 0.5
			local far = sqrt(fx * fx + fy * fy)
			local i = gy * cw + gx + 1
			away_x[i] = fx / (far + 0.0001)
			away_y[i] = fy / (far + 0.0001)
			away_r[i] = itrunc(far * 2.0)
		end
	end
	local ring_born, ring_power, ring_lays = self.ring_born, self.ring_power, self.ring_lays
	for k = 1, 8 do
		ring_born[#ring_born + 1] = -100.0
		ring_power[#ring_power + 1] = 0.0
		ring_lays[#ring_lays + 1] = 0.0
	end
	for d = 1, 192 do
		local n = #self.drop_kind + 1
		self.drop_x[n] = 0.0
		self.drop_y[n] = 0.0
		self.drop_z[n] = 0.0
		self.drop_vx[n] = 0.0
		self.drop_vy[n] = 0.0
		self.drop_vz[n] = 0.0
		self.drop_size[n] = 0.0
		self.drop_kind[n] = -1
		self.drop_entry[n] = 0
	end
	self:lay_bands()
	for settle = 0, 11 do self:project() end
	self:carry_ink(0.0, 0.0)
	self:shape_surface(0.0)
end

-- the palette: eight tubes of sixteen entries each, a tube's light swelling from the dark water
-- at its edges to a white-hot core; sixteen levels, from black through full light to the water
-- burning white; then the negative of every entry; then the floor's dark and its grid lines, and
-- their negative
function Maelstrom:mix_tubes()
	local pal = self.pal
	local neon = self.neon
	self.tubes = pal:size()
	local gain = {0.0, 0.08, 0.16, 0.25, 0.35, 0.46, 0.58, 0.72, 0.86, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0}
	local glare = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.12, 0.26, 0.42, 0.6, 0.8, 1.0}
	for negative = 0, 1 do
		for shade = 1, 16 do
			for e = 0, 127 do
				local hue = (e // 16) * 3 + 1
				local off = ((e % 16) + 0.5) / 16.0 - 0.5
				local glow = 1.0 - off * off / 0.1156
				glow = glow < 0.0 and 0.0 or glow
				local core = glow > 0.8 and (glow - 0.8) * 2.2 or 0.0
				local light = 0.05 + 0.95 * glow
				local white = core + (1.0 - core) * glare[shade] * (0.3 + 0.7 * glow)
				local r = 3.0 + 252.0 * neon[hue] * light
				local g = 8.0 + 247.0 * neon[hue + 1] * light
				local b = 24.0 + 231.0 * neon[hue + 2] * light
				r = (r + (255.0 - r) * white) * gain[shade]
				g = (g + (255.0 - g) * white) * gain[shade]
				b = (b + (255.0 - b) * white) * gain[shade]
				if negative == 1 then
					pal:add(255.0 - r, 255.0 - g, 255.0 - b)
				else
					pal:add(r, g, b)
				end
			end
		end
	end
	self.floor_at = pal:size()
	for negative2 = 0, 1 do
		for line = 0, 8 do
			local lit = line / 8.0
			local r2 = 90.0 * lit
			local g2 = 14.0 * lit
			local b2 = 5.0 + 125.0 * lit
			if negative2 == 1 then
				pal:add(255.0 - r2, 255.0 - g2, 255.0 - b2)
			else
				pal:add(r2, g2, b2)
			end
		end
	end
end

-- the ramp the ink settles back to: seven tubes to the tank's height, deepening along the unit vector (across, down)
function Maelstrom:lay_home(across, down)
	local dw, dh = self.dw, self.dh
	local home_x, home_y = MS_HOME_X, MS_HOME_Y
	local cx, cy = MS_CX, MS_CY
	local tube = 112.0 / dh
	for x = 0, dw - 1 do home_x[x + 1] = (x - cx) * across * tube end
	for y = 0, dh - 1 do home_y[y + 1] = 1080.0 + (y - cy) * down * tube end
end

-- the opening: the ink in its bands, across jets that run against their neighbours, their seams
-- already rippling so the shear rolls them up
function Maelstrom:lay_bands()
	self:lay_home(0.0, 1.0)
	local dw, dh, cw, ch, jets = self.dw, self.dh, self.cw, self.ch, self.jets
	local ink = MS_INK
	for y = 0, dh - 1 do
		local i = y * dw + 1
		local home = MS_HOME_Y[y + 1]
		for x = 0, dw - 1 do
			ink[i] = home
			i = i + 1
		end
	end
	local rng = self.rng
	local phase = {}
	for s = 0, jets do
		phase[#phase + 1] = rng:nextf() * 6.2831853
		phase[#phase + 1] = rng:nextf() * 6.2831853
	end
	local u, v = self.u, self.v
	local jet_speed, unit = self.jet_speed, self.unit
	for gy = 1, ch - 2 do
		local across = (gy + 0.5) * jets / ch
		local seam_at = itrunc(across + 0.5)
		local ripple = cos(3.14159265 * across)
		ripple = (seam_at == 0 or seam_at == jets) and 0.0 or ripple * ripple * ripple * ripple
		for gx = 1, cw - 2 do
			local j = gy * cw + gx + 1
			u[j] = self:band_jet(gy)
			local along = gx * 6.2831853 / cw
			v[j] = jet_speed * unit * ripple * (0.3 * sin(along * 5.0 + phase[seam_at * 2 + 1]) + 0.2 * sin(along * 3.0 + phase[seam_at * 2 + 2]))
		end
	end
end

-- a flow row's place in the opening's jets: alternate ways, shearing at the seams
function Maelstrom:band_jet(gy)
	local wave = sin(3.14159265 * (gy + 0.5) * self.jets / self.ch) * 3.0
	local steep = exp(wave)
	return self.jet_speed * self.unit * (steep - 1.0 / steep) / (steep + 1.0 / steep)
end

function Maelstrom:smooth01(x)
	local c = x < 0.0 and 0.0 or (x > 1.0 and 1.0 or x)
	return c * c * (3.0 - 2.0 * c)
end

function Maelstrom:kick_at(t)
	local cue = self.cue
	if cue == nil then return 0.0 end
	return cue:kick_env(t)
end

function Maelstrom:snare_at(t)
	local cue = self.cue
	if cue == nil then return 0.0 end
	return cue:snare_env(t)
end

function Maelstrom:hit_at(t)
	local cue = self.cue
	if cue == nil then return 0.0 end
	return cue:hit(t)
end

function Maelstrom:update(t, dt)
	local before = self.tt
	self.tt = t
	if dt <= 0.0 then return end
	self.vorticity = 9.0 + 9.0 * (self.cue == nil and 0.6 or self.cue:energy(t))
	local step = dt > 0.05 and 0.05 or dt
	self:strike(before, t)
	self:stir(t, step)
	self:confine(step)
	self:project()
	self:advect_flow(step)
	self:carry_ink(t, step)
	self:throw_water(before, t, step)
	self:ripple(step)
	self:fall(step)
	self.cam_yaw = self.cam_yaw + self.turn * (0.35 + 0.5 * self:smooth01((t - self.t_drain) / (MUSIC_BAR * 2.0))) * step
	self:shape_surface(t)
end

-- the hits, once each as the clock crosses them: a ring of light for the pair, and a shock for
-- the whirlpool, the reversal (the whole flow turning back on itself, the tank sloshing, every
-- tube trading its colour for the next) and the drain, whose funnel plunges; and each kick, a
-- wave: off a side wall, then out of the whirlpool's eye
function Maelstrom:strike(before, t)
	local unit = self.unit
	if before < self.t_pair and t >= self.t_pair then self:ring(self.t_pair, 0.8, 0.0) end
	if before < self.t_pool and t >= self.t_pool then self:ring(self.t_pool, 1.0, 1.0) end
	if before < self.t_flip and t >= self.t_flip then
		self:ring(self.t_flip, 1.0, 1.0)
		self.turn = 0.0 - self.turn
		self.traded = self.traded + 16
		local u, v, pr = self.u, self.v, self.pr
		for i = 1, self.cw * self.ch do
			u[i] = 0.0 - u[i]
			v[i] = 0.0 - v[i]
			pr[i] = 0.0 - pr[i]
		end
		local vel = self.hv
		local dw, dh = self.dw, self.dh
		local cx = MS_CX
		local tilt = 12.0 * unit * self.turn / (dw * 0.5)
		for y = 1, dh - 2 do
			local j = y * dw + 2
			for x = 1, dw - 2 do
				vel[j] = vel[j] + (x - cx) * tilt
				j = j + 1
			end
		end
	end
	if before < self.t_drain and t >= self.t_drain then
		self:ring(self.t_drain, 1.0, 1.0)
		self:splash(MS_CX, MS_CY, 12.0 * unit, 9.0 * unit)
		self:crown(MS_CX, MS_CY, 22, 4.0 * unit, 70.0 * unit, 5)
	end
	local kick = self:kick_at(t)
	if kick > self.kick_before + 0.3 then
		if t > self.t_pool then
			self:ring(t, 0.3 * kick, 0.0)
			self:bump(MS_CX, MS_CY, 45.0 * unit * kick, 7.0 * unit)
		else
			self:paddle(45.0 * unit * kick)
		end
	end
	self.kick_before = kick
end

-- a side wall shoves the water beside it up and the water beyond that down, the left wall and
-- the right in turn: a wave as long as the tank sets out across it
function Maelstrom:paddle(speed)
	local vel = self.hv
	local dw, dh = self.dw, self.dh
	self.paddle_turn = 1 - self.paddle_turn
	local paddle_turn = self.paddle_turn
	for y = 1, dh - 2 do
		local row = y * dw + 1
		for k = 0, 7 do
			local x = 1 + k
			if paddle_turn == 1 then x = dw - 2 - k end
			vel[row + x] = vel[row + x] + speed * cos(0.44879895 * k)
		end
	end
end

function Maelstrom:ring(born, power, lays)
	local at = self.ring_next + 1
	self.ring_born[at] = born
	self.ring_power[at] = power
	self.ring_lays[at] = lays
	self.ring_next = (self.ring_next + 1) % #self.ring_born
end

-- the choreography, in ink cells: the opening's jets, then a counter-rotating pair with a rod
-- stirring between them, then one whirlpool (home turning with it) that the drain tightens, six
-- nozzles orbiting from the pair on
function Maelstrom:stir(t, step)
	local cx = MS_CX
	local cy = MS_CY
	local kick = self:kick_at(t)
	local snare = self:snare_at(t)
	if t < self.t_pair then
		self:drive_jets((0.5 + 1.6 * kick) * step)
		return
	end
	local unit, dw, dh, ch, turn = self.unit, self.dw, self.dh, self.ch, self.turn
	local drain = self:smooth01((t - self.t_drain) / (MUSIC_BAR * 1.5))
	if t >= self.t_pool then
		local pool = self:smooth01((t - self.t_pool) / MUSIC_BEAT)
		self.home_turned = self.home_turned + turn * self.home_rate * (1.0 + drain) * pool * step
		self:lay_home(0.0 - sin(self.home_turned), cos(self.home_turned))
		self:whirl(cx, cy, turn * unit * (self.pool_speed + self.drain_speed * drain) * pool, ch * (0.26 - 0.1 * drain), (4.0 + 2.0 * drain) * step)
	else
		local pair = self:smooth01((t - self.t_pair) / MUSIC_BEAT)
		self:whirl(cx - dw * 0.23, cy, 0.0 - self.pair_speed * unit * pair, ch * 0.2, 3.5 * step)
		self:whirl(cx + dw * 0.23, cy, self.pair_speed * unit * pair, ch * 0.2, 3.5 * step)
		local a = t * 1.3
		local rod_x = cx + dw * 0.3 * sin(a)
		local rod_y = cy + dh * 0.27 * sin(a * 2.0)
		self:drag(rod_x, rod_y, dw * 0.195 * cos(a), dh * 0.351 * cos(a * 2.0), 4.0 * unit)
		self:bump(rod_x, rod_y, 0.0 - 90.0 * unit * step, 3.5 * unit)
	end
	local rush = self.cue == nil and 0.0 or self.cue:riser(t)
	self.orbit = self.orbit + turn * (0.55 + 1.2 * rush * rush + 1.4 * drain) * step
	local orbit = self.orbit
	local reach = 1.0 - 0.5 * drain
	for k = 0, 5 do
		local a2 = orbit + k * 1.0471976
		local ca = cos(a2)
		local sa = sin(a2)
		local pours = k % 2 == 0
		local beat = pours and kick or snare
		local aim = pours and 0.55 or 1.4
		local speed = (5.0 + 22.0 * beat) * unit / sqrt(1.0 + aim * aim)
		local x = cx + ca * dw * 0.37 * reach
		local y = cy + sa * dh * 0.37 * reach
		self:drag(x, y, (0.0 - sa * turn - ca * aim) * speed, (ca * turn - sa * aim) * speed, 3.5 * unit)
		self:pour(x, y, (pours and 1.0 or -1.0) * (12.0 + 80.0 * beat) * step, 4.5 * unit)
	end
end

-- the opening's jets pull the flow back toward their shear
function Maelstrom:drive_jets(grip)
	local u, cw, ch = self.u, self.cw, self.ch
	for gy = 1, ch - 2 do
		local jet = self:band_jet(gy)
		local i = gy * cw + 2
		for gx = 1, cw - 2 do
			u[i] = u[i] + (jet - u[i]) * grip
			i = i + 1
		end
	end
end

-- a disc of `rad` ink cells about ink cell (x, y) pulls the flow toward (vx, vy), hardest at its centre
function Maelstrom:drag(x, y, vx, vy, rad)
	local u, v, cw, ch = self.u, self.v, self.cw, self.ch
	local qx = x * 0.5 - 0.25
	local qy = y * 0.5 - 0.25
	local qr = rad * 0.5 + 0.75
	local qr2 = qr * qr
	local x0 = max(1, itrunc(qx - qr))
	local x1 = min(cw - 2, itrunc(qx + qr) + 1)
	local y0 = max(1, itrunc(qy - qr))
	local y1 = min(ch - 2, itrunc(qy + qr) + 1)
	for gy = y0, y1 do
		local ey = gy - qy
		for gx = x0, x1 do
			local ex = gx - qx
			local q = ex * ex + ey * ey
			if q < qr2 then
				local f = (1.0 - q / qr2) * 0.6
				local i = gy * cw + gx + 1
				u[i] = u[i] + (vx - u[i]) * f
				v[i] = v[i] + (vy - v[i]) * f
			end
		end
	end
end

-- ink poured into (or, negative, drawn out of) a disc about ink cell (x, y), most at its centre
function Maelstrom:pour(x, y, amount, rad)
	local ink, dw, dh = MS_INK, self.dw, self.dh
	local r2 = rad * rad
	local x0 = max(1, itrunc(x - rad))
	local x1 = min(dw - 2, itrunc(x + rad) + 1)
	local y0 = max(1, itrunc(y - rad))
	local y1 = min(dh - 2, itrunc(y + rad) + 1)
	for py = y0, y1 do
		local ey = py - y
		for px = x0, x1 do
			local ex = px - x
			local q = ex * ex + ey * ey
			if q < r2 then
				local fall = 1.0 - q / r2
				local i = py * dw + px + 1
				ink[i] = ink[i] + amount * fall * fall
			end
		end
	end
end

-- a vortex at ink cell (x, y): the flow is pulled toward a swirl about it that turns `speed`
-- flow cells a second at `core` cells out and falls away beyond
function Maelstrom:whirl(x, y, speed, core, grip)
	local u, v, cw, ch = self.u, self.v, self.cw, self.ch
	local qx = x * 0.5 - 0.25
	local qy = y * 0.5 - 0.25
	local c2 = core * core
	local k = 2.0 * speed / core
	for gy = 1, ch - 2 do
		local ey = gy - qy
		local i = gy * cw + 2
		for gx = 1, cw - 2 do
			local ex = gx - qx
			local fall = 1.0 / (1.0 + (ex * ex + ey * ey) / c2)
			local pull = grip * fall
			u[i] = u[i] + (0.0 - ey * k * fall - u[i]) * pull
			v[i] = v[i] + (ex * k * fall - v[i]) * pull
			i = i + 1
		end
	end
end

-- vorticity confinement: push along N x curl, N the unit gradient of |curl|
function Maelstrom:confine(step)
	local uu = self.u
	local vv = self.v
	local c = self.cu
	local gw = self.cw
	local gh = self.ch
	for y = 1, gh - 2 do
		local i = y * gw + 2
		for x = 1, gw - 2 do
			c[i] = 0.5 * (vv[i + 1] - vv[i - 1] - uu[i + gw] + uu[i - gw])
			i = i + 1
		end
	end
	local eps = self.vorticity * step
	for y2 = 2, gh - 3 do
		local j = y2 * gw + 3
		for x2 = 2, gw - 3 do
			local cr = c[j + 1]
			local cl = c[j - 1]
			local cd = c[j + gw]
			local cn = c[j - gw]
			local gx = (cr < 0.0 and -cr or cr) - (cl < 0.0 and -cl or cl)
			local gy = (cd < 0.0 and -cd or cd) - (cn < 0.0 and -cn or cn)
			local f = eps * c[j] / (sqrt(gx * gx + gy * gy) + 0.00001)
			uu[j] = uu[j] + gy * f
			vv[j] = vv[j] - gx * f
			j = j + 1
		end
	end
end

-- make the flow divergence-free: solve laplace(p) = div(u) by successive over-relaxation from last
-- frame's pressure (walls are Neumann: a border cell copies its inner neighbour), then subtract grad(p)
function Maelstrom:project()
	local uu = self.u
	local vv = self.v
	local p = self.pr
	local dvg = self.dv
	local gw = self.cw
	local gh = self.ch
	for y = 1, gh - 2 do
		local i = y * gw + 2
		for x = 1, gw - 2 do
			dvg[i] = 0.5 * (uu[i + 1] - uu[i - 1] + vv[i + gw] - vv[i - gw])
			i = i + 1
		end
	end
	local last = (gh - 1) * gw
	for it = 1, self.sor_iters do
		for y2 = 1, gh - 2 do
			local j = y2 * gw + 2
			for x2 = 1, gw - 2 do
				local g = (p[j - 1] + p[j + 1] + p[j - gw] + p[j + gw] - dvg[j]) * 0.25
				p[j] = p[j] + 1.7 * (g - p[j])
				j = j + 1
			end
		end
		for x3 = 1, gw do
			p[x3] = p[x3 + gw]
			p[last + x3] = p[last - gw + x3]
		end
		for y3 = 0, gh - 1 do
			local r = y3 * gw + 1
			p[r] = p[r + 1]
			p[r + gw - 1] = p[r + gw - 2]
		end
	end
	for y4 = 1, gh - 2 do
		local k = y4 * gw + 2
		for x4 = 1, gw - 2 do
			uu[k] = uu[k] - 0.5 * (p[k + 1] - p[k - 1])
			vv[k] = vv[k] - 0.5 * (p[k + gw] - p[k - gw])
			k = k + 1
		end
	end
end

-- semi-Lagrangian: each cell traces its velocity back one step and takes the bilinear blend of
-- the four cells it lands among; the walls hold still and the water slowly calms
function Maelstrom:advect_flow(step)
	local uu = self.u
	local vv = self.v
	local un = self.u1
	local vn = self.v1
	local gw = self.cw
	local gh = self.ch
	local xmax = gw - 1.001
	local ymax = gh - 1.001
	local keep = 1.0 - 0.15 * step
	for y = 1, gh - 2 do
		local i = y * gw + 2
		for x = 1, gw - 2 do
			local px = x - step * uu[i]
			local py = y - step * vv[i]
			px = px < 0.0 and 0.0 or (px > xmax and xmax or px)
			py = py < 0.0 and 0.0 or (py > ymax and ymax or py)
			local ix = floor(px)
			local iy = floor(py)
			local sx = px - ix
			local sy = py - iy
			local k = iy * gw + ix + 1
			local top = uu[k] + (uu[k + 1] - uu[k]) * sx
			local bot = uu[k + gw] + (uu[k + gw + 1] - uu[k + gw]) * sx
			un[i] = (top + (bot - top) * sy) * keep
			local top2 = vv[k] + (vv[k + 1] - vv[k]) * sx
			local bot2 = vv[k + gw] + (vv[k + gw + 1] - vv[k + gw]) * sx
			vn[i] = (top2 + (bot2 - top2) * sy) * keep
			i = i + 1
		end
	end
	-- the original assigns by value: u and u1 stay two arrays
	move(un, 1, #un, 1, uu)
	move(vn, 1, #vn, 1, vv)
end

-- what the tank's middle sends out, by radius: every ring's lit crest, dark wake and shove
-- outward (a shock's wake also lays the ink back in its bands), the drain's pull inward, its
-- white eye and the dark closing in on it
function Maelstrom:radiate(t, step)
	local settle = MS_SETTLE
	local push = self.radial
	local lit = MS_GLOW
	local kept = MS_DUSK
	local unit = self.unit
	local t_drain = self.t_drain
	local n = #lit
	local drain = self:smooth01((t - t_drain) / MUSIC_BAR)
	local closing = self:smooth01((t - t_drain - MUSIC_BAR * 0.5) / (MUSIC_BAR * 1.5))
	local dark_at = n * (1.0 - closing) + 11.0 * unit * closing
	local dusk = 0.25 / unit
	local eye = 3.5 * unit
	local mouth = 9.0 * unit
	local pull = 26.0 * unit * drain
	local calm = (self.settling + (self.pool_settling - self.settling) * self:smooth01((t - self.t_pool) / MUSIC_BEAT)) * step
	local full = 8.0 + 2.5 * self:hit_at(t)
	for r = 0, n - 1 do
		local m = r / mouth
		local q = r / eye
		push[r + 1] = 0.0 - pull * m / (1.0 + m * m)
		lit[r + 1] = full + 5.0 * drain / (1.0 + q * q * q * q)
		settle[r + 1] = calm
	end
	local wide = 3.5 * unit
	local ring_born, ring_power, ring_lays = self.ring_born, self.ring_power, self.ring_lays
	for k = 1, #ring_born do
		local age = t - ring_born[k]
		if not (age < 0.0 or age > 2.2) then
			local amp = ring_power[k] * exp(0.0 - age * 1.5)
			local at = (3.0 + age * 48.0) * unit
			local r0 = max(0, itrunc(at - wide * 2.6))
			local r1 = min(n - 1, itrunc(at + wide) + 1)
			for r2 = r0, r1 do
				local e = (r2 - at) / wide
				if e > -1.0 then
					local crest = 1.0 - e * e
					if crest > 0.0 then
						lit[r2 + 1] = lit[r2 + 1] + 3.4 * amp * crest
						push[r2 + 1] = push[r2 + 1] + 24.0 * unit * amp * crest
					end
				else
					local wake = 1.0 - (e + 1.8) * (e + 1.8) / 0.64
					if wake > 0.0 then
						lit[r2 + 1] = lit[r2 + 1] - 3.0 * amp * wake
						settle[r2 + 1] = settle[r2 + 1] + ring_lays[k] * wake
					end
				end
			end
		end
	end
	for r3 = 0, n - 1 do
		if settle[r3 + 1] > 1.0 then settle[r3 + 1] = 1.0 end
		local left = (dark_at - r3) * dusk
		kept[r3 + 1] = left < 0.0 and 0.0 or (left > 1.0 and 1.0 or left)
	end
end

-- the ink rides the flow plus what the rings and the drain add (a push the projection would
-- cancel, so the flow itself never takes it); every row reads the old ink before it is rebuilt
function Maelstrom:carry_ink(t, step)
	self:radiate(t, step)
	local u, v = self.u, self.v
	local cn = self.cw * self.ch
	-- the original assigns by value: MS_FU is a copy of u
	if #MS_FU ~= cn then
		MS_FU = {}
		MS_FV = {}
	end
	move(u, 1, cn, 1, MS_FU)
	move(v, 1, cn, 1, MS_FV)
	local fu = MS_FU
	local fv = MS_FV
	local radial, away_r, away_x, away_y = self.radial, self.away_r, self.away_x, self.away_y
	for i = 1, cn do
		local out = radial[away_r[i] + 1]
		fu[i] = fu[i] + away_x[i] * out
		fv[i] = fv[i] + away_y[i] * out
	end
	MS_RUN = step * 2.0
	local rows = parallel_transform(MS_INK_ROWS, maelstrom_ink_row)
	local ink = MS_INK
	local dw = self.dw
	local n = 0
	for r = 1, #rows do
		move(rows[r], 1, dw, n + 1, ink)
		n = n + dw
	end
	for i = #ink, n + 1, -1 do ink[i] = nil end
end

-- the wave equation on the ink's grid, in steps short enough that a wave crosses less than a cell
-- in one: each cell is pulled toward the mean of its four neighbours and its speed toward theirs
-- (which calms the chop and leaves the swell); a wall cell copies its inner neighbour, so waves reflect
function Maelstrom:ripple(step)
	local hgt = self.hw
	local vel = self.hv
	local gw = self.dw
	local gh = self.dh
	local wave_speed = self.wave_speed
	local subs = itrunc(step * wave_speed / 0.6) + 1
	local part = step / subs
	local pull = wave_speed * wave_speed * part
	local calm = self.wave_drag * part
	local keep = 1.0 - 0.8 * part
	local last = (gh - 1) * gw
	for sub = 1, subs do
		for y = 1, gh - 2 do
			local i = y * gw + 2
			for x = 1, gw - 2 do
				local here = hgt[i]
				local speed = vel[i]
				vel[i] = (speed + pull * (hgt[i - 1] + hgt[i + 1] + hgt[i - gw] + hgt[i + gw] - 4.0 * here)
					+ calm * (vel[i - 1] + vel[i + 1] + vel[i - gw] + vel[i + gw] - 4.0 * speed)) * keep
				i = i + 1
			end
		end
		for y2 = 1, gh - 2 do
			local j = y2 * gw + 2
			for x2 = 1, gw - 2 do
				hgt[j] = hgt[j] + vel[j] * part
				j = j + 1
			end
		end
		for x3 = 1, gw do
			hgt[x3] = hgt[x3 + gw]
			hgt[last + x3] = hgt[last - gw + x3]
		end
		for y3 = 0, gh - 1 do
			local r = y3 * gw + 1
			hgt[r] = hgt[r + 1]
			hgt[r + gw - 1] = hgt[r + gw - 2]
		end
	end
end

-- something lands at ink cell (x, y): a crater `deep` at its middle with a raised rim, which
-- between them hold as much water as the flat they replace
function Maelstrom:splash(x, y, deep, rad)
	local hgt, dw, dh = self.hw, self.dw, self.dh
	local r2 = rad * rad
	local x0 = max(1, itrunc(x - rad))
	local x1 = min(dw - 2, itrunc(x + rad) + 1)
	local y0 = max(1, itrunc(y - rad))
	local y1 = min(dh - 2, itrunc(y + rad) + 1)
	for py = y0, y1 do
		local ey = py - y
		for px = x0, x1 do
			local ex = px - x
			local q = (ex * ex + ey * ey) / r2
			if q < 1.0 then
				local i = py * dw + px + 1
				hgt[i] = hgt[i] - deep * (1.0 - q) * (1.0 - q) * (1.0 - 4.0 * q)
			end
		end
	end
end

-- a disc of the surface about ink cell (x, y) is thrown upward (or, negative, pressed down),
-- fastest at its middle, its rim going the other way by as much water
function Maelstrom:bump(x, y, speed, rad)
	local vel, dw, dh = self.hv, self.dw, self.dh
	local r2 = rad * rad
	local x0 = max(1, itrunc(x - rad))
	local x1 = min(dw - 2, itrunc(x + rad) + 1)
	local y0 = max(1, itrunc(y - rad))
	local y1 = min(dh - 2, itrunc(y + rad) + 1)
	for py = y0, y1 do
		local ey = py - y
		for px = x0, x1 do
			local ex = px - x
			local q = (ex * ex + ey * ey) / r2
			if q < 1.0 then
				local i = py * dw + px + 1
				vel[i] = vel[i] + speed * (1.0 - q) * (1.0 - q) * (1.0 - 4.0 * q)
			end
		end
	end
end

function Maelstrom:drop(kind, size, entry, x, y, z, vx, vy, vz)
	local i = self.drop_next + 1
	self.drop_next = (self.drop_next + 1) % #self.drop_kind
	self.drop_kind[i] = kind
	self.drop_size[i] = size
	self.drop_entry[i] = entry
	self.drop_x[i] = x
	self.drop_y[i] = y
	self.drop_z[i] = z
	self.drop_vx[i] = vx
	self.drop_vy[i] = vy
	self.drop_vz[i] = vz
end

-- the bright part of the tube the ink at ink cell (x, y) lies in
function Maelstrom:tube_at(x, y)
	local ink = itrunc(MS_INK[itrunc(y) * self.dw + itrunc(x) + 1])
	return (((ink + self.traded) & 127) // 16) * 16 + 5
end

-- a blob let go over ink cell (x, y) so that it lands `fall_lead` from now
function Maelstrom:let_fall(x, y, size)
	self.blob_tube = (self.blob_tube + 3) % 8
	local fall_speed, fall_lead = self.fall_speed, self.fall_lead
	local from = fall_speed * fall_lead + 0.5 * self.gravity * fall_lead * fall_lead
	self:drop(2, size, self.blob_tube * 16 + 5, x, y, from, 0.0, 0.0, 0.0 - fall_speed)
end

-- `count` drops of spray thrown up and outward from a ring about ink cell (x, y)
function Maelstrom:crown(x, y, count, rad, up, entry)
	local rng, unit = self.rng, self.unit
	local from = MS_Z[itrunc(y) * self.dw + itrunc(x) + 1] + 0.6
	for j = 1, count do
		local a = rng:nextf() * 6.2831853
		local out = (5.0 + 9.0 * rng:nextf()) * unit
		local ca = cos(a)
		local sa = sin(a)
		self:drop(0, 0.0, entry, x + ca * rad, y + sa * rad, from, ca * out, sa * out, up * (0.55 + 0.6 * rng:nextf()))
	end
end

-- what the cue throws at the water: rain over the opening, a blob let go ahead of every kick
-- (anywhere at first, then over the pouring nozzles in turn), the hits' own blobs, a spout from
-- a drawing nozzle on each snare, and spray wherever a wave climbs a wall
function Maelstrom:throw_water(before, t, step)
	local cx = MS_CX
	local cy = MS_CY
	local rng, unit, dw, dh = self.rng, self.unit, self.dw, self.dh
	local t_pair, t_pool, t_drain, fall_lead = self.t_pair, self.t_pool, self.t_drain, self.fall_lead
	self.rain_due = self.rain_due + (t < t_pair and 7.0 or (t < t_drain and 1.5 or 0.0)) * step
	while self.rain_due >= 1.0 do
		self.rain_due = self.rain_due - 1.0
		local rx = 2.0 + rng:nextf() * (dw - 5.0)
		local ry = 2.0 + rng:nextf() * (dh - 5.0)
		self:drop(1, 0.0, self:tube_at(rx, ry), rx, ry, 34.0 * unit, 0.0, 0.0, -25.0 * unit)
	end
	local lands = t + fall_lead
	local ahead = self:kick_at(lands)
	local scripted = abs(lands - t_pair) < 0.25 or abs(lands - t_pool) < 0.25 or abs(lands - t_drain) < 0.25
	if ahead > self.kick_ahead_before + 0.3 and not scripted and lands < t_drain + MUSIC_BAR then
		if lands < t_pair then
			local fx = cx + (rng:nextf() - 0.5) * dw * 0.7
			local fy = cy + (rng:nextf() - 0.5) * dh * 0.7
			self:let_fall(fx, fy, 2.2 * unit)
		else
			local reach = 1.0 - 0.5 * self:smooth01((lands - t_drain) / (MUSIC_BAR * 1.5))
			local a = self.orbit + self.turn * 0.55 * fall_lead + self.blob_turn * 2.0943951
			self.blob_turn = (self.blob_turn + 1) % 3
			self:let_fall(cx + cos(a) * dw * 0.37 * reach, cy + sin(a) * dh * 0.37 * reach, 2.2 * unit)
		end
	end
	self.kick_ahead_before = ahead
	local pair_falls = t_pair - fall_lead
	if before < pair_falls and t >= pair_falls then
		self:let_fall(cx - dw * 0.23, cy, 3.0 * unit)
		self:let_fall(cx + dw * 0.23, cy, 3.0 * unit)
	end
	local pool_falls = t_pool - fall_lead
	if before < pool_falls and t >= pool_falls then self:let_fall(cx, cy, 4.5 * unit) end
	local snare = self:snare_at(t)
	if t >= t_pair and t < t_drain + MUSIC_BAR and snare > self.snare_before + 0.3 then
		local reach2 = 1.0 - 0.5 * self:smooth01((t - t_drain) / (MUSIC_BAR * 1.5))
		local a2 = self.orbit + (1 + 2 * self.spout_turn) * 1.0471976
		self.spout_turn = (self.spout_turn + 1) % 3
		local sx = cx + cos(a2) * dw * 0.37 * reach2
		local sy = cy + sin(a2) * dh * 0.37 * reach2
		self:bump(sx, sy, 50.0 * unit, 5.0 * unit)
		self:crown(sx, sy, 5, 1.5 * unit, 60.0 * unit, self:tube_at(sx, sy))
	end
	self.snare_before = snare
	local hw, hv = self.hw, self.hv
	for s = 1, 10 do
		local side = rng:next(4)
		local along = rng:nextf()
		local wx = 2.0 + along * (dw - 5.0)
		local wy = 2.0 + along * (dh - 5.0)
		local in_x = 0.0
		local in_y = 0.0
		if side == 0 then
			wy = 2.0
			in_y = 1.0
		elseif side == 1 then
			wy = dh - 3.0
			in_y = -1.0
		elseif side == 2 then
			wx = 2.0
			in_x = 1.0
		else
			wx = dw - 3.0
			in_x = -1.0
		end
		local k = itrunc(wy) * dw + itrunc(wx) + 1
		if hw[k] > 3.0 * unit and hv[k] > 0.0 then
			self:drop(0, 0.0, self:tube_at(wx, wy), wx, wy, MS_Z[k] + 0.6, in_x * 5.0 * unit, in_y * 5.0 * unit, 30.0 * unit + 0.5 * hv[k])
		end
	end
end

-- everything in the air falls; what meets the water on its way down lands, and what leaves the
-- tank falls past it to the floor
function Maelstrom:fall(step)
	local pull = self.gravity * step
	local dw = self.dw
	local xhi = dw - 2.0
	local yhi = self.dh - 2.0
	local bed = 0.0 - MS_DEPTH
	local drop_kind, drop_x, drop_y, drop_z = self.drop_kind, self.drop_x, self.drop_y, self.drop_z
	local drop_vx, drop_vy, drop_vz = self.drop_vx, self.drop_vy, self.drop_vz
	local n = #drop_kind
	for i = 1, n do
		local kind = drop_kind[i]
		if kind >= 0 then
			drop_vz[i] = drop_vz[i] - pull
			local x = drop_x[i] + drop_vx[i] * step
			local y = drop_y[i] + drop_vy[i] * step
			local z = drop_z[i] + drop_vz[i] * step
			drop_x[i] = x
			drop_y[i] = y
			drop_z[i] = z
			if x < 1.0 or x > xhi or y < 1.0 or y > yhi then
				if z < bed then drop_kind[i] = -1 end
			elseif not (drop_vz[i] > 0.0 or z > MS_Z[itrunc(y) * dw + itrunc(x) + 1]) then
				drop_kind[i] = -1
				self:land(kind, self.drop_size[i], self.drop_entry[i], x, y)
			end
		end
	end
end

-- spray makes a ripple, rain a small crater and a couple of drops, a blob a crater to its size,
-- a crown of spray and its ink
function Maelstrom:land(kind, size, entry, x, y)
	local unit = self.unit
	if kind == 0 then
		self:splash(x, y, 0.4 * unit, max(3.0 * unit, 2.5))
		return
	end
	if kind == 1 then
		self:splash(x, y, 2.0 * unit, max(4.0 * unit, 3.0))
		self:crown(x, y, 2, 1.0 * unit, 26.0 * unit, entry)
		return
	end
	local big = size / (2.2 * unit)
	self:splash(x, y, 8.0 * unit * big, 5.5 * unit * big)
	self:pour(x, y, 36.0 * big, 4.5 * unit * big)
	self:crown(x, y, itrunc(8.0 * big * big), 2.4 * unit * big, 46.0 * unit * sqrt(big), entry)
end

-- the camera swings about the tank's long side, framing it whole from wherever it stands: it
-- punches in on the kick and the hits, shakes with a hit, and over the drain climbs and dives at the eye
function Maelstrom:place_camera(t)
	local w, h, dw, dh = self.w, self.h, self.dw, self.dh
	local flash = self:hit_at(t)
	local dive = self:smooth01((t - self.t_drain) / (MUSIC_BAR * 2.0))
	local yaw = 1.5707963 + 0.36 * sin(self.cam_yaw)
	local fx = cos(yaw)
	local fy = sin(yaw)
	local half_w = dw * 0.5
	local half_h = dh * 0.5
	local dist = 0.9 * sqrt(dw * dw + dh * dh * 1.0)
	local along_a = abs(half_w * fx + half_h * fy)
	local aside_a = abs(half_h * fx - half_w * fy)
	local along_b = abs(half_w * fx - half_h * fy)
	local aside_b = abs(half_h * fx + half_w * fy)
	local along = along_a > along_b and along_a or along_b
	local near = dist - along
	local far = dist + along
	local fit_a = (dist - along_a) / (aside_a + 0.001)
	local fit_b = (dist - along_b) / (aside_b + 0.001)
	local camh = dist * (0.5 + 0.5 * dive)
	local wide = 0.48 * w * (fit_a < fit_b and fit_a or fit_b)
	local tall = 0.78 * h / ((camh + MS_DEPTH) / near - camh / far)
	local whole = wide < tall and wide or tall
	local focal = whole * (1.0 + 1.6 * dive)
	local hz_whole = 0.61 * h - 0.5 * whole * ((camh + MS_DEPTH) / near + camh / far)
	local hz_eye = 0.5 * h - camh * focal / dist
	local hz = hz_whole + (hz_eye - hz_whole) * dive
	local punch = 1.0 + 0.035 * self:kick_at(t) + 0.08 * flash
	local mid = h * 0.5
	MS_FOCAL = focal * punch
	MS_HORIZON = mid - (mid - hz) * punch + sin(t * 53.0) * flash * flash * h * 0.02
	MS_CAM_H = camh
	MS_FACE_X = fx
	MS_FACE_Y = fy
	MS_CAM_X = dw * 0.5 - fx * dist
	MS_CAM_Y = dh * 0.5 - fy * dist
	MS_GLINT_X = 0.5 * fx
	MS_GLINT_Y = 0.5 * fy
	local closing = self:smooth01((t - self.t_drain - MUSIC_BAR * 0.5) / (MUSIC_BAR * 1.5))
	local reach = far * 1.5 - near * 0.8
	MS_GRID_FADE = 7.0 / reach
	MS_GRID_GAIN = (7.0 + near * 0.8 * MS_GRID_FADE + 1.5 * self:kick_at(t)) * (1.0 - closing)
end

-- what the picture reads: the waves with the vortices' dimples and the drain's funnel pressed
-- into them, the camera, and the surface's light from its slopes
function Maelstrom:shape_surface(t)
	self:place_camera(t)
	local unit = self.unit
	local pool = self:smooth01((t - self.t_pool) / MUSIC_BEAT)
	local pair = self:smooth01((t - self.t_pair) / MUSIC_BEAT) * (1.0 - pool)
	local drain = self:smooth01((t - self.t_drain) / (MUSIC_BAR * 1.5))
	local pair_deep = 5.0 * unit * pair
	local pool_deep = 6.0 * unit * pool
	local drain_deep = 20.0 * unit * drain
	local z = MS_Z
	local hgt = self.hw
	local pair_dish = self.dish_pair
	local pool_dish = self.dish_pool
	for i = 1, self.dw * self.dh do
		local f = pool_dish[i]
		z[i] = hgt[i] - pair_deep * pair_dish[i] - (pool_deep + drain_deep * f) * f
	end
	MS_LIGHT_X = 1.4
	MS_LIGHT_Y = 1.9
	MS_GLINT_NARROW = 1.0 / (0.42 * 0.42)
	MS_GLINT = 7.0
	MS_CREST = 0.4 / unit
	local rows = parallel_transform(MS_INK_ROWS, maelstrom_light_row)
	local lit = MS_LIT
	local dw = self.dw
	local n = 0
	for r = 1, #rows do
		move(rows[r], 1, dw, n + 1, lit)
		n = n + dw
	end
	for i = #lit, n + 1, -1 do lit[i] = nil end
end

-- what is in the air, over the finished picture: spray and rain as short streaks, blobs as discs
function Maelstrom:draw_drops(grid)
	local fx = MS_FACE_X
	local fy = MS_FACE_Y
	local camx = MS_CAM_X
	local camy = MS_CAM_Y
	local camh = MS_CAM_H
	local focal = MS_FOCAL
	local hz = MS_HORIZON
	local ww = self.w
	local hh = self.h
	local mid = ww * 0.5
	local spray = MS_TUBES + 11 * 128
	local blob = MS_TUBES + 14 * 128
	local drop_kind, drop_x, drop_y, drop_z = self.drop_kind, self.drop_x, self.drop_y, self.drop_z
	local drop_vz, drop_size, drop_entry = self.drop_vz, self.drop_size, self.drop_entry
	local n = #drop_kind
	for i = 1, n do
		if drop_kind[i] >= 0 then
			local ex = drop_x[i] - camx
			local ey = drop_y[i] - camy
			local depth = ex * fx + ey * fy
			if depth >= 4.0 then
				local scale = focal / depth
				local px = mid + (ey * fx - ex * fy) * scale
				local py = hz + (camh - drop_z[i]) * scale
				local rad = drop_size[i] * scale
				if rad < 0.9 then
					local tail = py + drop_vz[i] * 0.03 * scale
					local top = py < tail and py or tail
					local bottom = py < tail and tail or py
					if not (px < 0.0 or px >= ww or bottom < 0.0 or top >= hh) then
						local sx = floor(px) + 1
						local ya = top < 0.0 and 0 or itrunc(top)
						local yb = bottom >= hh and hh - 1 or itrunc(bottom)
						if yb > ya + 3 then yb = ya + 3 end
						local pix = spray + drop_entry[i]
						for y = ya, yb do grid[y * ww + sx] = pix end
					end
				else
					local reach = floor(rad + 1.0)
					local cx = itrunc(px + 1000.0) - 1000
					local cy = itrunc(py + 1000.0) - 1000
					local r2 = rad * rad + 0.3
					local core = blob + drop_entry[i]
					local rim = spray + drop_entry[i]
					for oy = -reach, reach do
						local y2 = cy + oy
						if y2 >= 0 and y2 < hh then
							for ox = -reach, reach do
								local x2 = cx + ox
								local q = ox * ox + oy * oy
								if not (x2 < 0 or x2 >= ww or q > r2) then
									if q * 2.5 < r2 then
										grid[y2 * ww + x2 + 1] = core
									else
										grid[y2 * ww + x2 + 1] = rim
									end
								end
							end
						end
					end
				end
			end
		end
	end
end

function Maelstrom:render(grid)
	-- the shocks flash the picture negative
	local tt = self.tt
	local since_pool = tt - self.t_pool
	local since_drain = tt - self.t_drain
	local negative = (since_pool >= 0.0 and since_pool < 0.1) or (since_drain >= 0.0 and since_drain < 0.1)
	local ww = self.w
	local hh = self.h
	MS_W = ww
	MS_H = hh
	MS_TUBES = negative and self.tubes + 2048 or self.tubes
	MS_FLOOR = negative and self.floor_at + 9 or self.floor_at
	MS_PHASE = self.traded + itrunc(3.5 * self:kick_at(tt))
	local cols = parallel_transform(MS_COLS, maelstrom_column)
	local n = ww * hh
	if #grid ~= n then
		for i = #grid + 1, n do grid[i] = 0 end
		for i = #grid, n + 1, -1 do grid[i] = nil end
	end
	for x = 1, ww do
		local col = cols[x]
		local gi = x
		for y = 1, hh do
			grid[gi] = col[y]
			gi = gi + ww
		end
	end
	self:draw_drops(grid)
end

SCENES[10] = Maelstrom
