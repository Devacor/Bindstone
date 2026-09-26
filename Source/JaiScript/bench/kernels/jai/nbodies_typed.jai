include "profile.jai";

float PI = 3.141592653589793;
float SOLAR_MASS = 4.0 * PI * PI;
float DAYS_PER_YEAR = 365.24;

class Body {
	float x = 0.0;
	float y = 0.0;
	float z = 0.0;
	float vx = 0.0;
	float vy = 0.0;
	float vz = 0.0;
	float mass = 0.0;
	Body(float px, float py, float pz, float pvx, float pvy, float pvz, float pmass) {
		x = px; y = py; z = pz; vx = pvx; vy = pvy; vz = pvz; mass = pmass;
	}
}

array<Body> make_bodies() {
	array<Body> b = [];
	b.push(Body(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, SOLAR_MASS));
	b.push(Body(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
		1.66007664274403694e-03 * DAYS_PER_YEAR, 7.69901118419740425e-03 * DAYS_PER_YEAR, -6.90460016972063023e-05 * DAYS_PER_YEAR,
		9.54791938424326609e-04 * SOLAR_MASS));
	b.push(Body(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
		-2.76742510726862411e-03 * DAYS_PER_YEAR, 4.99852801234917238e-03 * DAYS_PER_YEAR, 2.30417297573763929e-05 * DAYS_PER_YEAR,
		2.85885980666130812e-04 * SOLAR_MASS));
	b.push(Body(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
		2.96460137564761618e-03 * DAYS_PER_YEAR, 2.37847173959480950e-03 * DAYS_PER_YEAR, -2.96589568540237556e-05 * DAYS_PER_YEAR,
		4.36624404335156298e-05 * SOLAR_MASS));
	b.push(Body(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
		2.68067772490389322e-03 * DAYS_PER_YEAR, 1.62824170038242295e-03 * DAYS_PER_YEAR, -9.51592254519715870e-05 * DAYS_PER_YEAR,
		5.15138902046611451e-05 * SOLAR_MASS));
	return b;
}

void advance(array<Body>& bodies, int nbody) {
	for (int i = 0; i < nbody; ++i) {
		Body& bi = bodies[i];
		float bix = bi.x;
		float biy = bi.y;
		float biz = bi.z;
		float bimass = bi.mass;
		float bivx = bi.vx;
		float bivy = bi.vy;
		float bivz = bi.vz;
		for (int j = i + 1; j < nbody; ++j) {
			Body& bj = bodies[j];
			float dx = bix - bj.x;
			float dy = biy - bj.y;
			float dz = biz - bj.z;
			float distance = sqrt(dx * dx + dy * dy + dz * dz);
			float mag = 1.0 / (distance * distance * distance);
			float bim = bimass * mag;
			float bjm = bj.mass * mag;
			bivx = bivx - (dx * bjm);
			bivy = bivy - (dy * bjm);
			bivz = bivz - (dz * bjm);
			bj.vx = bj.vx + (dx * bim);
			bj.vy = bj.vy + (dy * bim);
			bj.vz = bj.vz + (dz * bim);
		}
		bi.vx = bivx;
		bi.vy = bivy;
		bi.vz = bivz;
	}
	for (int i = 0; i < nbody; ++i) {
		Body& bi = bodies[i];
		bi.x = bi.x + bi.vx;
		bi.y = bi.y + bi.vy;
		bi.z = bi.z + bi.vz;
	}
}

float energy(array<Body>& bodies, int nbody) {
	float e = 0.0;
	for (int i = 0; i < nbody; ++i) {
		Body& bi = bodies[i];
		float vx = bi.vx;
		float vy = bi.vy;
		float vz = bi.vz;
		float bim = bi.mass;
		e = e + (0.5 * bim * (vx * vx + vy * vy + vz * vz));
		for (int j = i + 1; j < nbody; ++j) {
			Body& bj = bodies[j];
			float dx = bi.x - bj.x;
			float dy = bi.y - bj.y;
			float dz = bi.z - bj.z;
			float distance = sqrt(dx * dx + dy * dy + dz * dz);
			e = e - ((bim * bj.mass) / distance);
		}
	}
	return e;
}

void scale_bodies(array<Body>& bodies, int nbody, float scale) {
	for (int i = 0; i < nbody; ++i) {
		Body& b = bodies[i];
		b.mass = b.mass * scale * scale;
		b.vx = b.vx * scale;
		b.vy = b.vy * scale;
		b.vz = b.vz * scale;
	}
}

void offset_momentum(array<Body>& b, int nbody) {
	float px = 0.0;
	float py = 0.0;
	float pz = 0.0;
	for (int i = 0; i < nbody; ++i) {
		Body& bi = b[i];
		float bim = bi.mass;
		px = px + (bi.vx * bim);
		py = py + (bi.vy * bim);
		pz = pz + (bi.vz * bim);
	}
	b[0].vx = -px / SOLAR_MASS;
	b[0].vy = -py / SOLAR_MASS;
	b[0].vz = -pz / SOLAR_MASS;
}

void simulate(int n, array<Body>& bodies, int nbody) {
	scale_bodies(bodies, nbody, 0.01);
	for (int i = 1; i <= n; ++i) { advance(bodies, nbody); }
	scale_bodies(bodies, nbody, 1.0 / 0.01);
}

int N = 33000;

array<Body> check = make_bodies();
offset_momentum(check, 5);
simulate(N, check, 5);
float checksum = energy(check, 5);

array<Body> bodies = make_bodies();
offset_momentum(bodies, 5);
profile_it("n-bodies", []() { simulate(N, bodies, 5); });
print("checksum {:.9f}", checksum);
