// Game state + the fixed-timestep tick + the deterministic autopilot.
// G is a single class instance (Squirrel instances are reference-semantic,
// so nothing can deep-copy the world by accident). The autopilot is a native
// generator held in a field — same shape as the enemy brains.

class Game {
	// console + view geometry (sub-cell pixels: vw = w*PIXW, vh = rows*PIXH)
	w = 100;
	h = 40;
	vw = 200;
	vh = 68;

	// flow
	mode = 0;          // 0 title, 1 play, 2 tally, 3 dead, 4 victory
	mode_t = 0;
	tick = 0;
	accum = 0.0;
	quit = false;
	autopilot = false;
	frame_key = "";
	hash = 2166136261;

	// map
	map_i = 0;
	map_name = "";
	par = 0;
	mw = 0;
	mh = 0;
	tiles = null;

	// player
	px = 2.5;
	py = 2.5;
	pang = 0.0;
	hp = 100;
	armor = 0;
	ammo = null;
	have = null;
	weapon = 0;
	key_r = false;
	key_b = false;
	cooldown = 0;
	bob = 0.0;
	muzzle = 0;
	gun_kick = 0;
	death_cause = "the gloom";

	// world
	enemies = null;    // Enemy instance registry
	items = null;      // [kind, x, y, taken]
	shots = null;      // [kind, x, y, vx, vy, ttl]
	fx = null;         // ParticlePool
	noise = 0;
	warden_down = false;

	// feedback
	msg = "";
	msg_color = "97";
	msg_t = 0;
	face_pain = 0;
	face_grin = 0;
	hurt_flash = 0;
	automap = false;

	// tallies
	kills = 0;
	kill_total = 0;
	secrets = 0;
	secret_total = 0;
	map_ticks = 0;
	ep_kills = 0;
	ep_kill_total = 0;
	ep_secrets = 0;
	ep_secret_total = 0;
	ep_ticks = 0;
	deaths = 0;

	// render scratch (allocated once; bg rebaked per map)
	bg = null;
	zbuf = null;
	lutc = null;

	// the autopilot: a generator handle living in a field
	pilot = null;
	input = null;

	constructor(w_, h_) {
		w = w_;
		h = h_;
		vw = w_ * ::PIXW;
		vh = (h_ - 6) * ::PIXH;
		fx = ::ParticlePool();
		tiles = [];
		ammo = [48, 0, 0];
		have = [1, 0, 0];
		enemies = [];
		items = [];
		shots = [];
		bg = [];
		lutc = {};
		input = {};
		zbuf = ::array(vw, 99.0);
	}

	function reset_player() {
		hp = 100;
		armor = 0;
		ammo = [48, 0, 0];
		have = [1, 0, 0];
		weapon = 0;
		cooldown = 0;
		bob = 0.0;
	}

	function start_episode() {
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

	function begin_map() {
		key_r = false;
		key_b = false;
		shots = [];
		fx.reset();
		::load_map(map_i);
		mode = 1;
		mode_t = 0;
		::show_msg(map_name, "97");
	}

	function finish_map() {
		ep_kills = ep_kills + kills;
		ep_kill_total = ep_kill_total + kill_total;
		ep_secrets = ep_secrets + secrets;
		ep_secret_total = ep_secret_total + secret_total;
		ep_ticks = ep_ticks + map_ticks;
		map_i = map_i + 1;
		if (map_i >= ::MAPS.len()) {
			mode = 4;
			mode_t = 0;
		} else {
			begin_map();
		}
	}

	// deterministic input for this tick, human or generator
	function gather_input() {
		if (autopilot) {
			if (pilot == null || pilot.getstatus() == "dead") { pilot = pilot_brain(); }
			return resume pilot;
		}
		local fire_edge = frame_key == " " || frame_key == "enter";
		return {
			fwd = ::key_down("w") || ::key_down("up"),
			back = ::key_down("s") || ::key_down("down"),
			sl = ::key_down("a"),
			sr = ::key_down("d"),
			tl = ::key_down("left"),
			tr = ::key_down("right"),
			run = ::key_down("shift"),
			fire = ::key_down("space") || ::key_down("ctrl"),
			use = frame_key == "e",
			weapon = frame_key == "1" ? 0 : (frame_key == "2" ? 1 : (frame_key == "3" ? 2 : -1)),
			start = fire_edge || ::key_down("space") || ::key_down("ctrl")
		};
	}

	function run_tick() {
		tick = tick + 1;
		input = gather_input();
		local start_pressed = input.start;
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
				begin_map();       // the floor repopulates; the RNG stream continues
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
		local wsel = input.weapon;
		if (wsel >= 0 && have[wsel] == 1 && wsel != weapon) {
			weapon = wsel;
			cooldown = 8;
			::show_msg(::WEAPONS[wsel].name + " ready", "97");
		}
		if (frame_key == "m") { automap = !automap; }
		if (input.use) { ::player_use(); }
		::player_move(input);
		if (input.fire) { ::player_fire(); }

		if (cooldown > 0) { cooldown = cooldown - 1; }
		if (muzzle > 0) { muzzle = muzzle - 1; }
		if (gun_kick > 0) { gun_kick = gun_kick - 1; }
		if (noise > 0) { noise = noise - 1; }
		if (msg_t > 0) { msg_t = msg_t - 1; }
		if (face_pain > 0) { face_pain = face_pain - 1; }
		if (face_grin > 0) { face_grin = face_grin - 1; }
		if (hurt_flash > 0) { hurt_flash = hurt_flash - 1; }

		foreach (e in enemies) { e.tick(); }
		::update_shots();
		fx.update();
		map_ticks = map_ticks + 1;
		accumulate_hash();
	}

	function accumulate_hash() {
		local hh = hash;
		hh = ::mix32(hh, tick);
		hh = ::mix32(hh, mode * 31 + map_i);
		hh = ::mix32(hh, (px * 256.0).tointeger() * 4096 + (py * 256.0).tointeger());
		hh = ::mix32(hh, (pang * 1024.0).tointeger());
		hh = ::mix32(hh, hp * 512 + armor);
		hh = ::mix32(hh, ammo[0] * 65536 + ammo[1] * 256 + ammo[2]);
		hh = ::mix32(hh, weapon * 64 + (key_r ? 2 : 0) + (key_b ? 1 : 0));
		local live = 0;
		local acc = 0;
		foreach (e in enemies) {
			if (!e.alive) { continue; }
			live = live + 1;
			acc = (acc + (e.x * 64.0).tointeger() * 977 + (e.y * 64.0).tointeger() * 331 + e.hp * 7) & 0xFFFFFFFF;
		}
		hh = ::mix32(hh, live);
		hh = ::mix32(hh, acc);
		local pacc = 0;
		foreach (p in fx.pool) {
			local pk = p[0];
			if (pk == 0) { continue; }
			pacc = (pacc + pk * 131 + ((p[1] * 16.0).tointeger() * 61 + (p[2] * 16.0).tointeger()) * 17 + p[7]) & 0xFFFFFFFF;
		}
		hh = ::mix32(hh, pacc);
		hh = ::mix32(hh, shots.len() * 8191 + kills * 127 + secrets * 31);
		hh = ::mix32(hh, ::RNG.state());
		hash = hh;
	}

	// ------------------------------------------------------- autopilot ------
	// One generator, one yield per tick, every decision from game state + RNG.
	// Fights what it sees, loots what remains, hunts secrets it "remembers",
	// opens what its keys allow, and pulls the exit switch.
	function pilot_brain() {
		local dist_field = [];
		local field_age = 999;
		local goal_x = -1;
		local goal_y = -1;
		local strafe_dir = 1;
		local strafe_t = 0;
		while (true) {
			if (mode != 1) {
				yield pilot_input(false, false, false, false, 0, false, false, -1, true);
				continue;
			}

			// --- combat: nearest visible enemy inside 12 tiles. Below 40 hp the
			// pilot disengages unless cornered — navigation will chase medkits.
			local desperate = hp < 40;
			local found = false;
			local tgt_x = 0.0;
			local tgt_y = 0.0;
			local best_d = 12.0;
			foreach (e in enemies) {
				if (!e.alive) { continue; }
				local d = ::sqrt(::dist2(e.x, e.y, px, py));
				if (d < best_d && ::los_clear(px, py, e.x, e.y)) {
					best_d = d;
					tgt_x = e.x;
					tgt_y = e.y;
					found = true;
				}
			}
			if (found && desperate && best_d > 3.0) { found = false; }
			if (found) {
				local want = ::atan2(tgt_y - py, tgt_x - px);
				local da = ::ang_diff(pang, want);
				local turn = 0;
				if (da > 0.05) { turn = 1; }
				if (da < -0.05) { turn = -1; }
				// weapon sense: scatter close, hex for crowds/far, pistol default
				local wsel = -1;
				if (have[1] == 1 && ammo[1] > 0 && best_d < 6.0) { wsel = 1; }
				else if (have[2] == 1 && ammo[2] > 0 && best_d > 3.5) { wsel = 2; }
				else if (ammo[0] > 0) { wsel = 0; }
				else if (have[1] == 1 && ammo[1] > 0) { wsel = 1; }
				else if (have[2] == 1 && ammo[2] > 0) { wsel = 2; }
				if (wsel == weapon) { wsel = -1; }
				local aligned = da < 0.14 && da > -0.14;
				local fire = aligned && cooldown == 0 && best_d > 1.2;
				// footwork: back off rushers, strafe under fire
				local back = best_d < 2.4;
				local fwd = best_d > 8.5 && aligned;
				strafe_t = strafe_t + 1;
				if (strafe_t > 18) {
					strafe_t = 0;
					if (::RNG.chance(60)) { strafe_dir = 0 - strafe_dir; }
				}
				yield pilot_input(fwd, back, strafe_dir < 0, strafe_dir > 0, turn, fire, false, wsel, false);
				continue;
			}

			// --- navigate: pick a goal, BFS-from-goal field, walk downhill.
			// COMMIT to the goal until arrival; the field alone refreshes
			// periodically so newly opened doors shorten the route.
			field_age = field_age + 1;
			local goal_gone = goal_x < 0 || field_age > 240;
			if (!goal_gone) {
				if (::tile_at(goal_x, goal_y) == 0 && ::ifloor(px) == goal_x && ::ifloor(py) == goal_y) { goal_gone = true; }
			}
			if (goal_gone) {
				local goal = pick_goal();
				goal_x = goal[0];
				goal_y = goal[1];
				dist_field = bfs_field(goal_x, goal_y);
				field_age = 0;
			} else if (field_age % 45 == 0) {
				dist_field = bfs_field(goal_x, goal_y);
			}
			if (goal_x < 0) {
				// nothing to want: spin slowly (a lost pilot is still deterministic)
				yield pilot_input(false, false, false, false, 1, false, false, -1, false);
				continue;
			}
			local ptx = ::ifloor(px);
			local pty = ::ifloor(py);
			local here = dist_field[pty * mw + ptx];
			if (here < 0) {
				// goal unreachable from here (shouldn't happen): drop it
				goal_x = -1;
				yield pilot_input(false, false, false, false, 0, false, false, -1, false);
				continue;
			}
			// pick the walkable-or-door neighbor closest to the goal
			local nx = ptx;
			local ny = pty;
			local bestv = here;
			local dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
			foreach (d2 in dirs) {
				local cx = ptx + d2[0];
				local cy = pty + d2[1];
				if (cx < 0 || cy < 0 || cx >= mw || cy >= mh) { continue; }
				local v = dist_field[cy * mw + cx];
				if (v >= 0 && v < bestv) {
					bestv = v;
					nx = cx;
					ny = cy;
				}
			}
			local wx = nx + 0.5;
			local wy = ny + 0.5;
			local want2 = ::atan2(wy - py, wx - px);
			local da2 = ::ang_diff(pang, want2);
			local turn2 = 0;
			if (da2 > 0.08) { turn2 = 1; }
			if (da2 < -0.08) { turn2 = -1; }
			local fwd2 = da2 < 0.6 && da2 > -0.6;
			// a door/secret in the way? use it when close and mostly facing
			local front_t = ::tile_at(nx, ny);
			local use2 = false;
			if (front_t != 0 && ::dist2(px, py, wx, wy) < 2.1 && fwd2) { use2 = true; }
			yield pilot_input(fwd2, false, false, false, turn2, false, use2, -1, false);
		}
	}

	function pilot_input(fwd, back, sl, sr, turn, fire, use, wsel, start) {
		return {
			fwd = fwd, back = back, sl = sl, sr = sr,
			tl = turn < 0, tr = turn > 0,
			run = true, fire = fire, use = use,
			weapon = wsel, start = start
		};
	}

	// goal priority: medkits when hurting -> live enemies -> loot -> unfound
	// secrets (the pilot "remembers the walls") -> the exit
	function pick_goal() {
		if (hp < 55) {
			local med = nearest_item([0, 1, 10]);
			if (med[0] >= 0) { return med; }
		}
		local hunt_x = -1;
		local hunt_y = -1;
		local hd = 9999.0;
		foreach (e in enemies) {
			if (!e.alive) { continue; }
			local d = ::dist2(e.x, e.y, px, py);
			if (d < hd) {
				hd = d;
				hunt_x = ::ifloor(e.x);
				hunt_y = ::ifloor(e.y);
			}
		}
		if (hunt_x >= 0) { return [hunt_x, hunt_y]; }
		local loot = nearest_item([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
		if (loot[0] >= 0) { return loot; }
		if (secrets < secret_total) {
			local sgoal = nearest_secret();
			if (sgoal[0] >= 0) { return sgoal; }
		}
		return exit_tile();
	}

	function nearest_item(kinds) {
		local bx = -1;
		local by = -1;
		local bd = 9999.0;
		for (local i = 0; i < items.len(); ++i) {
			local it = items[i];
			if (it[3] == 1) { continue; }
			local k = it[0];
			local wanted = false;
			foreach (kk in kinds) { if (kk == k) { wanted = true; } }
			if (!wanted) { continue; }
			// skip anything the pickup code would refuse — otherwise the pilot
			// orbits a stimpack it can't absorb until the heat death of the run
			if ((k == 0 || k == 1) && hp >= 100) { continue; }
			if (k == 10 && hp >= 150) { continue; }
			if (k == 2 && armor >= 100) { continue; }
			if (k == 3 && ammo[0] >= ::AMMO_MAX[0]) { continue; }
			if (k == 4 && ammo[1] >= ::AMMO_MAX[1]) { continue; }
			if (k == 5 && ammo[2] >= ::AMMO_MAX[2]) { continue; }
			// keys we already carry are not loot
			if (k == 6 && key_r) { continue; }
			if (k == 7 && key_b) { continue; }
			local d = ::dist2(it[1], it[2], px, py);
			if (d < bd) {
				bd = d;
				bx = ::ifloor(it[1]);
				by = ::ifloor(it[2]);
			}
		}
		return [bx, by];
	}

	function nearest_secret() {
		local bx = -1;
		local by = -1;
		local bd = 9999.0;
		for (local ty = 0; ty < mh; ++ty) {
			for (local tx = 0; tx < mw; ++tx) {
				if (tiles[ty * mw + tx] != 7) { continue; }
				local d = ::dist2(tx + 0.5, ty + 0.5, px, py);
				if (d < bd) {
					bd = d;
					bx = tx;
					by = ty;
				}
			}
		}
		return [bx, by];
	}

	function exit_tile() {
		for (local ty = 0; ty < mh; ++ty) {
			for (local tx = 0; tx < mw; ++tx) {
				if (tiles[ty * mw + tx] == 6) { return [tx, ty]; }
			}
		}
		return [-1, -1];
	}

	// BFS distances FROM the goal, over floor + doors this pilot can open
	// (+ the goal tile itself so walls like secrets/exit are approachable)
	function bfs_field(gx, gy) {
		local n = mw * mh;
		local field = ::array(n, -1);
		if (gx < 0) { return field; }
		local queue = [[gx, gy]];
		field[gy * mw + gx] = 0;
		local head = 0;
		while (head < queue.len()) {
			local cur = queue[head];
			head = head + 1;
			local cx = cur[0];
			local cy = cur[1];
			local cd = field[cy * mw + cx];
			local dirs = [[1, 0], [-1, 0], [0, 1], [0, -1]];
			foreach (d in dirs) {
				local nx = cx + d[0];
				local ny = cy + d[1];
				if (nx < 0 || ny < 0 || nx >= mw || ny >= mh) { continue; }
				if (field[ny * mw + nx] >= 0) { continue; }
				local t = tiles[ny * mw + nx];
				local pass = t == 0 || t == 5;
				if (t == 8 && key_r) { pass = true; }
				if (t == 9 && key_b) { pass = true; }
				if (!pass) { continue; }
				field[ny * mw + nx] = cd + 1;
				queue.push([nx, ny]);
			}
		}
		return field;
	}
}

// the one world handle
::G <- null;
