// Game state + the fixed-timestep tick + the deterministic autopilot.
// G is a handle global: reference semantics, one world.

// one tick of intent, human or autopilot
class Input {
	bool fwd;
	bool back;
	bool sl;
	bool sr;
	bool tl;
	bool tr;
	bool run;
	bool fire;
	bool use;
	int weapon;
	bool start;

	Input(bool fwd_, bool back_, bool sl_, bool sr_, bool tl_, bool tr_, bool run_,
			bool fire_, bool use_, int weapon_, bool start_) {
		fwd = fwd_;
		back = back_;
		sl = sl_;
		sr = sr_;
		tl = tl_;
		tr = tr_;
		run = run_;
		fire = fire_;
		use = use_;
		weapon = weapon_;
		start = start_;
	}
}

class Game {
	// console + view geometry (sub-cell pixels: vw = w*PIXW, vh = rows*PIXH)
	int w;
	int h;
	int vw;
	int vh;

	// flow
	int mode = 0;          // 0 title, 1 play, 2 tally, 3 dead, 4 victory
	int mode_t = 0;
	int tick = 0;
	double accum = 0.0;
	bool quit = false;
	bool autopilot = false;
	string frame_key = "";
	int64 hash = 2166136261;

	// map
	int map_i = 0;
	string map_name = "";
	int par = 0;
	int mw = 0;
	int mh = 0;
	array<int> tiles;

	// player
	double px = (25.0 / 10.0);
	double py = (25.0 / 10.0);
	double pang = 0.0;
	int hp = 100;
	int armor = 0;
	array<int> ammo = {48, 0, 0};
	array<int> have = {1, 0, 0};
	int weapon = 0;
	bool key_r = false;
	bool key_b = false;
	int cooldown = 0;
	double bob = 0.0;
	int muzzle = 0;
	int gun_kick = 0;
	string death_cause = "the gloom";

	// world
	array<Enemy@> enemies;
	array<Item@> items;
	array<Shot@> shots;
	ParticlePool@ fx;
	int noise = 0;
	bool warden_down = false;

	// feedback
	string msg = "";
	string msg_color = "97";
	int msg_t = 0;
	int face_pain = 0;
	int face_grin = 0;
	int hurt_flash = 0;
	bool automap = false;

	// tallies
	int kills = 0;
	int kill_total = 0;
	int secrets = 0;
	int secret_total = 0;
	int map_ticks = 0;
	int ep_kills = 0;
	int ep_kill_total = 0;
	int ep_secrets = 0;
	int ep_secret_total = 0;
	int ep_ticks = 0;
	int deaths = 0;

	// render scratch (allocated once; bg rebaked per map)
	array<int> pix;
	array<int> bg;
	array<double> zbuf;
	dictionary lutc;

	// the autopilot: a coroutine handle living in a field, and the input it yielded
	coroutine@ pilot;
	Input@ pilot_out;
	Input@ input;   // the current tick's

	Game(int w_, int h_) {
		w = w_;
		h = h_;
		vw = w_ * PIXW;
		vh = (h_ - 6) * PIXH;
		@fx = ParticlePool();
		pix.resize(vw * vh);
		zbuf.resize(vw);
		for (int i = 0; i < vw; ++i) { zbuf[i] = 99.0; }
	}

	void reset_player() {
		hp = 100;
		armor = 0;
		ammo = {48, 0, 0};
		have = {1, 0, 0};
		weapon = 0;
		cooldown = 0;
		bob = 0.0;
	}

	void start_episode() {
		map_i = 0;
		ep_kills = 0;
		ep_kill_total = 0;
		ep_secrets = 0;
		ep_secret_total = 0;
		ep_ticks = 0;
		deaths = 0;
		reset_player();
		begin_map();
	}

	void begin_map() {
		key_r = false;
		key_b = false;
		shots.resize(0);
		fx.reset_pool();
		load_map(map_i);
		mode = 1;
		mode_t = 0;
		show_msg(map_name, "97");
	}

	void finish_map() {
		ep_kills = ep_kills + kills;
		ep_kill_total = ep_kill_total + kill_total;
		ep_secrets = ep_secrets + secrets;
		ep_secret_total = ep_secret_total + secret_total;
		ep_ticks = ep_ticks + map_ticks;
		map_i = map_i + 1;
		if (map_i >= int(MAPS.length())) {
			mode = 4;
			mode_t = 0;
		} else {
			begin_map();
		}
	}

	// deterministic input for this tick, human or coroutine
	Input@ gather_input() {
		if (autopilot) {
			if (pilot is null || pilot.done()) { @pilot = coroutine(coroutine_body(this.pilot_brain)); }
			pilot.resume();
			return pilot_out;
		}
		bool fire_edge = frame_key == " " || frame_key == "enter";
		int wsel = frame_key == "1" ? 0 : (frame_key == "2" ? 1 : (frame_key == "3" ? 2 : -1));
		return Input(
			key_down("w") || key_down("up"),
			key_down("s") || key_down("down"),
			key_down("a"),
			key_down("d"),
			key_down("left"),
			key_down("right"),
			key_down("shift"),
			key_down("space") || key_down("ctrl"),
			frame_key == "e",
			wsel,
			fire_edge || key_down("space") || key_down("ctrl"));
	}

	void run_tick() {
		tick = tick + 1;
		@input = gather_input();
		bool start_pressed = input.start;
		if (mode == 0) {
			mode_t = mode_t + 1;
			if (start_pressed && mode_t > 8) { start_episode(); }
			accumulate_hash();
			return;
		}
		if (mode == 2) {
			mode_t = mode_t + 1;
			if (start_pressed && mode_t > 25) { finish_map(); }
			accumulate_hash();
			return;
		}
		if (mode == 3) {
			mode_t = mode_t + 1;
			if (start_pressed && mode_t > 25) {
				deaths = deaths + 1;
				reset_player();
				begin_map();       // the floor repopulates; the Well remembers nothing
			}
			accumulate_hash();
			return;
		}
		if (mode == 4) {
			mode_t = mode_t + 1;
			accumulate_hash();
			return;
		}

		// --- play ---
		int wsel = input.weapon;
		if (wsel >= 0 && have[wsel] == 1 && wsel != weapon) {
			weapon = wsel;
			cooldown = 8;
			show_msg(WEAPONS[wsel].name + " ready", "97");
		}
		if (frame_key == "m") { automap = !automap; }
		if (input.use) { player_use(); }
		player_move(input);
		if (input.fire) { player_fire(); }

		if (cooldown > 0) { cooldown = cooldown - 1; }
		if (muzzle > 0) { muzzle = muzzle - 1; }
		if (gun_kick > 0) { gun_kick = gun_kick - 1; }
		if (noise > 0) { noise = noise - 1; }
		if (msg_t > 0) { msg_t = msg_t - 1; }
		if (face_pain > 0) { face_pain = face_pain - 1; }
		if (face_grin > 0) { face_grin = face_grin - 1; }
		if (hurt_flash > 0) { hurt_flash = hurt_flash - 1; }

		// live length each iteration: warden summons append mid-loop and tick this same tick
		for (uint i = 0; i < enemies.length(); ++i) { enemies[i].tick(); }
		update_shots();
		fx.update();
		map_ticks = map_ticks + 1;
		accumulate_hash();
	}

	void accumulate_hash() {
		int64 hh = hash;
		hh = mix32(hh, tick);
		hh = mix32(hh, mode * 31 + map_i);
		hh = mix32(hh, itrunc(px * 256.0) * 4096 + itrunc(py * 256.0));
		hh = mix32(hh, itrunc(pang * 1024.0));
		hh = mix32(hh, hp * 512 + armor);
		hh = mix32(hh, ammo[0] * 65536 + ammo[1] * 256 + ammo[2]);
		hh = mix32(hh, weapon * 64 + (key_r ? 2 : 0) + (key_b ? 1 : 0));
		int live = 0;
		int64 acc = 0;
		for (uint i = 0; i < enemies.length(); ++i) {
			Enemy@ e = enemies[i];
			if (!e.alive) { continue; }
			live = live + 1;
			acc = (acc + itrunc(e.x * 64.0) * 977 + itrunc(e.y * 64.0) * 331 + e.hp * 7) & MASK32;
		}
		hh = mix32(hh, live);
		hh = mix32(hh, acc);
		int64 pacc = 0;
		for (uint i = 0; i < fx.pool.length(); ++i) {
			Particle@ p = fx.pool[i];
			int pk = p.kind;
			if (pk == 0) { continue; }
			pacc = (pacc + pk * 131 + (itrunc(p.x * 16.0) * 61 + itrunc(p.y * 16.0)) * 17 + p.life) & MASK32;
		}
		hh = mix32(hh, pacc);
		hh = mix32(hh, int(shots.length()) * 8191 + kills * 127 + secrets * 31);
		hh = mix32(hh, RNG.state());
		hash = hh;
	}

	// ------------------------------------------------------- autopilot ------
	// One coroutine, one yield per tick, every decision from game state + RNG.
	// Fights what it sees, loots what remains, hunts secrets it "remembers",
	// opens what its keys allow, and pulls the exit switch.
	void pilot_brain() {
		array<int> dist_field;
		int field_age = 999;
		int goal_x = -1;
		int goal_y = -1;
		int strafe_dir = 1;
		int strafe_t = 0;
		while (true) {
			if (mode != 1) {
				pilot_yield(false, false, false, false, 0, false, false, -1, true);
				continue;
			}

			// --- combat: nearest visible enemy inside 12 tiles. Below 40 hp the
			// pilot disengages unless cornered; navigation will chase medkits.
			bool desperate = hp < 40;
			bool found = false;
			double tgt_x = 0.0;
			double tgt_y = 0.0;
			double best_d = 12.0;
			for (uint i = 0; i < enemies.length(); ++i) {
				Enemy@ e = enemies[i];
				if (!e.alive) { continue; }
				double d = sqrt(dist2(e.x, e.y, px, py));
				if (d < best_d && los_clear(px, py, e.x, e.y)) {
					best_d = d;
					tgt_x = e.x;
					tgt_y = e.y;
					found = true;
				}
			}
			if (found && desperate && best_d > 3.0) { found = false; }
			if (found) {
				double want = atan2(tgt_y - py, tgt_x - px);
				double da = ang_diff(pang, want);
				int turn = 0;
				if (da > (5.0 / 100.0)) { turn = 1; }
				if (da < -(5.0 / 100.0)) { turn = -1; }
				// weapon sense: scatter close, hex for crowds/far, pistol default
				int wsel = -1;
				if (have[1] == 1 && ammo[1] > 0 && best_d < 6.0) { wsel = 1; }
				else if (have[2] == 1 && ammo[2] > 0 && best_d > (35.0 / 10.0)) { wsel = 2; }
				else if (ammo[0] > 0) { wsel = 0; }
				else if (have[1] == 1 && ammo[1] > 0) { wsel = 1; }
				else if (have[2] == 1 && ammo[2] > 0) { wsel = 2; }
				if (wsel == weapon) { wsel = -1; }
				bool aligned = da < (14.0 / 100.0) && da > -(14.0 / 100.0);
				bool fire = aligned && cooldown == 0 && best_d > (12.0 / 10.0);
				// footwork: back off rushers, strafe under fire
				bool back = best_d < (24.0 / 10.0);
				bool fwd = best_d > (85.0 / 10.0) && aligned;
				strafe_t = strafe_t + 1;
				if (strafe_t > 18) {
					strafe_t = 0;
					if (RNG.chance(60)) { strafe_dir = 0 - strafe_dir; }
				}
				pilot_yield(fwd, back, strafe_dir < 0, strafe_dir > 0, turn, fire, false, wsel, false);
				continue;
			}

			// --- navigate: pick a goal, BFS-from-goal field, walk downhill.
			// COMMIT to the goal until arrival (re-picking nearest-by-euclid on
			// a timer thrashes: the BFS route west may start by walking east,
			// which flips the euclidean winner). The field alone refreshes
			// periodically so newly opened doors shorten the route; a stale
			// valve breaks any pathological commitment.
			field_age = field_age + 1;
			bool goal_gone = goal_x < 0 || field_age > 240;
			if (!goal_gone) {
				// goal completed? (item taken / secret opened / enemy dead handled above)
				if (tile_at(goal_x, goal_y) == 0 && ifloor(px) == goal_x && ifloor(py) == goal_y) { goal_gone = true; }
			}
			if (goal_gone) {
				pick_goal(goal_x, goal_y);
				dist_field = bfs_field(goal_x, goal_y);
				field_age = 0;
			} else if (field_age % 45 == 0) {
				dist_field = bfs_field(goal_x, goal_y);
			}
			if (goal_x < 0) {
				// nothing to want: spin slowly (a lost pilot is still deterministic)
				pilot_yield(false, false, false, false, 1, false, false, -1, false);
				continue;
			}
			int ptx = ifloor(px);
			int pty = ifloor(py);
			int here = dist_field[pty * mw + ptx];
			if (here < 0) {
				// goal unreachable from here (shouldn't happen): drop it
				goal_x = -1;
				pilot_yield(false, false, false, false, 0, false, false, -1, false);
				continue;
			}
			// pick the walkable-or-door neighbor closest to the goal
			int nx = ptx;
			int ny = pty;
			int bestv = here;
			for (int d2 = 0; d2 < 4; ++d2) {
				int cx = ptx + DIR_X[d2];
				int cy = pty + DIR_Y[d2];
				if (cx < 0 || cy < 0 || cx >= mw || cy >= mh) { continue; }
				int v = dist_field[cy * mw + cx];
				if (v >= 0 && v < bestv) {
					bestv = v;
					nx = cx;
					ny = cy;
				}
			}
			double wx = nx + (5.0 / 10.0);
			double wy = ny + (5.0 / 10.0);
			double want2 = atan2(wy - py, wx - px);
			double da2 = ang_diff(pang, want2);
			int turn2 = 0;
			if (da2 > (8.0 / 100.0)) { turn2 = 1; }
			if (da2 < -(8.0 / 100.0)) { turn2 = -1; }
			bool fwd2 = da2 < (6.0 / 10.0) && da2 > -(6.0 / 10.0);
			// a door/secret in the way? use it when close and mostly facing
			int front_t = tile_at(nx, ny);
			bool use2 = false;
			if (front_t != 0 && dist2(px, py, wx, wy) < (21.0 / 10.0) && fwd2) { use2 = true; }
			pilot_yield(fwd2, false, false, false, turn2, false, use2, -1, false);
		}
	}

	// the pilot's one tick of intent: publish it, then suspend until the next tick
	void pilot_yield(bool fwd, bool back, bool sl, bool sr, int turn, bool fire, bool use, int wsel, bool start) {
		@pilot_out = Input(fwd, back, sl, sr, turn < 0, turn > 0, true, fire, use, wsel, start);
		yield();
	}

	// goal priority: medkits when hurting -> live enemies -> loot -> unfound
	// secrets (the pilot "remembers the walls") -> the exit
	void pick_goal(int &out gx, int &out gy) {
		if (hp < 55) {
			nearest_item({0, 1, 10}, gx, gy);
			if (gx >= 0) { return; }
		}
		int hunt_x = -1;
		int hunt_y = -1;
		double hd = 9999.0;
		for (uint i = 0; i < enemies.length(); ++i) {
			Enemy@ e = enemies[i];
			if (!e.alive) { continue; }
			double d = dist2(e.x, e.y, px, py);
			if (d < hd) {
				hd = d;
				hunt_x = ifloor(e.x);
				hunt_y = ifloor(e.y);
			}
		}
		if (hunt_x >= 0) {
			gx = hunt_x;
			gy = hunt_y;
			return;
		}
		nearest_item({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, gx, gy);
		if (gx >= 0) { return; }
		if (secrets < secret_total) {
			nearest_secret(gx, gy);
			if (gx >= 0) { return; }
		}
		exit_tile(gx, gy);
	}

	void nearest_item(const array<int> &in kinds, int &out bx, int &out by) {
		bx = -1;
		by = -1;
		double bd = 9999.0;
		for (uint i = 0; i < items.length(); ++i) {
			Item@ it = items[i];
			if (it.taken) { continue; }
			int k = it.kind;
			if (kinds.find(k) < 0) { continue; }
			// skip anything the pickup code would refuse: otherwise the pilot
			// orbits a stimpack it can't absorb until the heat death of the run
			if ((k == 0 || k == 1) && hp >= 100) { continue; }
			if (k == 10 && hp >= 150) { continue; }
			if (k == 2 && armor >= 100) { continue; }
			if (k == 3 && ammo[0] >= AMMO_MAX[0]) { continue; }
			if (k == 4 && ammo[1] >= AMMO_MAX[1]) { continue; }
			if (k == 5 && ammo[2] >= AMMO_MAX[2]) { continue; }
			// keys we already carry are not loot
			if (k == 6 && key_r) { continue; }
			if (k == 7 && key_b) { continue; }
			double d = dist2(it.x, it.y, px, py);
			if (d < bd) {
				bd = d;
				bx = ifloor(it.x);
				by = ifloor(it.y);
			}
		}
	}

	void nearest_secret(int &out bx, int &out by) {
		bx = -1;
		by = -1;
		double bd = 9999.0;
		for (int ty = 0; ty < mh; ++ty) {
			for (int tx = 0; tx < mw; ++tx) {
				if (tiles[ty * mw + tx] != 7) { continue; }
				double d = dist2(tx + (5.0 / 10.0), ty + (5.0 / 10.0), px, py);
				if (d < bd) {
					bd = d;
					bx = tx;
					by = ty;
				}
			}
		}
	}

	void exit_tile(int &out ex, int &out ey) {
		ex = -1;
		ey = -1;
		for (int ty = 0; ty < mh; ++ty) {
			for (int tx = 0; tx < mw; ++tx) {
				if (tiles[ty * mw + tx] == 6) {
					ex = tx;
					ey = ty;
					return;
				}
			}
		}
	}

	// BFS distances FROM the goal, over floor + doors this pilot can open
	// (+ the goal tile itself so walls like secrets/exit are approachable)
	array<int> bfs_field(int gx, int gy) {
		array<int> field(mw * mh, -1);
		if (gx < 0) { return field; }
		array<int> queue = {gy * mw + gx};
		field[gy * mw + gx] = 0;
		uint head = 0;
		while (head < queue.length()) {
			int cur = queue[head];
			head = head + 1;
			int cx = cur % mw;
			int cy = cur / mw;
			int cd = field[cur];
			for (int d = 0; d < 4; ++d) {
				int nx = cx + DIR_X[d];
				int ny = cy + DIR_Y[d];
				if (nx < 0 || ny < 0 || nx >= mw || ny >= mh) { continue; }
				if (field[ny * mw + nx] >= 0) { continue; }
				int t = tiles[ny * mw + nx];
				bool pass = t == 0 || t == 5;
				if (t == 8 && key_r) { pass = true; }
				if (t == 9 && key_b) { pass = true; }
				if (!pass) { continue; }
				field[ny * mw + nx] = cd + 1;
				queue.insertLast(ny * mw + nx);
			}
		}
		return field;
	}
}

// the one world handle
Game@ G;
