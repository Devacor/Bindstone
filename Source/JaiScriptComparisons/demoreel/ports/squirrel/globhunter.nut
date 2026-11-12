// GLOBHUNTER (Squirrel port of scenes/globhunter.jai): a Wolfenstein-style grid raycaster, one DDA ray
// per screen column over an authored keep, textured walls, floor and ceiling cast per pixel, a light map
// rebuilt from flickering torches, billboarded globs lit as spheres, goo particles, and a scripted camera
// that hunts the garrison to its cue and escapes through the east wall.

::BAYER4 <- [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5];

class GlobHunter extends ::Scene {
	wmap = null;
	mw = 24;
	mh = 27;
	wpx = null;
	wpy = null;
	seglen = null;
	nwp = 6;
	path_len = 0.0;
	travel = 0.0;
	posx = 2.5;
	posy = 2.5;
	dirx = 0.0;
	diry = 1.0;
	planex = 0.0;
	planey = 0.0;
	aim = 0.0;
	pitch = 0.0;
	tex = null;
	wall_base = 0;
	banner_base = 0;
	floor_base = 0;
	ceil_base = 0;
	sprite_base = 0;
	tt = 0.0;
	fr = 0;
	ex = null;
	ey = null;
	et = null;
	es = null;
	est = null;
	stx = null;
	sty = null;
	drawn = null;
	zcol = null;
	wall_top = null;
	wall_foot = null;
	wall_hit = null;
	ceil_lo = null;
	ceil_hi = null;
	floor_lo = null;
	floor_hi = null;
	gun = null;
	ne = 18;
	boss_flash = 0.0;
	kills = 0;
	cool = 0.6;
	last_step = -1;
	muzzle_t = 0.0;
	tracer_t = 0.0;
	hot_t = 0.0;
	kick_t = 0.0;
	stop_t = 0.0;
	last_kill = -9.0;
	combo = 0;
	call_t = 0.0;
	callout = "";
	tr_x = 0;
	tr_y = 0;
	tox = null;
	toy = null;
	still = null;
	owner = null;
	lmap = null;
	lw = 48;
	lh = 54;
	vault = 2.0;
	gx = null;
	gy = null;
	gz = null;
	gvx = null;
	gvy = null;
	gvz = null;
	glife = null;
	gtype = null;
	ngoo = 220;
	goo_next = 0;
	vigx = null;
	vigy = null;
	rubble_base = 0;
	breach_x = 23;
	breach_y = 13;
	last_snare = 0.0;

	constructor() {
		wmap = [];
		wpx = [];
		wpy = [];
		seglen = [];
		tex = [];
		ex = [];
		ey = [];
		et = [];
		es = [];
		est = [];
		stx = [];
		sty = [];
		drawn = [];
		zcol = [];
		wall_top = [];
		wall_foot = [];
		wall_hit = [];
		ceil_lo = [];
		ceil_hi = [];
		floor_lo = [];
		floor_hi = [];
		gun = [];
		tox = [];
		toy = [];
		still = [];
		owner = [];
		lmap = [];
		gx = [];
		gy = [];
		gz = [];
		gvx = [];
		gvy = [];
		gvz = [];
		glife = [];
		gtype = [];
		vigx = [];
		vigy = [];
	}

	function build() {
		name = "GLOBHUNTER";
		handle = "grublin keep . torchlight, one ray per column, zero mercy";
		replay = 26.0;
		// # stone  M moss  G gold pillar  B banner (rainbow-fire, animated)
		local rows = [
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
		];
		for (local y = 0; y < mh; ++y) {
			local row = rows[y];
			for (local x = 0; x < mw; ++x) {
				local ch = row[x];
				local v = 0;
				if (ch == '#') { v = 1; }
				else if (ch == 'M') { v = 2; }
				else if (ch == 'G') { v = 3; }
				else if (ch == 'B') { v = 4; }
				wmap.push(v);
			}
		}
		wmap[breach_y * mw + breach_x] = 5;
		local fogc = [5.0, 7.0, 18.0];
		local wall_cols = [
			[[40.0, 36.0, 44.0], [88.0, 88.0, 100.0], [104.0, 104.0, 118.0], [120.0, 120.0, 136.0], [138.0, 138.0, 152.0], [156.0, 156.0, 170.0], [186.0, 186.0, 200.0], [70.0, 70.0, 82.0]],
			[[34.0, 36.0, 34.0], [84.0, 90.0, 92.0], [100.0, 106.0, 108.0], [118.0, 124.0, 126.0], [60.0, 118.0, 50.0], [84.0, 150.0, 62.0], [120.0, 190.0, 80.0], [40.0, 86.0, 36.0]],
			[[90.0, 60.0, 20.0], [140.0, 100.0, 36.0], [176.0, 130.0, 50.0], [206.0, 160.0, 64.0], [232.0, 190.0, 90.0], [252.0, 222.0, 130.0], [255.0, 246.0, 200.0], [118.0, 80.0, 28.0]]];
		wall_base = pal.size();
		for (local t = 0; t < 3; ++t) {
			local cols = wall_cols[t];
			for (local side = 0; side < 2; ++side) {
				for (local f = 0; f < 8; ++f) {
					for (local c = 0; c < 8; ++c) {
						local cc = cols[c];
						lit(cc[0], cc[1], cc[2], f, side, fogc);
					}
				}
			}
		}
		banner_base = pal.size();
		for (local side2 = 0; side2 < 2; ++side2) {
			for (local f2 = 0; f2 < 8; ++f2) {
				for (local hu = 0; hu < 4; ++hu) {
					local rc = ::gfx.rainbow_rgb(hu * 1.5708);
					local rr = rc[0]; local rg = rc[1]; local rb = rc[2];
					local f3 = f2 < 3 ? 3 : f2;   // the banner's fire glows in the dark
					lit(40.0, 16.0, 60.0, f3, side2, fogc);
					lit(rr * 0.45, rg * 0.45, rb * 0.45, f3, side2, fogc);
					lit(rr, rg, rb, f3, side2, fogc);
					lit(255.0, 196.0, 80.0, f3, side2, fogc);
				}
			}
		}
		local floor_cols = [[46.0, 38.0, 34.0], [92.0, 80.0, 70.0], [104.0, 92.0, 80.0], [116.0, 102.0, 88.0], [128.0, 112.0, 96.0], [140.0, 124.0, 106.0], [76.0, 66.0, 58.0], [160.0, 142.0, 120.0]];
		floor_base = pal.size();
		for (local f4 = 0; f4 < 8; ++f4) {
			for (local c3 = 0; c3 < 8; ++c3) { local fc = floor_cols[c3]; lit(fc[0], fc[1], fc[2], f4, 0, fogc); }
		}
		local ceil_cols = [[20.0, 12.0, 8.0], [60.0, 38.0, 22.0], [72.0, 46.0, 26.0], [84.0, 54.0, 30.0], [96.0, 62.0, 34.0], [50.0, 32.0, 18.0], [110.0, 72.0, 40.0], [36.0, 24.0, 14.0]];
		ceil_base = pal.size();
		for (local f5 = 0; f5 < 8; ++f5) {
			for (local c4 = 0; c4 < 8; ++c4) { local kc = ceil_cols[c4]; lit(kc[0] * 0.8, kc[1] * 0.8, kc[2] * 0.8, f5, 0, fogc); }
		}
		// sprites + combat kit
		sprite_base = pal.size();
		pal.ramp(12, 10.0, 34.0, 12.0, 140.0, 240.0, 110.0);      // +0..11 grublin
		pal.ramp(12, 36.0, 16.0, 10.0, 230.0, 140.0, 80.0);       // +12..23 orglis
		pal.add(255, 255, 255);                                   // +24 hit flash
		pal.add(250, 246, 220);                                   // +25 eye white
		pal.add(20, 8, 8);                                        // +26 pupil / mouth
		pal.ramp(4, 36.0, 38.0, 48.0, 150.0, 156.0, 176.0);       // +27..30 gun
		pal.add(255, 250, 200);                                   // +31 muzzle core
		pal.add(255, 196, 70);                                    // +32 muzzle rim
		pal.add(255, 110, 30);                                    // +33 muzzle tip
		for (local tr = 0; tr < 6; ++tr) {                        // +34..39 rainbow tracer
			local ct = ::gfx.rainbow_rgb(tr * 1.0471975512);
			pal.add(ct[0], ct[1], ct[2]);
		}
		pal.add(130, 255, 150);                                   // +40 crosshair
		pal.add(255, 84, 64);                                     // +41 crosshair, target down
		pal.add(255, 226, 100);                                   // +42 tally ink
		pal.add(255, 140, 50);                                    // +43 tally ink low
		pal.add(8, 6, 10);                                        // +44 tally shadow
		pal.add(170, 30, 30);                                     // +45 grin
		pal.add(226, 170, 128);                                   // +46 skin
		pal.add(150, 100, 72);                                    // +47 skin shade
		pal.add(255, 60, 40);                                     // +48 glowing eye
		pal.ramp(4, 120.0, 255.0, 120.0, 230.0, 255.0, 200.0);    // +49..52 callout ink, grublin goo glint
		pal.ramp(4, 255.0, 120.0, 220.0, 255.0, 240.0, 255.0);    // +53..56 combo ink
		rubble_base = pal.size();
		pal.ramp(12, 22.0, 20.0, 28.0, 196.0, 186.0, 172.0);
		grow_textures();
		local gun_rows = ["......dlhd......", "......dlmd......", "......dlmd......", ".....ddlmdd.....", ".....dlhmmd.....",
			"....ddlmmmdd....", "....dlhmmmmd....", "...kdlmmmmmdk...", "..kkdmmmmmmdkk..", ".kkkkdmmmmdkkkk.",
			"skkkkkddddkkkkks", "sskkkkkkkkkkkkss"];
		for (local gr = 0; gr < gun_rows.len(); ++gr) {
			local gun_row = gun_rows[gr];
			for (local gc = 0; gc < 16; ++gc) {
				local ch2 = gun_row[gc];
				gun.push(ch2 == '.' ? 0 : (ch2 == 'd' ? 27 : (ch2 == 'm' ? 28 : (ch2 == 'l' ? 29 : (ch2 == 'h' ? 30 : (ch2 == 'k' ? 46 : 47))))));
			}
		}
		// the flight loop down the middle of each hall: west hall, south room and arena, mid room, east hall
		wpx = [3.5, 3.5, 13.5, 13.5, 22.5, 22.5];
		wpy = [3.5, 22.5, 22.5, 13.5, 13.5, 3.5];
		for (local i = 0; i < nwp; ++i) {
			local ddx = wpx[(i + 1) % nwp] - wpx[i];
			local ddy = wpy[(i + 1) % nwp] - wpy[i];
			seglen.push(::sqrt(ddx * ddx + ddy * ddy));
			path_len += seglen[i];
		}
		// garrison along the route (three hold the south room for the frenzy, three wait along the escape);
		// every post stands farther from the nearest wall than its kind's reach, the boss in the arena's open floor
		ex = [3.5, 3.0, 4.0, 3.0, 7.0, 15.5, 13.5, 14.5, 19.0, 21.5, 20.5, 10.0, 11.5, 12.0, 11.0, 17.5, 21.0, 13.5];
		ey = [7.5, 13.5, 17.5, 21.2, 21.0, 21.0, 14.0, 12.5, 13.5, 8.5, 3.0, 22.5, 23.5, 21.8, 12.5, 13.5, 13.0, 18.5];
		et = [0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 0, 1, 0, 1];
		for (local i2 = 0; i2 < ne; ++i2) {
			es.push(0);
			est.push(0.0);
			stx.push(0.0);
			sty.push(0.0);
			drawn.push(0);
		}
		es[ne - 1] = 3;   // the boss waits for bar seven
		for (local a = ne - 4; a < ne - 1; ++a) {
			// the ambush rises (a downed mark stands again after 16 s) as the escape begins
			es[a] = 3;
			est[a] = 16.0 - ::MUSIC_BAR * 10.0;
		}
		zcol = ::gfx.make_fgrid(w);
		for (local i3 = 0; i3 < w; ++i3) { zcol[i3] = 99.0; }
		wall_top = ::gfx.make_grid(w);
		wall_foot = ::gfx.make_grid(w);
		wall_hit = ::gfx.make_grid(w);
		ceil_lo = ::gfx.make_grid(h + 1);
		ceil_hi = ::gfx.make_grid(h + 1);
		floor_lo = ::gfx.make_grid(h + 1);
		floor_hi = ::gfx.make_grid(h + 1);
		// torches hang beside the walls along the route; each keeps its falloff over the light map
		local spots = [[1.2, 3.5], [7.5, 1.3], [2.2, 9.5], [4.8, 15.5], [1.2, 22.5], [7.5, 20.3], [9.3, 19.5],
			[17.7, 22.5], [11.4, 17.3], [15.6, 17.3], [9.3, 13.5], [15.5, 11.2], [20.5, 14.8], [22.8, 7.5], [22.8, 2.5], [17.2, 1.4]];
		for (local k = 0; k < spots.len(); ++k) {
			local sp = spots[k];
			local px = sp[0];
			local py = sp[1];
			if (wmap[py.tointeger() * mw + px.tointeger()] != 0) { continue; }
			tox.push(px);
			toy.push(py);
		}
		local best = [];
		for (local c5 = 0; c5 < lw * lh; ++c5) {
			still.push(0.0);
			owner.push(0);
			best.push(0.0);
		}
		for (local k3 = 0; k3 < tox.len(); ++k3) {
			for (local c6 = 0; c6 < lw * lh; ++c6) {
				local lx = (c6 % lw + 0.5) * 0.5 - tox[k3];
				local ly = (c6 / lw + 0.5) * 0.5 - toy[k3];
				local f = 1.0 / (1.0 + (lx * lx + ly * ly) * 1.7);
				still[c6] = still[c6] + f * 5.6 * 0.82;
				if (f > best[c6]) {
					best[c6] = f;
					owner[c6] = k3;
				}
			}
		}
		lmap = ::gfx.make_grid(lw * lh);
		for (local g = 0; g < ngoo; ++g) {
			gx.push(0.0);
			gy.push(0.0);
			gz.push(0.0);
			gvx.push(0.0);
			gvy.push(0.0);
			gvz.push(0.0);
			glife.push(0.0);
			gtype.push(0);
		}
		vigx = ::gfx.make_grid(w);
		vigy = ::gfx.make_grid(h);
		for (local vx = 0; vx < w; ++vx) {
			local e = 2.0 * vx / (w - 1.0) - 1.0;
			vigx[vx] = (e * e * e * e * 2.4).tointeger();
		}
		for (local vy = 0; vy < h; ++vy) {
			local e2 = 2.0 * vy / (h - 1.0) - 1.0;
			vigy[vy] = (e2 * e2 * e2 * e2 * 1.6).tointeger();
		}
	}

	// one entry: a colour at light level f (0 black gloom .. 7 full torchlight):
	// the dark sinks into a cold blue, the lit end warms toward the fire
	function lit(r, g, b, f, side, fogc) {
		local k = (0.07 + 0.93 * f / 7.0) * (side == 1 ? 0.72 : 1.0);
		local warm = f >= 5 ? (f - 4) / 3.0 : 0.0;
		local fr2 = fogc[0]; local fg = fogc[1]; local fb = fogc[2];
		local cr = r * (1.0 + 0.32 * warm);
		local cg = g * (1.0 + 0.06 * warm);
		local cb = b * (1.0 - 0.28 * warm);
		pal.add(cr * k + fr2 * (1.0 - k), cg * k + fg * (1.0 - k), cb * k + fb * (1.0 - k));
	}

	function hash2(a, b) {
		local hs = (a * 73856093 + b * 19349663 + (seed & 255) * 83492791) & 1048575;
		return ((hs * 1274126177) >> 12) & 1023;
	}

	function grow_textures() {
		// 0 coursed stone, 1 stone mossy toward its foot, 2 fluted gold (16 wide, the vault's 32 tall), 3 flagstones, 4 oak planks
		for (local t = 0; t < 5; ++t) {
			local tall = t < 3 ? 32 : 16;
			for (local v = 0; v < tall; ++v) {
				for (local u = 0; u < 16; ++u) {
					local c = 0;
					if (t <= 1) {
						local row = v / 4;
						local bu = (u + (row % 2) * 4) % 8;
						local bv = v % 4;
						local brick = hash2((u + (row % 2) * 4) / 8 + row * 7, t);
						c = 1 + brick % 4;
						if (bv == 1) { ++c; }
						if (bv == 3 && c > 1) { --c; }
						if (bu == 0 || bv == 0) { c = 0; }
						if (t == 1 && c != 0) {
							local m = hash2(u * 3 + v, v * 5 + 11) % 100;
							if (m < (v - 13) * 5) { c = 4 + m % 3; }
						}
						if (t == 1 && c == 0 && v > 12 && hash2(u, v) % 3 == 0) { c = 7; }
					} else if (t == 2) {
						local fl = u % 4;
						c = fl == 0 ? 7 : (fl == 1 ? 5 : (fl == 2 ? 4 : 2));
						if (v % 8 == 0) { c = 6; }
						if (v % 8 == 1) { c = 1; }
					} else if (t == 3) {
						local su = (u + (v / 8) * 4) % 8;
						local sv = v % 8;
						local stone = hash2((u + (v / 8) * 4) / 8, v / 8 + 40);
						c = 1 + stone % 5;
						if (sv == 1 || su == 1) { c = 7; }
						if (su == 0 || sv == 0) { c = 0; }
						if (hash2(u + 50, v) % 11 == 0 && c != 0) { c = 6; }
					} else {
						local plank = v / 4;
						local pv = v % 4;
						c = 2 + (hash2(plank, 90) % 3);
						if ((u + plank * 5) % 16 == 0) { c = 0; }
						if (pv == 0) { c = 5; }
						if (pv == 3) { c = 7; }
						if (hash2(u, v + 90) % 13 == 0) { c = 6; }
					}
					tex.push(c);
				}
			}
		}
		// plain copies of the floor and the ceiling for their far rows: the joints over one shade
		for (local i = 1536; i < 2048; ++i) {
			local texel = tex[i];
			local joint = texel == 0 || (i >= 1792 && texel == 7);
			tex.push(joint ? texel : 3);
		}
	}

	function path_at(s) {
		s -= ::floor(s / path_len) * path_len;
		for (local i = 0; i < nwp; ++i) {
			local L = seglen[i];
			if (s <= L) {
				local f = L > 0.0001 ? s / L : 0.0;
				local j = (i + 1) % nwp;
				return [wpx[i] + (wpx[j] - wpx[i]) * f, wpy[i] + (wpy[j] - wpy[i]) * f];
			}
			s -= L;
		}
		return [wpx[0], wpy[0]];
	}

	// DDA toward (dx, dy) in parameter space: true if no wall before frac of the way
	function los_clear(dx, dy, frac) {
		local mx = posx.tointeger();
		local my = posy.tointeger();
		local adx = dx < 0.0 ? -dx : dx;
		local ady = dy < 0.0 ? -dy : dy;
		local ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
		local ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
		local stepx = dx < 0.0 ? -1 : 1;
		local stepy = dy < 0.0 ? -1 : 1;
		local sdx = dx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
		local sdy = dy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
		local wm = wmap;
		local mcols = mw;
		for (local g = 0; g < 64; ++g) {
			local trav = sdx < sdy ? sdx : sdy;
			if (trav >= frac) { return true; }
			if (sdx < sdy) { sdx += ddx; mx += stepx; }
			else { sdy += ddy; my += stepy; }
			local cv = wm[my * mcols + mx];
			if (cv > 0) { return false; }
		}
		return false;
	}

	function length_bars() { return 13; }
	function hit_times() { return [::MUSIC_BAR * 3.0, ::MUSIC_BAR * 5.0, ::MUSIC_BAR * 7.0, ::MUSIC_BAR * 9.0, ::MUSIC_BAR * 10.0, ::MUSIC_BAR * 12.0]; }
	function hit_levels() { return [1, 2, 1, 2, 1, 2]; }

	// the route's distance from which the walk faces the arena floor where the boss rises
	function arena_stand() { return 19.0 + 9.8; }

	// the route's distance at which the escape reaches the breach, facing it
	function breach_stop() { return 19.0 + 10.0 + 9.0 + 8.3; }

	function update(t, dt) {
		local bar = ::MUSIC_BAR;
		tt = t;
		++fr;
		local aftermath = t > bar * 9.0 && t < bar * 10.0;
		local escape = t >= bar * 10.0 && t < bar * 12.0;
		local out = t >= bar * 12.0;
		local fight = t > bar * 7.0 && t < bar * 9.0;
		local bullet = slow_mo(t);
		local lap = travel - ::floor(travel / path_len) * path_len;
		local pace = stop_t > 0.0 ? 0.18 : (aftermath ? 0.4 : (fight ? 0.16 : 1.0));
		if (escape) {
			// the run for the breach: always just fast enough to reach it on bar twelve
			local want = (breach_stop() - lap) / (bar * 12.0 - t + 0.05) / 2.4;
			want = want < 0.0 ? 0.0 : (want > 3.0 ? 3.0 : want);
			pace = stop_t > 0.0 ? want * 0.5 : want;
		}
		if (t >= bar * 6.0 && t < bar * 7.0) {
			// the walk into the arena: always just fast enough to face the boss's floor on bar seven
			local gait = (arena_stand() - lap) / (bar * 7.0 - t + 0.05) / 2.4;
			gait = gait < 0.3 ? 0.3 : (gait > 2.0 ? 2.0 : gait);
			pace = stop_t > 0.0 ? gait * 0.5 : gait;
		}
		if (out) { pace = 0.0; }
		pace *= bullet;
		travel += dt * 2.4 * pace;
		lap = travel - ::floor(travel / path_len) * path_len;
		local look = 1.7;
		if (escape || out) {
			local to_corner = 19.0 + 10.0 + 9.0 + 9.0 - lap;
			look = to_corner < look && to_corner > 0.0 ? to_corner : look;
		}
		local p = path_at(travel);
		local q = path_at(travel + look);   // look-ahead steers the turns
		posx = p[0];
		posy = p[1];
		if (out) {
			// into the light: the last bar walks through the breach
			local walk = (t - bar * 12.0) / bar;
			walk = walk > 1.0 ? 1.0 : walk;
			posx += walk * (2.0 - walk) * 0.92;
		}
		local bx = q[0] - posx;
		local by = q[1] - posy;
		local bl = ::sqrt(bx * bx + by * by);
		if (out) { bx = 1.0; by = 0.0; bl = 1.0; }
		if (bl > 0.0001) { bx /= bl; by /= bl; } else { bx = dirx; by = diry; }
		// the hunt: swing toward the nearest live mark in clear sight ahead
		local want = 0.0;
		local near = 5.5;
		for (local m = 0; m < ne; ++m) {
			if (es[m] != 0) { continue; }
			local mx = ex[m] - posx;
			local my = ey[m] - posy;
			local md = ::sqrt(mx * mx + my * my);
			if (md > near || md < 0.6) { continue; }
			local ang = ::atan2(bx * my - by * mx, bx * mx + by * my);
			if (ang > 0.75 || ang < -0.75) { continue; }
			if (!los_clear(mx, my, 0.95)) { continue; }
			near = md;
			want = ang;
		}
		if (out) { want = 0.0; }
		local ease = dt * 7.0;
		ease = ease > 1.0 ? 1.0 : ease;
		aim += (want - aim) * ease;
		// the quake: each kill shakes the view, the big hit hardest, and the keep rumbles as it falls
		local kick_now = cue != null ? cue.kick_env(t) : 0.0;
		local quake = (hot_t > 0.0 ? hot_t * 0.16 : 0.0) + (cue != null ? cue.hit(t) * 0.05 : 0.0);
		if (escape) { quake += 0.012 + kick_now * 0.03; }
		local view = aim + quake * ::sin(fr * 2.7);
		local ca = ::cos(view);
		local sa = ::sin(view);
		dirx = bx * ca - by * sa;
		diry = bx * sa + by * ca;
		planex = 0.0 - diry * 0.66;
		planey = dirx * 0.66;
		// timers and the enemy state machine
		cool -= dt;
		muzzle_t -= dt;
		tracer_t -= dt;
		hot_t -= dt;
		kick_t -= dt;
		stop_t -= dt;
		call_t -= dt;
		boss_flash -= dt;
		for (local i = 0; i < ne; ++i) {
			local st = es[i];
			if (st == 1) { est[i] = est[i] + dt; if (est[i] > 0.14) { es[i] = 2; est[i] = 0.0; } }
			else if (st == 2) { est[i] = est[i] + dt; if (est[i] > 0.55) { es[i] = 3; est[i] = 0.0; } }
			else if (st == 3) { est[i] = est[i] + dt; if (est[i] > 16.0) { es[i] = 0; est[i] = 0.0; } }
		}
		// auto-fire on the beat: nearest alive enemy in the crosshair cone with clear LOS
		local e = cue != null ? cue.energy(t) : 0.6;
		local eighths = e > 0.92 || (t > bar * 5.0 && t < bar * 6.0) || escape;
		// the big hit: the hall floods with light and the frenzy is called
		if (t >= bar * 5.0 && t - dt < bar * 5.0) {
			callout = "FRENZY";
			call_t = 1.2;
		}
		local step = ::ifloor(t / (eighths ? ::MUSIC_BEAT * 0.5 : ::MUSIC_BEAT));
		local on_step = step != last_step;
		last_step = step;
		if (t >= bar * 10.0 && t - dt < bar * 10.0) {
			callout = "RUN";
			call_t = 1.3;
			hot_t = 0.5;
		}
		if (t >= bar * 12.0 && t - dt < bar * 12.0) { blow_breach(); }
		if (escape) { falling_stone(t); }
		if (cool <= 0.0 && on_step && !aftermath && !out && t > bar) {
			local invdet = 1.0 / (planex * diry - dirx * planey);
			local best = -1;
			local besty = 99.0;
			local bestx = 0.0;
			for (local i2 = 0; i2 < ne; ++i2) {
				local st2 = es[i2];
				if (st2 != 0) { continue; }
				local rx = ex[i2] - posx;
				local ry = ey[i2] - posy;
				local txx = invdet * (diry * rx - dirx * ry);
				local tyy = invdet * (planex * ry - planey * rx);
				if (tyy < 1.0 || tyy > 4.6) { continue; }
				local off = txx < 0.0 ? -txx : txx;
				if (off > tyy * 0.12) { continue; }
				if (!los_clear(rx, ry, 0.95)) { continue; }
				if (tyy < besty) { best = i2; besty = tyy; bestx = txx; }
			}
			if (best >= 0) { fire(best, bestx, besty, t); }
		}
		// the boss rises from the arena's open floor on bar seven
		local boss = ne - 1;
		if (t >= bar * 7.0 && t - dt < bar * 7.0) {
			es[boss] = 0;
			est[boss] = 0.0;
			callout = "BOSS GLOB";
			call_t = 1.6;
			// the garrison scatters before it
			for (local m2 = 0; m2 < boss; ++m2) {
				local ddx2 = ex[m2] - posx;
				local ddy2 = ey[m2] - posy;
				if (ddx2 * ddx2 + ddy2 * ddy2 < 64.0) {
					es[m2] = 3;
					est[m2] = 0.0;
				}
			}
		}
		// bar nine's downbeat: the boss bursts
		if (t >= bar * 9.0 && t - dt < bar * 9.0 && es[boss] == 0) {
			local invdet2 = 1.0 / (planex * diry - dirx * planey);
			local rbx = ex[boss] - posx;
			local rby = ey[boss] - posy;
			local bsx = invdet2 * (diry * rbx - dirx * rby);
			local bsy = invdet2 * (planex * rby - planey * rbx);
			fire(boss, bsx, bsy > 0.3 ? bsy : 0.3, t);
		}
		move_goo(dt * bullet);
		// the flicker is redrawn at 15 Hz, and every frame while the muzzle burns or the kick lands
		local beat_light = cue != null ? cue.kick_env(t) + cue.hit(t) : 0.0;
		if ((fr & 1) == 0 || muzzle_t > -0.05 || beat_light > 0.2 || escape || out) { light_up(); }
		// the view: a footfall on each beat (each eighth on the run), the recoil's kick, a shudder
		// while the muzzle burns, and the falling keep's tremor
		local stride = cue != null ? cue.beat_phase(t) : travel * 0.83;
		if (escape) { stride = stride * 2.0 - ::floor(stride * 2.0); }
		local bob = ::sin(stride * 3.1415927) * h * (escape ? 0.022 : 0.014) * (aftermath || out ? 0.4 : 1.0);
		local kick = kick_t > 0.0 ? h * 0.05 * kick_t / 0.2 : 0.0;
		local shud = muzzle_t > 0.0 ? ((fr * 7) % 3 - 1) * h * 0.012 : 0.0;
		local tremor = escape ? ::sin(fr * 5.3) * h * (0.006 + kick_now * 0.02) : 0.0;
		pitch = bob + kick + shud + tremor;
	}

	// the escape: on each snare stones shake loose from the vault ahead and fall
	function falling_stone(t) {
		local sn = cue != null ? cue.snare_env(t) : 0.0;
		local onset = sn > last_snare + 0.25;
		last_snare = sn;
		if (!onset) { return; }
		for (local k = 0; k < 14; ++k) {
			local g = goo_next;
			goo_next = (goo_next + 1) % ngoo;
			local ahead = 1.2 + rng.nextf() * 3.2;
			local across = (rng.nextf() - 0.5) * 2.6;
			local sx = posx + dirx * ahead - diry * across;
			local sy = posy + diry * ahead + dirx * across;
			if (wmap[sy.tointeger() * mw + sx.tointeger()] != 0) { continue; }
			gx[g] = sx;
			gy[g] = sy;
			gz[g] = vault - 0.05;
			gvx[g] = (rng.nextf() - 0.5) * 0.6;
			gvy[g] = (rng.nextf() - 0.5) * 0.6;
			gvz[g] = 0.0 - rng.nextf() * 1.5;
			glife[g] = 1.4 + rng.nextf() * 0.8;
			gtype[g] = 2;
		}
	}

	// bar twelve: the east wall bursts, stone flies at the camera and daylight floods in
	function blow_breach() {
		callout = "KEEP CLEARED";
		call_t = 2.6;
		hot_t = 0.8;
		for (local k = 0; k < 70; ++k) {
			local g = goo_next;
			goo_next = (goo_next + 1) % ngoo;
			gx[g] = breach_x - 0.05;
			gy[g] = breach_y + 0.1 + rng.nextf() * 0.8;
			gz[g] = 0.1 + rng.nextf() * 0.8;
			gvx[g] = 0.0 - (1.5 + rng.nextf() * 4.5);
			gvy[g] = (rng.nextf() - 0.5) * 2.4;
			gvz[g] = 0.5 + rng.nextf() * 2.5;
			glife[g] = 1.8 + rng.nextf() * 1.2;
			gtype[g] = 2;
		}
	}

	// 0 a wall, rising to 1 as the cracks of the breach open through the escape, 2 blown open
	function breach_state() {
		if (tt >= ::MUSIC_BAR * 12.0) { return 2.0; }
		local k = (tt - ::MUSIC_BAR * 10.0) / (::MUSIC_BAR * 2.0);
		return k < 0.0 ? 0.0 : k;
	}

	// bullet time: two beats after each big hit play at a fifth of the speed
	function slow_mo(t) {
		local since = t - ::MUSIC_BAR * 5.0;
		if (t >= ::MUSIC_BAR * 9.0) { since = t - ::MUSIC_BAR * 9.0; }
		if (t >= ::MUSIC_BAR * 12.0) { since = t - ::MUSIC_BAR * 12.0; }
		if (since < 0.0 || since > ::MUSIC_BEAT * 2.5) { return 1.0; }
		local back = (since - ::MUSIC_BEAT * 2.0) / (::MUSIC_BEAT * 0.5);
		back = back < 0.0 ? 0.0 : (back > 1.0 ? 1.0 : back);
		return 0.2 + 0.8 * back;
	}

	function fire(i, bx, by, t) {
		local boss = i == ne - 1;
		local boss_down = boss && t >= ::MUSIC_BAR * 9.0 - 0.01;
		if (boss && !boss_down) {
			// a hit the boss takes: the muzzle, the tracer and a flash, but it stands
			boss_flash = 0.1;
			cool = 0.2;
			muzzle_t = 0.12;
			tracer_t = 0.14;
			kick_t = 0.2;
			tr_x = (w * 0.5 * (1.0 + bx / by)).tointeger();
			tr_y = h / 2 + (h * 0.10 / by).tointeger();
			callout = "";
			for (local k0 = 0; k0 < 8; ++k0) {
				local g0 = goo_next;
				goo_next = (goo_next + 1) % ngoo;
				local a0 = rng.nextf() * 6.2831853;
				gx[g0] = ex[i];
				gy[g0] = ey[i];
				gz[g0] = 0.5 + rng.nextf() * 0.4;
				gvx[g0] = ::cos(a0) * 1.2 - dirx * 0.8;
				gvy[g0] = ::sin(a0) * 1.2 - diry * 0.8;
				gvz[g0] = 1.0 + rng.nextf() * 2.0;
				glife[g0] = 1.6;
				gtype[g0] = 1;
			}
			return;
		}
		es[i] = 1;
		est[i] = 0.0;
		++kills;
		cool = 0.2;
		muzzle_t = 0.12;
		tracer_t = 0.14;
		hot_t = 0.35;
		kick_t = 0.2;
		stop_t = 0.16;
		tr_x = (w * 0.5 * (1.0 + bx / by)).tointeger();
		tr_y = h / 2 + (h * 0.10 / by).tointeger();
		combo = t - last_kill < 1.8 ? combo + 1 : 1;
		last_kill = t;
		local words = ["SPLAT", "GLOBBED", "SQUISH", "POP"];
		callout = boss_down ? "BOSS DOWN" : (combo >= 3 ? "MULTI KILL" : (combo == 2 ? "DOUBLE KILL" : words[kills % 4]));
		call_t = boss_down ? 2.2 : 0.9;
		if (boss_down) { hot_t = 0.8; }
		// the burst: goo flung out of the body, mostly up and back from the shot
		for (local k = 0; k < (boss_down ? 120 : 34); ++k) {
			local g = goo_next;
			goo_next = (goo_next + 1) % ngoo;
			local a = rng.nextf() * 6.2831853;
			local sp = 0.8 + rng.nextf() * 2.2;
			gx[g] = ex[i];
			gy[g] = ey[i];
			gz[g] = 0.35 + rng.nextf() * 0.3;
			gvx[g] = ::cos(a) * sp + dirx * 1.2;
			gvy[g] = ::sin(a) * sp + diry * 1.2;
			gvz[g] = 1.2 + rng.nextf() * 2.6;
			glife[g] = 2.2 + rng.nextf() * 1.5;
			gtype[g] = et[i];
		}
	}

	// goo falls, bounces off the flagstones and the walls, then lies there fading
	function move_goo(dt) {
		local wm = wmap;
		local mcols = mw;
		local lx = gx;
		local ly = gy;
		local lz = gz;
		local lvx = gvx;
		local lvy = gvy;
		local lvz = gvz;
		local lf = glife;
		for (local g = 0; g < ngoo; ++g) {
			local life = lf[g];
			if (life <= 0.0) { continue; }
			lf[g] = life - dt;
			local vz = lvz[g] - 7.5 * dt;
			local nz = lz[g] + vz * dt;
			if (nz < 0.0) {
				nz = 0.0;
				vz = vz < -0.6 ? 0.0 - vz * 0.3 : 0.0;
				lvx[g] = lvx[g] * 0.55;
				lvy[g] = lvy[g] * 0.55;
			}
			lz[g] = nz;
			lvz[g] = vz;
			local nx = lx[g] + lvx[g] * dt;
			local ny = ly[g] + lvy[g] * dt;
			if (wm[ly[g].tointeger() * mcols + nx.tointeger()] != 0) { lvx[g] = 0.0 - lvx[g] * 0.4; nx = lx[g]; }
			if (wm[ny.tointeger() * mcols + lx[g].tointeger()] != 0) { lvy[g] = 0.0 - lvy[g] * 0.4; ny = ly[g]; }
			lx[g] = nx;
			ly[g] = ny;
		}
	}

	// the light map: the torches' steady sum, each cell flickering with the torch
	// that rules it, plus the muzzle near the camera while it burns
	function light_up() {
		local sin = ::sin;
		local n = lw * lh;
		local nt = tox.len();
		local flick = [];
		local kick = cue != null ? cue.kick_env(tt) : 0.0;
		// on the run the torches gutter low and the kick strobes them back up
		local run = tt >= ::MUSIC_BAR * 10.0 && tt < ::MUSIC_BAR * 12.0;
		local gutter = run ? 0.4 + kick * 1.1 : 1.0;
		local beat_flare = cue != null ? 0.45 * kick + 0.7 * cue.hit(tt) : 0.0;
		for (local k = 0; k < nt; ++k) {
			flick.push((1.0 + 0.12 * sin(tt * 13.0 + k * 2.3) + 0.10 * sin(tt * 29.0 + k * 5.1)) * gutter + beat_flare);
		}
		local st = still;
		local ow = owner;
		local lm = lmap;
		local lcols = lw;
		local lrows = lh;
		local flood = cue != null ? (cue.hit(tt) * 4.0).tointeger() : 0;
		for (local c = 0; c < n; ++c) {
			local lv = (st[c] * flick[ow[c]]).tointeger() + flood;
			lm[c] = lv > 5 ? 5 : lv;
		}
		// every burst lights the hall around it while the goo flies
		for (local b = 0; b < ne; ++b) {
			local sb = es[b];
			if (sb != 1 && sb != 2) { continue; }
			local age = sb == 1 ? est[b] : 0.14 + est[b];
			local power = 5.0 * (1.0 - age / 0.7);
			if (power <= 0.0) { continue; }
			local bx0 = (ex[b] * 2.0).tointeger();
			local by0 = (ey[b] * 2.0).tointeger();
			for (local dy2 = -9; dy2 <= 9; ++dy2) {
				local cy2 = by0 + dy2;
				if (cy2 < 0 || cy2 >= lrows) { continue; }
				for (local dx2 = -9; dx2 <= 9; ++dx2) {
					local cx2 = bx0 + dx2;
					if (cx2 < 0 || cx2 >= lcols) { continue; }
					local dd = (dx2 * dx2 + dy2 * dy2) * 0.25;
					local c3 = cy2 * lcols + cx2;
					local lv3 = lm[c3] + (power / (1.0 + dd * 0.5)).tointeger();
					lm[c3] = lv3 > 5 ? 5 : lv3;
				}
			}
		}
		// daylight through the breach: a glow through its cracks, then a flood once it is open
		local bs = breach_state();
		if (bs > 0.0) {
			local power = bs >= 2.0 ? 6.0 : bs * (2.0 + kick * 2.0);
			local bx1 = breach_x * 2;
			local by1 = breach_y * 2 + 1;
			for (local dy3 = -12; dy3 <= 12; ++dy3) {
				local cy3 = by1 + dy3;
				if (cy3 < 0 || cy3 >= lrows) { continue; }
				for (local dx3 = -24; dx3 <= 0; ++dx3) {
					local cx3 = bx1 + dx3;
					if (cx3 < 0) { continue; }
					local dd3 = (dx3 * dx3 + dy3 * dy3 * 2.0) * 0.25;
					local c4 = cy3 * lcols + cx3;
					local lv4 = lm[c4] + (power / (1.0 + dd3 * 0.12)).tointeger();
					lm[c4] = lv4 > 5 ? 5 : lv4;
				}
			}
		}
		if (muzzle_t > 0.0) {
			local cx0 = (posx * 2.0).tointeger();
			local cy0 = (posy * 2.0).tointeger();
			for (local dy = -10; dy <= 10; ++dy) {
				local cy = cy0 + dy;
				if (cy < 0 || cy >= lrows) { continue; }
				for (local dx = -10; dx <= 10; ++dx) {
					local cx = cx0 + dx;
					if (cx < 0 || cx >= lcols) { continue; }
					local d2 = (dx * dx + dy * dy) * 0.25;
					local c2 = cy * lcols + cx;
					local lv2 = (st[c2] * flick[ow[c2]] + 4.5 / (1.0 + d2 * 0.35)).tointeger();
					lm[c2] = lv2 > 5 ? 5 : lv2;
				}
			}
		}
	}

	function light_at(x, y) {
		local cx = (x * 2.0).tointeger();
		local cy = (y * 2.0).tointeger();
		cx = cx < 0 ? 0 : (cx >= lw ? lw - 1 : cx);
		cy = cy < 0 ? 0 : (cy >= lh ? lh - 1 : cy);
		return lmap[cy * lw + cx];
	}

	// one ray per column: the wall's depth, the rows its slice covers, and what it hit;
	// each column is also filed under the rows where its ceiling ends and its floor begins
	function cast_walls() {
		local floor = ::floor;
		local ww = w;
		local hh = h;
		local wm = wmap;
		local zc = zcol;
		local vx = vigx;
		local top = wall_top;
		local foot = wall_foot;
		local hit = wall_hit;
		local clo = ceil_lo;
		local chi = ceil_hi;
		local flo = floor_lo;
		local fhi = floor_hi;
		local lm = lmap;
		local lcols = lw;
		local lrows = lh;
		local mcols = mw;
		local px = posx;
		local py = posy;
		local dx0 = dirx;
		local dy0 = diry;
		local plx = planex;
		local ply = planey;
		local hc = (hh / 2 + pitch.tointeger()).tofloat();
		local rise = vault - 0.5;
		for (local v = 0; v <= hh; ++v) {
			clo[v] = ww;
			chi[v] = -1;
			flo[v] = ww;
			fhi[v] = -1;
		}
		for (local x = 0; x < ww; ++x) {
			local camx = 2.0 * x / (ww - 1.0) - 1.0;
			local rdx = dx0 + plx * camx;
			local rdy = dy0 + ply * camx;
			local mx = px.tointeger();
			local my = py.tointeger();
			local adx = rdx < 0.0 ? -rdx : rdx;
			local ady = rdy < 0.0 ? -rdy : rdy;
			local ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
			local ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
			local stepx = rdx < 0.0 ? -1 : 1;
			local stepy = rdy < 0.0 ? -1 : 1;
			local sdx = rdx < 0.0 ? (px - mx) * ddx : (mx + 1.0 - px) * ddx;
			local sdy = rdy < 0.0 ? (py - my) * ddy : (my + 1.0 - py) * ddy;
			local side = 0;
			local hitv = 0;
			local guard = 0;
			while (hitv == 0 && guard < 64) {
				if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
				else { sdy += ddy; my += stepy; side = 1; }
				hitv = wm[my * mcols + mx];
				++guard;
			}
			hitv = hitv == 0 ? 1 : hitv;
			local pdist = side == 0 ? sdx - ddx : sdy - ddy;   // perpendicular: no fisheye
			pdist = pdist < 0.05 ? 0.05 : pdist;
			zc[x] = pdist;
			local wallx = side == 0 ? py + pdist * rdy : px + pdist * rdx;
			wallx -= floor(wallx);
			local tu = (wallx * 16.0).tointeger() & 15;
			local lineh = hh / pdist;
			local y0 = (hc - lineh * rise).tointeger();
			local y1 = (hc + lineh * 0.5).tointeger();
			y0 = y0 < 0 ? 0 : y0;
			y1 = y1 > hh ? hh : y1;
			top[x] = y0;
			foot[x] = y1;
			if (clo[y0] > x) { clo[y0] = x; }
			chi[y0] = x;
			if (flo[y1] > x) { flo[y1] = x; }
			fhi[y1] = x;
			// light: the gloom by distance, the torches at the face, the vignette
			local amb = (8.0 / (pdist + 1.8)).tointeger();
			amb = amb > 3 ? 3 : amb;
			local lcx = ((px + rdx * (pdist - 0.05)) * 2.0).tointeger();
			local lcy = ((py + rdy * (pdist - 0.05)) * 2.0).tointeger();
			lcx = lcx < 0 ? 0 : (lcx >= lcols ? lcols - 1 : lcx);
			lcy = lcy < 0 ? 0 : (lcy >= lrows ? lrows - 1 : lcy);
			local lv = amb + lm[lcy * lcols + lcx] - vx[x];
			lv = lv < 0 ? 0 : (lv > 7 ? 7 : lv);
			hit[x] = ((hitv * 2 + side) * 16 + tu) * 8 + lv;
		}
	}

	// floor and ceiling, cast per pixel only where no wall slice covers them: each
	// row off the horizon is a fixed distance, stepped along the camera plane across
	// the columns whose ceiling (or floor) reaches that row
	function cast_planes(grid) {
		local hh = h;
		local h0 = hh / 2 + pitch.tointeger();
		local clo = ceil_lo;
		local chi = ceil_hi;
		local flo = floor_lo;
		local fhi = floor_hi;
		local rise = vault - 0.5;
		local lo = clo[hh];
		local hi = chi[hh];
		for (local y = hh - 1; y >= 0; --y) {
			if (y < h0 && lo <= hi) {
				local up = h0 - y - 0.5;
				local ceil_d = rise * hh / up;
				// a texel is up / (16 * ceil_d) pixels tall: under two, its grain would only speckle
				cast_plane_row(grid, y, ceil_d, lo, hi, ceil_base, up < ceil_d * 32.0 ? 2304 : 1792);
			}
			lo = clo[y] < lo ? clo[y] : lo;
			hi = chi[y] > hi ? chi[y] : hi;
		}
		lo = w;
		hi = -1;
		for (local y2 = 0; y2 < hh; ++y2) {
			lo = flo[y2] < lo ? flo[y2] : lo;
			hi = fhi[y2] > hi ? fhi[y2] : hi;
			if (y2 >= h0 && lo <= hi) {
				local down = y2 - h0 + 0.5;
				local floor_d = 0.5 * hh / down;
				cast_plane_row(grid, y2, floor_d, lo, hi, floor_base, down < floor_d * 32.0 ? 2048 : 1536);
			}
		}
	}

	function cast_plane_row(grid, y, rowd, x0, x1, plane_base, plane_tex) {
		local ww = w;
		local tx = tex;
		local lm = lmap;
		local vx = vigx;
		local lcols = lw;
		local lrows = lh;
		local step_x = rowd * 2.0 * planex / ww;
		local step_y = rowd * 2.0 * planey / ww;
		local fx = posx + rowd * (dirx - planex) + step_x * x0;
		local fy = posy + rowd * (diry - planey) + step_y * x0;
		local amb = (8.0 / (rowd + 1.8)).tointeger();
		amb = (amb > 3 ? 3 : amb) - vigy[y];
		local gi = y * ww;
		local cell = -1;
		local lit = 0;
		for (local x = x0; x <= x1; ++x) {
			local iu = (fx * 16.0).tointeger();
			local iv = (fy * 16.0).tointeger();
			// the light changes only from one light-map cell to the next
			local lc = (iv >> 3) * 4096 + (iu >> 3);
			if (lc != cell) {
				cell = lc;
				local lcx = iu >> 3;
				local lcy = iv >> 3;
				lcx = lcx < 0 ? 0 : (lcx >= lcols ? lcols - 1 : lcx);
				lcy = lcy < 0 ? 0 : (lcy >= lrows ? lrows - 1 : lcy);
				lit = amb + lm[lcy * lcols + lcx];
			}
			local lv = lit - vx[x];
			lv = lv < 0 ? 0 : (lv > 7 ? 7 : lv);
			grid[gi + x] = plane_base + lv * 8 + tx[plane_tex + (iv & 15) * 16 + (iu & 15)];
			fx += step_x;
			fy += step_y;
		}
	}

	// one billboard: an ellipsoid body lit as a sphere per pixel, eyes that glow
	// in the gloom, a grin; walls occlude per column; hit-flash paints it white
	function draw_blob(grid, i) {
		local sqrt = ::sqrt;
		local ww = w;
		local hh = h;
		local zc = zcol;
		local tyy = sty[i];
		local txx = stx[i];
		local scx = (ww * 0.5 * (1.0 + txx / tyy)).tointeger();
		local typ = et[i];
		local boss = i == ne - 1;
		local size = boss ? 1.7 + (cue != null ? cue.kick_env(tt) * 0.22 : 0.0) : (typ == 1 ? 1.1 : 0.78);
		local sh = (hh * size / tyy).tointeger();
		sh = sh < 3 ? 3 : sh;
		sh = sh > hh ? hh : sh;
		// its half-width never passes its kind's reach in world units, whatever the pixels' shape
		local hwm = sh * (boss ? 0.57 : (typ == 1 ? 0.6 : 0.55));
		local room = (boss ? 1.2 : (typ == 1 ? 0.75 : 0.5)) * ww / (1.32 * tyy);
		hwm = hwm > room ? room : hwm;
		local feet = hh / 2 + pitch.tointeger() + (hh * 0.5 / tyy).tointeger();
		local top = feet - sh;
		local wob = ::sin(tt * 5.0 + i * 2.1) * 0.10;
		local flash = es[i] == 1 || (boss && boss_flash > 0.0);
		local sb = sprite_base;
		local body = sb + typ * 12;
		local fogk = 0.18 + light_at(ex[i], ey[i]) * 0.15 + 0.35 / (tyy + 0.6);
		fogk = fogk < 0.2 ? 0.2 : (fogk > 1.0 ? 1.0 : fogk);
		local glow = fogk < 0.55 || boss;
		for (local dy = 0; dy < sh; ++dy) {
			local rr = top + dy;
			if (rr < 0 || rr >= hh) { continue; }
			local v = (dy + 0.5) / sh * 2.0 - 1.0;
			local vv = 1.0 - v * v;
			if (vv <= 0.0) { continue; }
			local hwf = sqrt(vv) * hwm;
			local hw = hwf.tointeger();
			local cxr = scx + (wob * v * hwm).tointeger();
			local rowb = rr * ww;
			for (local dx = 0 - hw; dx <= hw; ++dx) {
				local col = cxr + dx;
				if (col < 0 || col >= ww) { continue; }
				if (tyy >= zc[col]) { continue; }
				local pi = sb + 24;
				if (!flash) {
					local nx = dx / (hwm + 0.5);
					local q = 1.0 - nx * nx - v * v;
					local nz = q > 0.0 ? sqrt(q) : 0.0;
					local li = -0.45 * nx - 0.55 * v + 0.70 * nz;
					li = li < 0.0 ? 0.0 : li;
					// a rim of torchlight on the silhouette's edge
					local rim = q < 0.12 && li < 0.3 ? 0.35 : 0.0;
					local lv = ((0.10 + 0.9 * li + rim) * fogk * 11.99).tointeger();
					pi = body + (lv > 11 ? 11 : lv);
					local ax = nx < 0.0 ? -nx : nx;
					if (v > -0.5 && v < -0.12 && ax > 0.14 && ax < 0.52) {
						local ey2 = (v + 0.31) * 5.0;
						local ex2 = (ax - 0.33) * 5.0;
						if (ex2 * ex2 + ey2 * ey2 < 1.0) {
							local core = ex2 * ex2 + ey2 * ey2 < 0.3;
							pi = core ? (glow ? sb + 48 : sb + 26) : (glow ? sb + 26 : sb + 25);
						}
					}
					if (v > 0.18 && v < 0.34 && ax < 0.42) {
						pi = (dx & 1) == 0 && v < 0.24 ? sb + 25 : sb + 45;
					}
				}
				grid[rowb + col] = pi;
			}
		}
	}

	// a torch's flame: a flickering teardrop, occluded by nearer walls
	function draw_flame(grid, k) {
		local invdet = 1.0 / (planex * diry - dirx * planey);
		local rx = tox[k] - posx;
		local ry = toy[k] - posy;
		local tyy = invdet * (planex * ry - planey * rx);
		if (tyy < 0.3) { return; }
		local txx = invdet * (diry * rx - dirx * ry);
		local ww = w;
		local hh = h;
		local zc = zcol;
		local scx = (ww * 0.5 * (1.0 + txx / tyy)).tointeger();
		local hc = (hh / 2 + pitch.tointeger()).tofloat();
		local fh = (hh * 0.24 / tyy).tointeger();
		fh = fh < 3 ? 3 : fh;
		local fy0 = (hc - hh * 0.14 / tyy).tointeger() - fh;
		local flick = ::sin(tt * 17.0 + k * 3.0);
		local sb = sprite_base;
		for (local dy = 0; dy < fh; ++dy) {
			local v = (dy + 0.5) / fh;
			local half = (v < 0.65 ? v / 0.65 : 1.0 - (v - 0.65) / 0.35 * 0.4) * fh * 0.28;
			local sway = (flick * (1.0 - v) * fh * 0.12).tointeger();
			local hw = half.tointeger();
			local row = fy0 + dy;
			if (row < 0 || row >= hh) { continue; }
			for (local dx = 0 - hw; dx <= hw; ++dx) {
				local col = scx + dx + sway;
				if (col < 0 || col >= ww || tyy >= zc[col]) { continue; }
				// ax is a float slot in the original, so ax / hw divides as floats
				local ax = (dx < 0 ? 0 - dx : dx).tofloat();
				local e = hw > 0 ? ax / hw : 0.0;
				local c = e < 0.4 && v > 0.45 ? 31 : (e < 0.75 ? 32 : 33);
				grid[row * ww + col] = sb + c;
			}
		}
	}

	function render(grid) {
		cast_walls();
		cast_planes(grid);
		local ww = w;
		local hh = h;
		local tx = tex;
		local zc = zcol;
		local top = wall_top;
		local foot = wall_foot;
		local hit = wall_hit;
		local wb = wall_base;
		local bb = banner_base;
		local tshift = (tt * 14.0).tointeger();
		local hc = (hh / 2 + pitch.tointeger()).tofloat();
		local rise = vault - 0.5;
		for (local x = 0; x < ww; ++x) {
			local y0 = top[x];
			local y1 = foot[x];
			local packed = hit[x];
			local lv = packed & 7;
			local tu = (packed >> 3) & 15;
			local side = (packed >> 7) & 1;
			local hitv = packed >> 8;
			local lineh = hh / zc[x];
			local tstep = 16.0 / lineh;
			local tpos = (y0 + 0.5 - (hc - lineh * rise)) * tstep;
			local gi = y0 * ww + x;
			if (hitv == 5) {
				breach_slice(grid, x, y0, y1, tpos, tstep, tu, lv, side);
			} else if (hitv == 4) {
				local brow = bb + (side * 8 + lv) * 16;
				// rainbow fire: ragged flame tongues per texel column, hue bands scroll up the cloth
				local wave = (tu * 3 + tshift) % 12;
				local flen = 12 + (wave < 6 ? wave : 12 - wave) * 2;
				local bank = (((tu >> 2) + (tshift >> 3)) & 3) * 4;
				for (local y = y0; y < y1; ++y) {
					local tv = tpos.tointeger();
					local up = 31 - (tv & 31);
					local heat = up > flen ? 0 : (up * 3 > flen * 2 ? 1 : (up * 3 > flen ? 2 : 3));
					grid[gi] = brow + bank + heat;
					tpos += tstep;
					gi += ww;
				}
			} else {
				local wrow = wb + (((hitv - 1) * 2 + side) * 8 + lv) * 8;
				local tbase = (hitv - 1) * 512 + tu;
				for (local y2 = y0; y2 < y1; ++y2) {
					local tv2 = tpos.tointeger();
					grid[gi] = wrow + tx[tbase + (tv2 & 31) * 16];
					tpos += tstep;
					gi += ww;
				}
			}
		}
		for (local k = 0; k < tox.len(); ++k) { draw_flame(grid, k); }
		// sprite pass: camera-space transforms, then paint far to near
		local invdet = 1.0 / (planex * diry - dirx * planey);
		for (local i = 0; i < ne; ++i) {
			local rx = ex[i] - posx;
			local ry = ey[i] - posy;
			stx[i] = invdet * (diry * rx - dirx * ry);
			sty[i] = invdet * (planex * ry - planey * rx);
			drawn[i] = 0;
		}
		for (local pass = 0; pass < ne; ++pass) {
			local pick = -1;
			local pty = -1.0;
			for (local i2 = 0; i2 < ne; ++i2) {
				if (drawn[i2] == 1) { continue; }
				local st = es[i2];
				if (st >= 2) { continue; }
				local sv = sty[i2];
				if (sv <= 0.25) { continue; }
				if (sv > pty) { pty = sv; pick = i2; }
			}
			if (pick < 0) { break; }
			drawn[pick] = 1;
			draw_blob(grid, pick);
		}
		draw_goo(grid, invdet);
		daylight(grid);
		draw_hud(grid);
	}

	// out through the breach: daylight blooms from the heart of the frame, its edge dithered gold
	function daylight(grid) {
		if (tt < ::MUSIC_BAR * 12.0) { return; }
		local sqrt = ::sqrt;
		local bayer = ::BAYER4;
		local k = (tt - ::MUSIC_BAR * 12.0) / ::MUSIC_BAR;
		k = k > 1.0 ? 1.0 : k;
		// the blast whites out the frame for a moment, then the opening blooms
		local r = k < 0.06 ? w * 1.2 : (0.12 + k * k * 0.6) * w;
		local band = 7.0;
		local cx = w * 0.5;
		local cy = h * 0.5 + pitch;
		local sb = sprite_base;
		for (local y = 0; y < h; ++y) {
			local dy = (y + 0.5 - cy) * 1.5;
			local oo = r * r - dy * dy;
			if (oo <= 0.0) { continue; }
			// per row: the white heart, the warm white ring, the dithered gold edge
			local xo = sqrt(oo);
			local ri = r - band;
			local ii = ri * ri - dy * dy;
			local xi = ii > 0.0 ? sqrt(ii) : 0.0;
			local hh = r * r * 0.36 - dy * dy;
			local xh = hh > 0.0 ? sqrt(hh) : 0.0;
			local row = y * w;
			local x0 = (cx - xo).tointeger();
			local x1 = (cx + xo).tointeger() + 1;
			x0 = x0 < 0 ? 0 : x0;
			x1 = x1 > w ? w : x1;
			for (local x = x0; x < x1; ++x) {
				local dx = x + 0.5 - cx;
				local ax = dx < 0.0 ? -dx : dx;
				if (ax < xh) {
					grid[row + x] = sb + 24;
				} else if (ax < xi) {
					grid[row + x] = sb + 31;
				} else {
					local over = (r - sqrt(dx * dx + dy * dy)) / band;
					if (over * 16.0 > bayer[(y & 3) * 4 + (x & 3)]) { grid[row + x] = sb + 32; }
				}
			}
		}
	}

	// the breach: stone whose cracks glow hotter through the escape, then open daylight
	function breach_slice(grid, x, y0, y1, tpos, tstep, tu, lv, side) {
		local bs = breach_state();
		local sb = sprite_base;
		local ww = w;
		local gi = y0 * ww + x;
		if (bs >= 2.0) {
			// open: white at the heart of the opening, gold toward its edges, rays rippling up it
			local shimmer = (tt * 9.0).tointeger();
			for (local y = y0; y < y1; ++y) {
				local tv = tpos.tointeger();
				local ev = (tv < 16 ? tv : 31 - tv) >> 1;
				local eu = tu < 8 ? tu : 15 - tu;
				local edge = ev < eu ? ev : eu;
				local ray = ((tu * 5 + (tv >> 1) + shimmer) & 7) == 0 ? 1 : 0;
				grid[gi] = edge > 3 ? sb + 24 : (edge > 1 - ray ? sb + 31 : sb + 32);
				tpos += tstep;
				gi += ww;
			}
			return;
		}
		local kick = cue != null ? cue.kick_env(tt) : 0.0;
		local heat = ((bs + kick * 0.35) * 14.0).tointeger();
		local wrow = wall_base + (side * 8 + lv) * 8;
		local tx = tex;
		for (local y2 = y0; y2 < y1; ++y2) {
			local tv2 = tpos.tointeger();
			local ti = (tv2 & 31) * 16 + tu;
			local crack = (hash2(ti, 77) & 15);
			local c = tx[ti];
			// mortar lines and a hashed web of cracks catch fire from the light behind
			local pix = wrow + c;
			if ((c == 0 && crack < heat) || crack + 6 < heat - 8) { pix = crack < heat - 6 ? sb + 31 : (crack < heat - 3 ? sb + 32 : sb + 33); }
			grid[gi] = pix;
			tpos += tstep;
			gi += ww;
		}
	}

	// the goo in flight and on the floor: blobs sized by distance, lit by the map
	function draw_goo(grid, invdet) {
		local ww = w;
		local hh = h;
		local zc = zcol;
		local hc = (hh / 2 + pitch.tointeger()).tofloat();
		local px = posx;
		local py = posy;
		local dx0 = dirx;
		local dy0 = diry;
		local plx = planex;
		local ply = planey;
		local lf = glife;
		local lx = gx;
		local ly = gy;
		local lz = gz;
		local lt = gtype;
		local sb = sprite_base;
		for (local g = 0; g < ngoo; ++g) {
			local life = lf[g];
			if (life <= 0.0) { continue; }
			local rx = lx[g] - px;
			local ry = ly[g] - py;
			local tyy = invdet * (plx * ry - ply * rx);
			if (tyy < 0.3) { continue; }
			local txx = invdet * (dy0 * rx - dx0 * ry);
			local scx = (ww * 0.5 * (1.0 + txx / tyy)).tointeger();
			if (scx < 0 || scx >= ww || tyy >= zc[scx]) { continue; }
			local scy = (hc + (0.5 - lz[g]) * hh / tyy).tointeger();
			local rad = (hh * (lt[g] == 2 ? 0.024 : 0.04) / tyy * (life < 0.6 ? life / 0.6 : 1.0)).tointeger();
			local flat = lz[g] <= 0.0 ? 1 : 0;
			local lk = 0.3 + light_at(lx[g], ly[g]) * 0.14;
			// the original's `base` is a Squirrel keyword
			local kind_base = lt[g] == 2 ? rubble_base : sb + lt[g] * 12;
			local body = kind_base + (lk * 10.0).tointeger();
			body = body > kind_base + 11 ? kind_base + 11 : body;
			for (local dy = 0 - rad; dy <= rad; ++dy) {
				local row = scy + (flat == 1 ? dy / 2 : dy);
				if (row < 0 || row >= hh) { continue; }
				for (local dx = 0 - rad - flat * rad; dx <= rad + flat * rad; ++dx) {
					local col = scx + dx;
					if (col < 0 || col >= ww || tyy >= zc[col]) { continue; }
					if (dx * dx + dy * dy * (flat == 1 ? 4 : 1) > (rad + 1) * (rad + 1) * (flat == 1 ? 4 : 1)) { continue; }
					grid[row * ww + col] = dy < 0 && dx < 0 && flat == 0 && lt[g] != 2 ? sb + 52 : body;
				}
			}
		}
	}

	function draw_hud(grid) {
		local gx0 = w / 2;
		local gs = h / 40;
		gs = gs < 2 ? 2 : gs;
		local flick = (tt * 10.0).tointeger();
		// rainbow hitscan tracer from the muzzle to the target
		if (tracer_t > 0.0) {
			local y0t = h - 11 * gs;
			local steps = 14 * gs;
			for (local k2 = 1; k2 < steps; ++k2) {
				local xx = gx0 + (tr_x - gx0) * k2 / steps;
				local yy = y0t + (tr_y - y0t) * k2 / steps;
				for (local th = 0; th < gs / 2; ++th) {
					if (xx + th < 0 || xx + th >= w || yy < 1 || yy >= h) { continue; }
					grid[yy * w + xx + th] = sprite_base + 34 + (k2 / gs + flick) % 6;
				}
			}
		}
		// the gun, drawn at gs x: bobs with the walk, kicks on the shot
		local bob = (::sin(travel * 5.2) * 1.25 * gs).tointeger();
		local kick = muzzle_t > 0.0 ? 2 * gs : 0;
		local gw = 16;
		local gh = gun.len() / gw;
		local gn = gun;
		local sb = sprite_base;
		local ww = w;
		local hh = h;
		local gxs = gx0 - gw * gs / 2 + bob;
		local gys = hh - gh * gs + 2 * gs + kick + (bob < 0 ? 0 - bob : bob) / 2;
		for (local ry = 0; ry < gh * gs; ++ry) {
			local grow = (ry / gs) * gw;
			local yy2 = gys + ry;
			if (yy2 < 0 || yy2 >= hh) { continue; }
			for (local rx = 0; rx < gw * gs; ++rx) {
				local c = gn[grow + rx / gs];
				if (c == 0) { continue; }
				local xx2 = gxs + rx;
				if (xx2 >= 0 && xx2 < ww) { grid[yy2 * ww + xx2] = sb + c; }
			}
		}
		if (muzzle_t > 0.0) {
			local mx = gxs + gw * gs / 2 - 1;
			local my = gys - 2 * gs;
			local ray = 3 * gs;
			for (local q = 0 - ray; q <= ray; ++q) {
				local aq = q < 0 ? 0 - q : q;
				local c2 = aq * 5 <= ray ? 31 : (aq * 5 <= ray * 3 ? 32 : 33);
				for (local th2 = 0; th2 < gs; ++th2) {
					if (my + q >= 0 && my + q < h && mx + th2 < w) { grid[(my + q) * w + mx + th2] = sprite_base + c2; }
					if (mx + q >= 0 && mx + q < w && my + th2 >= 0 && my + th2 < h) { grid[(my + th2) * w + mx + q] = sprite_base + c2; }
				}
				if (aq * 5 <= ray * 3 && mx + q >= 0 && mx + q < w && my + q >= 0 && my + q < h) { grid[(my + q) * w + mx + q] = sprite_base + 32; }
				if (aq * 5 <= ray * 3 && mx - q >= 0 && mx - q < w && my + q >= 0 && my + q < h) { grid[(my + q) * w + mx - q] = sprite_base + 32; }
			}
		}
		local chc = hot_t > 0.0 ? sprite_base + 41 : sprite_base + 40;
		local cy = (h * 0.5).tointeger();
		local cr = gs / 2;
		for (local d = cr + 1; d <= cr * 2 + 2; ++d) {
			grid[cy * w + gx0 - d] = chc;
			grid[cy * w + gx0 + d] = chc;
			grid[(cy - d) * w + gx0] = chc;
			grid[(cy + d) * w + gx0] = chc;
		}
		local ts = h / 80;
		ts = ts < 1 ? 1 : ts;
		local tally = "KILLS " + (kills < 10 ? "0" : "") + kills;
		::gfx.draw_text(grid, w, h, tally, 2, 2, ts, ts, sprite_base + 42, 2, sprite_base + 44, 99);
		// the callout: it punches in large and settles, rising as it fades
		if (call_t > 0.0 && callout.len() > 0) {
			local age = 0.9 - call_t;
			local cs = w * 3 / 5 / (callout.len() * 6);
			cs = cs < 1 ? 1 : (cs > 3 * ts ? 3 * ts : cs);
			cs += age < 0.08 ? 1 : 0;
			local tw = callout.len() * 6 * cs;
			local cx = (w - tw) / 2;
			local cyy = (h * 0.22 - age * h * 0.08).tointeger();
			local ink = combo >= 2 ? sprite_base + 53 + (flick & 3) : sprite_base + 49 + (flick & 1) * 2;
			if (cx > 0) { ::gfx.draw_text(grid, w, h, callout, cx, cyy, cs, cs, ink, 1, sprite_base + 44, 99); }
		}
	}
}

::SCENES[6] <- ::GlobHunter;
