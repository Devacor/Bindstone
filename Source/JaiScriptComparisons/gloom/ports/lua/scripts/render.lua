-- The renderer. A flat truecolor palette-index pixel grid blitted to the
-- terminal through sub-cell block glyphs (half / quad / sext), a straight
-- serial transliteration of the reference pipeline: one DDA ray per cell
-- column, prebaked background copy, lazy texture strips, far-to-near
-- billboards, z-clipped particles, then the glyph row builders.
-- Locals-heavy throughout: every hot table and function is hoisted.

local sqrt, cos, sin, floor, abs = math.sqrt, math.cos, math.sin, math.floor, math.abs
local concat = table.concat
local move = table.move

PIXW = 2        -- pixels per cell horizontally (set by gloom_boot)
PIXH = 2        -- pixels per cell vertically
RESET = ""
QUADG = {}      -- quadrant glyphs by TL|TR<<1|BL<<2|BR<<3, +1
SEXTG = {}      -- sextant glyphs by TL|TR<<1|ML<<2|MR<<3|BL<<4|BR<<5, +1
HALFG = ""      -- upper half block
AM_COLS = {}    -- automap tile colors (built at boot), [kind + 1]
AM_FLOOR = 0
AM_BLINK = 0
AM_ITEM = 0
AM_ENEMY = 0
AM_SELF = 0
AM_HEAD = 0
HURT_RED = 0
STRIPS = {}     -- lazy 33-tall wall texture strips keyed by (kind,side,shade,tx)

function init_render()
	RESET = ESC .. "[0m"
	PAL_BLACK = rgb_idx(0, 0, 0)     -- first interned color is black
	HALFG = utf8(0x2580)
	QUADG = {" "}
	local quad_cps = {0x2598, 0x259D, 0x2580, 0x2596, 0x258C, 0x259E, 0x259B,
		0x2597, 0x259A, 0x2590, 0x259C, 0x2584, 0x2599, 0x259F, 0x2588}
	for i = 1, 15 do QUADG[i + 1] = utf8(quad_cps[i]) end
	SEXTG = {}
	for b = 0, 63 do
		if b == 0 then SEXTG[b + 1] = " "
		elseif b == 21 then SEXTG[b + 1] = utf8(0x258C)        -- left half
		elseif b == 42 then SEXTG[b + 1] = utf8(0x2590)        -- right half
		elseif b == 63 then SEXTG[b + 1] = utf8(0x2588)        -- full
		else
			local cp = 0x1FB00 + b - 1
			if b > 21 then cp = cp - 1 end
			if b > 42 then cp = cp - 1 end
			SEXTG[b + 1] = utf8(cp)
		end
	end
	AM_COLS = {0, rgb_idx(150, 140, 120), rgb_idx(110, 150, 110), rgb_idx(110, 120, 160), rgb_idx(160, 110, 96),
		rgb_idx(200, 170, 110), rgb_idx(90, 230, 110), rgb_idx(150, 140, 120), rgb_idx(230, 70, 60), rgb_idx(90, 120, 240)}
	AM_FLOOR = rgb_idx(40, 40, 46)
	AM_BLINK = rgb_idx(240, 255, 160)
	AM_ITEM = rgb_idx(255, 220, 80)
	AM_ENEMY = rgb_idx(255, 60, 50)
	AM_SELF = rgb_idx(255, 255, 255)
	AM_HEAD = rgb_idx(255, 250, 140)
	HURT_RED = rgb_idx(200, 30, 24)
	build_particle_colors()
end

-- wall tone table: [kind 0..9][side 0..1][shade 0..15][tone 0..2] flattened
-- (+1 at the read); tones: 0 base, 1 accent, 2 mortar/frame.
WALLT = {}
CEILP = {}     -- per pixel-row ceiling color, [y + 1]
FLOORP = {}    -- per pixel-row floor color, [y + 1]

local function push_wall_tones(base, accent)
	local n = #WALLT
	for side = 0, 1 do
		local facing = side == 1 and 0.70 or 1.0
		for s = 0, 15 do
			local lum = (0.13 + 0.87 * s / 15.0) * facing
			n = n + 1
			WALLT[n] = rgb_idx(trunc(base[1] * lum), trunc(base[2] * lum), trunc(base[3] * lum))
			local alum = lum * 1.35
			if alum > 1.0 then alum = 1.0 end
			n = n + 1
			WALLT[n] = rgb_idx(trunc(accent[1] * alum), trunc(accent[2] * alum), trunc(accent[3] * alum))
			local mlum = lum * 0.42
			n = n + 1
			WALLT[n] = rgb_idx(trunc(base[1] * mlum), trunc(base[2] * mlum), trunc(base[3] * mlum))
		end
	end
end

function build_render_tables(mi)
	local g = G
	if SHOWCASE then
		build_showcase_tables(mi)
		g.lutc = {}
		return
	end
	local theme = MAPS[mi + 1].theme
	WALLT = {}
	-- kind 0 (never rendered): flat dark
	local zero = {10.0, 10.0, 12.0}
	push_wall_tones(zero, zero)
	-- kinds 1..4 from the theme
	for k = 1, 4 do
		local base = theme.walls[k]
		push_wall_tones(base, base)
	end
	-- kind 5 door, 6 exit, 7 red door, 8 blue door (episode constants)
	push_wall_tones({138.0, 116.0, 94.0}, {188.0, 164.0, 132.0})
	push_wall_tones({64.0, 160.0, 84.0}, {220.0, 235.0, 120.0})
	push_wall_tones({176.0, 50.0, 42.0}, {245.0, 130.0, 96.0})
	push_wall_tones({62.0, 94.0, 198.0}, {136.0, 176.0, 255.0})

	-- ceiling / floor gradients (dark at the horizon), quantized to CELL rows
	CEILP = {}
	FLOORP = {}
	local crgb = theme.ceil
	local frgb = theme.floor
	local vh = g.vh
	local half = vh // 2
	for y = 0, vh - 1 do
		local yq = (y // PIXH) * PIXH
		if y < half then
			local t = half > 1 and 1.0 * yq / (half - 1) or 1.0
			local lum = 1.0 - 0.72 * t
			CEILP[y + 1] = rgb_idx(trunc(crgb[1] * lum), trunc(crgb[2] * lum), trunc(crgb[3] * lum))
			FLOORP[y + 1] = 0
		else
			local t2 = 1.0 * (yq - half) / imax(1, vh - 1 - half)
			if t2 > 1.0 then t2 = 1.0 elseif t2 < 0.0 then t2 = 0.0 end
			local lum2 = 0.26 + 0.74 * t2
			CEILP[y + 1] = 0
			FLOORP[y + 1] = rgb_idx(trunc(frgb[1] * lum2), trunc(frgb[2] * lum2), trunc(frgb[3] * lum2))
		end
	end
	-- prebaked background: one table.move per frame replaces the per-pixel
	-- ceiling/floor loops outright
	local bg = {}
	local bi = 0
	local vw = g.vw
	for y2 = 0, vh - 1 do
		local c
		if y2 < half then c = CEILP[y2 + 1] else c = FLOORP[y2 + 1] end
		for x2 = 1, vw do
			bi = bi + 1
			bg[bi] = c
		end
	end
	g.bg = bg
	-- wall tone strips build lazily per (kind, side, shade, tx) combination
	STRIPS = {}
	g.lutc = {}
end

-- texture tone at (tx, ty) in 0..31 for a wall kind: 0 base, 1 accent, 2 mortar
local function tex_tone(kind, tx, ty)
	if kind == 1 then
		local tone = ((tx >> 2) + (ty >> 2)) & 1
		if (ty & 7) == 7 then tone = 2 end
		return tone
	end
	if kind == 2 then
		local tone = ((tx + ((ty >> 3 & 1) << 2)) >> 3) & 1
		if (ty & 7) == 0 then tone = 2 end
		return tone
	end
	if kind == 3 then
		local tone = (tx >> 3) & 1
		if (tx & 7) == 0 then tone = 2 end
		return tone
	end
	if kind == 4 then
		local tone = ((tx >> 4) + (ty >> 4)) & 1
		if (tx & 15) == 0 or (ty & 15) == 0 then tone = 2 end
		return tone
	end
	if kind == 6 then
		local tone = ((tx + ty) >> 2) & 1               -- hazard stripes
		if ty > 26 then tone = 2 end
		return tone
	end
	-- doors (plain / red / blue): frame, seam, lock studs
	if tx < 3 or tx > 28 or ty < 2 or ty > 29 then return 2 end
	if tx == 15 or tx == 16 then return 2 end
	if ty >= 14 and ty <= 17 and (tx & 3) == 1 then return 1 end
	return 0
end

function build_strip(kind, side, shade, tx)
	local wallt = WALLT
	local tbase = ((kind * 2 + side) * 16 + shade) * 3
	local s = {}
	for ty = 0, 31 do
		s[ty + 1] = wallt[tbase + tex_tone(kind, tx, ty) + 1]
	end
	s[33] = s[32]        -- fixed-point overshoot guard
	return s
end

-- ---------------------------------------------------------- sprite LUTs -----
-- per (art, shade band 0..7, flash) palette-code -> palette-index table, cached
local BAND_LUM = {1.0, 0.88, 0.76, 0.64, 0.52, 0.42, 0.32, 0.24}   -- [band + 1]

function sprite_lut(name, band, flash, variant)
	local g = G
	local key = name .. ":" .. band .. ":" .. flash
	if SHOWCASE then key = key .. ":" .. variant end
	local lut = g.lutc[key]
	if lut then return lut end
	if SHOWCASE then
		lut = showcase_sprite_lut(name, band, flash, variant)
		g.lutc[key] = lut
		return lut
	end
	local art = ARTS[name]
	local rgb = art.rgb
	local lum = BAND_LUM[band + 1]
	lut = {}
	for c = 1, #rgb do
		local e = rgb[c]
		local r, gg, b = e[1], e[2], e[3]
		if flash == 1 then
			r = imin(255, r + 170)
			gg = imin(255, gg + 150)
			b = imin(255, b + 140)
		end
		lut[c] = rgb_idx(trunc(r * lum), trunc(gg * lum), trunc(b * lum))
	end
	g.lutc[key] = lut
	return lut
end

-- -------------------------------------------------------------- the view ----
function render_view()
	local g = G
	local pix = g.pix
	local zbuf = g.zbuf
	local vw = g.vw
	local vh = g.vh
	local pang = g.pang
	if SHOWCASE then pang = pang + g.feel.yaw end
	local dirx = cos(pang)
	local diry = sin(pang)
	local planex = 0.0 - diry * 0.78
	local planey = dirx * 0.78
	local px = g.px
	local py = g.py
	local mw = g.mw
	local mk = g.mapkind
	local pxw = PIXW

	if SHOWCASE then
		showcase_view(px, py, dirx, diry, planex, planey)
	else
		-- --- 1+2. rays + columns: ONE ray per cell column (serial DDA, same float
		-- stream as the reference's chunked parallel_transform), prebaked
		-- background + textured wall slices
		move(g.bg, 1, vw * vh, 1, pix)     -- ceiling + floor in one copy
		local ncols = vw // pxw
		local xstep = 2.0 / (ncols - 1.0)
		local boost = g.muzzle > 0 and 2 or 0
		local strips = STRIPS
		for x = 0, ncols - 1 do
			local camx = x * xstep - 1.0
			local rdx = dirx + planex * camx
			local rdy = diry + planey * camx
			local mx = floor(px)
			local my = floor(py)
			local adx = rdx < 0.0 and -rdx or rdx
			local ady = rdy < 0.0 and -rdy or rdy
			local ddx = adx < 0.00000001 and 100000000.0 or 1.0 / adx
			local ddy = ady < 0.00000001 and 100000000.0 or 1.0 / ady
			local stepx, sdx
			if rdx < 0.0 then stepx = -1; sdx = (px - mx) * ddx
			else stepx = 1; sdx = (mx + 1.0 - px) * ddx end
			local stepy, sdy
			if rdy < 0.0 then stepy = -1; sdy = (py - my) * ddy
			else stepy = 1; sdy = (my + 1.0 - py) * ddy end
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
			local dist = side == 0 and sdx - ddx or sdy - ddy
			if dist < 0.02 then dist = 0.02 end
			local wallx
			if side == 0 then wallx = py + dist * rdy else wallx = px + dist * rdx end
			wallx = wallx - floor(wallx)
			local tx = floor(wallx * 64.0) >> 1             -- texcol/2: 0..31

			local px0 = x * pxw
			zbuf[px0 + 1] = dist
			if pxw == 2 then zbuf[px0 + 2] = dist end
			local lineh = floor(vh / dist)                  -- 1.0*vh/dist, truncated
			if lineh < 1 then lineh = 1 end
			local y0 = idivt(vh - lineh, 2)
			local y1 = y0 + lineh
			local cy0 = y0 < 0 and 0 or y0
			local cy1 = y1 > vh and vh or y1
			local shade = 15 - floor(dist * 1.55) + boost
			if shade < 0 then shade = 0 elseif shade > 15 then shade = 15 end
			local sidx = ((kind * 2 + side) * 16 + shade) * 32 + tx
			local strip = strips[sidx]
			if not strip then
				strip = build_strip(kind, side, shade, tx)
				strips[sidx] = strip
			end
			local tstep = 65536 // lineh                    -- (32 << 11) / lineh
			local tacc = (cy0 - y0) * tstep
			local gi = cy0 * vw + px0 + 1
			if pxw == 2 then
				for y = cy0, cy1 - 1 do
					local c = strip[(tacc >> 11) + 1]
					pix[gi] = c
					pix[gi + 1] = c
					tacc = tacc + tstep
					gi = gi + vw
				end
			else
				for y = cy0, cy1 - 1 do
					pix[gi] = strip[(tacc >> 11) + 1]
					tacc = tacc + tstep
					gi = gi + vw
				end
			end
		end
	end

	-- --- 3. billboards: enemies, pickups, projectiles (far to near) ---
	local det = planex * diry - dirx * planey
	if det < 0.0001 and det > -0.0001 then det = 0.0001 end
	local inv = 1.0 / det
	local sp = {}
	local enemies = g.enemies
	for i = 1, #enemies do
		local e = enemies[i]
		collect_billboard(sp, e.x, e.y, enemy_art(e), ENEMY_DEFS[e.kind + 1].size, 0.0,
			e.flash > 0 and 1 or 0, inv, dirx, diry, planex, planey)
	end
	local items = g.items
	for i = 1, #items do
		local it = items[i]
		if it[4] ~= 1 then
			local idef = ITEM_DEFS[it[1] + 1]
			collect_billboard(sp, it[2], it[3], idef.art, idef.size, 0.0,
				0, inv, dirx, diry, planex, planey)
		end
	end
	local shots = g.shots
	for i = 1, #shots do
		local s = shots[i]
		local sk = s[1]
		local art = sk == 1 and "pr_spit" or (sk == 2 and "pr_fire" or "pr_hex")
		collect_billboard(sp, s[2], s[3], art, 0.16, 0.35, 0, inv, dirx, diry, planex, planey)
	end
	-- insertion sort, far first (records: {depth, scx, name, size, zlift, flash, wx, wy})
	for i = 2, #sp do
		local rec = sp[i]
		local di = rec[1]
		local j = i - 1
		while j >= 1 do
			local cmp = sp[j]
			if cmp[1] >= di then break end
			sp[j + 1] = cmp
			j = j - 1
		end
		sp[j + 1] = rec
	end
	for i = 1, #sp do draw_billboard(sp[i], pix, zbuf) end

	-- --- 4. particles: pixel confetti with z-buffer clipping ---
	local pcols = PART_COLS
	local half2 = g.horizon
	local pxh = PIXH
	local pool = g.fx.pool
	for pi = 1, PART_MAX do
		local prt = pool[pi]
		local pk = prt[1]
		if pk ~= 0 then
			local rx = prt[2] - px
			local ry = prt[3] - py
			local depth = inv * (planex * ry - planey * rx)
			if depth >= 0.15 and depth <= 14.0 then
				local ttx = inv * (diry * rx - dirx * ry)
				local scx = trunc((vw / 2.0) * (1.0 + ttx / depth))
				if scx >= 0 and scx < vw then
					local zd = zbuf[scx + 1]
					if depth < zd then
						local span = 1.0 * vh / depth
						local pyx = trunc(half2 + span * 0.5 - prt[4] * span)
						local life = prt[8]
						local phase = 3 - imin(3, life // 5)
						local color = pcols[pk * 4 + phase + 1]
						local psz = trunc(0.07 * span) + 1
						if psz > 3 then psz = 3 end
						local psx = (psz * 2 * pxw) // pxh     -- square-ish
						if psx < 1 then psx = 1 end
						for oy = 0, psz - 1 do
							local yy = pyx + oy
							if yy >= 0 and yy < vh then
								local rowb = yy * vw
								for ox = 0, psx - 1 do
									local xx = scx + ox
									if xx < vw and depth < zbuf[xx + 1] then
										pix[rowb + xx + 1] = color
									end
								end
							end
						end
					end
				end
			end
		end
	end

	-- --- 5. overlays: the gun in hand and the feel (showcase), automap, hurt flash ---
	if SHOWCASE then
		draw_viewmodel()
		g.feel:overlay(vw, vh)
	end
	if g.automap then draw_automap(pix, g.tiles) end
	if not SHOWCASE and g.hurt_flash > 0 then
		local red = HURT_RED
		local thick = pxw * 2
		for x4 = 0, vw - 1 do
			pix[x4 + 1] = red
			pix[vw + x4 + 1] = red
			pix[(vh - 2) * vw + x4 + 1] = red
			pix[(vh - 1) * vw + x4 + 1] = red
		end
		for y4 = 0, vh - 1 do
			local rb = y4 * vw
			for t4 = 0, thick - 1 do
				pix[rb + t4 + 1] = red
				pix[rb + vw - t4] = red      -- rb + vw - 1 - t4, +1
			end
		end
	end

	-- --- 6. pixel grid -> sub-cell glyph rows -------------------------------
	if PIXH == 3 then return rows_sext(pix, g.rowparts) end
	if PIXW == 2 then return rows_quad(pix, g.rowparts) end
	return rows_half(pix, g.rowparts)
end

-- half mode: fg = top pixel, bg = bottom pixel, upper-half-block glyph
function rows_half(pix, parts)
	local g = G
	local vw = g.vw
	local fgs = PAL_FG
	local bgs = PAL_BG
	local hb = HALFG
	local reset = RESET
	local rows = {}
	local vrows = g.vh // 2
	for ry = 0, vrows - 1 do
		local n = 0
		local ptop = -1
		local pbot = -1
		local itop = ry * 2 * vw + 1
		local ibot = itop + vw
		for x = 0, vw - 1 do
			local tc = pix[itop + x]
			local bc = pix[ibot + x]
			if tc ~= ptop then n = n + 1; parts[n] = fgs[tc]; ptop = tc end
			if bc ~= pbot then n = n + 1; parts[n] = bgs[bc]; pbot = bc end
			n = n + 1
			parts[n] = hb
		end
		n = n + 1
		parts[n] = reset
		rows[ry + 1] = concat(parts, "", 1, n)
	end
	return rows
end

-- quad mode: 2x2 pixels per cell, two colors by luminosity partition
function rows_quad(pix, parts)
	local g = G
	local vw = g.vw
	local fgs = PAL_FG
	local bgs = PAL_BG
	local lums = PAL_LUM
	local qg = QUADG
	local reset = RESET
	local rows = {}
	local vrows = g.vh // 2
	local cells = vw // 2
	for ry = 0, vrows - 1 do
		local n = 0
		local pfg = -1
		local pbg = -1
		local itop = ry * 2 * vw + 1
		local ibot = itop + vw
		for cx = 0, cells - 1 do
			local xx = cx * 2
			local p0 = pix[itop + xx]
			local p1 = pix[itop + xx + 1]
			local p2 = pix[ibot + xx]
			local p3 = pix[ibot + xx + 1]
			if p0 == p1 and p0 == p2 and p0 == p3 then
				if p0 ~= pbg then n = n + 1; parts[n] = bgs[p0]; pbg = p0 end
				n = n + 1
				parts[n] = " "
			elseif p0 == p2 and p1 == p3 then
				-- vertical halves two-color fast path
				if p0 ~= pfg then n = n + 1; parts[n] = fgs[p0]; pfg = p0 end
				if p1 ~= pbg then n = n + 1; parts[n] = bgs[p1]; pbg = p1 end
				n = n + 1
				parts[n] = qg[6]                 -- left half (bits 5)
			elseif p0 == p1 and p2 == p3 then
				if p0 ~= pfg then n = n + 1; parts[n] = fgs[p0]; pfg = p0 end
				if p2 ~= pbg then n = n + 1; parts[n] = bgs[p2]; pbg = p2 end
				n = n + 1
				parts[n] = qg[4]                 -- upper half (bits 3)
			else
				local l0 = lums[p0]
				local l1 = lums[p1]
				local l2 = lums[p2]
				local l3 = lums[p3]
				local lmin = l0
				local lmax = l0
				local fgp = p0
				local bgp = p0
				if l1 < lmin then lmin = l1; bgp = p1 end
				if l1 > lmax then lmax = l1; fgp = p1 end
				if l2 < lmin then lmin = l2; bgp = p2 end
				if l2 > lmax then lmax = l2; fgp = p2 end
				if l3 < lmin then lmin = l3; bgp = p3 end
				if l3 > lmax then lmax = l3; fgp = p3 end
				local thr = (lmin + lmax + 1) // 2
				local bits = 0
				if l0 >= thr then bits = 1 end
				if l1 >= thr then bits = bits + 2 end
				if l2 >= thr then bits = bits + 4 end
				if l3 >= thr then bits = bits + 8 end
				if fgp ~= pfg then n = n + 1; parts[n] = fgs[fgp]; pfg = fgp end
				if bits ~= 15 and bgp ~= pbg then n = n + 1; parts[n] = bgs[bgp]; pbg = bgp end
				n = n + 1
				parts[n] = qg[bits + 1]
			end
		end
		n = n + 1
		parts[n] = reset
		rows[ry + 1] = concat(parts, "", 1, n)
	end
	return rows
end

-- sext mode: 2x3 pixels per cell (Unicode 13 "Symbols for Legacy Computing")
function rows_sext(pix, parts)
	local g = G
	local vw = g.vw
	local fgs = PAL_FG
	local bgs = PAL_BG
	local lums = PAL_LUM
	local sg = SEXTG
	local reset = RESET
	local rows = {}
	local vrows = g.vh // 3
	local cells = vw // 2
	for ry = 0, vrows - 1 do
		local n = 0
		local pfg = -1
		local pbg = -1
		local r0 = ry * 3 * vw + 1
		local r1 = r0 + vw
		local r2 = r1 + vw
		for cx = 0, cells - 1 do
			local xx = cx * 2
			local p0 = pix[r0 + xx]
			local p1 = pix[r0 + xx + 1]
			local p2 = pix[r1 + xx]
			local p3 = pix[r1 + xx + 1]
			local p4 = pix[r2 + xx]
			local p5 = pix[r2 + xx + 1]
			if p0 == p1 and p0 == p2 and p0 == p3 and p0 == p4 and p0 == p5 then
				if p0 ~= pbg then n = n + 1; parts[n] = bgs[p0]; pbg = p0 end
				n = n + 1
				parts[n] = " "
			else
				local l0 = lums[p0]
				local l1 = lums[p1]
				local l2 = lums[p2]
				local l3 = lums[p3]
				local l4 = lums[p4]
				local l5 = lums[p5]
				local lmin = l0
				local lmax = l0
				local fgp = p0
				local bgp = p0
				if l1 < lmin then lmin = l1; bgp = p1 end
				if l1 > lmax then lmax = l1; fgp = p1 end
				if l2 < lmin then lmin = l2; bgp = p2 end
				if l2 > lmax then lmax = l2; fgp = p2 end
				if l3 < lmin then lmin = l3; bgp = p3 end
				if l3 > lmax then lmax = l3; fgp = p3 end
				if l4 < lmin then lmin = l4; bgp = p4 end
				if l4 > lmax then lmax = l4; fgp = p4 end
				if l5 < lmin then lmin = l5; bgp = p5 end
				if l5 > lmax then lmax = l5; fgp = p5 end
				local thr = (lmin + lmax + 1) // 2
				local bits = 0
				if l0 >= thr then bits = 1 end
				if l1 >= thr then bits = bits + 2 end
				if l2 >= thr then bits = bits + 4 end
				if l3 >= thr then bits = bits + 8 end
				if l4 >= thr then bits = bits + 16 end
				if l5 >= thr then bits = bits + 32 end
				if fgp ~= pfg then n = n + 1; parts[n] = fgs[fgp]; pfg = fgp end
				if bits ~= 63 and bgp ~= pbg then n = n + 1; parts[n] = bgs[bgp]; pbg = bgp end
				n = n + 1
				parts[n] = sg[bits + 1]
			end
		end
		n = n + 1
		parts[n] = reset
		rows[ry + 1] = concat(parts, "", 1, n)
	end
	return rows
end

-- world -> camera -> screen; appends {depth, scx, art, size, zlift, flash, wx, wy}
function collect_billboard(sp, wx, wy, art, size, zlift,
		flash, inv, dirx, diry, planex, planey)
	local g = G
	local rx = wx - g.px
	local ry = wy - g.py
	local depth = inv * (planex * ry - planey * rx)
	if depth < 0.18 or depth > 15.0 then return end
	local ttx = inv * (diry * rx - dirx * ry)
	local scx = trunc((g.vw / 2.0) * (1.0 + ttx / depth))
	if scx < -g.vw or scx > g.vw * 2 then return end
	sp[#sp + 1] = {depth, scx, art, size, zlift, flash, wx, wy}
end

function draw_billboard(rec, pix, zbuf)
	local depth = rec[1]
	local scx = rec[2]
	local name = rec[3]
	local size = rec[4]
	local zlift = rec[5]
	local flash = rec[6]
	local art = ARTS[name]
	local aw = art.w
	local ah = art.h
	local apix = art.pix
	local g = G
	local vw = g.vw
	local vh = g.vh
	local span = 1.0 * vh / depth
	local ph = trunc(span * size)
	if ph < 2 then ph = 2 end
	-- pixel aspect: a cell is ~1:2 (w:h) split into PIXW x PIXH
	local pw = ph * aw * 2 * PIXW // (ah * PIXH)
	if pw < 2 then pw = 2 end
	local band = trunc(depth * 0.85) - (g.muzzle > 0 and 1 or 0)
	if band < 0 then band = 0 elseif band > 7 then band = 7 end
	local lut = sprite_lut(name, band, flash, SHOWCASE and g.lights:at(rec[7], rec[8]) or 0)
	local bottom = trunc(g.horizon + span * 0.5 - zlift * span)
	local top = bottom - ph
	local x0 = scx - pw // 2
	local x1 = x0 + pw
	local cx0 = x0 < 0 and 0 or x0
	local cx1 = x1 > vw and vw or x1
	local cy0 = top < 0 and 0 or top
	local cy1 = bottom > vh and vh or bottom
	for x = cx0, cx1 - 1 do
		local zd = zbuf[x + 1]
		if depth < zd then
			local acol = ((x - x0) * aw // pw) + 1
			for y = cy0, cy1 - 1 do
				local ay = (y - top) * ah // ph
				local code = apix[ay * aw + acol]
				if code > 0 then pix[y * vw + x + 1] = lut[code] end
			end
		end
	end
end

-- arcade automap: square tiles, walls in kind colors, live blips
function draw_automap(pix, tiles)
	local g = G
	local vw = g.vw
	local mw = g.mw
	local mh = g.mh
	local sx = PIXW * 2
	local sy = PIXH
	local ox = sx * 2
	local oy = sy * 2
	local kc = AM_COLS
	local am_floor = AM_FLOOR
	for ty = 0, mh - 1 do
		for tx = 0, mw - 1 do
			local t = tiles[ty * mw + tx + 1]
			local c = t == 0 and am_floor or kc[t + 1]
			if t == 6 and (g.tick & 8) == 0 then c = AM_BLINK end
			local bx = ox + tx * sx
			local by = oy + ty * sy
			for yy = 0, sy - 1 do
				local rb = (by + yy) * vw + bx + 1
				for xx = 0, sx - 1 do pix[rb + xx] = c end
			end
		end
	end
	-- blips: items gold, live enemies red, you white (+ heading pixel)
	local items = g.items
	for i = 1, #items do
		local it = items[i]
		if it[4] ~= 1 then
			blip(pix, ox + trunc(it[2]) * sx, oy + trunc(it[3]) * sy, AM_ITEM)
		end
	end
	local enemies = g.enemies
	for i = 1, #enemies do
		local e = enemies[i]
		if e.alive then
			blip(pix, ox + trunc(e.x) * sx, oy + trunc(e.y) * sy, AM_ENEMY)
		end
	end
	blip(pix, ox + trunc(g.px) * sx, oy + trunc(g.py) * sy, AM_SELF)
	blip(pix, ox + trunc(g.px + cos(g.pang) * 1.4) * sx, oy + trunc(g.py + sin(g.pang) * 1.4) * sy, AM_HEAD)
end

function blip(pix, bx, by, c)
	local g = G
	local vw = g.vw
	local vh = g.vh
	if bx < 0 or by < 0 or bx + 1 >= vw or by + 1 >= vh then return end
	pix[by * vw + bx + 1] = c
	pix[by * vw + bx + 2] = c
	pix[(by + 1) * vw + bx + 1] = c
	pix[(by + 1) * vw + bx + 2] = c
end

-- ------------------------------------------------------ the showcase look ---
-- per-floor tables (look.lua builds them), rebuilt by build_showcase_tables
LWALL = {}       -- [kind][variant][side][shade][tone]
LFLOOR = {}      -- [variant][shade][tone]
LCEIL = {}
WALLTEX = {}     -- wall tones [far][kind][ty][tx]
FLOORTEX = {}    -- floor tones [far][variant][v][u]
CEILTEX = {}
FLOOR_VAR = {}   -- per tile: which floor variant, times 256
CEIL_VAR = {}    -- per tile: 256 under a lamp
STAINS = {}      -- per tile 8x8 decal tones, 0 none
ROWD = {}        -- per pixel row: floor or ceiling distance
ROWS_SHADE = {}
ROWS_FAR = {}    -- per pixel row: 1 past FAR_TEXTURE

-- the showcase look's lit tables, textures and lamps for floor mi
function build_showcase_tables(mi)
	local g = G
	LWALL = showcase_lit_wall_tones(mi)
	LFLOOR = showcase_floor_tones(mi)
	LCEIL = showcase_ceiling_tones(mi)
	WALLTEX = showcase_wall_texture()
	FLOORTEX = showcase_floor_texture(mi)
	CEILTEX = showcase_ceiling_texture()
	g.lights:load()
	local mw, mh = g.mw, g.mh
	local lamps = g.lights.lamps
	FLOOR_VAR = {}
	CEIL_VAR = {}
	for ty = 0, mh - 1 do
		for tx = 0, mw - 1 do
			local i = ty * mw + tx + 1
			FLOOR_VAR[i] = (texel_noise(31, tx, ty) & 3) * 256
			CEIL_VAR[i] = lamps[i] and 256 or 0
		end
	end
	STAINS = {}
	for i = 1, mw * mh * 64 do STAINS[i] = 0 end
	ROWD = {}
	ROWS_SHADE = {}
	ROWS_FAR = {}
	for y = 1, g.vh do
		ROWD[y] = 0.0
		ROWS_SHADE[y] = 0
		ROWS_FAR[y] = 0
	end
end

-- the showcase look's view: a ray per pixel column, then each column top to
-- bottom: textured ceiling, the lit wall slice, textured floor with its
-- stains. Every surface reads its light texel and a lit table.
function showcase_view(px, py, dirx, diry, planex, planey)
	local g = G
	local vw = g.vw
	local vh = g.vh
	local horizon = vh // 2 + g.feel.pitch
	g.horizon = horizon
	local lights = g.lights
	lights:frame(g.feel.sources)

	-- the floor or ceiling distance and shade of every pixel row
	local hz = horizon
	local rowd = ROWD
	local rows_shade = ROWS_SHADE
	local rows_far = ROWS_FAR
	local far_texture = FAR_TEXTURE
	for y = 0, vh - 1 do
		local k = y + 0.5 - hz
		if k < 0.0 then k = 0.0 - k end
		local d = k > 0.3 and vh / (2.0 * k) or 60.0
		rowd[y + 1] = d
		local shade = 15 - floor(d * 1.55)
		rows_shade[y + 1] = shade < 0 and 0 or shade
		rows_far[y + 1] = d > far_texture and 1 or 0
	end

	local pix = g.pix
	local zb = g.zbuf
	local lg = lights.grid
	local gw = lights.gw
	local mw = g.mw
	local mk = g.mapkind
	local walls = LWALL
	local floors = LFLOOR
	local ceilings = LCEIL
	local wtex = WALLTEX
	local ftex = FLOORTEX
	local ctex = CEILTEX
	local fvar = FLOOR_VAR
	local cvar = CEIL_VAR
	local stain = STAINS
	local stride = WALL_TABLE_STRIDE
	local floor_tones = FLOOR_TONES
	local xstep = 2.0 / (vw - 1.0)
	for x = 0, vw - 1 do
		local camx = x * xstep - 1.0
		local rdx = dirx + planex * camx
		local rdy = diry + planey * camx
		local mx = floor(px)
		local my = floor(py)
		local adx = rdx < 0.0 and -rdx or rdx
		local ady = rdy < 0.0 and -rdy or rdy
		local ddx = adx < 0.00000001 and 100000000.0 or 1.0 / adx
		local ddy = ady < 0.00000001 and 100000000.0 or 1.0 / ady
		local stepx, sdx
		if rdx < 0.0 then stepx = -1; sdx = (px - mx) * ddx
		else stepx = 1; sdx = (mx + 1.0 - px) * ddx end
		local stepy, sdy
		if rdy < 0.0 then stepy = -1; sdy = (py - my) * ddy
		else stepy = 1; sdy = (my + 1.0 - py) * ddy end
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
		local dist = side == 0 and sdx - ddx or sdy - ddy
		if dist < 0.02 then dist = 0.02 end
		local wallx
		if side == 0 then wallx = py + dist * rdy else wallx = px + dist * rdx end
		wallx = wallx - floor(wallx)
		local tx = floor(wallx * 64.0) >> 1             -- texcol/2: 0..31
		zb[x + 1] = dist
		-- the wall's rows are those whose centers lie within half of the horizon,
		-- so every ceiling and floor pixel is strictly nearer than the wall
		local half = 0.5 * vh / dist
		local ytop = hz - half
		local y0 = 0 - floor(0.5 - ytop)
		local y1 = floor(hz - 0.5 + half) + 1
		local cy0 = y0 < 0 and 0 or (y0 > vh and vh or y0)
		local cy1 = y1 > vh and vh or (y1 < cy0 and cy0 or y1)

		-- ceiling
		local gi = x + 1
		for y = 0, cy0 - 1 do
			local d = rowd[y + 1]
			local fx = px + rdx * d
			local fy = py + rdy * d
			local ix = floor(fx)
			local iy = floor(fy)
			local u = floor((fx - ix) * 16.0)
			local v = floor((fy - iy) * 16.0)
			local lx = floor(fx * 4.0)
			local ly = floor(fy * 4.0)
			local tone = ctex[rows_far[y + 1] * 512 + cvar[iy * mw + ix + 1] + v * 16 + u + 1]
			pix[gi] = ceilings[(lg[ly * gw + lx + 1] * 16 + rows_shade[y + 1]) * 8 + tone + 1]
			gi = gi + vw
		end

		-- wall
		local shade = 15 - floor(dist * 1.55)
		if shade < 0 then shade = 0 elseif shade > 15 then shade = 15 end
		local back = dist - 0.03
		local wlx = floor((px + rdx * back) * 4.0)
		local wly = floor((py + rdy * back) * 4.0)
		local light = lg[wly * gw + wlx + 1]
		local wbase = kind * stride + ((light * 2 + side) * 16 + shade) * 8 + 1
		local tbase = (dist > far_texture and 9216 or 0) + kind * 1024 + tx + 1
		local tscale = 65536.0 / (2.0 * half)
		local tstep = floor(tscale)
		local tacc = floor((cy0 + 0.5 - ytop) * tscale)
		gi = cy0 * vw + x + 1
		for y = cy0, cy1 - 1 do
			pix[gi] = walls[wbase + wtex[tbase + ((tacc >> 11) & 31) * 32]]
			tacc = tacc + tstep
			gi = gi + vw
		end

		-- floor
		for y = cy1, vh - 1 do
			local d = rowd[y + 1]
			local fx = px + rdx * d
			local fy = py + rdy * d
			local ix = floor(fx)
			local iy = floor(fy)
			local u = floor((fx - ix) * 16.0)
			local v = floor((fy - iy) * 16.0)
			local lx = floor(fx * 4.0)
			local ly = floor(fy * 4.0)
			local ti = iy * mw + ix
			local tone = stain[ti * 64 + (v >> 1) * 8 + (u >> 1) + 1]
			if tone == 0 then tone = ftex[rows_far[y + 1] * 1024 + fvar[ti + 1] + v * 16 + u + 1] end
			pix[gi] = floors[(lg[ly * gw + lx + 1] * 16 + rows_shade[y + 1]) * floor_tones + tone + 1]
			gi = gi + vw
		end
	end
end

-- a decal on the floor: tone over the texels within radius of (x, y), each kept with chance keep/256
function stain_floor(x, y, radius, tone, keep)
	local g = G
	local mw = g.mw
	local tiles = g.tiles
	local stains = STAINS
	local x0 = trunc((x - radius) * 8.0)
	local x1 = trunc((x + radius) * 8.0)
	local y0 = trunc((y - radius) * 8.0)
	local y1 = trunc((y + radius) * 8.0)
	local r2 = radius * radius * 64.0
	for sy = imax(0, y0), imin(g.mh * 8 - 1, y1) do
		for sx = imax(0, x0), imin(mw * 8 - 1, x1) do
			local dx = sx + 0.5 - x * 8.0
			local dy = sy + 0.5 - y * 8.0
			if dx * dx + dy * dy <= r2 and (texel_noise(41, sx, sy) & 255) < keep then
				local tile = (sy >> 3) * mw + (sx >> 3)
				if tiles[tile + 1] == 0 then
					local i = tile * 64 + (sy & 7) * 8 + (sx & 7) + 1
					if not (tone == TONE_BLOOD and stains[i] == TONE_SCORCH) then stains[i] = tone end
				end
			end
		end
	end
end

-- old blood dries: one pass a second darkens a slice of the stains
function dry_stains(tick)
	local stains = STAINS
	local n = #stains
	local slice = n // 30 + 1
	local start = (tick % 30) * slice
	for i = start, imin(n, start + slice) - 1 do
		if stains[i + 1] == TONE_BLOOD and (texel_noise(43, i, tick // 30) & 7) == 0 then stains[i + 1] = TONE_BLOOD_DRY end
	end
end

-- the showcase look's gun in hand: sways with the walk, kicks on each shot
function draw_viewmodel()
	local g = G
	if g.automap then return end
	local firing = g.cooldown > WEAPONS[g.weapon + 1].cd - 4 or g.gun_kick > 0
	local name = VIEWMODEL_NAMES[g.weapon + 1]
	if firing and g.muzzle > 0 then name = name .. "_fire" end
	local art = VIEWMODELS[name]
	local lut = viewmodel_lut(name, g.muzzle > 0, g.lights:at(g.px, g.py))
	local vw = g.vw
	local vh = g.vh
	-- art pixels are square: a view pixel is PIXW x PIXH of a 1:2 cell
	local scale = vh * 0.4 / 24.0
	local aw = art.w
	local ah = art.h
	local ph = trunc(ah * scale)
	local pw = ph * aw * 2 * PIXW // (ah * PIXH)
	local sway = trunc(sin(g.bob * 0.5) * vw * 0.025)
	local lift = trunc(abs(cos(g.bob * 0.5)) * scale * 1.5) + g.gun_kick * trunc(scale * 0.9)
	local x0 = vw // 2 - pw // 2 + sway
	local y0 = vh - ph + lift + idivt(g.horizon - vh // 2, 2)
	local cx0 = imax(0, x0)
	local cx1 = imin(vw, x0 + pw)
	local cy1 = imin(vh, y0 + ph)
	local pix = g.pix
	local apix = art.pix
	for y = imax(0, y0), cy1 - 1 do
		local row = ((y - y0) * ah // ph) * aw + 1
		local base = y * vw + 1
		for x = cx0, cx1 - 1 do
			local code = apix[row + (x - x0) * aw // pw]
			if code > 0 then pix[base + x] = lut[code] end
		end
	end
end

-- palette code -> index for a gun under this floor's light, brighter in its own flash
function viewmodel_lut(name, flashing, variant)
	local g = G
	local key = name .. ":" .. tostring(flashing) .. ":" .. variant
	local lut = g.lutc[key]
	if lut then return lut end
	local mood = MOODS[g.map_i + 1]
	local art = VIEWMODELS[name]
	local light = cadd(cscale(cmix(mood.light, rgb(255, 255, 255), 0.4), flashing and 1.25 or 0.7),
		cscale(sprite_light(mood, variant), 0.8))
	lut = {}
	local argb = art.rgb
	local keys = art.keys
	for c = 1, #argb do
		local e = argb[c]
		local col_ = rgb(e[1], e[2], e[3])
		local b = keys[c]:byte(1)
		if b >= 65 and b <= 90 then
			lut[c] = color_idx(col_)
		else
			lut[c] = color_idx(cclamp(cmul(col_, light), 0.0, 1.0))
		end
	end
	g.lutc[key] = lut
	return lut
end
