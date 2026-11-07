// Player simulation: movement with wall sliding, the 'use' action (doors,
// secrets, the exit switch), pickups.

// circle-vs-grid: all four corners of the actor's square must be on floor
bool spot_free(double x, double y, double r) {
	if (tile_solid(tile_at(ifloor(x - r), ifloor(y - r)))) { return false; }
	if (tile_solid(tile_at(ifloor(x + r), ifloor(y - r)))) { return false; }
	if (tile_solid(tile_at(ifloor(x - r), ifloor(y + r)))) { return false; }
	if (tile_solid(tile_at(ifloor(x + r), ifloor(y + r)))) { return false; }
	return true;
}

void player_move(Input@ input) {
	double turn = 0.0;
	if (input.tl) { turn -= 1.0; }
	if (input.tr) { turn += 1.0; }
	G.pang = G.pang + turn * (27.0 / 10.0) * TICK;
	while (G.pang > (62831853.0 / 10000000.0)) { G.pang -= (62831853.0 / 10000000.0); }
	while (G.pang < 0.0) { G.pang += (62831853.0 / 10000000.0); }

	double dirx = cos(G.pang);
	double diry = sin(G.pang);
	double wx = 0.0;
	double wy = 0.0;
	if (input.fwd) {
		wx += dirx;
		wy += diry;
	}
	if (input.back) {
		wx -= dirx;
		wy -= diry;
	}
	if (input.sl) {
		wx += diry;
		wy -= dirx;
	}
	if (input.sr) {
		wx -= diry;
		wy += dirx;
	}
	double wl = sqrt(wx * wx + wy * wy);
	if (wl > (1.0 / 1000.0)) {
		bool run = input.run;
		double speed = (run ? 5.0 : (34.0 / 10.0)) * TICK;
		wx = wx / wl * speed;
		wy = wy / wl * speed;
		double nx = G.px + wx;
		double ny = G.py + wy;
		bool moved = false;
		if (spot_free(nx, G.py, (26.0 / 100.0))) {
			G.px = nx;
			moved = true;
		}
		if (spot_free(G.px, ny, (26.0 / 100.0))) {
			G.py = ny;
			moved = true;
		}
		if (moved) {
			G.bob = G.bob + (run ? (3.0 / 10.0) : (22.0 / 100.0));
			check_pickups();
		}
	}
}

// ------------------------------------------------------------ 'use' ---------
// probe a few steps down the facing for the first non-floor tile
void player_use() {
	double dirx = cos(G.pang);
	double diry = sin(G.pang);
	const array<double> probes = {(5.0 / 10.0), (9.0 / 10.0), (13.0 / 10.0)};
	for (uint i = 0; i < probes.length(); ++i) {
		int tx = ifloor(G.px + dirx * probes[i]);
		int ty = ifloor(G.py + diry * probes[i]);
		int t = tile_at(tx, ty);
		if (t == 0) { continue; }
		use_tile(tx, ty, t);
		return;
	}
	show_msg("nothing to use", "90");
}

void use_tile(int tx, int ty, int t) {
	if (t == 5) {
		open_door(tx, ty, "the door grinds open");
		return;
	}
	if (t == 8) {
		if (G.key_r) { open_door(tx, ty, "RED lock released"); }
		else { show_msg("you need the RED keycard", "91"); }
		return;
	}
	if (t == 9) {
		if (G.key_b) { open_door(tx, ty, "BLUE lock released"); }
		else { show_msg("you need the BLUE keycard", "94"); }
		return;
	}
	if (t == 7) {
		G.tiles[ty * G.mw + tx] = 0;
		G.secrets = G.secrets + 1;
		fx_secret_glitter(tx + (5.0 / 10.0), ty + (5.0 / 10.0));
		show_msg("you found a secret!", "96");
		return;
	}
	if (t == 6) {
		G.mode = 2;      // tally
		G.mode_t = 0;
		show_msg("level complete", "92");
		return;
	}
	show_msg("solid rock. very solid.", "90");
}

void open_door(int tx, int ty, const string &in msg) {
	G.tiles[ty * G.mw + tx] = 0;
	fx_door_puff(tx + (5.0 / 10.0), ty + (5.0 / 10.0));
	G.noise = imax(G.noise, 10);
	show_msg(msg, "97");
}

// ------------------------------------------------------------ pickups -------
bool give_ammo(int t, int n) {
	if (G.ammo[t] >= AMMO_MAX[t]) { return false; }
	G.ammo[t] = imin(AMMO_MAX[t], G.ammo[t] + n);
	return true;
}

void check_pickups() {
	for (uint i = 0; i < G.items.length(); ++i) {
		Item@ it = G.items[i];
		if (it.taken) { continue; }
		int kind = it.kind;
		if (dist2(G.px, G.py, it.x, it.y) > (36.0 / 100.0)) { continue; }
		bool got = false;
		if (kind == 0) {
			if (G.hp < 100) {
				G.hp = imin(100, G.hp + 10);
				got = true;
			}
		} else if (kind == 1) {
			if (G.hp < 100) {
				G.hp = imin(100, G.hp + 25);
				got = true;
			}
		} else if (kind == 2) {
			if (G.armor < 100) {
				G.armor = imin(100, G.armor + 50);
				got = true;
			}
		} else if (kind == 3) {
			got = give_ammo(0, 20);
		} else if (kind == 4) {
			got = give_ammo(1, 8);
		} else if (kind == 5) {
			got = give_ammo(2, 30);
		} else if (kind == 6) {
			G.key_r = true;
			got = true;
		} else if (kind == 7) {
			G.key_b = true;
			got = true;
		} else if (kind == 8) {
			G.have[1] = 1;
			give_ammo(1, 8);
			G.weapon = 1;
			G.cooldown = 8;
			got = true;
			G.face_grin = 30;
		} else if (kind == 9) {
			G.have[2] = 1;
			give_ammo(2, 40);
			G.weapon = 2;
			G.cooldown = 8;
			got = true;
			G.face_grin = 30;
		} else if (kind == 10) {
			G.hp = imin(150, G.hp + 40);
			got = true;
			G.face_grin = 20;
		}
		if (got) {
			it.taken = true;
			show_msg("picked up " + ITEM_DEFS[kind].name, "93");
		}
	}
}
