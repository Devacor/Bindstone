// Scene base + the perf path of the Reel (Squirrel port of scenes/reel.jai). Scenes see w and h in PIXELS
// and paint square pixels. The port starts mid-scene through jump and runs no transitions, title cards,
// HUD, keys or reload; scene files register their class in ::SCENES (contract in main.nut).

::SCENES <- {};

class Scene {
	name = "SCENE";
	handle = "";
	w = 0;
	h = 0;
	seed = 0;
	pal = null;
	rng = null;
	replay = 3.0;
	cue = null;
	pixel_budget = 0;
	titled = true;

	function setup(w_, h_, seed_) {
		w = w_;
		h = h_;
		seed = seed_;
		rng = ::Rng(seed_);
		pal = ::Palette();
		build();
	}

	function build() {}
	function update(t, dt) {}
	function render(grid) {}
	function text_rows(rows) {}
	function hit_times() { return []; }
	function hit_levels() { return []; }
	function length_bars() { return 0; }
}

class Reel {
	w = 0;
	h = 0;
	sext = false;
	pw = 0;
	ph = 0;
	seed = 0;
	smoke = false;
	backend = "";
	hud = true;

	nscenes = 12;
	intro_idx = 12;
	bars = null;
	cues = null;
	seed_slots = null;
	cur = null;
	cur_idx = 0;
	scene_t = 0.0;
	dur_cur = 10.0;
	title_len = 0.0;
	in_trans = false;

	grid_a = null;
	grid_b = null;
	flash_noise = null;
	ring = null;
	small_a = null;
	small_b = null;
	melt = null;
	ui = null;
	ui_off = 0;
	off_b = 0;
	ui_band = 0;
	ui_rule = 0;
	ui_ink = 0;
	ui_shine = 0;
	ui_shadow = 0;
	ui_edge = 0;

	constructor(w_, h_, seed_, backend_, smoke_, hud_) {
		title_len = ::MUSIC_BAR;
		flash_noise = [];
		ring = [];
		small_a = [];
		small_b = [];
		w = w_;
		h = h_;
		sext = ::PIX_MODE == "sext" || (::PIX_MODE == "" && ::hb_pixel_w(w_, true) * ::hb_pixel_h(h_, true) <= ::PIX_BUDGET);
		pw = ::hb_pixel_w(w_, sext);
		ph = ::hb_pixel_h(h_, sext);
		seed = seed_;
		backend = backend_;
		smoke = smoke_;
		hud = hud_;
		grid_a = ::gfx.make_grid(pw * ph);
		grid_b = ::gfx.make_grid(pw * ph);
		melt = ::gfx.make_fgrid(pw);
		ui = ::Palette();
		ui_band = ui.size();
		for (local b = 0; b < 12; ++b) {
			local f = b < 6 ? b / 5.0 : (11 - b) / 5.0;
			ui.add(10.0 + 14.0 * f, 8.0 + 8.0 * f, 30.0 + 26.0 * f);
		}
		ui_rule = ui.add(255, 206, 110);
		ui.add(110, 200, 255);
		ui_ink = ui.gradient(8, [[255.0, 250.0, 214.0], [255.0, 214.0, 110.0], [255.0, 128.0, 60.0], [214.0, 52.0, 92.0]]);
		ui_shine = ui.add(255, 255, 255);
		ui_shadow = ui.add(4, 2, 12);
		ui_edge = ui.add(255, 244, 200);
		bars = [32, 6, 6, 6, 7, 7, 8, 8, 6, 21, 8, 24, 4];
		seed_slots = [16, 3, 4, 5, 6, 8, 9, 10, 13, 15, 17, 23, 21];
		// each cue depends only on its own index and the seed, so the unported scenes' are skipped
		cues = ::array(intro_idx + 1, null);
		for (local i = 0; i <= intro_idx; ++i) {
			if (!(i in ::SCENES)) { continue; }
			local probe = make_scene(i);
			if (probe.length_bars() > 0) { bars[i] = probe.length_bars(); }
			cues[i] = ::Cue(bars[i], ::music_mood(i), seed + seed_slots[i] * 101, probe.hit_times(), probe.hit_levels());
		}
	}

	function make_scene(idx) {
		if (!(idx in ::SCENES)) { throw "scene " + idx + " is not ported"; }
		return ::SCENES[idx]();
	}

	function dur_for(idx) {
		return smoke ? 3.0 : bars[idx] * ::MUSIC_BAR;
	}

	// a scene at its grid, or at the largest grid within its pixel budget
	function setup_scene(sc, idx) {
		sc.cue = cues[idx];
		local sw = pw;
		local sh = ph;
		if (sc.pixel_budget > 0 && pw * ph > sc.pixel_budget) {
			local k = ::sqrt(sc.pixel_budget * 1.0 / (pw * ph));
			sw = (pw * k).tointeger();
			sh = (ph * k).tointeger();
		}
		sc.setup(sw, sh, seed + seed_slots[idx] * 7919);
	}

	// render into a grid of the reel's size, scaling up a scene that renders smaller; the scratch grid is
	// rebuilt in place because the reference rebinds the reel's own field through its var& parameter
	function render_scene(sc, target, scratch) {
		if (sc.w == pw && sc.h == ph) {
			sc.render(target);
			return;
		}
		if (scratch.len() != sc.w * sc.h) {
			scratch.resize(0);
			scratch.resize(sc.w * sc.h, 0);
		}
		sc.render(scratch);
		local sw = sc.w;
		local sh = sc.h;
		local lpw = pw;
		local lph = ph;
		local idx = 0;
		for (local y = 0; y < lph; ++y) {
			local row = (y * sh / lph) * sw;
			for (local x = 0; x < lpw; ++x) {
				target[idx] = scratch[row + x * sw / lpw];
				++idx;
			}
		}
	}

	// the presenter's string tables: the current scene, then the reel's own UI entries
	function bind_palettes() {
		::hb_ensure(w, h, sext);
		::HB_FG = [];
		::HB_BG = [];
		::HB_RGB = [];
		::HB_LUM = [];
		absorb(cur.pal);
		off_b = ::HB_RGB.len();
		ui_off = ::HB_RGB.len();
		absorb(ui);
	}

	function absorb(p) {
		local n = p.rgb.len();
		for (local i = 0; i < n; ++i) {
			::HB_FG.push(p.fg[i]);
			::HB_BG.push(p.bg[i]);
			local c = p.rgb[i];
			::HB_RGB.push(c);
			::HB_LUM.push((c >> 16) * 2 + ((c >> 8) & 255) * 3 + (c & 255));
		}
	}

	function start_scene(idx, at_t) {
		cur_idx = idx == intro_idx ? idx : ((idx % nscenes) + nscenes) % nscenes;
		cur = make_scene(cur_idx);
		setup_scene(cur, cur_idx);
		dur_cur = dur_for(cur_idx);
		scene_t = at_t;
		in_trans = false;
		bind_palettes();
	}

	// a jump to a scene mid-flight: stateful scenes replay up to their replay seconds
	function jump(idx, at_t) {
		start_scene(idx, at_t);
		local steps = (at_t * 30.0).tointeger();
		local most = (cur.replay * 30.0).tointeger();
		steps = steps > most ? most : steps;
		local ft = at_t - steps * 0.0333333333;
		for (local i = 0; i < steps; ++i) {
			ft += 0.0333333333;
			cur.update(ft, 0.0333333333);
		}
	}

	function ensure_noise() {
		local n = pw * ph;
		if (flash_noise.len() == n) { return; }
		local noise_rng = ::Rng(seed + 977);
		flash_noise = [];
		ring = [];
		for (local i = 0; i < n; ++i) {
			flash_noise.push(noise_rng.next(1000));
			local dx = (i % pw - pw * 0.5) / (pw * 0.5);
			local dy = (i / pw - ph * 0.5) / (ph * 0.5);
			ring.push(((dx * dx + dy * dy) * 500.0).tointeger());
		}
	}

	function dither_flash(keep) {
		ensure_noise();
		local pix = ::HB_PIX;
		local shine = ui_off + ui_shine;
		local noise = flash_noise;
		local n = pw * ph;
		for (local i = 0; i < n; ++i) {
			if (noise[i] < keep) { pix[i] = shine; }
		}
	}

	// every big hit lands on the whole frame: over the bar before it an iris closes in,
	// tightest through the score's beat of silence, then the hit throws it open and shakes the picture
	function drop_fx(sc, t) {
		if (sc.cue == null) { return; }
		local cue = sc.cue;
		local close = 0.0;
		local since = 9.0;
		for (local k = 0; k < cue.hit_t.len(); ++k) {
			if (cue.hit_level[k] < 2) { continue; }
			local to = cue.hit_t[k] - t;
			if (to > 0.0 && to <= ::MUSIC_BAR) {
				local c = 1.0 - to / ::MUSIC_BAR;
				c = to <= ::MUSIC_STEP * 4.0 ? 0.62 : c * c * 0.45;
				close = c > close ? c : close;
			}
			if (to <= 0.0 && 0.0 - to < since) { since = 0.0 - to; }
		}
		if (close > 0.0) {
			ensure_noise();
			local pix = ::HB_PIX;
			local noise = flash_noise;
			local rg = ring;
			local dark = ui_off + ui_shadow;
			// past the iris everything is dark, across a thin rim it dithers in
			local edge = ((1.04 - close) * 1000.0).tointeger();
			local rim = 70;
			local n = pw * ph;
			for (local i = 0; i < n; ++i) {
				local over = rg[i] - edge + rim;
				if (over > 0 && (over >= rim || noise[i] * rim < over * 1000)) { pix[i] = dark; }
			}
		}
		if (since < 0.32) {
			local amp = (0.32 - since) / 0.32 * (sext ? 4.0 : 2.5);
			local dx = (::sin(t * 97.0) * amp).tointeger();
			local dy = (::cos(t * 71.0) * amp * 0.6).tointeger();
			if (dx != 0 || dy != 0) { shift_frame(dx, dy); }
		}
	}

	// the frame moved by (dx, dy) pixels, its edges smeared in
	function shift_frame(dx, dy) {
		local pix = ::HB_PIX;
		local tmp = grid_b;
		local lpw = pw;
		local lph = ph;
		local n = lpw * lph;
		for (local i = 0; i < n; ++i) { tmp[i] = pix[i]; }
		local i2 = 0;
		for (local y = 0; y < lph; ++y) {
			local sy = y - dy;
			sy = sy < 0 ? 0 : (sy >= lph ? lph - 1 : sy);
			local row = sy * lpw;
			for (local x = 0; x < lpw; ++x) {
				local sx = x - dx;
				sx = sx < 0 ? 0 : (sx >= lpw ? lpw - 1 : sx);
				pix[i2] = tmp[row + sx];
				++i2;
			}
		}
	}

	function frame(t, dt, key, fps, ms_sim, ms_draw, ms_other) {
		if (cur == null) { throw "the port starts each scene with demo_jump: no scene is running"; }
		::hb_ensure(w, h, sext);
		::txt_clear();
		if (::HB_RGB.len() == 0) { bind_palettes(); }
		local step = dt;
		scene_t += step;
		if (!in_trans && scene_t >= dur_cur) { throw "the scene reached its end: the port starts mid-scene and runs no transitions"; }
		cur.update(scene_t, step);
		render_scene(cur, ::HB_PIX, small_a);
		drop_fx(cur, scene_t);
		if (scene_t < title_len && cur.titled) { throw "a title card is up: the port starts mid-scene and draws no title cards"; }
		local rows = ::hb_present();
		cur.text_rows(rows);
		return ::join_arr(rows, "\n");
	}
}
