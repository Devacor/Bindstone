// PURE parallel bodies. Everything in this file must pass the parallel
// admission walk (docs/parallel_design.md): each function touches ONLY its one
// element parameter and locals — no globals, no member access, no host calls
// beyond the math whitelist. The same functions run serially when
// HOST_WORKERS == 0, which is the 1-vs-N determinism check by construction.

// A CHUNK of wall rays. The pure-value contract means shared inputs (the map
// snapshot) ride inside every element, so elements are column RANGES — the
// snapshot is duplicated once per chunk, not once per column. Element:
//   [x0, x1, vw, posx, posy, dirx, diry, planex, planey, mw, pack0, ...]
// pack = tile snapshot, 15 cells x 4 bits per int64 (bits 60+ unused: no
// sign-bit traps under checked overflow). Returns 4 values per column, flat:
// [dist, side, kind, texcol, dist, side, ...].
function gloom_ray_chunk(var t) -> var {
	int x0 = t[0];
	int x1 = t[1];
	int vw = t[2];
	float posx = t[3];
	float posy = t[4];
	float dirx = t[5];
	float diry = t[6];
	float planex = t[7];
	float planey = t[8];
	int mw = t[9];
	float xstep = 2.0 / (vw - 1.0);
	var out = [];
	for (int x = x0; x < x1; ++x) {
		float camx = x * xstep - 1.0;
		float rdx = dirx + planex * camx;
		float rdy = diry + planey * camx;
		int mx = posx;
		int my = posy;
		float adx = rdx < 0.0 ? -rdx : rdx;
		float ady = rdy < 0.0 ? -rdy : rdy;
		float ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
		float ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
		int stepx = rdx < 0.0 ? -1 : 1;
		int stepy = rdy < 0.0 ? -1 : 1;
		float sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
		float sdy = rdy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
		int side = 0;
		int kind = 1;
		int guard = 0;
		bool hit = false;
		while (!hit && guard < 128) {
			if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
			else { sdy += ddy; my += stepy; side = 1; }
			int cell = my * mw + mx;
			int word = t[10 + cell / 15];
			int c = (word >> ((cell % 15) * 4)) & 15;
			if (c != 0) {
				hit = true;
				kind = c;
			}
			guard = guard + 1;
		}
		float pdist = side == 0 ? sdx - ddx : sdy - ddy;
		pdist = pdist < 0.02 ? 0.02 : pdist;
		float wallx = side == 0 ? posy + pdist * rdy : posx + pdist * rdx;
		wallx = wallx - floor(wallx);
		int texcol = wallx * 64.0;
		out.push(pdist);
		out.push(side);
		out.push(kind);
		out.push(texcol);
	}
	return out;
}

// One wall ray (the serial helper for hitscan/tracers; ray_record builds its
// element). Same DDA as the chunk body. Element: [posx, posy, rdx, rdy, mw,
// pack0, ...]. Returns [perp_dist, side, kind, texcol 0..63].
function gloom_ray(var t) -> var {
	float posx = t[0];
	float posy = t[1];
	float rdx = t[2];
	float rdy = t[3];
	int mw = t[4];
	int mx = posx;                    // typed-local truncation; coords stay positive
	int my = posy;
	float adx = rdx < 0.0 ? -rdx : rdx;
	float ady = rdy < 0.0 ? -rdy : rdy;
	float ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
	float ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
	int stepx = rdx < 0.0 ? -1 : 1;
	int stepy = rdy < 0.0 ? -1 : 1;
	float sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
	float sdy = rdy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
	int side = 0;
	int kind = 1;
	int guard = 0;
	bool hit = false;
	while (!hit && guard < 128) {
		if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
		else { sdy += ddy; my += stepy; side = 1; }
		int cell = my * mw + mx;
		int word = t[5 + cell / 15];
		int c = (word >> ((cell % 15) * 4)) & 15;
		if (c != 0) {
			hit = true;
			kind = c;
		}
		guard = guard + 1;
	}
	float pdist = side == 0 ? sdx - ddx : sdy - ddy;
	pdist = pdist < 0.02 ? 0.02 : pdist;
	float wallx = side == 0 ? posy + pdist * rdy : posx + pdist * rdx;
	wallx = wallx - floor(wallx);
	int texcol = wallx * 64.0;
	return [pdist, side, kind, texcol];
}

// One particle step at the fixed timestep (TICK baked in as a literal — the
// admission walk forbids reading enclosing state, and the sim is fixed-step
// anyway). Element: [kind, x, y, z, vx, vy, vz, life]
//   kind 0 dead | 1 spark | 2 blood | 3 gib | 4 smoke | 5 flash | 6 hexmote
// z is height in [0..~1.2], 0 = floor. Pure ballistic/drag updates; collision
// with WALLS is intentionally skipped (particles are garnish, walls clip them
// visually via the z-buffer at render time).
function gloom_particle(var p) -> var {
	int kind = p[0];
	if (kind == 0) { return p; }
	float x = p[1];
	float y = p[2];
	float z = p[3];
	float vx = p[4];
	float vy = p[5];
	float vz = p[6];
	int life = p[7];
	life = life - 1;
	if (life <= 0) { return [0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0]; }
	if (kind == 1) {                       // spark: fast, hard gravity
		vz = vz - 0.30;
		vx = vx * 0.90;
		vy = vy * 0.90;
	} else if (kind == 2) {                // blood: droops
		vz = vz - 0.22;
		vx = vx * 0.94;
		vy = vy * 0.94;
	} else if (kind == 3) {                // gib: ballistic, bounces once-ish
		vz = vz - 0.16;
	} else if (kind == 4) {                // smoke: rises, drifts
		vz = vz * 0.90 + 0.012;
		vx = vx * 0.96;
		vy = vy * 0.96;
	} else if (kind == 5) {                // muzzle/impact flash: hangs still
		vx = vx * 0.5;
		vy = vy * 0.5;
	} else {                               // hexmote: swirly float
		vz = vz * 0.98 + 0.004;
		vx = vx * 0.97;
		vy = vy * 0.97;
	}
	x = x + vx * 0.033333333;
	y = y + vy * 0.033333333;
	z = z + vz * 0.033333333;
	if (z < 0.02) {
		z = 0.02;
		if (kind == 3 && (vz < -0.5 || vz > 0.5)) { vz = 0.0 - vz * 0.45; }
		else { vz = 0.0; }
		if (kind == 1 || kind == 2) { life = life < 4 ? life : 4; }
	}
	return [kind, x, y, z, vx, vy, vz, life];
}
