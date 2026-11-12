"""GLOBHUNTER (scenes/globhunter.jai, scene index 6): a Wolfenstein-style grid raycaster over an
authored 24x27 keep of three-wide halls under a two-unit vault. One DDA ray per screen column,
perpendicular-distance slices, textured walls (16x32 textures grown in script), floor and
ceiling cast per pixel across the spans the walls leave open, a light map rebuilt from
flickering torches (and the muzzle on each shot), billboard Grublins and Orglis brutes lit as
spheres per pixel, goo that bursts in 3D and bounces, a boss, an escape and a breach, all on
the scene's cue. The scripted camera hunts the garrison and fires on the beat. The ray fan
comes from the camera-plane vector: no per-column trig.
"""

import math

from music import MUSIC_BAR, MUSIC_BEAT
from reel import Scene, SCENES
from util import gfx, idiv

BAYER4 = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]


class GlobHunter(Scene):
	def __init__(self):
		Scene.__init__(self)
		self.wmap = []
		self.mw = 24
		self.mh = 27
		self.wpx = []
		self.wpy = []
		self.seglen = []
		self.nwp = 6
		self.path_len = 0.0
		self.travel = 0.0      # distance along the route: it crawls through each hit-stop
		self.posx = 2.5
		self.posy = 2.5
		self.dirx = 0.0
		self.diry = 1.0
		self.planex = 0.0
		self.planey = 0.0
		self.aim = 0.0         # the eased swing of the view toward the current mark
		self.pitch = 0.0       # the horizon's offset in pixels: walk bob and recoil
		self.tex = []          # 3 wall textures (16x32) + floor + ceiling (16x16) + their plain copies, texel indices 0..7
		self.wall_base = 0     # ((type*2 + side)*8 + light)*8 + texel
		self.banner_base = 0   # (side*8 + light)*16 + hue
		self.floor_base = 0    # light*8 + texel
		self.ceil_base = 0
		self.sprite_base = 0
		self.tt = 0.0
		self.fr = 0
		# the bestiary (parallel arrays): type 0 grublin, 1 orglis
		self.ex = []
		self.ey = []
		self.et = []
		self.es = []           # 0 alive, 1 hit-flash, 2 gibbing, 3 down (respawns)
		self.est = []
		self.stx = []          # per-frame camera-space transforms
		self.sty = []
		self.drawn = []
		self.zcol = []         # per-column wall depth from the last wall pass
		self.wall_top = []     # per column: the slice's first row and the row past its last
		self.wall_foot = []
		self.wall_hit = []     # ((cell*2 + side)*16 + texel column)*8 + light
		self.ceil_lo = []      # per row: the first and last column whose slice starts there (its ceiling ends)
		self.ceil_hi = []
		self.floor_lo = []     # and whose slice ends there (its floor begins)
		self.floor_hi = []
		self.gun = []          # 16 texels a row: a sprite palette offset, 0 where the sprite is clear
		self.ne = 18           # seventeen of the garrison (three wait in ambush for the escape), and the boss in the last slot
		self.boss_flash = 0.0  # the boss shrugs off each hit with a flash until bar nine
		self.kills = 0
		self.cool = 0.6
		self.last_step = -1    # the last beat (or eighth) the gun was allowed to fire on
		self.muzzle_t = 0.0
		self.tracer_t = 0.0
		self.hot_t = 0.0
		self.kick_t = 0.0
		self.stop_t = 0.0      # the hit-stop
		self.last_kill = -9.0
		self.combo = 0
		self.call_t = 0.0      # the callout's life
		self.callout = ""
		self.tr_x = 0
		self.tr_y = 0
		# torches: positions; the light map's steady sum of them, and which one rules each cell
		self.tox = []
		self.toy = []
		self.still = []
		self.owner = []
		self.lmap = []         # two cells per map cell each way, light levels 0..5
		self.lw = 48
		self.lh = 54
		self.vault = 2.0       # the walls' height; the eye rides at 0.5
		# goo: a pool of particles in world space (z up, 0 = floor, 1 = ceiling)
		self.gx = []
		self.gy = []
		self.gz = []
		self.gvx = []
		self.gvy = []
		self.gvz = []
		self.glife = []
		self.gtype = []
		self.ngoo = 220
		self.goo_next = 0
		self.vigx = []         # the vignette: light levels lost toward the edges
		self.vigy = []
		self.rubble_base = 0   # falling stone, 12 levels
		self.breach_x = 23     # the wall the escape blows open, east at the end of the mid hall
		self.breach_y = 13
		self.last_snare = 0.0

	def build(self):
		self.name = "GLOBHUNTER"
		self.handle = "grublin keep . torchlight, one ray per column, zero mercy"
		self.replay = 26.0
		pal = self.pal
		w = self.w
		h = self.h
		mw = self.mw
		mh = self.mh
		# # stone  M moss  G gold pillar  B banner (rainbow-fire, animated)
		rows = [
			"##################BBBB##",
			"#.......#########......#",
			"#......................#",
			"#......................#",
			"#.....G................#",
			"#.......MMMMMMMMM......#",
			"##...###############...#",
			"#M...M##############...#",
			"##...B##############...#",
			"##...##############M...M",
			"##...####MMMMMMMMM##...#",
			"##...####.........##...#",
			"##...####..............#",
			"##...####..............#",
			"##...####..............#",
			"##...####.........######",
			"##...####MMM...MMM######",
			"##...####.........######",
			"##...####.........######",
			"##...####.........######",
			"#.......#.........######",
			"#.................######",
			"#.................######",
			"#.................######",
			"#.G..G..#.........######",
			"#.......#.........######",
			"##BBBB##################"
		]
		wmap = self.wmap
		for y in range(mh):
			row = rows[y]
			for x in range(mw):
				ch = row[x]
				v = 0
				if ch == "#":
					v = 1
				elif ch == "M":
					v = 2
				elif ch == "G":
					v = 3
				elif ch == "B":
					v = 4
				wmap.append(v)
		wmap[self.breach_y * mw + self.breach_x] = 5
		fogc = [5.0, 7.0, 18.0]
		wall_cols = [
			[[40.0, 36.0, 44.0], [88.0, 88.0, 100.0], [104.0, 104.0, 118.0], [120.0, 120.0, 136.0], [138.0, 138.0, 152.0], [156.0, 156.0, 170.0], [186.0, 186.0, 200.0], [70.0, 70.0, 82.0]],
			[[34.0, 36.0, 34.0], [84.0, 90.0, 92.0], [100.0, 106.0, 108.0], [118.0, 124.0, 126.0], [60.0, 118.0, 50.0], [84.0, 150.0, 62.0], [120.0, 190.0, 80.0], [40.0, 86.0, 36.0]],
			[[90.0, 60.0, 20.0], [140.0, 100.0, 36.0], [176.0, 130.0, 50.0], [206.0, 160.0, 64.0], [232.0, 190.0, 90.0], [252.0, 222.0, 130.0], [255.0, 246.0, 200.0], [118.0, 80.0, 28.0]]]
		lit = self.lit
		self.wall_base = pal.size()
		for t in range(3):
			cols = wall_cols[t]
			for side in range(2):
				for f in range(8):
					for c in range(8):
						cc = cols[c]
						lit(cc[0], cc[1], cc[2], f, side, fogc)
		self.banner_base = pal.size()
		for side2 in range(2):
			for f2 in range(8):
				for hu in range(4):
					rc = gfx.rainbow_rgb(hu * 1.5708)
					rr = rc[0]
					rg = rc[1]
					rb = rc[2]
					f3 = 3 if f2 < 3 else f2   # the banner's fire glows in the dark
					lit(40.0, 16.0, 60.0, f3, side2, fogc)
					lit(rr * 0.45, rg * 0.45, rb * 0.45, f3, side2, fogc)
					lit(rr, rg, rb, f3, side2, fogc)
					lit(255.0, 196.0, 80.0, f3, side2, fogc)
		floor_cols = [[46.0, 38.0, 34.0], [92.0, 80.0, 70.0], [104.0, 92.0, 80.0], [116.0, 102.0, 88.0], [128.0, 112.0, 96.0], [140.0, 124.0, 106.0], [76.0, 66.0, 58.0], [160.0, 142.0, 120.0]]
		self.floor_base = pal.size()
		for f4 in range(8):
			for c3 in range(8):
				fc = floor_cols[c3]
				lit(fc[0], fc[1], fc[2], f4, 0, fogc)
		ceil_cols = [[20.0, 12.0, 8.0], [60.0, 38.0, 22.0], [72.0, 46.0, 26.0], [84.0, 54.0, 30.0], [96.0, 62.0, 34.0], [50.0, 32.0, 18.0], [110.0, 72.0, 40.0], [36.0, 24.0, 14.0]]
		self.ceil_base = pal.size()
		for f5 in range(8):
			for c4 in range(8):
				kc = ceil_cols[c4]
				lit(kc[0] * 0.8, kc[1] * 0.8, kc[2] * 0.8, f5, 0, fogc)
		# sprites + combat kit
		self.sprite_base = pal.size()
		pal.ramp(12, 10.0, 34.0, 12.0, 140.0, 240.0, 110.0)      # +0..11 grublin
		pal.ramp(12, 36.0, 16.0, 10.0, 230.0, 140.0, 80.0)       # +12..23 orglis
		pal.add(255, 255, 255)                                   # +24 hit flash
		pal.add(250, 246, 220)                                   # +25 eye white
		pal.add(20, 8, 8)                                        # +26 pupil / mouth
		pal.ramp(4, 36.0, 38.0, 48.0, 150.0, 156.0, 176.0)       # +27..30 gun
		pal.add(255, 250, 200)                                   # +31 muzzle core
		pal.add(255, 196, 70)                                    # +32 muzzle rim
		pal.add(255, 110, 30)                                    # +33 muzzle tip
		for tr in range(6):                                      # +34..39 rainbow tracer
			ct = gfx.rainbow_rgb(tr * 1.0471975512)
			pal.add(ct[0], ct[1], ct[2])
		pal.add(130, 255, 150)                                   # +40 crosshair
		pal.add(255, 84, 64)                                     # +41 crosshair, target down
		pal.add(255, 226, 100)                                   # +42 tally ink
		pal.add(255, 140, 50)                                    # +43 tally ink low
		pal.add(8, 6, 10)                                        # +44 tally shadow
		pal.add(170, 30, 30)                                     # +45 grin
		pal.add(226, 170, 128)                                   # +46 skin
		pal.add(150, 100, 72)                                    # +47 skin shade
		pal.add(255, 60, 40)                                     # +48 glowing eye
		pal.ramp(4, 120.0, 255.0, 120.0, 230.0, 255.0, 200.0)    # +49..52 callout ink, grublin goo glint
		pal.ramp(4, 255.0, 120.0, 220.0, 255.0, 240.0, 255.0)    # +53..56 combo ink
		self.rubble_base = pal.size()
		pal.ramp(12, 22.0, 20.0, 28.0, 196.0, 186.0, 172.0)
		self.grow_textures()
		gun_rows = ["......dlhd......", "......dlmd......", "......dlmd......", ".....ddlmdd.....", ".....dlhmmd.....",
			"....ddlmmmdd....", "....dlhmmmmd....", "...kdlmmmmmdk...", "..kkdmmmmmmdkk..", ".kkkkdmmmmdkkkk.",
			"skkkkkddddkkkkks", "sskkkkkkkkkkkkss"]
		gun = self.gun
		for gr in range(len(gun_rows)):
			gun_row = gun_rows[gr]
			for gc in range(16):
				ch2 = gun_row[gc]
				gun.append(0 if ch2 == "." else (27 if ch2 == "d" else (28 if ch2 == "m" else (29 if ch2 == "l" else (30 if ch2 == "h" else (46 if ch2 == "k" else 47))))))
		# the flight loop down the middle of each hall: west hall, south room and arena, mid room, east hall
		self.wpx = [3.5, 3.5, 13.5, 13.5, 22.5, 22.5]
		self.wpy = [3.5, 22.5, 22.5, 13.5, 13.5, 3.5]
		wpx = self.wpx
		wpy = self.wpy
		nwp = self.nwp
		seglen = self.seglen
		for i in range(nwp):
			ddx = wpx[(i + 1) % nwp] - wpx[i]
			ddy = wpy[(i + 1) % nwp] - wpy[i]
			seglen.append(math.sqrt(ddx * ddx + ddy * ddy))
			self.path_len += seglen[i]
		# garrison along the route (three hold the south room for the frenzy, three wait along the escape);
		# every post stands farther from the nearest wall than its kind's reach, the boss in the arena's open floor
		self.ex = [3.5, 3.0, 4.0, 3.0, 7.0, 15.5, 13.5, 14.5, 19.0, 21.5, 20.5, 10.0, 11.5, 12.0, 11.0, 17.5, 21.0, 13.5]
		self.ey = [7.5, 13.5, 17.5, 21.2, 21.0, 21.0, 14.0, 12.5, 13.5, 8.5, 3.0, 22.5, 23.5, 21.8, 12.5, 13.5, 13.0, 18.5]
		self.et = [0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1]
		ne = self.ne
		es = self.es
		est = self.est
		for i2 in range(ne):
			es.append(0)
			est.append(0.0)
			self.stx.append(0.0)
			self.sty.append(0.0)
			self.drawn.append(0)
		es[ne - 1] = 3   # the boss waits for bar seven
		for a in range(ne - 4, ne - 1):
			# the ambush rises (a downed mark stands again after 16 s) as the escape begins
			es[a] = 3
			est[a] = 16.0 - MUSIC_BAR * 10.0
		self.zcol = gfx.make_fgrid(w)
		zcol = self.zcol
		for i3 in range(w):
			zcol[i3] = 99.0
		self.wall_top = gfx.make_grid(w)
		self.wall_foot = gfx.make_grid(w)
		self.wall_hit = gfx.make_grid(w)
		self.ceil_lo = gfx.make_grid(h + 1)
		self.ceil_hi = gfx.make_grid(h + 1)
		self.floor_lo = gfx.make_grid(h + 1)
		self.floor_hi = gfx.make_grid(h + 1)
		# torches hang beside the walls along the route; each keeps its falloff over the light map
		spots = [[1.2, 3.5], [7.5, 1.3], [2.2, 9.5], [4.8, 15.5], [1.2, 22.5], [7.5, 20.3], [9.3, 19.5],
			[17.7, 22.5], [11.4, 17.3], [15.6, 17.3], [9.3, 13.5], [15.5, 11.2], [20.5, 14.8], [22.8, 7.5], [22.8, 2.5], [17.2, 1.4]]
		tox = self.tox
		toy = self.toy
		for k in range(len(spots)):
			sp = spots[k]
			px = sp[0]
			py = sp[1]
			if wmap[int(py) * mw + int(px)] != 0:
				continue
			tox.append(px)
			toy.append(py)
		lw = self.lw
		lh = self.lh
		still = self.still
		owner = self.owner
		best = []
		for c5 in range(lw * lh):
			still.append(0.0)
			owner.append(0)
			best.append(0.0)
		for k3 in range(len(tox)):
			for c6 in range(lw * lh):
				lx = (c6 % lw + 0.5) * 0.5 - tox[k3]
				ly = (c6 // lw + 0.5) * 0.5 - toy[k3]
				f = 1.0 / (1.0 + (lx * lx + ly * ly) * 1.7)
				still[c6] = still[c6] + f * 5.6 * 0.82
				if f > best[c6]:
					best[c6] = f
					owner[c6] = k3
		self.lmap = gfx.make_grid(lw * lh)
		for g in range(self.ngoo):
			self.gx.append(0.0)
			self.gy.append(0.0)
			self.gz.append(0.0)
			self.gvx.append(0.0)
			self.gvy.append(0.0)
			self.gvz.append(0.0)
			self.glife.append(0.0)
			self.gtype.append(0)
		self.vigx = gfx.make_grid(w)
		self.vigy = gfx.make_grid(h)
		vigx = self.vigx
		vigy = self.vigy
		for vx in range(w):
			e = 2.0 * vx / (w - 1.0) - 1.0
			vigx[vx] = int(e * e * e * e * 2.4)
		for vy in range(h):
			e2 = 2.0 * vy / (h - 1.0) - 1.0
			vigy[vy] = int(e2 * e2 * e2 * e2 * 1.6)

	# one entry: a colour at light level f (0 black gloom .. 7 full torchlight):
	# the dark sinks into a cold blue, the lit end warms toward the fire
	def lit(self, r, g, b, f, side, fogc):
		k = (0.07 + 0.93 * f / 7.0) * (0.72 if side == 1 else 1.0)
		warm = (f - 4) / 3.0 if f >= 5 else 0.0
		fr2 = fogc[0]
		fg = fogc[1]
		fb = fogc[2]
		cr = r * (1.0 + 0.32 * warm)
		cg = g * (1.0 + 0.06 * warm)
		cb = b * (1.0 - 0.28 * warm)
		self.pal.add(cr * k + fr2 * (1.0 - k), cg * k + fg * (1.0 - k), cb * k + fb * (1.0 - k))

	def hash2(self, a, b):
		hs = (a * 73856093 + b * 19349663 + (self.seed & 255) * 83492791) & 1048575
		return ((hs * 1274126177) >> 12) & 1023

	def grow_textures(self):
		hash2 = self.hash2
		tex = self.tex
		# 0 coursed stone, 1 stone mossy toward its foot, 2 fluted gold (16 wide, the vault's 32 tall), 3 flagstones, 4 oak planks
		for t in range(5):
			tall = 32 if t < 3 else 16
			for v in range(tall):
				for u in range(16):
					c = 0
					if t <= 1:
						row = v // 4
						bu = (u + (row % 2) * 4) % 8
						bv = v % 4
						brick = hash2((u + (row % 2) * 4) // 8 + row * 7, t)
						c = 1 + brick % 4
						if bv == 1:
							c += 1
						if bv == 3 and c > 1:
							c -= 1
						if bu == 0 or bv == 0:
							c = 0
						if t == 1 and c != 0:
							m = hash2(u * 3 + v, v * 5 + 11) % 100
							if m < (v - 13) * 5:
								c = 4 + m % 3
						if t == 1 and c == 0 and v > 12 and hash2(u, v) % 3 == 0:
							c = 7
					elif t == 2:
						fl = u % 4
						c = 7 if fl == 0 else (5 if fl == 1 else (4 if fl == 2 else 2))
						if v % 8 == 0:
							c = 6
						if v % 8 == 1:
							c = 1
					elif t == 3:
						su = (u + (v // 8) * 4) % 8
						sv = v % 8
						stone = hash2((u + (v // 8) * 4) // 8, v // 8 + 40)
						c = 1 + stone % 5
						if sv == 1 or su == 1:
							c = 7
						if su == 0 or sv == 0:
							c = 0
						if hash2(u + 50, v) % 11 == 0 and c != 0:
							c = 6
					else:
						plank = v // 4
						pv = v % 4
						c = 2 + (hash2(plank, 90) % 3)
						if (u + plank * 5) % 16 == 0:
							c = 0
						if pv == 0:
							c = 5
						if pv == 3:
							c = 7
						if hash2(u, v + 90) % 13 == 0:
							c = 6
					tex.append(c)
		# plain copies of the floor and the ceiling for their far rows: the joints over one shade
		for i in range(1536, 2048):
			texel = tex[i]
			joint = texel == 0 or (i >= 1792 and texel == 7)
			tex.append(texel if joint else 3)

	def path_at(self, s):
		path_len = self.path_len
		seglen = self.seglen
		wpx = self.wpx
		wpy = self.wpy
		nwp = self.nwp
		s -= math.floor(s / path_len) * path_len
		for i in range(nwp):
			L = seglen[i]
			if s <= L:
				f = s / L if L > 0.0001 else 0.0
				j = (i + 1) % nwp
				return [wpx[i] + (wpx[j] - wpx[i]) * f, wpy[i] + (wpy[j] - wpy[i]) * f]
			s -= L
		return [wpx[0], wpy[0]]

	# DDA toward (dx, dy) in parameter space: true if no wall before frac of the way
	def los_clear(self, dx, dy, frac):
		posx = self.posx
		posy = self.posy
		wmap = self.wmap
		mw = self.mw
		mx = int(posx)
		my = int(posy)
		adx = -dx if dx < 0.0 else dx
		ady = -dy if dy < 0.0 else dy
		ddx = 100000000.0 if adx < 0.00000001 else 1.0 / adx
		ddy = 100000000.0 if ady < 0.00000001 else 1.0 / ady
		stepx = -1 if dx < 0.0 else 1
		stepy = -1 if dy < 0.0 else 1
		sdx = (posx - mx) * ddx if dx < 0.0 else (mx + 1.0 - posx) * ddx
		sdy = (posy - my) * ddy if dy < 0.0 else (my + 1.0 - posy) * ddy
		for g in range(64):
			trav = sdx if sdx < sdy else sdy
			if trav >= frac:
				return True
			if sdx < sdy:
				sdx += ddx
				mx += stepx
			else:
				sdy += ddy
				my += stepy
			cv = wmap[my * mw + mx]
			if cv > 0:
				return False
		return False

	def length_bars(self):
		return 13

	def hit_times(self):
		return [MUSIC_BAR * 3.0, MUSIC_BAR * 5.0, MUSIC_BAR * 7.0, MUSIC_BAR * 9.0, MUSIC_BAR * 10.0, MUSIC_BAR * 12.0]

	def hit_levels(self):
		return [1, 2, 1, 2, 1, 2]

	# the route's distance from which the walk faces the arena floor where the boss rises
	def arena_stand(self):
		return 19.0 + 9.8

	# the route's distance at which the escape reaches the breach, facing it
	def breach_stop(self):
		return 19.0 + 10.0 + 9.0 + 8.3

	def update(self, t, dt):
		sin = math.sin
		cos = math.cos
		sqrt = math.sqrt
		floor = math.floor
		self.tt = t
		self.fr += 1
		fr = self.fr
		cue = self.cue
		ne = self.ne
		es = self.es
		est = self.est
		ex = self.ex
		ey = self.ey
		h = self.h
		path_len = self.path_len
		aftermath = t > MUSIC_BAR * 9.0 and t < MUSIC_BAR * 10.0
		escape = t >= MUSIC_BAR * 10.0 and t < MUSIC_BAR * 12.0
		out = t >= MUSIC_BAR * 12.0
		fight = t > MUSIC_BAR * 7.0 and t < MUSIC_BAR * 9.0
		bullet = self.slow_mo(t)
		lap = self.travel - floor(self.travel / path_len) * path_len
		pace = 0.18 if self.stop_t > 0.0 else (0.4 if aftermath else (0.16 if fight else 1.0))
		if escape:
			# the run for the breach: always just fast enough to reach it on bar twelve
			want = (self.breach_stop() - lap) / (MUSIC_BAR * 12.0 - t + 0.05) / 2.4
			want = 0.0 if want < 0.0 else (3.0 if want > 3.0 else want)
			pace = want * 0.5 if self.stop_t > 0.0 else want
		if t >= MUSIC_BAR * 6.0 and t < MUSIC_BAR * 7.0:
			# the walk into the arena: always just fast enough to face the boss's floor on bar seven
			gait = (self.arena_stand() - lap) / (MUSIC_BAR * 7.0 - t + 0.05) / 2.4
			gait = 0.3 if gait < 0.3 else (2.0 if gait > 2.0 else gait)
			pace = gait * 0.5 if self.stop_t > 0.0 else gait
		if out:
			pace = 0.0
		pace *= bullet
		self.travel += dt * 2.4 * pace
		travel = self.travel
		lap = travel - floor(travel / path_len) * path_len
		look = 1.7
		if escape or out:
			to_corner = 19.0 + 10.0 + 9.0 + 9.0 - lap
			look = to_corner if to_corner < look and to_corner > 0.0 else look
		p = self.path_at(travel)
		q = self.path_at(travel + look)   # look-ahead steers the turns
		posx = p[0]
		posy = p[1]
		if out:
			# into the light: the last bar walks through the breach
			walk = (t - MUSIC_BAR * 12.0) / MUSIC_BAR
			walk = 1.0 if walk > 1.0 else walk
			posx += walk * (2.0 - walk) * 0.92
		self.posx = posx
		self.posy = posy
		bx = q[0] - posx
		by = q[1] - posy
		bl = sqrt(bx * bx + by * by)
		if out:
			bx = 1.0
			by = 0.0
			bl = 1.0
		if bl > 0.0001:
			bx /= bl
			by /= bl
		else:
			bx = self.dirx
			by = self.diry
		# the hunt: swing toward the nearest live mark in clear sight ahead
		want = 0.0
		near = 5.5
		for m in range(ne):
			if es[m] != 0:
				continue
			mx = ex[m] - posx
			my = ey[m] - posy
			md = sqrt(mx * mx + my * my)
			if md > near or md < 0.6:
				continue
			ang = math.atan2(bx * my - by * mx, bx * mx + by * my)
			if ang > 0.75 or ang < -0.75:
				continue
			if not self.los_clear(mx, my, 0.95):
				continue
			near = md
			want = ang
		if out:
			want = 0.0
		ease = dt * 7.0
		ease = 1.0 if ease > 1.0 else ease
		self.aim += (want - self.aim) * ease
		# the quake: each kill shakes the view, the big hit hardest, and the keep rumbles as it falls
		kick_now = cue.kick_env(t) if cue is not None else 0.0
		quake = (self.hot_t * 0.16 if self.hot_t > 0.0 else 0.0) + (cue.hit(t) * 0.05 if cue is not None else 0.0)
		if escape:
			quake += 0.012 + kick_now * 0.03
		view = self.aim + quake * sin(fr * 2.7)
		ca = cos(view)
		sa = sin(view)
		dirx = bx * ca - by * sa
		diry = bx * sa + by * ca
		self.dirx = dirx
		self.diry = diry
		self.planex = 0.0 - diry * 0.66
		self.planey = dirx * 0.66
		planex = self.planex
		planey = self.planey
		# timers and the enemy state machine
		self.cool -= dt
		self.muzzle_t -= dt
		self.tracer_t -= dt
		self.hot_t -= dt
		self.kick_t -= dt
		self.stop_t -= dt
		self.call_t -= dt
		self.boss_flash -= dt
		for i in range(ne):
			st = es[i]
			if st == 1:
				est[i] = est[i] + dt
				if est[i] > 0.14:
					es[i] = 2
					est[i] = 0.0
			elif st == 2:
				est[i] = est[i] + dt
				if est[i] > 0.55:
					es[i] = 3
					est[i] = 0.0
			elif st == 3:
				est[i] = est[i] + dt
				if est[i] > 16.0:
					es[i] = 0
					est[i] = 0.0
		# auto-fire on the beat: nearest alive enemy in the crosshair cone with clear LOS
		e = cue.energy(t) if cue is not None else 0.6
		eighths = e > 0.92 or (t > MUSIC_BAR * 5.0 and t < MUSIC_BAR * 6.0) or escape
		# the big hit: the hall floods with light and the frenzy is called
		if t >= MUSIC_BAR * 5.0 and t - dt < MUSIC_BAR * 5.0:
			self.callout = "FRENZY"
			self.call_t = 1.2
		step = floor(t / (MUSIC_BEAT * 0.5 if eighths else MUSIC_BEAT))
		on_step = step != self.last_step
		self.last_step = step
		if t >= MUSIC_BAR * 10.0 and t - dt < MUSIC_BAR * 10.0:
			self.callout = "RUN"
			self.call_t = 1.3
			self.hot_t = 0.5
		if t >= MUSIC_BAR * 12.0 and t - dt < MUSIC_BAR * 12.0:
			self.blow_breach()
		if escape:
			self.falling_stone(t)
		if self.cool <= 0.0 and on_step and not aftermath and not out and t > MUSIC_BAR:
			invdet = 1.0 / (planex * diry - dirx * planey)
			best = -1
			besty = 99.0
			bestx = 0.0
			for i2 in range(ne):
				st2 = es[i2]
				if st2 != 0:
					continue
				rx = ex[i2] - posx
				ry = ey[i2] - posy
				txx = invdet * (diry * rx - dirx * ry)
				tyy = invdet * (planex * ry - planey * rx)
				if tyy < 1.0 or tyy > 4.6:
					continue
				off = -txx if txx < 0.0 else txx
				if off > tyy * 0.12:
					continue
				if not self.los_clear(rx, ry, 0.95):
					continue
				if tyy < besty:
					best = i2
					besty = tyy
					bestx = txx
			if best >= 0:
				self.fire(best, bestx, besty, t)
		# the boss rises from the arena's open floor on bar seven
		boss = ne - 1
		if t >= MUSIC_BAR * 7.0 and t - dt < MUSIC_BAR * 7.0:
			es[boss] = 0
			est[boss] = 0.0
			self.callout = "BOSS GLOB"
			self.call_t = 1.6
			# the garrison scatters before it
			for m2 in range(boss):
				ddx2 = ex[m2] - posx
				ddy2 = ey[m2] - posy
				if ddx2 * ddx2 + ddy2 * ddy2 < 64.0:
					es[m2] = 3
					est[m2] = 0.0
		# bar nine's downbeat: the boss bursts
		if t >= MUSIC_BAR * 9.0 and t - dt < MUSIC_BAR * 9.0 and es[boss] == 0:
			invdet2 = 1.0 / (planex * diry - dirx * planey)
			rbx = ex[boss] - posx
			rby = ey[boss] - posy
			bsx = invdet2 * (diry * rbx - dirx * rby)
			bsy = invdet2 * (planex * rby - planey * rbx)
			self.fire(boss, bsx, bsy if bsy > 0.3 else 0.3, t)
		self.move_goo(dt * bullet)
		# the flicker is redrawn at 15 Hz, and every frame while the muzzle burns or the kick lands
		beat_light = cue.kick_env(t) + cue.hit(t) if cue is not None else 0.0
		if (fr & 1) == 0 or self.muzzle_t > -0.05 or beat_light > 0.2 or escape or out:
			self.light_up()
		# the view: a footfall on each beat (each eighth on the run), the recoil's kick, a shudder
		# while the muzzle burns, and the falling keep's tremor
		stride = cue.beat_phase(t) if cue is not None else travel * 0.83
		if escape:
			stride = stride * 2.0 - floor(stride * 2.0)
		bob = sin(stride * 3.1415927) * h * (0.022 if escape else 0.014) * (0.4 if aftermath or out else 1.0)
		kick = h * 0.05 * self.kick_t / 0.2 if self.kick_t > 0.0 else 0.0
		shud = ((fr * 7) % 3 - 1) * h * 0.012 if self.muzzle_t > 0.0 else 0.0
		tremor = sin(fr * 5.3) * h * (0.006 + kick_now * 0.02) if escape else 0.0
		self.pitch = bob + kick + shud + tremor

	# the escape: on each snare stones shake loose from the vault ahead and fall
	def falling_stone(self, t):
		cue = self.cue
		sn = cue.snare_env(t) if cue is not None else 0.0
		onset = sn > self.last_snare + 0.25
		self.last_snare = sn
		if not onset:
			return
		rng = self.rng
		wmap = self.wmap
		mw = self.mw
		ngoo = self.ngoo
		posx = self.posx
		posy = self.posy
		dirx = self.dirx
		diry = self.diry
		for k in range(14):
			g = self.goo_next
			self.goo_next = (self.goo_next + 1) % ngoo
			ahead = 1.2 + rng.nextf() * 3.2
			across = (rng.nextf() - 0.5) * 2.6
			sx = posx + dirx * ahead - diry * across
			sy = posy + diry * ahead + dirx * across
			if wmap[int(sy) * mw + int(sx)] != 0:
				continue
			self.gx[g] = sx
			self.gy[g] = sy
			self.gz[g] = self.vault - 0.05
			self.gvx[g] = (rng.nextf() - 0.5) * 0.6
			self.gvy[g] = (rng.nextf() - 0.5) * 0.6
			self.gvz[g] = 0.0 - rng.nextf() * 1.5
			self.glife[g] = 1.4 + rng.nextf() * 0.8
			self.gtype[g] = 2

	# bar twelve: the east wall bursts, stone flies at the camera and daylight floods in
	def blow_breach(self):
		self.callout = "KEEP CLEARED"
		self.call_t = 2.6
		self.hot_t = 0.8
		rng = self.rng
		ngoo = self.ngoo
		for k in range(70):
			g = self.goo_next
			self.goo_next = (self.goo_next + 1) % ngoo
			self.gx[g] = self.breach_x - 0.05
			self.gy[g] = self.breach_y + 0.1 + rng.nextf() * 0.8
			self.gz[g] = 0.1 + rng.nextf() * 0.8
			self.gvx[g] = 0.0 - (1.5 + rng.nextf() * 4.5)
			self.gvy[g] = (rng.nextf() - 0.5) * 2.4
			self.gvz[g] = 0.5 + rng.nextf() * 2.5
			self.glife[g] = 1.8 + rng.nextf() * 1.2
			self.gtype[g] = 2

	# 0 a wall, rising to 1 as the cracks of the breach open through the escape, 2 blown open
	def breach_state(self):
		if self.tt >= MUSIC_BAR * 12.0:
			return 2.0
		k = (self.tt - MUSIC_BAR * 10.0) / (MUSIC_BAR * 2.0)
		return 0.0 if k < 0.0 else k

	# bullet time: two beats after each big hit play at a fifth of the speed
	def slow_mo(self, t):
		since = t - MUSIC_BAR * 5.0
		if t >= MUSIC_BAR * 9.0:
			since = t - MUSIC_BAR * 9.0
		if t >= MUSIC_BAR * 12.0:
			since = t - MUSIC_BAR * 12.0
		if since < 0.0 or since > MUSIC_BEAT * 2.5:
			return 1.0
		back = (since - MUSIC_BEAT * 2.0) / (MUSIC_BEAT * 0.5)
		back = 0.0 if back < 0.0 else (1.0 if back > 1.0 else back)
		return 0.2 + 0.8 * back

	def fire(self, i, bx, by, t):
		cos = math.cos
		sin = math.sin
		rng = self.rng
		ngoo = self.ngoo
		w = self.w
		h = self.h
		gx = self.gx
		gy = self.gy
		gz = self.gz
		gvx = self.gvx
		gvy = self.gvy
		gvz = self.gvz
		glife = self.glife
		gtype = self.gtype
		boss = i == self.ne - 1
		boss_down = boss and t >= MUSIC_BAR * 9.0 - 0.01
		if boss and not boss_down:
			# a hit the boss takes: the muzzle, the tracer and a flash, but it stands
			self.boss_flash = 0.1
			self.cool = 0.2
			self.muzzle_t = 0.12
			self.tracer_t = 0.14
			self.kick_t = 0.2
			self.tr_x = int(w * 0.5 * (1.0 + bx / by))
			self.tr_y = h // 2 + int(h * 0.10 / by)
			self.callout = ""
			for k0 in range(8):
				g0 = self.goo_next
				self.goo_next = (self.goo_next + 1) % ngoo
				a0 = rng.nextf() * 6.2831853
				gx[g0] = self.ex[i]
				gy[g0] = self.ey[i]
				gz[g0] = 0.5 + rng.nextf() * 0.4
				gvx[g0] = cos(a0) * 1.2 - self.dirx * 0.8
				gvy[g0] = sin(a0) * 1.2 - self.diry * 0.8
				gvz[g0] = 1.0 + rng.nextf() * 2.0
				glife[g0] = 1.6
				gtype[g0] = 1
			return
		self.es[i] = 1
		self.est[i] = 0.0
		self.kills += 1
		self.cool = 0.2
		self.muzzle_t = 0.12
		self.tracer_t = 0.14
		self.hot_t = 0.35
		self.kick_t = 0.2
		self.stop_t = 0.16
		self.tr_x = int(w * 0.5 * (1.0 + bx / by))
		self.tr_y = h // 2 + int(h * 0.10 / by)
		self.combo = self.combo + 1 if t - self.last_kill < 1.8 else 1
		self.last_kill = t
		words = ["SPLAT", "GLOBBED", "SQUISH", "POP"]
		combo = self.combo
		self.callout = "BOSS DOWN" if boss_down else ("MULTI KILL" if combo >= 3 else ("DOUBLE KILL" if combo == 2 else words[self.kills % 4]))
		self.call_t = 2.2 if boss_down else 0.9
		if boss_down:
			self.hot_t = 0.8
		# the burst: goo flung out of the body, mostly up and back from the shot
		exi = self.ex[i]
		eyi = self.ey[i]
		dirx = self.dirx
		diry = self.diry
		eti = self.et[i]
		for k in range(120 if boss_down else 34):
			g = self.goo_next
			self.goo_next = (self.goo_next + 1) % ngoo
			a = rng.nextf() * 6.2831853
			sp = 0.8 + rng.nextf() * 2.2
			gx[g] = exi
			gy[g] = eyi
			gz[g] = 0.35 + rng.nextf() * 0.3
			gvx[g] = cos(a) * sp + dirx * 1.2
			gvy[g] = sin(a) * sp + diry * 1.2
			gvz[g] = 1.2 + rng.nextf() * 2.6
			glife[g] = 2.2 + rng.nextf() * 1.5
			gtype[g] = eti

	# goo falls, bounces off the flagstones and the walls, then lies there fading
	def move_goo(self, dt):
		wmap = self.wmap
		mw = self.mw
		gx = self.gx
		gy = self.gy
		gz = self.gz
		gvx = self.gvx
		gvy = self.gvy
		gvz = self.gvz
		glife = self.glife
		for g in range(self.ngoo):
			life = glife[g]
			if life <= 0.0:
				continue
			glife[g] = life - dt
			vz = gvz[g] - 7.5 * dt
			nz = gz[g] + vz * dt
			if nz < 0.0:
				nz = 0.0
				vz = 0.0 - vz * 0.3 if vz < -0.6 else 0.0
				gvx[g] = gvx[g] * 0.55
				gvy[g] = gvy[g] * 0.55
			gz[g] = nz
			gvz[g] = vz
			nx = gx[g] + gvx[g] * dt
			ny = gy[g] + gvy[g] * dt
			if wmap[int(gy[g]) * mw + int(nx)] != 0:
				gvx[g] = 0.0 - gvx[g] * 0.4
				nx = gx[g]
			if wmap[int(ny) * mw + int(gx[g])] != 0:
				gvy[g] = 0.0 - gvy[g] * 0.4
				ny = gy[g]
			gx[g] = nx
			gy[g] = ny

	# the light map: the torches' steady sum, each cell flickering with the torch
	# that rules it, plus the muzzle near the camera while it burns
	def light_up(self):
		sin = math.sin
		lw = self.lw
		lh = self.lh
		n = lw * lh
		tox = self.tox
		nt = len(tox)
		tt = self.tt
		cue = self.cue
		flick = []
		kick = cue.kick_env(tt) if cue is not None else 0.0
		# on the run the torches gutter low and the kick strobes them back up
		run = tt >= MUSIC_BAR * 10.0 and tt < MUSIC_BAR * 12.0
		gutter = 0.4 + kick * 1.1 if run else 1.0
		beat_flare = 0.45 * kick + 0.7 * cue.hit(tt) if cue is not None else 0.0
		for k in range(nt):
			flick.append((1.0 + 0.12 * sin(tt * 13.0 + k * 2.3) + 0.10 * sin(tt * 29.0 + k * 5.1)) * gutter + beat_flare)
		st = self.still
		ow = self.owner
		lm = self.lmap
		flood = int(cue.hit(tt) * 4.0) if cue is not None else 0
		for c in range(n):
			lv = int(st[c] * flick[ow[c]]) + flood
			lm[c] = 5 if lv > 5 else lv
		# every burst lights the hall around it while the goo flies
		es = self.es
		est = self.est
		for b in range(self.ne):
			sb = es[b]
			if sb != 1 and sb != 2:
				continue
			age = est[b] if sb == 1 else 0.14 + est[b]
			power = 5.0 * (1.0 - age / 0.7)
			if power <= 0.0:
				continue
			bx0 = int(self.ex[b] * 2.0)
			by0 = int(self.ey[b] * 2.0)
			for dy2 in range(-9, 10):
				cy2 = by0 + dy2
				if cy2 < 0 or cy2 >= lh:
					continue
				for dx2 in range(-9, 10):
					cx2 = bx0 + dx2
					if cx2 < 0 or cx2 >= lw:
						continue
					dd = (dx2 * dx2 + dy2 * dy2) * 0.25
					c3 = cy2 * lw + cx2
					lv3 = lm[c3] + int(power / (1.0 + dd * 0.5))
					lm[c3] = 5 if lv3 > 5 else lv3
		# daylight through the breach: a glow through its cracks, then a flood once it is open
		bs = self.breach_state()
		if bs > 0.0:
			power = 6.0 if bs >= 2.0 else bs * (2.0 + kick * 2.0)
			bx1 = self.breach_x * 2
			by1 = self.breach_y * 2 + 1
			for dy3 in range(-12, 13):
				cy3 = by1 + dy3
				if cy3 < 0 or cy3 >= lh:
					continue
				for dx3 in range(-24, 1):
					cx3 = bx1 + dx3
					if cx3 < 0:
						continue
					dd3 = (dx3 * dx3 + dy3 * dy3 * 2.0) * 0.25
					c4 = cy3 * lw + cx3
					lv4 = lm[c4] + int(power / (1.0 + dd3 * 0.12))
					lm[c4] = 5 if lv4 > 5 else lv4
		if self.muzzle_t > 0.0:
			cx0 = int(self.posx * 2.0)
			cy0 = int(self.posy * 2.0)
			for dy in range(-10, 11):
				cy = cy0 + dy
				if cy < 0 or cy >= lh:
					continue
				for dx in range(-10, 11):
					cx = cx0 + dx
					if cx < 0 or cx >= lw:
						continue
					d2 = (dx * dx + dy * dy) * 0.25
					c2 = cy * lw + cx
					lv2 = int(st[c2] * flick[ow[c2]] + 4.5 / (1.0 + d2 * 0.35))
					lm[c2] = 5 if lv2 > 5 else lv2

	def light_at(self, x, y):
		lw = self.lw
		lh = self.lh
		cx = int(x * 2.0)
		cy = int(y * 2.0)
		cx = 0 if cx < 0 else (lw - 1 if cx >= lw else cx)
		cy = 0 if cy < 0 else (lh - 1 if cy >= lh else cy)
		return self.lmap[cy * lw + cx]

	# one ray per column: the wall's depth, the rows its slice covers, and what it hit;
	# each column is also filed under the rows where its ceiling ends and its floor begins
	def cast_walls(self):
		ww = self.w
		hh = self.h
		wm = self.wmap
		zc = self.zcol
		vx = self.vigx
		top = self.wall_top
		foot = self.wall_foot
		hit = self.wall_hit
		clo = self.ceil_lo
		chi = self.ceil_hi
		flo = self.floor_lo
		fhi = self.floor_hi
		lm = self.lmap
		lcols = self.lw
		lrows = self.lh
		mcols = self.mw
		px = self.posx
		py = self.posy
		dirx = self.dirx
		diry = self.diry
		planex = self.planex
		planey = self.planey
		floor = math.floor
		hc = float(hh // 2 + int(self.pitch))
		rise = self.vault - 0.5
		for v in range(hh + 1):
			clo[v] = ww
			chi[v] = -1
			flo[v] = ww
			fhi[v] = -1
		for x in range(ww):
			camx = 2.0 * x / (ww - 1.0) - 1.0
			rdx = dirx + planex * camx
			rdy = diry + planey * camx
			mx = int(px)
			my = int(py)
			adx = -rdx if rdx < 0.0 else rdx
			ady = -rdy if rdy < 0.0 else rdy
			ddx = 100000000.0 if adx < 0.00000001 else 1.0 / adx
			ddy = 100000000.0 if ady < 0.00000001 else 1.0 / ady
			stepx = -1 if rdx < 0.0 else 1
			stepy = -1 if rdy < 0.0 else 1
			sdx = (px - mx) * ddx if rdx < 0.0 else (mx + 1.0 - px) * ddx
			sdy = (py - my) * ddy if rdy < 0.0 else (my + 1.0 - py) * ddy
			side = 0
			hitv = 0
			guard = 0
			while hitv == 0 and guard < 64:
				if sdx < sdy:
					sdx += ddx
					mx += stepx
					side = 0
				else:
					sdy += ddy
					my += stepy
					side = 1
				hitv = wm[my * mcols + mx]
				guard += 1
			hitv = 1 if hitv == 0 else hitv
			pdist = sdx - ddx if side == 0 else sdy - ddy   # perpendicular: no fisheye
			pdist = 0.05 if pdist < 0.05 else pdist
			zc[x] = pdist
			wallx = py + pdist * rdy if side == 0 else px + pdist * rdx
			wallx -= floor(wallx)
			tu = int(wallx * 16.0) & 15
			lineh = hh / pdist
			y0 = int(hc - lineh * rise)
			y1 = int(hc + lineh * 0.5)
			y0 = 0 if y0 < 0 else y0
			y1 = hh if y1 > hh else y1
			top[x] = y0
			foot[x] = y1
			if clo[y0] > x:
				clo[y0] = x
			chi[y0] = x
			if flo[y1] > x:
				flo[y1] = x
			fhi[y1] = x
			# light: the gloom by distance, the torches at the face, the vignette
			amb = int(8.0 / (pdist + 1.8))
			amb = 3 if amb > 3 else amb
			lcx = int((px + rdx * (pdist - 0.05)) * 2.0)
			lcy = int((py + rdy * (pdist - 0.05)) * 2.0)
			lcx = 0 if lcx < 0 else (lcols - 1 if lcx >= lcols else lcx)
			lcy = 0 if lcy < 0 else (lrows - 1 if lcy >= lrows else lcy)
			lv = amb + lm[lcy * lcols + lcx] - vx[x]
			lv = 0 if lv < 0 else (7 if lv > 7 else lv)
			hit[x] = ((hitv * 2 + side) * 16 + tu) * 8 + lv

	# floor and ceiling, cast per pixel only where no wall slice covers them: each
	# row off the horizon is a fixed distance, stepped along the camera plane across
	# the columns whose ceiling (or floor) reaches that row
	def cast_planes(self, grid):
		hh = self.h
		h0 = hh // 2 + int(self.pitch)
		clo = self.ceil_lo
		chi = self.ceil_hi
		flo = self.floor_lo
		fhi = self.floor_hi
		rise = self.vault - 0.5
		cast_plane_row = self.cast_plane_row
		ceil_base = self.ceil_base
		floor_base = self.floor_base
		lo = clo[hh]
		hi = chi[hh]
		for y in range(hh - 1, -1, -1):
			if y < h0 and lo <= hi:
				up = h0 - y - 0.5
				ceil_d = rise * hh / up
				# a texel is up / (16 * ceil_d) pixels tall: under two, its grain would only speckle
				cast_plane_row(grid, y, ceil_d, lo, hi, ceil_base, 2304 if up < ceil_d * 32.0 else 1792)
			lo = clo[y] if clo[y] < lo else lo
			hi = chi[y] if chi[y] > hi else hi
		lo = self.w
		hi = -1
		for y2 in range(hh):
			lo = flo[y2] if flo[y2] < lo else lo
			hi = fhi[y2] if fhi[y2] > hi else hi
			if y2 >= h0 and lo <= hi:
				down = y2 - h0 + 0.5
				floor_d = 0.5 * hh / down
				cast_plane_row(grid, y2, floor_d, lo, hi, floor_base, 2048 if down < floor_d * 32.0 else 1536)

	def cast_plane_row(self, grid, y, rowd, x0, x1, plane_base, plane_tex):
		ww = self.w
		tx = self.tex
		lm = self.lmap
		vx = self.vigx
		lcols = self.lw
		lrows = self.lh
		planex = self.planex
		planey = self.planey
		step_x = rowd * 2.0 * planex / ww
		step_y = rowd * 2.0 * planey / ww
		fx = self.posx + rowd * (self.dirx - planex) + step_x * x0
		fy = self.posy + rowd * (self.diry - planey) + step_y * x0
		amb = int(8.0 / (rowd + 1.8))
		amb = (3 if amb > 3 else amb) - self.vigy[y]
		gi = y * ww
		cell = -1
		lit = 0
		for x in range(x0, x1 + 1):
			iu = int(fx * 16.0)
			iv = int(fy * 16.0)
			# the light changes only from one light-map cell to the next
			lc = (iv >> 3) * 4096 + (iu >> 3)
			if lc != cell:
				cell = lc
				lcx = iu >> 3
				lcy = iv >> 3
				lcx = 0 if lcx < 0 else (lcols - 1 if lcx >= lcols else lcx)
				lcy = 0 if lcy < 0 else (lrows - 1 if lcy >= lrows else lcy)
				lit = amb + lm[lcy * lcols + lcx]
			lv = lit - vx[x]
			lv = 0 if lv < 0 else (7 if lv > 7 else lv)
			grid[gi + x] = plane_base + lv * 8 + tx[plane_tex + (iv & 15) * 16 + (iu & 15)]
			fx += step_x
			fy += step_y

	# one billboard: an ellipsoid body lit as a sphere per pixel, eyes that glow
	# in the gloom, a grin; walls occlude per column; hit-flash paints it white
	def draw_blob(self, grid, i):
		sqrt = math.sqrt
		w = self.w
		h = self.h
		cue = self.cue
		tt = self.tt
		zcol = self.zcol
		sprite_base = self.sprite_base
		tyy = self.sty[i]
		txx = self.stx[i]
		scx = int(w * 0.5 * (1.0 + txx / tyy))
		typ = self.et[i]
		boss = i == self.ne - 1
		size = 1.7 + (cue.kick_env(tt) * 0.22 if cue is not None else 0.0) if boss else (1.1 if typ == 1 else 0.78)
		sh = int(h * size / tyy)
		sh = 3 if sh < 3 else sh
		sh = h if sh > h else sh
		# its half-width never passes its kind's reach in world units, whatever the pixels' shape
		hwm = sh * (0.57 if boss else (0.6 if typ == 1 else 0.55))
		room = (1.2 if boss else (0.75 if typ == 1 else 0.5)) * w / (1.32 * tyy)
		hwm = room if hwm > room else hwm
		feet = h // 2 + int(self.pitch) + int(h * 0.5 / tyy)
		top = feet - sh
		wob = math.sin(tt * 5.0 + i * 2.1) * 0.10
		flash = self.es[i] == 1 or (boss and self.boss_flash > 0.0)
		body = sprite_base + typ * 12
		fogk = 0.18 + self.light_at(self.ex[i], self.ey[i]) * 0.15 + 0.35 / (tyy + 0.6)
		fogk = 0.2 if fogk < 0.2 else (1.0 if fogk > 1.0 else fogk)
		glow = fogk < 0.55 or boss
		for dy in range(sh):
			rr = top + dy
			if rr < 0 or rr >= h:
				continue
			v = (dy + 0.5) / sh * 2.0 - 1.0
			vv = 1.0 - v * v
			if vv <= 0.0:
				continue
			hwf = sqrt(vv) * hwm
			hw = int(hwf)
			cxr = scx + int(wob * v * hwm)
			rowb = rr * w
			for dx in range(0 - hw, hw + 1):
				col = cxr + dx
				if col < 0 or col >= w:
					continue
				if tyy >= zcol[col]:
					continue
				pi = sprite_base + 24
				if not flash:
					nx = dx / (hwm + 0.5)
					q = 1.0 - nx * nx - v * v
					nz = sqrt(q) if q > 0.0 else 0.0
					li = -0.45 * nx - 0.55 * v + 0.70 * nz
					li = 0.0 if li < 0.0 else li
					# a rim of torchlight on the silhouette's edge
					rim = 0.35 if q < 0.12 and li < 0.3 else 0.0
					lv = int((0.10 + 0.9 * li + rim) * fogk * 11.99)
					pi = body + (11 if lv > 11 else lv)
					ax = -nx if nx < 0.0 else nx
					if v > -0.5 and v < -0.12 and ax > 0.14 and ax < 0.52:
						ey2 = (v + 0.31) * 5.0
						ex2 = (ax - 0.33) * 5.0
						if ex2 * ex2 + ey2 * ey2 < 1.0:
							core = ex2 * ex2 + ey2 * ey2 < 0.3
							pi = (sprite_base + 48 if glow else sprite_base + 26) if core else (sprite_base + 26 if glow else sprite_base + 25)
					if v > 0.18 and v < 0.34 and ax < 0.42:
						pi = sprite_base + 25 if (dx & 1) == 0 and v < 0.24 else sprite_base + 45
				grid[rowb + col] = pi

	# a torch's flame: a flickering teardrop, occluded by nearer walls
	def draw_flame(self, grid, k):
		w = self.w
		h = self.h
		zcol = self.zcol
		planex = self.planex
		planey = self.planey
		dirx = self.dirx
		diry = self.diry
		invdet = 1.0 / (planex * diry - dirx * planey)
		rx = self.tox[k] - self.posx
		ry = self.toy[k] - self.posy
		tyy = invdet * (planex * ry - planey * rx)
		if tyy < 0.3:
			return
		txx = invdet * (diry * rx - dirx * ry)
		scx = int(w * 0.5 * (1.0 + txx / tyy))
		hc = float(h // 2 + int(self.pitch))
		fh = int(h * 0.24 / tyy)
		fh = 3 if fh < 3 else fh
		fy0 = int(hc - h * 0.14 / tyy) - fh
		flick = math.sin(self.tt * 17.0 + k * 3.0)
		sprite_base = self.sprite_base
		for dy in range(fh):
			v = (dy + 0.5) / fh
			half = (v / 0.65 if v < 0.65 else 1.0 - (v - 0.65) / 0.35 * 0.4) * fh * 0.28
			sway = int(flick * (1.0 - v) * fh * 0.12)
			hw = int(half)
			row = fy0 + dy
			if row < 0 or row >= h:
				continue
			for dx in range(0 - hw, hw + 1):
				col = scx + dx + sway
				if col < 0 or col >= w or tyy >= zcol[col]:
					continue
				ax = float(0 - dx if dx < 0 else dx)
				e = ax / hw if hw > 0 else 0.0
				c = 31 if e < 0.4 and v > 0.45 else (32 if e < 0.75 else 33)
				grid[row * w + col] = sprite_base + c

	def render(self, grid):
		self.cast_walls()
		self.cast_planes(grid)
		ww = self.w
		hh = self.h
		tx = self.tex
		zc = self.zcol
		top = self.wall_top
		foot = self.wall_foot
		hit = self.wall_hit
		wb = self.wall_base
		bb = self.banner_base
		tshift = int(self.tt * 14.0)
		hc = float(hh // 2 + int(self.pitch))
		rise = self.vault - 0.5
		for x in range(ww):
			y0 = top[x]
			y1 = foot[x]
			packed = hit[x]
			lv = packed & 7
			tu = (packed >> 3) & 15
			side = (packed >> 7) & 1
			hitv = packed >> 8
			lineh = hh / zc[x]
			tstep = 16.0 / lineh
			tpos = (y0 + 0.5 - (hc - lineh * rise)) * tstep
			gi = y0 * ww + x
			if hitv == 5:
				self.breach_slice(grid, x, y0, y1, tpos, tstep, tu, lv, side)
			elif hitv == 4:
				brow = bb + (side * 8 + lv) * 16
				# rainbow fire: ragged flame tongues per texel column, hue bands scroll up the cloth
				wave = (tu * 3 + tshift) % 12
				flen = 12 + (wave if wave < 6 else 12 - wave) * 2
				bank = (((tu >> 2) + (tshift >> 3)) & 3) * 4
				for y in range(y0, y1):
					tv = int(tpos)
					up = 31 - (tv & 31)
					heat = 0 if up > flen else (1 if up * 3 > flen * 2 else (2 if up * 3 > flen else 3))
					grid[gi] = brow + bank + heat
					tpos += tstep
					gi += ww
			else:
				wrow = wb + (((hitv - 1) * 2 + side) * 8 + lv) * 8
				tbase = (hitv - 1) * 512 + tu
				for y2 in range(y0, y1):
					tv2 = int(tpos)
					grid[gi] = wrow + tx[tbase + (tv2 & 31) * 16]
					tpos += tstep
					gi += ww
		draw_flame = self.draw_flame
		for k in range(len(self.tox)):
			draw_flame(grid, k)
		# sprite pass: camera-space transforms, then paint far to near
		planex = self.planex
		planey = self.planey
		dirx = self.dirx
		diry = self.diry
		posx = self.posx
		posy = self.posy
		ex = self.ex
		ey = self.ey
		es = self.es
		stx = self.stx
		sty = self.sty
		drawn = self.drawn
		ne = self.ne
		invdet = 1.0 / (planex * diry - dirx * planey)
		for i in range(ne):
			rx = ex[i] - posx
			ry = ey[i] - posy
			stx[i] = invdet * (diry * rx - dirx * ry)
			sty[i] = invdet * (planex * ry - planey * rx)
			drawn[i] = 0
		for pass_ in range(ne):
			pick = -1
			pty = -1.0
			for i2 in range(ne):
				if drawn[i2] == 1:
					continue
				st = es[i2]
				if st >= 2:
					continue
				sv = sty[i2]
				if sv <= 0.25:
					continue
				if sv > pty:
					pty = sv
					pick = i2
			if pick < 0:
				break
			drawn[pick] = 1
			self.draw_blob(grid, pick)
		self.draw_goo(grid, invdet)
		self.daylight(grid)
		self.draw_hud(grid)

	# out through the breach: daylight blooms from the heart of the frame, its edge dithered gold
	def daylight(self, grid):
		tt = self.tt
		if tt < MUSIC_BAR * 12.0:
			return
		sqrt = math.sqrt
		w = self.w
		h = self.h
		k = (tt - MUSIC_BAR * 12.0) / MUSIC_BAR
		k = 1.0 if k > 1.0 else k
		# the blast whites out the frame for a moment, then the opening blooms
		r = w * 1.2 if k < 0.06 else (0.12 + k * k * 0.6) * w
		band = 7.0
		cx = w * 0.5
		cy = h * 0.5 + self.pitch
		sb = self.sprite_base
		for y in range(h):
			dy = (y + 0.5 - cy) * 1.5
			oo = r * r - dy * dy
			if oo <= 0.0:
				continue
			# per row: the white heart, the warm white ring, the dithered gold edge
			xo = sqrt(oo)
			ri = r - band
			ii = ri * ri - dy * dy
			xi = sqrt(ii) if ii > 0.0 else 0.0
			hh = r * r * 0.36 - dy * dy
			xh = sqrt(hh) if hh > 0.0 else 0.0
			row = y * w
			x0 = int(cx - xo)
			x1 = int(cx + xo) + 1
			x0 = 0 if x0 < 0 else x0
			x1 = w if x1 > w else x1
			for x in range(x0, x1):
				dx = x + 0.5 - cx
				ax = -dx if dx < 0.0 else dx
				if ax < xh:
					grid[row + x] = sb + 24
				elif ax < xi:
					grid[row + x] = sb + 31
				else:
					over = (r - sqrt(dx * dx + dy * dy)) / band
					if over * 16.0 > BAYER4[(y & 3) * 4 + (x & 3)]:
						grid[row + x] = sb + 32

	# the breach: stone whose cracks glow hotter through the escape, then open daylight
	def breach_slice(self, grid, x, y0, y1, tpos, tstep, tu, lv, side):
		w = self.w
		bs = self.breach_state()
		sb = self.sprite_base
		gi = y0 * w + x
		if bs >= 2.0:
			# open: white at the heart of the opening, gold toward its edges, rays rippling up it
			shimmer = int(self.tt * 9.0)
			for y in range(y0, y1):
				tv = int(tpos)
				ev = (tv if tv < 16 else 31 - tv) >> 1
				eu = tu if tu < 8 else 15 - tu
				edge = ev if ev < eu else eu
				ray = 1 if ((tu * 5 + (tv >> 1) + shimmer) & 7) == 0 else 0
				grid[gi] = sb + 24 if edge > 3 else (sb + 31 if edge > 1 - ray else sb + 32)
				tpos += tstep
				gi += w
			return
		cue = self.cue
		hash2 = self.hash2
		tex = self.tex
		kick = cue.kick_env(self.tt) if cue is not None else 0.0
		heat = int((bs + kick * 0.35) * 14.0)
		wrow = self.wall_base + (side * 8 + lv) * 8
		for y2 in range(y0, y1):
			tv2 = int(tpos)
			ti = (tv2 & 31) * 16 + tu
			crack = (hash2(ti, 77) & 15)
			c = tex[ti]
			# mortar lines and a hashed web of cracks catch fire from the light behind
			pix = wrow + c
			if (c == 0 and crack < heat) or crack + 6 < heat - 8:
				pix = sb + 31 if crack < heat - 6 else (sb + 32 if crack < heat - 3 else sb + 33)
			grid[gi] = pix
			tpos += tstep
			gi += w

	# the goo in flight and on the floor: blobs sized by distance, lit by the map
	def draw_goo(self, grid, invdet):
		w = self.w
		h = self.h
		hc = float(h // 2 + int(self.pitch))
		zcol = self.zcol
		posx = self.posx
		posy = self.posy
		planex = self.planex
		planey = self.planey
		dirx = self.dirx
		diry = self.diry
		gx = self.gx
		gy = self.gy
		gz = self.gz
		glife = self.glife
		gtype = self.gtype
		sprite_base = self.sprite_base
		rubble_base = self.rubble_base
		light_at = self.light_at
		for g in range(self.ngoo):
			life = glife[g]
			if life <= 0.0:
				continue
			rx = gx[g] - posx
			ry = gy[g] - posy
			tyy = invdet * (planex * ry - planey * rx)
			if tyy < 0.3:
				continue
			txx = invdet * (diry * rx - dirx * ry)
			scx = int(w * 0.5 * (1.0 + txx / tyy))
			if scx < 0 or scx >= w or tyy >= zcol[scx]:
				continue
			scy = int(hc + (0.5 - gz[g]) * h / tyy)
			rad = int(h * (0.024 if gtype[g] == 2 else 0.04) / tyy * (life / 0.6 if life < 0.6 else 1.0))
			flat = 1 if gz[g] <= 0.0 else 0
			lk = 0.3 + light_at(gx[g], gy[g]) * 0.14
			base = rubble_base if gtype[g] == 2 else sprite_base + gtype[g] * 12
			body = base + int(lk * 10.0)
			body = base + 11 if body > base + 11 else body
			for dy in range(0 - rad, rad + 1):
				# the original's int dy / 2 truncates toward zero
				row = scy + (int(dy / 2) if flat == 1 else dy)
				if row < 0 or row >= h:
					continue
				for dx in range(0 - rad - flat * rad, rad + flat * rad + 1):
					col = scx + dx
					if col < 0 or col >= w or tyy >= zcol[col]:
						continue
					if dx * dx + dy * dy * (4 if flat == 1 else 1) > (rad + 1) * (rad + 1) * (4 if flat == 1 else 1):
						continue
					grid[row * w + col] = sprite_base + 52 if dy < 0 and dx < 0 and flat == 0 and gtype[g] != 2 else body

	def draw_hud(self, grid):
		w = self.w
		h = self.h
		sprite_base = self.sprite_base
		gx0 = w // 2
		gs = h // 40
		gs = 2 if gs < 2 else gs
		flick = int(self.tt * 10.0)
		# rainbow hitscan tracer from the muzzle to the target
		if self.tracer_t > 0.0:
			tr_x = self.tr_x
			tr_y = self.tr_y
			y0t = h - 11 * gs
			steps = 14 * gs
			for k2 in range(1, steps):
				xx = gx0 + idiv((tr_x - gx0) * k2, steps)
				yy = y0t + idiv((tr_y - y0t) * k2, steps)
				for th in range(gs // 2):
					if xx + th < 0 or xx + th >= w or yy < 1 or yy >= h:
						continue
					grid[yy * w + xx + th] = sprite_base + 34 + (k2 // gs + flick) % 6
		# the gun, drawn at gs x: bobs with the walk, kicks on the shot
		bob = int(math.sin(self.travel * 5.2) * 1.25 * gs)
		kick = 2 * gs if self.muzzle_t > 0.0 else 0
		gw = 16
		gh = len(self.gun) // gw
		gn = self.gun
		sb = sprite_base
		gxs = gx0 - gw * gs // 2 + bob
		gys = h - gh * gs + 2 * gs + kick + (0 - bob if bob < 0 else bob) // 2
		for ry in range(gh * gs):
			grow = (ry // gs) * gw
			yy2 = gys + ry
			if yy2 < 0 or yy2 >= h:
				continue
			for rx in range(gw * gs):
				c = gn[grow + rx // gs]
				if c == 0:
					continue
				xx2 = gxs + rx
				if xx2 >= 0 and xx2 < w:
					grid[yy2 * w + xx2] = sb + c
		if self.muzzle_t > 0.0:
			mx = gxs + gw * gs // 2 - 1
			my = gys - 2 * gs
			ray = 3 * gs
			for q in range(0 - ray, ray + 1):
				aq = 0 - q if q < 0 else q
				c2 = 31 if aq * 5 <= ray else (32 if aq * 5 <= ray * 3 else 33)
				for th2 in range(gs):
					if my + q >= 0 and my + q < h and mx + th2 < w:
						grid[(my + q) * w + mx + th2] = sprite_base + c2
					if mx + q >= 0 and mx + q < w and my + th2 >= 0 and my + th2 < h:
						grid[(my + th2) * w + mx + q] = sprite_base + c2
				if aq * 5 <= ray * 3 and mx + q >= 0 and mx + q < w and my + q >= 0 and my + q < h:
					grid[(my + q) * w + mx + q] = sprite_base + 32
				if aq * 5 <= ray * 3 and mx - q >= 0 and mx - q < w and my + q >= 0 and my + q < h:
					grid[(my + q) * w + mx - q] = sprite_base + 32
		chc = sprite_base + 41 if self.hot_t > 0.0 else sprite_base + 40
		cy = int(h * 0.5)
		cr = gs // 2
		for d in range(cr + 1, cr * 2 + 3):
			grid[cy * w + gx0 - d] = chc
			grid[cy * w + gx0 + d] = chc
			grid[(cy - d) * w + gx0] = chc
			grid[(cy + d) * w + gx0] = chc
		ts = h // 80
		ts = 1 if ts < 1 else ts
		kills = self.kills
		tally = f"KILLS {'0' if kills < 10 else ''}{kills}"
		gfx.draw_text(grid, w, h, tally, 2, 2, ts, ts, sprite_base + 42, 2, sprite_base + 44, 99)
		# the callout: it punches in large and settles, rising as it fades
		callout = self.callout
		if self.call_t > 0.0 and len(callout) > 0:
			age = 0.9 - self.call_t
			cs = w * 3 // 5 // (len(callout) * 6)
			cs = 1 if cs < 1 else (3 * ts if cs > 3 * ts else cs)
			cs += 1 if age < 0.08 else 0
			tw = len(callout) * 6 * cs
			cx = idiv(w - tw, 2)
			cyy = int(h * 0.22 - age * h * 0.08)
			ink = sprite_base + 53 + (flick & 3) if self.combo >= 2 else sprite_base + 49 + (flick & 1) * 2
			if cx > 0:
				gfx.draw_text(grid, w, h, callout, cx, cyy, cs, cs, ink, 1, sprite_base + 44, 99)


SCENES[6] = GlobHunter
