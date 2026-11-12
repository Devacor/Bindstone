include "profile.jai";

class Vec3 {
	var x = 0.0;
	var y = 0.0;
	var z = 0.0;
	Vec3(var px, var py, var pz) { x = px; y = py; z = pz; }
}

class Particle {
	var pos = null;
	var vel = null;
	Particle(var p, var v) { pos = p; vel = v; }
}

function make_particles() {
	var ps = [];
	for (var i = 1; i <= 50000; ++i) {
		ps.push(Particle(Vec3(i + 0.1, i + 0.2, i + 0.3), Vec3(1.1, 2.1, 3.1)));
	}
	return ps;
}

function update(var& ps) {
	for (var& p : ps) {
		p.pos.x = p.pos.x + p.vel.x;
		p.pos.y = p.pos.y + p.vel.y;
		p.pos.z = p.pos.z + p.vel.z;
	}
}

function update_several_times(var& ps, var count) {
	for (var i = 0; i <= count; ++i) { update(ps); }
}

var check = make_particles();
update_several_times(check, 10);
var checksum = 0.0;
for (var& p : check) { checksum = checksum + p.pos.x + p.pos.y + p.pos.z; }

var particles = make_particles();
profile_it("particles kinematics", []() { update_several_times(particles, 10); });
print("checksum {:.6f}", checksum);
