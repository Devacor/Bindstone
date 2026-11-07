// DONUT — a product shot. The torus homage, grown up: sphere-traced per pixel,
// rows across every core with parallel_transform, in a studio: a glossy walnut
// table that reflects the donut and takes its soft shadow, a cyclorama backdrop
// and drifting bokeh. The donut is a distance field in its own frame: the dough
// torus, an icing layer with real thickness (a beaded lip, a wavy edge and drips
// hanging off it) and sprinkles raised off the icing as capsules on a hashed grid
// in the torus's (phi, theta) coordinates, so they ride the surface as it turns.
// Normals come from the field, so the sprinkles catch the light; a short march
// toward the key light shadows the icing under them, and a sample along the normal
// darkens their footing. The shot is choreographed: a dark open where a sweeping
// glint and a rim light reveal the silhouette, the key light coming up as the
// camera dollies in, an orbit, a push in on the sprinkles and a pull back.

float DN_M00 = 1.0; float DN_M01 = 0.0; float DN_M02 = 0.0;   // world -> donut rotation
float DN_M10 = 0.0; float DN_M11 = 1.0; float DN_M12 = 0.0;
float DN_M20 = 0.0; float DN_M21 = 0.0; float DN_M22 = 1.0;
float DN_BOB = 0.0;                                            // the donut's height
float DN_CX = 0.0; float DN_CY = 0.0; float DN_CZ = 0.0;       // camera position
float DN_FX = 0.0; float DN_FY = 0.0; float DN_FZ = 1.0;       // and basis
float DN_RX = 1.0; float DN_RY = 0.0; float DN_RZ = 0.0;
float DN_UX = 0.0; float DN_UY = 1.0; float DN_UZ = 0.0;
float DN_ZOOM = 1.0;
float DN_LX = 0.0; float DN_LY = 1.0; float DN_LZ = 0.0;      // key light, donut frame
float DN_WLX = 0.0; float DN_WLY = 1.0; float DN_WLZ = 0.0;   // and world frame
float DN_SX = 0.0; float DN_SY = 1.0; float DN_SZ = 0.0;      // the sweeping glint's light, donut frame
float DN_KEY = 1.0;          // key light level
float DN_GLINT = 0.0;        // the sweep's level
float DN_RIM = 0.0;          // rim light level
float DN_STAGE = 1.0;        // the set's light level
float DN_DRIFT = 0.0;        // bokeh drift
float DN_FLOOR = -3.4;
int DN_W = 0;
int DN_H = 0;
int DN_MAT = 0;
int DN_BG = 0;
int DN_TABLE = 0;
int DN_REFL = 0;
int DN_SEED = 0;
array<int> DN_ROWS = [];

// the donut's field in its own frame; with a_mat it answers the material instead:
// 0 dough, 1 the pale fried band, 2 icing, 3..7 sprinkles
function donut_field(float px, float py, float pz, bool a_mat) -> float {
	float ring = math::sqrt(px * px + pz * pz);
	float qx = ring - 2.0;
	float rt = math::sqrt(qx * qx + py * py);
	if (rt > 1.32 && !a_mat) { return rt - 1.17; }
	float sy = py / (rt > 0.0001 ? rt : 0.0001);
	// the icing's edge waves round the ring and drips hang off it in narrow tongues
	float phi = math::atan2(pz, px);
	float drip = math::cos(phi * 11.0 + 1.7 * math::sin(phi * 2.0));
	drip = drip > 0.0 ? drip : 0.0;
	float d2 = drip * drip;
	float d4 = d2 * d2;
	float edge = -0.02 + 0.1 * math::sin(phi * 5.0 + 2.0 * math::sin(phi * 3.0)) - 0.46 * d4 * d4;
	float above = (sy - edge) / 0.09;
	float ice = 0.0;
	if (above > 0.0) {
		float k = above > 1.0 ? 1.0 : above;
		ice = 0.07 * k * k * (3.0 - 2.0 * k);
		float lip = (above - 0.55) * 2.6;
		ice += 0.032 / (1.0 + lip * lip);
	}
	float result = rt - 1.0 - ice;
	int mat = above > 0.0 ? 2 : ((sy < 0.2 && sy > -0.22 && qx > 0.2) ? 1 : 0);
	if (above > 1.4 && qx > -0.6) {
		float th = math::atan2(py, qx);
		float fp = (phi + 3.14159265) * 9.5;
		float ft = (th + 3.14159265) * 3.5;
		int cp = math::ifloor(fp);
		int ct = math::ifloor(ft);
		int hs = ((cp * 73856093 + ct * 19349663 + DN_SEED) & 1048575) * 1274126177;
		int r = (hs >> 12) & 1023;
		if (r < 640) {
			float jx = (((hs >> 3) & 15) / 15.0 - 0.5) * 0.3;
			float jy = (((hs >> 7) & 15) / 15.0 - 0.5) * 0.3;
			float ang = ((r & 1) == 0 ? 0.0 : 1.5707963) + (((hs >> 22) & 31) / 31.0 - 0.5) * 0.7;
			float la = (fp - cp - 0.5 - jx) / 9.5 * ring;
			float lb = (ft - ct - 0.5 - jy) / 3.5 * 1.07;
			float ca = math::cos(ang);
			float sa = math::sin(ang);
			float su = la * ca + lb * sa;
			float sv = lb * ca - la * sa;
			float c = rt - 1.0 - ice - 0.022;
			float du = (su < 0.0 ? -su : su) - 0.068;
			du = du > 0.0 ? du : 0.0;
			float ds = math::sqrt(du * du + sv * sv + c * c) - 0.031;
			if (ds < result) {
				result = ds;
				mat = 3 + r % 5;
			}
		}
	}
	if (a_mat) { return mat; }
	return result < 0.045 ? result : 0.045 + (result - 0.045) * 0.5;
}

// sphere-trace the donut from a world-space ray; the hit's distance, or -1
function donut_trace(float ox, float oy, float oz, float dx, float dy, float dz, int steps) -> float {
	float py0 = oy - DN_BOB;
	float b = ox * dx + py0 * dy + oz * dz;
	float disc = b * b - (ox * ox + py0 * py0 + oz * oz) + 10.6;
	if (disc <= 0.0) { return -1.0; }
	float sq = math::sqrt(disc);
	float t = 0.0 - b - sq;
	t = t > 0.0 ? t : 0.0;
	float tfar = 0.0 - b + sq;
	for (int s = 0; s < steps; ++s) {
		float wx = ox + dx * t;
		float wy = oy + dy * t - DN_BOB;
		float wz = oz + dz * t;
		float px = DN_M00 * wx + DN_M01 * wy + DN_M02 * wz;
		float py = DN_M10 * wx + DN_M11 * wy + DN_M12 * wz;
		float pz = DN_M20 * wx + DN_M21 * wy + DN_M22 * wz;
		float d = donut_field(px, py, pz, false);
		if (d < 0.003) { return t; }
		t += d;
		if (t > tfar) { return -1.0; }
	}
	return -1.0;
}

// how much of the key light reaches a world point past the donut (soft)
function donut_shadow(float wx, float wy, float wz, float lx, float ly, float lz) -> float {
	float oy = wy - DN_BOB;
	float b = wx * lx + oy * ly + wz * lz;
	float c = wx * wx + oy * oy + wz * wz - 10.6;
	if (c > 0.0 && (b > 0.0 || b * b < c)) { return 1.0; }
	float tmax = 0.0 - b + math::sqrt(b * b - c);
	float result = 1.0;
	float t = 0.04;
	for (int s = 0; s < 11; ++s) {
		float qx = wx + lx * t;
		float qy = wy + ly * t - DN_BOB;
		float qz = wz + lz * t;
		float px = DN_M00 * qx + DN_M01 * qy + DN_M02 * qz;
		float py = DN_M10 * qx + DN_M11 * qy + DN_M12 * qz;
		float pz = DN_M20 * qx + DN_M21 * qy + DN_M22 * qz;
		float d = donut_field(px, py, pz, false);
		float k = 9.0 * d / t;
		result = k < result ? k : result;
		if (result < 0.02) { return 0.0; }
		t += d < 0.04 ? 0.04 : (d > 0.8 ? 0.8 : d);
		if (t > tmax) { break; }
	}
	return result < 0.0 ? 0.0 : result;
}

// the lit donut at a world hit: a palette index (a_refl: the table's reflection of it)
function donut_shade(float wx, float wy, float wz, float vx, float vy, float vz, bool a_refl) -> int {
	float qy = wy - DN_BOB;
	float px = DN_M00 * wx + DN_M01 * qy + DN_M02 * wz;
	float py = DN_M10 * wx + DN_M11 * qy + DN_M12 * wz;
	float pz = DN_M20 * wx + DN_M21 * qy + DN_M22 * wz;
	float e = 0.006;
	float a = donut_field(px + e, py - e, pz - e, false);
	float b = donut_field(px - e, py - e, pz + e, false);
	float c = donut_field(px - e, py + e, pz - e, false);
	float d = donut_field(px + e, py + e, pz + e, false);
	float nx = a - b - c + d;
	float ny = 0.0 - a - b + c + d;
	float nz = 0.0 - a + b - c + d;
	float nl = 1.0 / math::sqrt(nx * nx + ny * ny + nz * nz + 0.0000001);
	nx *= nl; ny *= nl; nz *= nl;
	int mat = itrunc(donut_field(px, py, pz, true));
	// the view ray, into the donut's frame
	float ux = DN_M00 * vx + DN_M01 * vy + DN_M02 * vz;
	float uy = DN_M10 * vx + DN_M11 * vy + DN_M12 * vz;
	float uz = DN_M20 * vx + DN_M21 * vy + DN_M22 * vz;
	float lx = DN_LX; float ly = DN_LY; float lz = DN_LZ;
	float nd = nx * lx + ny * ly + nz * lz;
	float lit = nd;
	if (mat == 2) { lit = (nd + 0.35) / 1.35; }
	lit = lit < 0.0 ? 0.0 : lit;
	float sh = 1.0;
	if (!a_refl && nd > 0.0) {
		// what stands close by toward the light (the sprinkles over the icing): four
		// fixed samples along the light from just off the surface, softly
		float bx = px + nx * 0.02;
		float by = py + ny * 0.02;
		float bz = pz + nz * 0.02;
		float reach = 0.05;
		for (int k = 0; k < 4; ++k) {
			float o = donut_field(bx + lx * reach, by + ly * reach, bz + lz * reach, false);
			float f = 8.0 * o / reach;
			sh = f < sh ? f : sh;
			reach *= 2.2;
		}
		sh = sh < 0.0 ? 0.0 : sh;
	}
	float ao = donut_field(px + nx * 0.07, py + ny * 0.07, pz + nz * 0.07, false) / 0.07;
	ao = ao < 0.35 ? 0.35 : (ao > 1.0 ? 1.0 : ao);
	float facing = 0.0 - (nx * ux + ny * uy + nz * uz);
	facing = facing < 0.0 ? 0.0 : facing;
	float rim = 1.0 - facing;
	rim = rim * rim * rim * DN_RIM;
	float diffuse = (0.1 * DN_STAGE + DN_KEY * lit * (0.25 + 0.75 * sh)) * ao + rim;
	if (mat == 2) { diffuse += 0.08 * DN_KEY * (1.0 - facing); }
	// Blinn speculars: the key light and the sweeping glint, glossier on icing and sprinkles
	float hx = lx - ux; float hy = ly - uy; float hz = lz - uz;
	float hl = 1.0 / math::sqrt(hx * hx + hy * hy + hz * hz + 0.0000001);
	float s = (nx * hx + ny * hy + nz * hz) * hl;
	s = s < 0.0 ? 0.0 : s;
	float gx = DN_SX - ux; float gy = DN_SY - uy; float gz = DN_SZ - uz;
	float gl = 1.0 / math::sqrt(gx * gx + gy * gy + gz * gz + 0.0000001);
	float g = (nx * gx + ny * gy + nz * gz) * gl;
	g = g < 0.0 ? 0.0 : g;
	float s2 = s * s; float s4 = s2 * s2; float s8 = s4 * s4; float s16 = s8 * s8;
	float g2 = g * g; float g4 = g2 * g2; float g8 = g4 * g4; float g16 = g8 * g8;
	float spec = mat <= 1 ? s8 * 0.35 : (mat == 2 ? s16 * s16 * s16 : s16 * s8);
	spec = spec * DN_KEY * sh;
	float glint = (mat <= 1 ? g8 * 0.4 : g16 * g16) * DN_GLINT;
	spec = spec > glint ? spec : glint;
	if (a_refl) {
		int lv = itrunc(diffuse * 7.99);
		return DN_REFL + mat * 8 + (lv < 0 ? 0 : (lv > 7 ? 7 : lv));
	}
	int di = itrunc(diffuse * 15.99);
	int si = itrunc(spec * 3.99);
	return DN_MAT + mat * 64 + (di < 0 ? 0 : (di > 15 ? 15 : di)) * 4 + (si > 3 ? 3 : si);
}

// the set behind: the cyclorama graded by the ray's height, lit from behind the
// donut by a soft halo, and bokeh drifting in the open sky above the table
function donut_backdrop(float dx, float dy, float dz) -> int {
	float g = dy * 2.2;
	g = g < 0.0 ? 0.0 : (g > 1.0 ? 1.0 : g);
	float toward = dx * DN_FX + dz * DN_FZ;
	toward = toward < 0.0 ? 0.0 : toward;
	float t2 = toward * toward;
	float t4 = t2 * t2;
	float halo = t4 * t4 * t4 * (1.0 - g * 0.6);
	float bright = ((1.0 - g) * 0.55 + halo * 0.6) * DN_STAGE;
	int level = 23 - itrunc(bright * 23.99);
	int b = 0;
	if (dy > 0.05) {
		float az = math::atan2(dx, dz) * 4.0 + DN_DRIFT;
		float el = dy * 4.0;
		int ca = math::ifloor(az);
		int ce = math::ifloor(el);
		int hs = ((ca * 92837111 + ce * 689287499 + DN_SEED) & 1048575) * 1274126177;
		if (((hs >> 10) & 7) < 4) {
			float jx = ((hs >> 4) & 15) / 15.0 * 0.4 + 0.3;
			float jy = ((hs >> 14) & 15) / 15.0 * 0.4 + 0.3;
			float ex = az - ca - jx;
			float ey = el - ce - jy;
			float rr = 0.14 + ((hs >> 18) & 7) * 0.028;
			float q = (ex * ex + ey * ey) / (rr * rr);
			if (q < 1.0) {
				// a lens disc: flat, a touch brighter at the rim, feathered at the edge
				float lit = 0.55 + (q > 0.7 ? (q - 0.7) * 1.2 : 0.0) + ((hs >> 21) & 3) * 0.12;
				if (q > 0.88) { lit *= (1.0 - q) / 0.12; }
				b = 1 + itrunc(lit * DN_STAGE * 7.0);
				b = b > 8 ? 8 : b;
				b += ((hs >> 24) % 3) * 8;
			}
		}
	}
	return DN_BG + (level < 0 ? 0 : (level > 23 ? 23 : level)) * 25 + b;
}

function donut_row(int y) -> array<int> {
	array<int> out = [];
	int ww = DN_W;
	int hh = DN_H;
	float sc = 1.5 / (ww * DN_ZOOM);
	float v = (hh * 0.5 - y - 0.5) * sc;
	float cx = DN_CX; float cy = DN_CY; float cz = DN_CZ;
	for (int x = 0; x < ww; ++x) {
		float u = (x + 0.5 - ww * 0.5) * sc;
		float dx = DN_FX + DN_RX * u + DN_UX * v;
		float dy = DN_FY + DN_RY * u + DN_UY * v;
		float dz = DN_FZ + DN_RZ * u + DN_UZ * v;
		float il = 1.0 / math::sqrt(dx * dx + dy * dy + dz * dz);
		dx *= il; dy *= il; dz *= il;
		float t = donut_trace(cx, cy, cz, dx, dy, dz, 48);
		if (t >= 0.0) {
			out.push(donut_shade(cx + dx * t, cy + dy * t, cz + dz * t, dx, dy, dz, false));
			continue;
		}
		if (dy >= -0.0001) {
			out.push(donut_backdrop(dx, dy, dz));
			continue;
		}
		float tf = (DN_FLOOR - cy) / dy;
		float fx = cx + dx * tf;
		float fz = cz + dz * tf;
		if (tf > 60.0) {
			out.push(donut_backdrop(dx, -0.0001, dz));
			continue;
		}
		// the table: a pool of light under the donut, walnut grain, its shadow and its reflection
		float pool = 1.0 / (1.0 + (fx * fx + fz * fz) * 0.035);
		float grain = math::sin(fx * 5.1 + math::sin(fz * 0.21) * 1.6) * 0.03 + math::sin(fx * 13.0 + fz * 0.4) * 0.015;
		float plank = fx / 2.6 - math::floor(fx / 2.6);
		if (plank < 0.025) { grain -= 0.1; }
		float sh = donut_shadow(fx, DN_FLOOR, fz, DN_WLX, DN_WLY, DN_WLZ);
		float far = 1.0 - (tf - 8.0) / 50.0;
		far = far < 0.5 ? 0.5 : (far > 1.0 ? 1.0 : far);
		float level = (0.12 * DN_STAGE + DN_KEY * pool * (0.25 + 0.75 * sh) * 0.85 + grain) * far;
		float rt = donut_trace(fx, DN_FLOOR, fz, dx, 0.0 - dy, dz, 28);
		if (rt >= 0.0 && tf < 30.0) {
			out.push(donut_shade(fx + dx * rt, DN_FLOOR - dy * rt, fz + dz * rt, dx, 0.0 - dy, dz, true));
			continue;
		}
		int lv = itrunc(level * 15.99);
		out.push(DN_TABLE + (lv < 0 ? 0 : (lv > 15 ? 15 : lv)));
	}
	return out;
}

class Donut : Scene {
	int mat0 = 0;
	float tt = 0.0;

	void build() override {
		name = "DONUT";
		handle = "a product shot . by strong_ptr";
		// the cyclorama: 24 levels, dusky near the horizon to deep navy overhead, each with
		// 8 levels of bokeh in three tints
		DN_BG = pal.size();
		for (int l = 0; l < 24; ++l) {
			float f = l / 23.0;
			float br = 58.0 - 52.0 * f;
			float bg = 52.0 - 44.0 * f;
			float bb = 84.0 - 62.0 * f;
			pal.add(br, bg, bb);
			var tints = [[255.0, 176.0, 96.0], [255.0, 120.0, 170.0], [110.0, 210.0, 230.0]];
			for (int tn = 0; tn < 3; ++tn) {
				var c = tints[tn];
				float tr = c[0]; float tg = c[1]; float tb = c[2];
				for (int k = 1; k <= 8; ++k) {
					float glow = k / 8.0 * 0.8;
					pal.add(br + (tr - br) * glow, bg + (tg - bg) * glow, bb + (tb - bb) * glow);
				}
			}
		}
		// the table: dark glossy walnut, 16 levels
		DN_TABLE = pal.ramp(16, 8.0, 5.0, 6.0, 168.0, 112.0, 76.0);
		// materials: dough, the pale band, icing, five sprinkles; 16 diffuse x 4 specular
		mat0 = pal.size();
		var mats = [[178.0, 104.0, 46.0], [232.0, 196.0, 128.0], [255.0, 120.0, 178.0], [255.0, 52.0, 60.0],
			[255.0, 214.0, 40.0], [60.0, 140.0, 255.0], [80.0, 226.0, 110.0], [252.0, 250.0, 246.0]];
		for (int m = 0; m < 8; ++m) {
			var c = mats[m];
			float cr = c[0]; float cg = c[1]; float cb = c[2];
			for (int d = 0; d < 16; ++d) {
				float df = 0.06 + 0.94 * d / 15.0;
				df = df > 1.0 ? 1.0 : df;
				for (int s = 0; s < 4; ++s) {
					float sp = s / 3.0 * (m <= 1 ? 0.4 : 0.92);
					float r = cr * df;
					float g = cg * df;
					float b = cb * df;
					pal.add(r + (255.0 - r) * sp, g + (240.0 - g) * sp, b + (235.0 - b) * sp);
				}
			}
		}
		// the donut in the table: each material at 8 levels, sunk into the walnut
		DN_REFL = pal.size();
		for (int m2 = 0; m2 < 8; ++m2) {
			var c2 = mats[m2];
			for (int l2 = 0; l2 < 8; ++l2) {
				float f2 = 0.08 + l2 / 7.0 * 0.5;
				pal.add(14.0 + c2[0] * f2 * 0.6, 9.0 + c2[1] * f2 * 0.6, 8.0 + c2[2] * f2 * 0.6);
			}
		}
	}

	void update(float t, float dt) override {
		tt = t;
	}

	float smooth(float a, float b, float t) {
		float k = (t - a) / (b - a);
		k = k < 0.0 ? 0.0 : (k > 1.0 ? 1.0 : k);
		return k * k * (3.0 - 2.0 * k);
	}

	void render(var& grid) override {
		float t = tt;
		// the donut: tipped toward the camera, spinning on its own axis, bobbing
		float tilt = 0.82 + 0.2 * math::sin(t * 0.55);
		float spin = t * 0.62 + 0.4;
		float ct = math::cos(tilt); float st = math::sin(tilt);
		float cs = math::cos(spin); float ss = math::sin(spin);
		// world-from-donut R = Rx(tilt) * Ry(spin); the field wants its transpose
		DN_M00 = cs;        DN_M01 = 0.0 - ss * st;     DN_M02 = 0.0 - ss * ct;
		DN_M10 = 0.0;       DN_M11 = ct;                DN_M12 = 0.0 - st;
		DN_M20 = ss;        DN_M21 = cs * st;           DN_M22 = cs * ct;
		DN_BOB = 0.18 * math::sin(t * 1.3);
		// the camera: a dolly in from the dark, an orbit, a push in on the sprinkles, a pull back
		float dist = 15.0 - 7.6 * smooth(0.0, 3.6, t) - 2.4 * smooth(6.2, 8.4, t) + 2.0 * smooth(8.9, 10.8, t);
		float az = -0.75 + t * 0.21;
		float el = 0.17 + 0.08 * math::sin(t * 0.45) + 0.12 * smooth(6.2, 8.4, t) - 0.12 * smooth(8.9, 10.8, t);
		DN_CX = dist * math::cos(el) * math::sin(az);
		DN_CY = dist * math::sin(el);
		DN_CZ = 0.0 - dist * math::cos(el) * math::cos(az);
		float tx = 0.0; float ty = -0.25 + 0.5 * smooth(6.2, 8.4, t) - 0.5 * smooth(8.9, 10.8, t); float tz = 0.0;
		float fx = tx - DN_CX; float fy = ty - DN_CY; float fz = tz - DN_CZ;
		float fl = 1.0 / math::sqrt(fx * fx + fy * fy + fz * fz);
		fx *= fl; fy *= fl; fz *= fl;
		float rx = fz; float rz = 0.0 - fx;
		float rl = 1.0 / math::sqrt(rx * rx + rz * rz);
		rx *= rl; rz *= rl;
		DN_FX = fx; DN_FY = fy; DN_FZ = fz;
		DN_RX = rx; DN_RY = 0.0; DN_RZ = rz;
		DN_UX = rz * fy; DN_UY = rx * fz - rz * fx; DN_UZ = 0.0 - rx * fy;
		DN_ZOOM = 1.0;
		// the lights: the key rises with the reveal, a glint sweeps across the dark open
		// and again on the push in, the rim light outlines it until the key takes over
		DN_KEY = 0.06 + 0.94 * smooth(2.2, 4.2, t);
		DN_STAGE = 0.15 + 0.85 * smooth(2.0, 4.6, t);
		DN_RIM = 0.95 - 0.6 * smooth(3.0, 5.0, t);
		float sweep1 = (t - 1.2) / 2.6;
		float sweep2 = (t - 6.4) / 2.2;
		float sw = sweep1 >= 0.0 && sweep1 <= 1.0 ? sweep1 : (sweep2 >= 0.0 && sweep2 <= 1.0 ? sweep2 : -1.0);
		DN_GLINT = sw < 0.0 ? 0.0 : math::sin(sw * 3.1415927) * 1.2;
		float sa = -1.4 + 2.8 * (sw < 0.0 ? 0.0 : sw);
		float wlx = -0.48; float wly = 0.74; float wlz = -0.47;
		DN_WLX = wlx; DN_WLY = wly; DN_WLZ = wlz;
		DN_LX = DN_M00 * wlx + DN_M01 * wly + DN_M02 * wlz;
		DN_LY = DN_M10 * wlx + DN_M11 * wly + DN_M12 * wlz;
		DN_LZ = DN_M20 * wlx + DN_M21 * wly + DN_M22 * wlz;
		// the glint's light swings across behind the camera
		float gx = math::sin(sa) * 0.8 + fx * -0.6;
		float gy = 0.45;
		float gz = math::cos(sa) * -0.5 + fz * -0.6;
		float gl = 1.0 / math::sqrt(gx * gx + gy * gy + gz * gz);
		gx *= gl; gy *= gl; gz *= gl;
		DN_SX = DN_M00 * gx + DN_M01 * gy + DN_M02 * gz;
		DN_SY = DN_M10 * gx + DN_M11 * gy + DN_M12 * gz;
		DN_SZ = DN_M20 * gx + DN_M21 * gy + DN_M22 * gz;
		DN_DRIFT = t * 0.12;
		DN_W = w;
		DN_H = h;
		DN_MAT = mat0;
		DN_SEED = seed & 1023;
		if (DN_ROWS.size() != h) {
			DN_ROWS = [];
			for (int r = 0; r < h; ++r) { DN_ROWS.push(r); }
		}
		var rows = parallel_transform(DN_ROWS, donut_row);
		int idx = 0;
		for (int y = 0; y < h; ++y) {
			var& row = rows[y];
			for (int x = 0; x < w; ++x) {
				grid[idx] = row[x];
				++idx;
			}
		}
	}
}
