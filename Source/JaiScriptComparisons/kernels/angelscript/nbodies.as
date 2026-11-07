#include "profile.as"

const double PI = 3.141592653589793;
const double SOLAR_MASS = 4 * PI * PI;
const double DAYS_PER_YEAR = 365.24;

class Body {
	double x;
	double y;
	double z;
	double vx;
	double vy;
	double vz;
	double mass;
	Body(double a_x, double a_y, double a_z, double a_vx, double a_vy, double a_vz, double a_mass) {
		x = a_x;
		y = a_y;
		z = a_z;
		vx = a_vx;
		vy = a_vy;
		vz = a_vz;
		mass = a_mass;
	}
}

array<Body@>@ make_bodies() {
	array<Body@> result = {
		Body(0, 0, 0, 0, 0, 0, SOLAR_MASS),
		Body(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
			1.66007664274403694e-03 * DAYS_PER_YEAR, 7.69901118419740425e-03 * DAYS_PER_YEAR, -6.90460016972063023e-05 * DAYS_PER_YEAR,
			9.54791938424326609e-04 * SOLAR_MASS),
		Body(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
			-2.76742510726862411e-03 * DAYS_PER_YEAR, 4.99852801234917238e-03 * DAYS_PER_YEAR, 2.30417297573763929e-05 * DAYS_PER_YEAR,
			2.85885980666130812e-04 * SOLAR_MASS),
		Body(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
			2.96460137564761618e-03 * DAYS_PER_YEAR, 2.37847173959480950e-03 * DAYS_PER_YEAR, -2.96589568540237556e-05 * DAYS_PER_YEAR,
			4.36624404335156298e-05 * SOLAR_MASS),
		Body(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
			2.68067772490389322e-03 * DAYS_PER_YEAR, 1.62824170038242295e-03 * DAYS_PER_YEAR, -9.51592254519715870e-05 * DAYS_PER_YEAR,
			5.15138902046611451e-05 * SOLAR_MASS)
	};
	return result;
}

void advance(array<Body@>& bodies, int nbody) {
	for (int i = 0; i < nbody; ++i) {
		Body@ bi = bodies[i];
		double bix = bi.x;
		double biy = bi.y;
		double biz = bi.z;
		double bimass = bi.mass;
		double bivx = bi.vx;
		double bivy = bi.vy;
		double bivz = bi.vz;
		for (int j = i + 1; j < nbody; ++j) {
			Body@ bj = bodies[j];
			double dx = bix - bj.x;
			double dy = biy - bj.y;
			double dz = biz - bj.z;
			double distance = sqrt(dx * dx + dy * dy + dz * dz);
			double mag = 1 / (distance * distance * distance);
			double bim = bimass * mag;
			double bjm = bj.mass * mag;
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
		Body@ bi = bodies[i];
		bi.x = bi.x + bi.vx;
		bi.y = bi.y + bi.vy;
		bi.z = bi.z + bi.vz;
	}
}

double energy(array<Body@>& bodies, int nbody) {
	double e = 0;
	for (int i = 0; i < nbody; ++i) {
		Body@ bi = bodies[i];
		double vx = bi.vx;
		double vy = bi.vy;
		double vz = bi.vz;
		double bim = bi.mass;
		e = e + (0.5 * bim * (vx * vx + vy * vy + vz * vz));
		for (int j = i + 1; j < nbody; ++j) {
			Body@ bj = bodies[j];
			double dx = bi.x - bj.x;
			double dy = bi.y - bj.y;
			double dz = bi.z - bj.z;
			double distance = sqrt(dx * dx + dy * dy + dz * dz);
			e = e - ((bim * bj.mass) / distance);
		}
	}
	return e;
}

void scale_bodies(array<Body@>& bodies, int nbody, double scale) {
	for (int i = 0; i < nbody; ++i) {
		Body@ b = bodies[i];
		b.mass = b.mass * scale * scale;
		b.vx = b.vx * scale;
		b.vy = b.vy * scale;
		b.vz = b.vz * scale;
	}
}

void offset_momentum(array<Body@>& b, int nbody) {
	double px = 0;
	double py = 0;
	double pz = 0;
	for (int i = 0; i < nbody; ++i) {
		Body@ bi = b[i];
		double bim = bi.mass;
		px = px + (bi.vx * bim);
		py = py + (bi.vy * bim);
		pz = pz + (bi.vz * bim);
	}
	b[0].vx = -px / SOLAR_MASS;
	b[0].vy = -py / SOLAR_MASS;
	b[0].vz = -pz / SOLAR_MASS;
}

void simulate(int n, array<Body@>& bodies, int nbody) {
	scale_bodies(bodies, nbody, 0.01);
	for (int i = 1; i <= n; ++i) {
		advance(bodies, nbody);
	}
	scale_bodies(bodies, nbody, 1 / 0.01);
}

const int STEPS = 33000;
array<Body@>@ bodies;

void main() {
	array<Body@>@ check = make_bodies();
	int nbody = check.length();
	offset_momentum(check, nbody);
	simulate(STEPS, check, nbody);
	double checksum = energy(check, nbody);

	@bodies = make_bodies();
	offset_momentum(bodies, nbody);
	profile_it("n-bodies", function() { simulate(STEPS, bodies, bodies.length()); });
	print("checksum " + formatFloat(checksum, "", 0, 9));
}
