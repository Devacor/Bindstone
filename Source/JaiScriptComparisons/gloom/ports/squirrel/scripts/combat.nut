// Combat: line of sight, hitscan weapons, projectiles, explosions, damage.

// stepped-ray LOS on the tile grid (deterministic float march)
function los_clear(ax, ay, bx, by) {
	local dx = bx - ax;
	local dy = by - ay;
	local len = ::sqrt(dx * dx + dy * dy);
	if (len < 0.001) { return true; }
	local inv = 1.0 / len;
	dx = dx * inv;
	dy = dy * inv;
	local t = 0.15;
	while (t < len) {
		if (::tile_solid(::tile_at(::ifloor(ax + dx * t), ::ifloor(ay + dy * t)))) { return false; }
		t += 0.15;
	}
	return true;
}

// wall distance along an arbitrary ray — the SAME DDA the renderer uses.
// dx/dy is unit here, so perpendicular == euclidean.
function wall_dist(ax, ay, dx, dy) {
	local hit = ::gloom_ray(ax, ay, dx, dy);
	return hit[0];
}

// nearest live enemy along a unit ray, cross-checked against the wall
function hitscan(ax, ay, dx, dy, lo, hi, from_player) {
	local g = ::G;
	local wd = ::wall_dist(ax, ay, dx, dy);
	local best = -1;
	local best_t = 999.0;
	foreach (i, e in g.enemies) {
		if (e.alive) {
			local rx = e.x - ax;
			local ry = e.y - ay;
			local t = rx * dx + ry * dy;
			if (t >= 0.2 && t <= wd + e.radius && t < best_t) {
				local perp = rx * dy - ry * dx;
				if (perp < 0.0) { perp = 0.0 - perp; }
				if (perp < e.radius + 0.12) {
					best = i;
					best_t = t;
				}
			}
		}
	}
	if (best >= 0) {
		g.enemies[best].hurt(::RNG.roll(lo, hi));
		return true;
	}
	// wall impact garnish just short of the surface
	::fx_wall_hit(ax + dx * (wd - 0.08), ay + dy * (wd - 0.08));
	return false;
}

// turret bolt: instant, accuracy falls off with range
function turret_shot(tx, ty) {
	local g = ::G;
	local dx = g.px - tx;
	local dy = g.py - ty;
	local d = ::sqrt(dx * dx + dy * dy);
	if (d < 0.001) { return; }
	::fx_muzzle(tx, ty, dx / d, dy / d);
	local hit_pct = ::imax(12, 66 - (d * 5.0).tointeger());
	if (::RNG.chance(hit_pct) && ::los_clear(tx, ty, g.px, g.py)) {
		::damage_player(::RNG.roll(2, 6), "turret fire");
	} else {
		// tracer smacks the wall behind you
		local wd = ::wall_dist(tx, ty, dx / d, dy / d) - 0.1;
		local t = d + 1.5;
		if (t > wd) { t = wd; }
		::fx_wall_hit(tx + dx / d * t, ty + dy / d * t);
	}
}

// ------------------------------------------------------------ projectiles ---
// value records: [kind, x, y, vx, vy, ttl]
//   kind 1 spitter gob | 2 warden hollow fire | 3 player hex bolt
function spawn_shot(kind, ax, ay, tx, ty, speed) {
	local dx = tx - ax;
	local dy = ty - ay;
	local len = ::sqrt(dx * dx + dy * dy);
	if (len < 0.001) { return; }
	::G.shots.push([kind, ax + dx / len * 0.4, ay + dy / len * 0.4, dx / len * speed, dy / len * speed, 150]);
}

function spawn_shot_dir(kind, ax, ay, dx, dy, speed) {
	::G.shots.push([kind, ax + dx * 0.45, ay + dy * 0.45, dx * speed, dy * speed, 150]);
}

function update_shots() {
	local g = ::G;
	local kept = [];
	for (local i = 0; i < g.shots.len(); ++i) {
		local s = g.shots[i];
		local kind = s[0];
		local x = s[1];
		local y = s[2];
		local vx = s[3];
		local vy = s[4];
		local ttl = s[5] - 1;
		local dead = ttl <= 0;
		// 3 substeps so fast bolts don't tunnel tile corners
		for (local sub = 0; sub < 3 && !dead; ++sub) {
			x += vx * ::TICK * 0.333333333;
			y += vy * ::TICK * 0.333333333;
			if (::tile_solid(::tile_at(::ifloor(x), ::ifloor(y)))) {
				if (kind == 3) { ::explode_hex(x - vx * 0.02, y - vy * 0.02); }
				else if (kind == 2) { ::explode_fire(x - vx * 0.02, y - vy * 0.02); }
				else { ::fx_wall_hit(x - vx * 0.02, y - vy * 0.02); }
				dead = true;
				break;
			}
			if (kind == 3) {
				// player bolt vs enemies
				foreach (e in g.enemies) {
					if (!e.alive) { continue; }
					if (::dist2(x, y, e.x, e.y) < (e.radius + 0.2) * (e.radius + 0.2)) {
						::explode_hex(x, y);
						dead = true;
						break;
					}
				}
			} else {
				// enemy shot vs player
				if (::dist2(x, y, g.px, g.py) < 0.14) {
					if (kind == 2) { ::explode_fire(x, y); }
					else { ::damage_player(::RNG.roll(4, 9), "a caustic gob"); ::fx_blood(x, y, 0.5, 3); }
					dead = true;
					break;
				}
			}
		}
		if (!dead) { kept.push([kind, x, y, vx, vy, ttl]); }
	}
	g.shots = kept;
	// glow trail
	for (local i2 = 0; i2 < g.shots.len(); ++i2) {
		local s2 = g.shots[i2];
		local k2 = s2[0];
		if ((g.tick + i2) % 2 == 0) {
			g.fx.spawn(k2 == 1 ? 6 : (k2 == 2 ? 1 : 6), s2[1], s2[2], 0.5, 0.0, 0.0, 0.0, 4);
		}
	}
}

function explode_hex(x, y) {
	local g = ::G;
	::fx_explosion(x, y);
	g.noise = 20;
	foreach (e in g.enemies) {
		if (!e.alive) { continue; }
		local d2 = ::dist2(x, y, e.x, e.y);
		if (d2 < 3.24) {                              // radius 1.8
			local dmg = (42.0 - ::sqrt(d2) * 14.0).tointeger();
			if (dmg > 0) { e.hurt(dmg); }
		}
	}
	// standing in your own spell is a choice
	local pd2 = ::dist2(x, y, g.px, g.py);
	if (pd2 < 2.25) {
		local self_dmg = (20.0 - ::sqrt(pd2) * 10.0).tointeger();
		if (self_dmg > 0) { ::damage_player(self_dmg, "your own hex"); }
	}
}

function explode_fire(x, y) {
	local g = ::G;
	::fx_explosion(x, y);
	local pd2 = ::dist2(x, y, g.px, g.py);
	if (pd2 < 2.89) {                                 // radius 1.7
		local dmg = (26.0 - ::sqrt(pd2) * 9.0).tointeger();
		if (dmg > 0) { ::damage_player(dmg, "hollow fire"); }
	}
}

// ------------------------------------------------------------ the player ----
function damage_player(dmg, src) {
	local g = ::G;
	if (g.mode != 1 || dmg <= 0) { return; }
	if (::HOST_GOD) { return; }
	local absorbed = ::imin(g.armor, dmg * 2 / 3);
	g.armor = g.armor - absorbed;
	local taken = dmg - absorbed;
	g.hp = g.hp - taken;
	g.face_pain = 12;
	g.hurt_flash = 4;
	::show_msg("-" + taken + " hp (" + src + ")", "91");
	if (g.hp <= 0) {
		g.hp = 0;
		g.mode = 3;
		g.mode_t = 0;
		g.death_cause = src;
		::show_msg("you died", "91");
	}
}

function player_fire() {
	local g = ::G;
	if (g.cooldown > 0) { return; }
	local w = ::WEAPONS[g.weapon];
	local ammo_type = w.ammo;
	if (g.ammo[ammo_type] <= 0) {
		::show_msg("out of " + ::AMMO_NAMES[ammo_type] + "S", "93");
		g.cooldown = 8;
		return;
	}
	g.ammo[ammo_type] = g.ammo[ammo_type] - 1;
	g.cooldown = w.cd;
	g.muzzle = 3;
	g.noise = 24;
	g.gun_kick = 4;
	local dirx = ::cos(g.pang);
	local diry = ::sin(g.pang);
	::fx_muzzle(g.px, g.py, dirx, diry);
	if (w.kind == 1) {
		::spawn_shot_dir(3, g.px, g.py, dirx, diry, 9.0);
		return;
	}
	local pellets = w.pellets;
	local spread = w.spread;
	for (local i = 0; i < pellets; ++i) {
		local a = g.pang + (::RNG.nextf() - 0.5) * 2.0 * spread;
		::hitscan(g.px, g.py, ::cos(a), ::sin(a), w.lo, w.hi, true);
	}
}
