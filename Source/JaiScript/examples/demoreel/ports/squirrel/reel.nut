// Scene interface + the Reel manager (Squirrel port of scenes/reel.jai).
// Scenes see h as PIXEL rows (two per terminal row) and paint square pixels.

class Scene {
	name = "SCENE";
	handle = "";
	w = 0;
	h = 0;
	seed = 0;
	pal = null;
	rng = null;

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
	function caption() { return ""; }
}

// porting aid (--only I): an empty scene carrying the real one's name
class StubScene extends Scene {
	constructor(nm) { name = nm; }
	function build() { pal.add(0, 0, 0); }
	function render(grid) {
		local n = w * h;
		for (local i = 0; i < n; ++i) { grid[i] = 0; }
	}
}

::SCENE_CLASSES <- null;
::SCENE_NAMES <- ["PLASMA", "STARFIELD", "TUNNEL", "DONUT", "JULIA", "LAVALAMP", "PIPEDREAM", "POWDER",
	"BOIDS", "KINSTEIN 3D", "CANYONRUN", "SPONGEWORKS", "BANNERFALL", "CHROME", "FIRE", "OSSUARY", "FINALE"];

class Reel {
	w = 0;
	h = 0;
	ph = 0;
	seed = 0;
	smoke = false;
	backend = "";
	hud = true;
	paused = false;

	nscenes = 17;
	durs = null;
	cur = null;
	nxt = null;
	cur_idx = 0;
	pending_idx = 0;
	scene_t = 0.0;
	dur_cur = 10.0;
	title_len = 2.4;
	trans_len = 1.1;
	in_trans = false;
	finish_pending = false;
	trans_t = 0.0;
	trans_kind = 0;
	trans_count = 0;

	grid_a = null;
	grid_b = null;
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
		w = w_;
		h = h_;
		ph = h_ * 2;
		seed = seed_;
		backend = backend_;
		smoke = smoke_;
		hud = hud_;
		grid_a = ::gfx.make_grid(w * ph);
		grid_b = ::gfx.make_grid(w * ph);
		melt = ::gfx.make_fgrid(w);
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
		durs = [12.0, 11.0, 12.0, 11.0, 11.0, 12.0, 14.0, 14.0, 12.0, 15.0, 15.0, 16.0, 16.5, 12.0, 11.0, 20.0, 18.0];
		start_scene(0, 0.0);
	}

	function make_scene(idx) {
		if (::SQ_ONLY_SCENE >= 0 && idx != ::SQ_ONLY_SCENE) { return ::StubScene(::SCENE_NAMES[idx]); }
		if (idx == 0) { return ::Plasma(); }
		if (idx == 1) { return ::Starfield(); }
		if (idx == 2) { return ::Tunnel(); }
		if (idx == 3) { return ::Donut(); }
		if (idx == 4) { return ::Julia(); }
		if (idx == 5) { return ::Lava(); }
		if (idx == 6) { return ::Pipes(); }
		if (idx == 7) { return ::SandBox(); }
		if (idx == 8) { return ::Boids(); }
		if (idx == 9) { return ::Kinstein(); }
		if (idx == 10) { return ::CanyonRun(); }
		if (idx == 11) { return ::SpongeWorks(); }
		if (idx == 12) { return ::BannerFall(); }
		if (idx == 13) { return ::Chrome(); }
		if (idx == 14) { return ::Fire(); }
		if (idx == 15) { return ::Ossuary(); }
		return ::Finale();
	}

	function dur_for(idx) {
		return smoke ? 3.0 : durs[idx];
	}

	// the presenter's string tables: current scene, incoming scene (during a
	// transition), then the reel's own UI entries
	function bind_palettes() {
		::hb_ensure(w, h);
		::HB_FG = [];
		::HB_BG = [];
		::HB_RGB = [];
		absorb(cur.pal);
		off_b = ::HB_RGB.len();
		if (in_trans) { absorb(nxt.pal); }
		ui_off = ::HB_RGB.len();
		absorb(ui);
	}

	function absorb(p) {
		local n = p.rgb.len();
		for (local i = 0; i < n; ++i) {
			::HB_FG.push(p.fg[i]);
			::HB_BG.push(p.bg[i]);
			::HB_RGB.push(p.rgb[i]);
		}
	}

	function start_scene(idx, at_t) {
		cur_idx = ((idx % nscenes) + nscenes) % nscenes;
		cur = make_scene(cur_idx);
		cur.setup(w, ph, seed + cur_idx * 7919);
		dur_cur = dur_for(cur_idx);
		scene_t = at_t.tofloat();
		in_trans = false;
		finish_pending = false;
		nxt = null;
		trans_t = 0.0;
		bind_palettes();
	}

	// runs at the top of the frame AFTER the one that finished the transition
	function complete_transition() {
		finish_pending = false;
		in_trans = false;
		cur = nxt;
		nxt = null;
		cur_idx = pending_idx;
		dur_cur = dur_for(cur_idx);
		scene_t = trans_t;
		trans_t = 0.0;
		bind_palettes();
	}

	function begin_transition() {
		pending_idx = (cur_idx + 1) % nscenes;
		nxt = make_scene(pending_idx);
		nxt.setup(w, ph, seed + pending_idx * 7919);
		in_trans = true;
		trans_t = 0.0;
		trans_kind = (trans_count + seed) % 5;
		trans_count = trans_count + 1;
		local m = 0;
		for (local x = 0; x < w; ++x) {
			m = m + ((x * 7 + trans_count * 13 + (x * x) % 11) % 3) - 1;
			m = m < 0 ? 0 : (m > 14 ? 14 : m);
			melt[x] = m * 1.0;
		}
		bind_palettes();
	}

	// backend swap lands here: same scene, same clock; stateful scenes replay
	// up to 3 sim-seconds so they arrive mid-flight instead of empty.
	function jump(idx, at_t) {
		at_t = at_t.tofloat();
		start_scene(idx, at_t);
		local steps = (at_t * 30.0).tointeger();
		steps = steps > 90 ? 90 : steps;
		local ft = at_t - steps * 0.0333333333;
		for (local i = 0; i < steps; ++i) {
			ft += 0.0333333333;
			cur.update(ft, 0.0333333333);
		}
	}

	function reload_current() {
		local keep_t = scene_t;
		start_scene(cur_idx, keep_t);
	}

	function handle_key(key) {
		if (key == "") { return; }
		if (key == "h") { hud = !hud; }
		else if (key == "f") { paused = !paused; }
		else if (key == "n" || key == " " || key == "right") { start_scene(cur_idx + 1, 0.0); }
		else if (key == "p" || key == "left") { start_scene(cur_idx - 1, 0.0); }
		else if (key.len() == 1) {
			local c0 = key[0];
			if (c0 >= '1' && c0 <= '9') {
				local d = key.tointeger();
				if (d <= nscenes) { start_scene(d - 1, 0.0); }
			}
			else if (c0 == '0') { start_scene(nscenes - 1, 0.0); }
		}
	}

	// the title card: a band opens from the screen's middle, the scene name
	// types itself in big pixel letters (a shine sweeps across them), the
	// handle sits under it as crisp text, and the band folds shut again
	function title_pixels() {
		local open = scene_t / 0.28;
		local close = (title_len - scene_t) / 0.3;
		local k = open < close ? open : close;
		k = k > 1.0 ? 1.0 : k;
		if (k <= 0.0) { return -1; }
		local y0 = ((ph / 2 - 12) / 2) * 2;
		local mid = y0 + 12;
		local reach = (k * 12.0 + 0.5).tointeger();
		local pix = ::HB_PIX;
		local band = ui_off + ui_band;
		for (local y = mid - reach; y < mid + reach; ++y) {
			if (y < 0 || y >= ph) { continue; }
			local ry = y - y0;
			local c = ry == 0 ? ui_off + ui_rule : (ry == 23 ? ui_off + ui_rule + 1 : band + ry / 2);
			local gi = y * w;
			for (local x = 0; x < w; ++x) { pix[gi + x] = c; }
		}
		if (k < 1.0) { return -1; }
		local name = cur.name;
		local sx = ::gfx.text_width(name, 2) <= w - 6 ? 2 : 1;
		local tw = ::gfx.text_width(name, sx);
		local tx = (w - tw) / 2;
		local ty = y0 + 3;
		local shown = ((scene_t - 0.28) * 34.0).tointeger() + 1;
		::gfx.draw_text(pix, w, ph, name, tx, ty, sx, 2, ui_off + ui_ink, 8, ui_off + ui_shadow, shown);
		// the shine: a diagonal stripe of white sweeping over the ink
		local sweep = ((scene_t - 0.6) * 90.0).tointeger() - 20;
		local ink0 = ui_off + ui_ink;
		for (local y2 = ty; y2 < ty + 14; ++y2) {
			local gi2 = y2 * w;
			for (local x2 = tx; x2 < tx + tw; ++x2) {
				local d = x2 + (y2 - ty) - sweep - tx;
				if (d >= 0 && d < 5) {
					local pv = pix[gi2 + x2];
					if (pv >= ink0 && pv < ink0 + 8) { pix[gi2 + x2] = ui_off + ui_shine; }
				}
			}
		}
		return (y0 + 20) / 2;
	}

	function title_text(rows, row) {
		if (row < 0) { return; }
		local c = ::HB_RGB[ui_off + ui_band + 10];
		local bgc = ::ESC + "[48;2;" + (c / 65536) + ";" + ((c / 256) % 256) + ";" + (c % 256) + "m";
		::gfx.overlay_center(rows, w, row, cur.handle, bgc + ::ESC + "[38;2;170;184;240m");
	}

	function hud_overlay(rows, fps, ms_sim, ms_draw, ms_other) {
		local ms_o = "";
		if (ms_other > 0.0) { ms_o = " (other " + ::gfx.fmt1(ms_other) + ")"; }
		local flow = paused ? " [FROZEN]" : "";
		local txt = " SQUIRREL DEMOREEL " + (cur_idx + 1) + "/" + nscenes + " " + cur.name +
			" | " + backend + " " + ::gfx.fmt1(ms_sim + ms_draw) + "ms = sim " + ::gfx.fmt1(ms_sim) +
			" + draw " + ::gfx.fmt1(ms_draw) + ms_o +
			" | " + ::gfx.fmt1(fps) + " fps" + flow +
			" | " + w + "x" + ph + " px | h f n p q";
		::gfx.overlay_left(rows, w, 0, txt, ::ESC + "[48;2;24;16;48m" + ::ESC + "[38;2;235;235;255m");
	}

	function frame(t, dt, key, fps, ms_sim, ms_draw, ms_other) {
		if (finish_pending) { complete_transition(); }
		handle_key(key);
		::hb_ensure(w, h);
		if (::HB_RGB.len() == 0) { bind_palettes(); }
		local step = paused ? 0.0 : dt;
		scene_t += step;
		if (!in_trans && scene_t >= dur_cur) { begin_transition(); }
		cur.update(scene_t, step);
		if (in_trans) {
			trans_t += step;
			cur.render(grid_a);
			nxt.update(trans_t, step);
			nxt.render(grid_b);
			local prog = trans_t / trans_len;
			prog = prog > 1.0 ? 1.0 : prog;
			::gfx.mix_grids(grid_a, grid_b, ::HB_PIX, w, ph, trans_kind, prog, off_b, ui_off + ui_edge, melt);
			if (trans_t >= trans_len) { finish_pending = true; }
		} else {
			cur.render(::HB_PIX);
		}
		local trow = -1;
		if (!in_trans && !finish_pending && scene_t < title_len) { trow = title_pixels(); }
		local rows = ::hb_present();
		title_text(rows, trow);
		if (!in_trans) {
			local cap = cur.caption();
			if (cap != "") { ::gfx.overlay_center(rows, w, h - 3, cap, ::ESC + "[48;2;6;4;12m" + ::ESC + "[38;2;255;226;160m"); }
		}
		if (hud) { hud_overlay(rows, fps, ms_sim, ms_draw, ms_other); }
		return ::join_arr(rows, "\n");
	}
}
