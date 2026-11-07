#include "profile.as"

class Vec3 {
	double x;
	double y;
	double z;
	Vec3() {}
	Vec3(double a_x, double a_y, double a_z) {
		x = a_x;
		y = a_y;
		z = a_z;
	}
}

class Particle {
	Vec3@ pos;
	Vec3@ vel;
	Particle(Vec3@ a_pos, Vec3@ a_vel) {
		@pos = a_pos;
		@vel = a_vel;
	}
}

array<Particle@>@ make_particles() {
	array<Particle@> result;
	result.reserve(50000);
	for (int i = 1; i <= 50000; ++i) {
		result.insertLast(Particle(Vec3(i + 0.1, i + 0.2, i + 0.3), Vec3(1.1, 2.1, 3.1)));
	}
	return result;
}

void update(array<Particle@>& ps) {
	for (uint i = 0; i < ps.length(); ++i) {
		Particle@ p = ps[i];
		p.pos.x = p.pos.x + p.vel.x;
		p.pos.y = p.pos.y + p.vel.y;
		p.pos.z = p.pos.z + p.vel.z;
	}
}

void update_several_times(array<Particle@>& ps, int count) {
	for (int i = 0; i <= count; ++i) {
		update(ps);
	}
}

array<Particle@>@ particles;

void main() {
	array<Particle@>@ check = make_particles();
	update_several_times(check, 10);
	double checksum = 0;
	for (uint i = 0; i < check.length(); ++i) {
		Particle@ p = check[i];
		checksum = checksum + p.pos.x + p.pos.y + p.pos.z;
	}

	@particles = make_particles();
	profile_it("particles kinematics", function() { update_several_times(particles, 10); });
	print("checksum " + formatFloat(checksum, "", 0, 6));
}
