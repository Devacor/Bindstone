// Scene interface + the Reel manager: timed auto-advance, transitions, title
// cards, HUD. Everything the host calls funnels through Reel.frame().
// (Scene construction stays an if-chain; the free-call/self-scope bug that forced
// it is FIXED — see util.jai.)

class Scene {
	string name = "SCENE";
	string handle = "";
	int w = 0;
	int h = 0;
	int seed = 0;
	var pal = null;
	var rng = null;

	void setup(int w_, int h_, int seed_) {
		w = w_;
		h = h_;
		seed = seed_;
		rng = Rng(seed_);
		pal = Palette();
		build();
	}

	void build() {}
	void update(float t, float dt) {}
	void render(var& grid) {}
}

class Reel {
	int w = 0;
	int h = 0;
	int seed = 0;
	bool smoke = false;
	string backend = "";
	bool hud = true;
	bool paused = false;

	int nscenes = 13;
	var durs = [];
	var cur = null;
	var nxt = null;
	int cur_idx = 0;
	int pending_idx = 0;
	float scene_t = 0.0;
	float dur_cur = 10.0;
	float title_len = 2.4;
	float trans_len = 1.1;
	bool in_trans = false;
	bool finish_pending = false;
	float trans_t = 0.0;
	int trans_kind = 0;

	var grid = null;
	var grid_b = null;
	var grid_m = null;
	var parts = [];

	Reel(int w_, int h_, int seed_, string backend_, bool smoke_, bool hud_) {
		w = w_;
		h = h_;
		seed = seed_;
		backend = backend_;
		smoke = smoke_;
		hud = hud_;
		grid = gfx.make_grid(w * h);
		grid_b = gfx.make_grid(w * h);
		grid_m = gfx.make_grid(w * h);
		durs.push(10.0);   // plasma
		durs.push(10.0);   // starfield
		durs.push(11.0);   // donut
		durs.push(11.0);   // julia
		durs.push(14.0);   // pipedream (the breather)
		durs.push(14.0);   // powder
		durs.push(12.0);   // boids
		durs.push(14.0);   // kinstein 3d
		durs.push(15.0);   // canyonrun
		durs.push(16.0);   // spongeworks
		durs.push(16.5);   // bannerfall
		durs.push(13.0);   // fire
		durs.push(17.0);   // finale
		start_scene(0, 0.0);
	}

	var make_scene(int idx) {
		if (idx == 0) { return Plasma(); }
		if (idx == 1) { return Starfield(); }
		if (idx == 2) { return Donut(); }
		if (idx == 3) { return Julia(); }
		if (idx == 4) { return Pipes(); }
		if (idx == 5) { return SandBox(); }
		if (idx == 6) { return Boids(); }
		if (idx == 7) { return Kinstein(); }
		if (idx == 8) { return CanyonRun(); }
		if (idx == 9) { return SpongeWorks(); }
		if (idx == 10) { return BannerFall(); }
		if (idx == 11) { return Fire(); }
		return Finale();
	}

	float dur_for(int idx) {
		return smoke ? 3.0 : durs[idx];
	}

	void start_scene(int idx, float at_t) {
		cur_idx = ((idx % nscenes) + nscenes) % nscenes;
		cur = make_scene(cur_idx);   // retired scene destroyed here (finding 7 FIXED: no graveyard needed)
		cur.setup(w, h, seed + cur_idx * 7919);
		dur_cur = dur_for(cur_idx);
		scene_t = at_t;
		in_trans = false;
		finish_pending = false;
		nxt = null;
		trans_t = 0.0;
	}

	// runs at the top of the frame AFTER the one that finished the transition
	// (the VM slot bug that once corrupted frame()'s locals here is FIXED).
	void complete_transition() {
		finish_pending = false;
		in_trans = false;
		cur = nxt;   // outgoing scene destroyed here (finding 7 FIXED)
		nxt = null;
		cur_idx = pending_idx;
		dur_cur = dur_for(cur_idx);
		scene_t = trans_t;
		trans_t = 0.0;
	}

	void begin_transition() {
		pending_idx = (cur_idx + 1) % nscenes;
		nxt = make_scene(pending_idx);
		nxt.setup(w, h, seed + pending_idx * 7919);
		in_trans = true;
		trans_t = 0.0;
		trans_kind = (cur_idx + seed) % 3;
	}

	// backend swap lands here: same scene, same clock; stateful scenes replay
	// up to 3 sim-seconds so they arrive mid-flight instead of empty.
	void jump(int idx, float at_t) {
		start_scene(idx, at_t);
		int steps = itrunc(at_t * 30.0);
		steps = steps > 90 ? 90 : steps;
		float ft = at_t - steps * 0.0333333333;
		for (int i = 0; i < steps; ++i) {
			ft += 0.0333333333;
			cur.update(ft, 0.0333333333);
		}
	}

	// after the host re-executes scene files, rebuild the live scene so
	// build()-time changes (palettes, tables, layouts) show immediately
	void reload_current() {
		float keep_t = scene_t;
		start_scene(cur_idx, keep_t);
	}

	void handle_key(string key) {
		if (key == "") { return; }
		if (key == "h") { hud = !hud; }
		else if (key == "f") { paused = !paused; }
		else if (key == "n" || key == " " || key == "right") { start_scene(cur_idx + 1, 0.0); }
		else if (key == "p" || key == "left") { start_scene(cur_idx - 1, 0.0); }
		else if (key.size() == 1) {
			char c0 = key[0];
			if (c0 >= '1' && c0 <= '9') {
				int d = key.to_int();
				if (d <= nscenes) { start_scene(d - 1, 0.0); }
			}
			else if (c0 == '0') { start_scene(nscenes - 1, 0.0); }
		}
	}

	void title_overlay(var& rows) {
		if (scene_t > title_len - 0.5) {
			int blink = itrunc(scene_t * 10.0);
			if (blink % 2 == 0) { return; }
		}
		var spaced_parts = [];
		for (int i = 0; i < cur.name.size(); ++i) {
			spaced_parts.push(cur.name.substr(i, 1));
		}
		string spaced = spaced_parts.join(" ");
		int mid = h / 2;
		string band = ESC + "[48;2;16;12;40m" + ESC + "[38;2;255;226;140m";
		string dim = ESC + "[48;2;16;12;40m" + ESC + "[38;2;150;160;220m";
		gfx.overlay_center(rows, w, mid - 2, "", band);
		gfx.overlay_center(rows, w, mid - 1, ":: " + spaced + " ::", band);
		gfx.overlay_center(rows, w, mid, cur.handle, dim);
		gfx.overlay_center(rows, w, mid + 1, "", band);
	}

	// ms_sim = script update + frame string; ms_draw = host console write.
	// The headline number is their SUM — the honest wall cost of a frame.
	void hud_overlay(var& rows, float fps, float ms_sim, float ms_draw, float ms_other) {
		string other = backend == "vm" ? "interp" : "vm";
		string ms_o = "";
		if (ms_other > 0.0) { ms_o = " (" + other + " " + gfx.fmt1(ms_other) + ")"; }
		string flow = paused ? " [FROZEN]" : "";
		string txt = " JAISCRIPT DEMOREEL " + (cur_idx + 1) + "/" + nscenes + " " + cur.name +
			" | " + backend + " " + gfx.fmt1(ms_sim + ms_draw) + "ms = sim " + gfx.fmt1(ms_sim) +
			" + draw " + gfx.fmt1(ms_draw) + ms_o +
			" | " + gfx.fmt1(fps) + " fps" + flow +
			" | tab r h f n p q";
		gfx.overlay_left(rows, w, 0, txt, ESC + "[48;2;24;16;48m" + ESC + "[38;2;235;235;255m");
	}

	string frame(float t, float dt, string key, float fps, float ms_sim, float ms_draw, float ms_other) {
		if (finish_pending) { complete_transition(); }
		handle_key(key);
		float step = paused ? 0.0 : dt;
		scene_t += step;
		if (!in_trans && scene_t >= dur_cur) { begin_transition(); }
		cur.update(scene_t, step);
		cur.render(grid);
		var rows = null;
		if (in_trans) {
			trans_t += step;
			nxt.update(trans_t, step);
			nxt.render(grid_b);
			float prog = trans_t / trans_len;
			prog = prog > 1.0 ? 1.0 : prog;
			var mp = Palette();
			int off = mp.absorb(cur.pal);
			mp.absorb(nxt.pal);
			gfx.mix_grids(grid, grid_b, grid_m, w, h, trans_kind, prog, off);
			rows = gfx.render_rows(grid_m, mp, w, h, parts);
			if (trans_t >= trans_len) { finish_pending = true; }
		} else {
			rows = gfx.render_rows(grid, cur.pal, w, h, parts);
		}
		if (!in_trans && !finish_pending && scene_t < title_len) { title_overlay(rows); }
		if (hud) { hud_overlay(rows, fps, ms_sim, ms_draw, ms_other); }
		return rows.join("\n");
	}
}
