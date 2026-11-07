// Combat: line of sight, hitscan weapons, projectiles, explosions, damage.

// stepped-ray LOS on the tile grid (deterministic float march)
bool los_clear(double ax, double ay, double bx, double by) {
	double dx = bx - ax;
	double dy = by - ay;
	double length = sqrt(dx * dx + dy * dy);
	if (length < (1.0 / 1000.0)) { return true; }
	double inv = 1.0 / length;
	dx = dx * inv;
	dy = dy * inv;
	double t = (15.0 / 100.0);
	while (t < length) {
		if (tile_solid(tile_at(ifloor(ax + dx * t), ifloor(ay + dy * t)))) { return false; }
		t += (15.0 / 100.0);
	}
	return true;
}

// wall distance along a unit ray: the renderer's DDA (perpendicular == euclidean for a unit ray)
double wall_dist(double ax, double ay, double dx, double dy) {
	double result;
	int side;
	int kind;
	int texcol;
	gloom_ray(ax, ay, dx, dy, result, side, kind, texcol);
	return result;
}

// nearest live enemy along a unit ray, cross-checked against the wall
bool hitscan(double ax, double ay, double dx, double dy, int lo, int hi) {
	double wd = wall_dist(ax, ay, dx, dy);
	Enemy@ best;
	double best_t = 999.0;
	for (uint i = 0; i < G.enemies.length(); ++i) {
		Enemy@ e = G.enemies[i];
		if (!e.alive) { continue; }
		double rx = e.x - ax;
		double ry = e.y - ay;
		double t = rx * dx + ry * dy;
		if (t >= (2.0 / 10.0) && t <= wd + e.radius && t < best_t) {
			double perp = rx * dy - ry * dx;
			if (perp < 0.0) { perp = 0.0 - perp; }
			if (perp < e.radius + (12.0 / 100.0)) {
				@best = e;
				best_t = t;
			}
		}
	}
	if (best !is null) {
		best.hurt(int(RNG.roll(lo, hi)));
		return true;
	}
	// wall impact garnish just short of the surface
	fx_wall_hit(ax + dx * (wd - (8.0 / 100.0)), ay + dy * (wd - (8.0 / 100.0)));
	return false;
}

// turret bolt: instant, accuracy falls off with range
void turret_shot(double tx, double ty) {
	double dx = G.px - tx;
	double dy = G.py - ty;
	double d = sqrt(dx * dx + dy * dy);
	if (d < (1.0 / 1000.0)) { return; }
	fx_muzzle(tx, ty, dx / d, dy / d);
	int hit_pct = imax(12, 66 - itrunc(d * 5.0));
	if (RNG.chance(hit_pct) && los_clear(tx, ty, G.px, G.py)) {
		damage_player(int(RNG.roll(2, 6)), "turret fire");
	} else {
		// tracer smacks the wall behind you
		double wd = wall_dist(tx, ty, dx / d, dy / d) - (1.0 / 10.0);
		double t = d + (15.0 / 10.0);
		if (t > wd) { t = wd; }
		fx_wall_hit(tx + dx / d * t, ty + dy / d * t);
	}
}

// ------------------------------------------------------------ projectiles ---
// kind 1 spitter gob | 2 warden hollow fire | 3 player hex bolt
class Shot {
	int kind;
	double x;
	double y;
	double vx;
	double vy;
	int ttl;

	Shot(int kind_, double x_, double y_, double vx_, double vy_, int ttl_) {
		kind = kind_;
		x = x_;
		y = y_;
		vx = vx_;
		vy = vy_;
		ttl = ttl_;
	}
}

void spawn_shot(int kind, double ax, double ay, double tx, double ty, double speed) {
	double dx = tx - ax;
	double dy = ty - ay;
	double length = sqrt(dx * dx + dy * dy);
	if (length < (1.0 / 1000.0)) { return; }
	G.shots.insertLast(Shot(kind, ax + dx / length * (4.0 / 10.0), ay + dy / length * (4.0 / 10.0), dx / length * speed, dy / length * speed, 150));
}

void spawn_shot_dir(int kind, double ax, double ay, double dx, double dy, double speed) {
	G.shots.insertLast(Shot(kind, ax + dx * (45.0 / 100.0), ay + dy * (45.0 / 100.0), dx * speed, dy * speed, 150));
}

void update_shots() {
	array<Shot@> kept;
	for (uint si = 0; si < G.shots.length(); ++si) {
		Shot@ s = G.shots[si];
		int kind = s.kind;
		double x = s.x;
		double y = s.y;
		double vx = s.vx;
		double vy = s.vy;
		int ttl = s.ttl - 1;
		bool dead = ttl <= 0;
		// 3 substeps so fast bolts don't tunnel tile corners
		for (int sub = 0; sub < 3 && !dead; ++sub) {
			x += vx * TICK * (333333333.0 / 1000000000.0);
			y += vy * TICK * (333333333.0 / 1000000000.0);
			if (tile_solid(tile_at(ifloor(x), ifloor(y)))) {
				if (kind == 3) { explode_hex(x - vx * (2.0 / 100.0), y - vy * (2.0 / 100.0)); }
				else if (kind == 2) { explode_fire(x - vx * (2.0 / 100.0), y - vy * (2.0 / 100.0)); }
				else { fx_wall_hit(x - vx * (2.0 / 100.0), y - vy * (2.0 / 100.0)); }
				dead = true;
				break;
			}
			if (kind == 3) {
				// player bolt vs enemies
				for (uint i = 0; i < G.enemies.length(); ++i) {
					Enemy@ e = G.enemies[i];
					if (!e.alive) { continue; }
					if (dist2(x, y, e.x, e.y) < (e.radius + (2.0 / 10.0)) * (e.radius + (2.0 / 10.0))) {
						explode_hex(x, y);
						dead = true;
						break;
					}
				}
			} else {
				// enemy shot vs player
				if (dist2(x, y, G.px, G.py) < (14.0 / 100.0)) {
					if (kind == 2) {
						explode_fire(x, y);
					} else {
						damage_player(int(RNG.roll(4, 9)), "a caustic gob");
						fx_blood(x, y, (5.0 / 10.0), 3);
					}
					dead = true;
					break;
				}
			}
		}
		if (!dead) {
			s.x = x;
			s.y = y;
			s.ttl = ttl;
			kept.insertLast(s);
		}
	}
	G.shots = kept;
	// glow trail
	for (uint i2 = 0; i2 < G.shots.length(); ++i2) {
		Shot@ s2 = G.shots[i2];
		int k2 = s2.kind;
		if ((G.tick + int(i2)) % 2 == 0) {
			G.fx.spawn(k2 == 1 ? 6 : (k2 == 2 ? 1 : 6), s2.x, s2.y, (5.0 / 10.0), 0.0, 0.0, 0.0, 4);
		}
	}
}

void explode_hex(double x, double y) {
	fx_explosion(x, y);
	G.noise = 20;
	for (uint i = 0; i < G.enemies.length(); ++i) {
		Enemy@ e = G.enemies[i];
		if (!e.alive) { continue; }
		double d2 = dist2(x, y, e.x, e.y);
		if (d2 < (324.0 / 100.0)) {                              // radius 1.8
			int dmg = itrunc(42.0 - sqrt(d2) * 14.0);
			if (dmg > 0) { e.hurt(dmg); }
		}
	}
	// standing in your own spell is a choice
	double pd2 = dist2(x, y, G.px, G.py);
	if (pd2 < (225.0 / 100.0)) {
		int self = itrunc(20.0 - sqrt(pd2) * 10.0);
		if (self > 0) { damage_player(self, "your own hex"); }
	}
}

void explode_fire(double x, double y) {
	fx_explosion(x, y);
	double pd2 = dist2(x, y, G.px, G.py);
	if (pd2 < (289.0 / 100.0)) {                                 // radius 1.7
		int dmg = itrunc(26.0 - sqrt(pd2) * 9.0);
		if (dmg > 0) { damage_player(dmg, "hollow fire"); }
	}
}

// ------------------------------------------------------------ the player ----
void damage_player(int dmg, const string &in src) {
	if (G.mode != 1 || dmg <= 0) { return; }
	if (HOST_GOD) { return; }
	int absorbed = imin(G.armor, dmg * 2 / 3);
	G.armor = G.armor - absorbed;
	int taken = dmg - absorbed;
	G.hp = G.hp - taken;
	G.face_pain = 12;
	G.hurt_flash = 4;
	show_msg("-" + taken + " hp (" + src + ")", "91");
	if (G.hp <= 0) {
		G.hp = 0;
		G.mode = 3;
		G.mode_t = 0;
		G.death_cause = src;
		show_msg("you died", "91");
	}
}

void player_fire() {
	if (G.cooldown > 0) { return; }
	WeaponDef@ w = WEAPONS[G.weapon];
	int ammo_type = w.ammo;
	if (G.ammo[ammo_type] <= 0) {
		show_msg("out of " + AMMO_NAMES[ammo_type] + "S", "93");
		G.cooldown = 8;
		return;
	}
	G.ammo[ammo_type] = G.ammo[ammo_type] - 1;
	G.cooldown = w.cd;
	G.muzzle = 3;
	G.noise = 24;
	G.gun_kick = 4;
	double dirx = cos(G.pang);
	double diry = sin(G.pang);
	fx_muzzle(G.px, G.py, dirx, diry);
	if (w.kind == 1) {
		spawn_shot_dir(3, G.px, G.py, dirx, diry, 9.0);
		return;
	}
	for (int i = 0; i < w.pellets; ++i) {
		double a = G.pang + (RNG.nextf() - (5.0 / 10.0)) * 2.0 * w.spread;
		hitscan(G.px, G.py, cos(a), sin(a), w.lo, w.hi);
	}
}
