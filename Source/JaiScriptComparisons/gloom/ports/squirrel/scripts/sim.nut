// Player simulation: movement with wall sliding, the 'use' action (doors,
// secrets, the exit switch), pickups.

// circle-vs-grid: all four corners of the actor's square must be on floor
function spot_free(x, y, r) {
	if (::tile_solid(::tile_at(::ifloor(x - r), ::ifloor(y - r)))) { return false; }
	if (::tile_solid(::tile_at(::ifloor(x + r), ::ifloor(y - r)))) { return false; }
	if (::tile_solid(::tile_at(::ifloor(x - r), ::ifloor(y + r)))) { return false; }
	if (::tile_solid(::tile_at(::ifloor(x + r), ::ifloor(y + r)))) { return false; }
	return true;
}

function player_move(input) {
	local g = ::G;
	local turn = 0.0;
	if (input.tl) { turn -= 1.0; }
	if (input.tr) { turn += 1.0; }
	g.pang = g.pang + turn * 2.7 * ::TICK;
	while (g.pang > 6.2831853) { g.pang -= 6.2831853; }
	while (g.pang < 0.0) { g.pang += 6.2831853; }

	local dirx = ::cos(g.pang);
	local diry = ::sin(g.pang);
	local wx = 0.0;
	local wy = 0.0;
	if (input.fwd) { wx += dirx; wy += diry; }
	if (input.back) { wx -= dirx; wy -= diry; }
	if (input.sl) { wx += diry; wy -= dirx; }
	if (input.sr) { wx -= diry; wy += dirx; }
	local wl = ::sqrt(wx * wx + wy * wy);
	if (wl > 0.001) {
		local run = input.run;
		local speed = (run ? 5.0 : 3.4) * ::TICK;
		wx = wx / wl * speed;
		wy = wy / wl * speed;
		local nx = g.px + wx;
		local ny = g.py + wy;
		local moved = false;
		if (::spot_free(nx, g.py, 0.26)) { g.px = nx; moved = true; }
		if (::spot_free(g.px, ny, 0.26)) { g.py = ny; moved = true; }
		if (moved) {
			g.bob = g.bob + (run ? 0.30 : 0.22);
			::check_pickups();
		}
	}
}

// ------------------------------------------------------------ 'use' ---------
// probe a few steps down the facing for the first non-floor tile
function player_use() {
	local g = ::G;
	local dirx = ::cos(g.pang);
	local diry = ::sin(g.pang);
	local probes = [0.5, 0.9, 1.3];
	foreach (p in probes) {
		local tx = ::ifloor(g.px + dirx * p);
		local ty = ::ifloor(g.py + diry * p);
		local t = ::tile_at(tx, ty);
		if (t == 0) { continue; }
		::use_tile(tx, ty, t);
		return;
	}
	::show_msg("nothing to use", "90");
}

function use_tile(tx, ty, t) {
	local g = ::G;
	if (t == 5) { ::open_door(tx, ty, "the door grinds open"); return; }
	if (t == 8) {
		if (g.key_r) { ::open_door(tx, ty, "RED lock released"); }
		else { ::show_msg("you need the RED keycard", "91"); }
		return;
	}
	if (t == 9) {
		if (g.key_b) { ::open_door(tx, ty, "BLUE lock released"); }
		else { ::show_msg("you need the BLUE keycard", "94"); }
		return;
	}
	if (t == 7) {
		g.tiles[ty * g.mw + tx] = 0;
		g.secrets++;
		::fx_secret_glitter(tx + 0.5, ty + 0.5);
		::show_msg("you found a secret!", "96");
		return;
	}
	if (t == 6) {
		g.mode = 2;      // tally
		g.mode_t = 0;
		::show_msg("level complete", "92");
		return;
	}
	::show_msg("solid rock. very solid.", "90");
}

function open_door(tx, ty, msg) {
	local g = ::G;
	g.tiles[ty * g.mw + tx] = 0;
	::fx_door_puff(tx + 0.5, ty + 0.5);
	g.noise = ::imax(g.noise, 10);
	::show_msg(msg, "97");
}

// ------------------------------------------------------------ pickups -------
function give_ammo(t, n) {
	local g = ::G;
	if (g.ammo[t] >= ::AMMO_MAX[t]) { return false; }
	g.ammo[t] = ::imin(::AMMO_MAX[t], g.ammo[t] + n);
	return true;
}

function check_pickups() {
	local g = ::G;
	local items = g.items;
	for (local i = 0; i < items.len(); ++i) {
		local it = items[i];
		if (it[3] == 1) { continue; }
		local kind = it[0];
		local ix = it[1];
		local iy = it[2];
		if (::dist2(g.px, g.py, ix, iy) > 0.36) { continue; }
		local got = false;
		if (kind == 0) { if (g.hp < 100) { g.hp = ::imin(100, g.hp + 10); got = true; } }
		else if (kind == 1) { if (g.hp < 100) { g.hp = ::imin(100, g.hp + 25); got = true; } }
		else if (kind == 2) { if (g.armor < 100) { g.armor = ::imin(100, g.armor + 50); got = true; } }
		else if (kind == 3) { got = ::give_ammo(0, 20); }
		else if (kind == 4) { got = ::give_ammo(1, 8); }
		else if (kind == 5) { got = ::give_ammo(2, 30); }
		else if (kind == 6) { g.key_r = true; got = true; }
		else if (kind == 7) { g.key_b = true; got = true; }
		else if (kind == 8) {
			g.have[1] = 1;
			::give_ammo(1, 8);
			g.weapon = 1;
			g.cooldown = 8;
			got = true;
			g.face_grin = 30;
		}
		else if (kind == 9) {
			g.have[2] = 1;
			::give_ammo(2, 40);
			g.weapon = 2;
			g.cooldown = 8;
			got = true;
			g.face_grin = 30;
		}
		else if (kind == 10) { g.hp = ::imin(150, g.hp + 40); got = true; g.face_grin = 20; }
		if (got) {
			it[3] = 1;
			::show_msg("picked up " + ::ITEM_DEFS[kind].name, "93");
		}
	}
}
