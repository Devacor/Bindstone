include "profile.jai";

var PI = 3.141592653589793;
var SOLAR_MASS = 4.0 * PI * PI;
var DAYS_PER_YEAR = 365.24;

class Body {
	var x = 0.0;
	var y = 0.0;
	var z = 0.0;
	var vx = 0.0;
	var vy = 0.0;
	var vz = 0.0;
	var mass = 0.0;
	Body(var px, var py, var pz, var pvx, var pvy, var pvz, var pmass) {
		x = px; y = py; z = pz; vx = pvx; vy = pvy; vz = pvz; mass = pmass;
	}
}

function make_bodies() {
	var b = [];
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

function advance(var& bodies, var nbody) {
	for (var i = 0; i < nbody; ++i) {
		var& bi = bodies[i];
		var bix = bi.x;
		var biy = bi.y;
		var biz = bi.z;
		var bimass = bi.mass;
		var bivx = bi.vx;
		var bivy = bi.vy;
		var bivz = bi.vz;
		for (var j = i + 1; j < nbody; ++j) {
			var& bj = bodies[j];
			var dx = bix - bj.x;
			var dy = biy - bj.y;
			var dz = biz - bj.z;
			var distance = sqrt(dx * dx + dy * dy + dz * dz);
			var mag = 1.0 / (distance * distance * distance);
			var bim = bimass * mag;
			var bjm = bj.mass * mag;
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
	for (var i = 0; i < nbody; ++i) {
		var& bi = bodies[i];
		bi.x = bi.x + bi.vx;
		bi.y = bi.y + bi.vy;
		bi.z = bi.z + bi.vz;
	}
}

function energy(var& bodies, var nbody) {
	var e = 0.0;
	for (var i = 0; i < nbody; ++i) {
		var& bi = bodies[i];
		var vx = bi.vx;
		var vy = bi.vy;
		var vz = bi.vz;
		var bim = bi.mass;
		e = e + (0.5 * bim * (vx * vx + vy * vy + vz * vz));
		for (var j = i + 1; j < nbody; ++j) {
			var& bj = bodies[j];
			var dx = bi.x - bj.x;
			var dy = bi.y - bj.y;
			var dz = bi.z - bj.z;
			var distance = sqrt(dx * dx + dy * dy + dz * dz);
			e = e - ((bim * bj.mass) / distance);
		}
	}
	return e;
}

function scale_bodies(var& bodies, var nbody, var scale) {
	for (var i = 0; i < nbody; ++i) {
		var& b = bodies[i];
		b.mass = b.mass * scale * scale;
		b.vx = b.vx * scale;
		b.vy = b.vy * scale;
		b.vz = b.vz * scale;
	}
}

function offset_momentum(var& b, var nbody) {
	var px = 0.0;
	var py = 0.0;
	var pz = 0.0;
	for (var i = 0; i < nbody; ++i) {
		var& bi = b[i];
		var bim = bi.mass;
		px = px + (bi.vx * bim);
		py = py + (bi.vy * bim);
		pz = pz + (bi.vz * bim);
	}
	b[0].vx = -px / SOLAR_MASS;
	b[0].vy = -py / SOLAR_MASS;
	b[0].vz = -pz / SOLAR_MASS;
}

function simulate(var n, var& bodies, var nbody) {
	scale_bodies(bodies, nbody, 0.01);
	for (var i = 1; i <= n; ++i) { advance(bodies, nbody); }
	scale_bodies(bodies, nbody, 1.0 / 0.01);
}

var N = 33000;

var check = make_bodies();
offset_momentum(check, 5);
simulate(N, check, 5);
var checksum = energy(check, 5);

var bodies = make_bodies();
offset_momentum(bodies, 5);
profile_it("n-bodies", []() { simulate(N, bodies, 5); });
print("checksum {:.9f}", checksum);
