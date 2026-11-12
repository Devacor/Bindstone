"""demoreel reel (scenes/reel.jai): the Scene base and the part of Reel the perf path runs.

Scene modules register themselves: a scene module ends with `SCENES[idx] = TheScene`
(JULIA 2, GLOBHUNTER 6, OSSUARY 9, MAELSTROM 10) and demoreel.py imports it. The Reel
composes a cue, and probes length_bars() / hit_times() / hit_levels(), only for the
registered indices; make_scene of any other index raises "scene I is not ported".

A scene module is a transliteration of its .jai file:
- `from reel import Scene, SCENES`; the class subclasses Scene, calls Scene.__init__ in
  its own __init__ (field defaults), and overrides build / update / render / text_rows
  and the hit_times / hit_levels / length_bars probes as the original does.
- The reel calls setup(w, h, seed): self.w / self.h are the scene's grid in pixels,
  self.rng the scene's Rng, self.pal its Palette (palette indices are what render
  writes into `grid`, a flat list of w * h ints it mutates in place), self.cue its Cue
  (music.Cue: kick_env, chord, energy, hit, ...). replay (seconds a jump replays) and
  pixel_budget are set in __init__ when the original sets them.
- Shared names: `import util` and read the presenter's tables as util.HB_PIX,
  util.HB_LUM, util.HB_RGB, util.HB_W, ... (hb_ensure and bind_palettes rebind them, so
  never `from util import HB_PIX`); functions and constants (Palette, Rng, gfx, ESC,
  RESET, idiv, imod, txt_put, txt_glyph, cell_col, sgr_fg, ...) import by name. Music
  constants come from music (MUSIC_BEAT, MUSIC_BAR, MUSIC_STEP).
- The scene's own globals are module globals of its file (`global` where it assigns them);
  like the reference's, they persist across boots.
- Ints: math::itrunc is int(x), math::ifloor and math::floor are math.floor (an int:
  wrap it in float() where the original keeps a float that can reach a string or an
  int-typed slot), int / int is // only when both are known non-negative, else
  util.idiv; % likewise with util.imod. Python's / on two ints is float division. Python raises where
  C returns inf or nan (x / 0.0, math.log(0.0), math.sqrt(-1.0), int(nan)), so check whether the
  original can reach any of those. parallel_transform(a, fn) is [fn(x) for x in a].
"""

import math

import util
from util import Palette, Rng, gfx, hb_ensure, hb_pixel_w, hb_pixel_h, txt_clear, hb_present, sgr_bg, sgr_fg
from music import MUSIC_BAR, MUSIC_BEAT, MUSIC_STEP, Cue, music_mood

# the cell shape: "half" blocks, "sext"ants, or "" for sextants while the grid stays within PIX_BUDGET pixels
PIX_MODE = ""
PIX_BUDGET = 48000

SCENES = {}


class Scene:
	def __init__(self):
		self.name = "SCENE"
		self.handle = ""
		self.w = 0
		self.h = 0
		self.seed = 0
		self.pal = None
		self.rng = None
		self.replay = 3.0
		self.cue = None
		self.pixel_budget = 0
		self.titled = True

	def setup(self, w_, h_, seed_):
		self.w = w_
		self.h = h_
		self.seed = seed_
		self.rng = Rng(seed_)
		self.pal = Palette()
		self.build()

	def build(self):
		pass

	def update(self, t, dt):
		pass

	def render(self, grid):
		pass

	def text_rows(self, rows):
		pass

	def hit_times(self):
		return []

	def hit_levels(self):
		return []

	def length_bars(self):
		return 0


class Reel:
	def __init__(self, w_, h_, seed_, backend_, smoke_, hud_):
		self.w = w_
		self.h = h_
		self.sext = PIX_MODE == "sext" or (PIX_MODE == "" and hb_pixel_w(w_, True) * hb_pixel_h(h_, True) <= PIX_BUDGET)
		self.pw = hb_pixel_w(w_, self.sext)
		self.ph = hb_pixel_h(h_, self.sext)
		self.seed = seed_
		self.backend = backend_
		self.smoke = smoke_
		self.hud = hud_
		self.hud_full = False
		self.paused = False
		self.nscenes = 12
		self.intro_idx = 12
		self.cur = None
		self.nxt = None
		self.cur_idx = 0
		self.pending_idx = 0
		self.scene_t = 0.0
		self.dur_cur = 10.0
		self.title_len = MUSIC_BAR
		self.trans_len = MUSIC_BEAT * 2.0
		self.in_trans = False
		self.finish_pending = False
		self.trans_t = 0.0
		pw = self.pw
		ph = self.ph
		self.grid_a = gfx.make_grid(pw * ph)
		self.grid_b = gfx.make_grid(pw * ph)
		self.flash_noise = []
		self.ring = []
		self.small_a = []
		self.small_b = []
		self.melt = gfx.make_fgrid(pw)
		ui = Palette()
		self.ui = ui
		self.ui_band = ui.size()
		for b in range(12):
			f = b / 5.0 if b < 6 else (11 - b) / 5.0
			ui.add(10.0 + 14.0 * f, 8.0 + 8.0 * f, 30.0 + 26.0 * f)
		self.ui_rule = ui.add(255, 206, 110)
		ui.add(110, 200, 255)
		self.ui_ink = ui.gradient(8, [[255.0, 250.0, 214.0], [255.0, 214.0, 110.0], [255.0, 128.0, 60.0], [214.0, 52.0, 92.0]])
		self.ui_shine = ui.add(255, 255, 255)
		self.ui_shadow = ui.add(4, 2, 12)
		self.ui_edge = ui.add(255, 244, 200)
		self.ui_off = 0
		self.off_b = 0
		self.bars = [32, 6, 6, 6, 7, 7, 8, 8, 6, 21, 8, 24, 4]
		self.seed_slots = [16, 3, 4, 5, 6, 8, 9, 10, 13, 15, 17, 23, 21]
		self.cues = [None] * (self.intro_idx + 1)
		for i in range(self.intro_idx + 1):
			if i not in SCENES:
				continue
			probe = self.make_scene(i)
			if probe.length_bars() > 0:
				self.bars[i] = probe.length_bars()
			self.cues[i] = Cue(self.bars[i], music_mood(i), self.seed + self.seed_slots[i] * 101, probe.hit_times(), probe.hit_levels())
		# the reference starts the intro here; the port has none and waits for demo_jump
		self.cur_idx = self.intro_idx

	def make_scene(self, idx):
		cls = SCENES.get(idx)
		if cls is None:
			raise RuntimeError(f"scene {idx} is not ported")
		return cls()

	def dur_for(self, idx):
		return 3.0 if self.smoke else self.bars[idx] * MUSIC_BAR

	# a scene at its grid, or at the largest grid within its pixel budget
	def setup_scene(self, sc, idx):
		sc.cue = self.cues[idx]
		pw = self.pw
		ph = self.ph
		sw = pw
		sh = ph
		if sc.pixel_budget > 0 and pw * ph > sc.pixel_budget:
			k = math.sqrt(sc.pixel_budget * 1.0 / (pw * ph))
			sw = int(pw * k)
			sh = int(ph * k)
		sc.setup(sw, sh, self.seed + self.seed_slots[idx] * 7919)

	# render into a grid of the reel's size, scaling up a scene that renders smaller
	def render_scene(self, sc, target, scratch):
		pw = self.pw
		ph = self.ph
		if sc.w == pw and sc.h == ph:
			sc.render(target)
			return
		if len(scratch) != sc.w * sc.h:
			scratch[:] = [0] * (sc.w * sc.h)
		sc.render(scratch)
		sw = sc.w
		sh = sc.h
		idx = 0
		for y in range(ph):
			row = (y * sh // ph) * sw
			for x in range(pw):
				target[idx] = scratch[row + x * sw // pw]
				idx += 1

	# the presenter's string tables: current scene, incoming scene (during a transition), then the reel's own UI entries
	def bind_palettes(self):
		hb_ensure(self.w, self.h, self.sext)
		util.HB_FG = []
		util.HB_BG = []
		util.HB_RGB = []
		util.HB_LUM = []
		self.absorb(self.cur.pal)
		self.off_b = len(util.HB_RGB)
		if self.in_trans:
			self.absorb(self.nxt.pal)
		self.ui_off = len(util.HB_RGB)
		self.absorb(self.ui)

	def absorb(self, p):
		hb_fg = util.HB_FG
		hb_bg = util.HB_BG
		hb_rgb = util.HB_RGB
		hb_lum = util.HB_LUM
		for i in range(len(p.rgb)):
			hb_fg.append(p.fg[i])
			hb_bg.append(p.bg[i])
			c = p.rgb[i]
			hb_rgb.append(c)
			hb_lum.append((c >> 16) * 2 + ((c >> 8) & 255) * 3 + (c & 255))

	def start_scene(self, idx, at_t):
		# Python's % already floors, the reference's ((idx % n) + n) % n
		self.cur_idx = idx if idx == self.intro_idx else idx % self.nscenes
		self.cur = self.make_scene(self.cur_idx)
		self.setup_scene(self.cur, self.cur_idx)
		self.dur_cur = self.dur_for(self.cur_idx)
		self.scene_t = at_t
		self.in_trans = False
		self.finish_pending = False
		self.nxt = None
		self.trans_t = 0.0
		self.bind_palettes()

	def complete_transition(self):
		raise RuntimeError("the port starts mid-scene and runs no transitions")

	def begin_transition(self):
		raise RuntimeError("the port starts mid-scene and runs no transitions")

	# a jump to a scene mid-flight: same scene, same clock; stateful scenes
	# replay up to their replay seconds so they arrive mid-flight instead of empty.
	def jump(self, idx, at_t):
		self.start_scene(idx, at_t)
		cur = self.cur
		steps = int(at_t * 30.0)
		most = int(cur.replay * 30.0)
		steps = most if steps > most else steps
		ft = at_t - steps * 0.0333333333
		for i in range(steps):
			ft += 0.0333333333
			cur.update(ft, 0.0333333333)

	def handle_key(self, key):
		if key == "":
			return
		raise RuntimeError("the port runs no keys")

	def ensure_noise(self):
		pw = self.pw
		ph = self.ph
		n = pw * ph
		if len(self.flash_noise) == n:
			return
		noise_rng = Rng(self.seed + 977)
		flash_noise = []
		ring = []
		self.flash_noise = flash_noise
		self.ring = ring
		for i in range(n):
			flash_noise.append(noise_rng.next(1000))
			dx = (i % pw - pw * 0.5) / (pw * 0.5)
			dy = (i // pw - ph * 0.5) / (ph * 0.5)
			ring.append(int((dx * dx + dy * dy) * 500.0))

	def dither_flash(self, keep):
		self.ensure_noise()
		pix = util.HB_PIX
		shine = self.ui_off + self.ui_shine
		noise = self.flash_noise
		for i in range(self.pw * self.ph):
			if noise[i] < keep:
				pix[i] = shine

	# every big hit lands on the whole frame: over the bar before it an iris closes in,
	# tightest through the score's beat of silence, then the hit throws it open and shakes the picture
	def drop_fx(self, sc, t):
		if sc.cue is None:
			return
		cue = sc.cue
		close = 0.0
		since = 9.0
		hit_t = cue.hit_t
		hit_level = cue.hit_level
		for k in range(len(hit_t)):
			if hit_level[k] < 2:
				continue
			to = hit_t[k] - t
			if to > 0.0 and to <= MUSIC_BAR:
				c = 1.0 - to / MUSIC_BAR
				c = 0.62 if to <= MUSIC_STEP * 4.0 else c * c * 0.45
				close = c if c > close else close
			if to <= 0.0 and 0.0 - to < since:
				since = 0.0 - to
		if close > 0.0:
			self.ensure_noise()
			pix = util.HB_PIX
			noise = self.flash_noise
			rg = self.ring
			dark = self.ui_off + self.ui_shadow
			# past the iris everything is dark, across a thin rim it dithers in
			edge = int((1.04 - close) * 1000.0)
			rim = 70
			for i in range(self.pw * self.ph):
				over = rg[i] - edge + rim
				if over > 0 and (over >= rim or noise[i] * rim < over * 1000):
					pix[i] = dark
		if since < 0.32:
			amp = (0.32 - since) / 0.32 * (4.0 if self.sext else 2.5)
			dx = int(math.sin(t * 97.0) * amp)
			dy = int(math.cos(t * 71.0) * amp * 0.6)
			if dx != 0 or dy != 0:
				self.shift_frame(dx, dy)

	# the frame moved by (dx, dy) pixels, its edges smeared in
	def shift_frame(self, dx, dy):
		pix = util.HB_PIX
		tmp = self.grid_b
		pw = self.pw
		ph = self.ph
		n = pw * ph
		for i in range(n):
			tmp[i] = pix[i]
		i2 = 0
		for y in range(ph):
			sy = y - dy
			sy = 0 if sy < 0 else (ph - 1 if sy >= ph else sy)
			row = sy * pw
			for x in range(pw):
				sx = x - dx
				sx = 0 if sx < 0 else (pw - 1 if sx >= pw else sx)
				pix[i2] = tmp[row + sx]
				i2 += 1

	def title_pixels(self):
		raise RuntimeError("the port starts mid-scene and draws no title cards")

	def title_text(self, rows, row):
		if row < 0:
			return
		c = util.HB_RGB[self.ui_off + self.ui_band + 10]
		gfx.overlay_center(rows, self.w, row, self.cur.handle, sgr_bg(c // 65536, (c // 256) % 256, c % 256) + sgr_fg(170, 184, 240))

	def frame(self, t, dt, key, fps, ms_sim, ms_draw, ms_other):
		if self.cur is None:
			raise RuntimeError("the port starts mid-scene: demo_jump comes before demo_frame")
		if self.finish_pending:
			self.complete_transition()
		self.handle_key(key)
		hb_ensure(self.w, self.h, self.sext)
		txt_clear()
		if len(util.HB_RGB) == 0:
			self.bind_palettes()
		step = 0.0 if self.paused else dt
		self.scene_t += step
		if not self.in_trans and self.scene_t >= self.dur_cur:
			self.begin_transition()
		cur = self.cur
		cur.update(self.scene_t, step)
		self.render_scene(cur, util.HB_PIX, self.small_a)
		self.drop_fx(cur, self.scene_t)
		trow = -1
		if not self.in_trans and not self.finish_pending and self.scene_t < self.title_len and cur.titled:
			trow = self.title_pixels()
		rows = hb_present()
		self.title_text(rows, trow)
		if not self.in_trans:
			cur.text_rows(rows)
		if self.hud:
			raise RuntimeError("the port runs no HUD")
		return "\n".join(rows)
