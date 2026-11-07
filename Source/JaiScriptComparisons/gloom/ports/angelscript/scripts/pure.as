// The reference's "pure parallel bodies" in their serial-equivalent form
// (ports run serial loops always; outputs are defined identical). The DDA
// reads the tile array directly through TILE_TO_KIND; the reference's
// bit-packed snapshot exists only for its parallel value-isolation contract.

// One wall ray: standard DDA over the tile grid. Out: perpendicular distance,
// side (0 x-face, 1 y-face), render kind, texture column 0..63.
void gloom_ray(double posx, double posy, double rdx, double rdy,
		double &out pdist, int &out side, int &out kind, int &out texcol) {
	array<int>@ tiles = G.tiles;
	int mw = G.mw;
	int mx = int(posx);               // truncation; coords stay positive
	int my = int(posy);
	double adx = rdx < 0.0 ? -rdx : rdx;
	double ady = rdy < 0.0 ? -rdy : rdy;
	double ddx = adx < (1.0 / 100000000.0) ? 100000000.0 : 1.0 / adx;
	double ddy = ady < (1.0 / 100000000.0) ? 100000000.0 : 1.0 / ady;
	int stepx = rdx < 0.0 ? -1 : 1;
	int stepy = rdy < 0.0 ? -1 : 1;
	double sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
	double sdy = rdy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
	side = 0;
	kind = 1;
	int guard = 0;
	bool hit = false;
	while (!hit && guard < 128) {
		if (sdx < sdy) {
			sdx += ddx;
			mx += stepx;
			side = 0;
		} else {
			sdy += ddy;
			my += stepy;
			side = 1;
		}
		int c = TILE_TO_KIND[tiles[my * mw + mx]];
		if (c != 0) {
			hit = true;
			kind = c;
		}
		guard = guard + 1;
	}
	pdist = side == 0 ? sdx - ddx : sdy - ddy;
	pdist = pdist < (2.0 / 100.0) ? (2.0 / 100.0) : pdist;
	double wallx = side == 0 ? posy + pdist * rdy : posx + pdist * rdx;
	wallx = wallx - floor(wallx);
	texcol = int(wallx * 64.0);
}

// One particle step at the fixed timestep (TICK baked in as a literal, like
// the reference). z is height in [0..~1.2], 0 = floor. Pure ballistic/drag
// updates; walls clip particles visually through the z-buffer at render time.
void gloom_particle_step(Particle@ p) {
	int kind = p.kind;
	if (kind == 0) { return; }
	double x = p.x;
	double y = p.y;
	double z = p.z;
	double vx = p.vx;
	double vy = p.vy;
	double vz = p.vz;
	int life = p.life;
	life = life - 1;
	if (life <= 0) {
		p.kind = 0;
		p.x = 0.0;
		p.y = 0.0;
		p.z = 0.0;
		p.vx = 0.0;
		p.vy = 0.0;
		p.vz = 0.0;
		p.life = 0;
		return;
	}
	if (kind == 1) {                       // spark: fast, hard gravity
		vz = vz - (3.0 / 10.0);
		vx = vx * (9.0 / 10.0);
		vy = vy * (9.0 / 10.0);
	} else if (kind == 2) {                // blood: droops
		vz = vz - (22.0 / 100.0);
		vx = vx * (94.0 / 100.0);
		vy = vy * (94.0 / 100.0);
	} else if (kind == 3) {                // gib: ballistic, bounces once-ish
		vz = vz - (16.0 / 100.0);
	} else if (kind == 4) {                // smoke: rises, drifts
		vz = vz * (9.0 / 10.0) + (12.0 / 1000.0);
		vx = vx * (96.0 / 100.0);
		vy = vy * (96.0 / 100.0);
	} else if (kind == 5) {                // muzzle/impact flash: hangs still
		vx = vx * (5.0 / 10.0);
		vy = vy * (5.0 / 10.0);
	} else {                               // hexmote: swirly float
		vz = vz * (98.0 / 100.0) + (4.0 / 1000.0);
		vx = vx * (97.0 / 100.0);
		vy = vy * (97.0 / 100.0);
	}
	x = x + vx * (33333333.0 / 1000000000.0);
	y = y + vy * (33333333.0 / 1000000000.0);
	z = z + vz * (33333333.0 / 1000000000.0);
	if (z < (2.0 / 100.0)) {
		z = (2.0 / 100.0);
		if (kind == 3 && (vz < -(5.0 / 10.0) || vz > (5.0 / 10.0))) {
			vz = 0.0 - vz * (45.0 / 100.0);
		} else {
			vz = 0.0;
		}
		if (kind == 1 || kind == 2) { life = life < 4 ? life : 4; }
	}
	p.x = x;
	p.y = y;
	p.z = z;
	p.vx = vx;
	p.vy = vy;
	p.vz = vz;
	p.life = life;
}
