include "profile.jai";

class Vec3 {
	float x = 0.0;
	float y = 0.0;
	float z = 0.0;
	Vec3(float px, float py, float pz) { x = px; y = py; z = pz; }
}

class Particle {
	Vec3 pos = Vec3(0.0, 0.0, 0.0);
	Vec3 vel = Vec3(0.0, 0.0, 0.0);
	Particle(Vec3 p, Vec3 v) { pos = p; vel = v; }
}

array<Particle> make_particles() {
	array<Particle> ps = [];
	for (int i = 1; i <= 50000; ++i) {
		ps.push(Particle(Vec3(i + 0.1, i + 0.2, i + 0.3), Vec3(1.1, 2.1, 3.1)));
	}
	return ps;
}

void update(array<Particle>& ps) {
	for (auto& p : ps) {
		p.pos.x = p.pos.x + p.vel.x;
		p.pos.y = p.pos.y + p.vel.y;
		p.pos.z = p.pos.z + p.vel.z;
	}
}

void update_several_times(array<Particle>& ps, int count) {
	for (int i = 0; i <= count; ++i) { update(ps); }
}

array<Particle> check = make_particles();
update_several_times(check, 10);
float checksum = 0.0;
for (auto& p : check) { checksum = checksum + p.pos.x + p.pos.y + p.pos.z; }

array<Particle> particles = make_particles();
profile_it("particles kinematics", []() { update_several_times(particles, 10); });
print("checksum {:.6f}", checksum);
