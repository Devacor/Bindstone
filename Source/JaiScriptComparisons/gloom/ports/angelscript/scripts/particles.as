// Particle pool: 288 fixed Particle slots, round-robin cursor overwrite,
// stepped in place by gloom_particle_step (pure.as) in slot order.

const int PART_MAX = 288;

// kind 0 dead | 1 spark | 2 blood | 3 gib | 4 smoke | 5 flash | 6 hexmote
class Particle {
	int kind = 0;
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
	double vx = 0.0;
	double vy = 0.0;
	double vz = 0.0;
	int life = 0;
}

class ParticlePool {
	array<Particle@> pool;
	private int cursor = 0;

	ParticlePool() { reset_pool(); }

	void reset_pool() {
		pool.resize(0);
		for (int i = 0; i < PART_MAX; ++i) { pool.insertLast(Particle()); }
		cursor = 0;
	}

	void spawn(int kind, double x, double y, double z, double vx, double vy, double vz, int life) {
		Particle@ p = pool[cursor];
		p.kind = kind;
		p.x = x;
		p.y = y;
		p.z = z;
		p.vx = vx;
		p.vy = vy;
		p.vz = vz;
		p.life = life;
		cursor = (cursor + 1) % PART_MAX;
	}

	void update() {
		for (int i = 0; i < PART_MAX; ++i) { gloom_particle_step(pool[i]); }
	}
}

// ------------------------------------------------------------ burst kit -----
// All velocity spreads come from RNG (the seeded host rng): deterministic.
// AngelScript evaluates call arguments last to first, so every RNG draw is
// taken into a local in the reference's left-to-right order before the call.
double rnd_spread(double scale) {
	return (RNG.nextf() - (5.0 / 10.0)) * 2.0 * scale;
}

void fx_muzzle(double x, double y, double dirx, double diry) {
	double mx = x + dirx * (5.0 / 10.0);
	double my = y + diry * (5.0 / 10.0);
	G.fx.spawn(5, mx, my, (52.0 / 100.0), dirx * 2.0, diry * 2.0, (2.0 / 10.0), 3);
	for (int i = 0; i < 3; ++i) {
		double vx = dirx * 3.0 + rnd_spread((16.0 / 10.0));
		double vy = diry * 3.0 + rnd_spread((16.0 / 10.0));
		double vz = (6.0 / 10.0) + RNG.nextf();
		G.fx.spawn(1, mx, my, (5.0 / 10.0), vx, vy, vz, 6);
	}
	G.fx.spawn(4, mx, my, (55.0 / 100.0), dirx * (6.0 / 10.0), diry * (6.0 / 10.0), (3.0 / 10.0), 16);
}

void fx_wall_hit(double x, double y) {
	for (int i = 0; i < 5; ++i) {
		double z = (45.0 / 100.0) + RNG.nextf() * (2.0 / 10.0);
		double vx = rnd_spread((22.0 / 10.0));
		double vy = rnd_spread((22.0 / 10.0));
		double vz = (8.0 / 10.0) + RNG.nextf() * (14.0 / 10.0);
		G.fx.spawn(1, x, y, z, vx, vy, vz, 8);
	}
	double sx = rnd_spread((3.0 / 10.0));
	double sy = rnd_spread((3.0 / 10.0));
	G.fx.spawn(4, x, y, (5.0 / 10.0), sx, sy, (4.0 / 10.0), 14);
}

void fx_blood(double x, double y, double z, int n) {
	for (int i = 0; i < n; ++i) {
		double pz = z + RNG.nextf() * (25.0 / 100.0);
		double vx = rnd_spread((18.0 / 10.0));
		double vy = rnd_spread((18.0 / 10.0));
		double vz = (5.0 / 10.0) + RNG.nextf() * (16.0 / 10.0);
		G.fx.spawn(2, x, y, pz, vx, vy, vz, 12);
	}
}

// death burst: meat + smoke; big enemies gib harder
void fx_gibs(double x, double y, int n) {
	for (int i = 0; i < n; ++i) {
		double z = (35.0 / 100.0) + RNG.nextf() * (3.0 / 10.0);
		double vx = rnd_spread((26.0 / 10.0));
		double vy = rnd_spread((26.0 / 10.0));
		double vz = (12.0 / 10.0) + RNG.nextf() * (24.0 / 10.0);
		G.fx.spawn(3, x, y, z, vx, vy, vz, 26);
	}
	for (int i = 0; i < 3; ++i) {
		double vx = rnd_spread((5.0 / 10.0));
		double vy = rnd_spread((5.0 / 10.0));
		G.fx.spawn(4, x, y, (4.0 / 10.0), vx, vy, (5.0 / 10.0), 20);
	}
}

void fx_explosion(double x, double y) {
	G.fx.spawn(5, x, y, (5.0 / 10.0), 0.0, 0.0, 0.0, 5);
	for (int i = 0; i < 10; ++i) {
		double a = RNG.nextf() * (62831853.0 / 10000000.0);
		double sp = (15.0 / 10.0) + RNG.nextf() * 3.0;
		double z = (3.0 / 10.0) + RNG.nextf() * (5.0 / 10.0);
		double vz = 1.0 + RNG.nextf() * 2.0;
		G.fx.spawn(1, x, y, z, cos(a) * sp, sin(a) * sp, vz, 11);
	}
	for (int i = 0; i < 5; ++i) {
		double vx = rnd_spread((9.0 / 10.0));
		double vy = rnd_spread((9.0 / 10.0));
		double vz = (6.0 / 10.0) + RNG.nextf();
		G.fx.spawn(4, x, y, (4.0 / 10.0), vx, vy, vz, 24);
	}
	for (int i = 0; i < 6; ++i) {
		double z = (2.0 / 10.0) + RNG.nextf() * (6.0 / 10.0);
		double vx = rnd_spread((14.0 / 10.0));
		double vy = rnd_spread((14.0 / 10.0));
		G.fx.spawn(6, x, y, z, vx, vy, (5.0 / 10.0), 20);
	}
}

void fx_door_puff(double x, double y) {
	for (int i = 0; i < 4; ++i) {
		double z = (3.0 / 10.0) + RNG.nextf() * (5.0 / 10.0);
		double vx = rnd_spread((7.0 / 10.0));
		double vy = rnd_spread((7.0 / 10.0));
		G.fx.spawn(4, x, y, z, vx, vy, (3.0 / 10.0), 18);
	}
}

void fx_secret_glitter(double x, double y) {
	for (int i = 0; i < 8; ++i) {
		double z = (2.0 / 10.0) + RNG.nextf() * (8.0 / 10.0);
		double vx = rnd_spread((12.0 / 10.0));
		double vy = rnd_spread((12.0 / 10.0));
		G.fx.spawn(6, x, y, z, vx, vy, (6.0 / 10.0), 24);
	}
}

// ------------------------------------------------------------- colors -------
// per-kind life-phase ramps (render reads PART_COLS[kind * 4 + phase]; phase 0 = fresh)
array<int> PART_COLS;

void build_particle_colors() {
	const array<int> ramps = {
		0, 0, 0,  0, 0, 0,  0, 0, 0,  0, 0, 0,                          // 0 dead (unused)
		255, 240, 160,  255, 200, 90,  230, 130, 50,  140, 70, 30,      // 1 spark
		210, 40, 30,  180, 34, 26,  140, 26, 20,  95, 18, 14,           // 2 blood
		200, 60, 44,  170, 46, 34,  130, 34, 26,  90, 24, 18,           // 3 gib
		130, 126, 122,  104, 102, 100,  80, 79, 78,  56, 56, 57,        // 4 smoke
		255, 252, 220,  255, 236, 160,  255, 190, 90,  200, 120, 50,    // 5 flash
		150, 255, 170,  110, 235, 200,  90, 190, 250,  80, 120, 220     // 6 hexmote
	};
	PART_COLS.resize(0);
	for (uint i = 0; i < ramps.length(); i += 3) {
		PART_COLS.insertLast(rgb_idx(ramps[i], ramps[i + 1], ramps[i + 2]));
	}
}
