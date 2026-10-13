-- Scene interface + the Reel manager: timed auto-advance, transitions, title
-- cards, HUD. Everything the host calls funnels through Reel:frame().
-- Scenes see h as PIXEL rows (two per terminal row) and paint square pixels.

Scene = class()

function Scene:init()
	self.name = "SCENE"
	self.handle = ""
	self.w = 0
	self.h = 0
	self.seed = 0
	self.pal = nil
	self.rng = nil
end

function Scene:setup(w_, h_, seed_)
	self.w = w_
	self.h = h_
	self.seed = seed_
	self.rng = Rng.new(seed_)
	self.pal = Palette.new()
	self:build()
end

function Scene:build() end
function Scene:update(t, dt) end
function Scene:render(grid) end
function Scene:caption() return "" end

Reel = class()

function Reel:init(w_, h_, seed_, backend_, smoke_, hud_)
	self.paused = false
	self.nscenes = 17
	self.cur = nil
	self.nxt = nil
	self.cur_idx = 0
	self.pending_idx = 0
	self.scene_t = 0.0
	self.dur_cur = 10.0
	self.title_len = 2.4
	self.trans_len = 1.1
	self.in_trans = false
	self.finish_pending = false
	self.trans_t = 0.0
	self.trans_kind = 0
	self.trans_count = 0
	self.ui_off = 0
	self.off_b = 0

	self.w = w_
	self.h = h_
	self.ph = h_ * 2
	self.seed = seed_
	self.backend = backend_
	self.smoke = smoke_
	self.hud = hud_
	self.grid_a = make_grid(self.w * self.ph)
	self.grid_b = make_grid(self.w * self.ph)
	self.melt = make_fgrid(self.w)
	local ui = Palette.new()
	self.ui = ui
	self.ui_band = ui:size()
	for b = 0, 11 do
		local f = b < 6 and b / 5.0 or (11 - b) / 5.0
		ui:add(10.0 + 14.0 * f, 8.0 + 8.0 * f, 30.0 + 26.0 * f)
	end
	self.ui_rule = ui:add(255, 206, 110)
	ui:add(110, 200, 255)
	self.ui_ink = ui:gradient(8, {{255.0, 250.0, 214.0}, {255.0, 214.0, 110.0}, {255.0, 128.0, 60.0}, {214.0, 52.0, 92.0}})
	self.ui_shine = ui:add(255, 255, 255)
	self.ui_shadow = ui:add(4, 2, 12)
	self.ui_edge = ui:add(255, 244, 200)
	self.durs = {12.0, 11.0, 12.0, 11.0, 11.0, 12.0, 14.0, 14.0, 12.0, 15.0, 15.0, 16.0, 16.5, 12.0, 11.0, 20.0, 18.0}
	self:start_scene(0, 0.0)
end

-- looked up per call, so a hot reload's fresh classes build the next scene
function Reel:make_scene(idx)
	local classes = {Plasma, Starfield, Tunnel, Donut, Julia, Lava, Pipes, SandBox, Boids, Kinstein,
		CanyonRun, SpongeWorks, BannerFall, Chrome, Fire, Ossuary, Finale}
	return (classes[idx + 1] or Finale).new()
end

function Reel:dur_for(idx)
	return self.smoke and 3.0 or self.durs[idx + 1]
end

-- the presenter's string tables: current scene, incoming scene (during a
-- transition), then the reel's own UI entries
function Reel:bind_palettes()
	hb_ensure(self.w, self.h)
	HB_FG = {}
	HB_BG = {}
	HB_RGB = {}
	self:absorb(self.cur.pal)
	self.off_b = #HB_RGB
	if self.in_trans then self:absorb(self.nxt.pal) end
	self.ui_off = #HB_RGB
	self:absorb(self.ui)
end

function Reel:absorb(p)
	local fg, bg, rgb = HB_FG, HB_BG, HB_RGB
	local n0 = #rgb
	for i = 1, #p.rgb do
		fg[n0 + i] = p.fg[i]
		bg[n0 + i] = p.bg[i]
		rgb[n0 + i] = p.rgb[i]
	end
end

function Reel:start_scene(idx, at_t)
	local n = self.nscenes
	self.cur_idx = ((idx % n) + n) % n
	self.cur = self:make_scene(self.cur_idx)
	self.cur:setup(self.w, self.ph, self.seed + self.cur_idx * 7919)
	self.dur_cur = self:dur_for(self.cur_idx)
	self.scene_t = at_t
	self.in_trans = false
	self.finish_pending = false
	self.nxt = nil
	self.trans_t = 0.0
	self:bind_palettes()
end

-- runs at the top of the frame AFTER the one that finished the transition
function Reel:complete_transition()
	self.finish_pending = false
	self.in_trans = false
	self.cur = self.nxt
	self.nxt = nil
	self.cur_idx = self.pending_idx
	self.dur_cur = self:dur_for(self.cur_idx)
	self.scene_t = self.trans_t
	self.trans_t = 0.0
	self:bind_palettes()
end

function Reel:begin_transition()
	self.pending_idx = (self.cur_idx + 1) % self.nscenes
	self.nxt = self:make_scene(self.pending_idx)
	self.nxt:setup(self.w, self.ph, self.seed + self.pending_idx * 7919)
	self.in_trans = true
	self.trans_t = 0.0
	self.trans_kind = (self.trans_count + self.seed) % 5
	self.trans_count = self.trans_count + 1
	local m = 0
	local melt = self.melt
	for x = 0, self.w - 1 do
		m = m + ((x * 7 + self.trans_count * 13 + (x * x) % 11) % 3) - 1
		m = m < 0 and 0 or (m > 14 and 14 or m)
		melt[x + 1] = m * 1.0
	end
	self:bind_palettes()
end

-- backend swap lands here: same scene, same clock; stateful scenes replay
-- up to 3 sim-seconds so they arrive mid-flight instead of empty.
function Reel:jump(idx, at_t)
	self:start_scene(idx, at_t)
	local steps = itrunc(at_t * 30.0)
	steps = steps > 90 and 90 or steps
	local ft = at_t - steps * 0.0333333333
	for i = 1, steps do
		ft = ft + 0.0333333333
		self.cur:update(ft, 0.0333333333)
	end
end

function Reel:reload_current()
	local keep_t = self.scene_t
	self:start_scene(self.cur_idx, keep_t)
end

function Reel:handle_key(key)
	if key == "" then return end
	if key == "h" then self.hud = not self.hud
	elseif key == "f" then self.paused = not self.paused
	elseif key == "n" or key == " " or key == "right" then self:start_scene(self.cur_idx + 1, 0.0)
	elseif key == "p" or key == "left" then self:start_scene(self.cur_idx - 1, 0.0)
	elseif #key == 1 then
		local c0 = key:byte(1)
		if c0 >= 49 and c0 <= 57 then
			local d = c0 - 48
			if d <= self.nscenes then self:start_scene(d - 1, 0.0) end
		elseif c0 == 48 then self:start_scene(self.nscenes - 1, 0.0) end
	end
end

-- the title card: a band opens from the screen's middle, the scene name
-- types itself in big pixel letters (a shine sweeps across them), the
-- handle sits under it as crisp text, and the band folds shut again
function Reel:title_pixels()
	local scene_t = self.scene_t
	local open = scene_t / 0.28
	local close = (self.title_len - scene_t) / 0.3
	local k = open < close and open or close
	k = k > 1.0 and 1.0 or k
	if k <= 0.0 then return -1 end
	local w, ph, ui_off = self.w, self.ph, self.ui_off
	local y0 = ((ph // 2 - 12) // 2) * 2
	local mid = y0 + 12
	local reach = itrunc(k * 12.0 + 0.5)
	local pix = HB_PIX
	local band = ui_off + self.ui_band
	for y = mid - reach, mid + reach - 1 do
		if y >= 0 and y < ph then
			local ry = y - y0
			local c = ry == 0 and ui_off + self.ui_rule or (ry == 23 and ui_off + self.ui_rule + 1 or band + ry // 2)
			local gi = y * w
			for x = 1, w do pix[gi + x] = c end
		end
	end
	if k < 1.0 then return -1 end
	local name = self.cur.name
	local sx = gfx:text_width(name, 2) <= w - 6 and 2 or 1
	local tw = gfx:text_width(name, sx)
	local tx = (w - tw) // 2
	local ty = y0 + 3
	local shown = itrunc((scene_t - 0.28) * 34.0) + 1
	gfx:draw_text(pix, w, ph, name, tx, ty, sx, 2, ui_off + self.ui_ink, 8, ui_off + self.ui_shadow, shown)
	-- the shine: a diagonal stripe of white sweeping over the ink
	local sweep = itrunc((scene_t - 0.6) * 90.0) - 20
	local ink0 = ui_off + self.ui_ink
	local shine = ui_off + self.ui_shine
	for y2 = ty, ty + 13 do
		local gi2 = y2 * w + 1
		for x2 = tx, tx + tw - 1 do
			local d = x2 + (y2 - ty) - sweep - tx
			if d >= 0 and d < 5 then
				local pv = pix[gi2 + x2]
				if pv >= ink0 and pv < ink0 + 8 then pix[gi2 + x2] = shine end
			end
		end
	end
	return (y0 + 20) // 2
end

function Reel:title_text(rows, row)
	if row < 0 then return end
	local c = HB_RGB[self.ui_off + self.ui_band + 10 + 1]
	local bgc = ESC .. "[48;2;" .. (c // 65536) .. ";" .. ((c // 256) % 256) .. ";" .. (c % 256) .. "m"
	gfx:overlay_center(rows, self.w, row, self.cur.handle, bgc .. ESC .. "[38;2;170;184;240m")
end

-- ms_sim = script update + frame string; ms_draw = host console write.
-- The headline number is their SUM: the honest wall cost of a frame.
function Reel:hud_overlay(rows, fps, ms_sim, ms_draw, ms_other)
	local other = self.backend == "lua" and "jai" or "lua"
	local ms_o = ""
	if ms_other > 0.0 then ms_o = " (" .. other .. " " .. gfx:fmt1(ms_other) .. ")" end
	local flow = self.paused and " [FROZEN]" or ""
	local txt = " LUA DEMOREEL " .. (self.cur_idx + 1) .. "/" .. self.nscenes .. " " .. self.cur.name ..
		" | " .. self.backend .. " " .. gfx:fmt1(ms_sim + ms_draw) .. "ms = sim " .. gfx:fmt1(ms_sim) ..
		" + draw " .. gfx:fmt1(ms_draw) .. ms_o ..
		" | " .. gfx:fmt1(fps) .. " fps" .. flow ..
		" | " .. self.w .. "x" .. self.ph .. " px | r h f n p q"
	gfx:overlay_left(rows, self.w, 0, txt, ESC .. "[48;2;24;16;48m" .. ESC .. "[38;2;235;235;255m")
end

function Reel:frame(t, dt, key, fps, ms_sim, ms_draw, ms_other)
	if self.finish_pending then self:complete_transition() end
	self:handle_key(key)
	hb_ensure(self.w, self.h)
	if #HB_RGB == 0 then self:bind_palettes() end
	local step = self.paused and 0.0 or dt
	self.scene_t = self.scene_t + step
	if not self.in_trans and self.scene_t >= self.dur_cur then self:begin_transition() end
	self.cur:update(self.scene_t, step)
	if self.in_trans then
		self.trans_t = self.trans_t + step
		self.cur:render(self.grid_a)
		self.nxt:update(self.trans_t, step)
		self.nxt:render(self.grid_b)
		local prog = self.trans_t / self.trans_len
		prog = prog > 1.0 and 1.0 or prog
		gfx:mix_grids(self.grid_a, self.grid_b, HB_PIX, self.w, self.ph, self.trans_kind, prog, self.off_b,
			self.ui_off + self.ui_edge, self.melt)
		if self.trans_t >= self.trans_len then self.finish_pending = true end
	else
		self.cur:render(HB_PIX)
	end
	local trow = -1
	if not self.in_trans and not self.finish_pending and self.scene_t < self.title_len then trow = self:title_pixels() end
	local rows = hb_present()
	self:title_text(rows, trow)
	if not self.in_trans then
		local cap = self.cur:caption()
		if cap ~= "" then gfx:overlay_center(rows, self.w, self.h - 3, cap, ESC .. "[48;2;6;4;12m" .. ESC .. "[38;2;255;226;160m") end
	end
	if self.hud then self:hud_overlay(rows, fps, ms_sim, ms_draw, ms_other) end
	return table.concat(rows, "\n")
end
