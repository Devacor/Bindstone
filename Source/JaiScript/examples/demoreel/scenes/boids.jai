// BOIDS — a starling murmuration over a lake at sunset. A thousand birds fly
// in three dimensions: each keeps its distance from its nearest neighbours and
// matches their heading (Reynolds' separation and alignment) while it is drawn
// to its place on a sheet that swings, stretches and folds, the shape that
// makes a murmuration read as veils and dark ribbons. Neighbours come from a
// uniform grid hashed fresh every frame, and the birds steer in parallel
// chunks. A peregrine falcon circles and stoops through the flock: birds near
// it panic, and panic passes from bird to neighbour, so a scare wave rolls
// across the cloud and splits it around the falcon. The camera stands on the
// shore; each bird lands on the screen in perspective and stacks into a
// density buffer, so the cloud darkens where it is thick and pales where the
// sheet faces you, and the lake mirrors it under the sun's glitter path. Sky
// and clouds are baked tones; every palette entry is a sky tone darkened by a
// level, so the birds shade whatever they cross.

// Globals on purpose: the parallel steering reads them (arrays borrow zero-copy).
array<float> BD_X = [];
array<float> BD_Y = [];
array<float> BD_Z = [];
array<float> BD_VX = [];
array<float> BD_VY = [];
array<float> BD_VZ = [];
array<float> BD_PANIC = [];
array<float> BD_U = [];      // per bird: its place along the sheet
array<float> BD_V = [];      // and across it
array<int> BD_HEAD = [];     // per grid cell: its first bird, -1 when empty
array<int> BD_NEXT = [];     // per bird: the next bird in its cell
array<int> BD_CHUNKS = [];
int BD_N = 0;
int BD_CHUNK = 0;
float BD_DT = 0.0;
float BD_TX = 0.0;           // where the flock is drawn to
float BD_TY = 0.0;
float BD_TZ = 0.0;
float BD_AX = 1.0;           // the sheet the flock folds into: its long axis,
float BD_AY = 0.0;
float BD_AZ = 0.0;
float BD_BX = 0.0;           // its short axis,
float BD_BY = 1.0;
float BD_BZ = 0.0;
float BD_NX = 0.0;           // its normal (the fold bends along it),
float BD_NY = 0.0;
float BD_NZ = 1.0;
float BD_LEN = 40.0;         // its half length and half width,
float BD_WID = 20.0;
float BD_FOLD = 10.0;        // the fold's depth and phase
float BD_PHASE = 0.0;
float BD_FX = 0.0;           // the falcon
float BD_FY = 0.0;
float BD_FZ = 0.0;

// the grid: 12-unit cells over x and z in [-96, 96), y in [0, 96)
int BD_GX = 16;
int BD_GY = 8;

function bd_cell(float x, float y, float z) -> int {
	int cx = (x + 96.0) * 0.0833333;
	int cy = y * 0.0833333;
	int cz = (z + 96.0) * 0.0833333;
	cx = cx < 0 ? 0 : (cx > 15 ? 15 : cx);
	cy = cy < 0 ? 0 : (cy > 7 ? 7 : cy);
	cz = cz < 0 ? 0 : (cz > 15 ? 15 : cz);
	return (cz * 8 + cy) * 16 + cx;
}

// one chunk of birds on a parallel_transform worker: each bird reads its
// neighbours in the 27 cells around it (at most ten: starlings track about
// seven) and returns its new velocity and panic
function boids_steer(int c) -> array<float> {
	int lo = c * BD_CHUNK;
	int hi = lo + BD_CHUNK;
	hi = hi > BD_N ? BD_N : hi;
	float dt = BD_DT;
	float tx = BD_TX;
	float ty = BD_TY;
	float tz = BD_TZ;
	float fx = BD_FX;
	float fy = BD_FY;
	float fz = BD_FZ;
	array<float> out = [];
	for (int i = lo; i < hi; ++i) {
		float x = BD_X[i];
		float y = BD_Y[i];
		float z = BD_Z[i];
		float vx = BD_VX[i];
		float vy = BD_VY[i];
		float vz = BD_VZ[i];
		float panic = BD_PANIC[i] * (1.0 - 1.6 * dt);
		float ax = 0.0;
		float ay = 0.0;
		float az = 0.0;
		float ux = 0.0;
		float uy = 0.0;
		float uz = 0.0;
		int near = 0;
		int cx = (x + 96.0) * 0.0833333;
		int cy = y * 0.0833333;
		int cz = (z + 96.0) * 0.0833333;
		for (int dz = -1; dz <= 1 && near < 10; ++dz) {
			int zz = cz + dz;
			if (zz < 0 || zz > 15) { continue; }
			for (int dy = -1; dy <= 1 && near < 10; ++dy) {
				int yy = cy + dy;
				if (yy < 0 || yy > 7) { continue; }
				for (int dx = -1; dx <= 1 && near < 10; ++dx) {
					int xx = cx + dx;
					if (xx < 0 || xx > 15) { continue; }
					int j = BD_HEAD[(zz * 8 + yy) * 16 + xx];
					while (j >= 0 && near < 10) {
						if (j != i) {
							float ex = BD_X[j] - x;
							float ey = BD_Y[j] - y;
							float ez = BD_Z[j] - z;
							float d2 = ex * ex + ey * ey + ez * ez;
							if (d2 < 121.0) {
								++near;
								ux += BD_VX[j];
								uy += BD_VY[j];
								uz += BD_VZ[j];
								if (d2 < 20.0) {
									float k = 46.0 / (d2 + 0.4);
									ax -= ex * k;
									ay -= ey * k;
									az -= ez * k;
								}
								float pj = BD_PANIC[j] * 0.94;
								panic = pj > panic ? pj : panic;
							}
						}
						j = BD_NEXT[j];
					}
				}
			}
		}
		if (near > 0) {
			float inv = 1.0 / near;
			float al = 2.2 + panic * 3.0;
			ax += (ux * inv - vx) * al;
			ay += (uy * inv - vy) * al;
			az += (uz * inv - vz) * al;
		}
		// each bird is drawn to its place on the folding sheet
		float su = BD_U[i] * BD_LEN;
		float sv = BD_V[i] * BD_WID;
		float fold = BD_FOLD * math::sin(BD_U[i] * 2.4 + BD_PHASE);
		float gx = tx + BD_AX * su + BD_BX * sv + BD_NX * fold - x;
		float gy = ty + BD_AY * su + BD_BY * sv + BD_NY * fold - y;
		float gz = tz + BD_AZ * su + BD_BZ * sv + BD_NZ * fold - z;
		float pull = 0.32 * (1.0 - panic * 0.7);
		ax += gx * pull;
		ay += gy * pull;
		az += gz * pull;
		if (y < 18.0) { ay += (18.0 - y) * 5.0; }
		if (y > 88.0) { ay -= (y - 88.0) * 5.0; }
		// the falcon: birds within its reach flee and panic
		float px = x - fx;
		float py = y - fy;
		float pz = z - fz;
		float pd2 = px * px + py * py + pz * pz;
		if (pd2 < 400.0) {
			float pd = math::sqrt(pd2) + 0.01;
			float k2 = 160.0 * (1.0 - pd / 20.0) / pd;
			ax += px * k2;
			ay += py * k2;
			az += pz * k2;
			panic = 1.0;
		}
		vx += ax * dt;
		vy += ay * dt;
		vz += az * dt;
		float sp = math::sqrt(vx * vx + vy * vy + vz * vz) + 0.0001;
		float top = 30.0 + panic * 16.0;
		float k3 = sp > top ? top / sp : (sp < 16.0 ? 16.0 / sp : 1.0);
		out.push(vx * k3);
		out.push(vy * k3);
		out.push(vz * k3);
		out.push(panic);
	}
	return out;
}

class Boids : Scene {
	int nb = 1000;
	int tones = 32;
	array<int> sky = [];        // per pixel: tone * 8 (darkness 0)
	array<float> glint = [];    // per lake pixel: the sun glitter's phase, < 0 off the path
	array<float> dens = [];     // the birds stacked on the screen, per pixel
	array<float> flap = [];     // per bird: wingbeat phase
	int horizon = 0;            // the first lake row
	float tt = 0.0;
	float cam_x = 0.0;          // the camera on the shore, and its basis
	float cam_y = 6.0;
	float cam_z = 0.0;
	float rx_ = 1.0;
	float rz_ = 0.0;
	float fwx = 0.0;
	float fwy = 0.0;
	float fwz = 1.0;
	float upx = 0.0;
	float upy = 1.0;
	float upz = 0.0;
	float focal = 90.0;
	float look_x = 0.0;         // where the camera turns to: the flock's centre, eased
	float look_z = 0.0;
	float fvx = 0.0;            // the falcon's heading, for its silhouette
	float fvy = 0.0;
	float fvz = 0.0;

	void build() override {
		name = "BOIDS";
		handle = "murmuration . a thousand starlings and a falcon";
		replay = 14.0;
		var stops = [[10.0, 12.0, 40.0], [48.0, 30.0, 90.0], [150.0, 60.0, 120.0], [240.0, 110.0, 90.0], [255.0, 190.0, 110.0], [255.0, 244.0, 200.0]];
		for (int t = 0; t < tones; ++t) {
			float f = t * 5.0 / (tones - 1.0);
			int k = itrunc(f);
			k = k > 4 ? 4 : k;
			float u = f - k;
			var a = stops[k];
			var b = stops[k + 1];
			float ar = a[0]; float ag = a[1]; float ab = a[2];
			float cr = ar + (b[0] - ar) * u;
			float cg = ag + (b[1] - ag) * u;
			float cb = ab + (b[2] - ab) * u;
			for (int d = 0; d < 8; ++d) {
				float k2 = 1.0 - d / 7.0 * 0.95;
				pal.add(cr * k2 + 5.0 * (1.0 - k2), cg * k2 + 3.0 * (1.0 - k2), cb * k2 + 9.0 * (1.0 - k2));
			}
		}
		focal = w * 0.9;
		horizon = itrunc(h * 0.74);
		bake_sky();
		dens = gfx.make_fgrid(w * h);
		BD_N = nb;
		BD_CHUNK = 64;
		BD_CHUNKS = [];
		for (int c = 0; c * BD_CHUNK < nb; ++c) { BD_CHUNKS.push(c); }
		BD_X = []; BD_Y = []; BD_Z = [];
		BD_VX = []; BD_VY = []; BD_VZ = [];
		BD_PANIC = [];
		BD_NEXT = [];
		BD_U = [];
		BD_V = [];
		flap = [];
		for (int i = 0; i < nb; ++i) {
			// they stream in from the left, low over the water
			BD_X.push(-80.0 + rng.nextf() * 30.0);
			BD_Y.push(30.0 + rng.nextf() * 20.0);
			BD_Z.push(-20.0 + rng.nextf() * 50.0);
			BD_VX.push(24.0 + rng.nextf() * 4.0);
			BD_VY.push(rng.nextf() * 4.0 - 1.0);
			BD_VZ.push(rng.nextf() * 6.0 - 3.0);
			BD_PANIC.push(0.0);
			BD_NEXT.push(-1);
			float ang = rng.nextf() * 6.2831853;
			float rad = math::sqrt(rng.nextf());
			BD_U.push(math::cos(ang) * rad);
			BD_V.push(math::sin(ang) * rad);
			flap.push(rng.nextf() * 6.28);
		}
		BD_HEAD = gfx.make_grid(BD_GX * BD_GY * BD_GX);
		look_x = -65.0;
		look_z = 5.0;
	}

	// the backdrop: a sunset gradient with the sun low over the water, streaks of
	// cloud lit from below, and the lake mirroring
	// it all a shade darker, with a glitter path under the sun
	void bake_sky() {
		sky = gfx.make_grid(w * h);
		glint = gfx.make_fgrid(w * h);
		float sunx = w * 0.62;
		float suny = horizon - h * 0.07;
		float sr = h * 0.075;
		for (int y = 0; y < horizon; ++y) {
			for (int x2 = 0; x2 < w; ++x2) {
				float g = y * 1.0 / horizon;
				float tone = 2.0 + g * g * 19.0;
				float dx = x2 - sunx;
				float dy = (y - suny) * 1.6;
				float d = math::sqrt(dx * dx + dy * dy);
				float glow = d < sr ? 31.0 : 27.0 - (d - sr) * 0.42;
				tone = glow > tone ? glow : tone;
				float cover = (cloud(x2, y) - 0.55) * 1.8;
				cover = cover < 0.0 ? 0.0 : (cover > 1.0 ? 1.0 : cover);
				cover *= g < 0.72 ? 1.0 - g * 0.6 : 0.0;
				if (cover > 0.0) {
					float below = cloud(x2, y + 2) - cloud(x2, y);
					float near_sun = 1.0 - d / (w * 0.8);
					near_sun = near_sun < 0.0 ? 0.0 : near_sun;
					float body = 3.0 + g * 10.0 + near_sun * 5.0;
					float lit = below < 0.0 ? -below * (12.0 + near_sun * 16.0) : 0.0;
					tone = tone + (body + lit - tone) * cover;
				}
				int it = itrunc(tone);
				sky[y * w + x2] = (it > 31 ? 31 : (it < 0 ? 0 : it)) * 8;
			}
		}
		for (int y2 = horizon; y2 < h; ++y2) {
			int mirror = 2 * horizon - 1 - y2;
			mirror = mirror < 0 ? 0 : mirror;
			float depth = (y2 - horizon) * 1.0 / (h - horizon);
			for (int x4 = 0; x4 < w; ++x4) {
				int src = sky[mirror * w + x4];
				int dk = src % 8;
				int tn = src / 8 - itrunc(2.0 + depth * 6.0);
				tn = tn < 0 ? 0 : tn;
				dk = dk > 1 ? dk : 1 + itrunc(depth * 2.0);
				sky[y2 * w + x4] = tn * 8 + (dk > 7 ? 7 : dk);
				float spread = 2.0 + depth * 14.0;
				float off = x4 - sunx;
				glint[y2 * w + x4] = off * off < spread * spread ? x4 * 1.7 + y2 * 2.9 + rng.nextf() * 6.28 : -1.0;
			}
		}
	}

	// cloud streaks: two stretched sine octaves under a band that thickens them in the upper sky
	float cloud(int x, int y) {
		float n = math::sin(x * 0.05 + y * 0.17 + math::sin(y * 0.33) * 1.5) + 0.5 * math::sin(x * 0.13 - y * 0.26 + 2.1);
		return n + math::sin(y * 7.5 / horizon + 0.4) * 0.8;
	}

	void update(float t, float dt) override {
		tt = t;
		place_camera(t);
		if (dt <= 0.0) { return; }
		float step = dt > 0.05 ? 0.05 : dt;
		BD_TX = 40.0 * math::sin(t * 0.33);
		BD_TY = 42.0 + 10.0 * math::sin(t * 0.53 + 0.8);
		BD_TZ = 30.0 * math::sin(t * 0.47 + 1.3);
		shape_sheet(t);
		float ox = BD_FX;
		float oy = BD_FY;
		float oz = BD_FZ;
		place_falcon(t);
		fvx = BD_FX - ox;
		fvy = BD_FY - oy;
		fvz = BD_FZ - oz;
		BD_DT = step;
		follow_flock(step);
		hash_birds();
		var vel = parallel_transform(BD_CHUNKS, boids_steer);
		var& x = BD_X;
		var& y = BD_Y;
		var& z = BD_Z;
		var& vx = BD_VX;
		var& vy = BD_VY;
		var& vz = BD_VZ;
		var& pn = BD_PANIC;
		int i = 0;
		for (int c = 0; c < vel.size(); ++c) {
			var& part = vel[c];
			int m = part.size();
			for (int k = 0; k < m; k += 4) {
				vx[i] = part[k];
				vy[i] = part[k + 1];
				vz[i] = part[k + 2];
				pn[i] = part[k + 3];
				x[i] += vx[i] * step;
				y[i] += vy[i] * step;
				z[i] += vz[i] * step;
				++i;
			}
		}
	}

	// the sheet swings and tilts, stretches into a ribbon and gathers in again,
	// and its fold travels along it: seen edge-on it is a dark line, face-on
	// a pale veil
	void shape_sheet(float t) {
		float yaw = 0.3 + 0.95 * math::sin(t * 0.35);
		float tilt = 0.5 * math::sin(t * 0.29);
		float cy = math::cos(yaw);
		float sy = math::sin(yaw);
		float ct = math::cos(tilt);
		float st = math::sin(tilt);
		BD_AX = cy * ct;
		BD_AY = st;
		BD_AZ = sy * ct;
		float roll = 0.9 + 0.7 * math::sin(t * 0.23 + 0.6);
		float cr = math::cos(roll);
		float sr = math::sin(roll);
		float px = -sy;
		float pz = cy;
		float qx = -cy * st;
		float qy = ct;
		float qz = -sy * st;
		BD_BX = px * cr + qx * sr;
		BD_BY = qy * sr;
		BD_BZ = pz * cr + qz * sr;
		BD_NX = BD_AY * BD_BZ - BD_AZ * BD_BY;
		BD_NY = BD_AZ * BD_BX - BD_AX * BD_BZ;
		BD_NZ = BD_AX * BD_BY - BD_AY * BD_BX;
		BD_LEN = 40.0 + 12.0 * math::sin(t * 0.37);
		BD_WID = 18.0 - 5.0 * math::sin(t * 0.37);
		BD_FOLD = 6.0 + 4.0 * math::sin(t * 0.61 + 2.0);
		BD_PHASE = t * 1.3;
	}

	void follow_flock(float step) {
		float sx = 0.0;
		float sz = 0.0;
		for (int i = 0; i < nb; ++i) {
			sx += BD_X[i];
			sz += BD_Z[i];
		}
		float k = step * 3.0;
		look_x += (sx / nb - look_x) * k;
		look_z += (sz / nb - look_z) * k;
	}

	// birds into their cells: each cell's list threads through BD_NEXT
	void hash_birds() {
		var& head = BD_HEAD;
		var& next = BD_NEXT;
		int cells = head.size();
		for (int c = 0; c < cells; ++c) { head[c] = -1; }
		for (int i = 0; i < nb; ++i) {
			int cell = bd_cell(BD_X[i], BD_Y[i], BD_Z[i]);
			next[i] = head[cell];
			head[cell] = i;
		}
	}

	// the falcon circles high over the flock and stoops through it twice: each
	// dive is a curve from its circle, through the heart of the cloud, and away
	void place_falcon(float t) {
		float ca = t * 0.8;
		float hx = BD_TX + 70.0 * math::cos(ca);
		float hy = BD_TY + 42.0;
		float hz = BD_TZ + 70.0 * math::sin(ca);
		float dive = -1.0;
		if (t > 5.0 && t < 6.6) { dive = (t - 5.0) / 1.6; }
		if (t > 9.4 && t < 11.0) { dive = (t - 9.4) / 1.6; }
		if (dive < 0.0) {
			BD_FX = hx;
			BD_FY = hy;
			BD_FZ = hz;
			return;
		}
		float ex = BD_TX - (hx - BD_TX);
		float ey = BD_TY - 30.0;
		float ez = BD_TZ - (hz - BD_TZ);
		float a = (1.0 - dive) * (1.0 - dive);
		float b = 2.0 * dive * (1.0 - dive);
		float c = dive * dive;
		BD_FX = hx * a + BD_TX * b + ex * c;
		BD_FY = hy * a + (BD_TY - 6.0) * b + ey * c;
		BD_FZ = hz * a + BD_TZ * b + ez * c;
	}

	// on the shore, turning slowly to follow the flock; the pitch stays fixed so
	// the horizon stays where the backdrop was baked
	void place_camera(float t) {
		float ang = -1.5707963 + 0.25 * math::sin(t * 0.11);
		cam_x = 115.0 * math::cos(ang);
		cam_z = 115.0 * math::sin(ang);
		cam_y = 6.0;
		float yaw = math::atan2(look_z - cam_z, look_x - cam_x);
		float pitch = math::atan2(horizon - h * 0.5, focal);
		float cp = math::cos(pitch);
		float sp = math::sin(pitch);
		float cy = math::cos(yaw);
		float sy = math::sin(yaw);
		fwx = cy * cp;
		fwy = sp;
		fwz = sy * cp;
		rx_ = -sy;
		rz_ = cy;
		upx = -cy * sp;
		upy = cp;
		upz = -sy * sp;
	}

	void render(var& grid) override {
		int n = w * h;
		var& dn = dens;
		for (int i = 0; i < n; ++i) { dn[i] = 0.0; }
		float half_w = w * 0.5;
		float half_h = h * 0.5;
		for (int b = 0; b < nb; ++b) {
			float ex = BD_X[b] - cam_x;
			float ey = BD_Y[b] - cam_y;
			float ez = BD_Z[b] - cam_z;
			float zc = ex * fwx + ey * fwy + ez * fwz;
			if (zc < 8.0) { continue; }
			float iz = focal / zc;
			int sx = half_w + (ex * rx_ + ez * rz_) * iz;
			int sy = half_h - (ex * upx + ey * upy + ez * upz) * iz;
			if (sx < 1 || sx >= w - 1 || sy < 1 || sy >= horizon) { continue; }
			float size = 1.6 * iz;
			float beat = math::sin(flap[b] + tt * (14.0 + BD_PANIC[b] * 10.0));
			// a bird banking in panic shows its wings: the scare wave reads darker
			float wgt = (size < 1.0 ? size * size : 1.0) * (0.75 + 0.25 * beat + BD_PANIC[b] * 0.8);
			int i2 = sy * w + sx;
			dn[i2] += wgt;
			if (size > 1.4) {
				int lift = beat > 0.3 ? -w : 0;
				dn[i2 - 1 + lift] += wgt * 0.8;
				dn[i2 + 1 + lift] += wgt * 0.8;
			}
		}
		draw_falcon();
		var& sk = sky;
		var& gl = glint;
		int glit_on = itrunc(tt * 9.0);
		for (int y = 0; y < h; ++y) {
			int row = y * w;
			if (y < horizon) {
				for (int x = 0; x < w; ++x) {
					int s = sk[row + x];
					int lv = dn[row + x] * 1.8;
					int dk = s % 8 + lv;
					grid[row + x] = s - s % 8 + (dk > 7 ? 7 : dk);
				}
				continue;
			}
			int mirror = 2 * horizon - 1 - y;
			int ripple = itrunc(1.6 * math::sin(y * 1.1 + tt * 2.3));
			for (int x2 = 0; x2 < w; ++x2) {
				int s2 = sk[row + x2];
				int mx = x2 + ripple;
				mx = mx < 0 ? 0 : (mx >= w ? w - 1 : mx);
				int lv2 = mirror >= 0 ? dn[mirror * w + mx] * 2.0 : 0.0;
				int dk2 = s2 % 8 + lv2;
				int tone = s2 - s2 % 8;
				float ph = gl[row + x2];
				if (ph >= 0.0 && math::sin(ph + glit_on * 0.7) > 0.8) { tone = 30 * 8; dk2 = lv2; }
				grid[row + x2] = tone + (dk2 > 7 ? 7 : dk2);
			}
		}
	}

	// the falcon: a swept-wing silhouette, bigger than any starling, its wings
	// along its screen heading
	void draw_falcon() {
		float ex = BD_FX - cam_x;
		float ey = BD_FY - cam_y;
		float ez = BD_FZ - cam_z;
		float zc = ex * fwx + ey * fwy + ez * fwz;
		if (zc < 8.0) { return; }
		float iz = focal / zc;
		float sx = w * 0.5 + (ex * rx_ + ez * rz_) * iz;
		float sy = h * 0.5 - (ex * upx + ey * upy + ez * upz) * iz;
		float hx = fvx * rx_ + fvz * rz_;
		float hy = -(fvx * upx + fvy * upy + fvz * upz);
		float hl = math::sqrt(hx * hx + hy * hy) + 0.0001;
		hx /= hl;
		hy /= hl;
		float span = 4.0 + 7.0 * iz;
		float beat = math::sin(tt * 9.0) * 0.6;
		var& dn = dens;
		for (int k = -14; k <= 14; ++k) {
			float a = (k < 0 ? -k : k) / 14.0;
			float s = k / 14.0 * span;
			float back = a * span * 0.55;
			float px = sx - hy * s - hx * back;
			float py = sy + hx * s - hy * back + a * beat * span * 0.4;
			int ix = itrunc(px);
			int iy = itrunc(py);
			if (ix >= 0 && ix < w && iy >= 0 && iy + 1 < horizon) {
				dn[iy * w + ix] += 4.0;
				if (a < 0.6) { dn[(iy + 1) * w + ix] += 4.0; }
			}
		}
		for (int k2 = -2; k2 <= 3; ++k2) {
			int bx = itrunc(sx + hx * k2 * span * 0.18);
			int by = itrunc(sy + hy * k2 * span * 0.18);
			if (bx >= 0 && bx < w && by >= 0 && by < horizon) { dn[by * w + bx] += 3.0; }
		}
	}
}
