-- Map parsing + boot-time validation (map DATA is in data.lua, generated from
-- the reference maps.jai so the rows are byte-exact).
--
-- Tile ints (G.tiles, 0-based VALUES in a 1-based flat array):
--   0 floor, 1-4 walls, 5 door, 6 exit, 7 secret, 8 red door, 9 blue door.

local WALL_KINDS = {["#"] = 1, ["%"] = 2, ["="] = 3, ["&"] = 4,
	D = 5, X = 6, S = 7, R = 8, B = 9}
local ITEM_KINDS = {["1"] = 0, ["2"] = 1, ["3"] = 2, ["4"] = 3, ["5"] = 4,
	["6"] = 5, r = 6, b = 7, ["7"] = 8, ["8"] = 9, ["9"] = 10}

function wall_kind_for(ch) return WALL_KINDS[ch] or 0 end
function item_kind_for(ch) return ITEM_KINDS[ch] or -1 end

-- parse MAPS[mi+1] into G: tiles, dimensions, spawns.
function load_map(mi)
	local g = G
	local m = MAPS[mi + 1]
	local rows = m.rows
	g.map_name = m.name
	g.par = m.par
	g.mh = #rows
	g.mw = #rows[1]
	g.tiles = {}
	g.enemies = {}
	g.items = {}
	g.shots = {}
	g.kills = 0
	g.kill_total = 0
	g.secrets = 0
	g.secret_total = 0
	g.map_ticks = 0
	local tiles = g.tiles
	local items = g.items
	local ti = 0
	for y = 0, g.mh - 1 do
		local row = rows[y + 1]
		for x = 0, g.mw - 1 do
			local ch = row:sub(x + 1, x + 1)
			local wk = wall_kind_for(ch)
			if wk == 7 then g.secret_total = g.secret_total + 1 end
			ti = ti + 1
			if wk > 0 then
				tiles[ti] = wk
			else
				tiles[ti] = 0
				if ch == "@" then
					g.px = x + 0.5
					g.py = y + 0.5
					g.pang = 0.0
				elseif ch == "g" then spawn_enemy(0, x, y)
				elseif ch == "z" then spawn_enemy(1, x, y)
				elseif ch == "t" then spawn_enemy(2, x, y)
				elseif ch == "W" then spawn_enemy(3, x, y)
				else
					local ik = item_kind_for(ch)
					if ik >= 0 then items[#items + 1] = {ik, x + 0.5, y + 0.5, 0} end
				end
			end
		end
	end
	rebuild_mapstr()
	build_render_tables(mi)
end

-- raycast snapshot: per-cell RENDER kinds (secret -> wall kind 1, red/blue
-- doors -> 7/8). The reference bit-packs this 15-per-int64 for its pure-value
-- parallel contract; a Lua port has no such constraint, so it's a flat array
-- (REFERENCE.md 4.2 explicitly permits this).
TILE_TO_KIND = {0, 1, 2, 3, 4, 5, 6, 1, 7, 8}   -- indexed [tile + 1]

function rebuild_mapstr()
	local g = G
	local tiles = g.tiles
	local t2k = TILE_TO_KIND
	local n = g.mw * g.mh
	local mk = g.mapkind
	if mk == nil then mk = {}; g.mapkind = mk end
	for i = 1, n do mk[i] = t2k[tiles[i] + 1] end
end

function tile_at(x, y)
	local g = G
	if x < 0 or y < 0 or x >= g.mw or y >= g.mh then return 1 end
	return g.tiles[y * g.mw + x + 1]
end

function tile_solid(t) return t ~= 0 end

-- walkable for actors: floor only (doors must be opened first)
function walkable(x, y) return tile_at(x, y) == 0 end

-- ----------------------------------------------------------- validation -----
function validate_map(mi)
	local rows = MAPS[mi + 1].rows
	local h = #rows
	local w = #rows[1]
	local start_count = 0
	for y = 0, h - 1 do
		local row = rows[y + 1]
		if #row ~= w then
			error(string.format("map %d row %d: width %d != %d", mi, y, #row, w))
		end
		for x = 0, w - 1 do
			local ch = row:sub(x + 1, x + 1)
			local border = x == 0 or y == 0 or x == w - 1 or y == h - 1
			if border and wall_kind_for(ch) == 0 then
				error(string.format("map %d (%d,%d): border leak '%s'", mi, x, y, ch))
			end
			if ch == "@" then start_count = start_count + 1 end
			if ch == "D" or ch == "R" or ch == "B" then
				local open_ns = wall_kind_for(rows[y]:sub(x + 1, x + 1)) == 0
					and wall_kind_for(rows[y + 2]:sub(x + 1, x + 1)) == 0
				local open_ew = wall_kind_for(row:sub(x, x)) == 0
					and wall_kind_for(row:sub(x + 2, x + 2)) == 0
				if not open_ns and not open_ew then
					error(string.format("map %d (%d,%d): door not flanked by floor", mi, x, y))
				end
			end
		end
	end
	if start_count ~= 1 then error(string.format("map %d: %d player starts", mi, start_count)) end

	-- reachability: flood from '@' through floor/doors/secrets
	local sx, sy = 0, 0
	local reach = {}
	for i = 1, w * h do reach[i] = 0 end
	for y = 0, h - 1 do
		for x = 0, w - 1 do
			if rows[y + 1]:sub(x + 1, x + 1) == "@" then sx = x; sy = y end
		end
	end
	local qx, qy = {sx}, {sy}
	reach[sy * w + sx + 1] = 1
	local head = 1
	local dirs = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}
	while head <= #qx do
		local cx, cy = qx[head], qy[head]
		head = head + 1
		for di = 1, 4 do
			local d = dirs[di]
			local nx, ny = cx + d[1], cy + d[2]
			if nx >= 0 and ny >= 0 and nx < w and ny < h
					and reach[ny * w + nx + 1] ~= 1 then
				local wk = wall_kind_for(rows[ny + 1]:sub(nx + 1, nx + 1))
				-- doors (any lock) and secrets are traversable for reachability
				if wk == 0 or wk == 5 or wk == 7 or wk == 8 or wk == 9 then
					reach[ny * w + nx + 1] = 1
					qx[#qx + 1] = nx
					qy[#qy + 1] = ny
				end
			end
		end
	end
	local exit_reached = false
	for y = 0, h - 1 do
		for x = 0, w - 1 do
			local ch = rows[y + 1]:sub(x + 1, x + 1)
			local wk = wall_kind_for(ch)
			local r = reach[y * w + x + 1]
			if wk == 0 and r == 0 then
				error(string.format("map %d (%d,%d): unreachable floor '%s'", mi, x, y, ch))
			end
			if ch == "X" then
				for di = 1, 4 do
					local d = dirs[di]
					local ax, ay = x + d[1], y + d[2]
					if ax >= 0 and ay >= 0 and ax < w and ay < h
							and reach[ay * w + ax + 1] == 1 then
						exit_reached = true
					end
				end
			end
		end
	end
	if not exit_reached then error(string.format("map %d: exit switch unreachable", mi)) end
end

function validate_all_maps()
	for mi = 0, #MAPS - 1 do validate_map(mi) end
end
