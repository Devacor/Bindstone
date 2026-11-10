// OSSUARY — twelve bars. A skull on eight bone legs lowers asleep down one thread
// across a round window of moonlit haze. On bar 2 its eyes catch fire and it throws
// an orb web in two bars: the spokes a sixteenth note apart, the rings an eighth
// apart. A golden moth flutters in from beside the lens and strikes the silk on
// bar 6; the strike rings out through the web, the hunter runs it down and winds it
// into a cocoon, walks back to the hub, rears, and on bar 10 leaps at the lens and
// holds the frame, glaring, its jaw snapping on the kick.
//
// The picture is painted back to front in one pass with no depth buffer: the window
// and its wall copied out of a map of the whole wall, the web as lines, the moth as
// triangles, the legs as tapered spans posed by two-bone inverse kinematics, and the
// skull and its jaw as sprites baked from a height field at three sizes (the two
// larger a few rows a frame, under the title). Cold blues carry the world; the only
// warm things in it are the eyes and the moth.

array<float> OS_DENSITY = [16.0, 32.0, 64.0];
array<float> OS_PIECE_LEFT = [-1.1, -0.7];
array<float> OS_PIECE_TOP = [1.25, -0.7];
array<float> OS_PIECE_WIDE = [2.2, 1.4];
array<float> OS_PIECE_TALL = [2.2, 0.7];
array<int> OS_BAKE_PIECE = [0, 1, 0, 1];
array<int> OS_BAKE_MIP = [1, 1, 2, 2];
array<int> OS_SPOKE_ORDER = [0, 6, 3, 9, 1, 7, 4, 10, 2, 8, 5, 11];
array<float> OS_REST_ANGLE = [0.68, 0.12, -0.42, -1.0];
array<float> OS_REST_REACH = [0.84, 0.86, 0.84, 0.76];
array<float> OS_THREAT_ANGLE = [1.3, 0.95, 0.08, -0.55];
array<float> OS_THREAT_REACH = [0.78, 0.8, 0.84, 0.8];
int OS_SPOKES = 12;
int OS_RINGS = 7;
float OS_RIM = 1.32;
float OS_VIEW_TALL = 2.35;
float OS_WALL_TOP = 2.0;
float OS_WALL_TALL = 3.3;
float OS_FEMUR = 0.3;
float OS_TIBIA = 0.4;
float OS_HEAD = 0.3;

class Ossuary : Scene {
	Ossuary() { pixel_budget = 36000; }

	int night = 0;
	int stone = 0;
	int bone = 0;
	int ember = 0;
	int silk = 0;
	int gold = 0;
	int ink = 0;

	array<int> texels = [];
	array<int> slot_at = [];
	array<int> slot_w = [];
	array<int> slot_h = [];
	array<float> height_above = [];
	array<float> height_here = [];
	array<float> height_below = [];
	array<int> mark_above = [];
	array<int> mark_here = [];
	array<int> mark_below = [];
	array<int> texel_colour = [];
	int baked_rows = 0;
	int ready_mip = 0;
	int shape_mark = 0;

	array<int> fog_tile = [];
	array<int> stone_tile = [];
	array<int> backdrop = [];
	int backdrop_wide = 0;
	int backdrop_tall = 0;
	float backdrop_left = 0.0;
	float backdrop_density = 1.0;

	array<float> spoke_angle = [];
	array<float> knot_reach = [];
	array<float> knot_x = [];
	array<float> knot_y = [];
	array<int> knot_glow = [];

	float t_wake = 3.871;
	float t_rings = 5.806;
	float t_web = 7.742;
	float t_moth = 8.0;
	float t_impact = 11.613;
	float t_rush = 11.85;
	float t_reach = 12.65;
	float t_wrap = 15.484;
	float t_home = 15.9;
	float t_hub = 17.2;
	float t_rear = 17.419;
	float t_lunge = 19.355;
	float strike_x = 0.78;
	float strike_y = 0.45;
	float stop_x = 0.31;
	float stop_y = 0.38;

	float tt = 0.0;
	float kick = 0.0;
	float struck = 0.0;
	float body_x = 0.0;
	float body_y = 1.25;
	float body_rot = 0.0;
	float body_scale = 1.0;
	float eye_glow = 0.0;
	float gape = 0.0;
	float gaze_x = 0.0;
	float gaze_y = 0.0;
	float cam_x = 0.0;
	float cam_y = 0.0;
	float zoom = 1.0;
	bool placed = false;
	array<float> foot_x = [];
	array<float> foot_y = [];
	array<float> from_x = [];
	array<float> from_y = [];
	array<float> step_age = [];
	array<float> toe_x = [];
	array<float> toe_y = [];
	float moth_x = 0.0;
	float moth_y = 0.0;
	float moth_size = 1.0;
	float moth_flap = 0.0;
	float cocoon = 0.0;
	array<float> spark_x = [];
	array<float> spark_y = [];
	array<float> spark_vx = [];
	array<float> spark_vy = [];
	array<float> spark_life = [];
	array<int> spark_colour = [];
	int spark_next = 0;
	float dust_due = 0.0;
	float knee_x = 0.0;
	float knee_y = 0.0;
	float tip_x = 0.0;
	float tip_y = 0.0;

	void build() override {
		name = "OSSUARY";
		handle = "eight legs of inverse kinematics . an orb web . a moth that came too close";
		replay = 23.5;
		night = pal.gradient(48, [[3.0, 4.0, 14.0], [9.0, 13.0, 40.0], [22.0, 34.0, 84.0], [44.0, 70.0, 132.0], [86.0, 120.0, 182.0]]);
		stone = pal.gradient(24, [[2.0, 2.0, 8.0], [12.0, 13.0, 30.0], [34.0, 40.0, 72.0], [96.0, 112.0, 160.0]]);
		bone = pal.size();
		array<float> cool = [16.0, 18.0, 44.0, 56.0, 66.0, 116.0, 124.0, 140.0, 186.0, 204.0, 216.0, 238.0, 255.0, 255.0, 250.0];
		array<float> warm = [34.0, 8.0, 18.0, 120.0, 36.0, 30.0, 214.0, 104.0, 56.0, 255.0, 190.0, 120.0, 255.0, 244.0, 214.0];
		for (int row = 0; row < 4; ++row) {
			float lit = row * 0.3;
			var stops = [];
			for (int s = 0; s < 5; ++s) {
				stops.push([cool[s * 3] + (warm[s * 3] - cool[s * 3]) * lit, cool[s * 3 + 1] + (warm[s * 3 + 1] - cool[s * 3 + 1]) * lit, cool[s * 3 + 2] + (warm[s * 3 + 2] - cool[s * 3 + 2]) * lit]);
			}
			pal.gradient(16, stops);
		}
		ember = pal.gradient(12, [[26.0, 3.0, 10.0], [120.0, 14.0, 10.0], [230.0, 70.0, 10.0], [255.0, 170.0, 50.0], [255.0, 246.0, 200.0]]);
		silk = pal.size();
		for (int hue = 0; hue < 12; ++hue) {
			var c = gfx.rainbow_rgb(hue * 0.5236 + 3.4);
			float r = c[0]; float g = c[1]; float b = c[2];
			pal.add(70.0 + r * 0.08, 88.0 + g * 0.08, 132.0 + b * 0.06);
			pal.add(128.0 + r * 0.17, 142.0 + g * 0.17, 178.0 + b * 0.14);
			pal.add(140.0 + r * 0.42, 140.0 + g * 0.42, 156.0 + b * 0.38);
			pal.add(232.0 + r * 0.09, 232.0 + g * 0.09, 232.0 + b * 0.09);
		}
		gold = pal.gradient(8, [[70.0, 26.0, 8.0], [190.0, 96.0, 14.0], [255.0, 190.0, 60.0], [255.0, 244.0, 190.0]]);
		ink = pal.add(4.0, 4.0, 12.0);

		for (int v = 0; v < 64; ++v) {
			for (int u = 0; u < 64; ++u) {
				float drift = math::sin(u * 0.0982) * math::sin(v * 0.1963 + 1.3) + 0.6 * math::sin((u + v) * 0.1963 + 0.7) * math::sin((u - v) * 0.0982);
				fog_tile.push(math::itrunc(26.0 + 16.0 * drift));
				bool mortar = v % 16 == 0 || (u + (v / 16 % 2) * 16) % 32 == 0;
				stone_tile.push(mortar ? 0 : 20 + (u * 7 + v * 13) % 5 * 3);
			}
		}
		backdrop_density = h / OS_VIEW_TALL;
		backdrop_left = -(w * 0.5 / backdrop_density + 0.25);
		backdrop_wide = w + math::itrunc(0.5 * backdrop_density) + 1;
		backdrop_tall = math::itrunc(OS_WALL_TALL * backdrop_density) + 1;
		backdrop.resize(backdrop_wide * backdrop_tall, -1);

		for (int slot = 0; slot < 6; ++slot) {
			float density = OS_DENSITY[slot % 3];
			slot_at.push(texels.size());
			slot_w.push(math::itrunc(OS_PIECE_WIDE[slot / 3] * density) + 1);
			slot_h.push(math::itrunc(OS_PIECE_TALL[slot / 3] * density) + 1);
			for (int n = slot_w[slot] * slot_h[slot]; n > 0; --n) { texels.push(-1); }
		}
		for (int x = 0; x < 160; ++x) {
			height_above.push(-1.0); height_here.push(-1.0); height_below.push(-1.0);
			mark_above.push(-1); mark_here.push(-1); mark_below.push(-1);
		}
		for (int code = 0; code < 192; ++code) { texel_colour.push(0); }
		for (int piece = 0; piece < 2; ++piece) {
			for (int row = 0; row < slot_h[piece * 3]; ++row) { bake_row(piece, 0, row); }
		}

		for (int k = 0; k < OS_SPOKES; ++k) {
			spoke_angle.push(1.5707963 + k * 6.2831853 / OS_SPOKES + (k == 0 ? 0.0 : (rng.next(13) - 6) * 0.012));
			for (int j = 0; j <= OS_RINGS; ++j) {
				knot_reach.push(j == OS_RINGS ? OS_RIM : 0.22 + 0.13 * j + (rng.next(9) - 4) * 0.004);
				knot_x.push(0.0);
				knot_y.push(0.0);
				knot_glow.push(1);
			}
		}
		for (int i = 0; i < 8; ++i) {
			foot_x.push(0.0); foot_y.push(0.0); from_x.push(0.0); from_y.push(0.0);
			step_age.push(-1.0); toe_x.push(0.0); toe_y.push(0.0);
		}
		for (int p = 0; p < 40; ++p) {
			spark_x.push(0.0); spark_y.push(0.0); spark_vx.push(0.0); spark_vy.push(0.0); spark_life.push(0.0); spark_colour.push(0);
		}
	}

	int length_bars() override { return 12; }

	array<float> hit_times() override { return [3.871, 7.742, 11.613, 15.484, 19.355]; }
	array<int> hit_levels() override { return [1, 1, 2, 1, 2]; }

	float smooth(float u) {
		float c = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
		return c * c * (3.0 - 2.0 * c);
	}

	// the skull (piece 0) or its jaw (piece 1) as a height over the face, in skull units; below zero outside it.
	// shape_mark is how much eye light the spot catches (0..3), plus 4 where it is a hollow
	float shape(int piece, float x, float y, float density) {
		float ax = x < 0.0 ? -x : x;
		float tooth_pitch = density >= 30.0 ? 0.15 : 0.25;
		float gap = 0.5 / (density * tooth_pitch);
		gap = gap < 0.09 ? 0.09 : gap;
		if (piece == 1) {
			if (y > -0.76 || y < -1.36) { return -1.0; }
			shape_mark = y > -1.05 ? 2 : 1;
			if (y > -0.93) {
				if (ax > 0.4) { return -1.0; }
				float low = x / tooth_pitch;
				low = low - math::floor(low) - 0.5;
				if (low < gap && low > -gap) { return -1.0; }
				return 0.5;
			}
			float half = 0.5 + (y + 0.93) * 0.3;
			float inside = 1.0 - (x / half) * (x / half);
			if (inside <= 0.0) { return -1.0; }
			float chin = (y + 1.36) / 0.16;
			return 0.45 * math::sqrt(inside * (chin > 1.0 ? 1.0 : chin)) + 0.02;
		}
		float result = -1.0;
		float cx = x / 0.95;
		float cy = (y - 0.32) / 0.82;
		float dome = 1.0 - cx * cx - cy * cy;
		if (dome > 0.0) { result = 0.9 * math::sqrt(dome) + 0.02; }
		bool face = false;
		if (y < 0.2 && y > -0.88) {
			float half = 0.46 + (y + 0.88) * 0.22;
			float inside = 1.0 - (x / half) * (x / half);
			if (inside > 0.0) {
				float block = 0.55 * math::sqrt(math::sqrt(inside)) + 0.02;
				result = block > result ? block : result;
				face = true;
			}
		}
		if (result < 0.0) { return -1.0; }
		float ex = ax - 0.38;
		float ey = y - 0.02;
		float lit = ex * ex + ey * ey;
		shape_mark = lit < 0.16 ? 3 : (lit < 0.42 ? 2 : (lit < 0.9 ? 1 : 0));
		float sy = (ey - 0.35 * ex) / 0.25;
		if (ex * ex / 0.0729 + sy * sy < 1.0) {
			shape_mark = 7;
			return 0.12;
		}
		if (y < -0.2 && y > -0.5 && ax < 0.015 + 0.16 * (-0.2 - y) / 0.3) {
			shape_mark = 5;
			return 0.15;
		}
		if (face && y < -0.64) {
			float up = (x + tooth_pitch * 0.5) / tooth_pitch;
			up = up - math::floor(up) - 0.5;
			if (up < gap && up > -gap) {
				shape_mark = 4;
				return 0.25;
			}
			return result + 0.06;
		}
		return result;
	}

	void shape_row(int piece, int mip, int row, var& heights, var& marks) {
		int slot = piece * 3 + mip;
		int tw = slot_w[slot];
		float density = OS_DENSITY[mip];
		float left = OS_PIECE_LEFT[piece];
		float y = OS_PIECE_TOP[piece] - (row + 0.5) / density;
		bool off = row < 0 || row >= slot_h[slot];
		for (int x = 0; x < tw; ++x) {
			float height = off ? -1.0 : shape(piece, left + (x + 0.5) / density, y, density);
			heights[x] = height;
			marks[x] = height < 0.0 ? -1 : shape_mark;
		}
	}

	// one row of a sprite: bone lit from the upper left by the slope of its height, hollows, and an ink line round the
	// outside. A texel is -1 for nothing, else kind * 64 + eye light * 16 + level (kind 0 bone, 1 ink, 2 hollow)
	void bake_row(int piece, int mip, int row) {
		int slot = piece * 3 + mip;
		int tw = slot_w[slot];
		float density = OS_DENSITY[mip];
		if (row == 0) {
			shape_row(piece, mip, -1, height_above, mark_above);
			shape_row(piece, mip, 0, height_here, mark_here);
		}
		shape_row(piece, mip, row + 1, height_below, mark_below);
		var& above = height_above;
		var& here = height_here;
		var& below = height_below;
		var& marks = mark_here;
		var& out = texels;
		int at = slot_at[slot] + row * tw;
		for (int x = 0; x < tw; ++x) {
			float height = here[x];
			float west = x > 0 ? here[x - 1] : -1.0;
			float east = x < tw - 1 ? here[x + 1] : -1.0;
			float north = above[x];
			float south = below[x];
			if (height < 0.0) {
				out[at + x] = west >= 0.0 || east >= 0.0 || north >= 0.0 || south >= 0.0 ? 64 : -1;
				continue;
			}
			int mark = marks[x];
			if (mark >= 4) {
				out[at + x] = 128 + (mark - 4) * 16;
				continue;
			}
			float gx = ((east < 0.0 ? 0.0 : east) - (west < 0.0 ? 0.0 : west)) * density * 0.5;
			float gy = ((north < 0.0 ? 0.0 : north) - (south < 0.0 ? 0.0 : south)) * density * 0.5;
			float facing = (0.45 * gx - 0.6 * gy + 0.66) / math::sqrt(gx * gx + gy * gy + 1.0);
			facing = facing < 0.0 ? 0.0 : facing;
			float open = 0.7 + 0.3 * (height > 0.35 ? 1.0 : height / 0.35);
			int level = math::itrunc(2.5 + 13.4 * (0.15 + 0.85 * facing) * open);
			out[at + x] = mark * 16 + (level > 15 ? 15 : level);
		}
		height_above = height_here;
		height_here = height_below;
		mark_above = mark_here;
		mark_here = mark_below;
	}

	// the two larger sizes of the sprites bake a few rows a frame, by the clock of the scene so a replay bakes the same
	void bake_ahead(float t) {
		int due = math::itrunc(t * 70.0);
		while (baked_rows < due && ready_mip < 2) {
			int row = baked_rows;
			int job = 0;
			while (job < 4 && row >= slot_h[OS_BAKE_PIECE[job] * 3 + OS_BAKE_MIP[job]]) {
				row -= slot_h[OS_BAKE_PIECE[job] * 3 + OS_BAKE_MIP[job]];
				++job;
			}
			if (job >= 4) {
				ready_mip = 2;
				break;
			}
			bake_row(OS_BAKE_PIECE[job], OS_BAKE_MIP[job], row);
			++baked_rows;
			if (job == 2 && row == 0) { ready_mip = 1; }
		}
	}

	float mix(float a, float b, float u) { return a + (b - a) * u; }

	float lunge(float t) { return smooth((t - t_lunge + 0.06) / 0.2); }

	// how far the web is spun: spoke n of the firing order shoots out over a tenth of a second from its sixteenth note
	float spoke_grown(int n, float t) {
		if (n == 0) { return 1.0; }
		float u = (t - t_wake - n * MUSIC_STEP) / 0.1;
		return u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
	}

	// ring j whips once round the hub from its eighth note
	float ring_swept(int j, float t) {
		float u = (t - t_rings - j * MUSIC_STEP * 2.0) / 0.22;
		return u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
	}

	void update(float t, float dt) override {
		tt = t;
		bake_ahead(t);
		kick = cue != null ? cue.kick_env(t) : 0.0;
		struck = cue != null ? cue.hit(t) : 0.0;
		pose_moth(t);
		pose_body(t);
		walk(t, dt);
		aim_camera(t);
		move_sparks(t, dt);
	}

	void pose_moth(float t) {
		cocoon = smooth((t - t_reach - 0.15) / (t_wrap - t_reach - 0.4));
		moth_flap = t * 30.0;
		if (t < t_moth) {
			moth_size = 0.0;
			return;
		}
		if (t < t_impact) {
			float u = (t - t_moth) / (t_impact - t_moth);
			float e = u * (0.7 + 0.3 * u);
			float f = 1.0 - e;
			moth_x = f * f * f * 1.55 + 3.0 * f * f * e * 0.5 + 3.0 * f * e * e * 1.3 + e * e * e * strike_x + math::sin(t * 9.0) * 0.06 * f;
			moth_y = f * f * f * -0.55 - 3.0 * f * f * e + 3.0 * f * e * e * 0.3 + e * e * e * strike_y + math::cos(t * 7.0) * 0.05 * f;
			moth_size = 2.4 - 1.4 * e;
			return;
		}
		float fight = math::exp((t_impact - t) * 0.8) * (1.0 - cocoon);
		moth_x = strike_x + math::sin(t * 31.0) * 0.014 * fight;
		moth_y = strike_y + math::cos(t * 27.0) * 0.014 * fight;
		moth_size = 1.0;
		moth_flap = t * 44.0 * (0.35 + 0.65 * fight);
	}

	void pose_body(float t) {
		float lower = smooth(t / 3.7);
		float swing = 0.14 * math::sin(t * 1.9) * (1.0 - smooth((t - 2.6) / 1.2));
		float out = smooth((t - t_rush) / (t_reach - t_rush)) * (1.0 - smooth((t - t_home) / (t_hub - t_home)));
		float rear = smooth((t - t_rear) / (t_lunge - t_rear - 0.1));
		float leap = lunge(t);
		float after = t > t_lunge ? t - t_lunge : 0.0;
		body_y = 1.25 * (1.0 - lower);
		body_x = swing * (OS_RIM - body_y) * 0.5 + stop_x * out;
		body_y += stop_y * out;
		body_rot = swing - 0.26 * out + 0.03 * math::sin(t * 23.0) * rear * (1.0 - leap) + 0.035 * math::sin(t * 1.3) * leap;
		float since = t - t_wake;
		if (since >= 0.0 && since < 12.0 * MUSIC_STEP + 0.4) {
			int n = math::itrunc(since / MUSIC_STEP);
			n = n > 11 ? 11 : n;
			body_rot -= 0.22 * math::cos(spoke_angle[OS_SPOKE_ORDER[n]]) * math::exp((n * MUSIC_STEP - since) * 7.0);
		}
		float awake = smooth(since / 0.2);
		body_scale = 1.0 + 0.035 * kick * awake + 0.16 * rear + 0.7 * leap + (t > t_lunge ? 0.6 * math::exp(after * -4.0) + 0.02 * after : 0.0);

		eye_glow = 0.3 * kick * smooth((t - 1.9) / 0.5);
		if (since >= 0.0) { eye_glow = 1.0 + 0.9 * math::exp(since * -3.0) + 0.2 * kick; }
		eye_glow += 0.3 * smooth((t - t_wrap) / 0.3) + 0.4 * rear + 0.5 * leap;
		float bite = 0.4 + 0.7 * math::exp(after * -1.8) + 0.35 * kick;
		gape = 0.12 * rear * (1.0 - leap) + leap * (bite > 1.15 ? 1.15 : bite);

		float look_x = 0.0;
		float look_y = 0.0;
		float sweep = t - t_rings;
		if (sweep >= 0.0 && t < t_web) {
			float turn = sweep / (MUSIC_STEP * 2.0);
			float angle = 1.5707963 + (turn - math::floor(turn)) * 6.2831853;
			look_x = math::cos(angle);
			look_y = math::sin(angle);
		} else if (moth_size > 0.0 && t < t_wrap) {
			look_x = (moth_x - body_x) * 2.5;
			look_y = (moth_y - body_y) * 2.5;
			float far = math::sqrt(look_x * look_x + look_y * look_y);
			if (far > 1.0) {
				look_x /= far;
				look_y /= far;
			}
		}
		gaze_x += (look_x - gaze_x) * 0.35;
		gaze_y += (look_y - gaze_y) * 0.35;
	}

	// a tetrapod gait on the silk: a foot steps when its rest spot has left it behind and the other four are down;
	// then the poses that take the feet off the web (asleep and curled, casting a spoke, winding the moth, reared)
	// blend over it
	void walk(float t, float dt) {
		float c = math::cos(body_rot);
		float s = math::sin(body_rot);
		bool even_moving = false;
		bool odd_moving = false;
		for (int i = 0; i < 8; ++i) {
			if (step_age[i] >= 0.0) {
				if ((i + i / 4) % 2 == 0) { even_moving = true; } else { odd_moving = true; }
			}
		}
		float threat = smooth((t - t_rear) / 1.0);
		float since_wake = t - t_wake;
		bool casting = since_wake >= MUSIC_STEP && since_wake < OS_SPOKES * MUSIC_STEP;
		int shot = casting ? math::itrunc(since_wake / MUSIC_STEP) : 0;
		float shot_x = math::cos(spoke_angle[OS_SPOKE_ORDER[shot]]);
		float shot_y = math::sin(spoke_angle[OS_SPOKE_ORDER[shot]]);
		float flick = math::exp((shot * MUSIC_STEP - since_wake) * 10.0);
		float reaching = smooth((t - t_reach) / 0.2) * (1.0 - smooth((t - t_wrap) / 0.4));
		for (int i = 0; i < 8; ++i) {
			int j = i % 4;
			float side = i < 4 ? 1.0 : -1.0;
			float ox = side * math::cos(OS_REST_ANGLE[j]) * OS_REST_REACH[j];
			float oy = math::sin(OS_REST_ANGLE[j]) * OS_REST_REACH[j];
			float rx = body_x + c * ox - s * oy;
			float ry = body_y + s * ox + c * oy;
			bool even = (i + i / 4) % 2 == 0;
			if (step_age[i] >= 0.0) {
				step_age[i] += dt / 0.11;
				float e = smooth(step_age[i]);
				foot_x[i] = from_x[i] + (rx - from_x[i]) * e;
				foot_y[i] = from_y[i] + (ry - from_y[i]) * e;
				if (step_age[i] >= 1.0) { step_age[i] = -1.0; }
			} else {
				float dx = rx - foot_x[i];
				float dy = ry - foot_y[i];
				float off = dx * dx + dy * dy;
				if (!placed || off > 0.36) {
					foot_x[i] = rx;
					foot_y[i] = ry;
				} else if (off > 0.0144 && !(even ? odd_moving : even_moving)) {
					from_x[i] = foot_x[i];
					from_y[i] = foot_y[i];
					step_age[i] = 0.0;
					if (even) { even_moving = true; } else { odd_moving = true; }
				}
			}
			float tx = foot_x[i];
			float ty = foot_y[i];
			float curl = 1.0 - smooth((t - t_wake) / 0.16 - j * 0.2);
			if (curl > 0.0) {
				float cx = side * (0.05 + 0.05 * j);
				float cy = -0.46 - 0.03 * j;
				tx = mix(tx, body_x + c * cx - s * cy, curl);
				ty = mix(ty, body_y + s * cx + c * cy, curl);
			}
			if (casting && shot_x * side * math::cos(OS_REST_ANGLE[j]) + shot_y * math::sin(OS_REST_ANGLE[j]) > 0.6) {
				tx = mix(tx, body_x + shot_x * 0.9, flick);
				ty = mix(ty, body_y + shot_y * 0.9, flick);
			}
			if (i < 2 && reaching > 0.0) {
				float pull = 0.07 * math::sin(t * 6.2831853 / MUSIC_BEAT + i * 3.1415927);
				tx = mix(tx, moth_x - 0.05, reaching);
				ty = mix(ty, moth_y + (i == 0 ? 0.07 : -0.07) + pull, reaching);
			}
			if (threat > 0.0) {
				float raise = OS_THREAT_ANGLE[j] + (j == 0 ? 0.12 * kick : 0.0);
				float px = side * math::cos(raise) * OS_THREAT_REACH[j] * body_scale;
				float py = math::sin(raise) * OS_THREAT_REACH[j] * body_scale;
				tx = mix(tx, body_x + c * px - s * py, threat);
				ty = mix(ty, body_y + s * px + c * py, threat);
			}
			toe_x[i] = tx;
			toe_y[i] = ty;
		}
		placed = true;
	}

	void aim_camera(float t) {
		float wake = smooth((t - t_wake) / 0.5);
		float approach = smooth((t - t_moth) / (t_impact - t_moth));
		float close = smooth((t - t_impact - 0.1) / 1.0);
		float back = smooth((t - t_wrap - 0.15) / 1.6);
		float rear = smooth((t - t_rear) / (t_lunge - t_rear));
		float leap = lunge(t);
		float lens = mix(mix(mix(mix(mix(1.7, 1.0, wake), 1.12, approach), 1.7, close), 1.2, back), 1.45, rear);
		cam_x = mix(mix(mix(mix(body_x, 0.0, wake), 0.2, approach), 0.6, close), 0.0, back);
		cam_y = mix(mix(mix(mix(body_y - 0.06, 0.0, wake), 0.12, approach), 0.42, close), 0.02, back);
		cam_y = mix(cam_y, body_y - 0.05 * body_scale, leap);
		cam_x += 0.012 * math::sin(t * 0.7);
		zoom = h / OS_VIEW_TALL * lens;
	}

	void spark(float x, float y, float vx, float vy, float life, int colour) {
		spark_x[spark_next] = x;
		spark_y[spark_next] = y;
		spark_vx[spark_next] = vx;
		spark_vy[spark_next] = vy;
		spark_life[spark_next] = life;
		spark_colour[spark_next] = colour;
		spark_next = (spark_next + 1) % 40;
	}

	// the moth sheds gold dust on the wing and a burst where it strikes
	void move_sparks(float t, float dt) {
		for (int p = 0; p < 40; ++p) {
			if (spark_life[p] <= 0.0) { continue; }
			spark_life[p] -= dt;
			spark_x[p] += spark_vx[p] * dt;
			spark_y[p] += spark_vy[p] * dt;
		}
		if (t >= t_impact && t - dt < t_impact) {
			for (int n = 0; n < 14; ++n) {
				float angle = n * 0.4488 + rng.next(30) * 0.01;
				float speed = 0.35 + rng.next(40) * 0.01;
				spark(strike_x, strike_y, math::cos(angle) * speed, math::sin(angle) * speed, 0.35 + rng.next(30) * 0.01, n % 2 == 0 ? gold + 4 : silk + 2);
			}
		}
		if (t < dust_due) { return; }
		if (moth_size > 0.0 && t < t_impact) {
			dust_due = t + 0.07;
			spark(moth_x, moth_y, (rng.next(21) - 10) * 0.012, -0.12 - rng.next(10) * 0.01, 0.55, gold + 3);
		}
	}

	void render(var& grid) override {
		paint_backdrop(grid);
		place_knots();
		paint_web(grid);
		paint_moth(grid);
		paint_legs(grid);
		paint_head(grid);
		paint_sparks(grid);
	}

	// one row of the wall map, a texel a pixel at the widest lens: the round window with its moon haze, brightest
	// up and to the left, and the blocks of the wall it is cut in
	void bake_backdrop_row(int v) {
		var& wall = backdrop;
		var& fog = fog_tile;
		var& blocks = stone_tile;
		int wide = backdrop_wide;
		float left = backdrop_left;
		float per_texel = 1.0 / backdrop_density;
		float y = OS_WALL_TOP - (v + 0.5) * per_texel;
		float haze_y = y - 0.55;
		int fog_row = (v & 63) * 64;
		int block_row = (math::ifloor(y * -26.0) & 63) * 64;
		int at = v * wide;
		for (int u = 0; u < wide; ++u) {
			float x = left + (u + 0.5) * per_texel;
			float beyond = x * x + y * y - OS_RIM * OS_RIM;
			if (beyond < 0.0) {
				float haze_x = x + 0.5;
				float far = (haze_x * haze_x + haze_y * haze_y) / 6.2;
				float near = far < 1.0 ? 1.0 - math::sqrt(far) : 0.0;
				wall[at + u] = night + ((math::itrunc(16.0 * (7.0 + 31.0 * near * near)) + fog[fog_row + (u & 63)]) >> 4);
			} else {
				float edge = beyond < 0.9 ? 1.0 - beyond / 0.9 : 0.0;
				wall[at + u] = stone + ((math::itrunc(16.0 * (2.5 + 17.0 * edge * edge * edge)) + blocks[block_row + (math::ifloor(x * 26.0) & 63)]) >> 4);
			}
		}
	}

	// the camera's view of the wall map, its rows baked as the camera first sees them; a hit lifts the haze
	void paint_backdrop(var& grid) {
		int width = w;
		int height = h;
		var& wall = backdrop;
		int wide = backdrop_wide;
		int tall = backdrop_tall;
		float texels = backdrop_density / zoom;
		int step = math::ifloor(65536.0 * texels);
		int from = math::ifloor(((cam_x - backdrop_left) * backdrop_density + (0.5 - width * 0.5) * texels) * 65536.0);
		float row_from = (OS_WALL_TOP - cam_y) * backdrop_density + (0.5 - height * 0.5) * texels;
		int first = 0;
		while (first < width && from + first * step < 0) { ++first; }
		int last = width;
		while (last > first && from + (last - 1) * step >= wide << 16) { --last; }
		int lift = math::ifloor(4.4 * struck);
		int wall_at = stone;
		int i = 0;
		for (int y = 0; y < height; ++y) {
			int v = math::ifloor(row_from + y * texels);
			v = v < 0 ? 0 : (v >= tall ? tall - 1 : v);
			int row = v * wide;
			if (wall[row] < 0) { bake_backdrop_row(v); }
			for (int x = 0; x < first; ++x) {
				grid[i] = wall_at;
				++i;
			}
			int u = from + first * step;
			if (lift == 0) {
				for (int x = first; x < last; ++x) {
					grid[i] = wall[row + (u >> 16)];
					u += step;
					++i;
				}
			} else {
				for (int x = first; x < last; ++x) {
					int colour = wall[row + (u >> 16)];
					grid[i] = colour < wall_at ? colour + lift : colour;
					u += step;
					++i;
				}
			}
			for (int x = last; x < width; ++x) {
				grid[i] = wall_at;
				++i;
			}
		}
	}

	// where every knot of the web is on screen this frame and how brightly it shines: the silk breathes, a sheen runs
	// out along it from every kick, and the moth's strike rings through it from where it hit
	void place_knots() {
		float t = tt;
		float scale = zoom;
		float hub_x = w * 0.5 - cam_x * scale;
		float hub_y = h * 0.5 + cam_y * scale;
		float rung = t - t_impact;
		float front = rung * 1.5;
		float swell = rung > 0.0 ? 0.05 * math::exp(rung * -1.6) : 0.0;
		float sheen = (1.0 - kick) * 1.3;
		float done = t - t_web;
		int flash = done >= 0.0 && done < 0.5 ? (done < 0.16 ? 3 : 2) : 1;
		int across = OS_RINGS + 1;
		for (int k = 0; k < OS_SPOKES; ++k) {
			float c = math::cos(spoke_angle[k]);
			float s = math::sin(spoke_angle[k]);
			for (int j = 0; j < across; ++j) {
				int at = k * across + j;
				float reach = knot_reach[at];
				int level = flash;
				if (j < OS_RINGS) {
					reach += 0.006 * math::sin(t * 1.7 + k * 1.3 + j * 0.7);
					float off = reach - sheen;
					if (kick > 0.25 && off < 0.09 && off > -0.09) { level = level < 2 ? 2 : level; }
					if (swell > 0.0) {
						float dx = c * reach - strike_x;
						float dy = s * reach - strike_y;
						float far = math::sqrt(dx * dx + dy * dy);
						if (far < front) {
							reach += swell * math::sin(far * 14.0 - rung * 22.0);
							if (front - far < 0.24 && rung < 1.5) { level = 3; }
						}
					}
				} else if (flash == 1) {
					level = 0;
				}
				knot_x[at] = hub_x + c * reach * scale;
				knot_y[at] = hub_y - s * reach * scale;
				knot_glow[at] = level;
			}
		}
	}

	void strand(var& grid, float ax, float ay, float bx, float by, int colour) {
		int width = w;
		int height = h;
		if ((ax < 0.0 && bx < 0.0) || (ay < 0.0 && by < 0.0) || (ax >= width && bx >= width) || (ay >= height && by >= height)) { return; }
		float dx = bx - ax;
		float dy = by - ay;
		float adx = dx < 0.0 ? -dx : dx;
		float ady = dy < 0.0 ? -dy : dy;
		int steps = math::itrunc(adx > ady ? adx : ady) + 1;
		float step_x = dx / steps;
		float step_y = dy / steps;
		float x = ax;
		float y = ay;
		for (int n = 0; n <= steps; ++n) {
			if (x >= 0.0 && y >= 0.0 && x < width && y < height) { grid[math::itrunc(y) * width + math::itrunc(x)] = colour; }
			x += step_x;
			y += step_y;
		}
	}

	void paint_web(var& grid) {
		float t = tt;
		float scale = zoom;
		var& kx = knot_x;
		var& ky = knot_y;
		var& glow = knot_glow;
		int across = OS_RINGS + 1;
		float hub_x = w * 0.5 - cam_x * scale;
		float hub_y = h * 0.5 + cam_y * scale;
		if (t < t_wake) {
			strand(grid, kx[OS_RINGS], ky[OS_RINGS], hub_x + body_x * scale, hub_y - body_y * scale, silk + 2);
			return;
		}
		int hue_turn = math::itrunc(t * 3.0);
		for (int n = 0; n < OS_SPOKES; ++n) {
			float grown = spoke_grown(n, t) * OS_RIM;
			if (grown <= 0.0) { break; }
			int k = OS_SPOKE_ORDER[n];
			bool fresh = t - t_wake - n * MUSIC_STEP < 0.2;
			float from_x = hub_x;
			float from_y = hub_y;
			float from_reach = 0.0;
			for (int j = 0; j < across; ++j) {
				int at = k * across + j;
				float reach = knot_reach[at];
				int colour = silk + ((k + j + hue_turn) % 12) * 4 + (fresh ? 3 : glow[at]);
				if (reach > grown) {
					float part = (grown - from_reach) / (reach - from_reach);
					strand(grid, from_x, from_y, from_x + (kx[at] - from_x) * part, from_y + (ky[at] - from_y) * part, colour);
					break;
				}
				strand(grid, from_x, from_y, kx[at], ky[at], colour);
				from_x = kx[at];
				from_y = ky[at];
				from_reach = reach;
			}
		}
		for (int j = 0; j < OS_RINGS; ++j) {
			float swept = ring_swept(j, t) * OS_SPOKES;
			if (swept <= 0.0) { break; }
			bool fresh = t - t_rings - j * MUSIC_STEP * 2.0 < 0.34;
			for (int k = 0; k < OS_SPOKES && k < swept; ++k) {
				int a = k * across + j;
				int b = ((k + 1) % OS_SPOKES) * across + j;
				int level = glow[a] > glow[b] ? glow[a] : glow[b];
				int colour = silk + ((k + j + hue_turn) % 12) * 4 + (fresh ? 3 : level);
				float part = swept - k;
				if (part < 1.0) {
					strand(grid, kx[a], ky[a], kx[a] + (kx[b] - kx[a]) * part, ky[a] + (ky[b] - ky[a]) * part, colour);
					break;
				}
				float sag_x = hub_x + ((kx[a] + kx[b]) * 0.5 - hub_x) * 0.95;
				float sag_y = hub_y + ((ky[a] + ky[b]) * 0.5 - hub_y) * 0.95;
				strand(grid, kx[a], ky[a], sag_x, sag_y, colour);
				strand(grid, sag_x, sag_y, kx[b], ky[b], colour);
			}
		}
	}

	void wedge(var& grid, float ax, float ay, float bx, float by, float cx, float cy, int colour) {
		int width = w;
		int height = h;
		float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
		if (area < 0.0) {
			float swap_x = bx;
			float swap_y = by;
			bx = cx;
			by = cy;
			cx = swap_x;
			cy = swap_y;
		}
		float low_x = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
		float high_x = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
		float low_y = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
		float high_y = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
		int x0 = low_x < 0.0 ? 0 : math::itrunc(low_x);
		int y0 = low_y < 0.0 ? 0 : math::itrunc(low_y);
		int x1 = high_x >= width ? width - 1 : math::ifloor(high_x);
		int y1 = high_y >= height ? height - 1 : math::ifloor(high_y);
		for (int y = y0; y <= y1; ++y) {
			float py = y + 0.5;
			for (int x = x0; x <= x1; ++x) {
				float px = x + 0.5;
				if ((bx - ax) * (py - ay) - (by - ay) * (px - ax) >= 0.0 && (cx - bx) * (py - by) - (cy - by) * (px - bx) >= 0.0 && (ax - cx) * (py - cy) - (ay - cy) * (px - cx) >= 0.0) {
					grid[y * width + x] = colour;
				}
			}
		}
	}

	void oval(var& grid, float cx, float cy, float rx, float ry, int colour) {
		int width = w;
		int height = h;
		int y0 = math::ifloor(cy - ry + 0.5);
		int y1 = math::ifloor(cy + ry - 0.5);
		y0 = y0 < 0 ? 0 : y0;
		y1 = y1 >= height ? height - 1 : y1;
		for (int y = y0; y <= y1; ++y) {
			float v = (y + 0.5 - cy) / ry;
			float span = 1.0 - v * v;
			if (span <= 0.0) { continue; }
			float half = rx * math::sqrt(span);
			int x0 = math::ifloor(cx - half + 0.5);
			int x1 = math::ifloor(cx + half - 0.5);
			x0 = x0 < 0 ? 0 : x0;
			x1 = x1 >= width ? width - 1 : x1;
			int i = y * width + x0;
			for (int x = x0; x <= x1; ++x) {
				grid[i] = colour;
				++i;
			}
		}
	}

	// a tapering span of bone, filled across its minor axis and shaded from its upper left edge to its lower right
	void limb(var& grid, float ax, float ay, float bx, float by, float wide_a, float wide_b, int lit, int mid, int shade) {
		int width = w;
		int height = h;
		float dx = bx - ax;
		float dy = by - ay;
		float adx = dx < 0.0 ? -dx : dx;
		float ady = dy < 0.0 ? -dy : dy;
		bool steep = ady > adx;
		float major = steep ? ady : adx;
		if (major < 0.5) { return; }
		float slant = math::sqrt(dx * dx + dy * dy) / major;
		int steps = math::itrunc(major) + 1;
		int along_limit = steep ? height : width;
		int across_limit = steep ? width : height;
		for (int n = 0; n <= steps; ++n) {
			float u = n * 1.0 / steps;
			float half = (wide_a + (wide_b - wide_a) * u) * slant;
			int along = math::ifloor(steep ? ay + dy * u : ax + dx * u);
			if (along < 0 || along >= along_limit) { continue; }
			float centre = steep ? ax + dx * u : ay + dy * u;
			int from = math::ifloor(centre - half + 0.5);
			int to = math::ifloor(centre + half - 0.5);
			if (to < from) {
				from = math::ifloor(centre);
				to = from;
			}
			int run = to - from + 1;
			int lit_end = from + (run + 2) / 3;
			int shade_from = to - run / 3;
			from = from < 0 ? 0 : from;
			to = to >= across_limit ? across_limit - 1 : to;
			for (int p = from; p <= to; ++p) {
				int colour = p < lit_end ? lit : (p > shade_from ? shade : mid);
				if (steep) {
					grid[along * width + p] = colour;
				} else {
					grid[p * width + along] = colour;
				}
			}
		}
	}

	// gold wings beating on a dark body; once caught, bands of silk close over it one by one
	void paint_moth(var& grid) {
		if (moth_size <= 0.0) { return; }
		float scale = zoom;
		float mx = w * 0.5 + (moth_x - cam_x) * scale;
		float my = h * 0.5 - (moth_y - cam_y) * scale;
		float unit = 0.17 * moth_size * scale;
		float open = 1.0 - cocoon * 1.4;
		if (open > 0.0) {
			float lean = tt < t_impact ? 0.35 * math::sin(tt * 5.0) : 0.5;
			float c = math::cos(lean);
			float s = math::sin(lean);
			float beat = math::cos(moth_flap);
			float spread = (0.4 + 0.6 * (beat < 0.0 ? -beat : beat)) * unit * open;
			for (int side = -1; side <= 1; side += 2) {
				float far = side * spread;
				float tip_at_x = mx + c * far - s * 0.8 * unit;
				float tip_at_y = my - s * far - c * 0.8 * unit;
				wedge(grid, mx + s * 0.1 * unit, my + c * 0.1 * unit, mx + c * far * 0.7 + s * 0.15 * unit, my - s * far * 0.7 + c * 0.15 * unit, mx + c * far * 0.45 + s * 0.8 * unit, my - s * far * 0.45 + c * 0.8 * unit, gold + 4);
				wedge(grid, mx - s * 0.2 * unit, my - c * 0.2 * unit, tip_at_x, tip_at_y, mx + c * far * 0.85 + s * 0.12 * unit, my - s * far * 0.85 + c * 0.12 * unit, gold + 6);
				strand(grid, tip_at_x, tip_at_y, tip_at_x, tip_at_y, gold + 7);
			}
			limb(grid, mx - s * 0.5 * unit, my - c * 0.5 * unit, mx + s * 0.55 * unit, my + c * 0.55 * unit, unit * 0.09, unit * 0.05, gold + 2, gold + 1, gold);
		}
		if (cocoon <= 0.0) { return; }
		int width = w;
		int height = h;
		float rx = 0.09 * scale;
		float ry = 0.15 * scale;
		int hue = 32;
		int y0 = math::ifloor(my - ry + 0.5);
		int y1 = math::ifloor(my + ry - 0.5);
		y0 = y0 < 0 ? 0 : y0;
		y1 = y1 >= height ? height - 1 : y1;
		for (int y = y0; y <= y1; ++y) {
			float v = (y + 0.5 - my) / ry;
			float wound = (v + 1.0) * 4.5;
			int band = math::itrunc(wound);
			if ((band * 5 + 2) % 9 >= cocoon * 9.0) { continue; }
			bool seam = wound - band < 0.3;
			float half = rx * math::sqrt(1.0 - v * v);
			int x0 = math::ifloor(mx - half + 0.5);
			int x1 = math::ifloor(mx + half - 0.5);
			int shade_from = x0 + (x1 - x0) * 2 / 3;
			x0 = x0 < 0 ? 0 : x0;
			x1 = x1 >= width ? width - 1 : x1;
			for (int x = x0; x <= x1; ++x) {
				grid[y * width + x] = seam || x > shade_from ? silk + hue + 1 : silk + 3;
			}
		}
	}

	// two-bone inverse kinematics: the knee stays on one side of the hip-to-toe line (a right leg's to its left, so a
	// leg held out sideways arches upward), and a toe out of reach is pulled in along that line
	void bend(float hip_x, float hip_y, float toe_at_x, float toe_at_y, float upper, float lower, float side) {
		float dx = toe_at_x - hip_x;
		float dy = toe_at_y - hip_y;
		float far = math::sqrt(dx * dx + dy * dy);
		if (far < 0.0001) {
			dx = 0.0;
			dy = -1.0;
			far = 1.0;
		}
		float nx = dx / far;
		float ny = dy / far;
		float most = (upper + lower) * 0.998;
		float least = (lower > upper ? lower - upper : upper - lower) + 0.002;
		far = far > most ? most : (far < least ? least : far);
		float along = (upper * upper - lower * lower + far * far) / (2.0 * far);
		float rise = upper * upper - along * along;
		rise = rise > 0.0 ? math::sqrt(rise) : 0.0;
		knee_x = hip_x + nx * along - ny * side * rise;
		knee_y = hip_y + ny * along + nx * side * rise;
		tip_x = hip_x + nx * far;
		tip_y = hip_y + ny * far;
	}

	void paint_legs(var& grid) {
		float scale = zoom;
		float size = body_scale;
		float c = math::cos(body_rot);
		float s = math::sin(body_rot);
		float mid_x = w * 0.5 - cam_x * scale;
		float mid_y = h * 0.5 + cam_y * scale;
		float thick = 0.038 * size * scale;
		for (int i = 0; i < 8; ++i) {
			int j = i % 4;
			float side = i < 4 ? 1.0 : -1.0;
			float ox = side * 0.2 * size;
			float oy = (0.08 - 0.07 * j) * size;
			float hip_x = body_x + c * ox - s * oy;
			float hip_y = body_y + s * ox + c * oy;
			bend(hip_x, hip_y, toe_x[i], toe_y[i], OS_FEMUR * size, OS_TIBIA * size, side);
			float ax = mid_x + hip_x * scale;
			float ay = mid_y - hip_y * scale;
			float bx = mid_x + knee_x * scale;
			float by = mid_y - knee_y * scale;
			float cx = mid_x + tip_x * scale;
			float cy = mid_y - tip_y * scale;
			limb(grid, ax, ay, bx, by, thick + 1.0, thick * 0.72 + 1.0, ink, ink, ink);
			limb(grid, bx, by, cx, cy, thick * 0.72 + 1.0, thick * 0.22 + 1.0, ink, ink, ink);
			oval(grid, bx, by, thick * 0.72 + 1.5, thick * 0.72 + 1.5, ink);
			limb(grid, ax, ay, bx, by, thick, thick * 0.72, bone + 14, bone + 11, bone + 7);
			limb(grid, bx, by, cx, cy, thick * 0.72, thick * 0.22, bone + 14, bone + 11, bone + 7);
			oval(grid, bx, by, thick * 0.72 + 0.5, thick * 0.72 + 0.5, bone + 13);
		}
	}

	// a sprite of the skull or its jaw, turned and scaled about the body: each screen pixel walks the texels in
	// 16.16 fixed point and looks its colour up through this frame's table
	void stamp(var& grid, int piece, int mip, float cx, float cy, float c, float s, float unit, float drop) {
		int width = w;
		int height = h;
		int slot = piece * 3 + mip;
		int tw = slot_w[slot];
		int th = slot_h[slot];
		int at = slot_at[slot];
		float density = OS_DENSITY[mip];
		float left = OS_PIECE_LEFT[piece];
		float top = OS_PIECE_TOP[piece] - drop;
		float wide = OS_PIECE_WIDE[piece];
		float tall = OS_PIECE_TALL[piece];
		float middle_x = left + wide * 0.5;
		float middle_y = top - tall * 0.5;
		float half = math::sqrt(wide * wide + tall * tall) * 0.5 * unit + 2.0;
		float px = cx + (c * middle_x - s * middle_y) * unit;
		float py = cy - (s * middle_x + c * middle_y) * unit;
		int x0 = math::ifloor(px - half);
		int x1 = math::ifloor(px + half);
		int y0 = math::ifloor(py - half);
		int y1 = math::ifloor(py + half);
		x0 = x0 < 0 ? 0 : x0;
		y0 = y0 < 0 ? 0 : y0;
		x1 = x1 >= width ? width - 1 : x1;
		y1 = y1 >= height ? height - 1 : y1;
		if (x0 > x1 || y0 > y1) { return; }
		float k = density / unit;
		float dx = x0 + 0.5 - cx;
		float up = cy - (y0 + 0.5);
		int u_row = math::ifloor(((c * dx + s * up) / unit - left) * density * 65536.0);
		int v_row = math::ifloor((top - (c * up - s * dx) / unit) * density * 65536.0);
		int u_across = math::ifloor(c * k * 65536.0);
		int v_across = math::ifloor(s * k * 65536.0);
		int u_down = math::ifloor(-s * k * 65536.0);
		int v_down = math::ifloor(c * k * 65536.0);
		var& tex = texels;
		var& colours = texel_colour;
		for (int y = y0; y <= y1; ++y) {
			int u = u_row;
			int v = v_row;
			int i = y * width + x0;
			for (int x = x0; x <= x1; ++x) {
				int ui = u >> 16;
				int vi = v >> 16;
				if (ui >= 0 && ui < tw && vi >= 0 && vi < th) {
					int code = tex[at + vi * tw + ui];
					if (code >= 0) { grid[i] = colours[code]; }
				}
				u += u_across;
				v += v_across;
				++i;
			}
			u_row += u_down;
			v_row += v_down;
		}
	}

	void paint_head(var& grid) {
		float scale = zoom;
		float unit = OS_HEAD * body_scale * scale;
		float cx = w * 0.5 + (body_x - cam_x) * scale;
		float cy = h * 0.5 - (body_y - cam_y) * scale;
		float c = math::cos(body_rot);
		float s = math::sin(body_rot);
		int warmth = math::itrunc(eye_glow * 1.6);
		warmth = warmth > 3 ? 3 : warmth;
		var& colours = texel_colour;
		for (int light = 0; light < 4; ++light) {
			int tint = bone + (light * warmth + 1) / 3 * 16;
			for (int level = 0; level < 16; ++level) {
				colours[light * 16 + level] = tint + level;
				colours[64 + light * 16 + level] = ink;
				colours[128 + light * 16 + level] = light == 3 && warmth > 0 ? ember + warmth : ink;
			}
		}
		int mip = 0;
		while (mip < ready_mip && OS_DENSITY[mip] < unit * 0.8) { ++mip; }
		float drop = gape * 0.45;
		if (drop > 0.02) {
			float mouth = -0.84 - drop * 0.5;
			float mouth_x = cx - s * mouth * unit;
			float mouth_y = cy - c * mouth * unit;
			oval(grid, mouth_x, mouth_y, 0.44 * unit, (drop * 0.5 + 0.12) * unit, ink);
			if (drop > 0.12) { oval(grid, mouth_x, mouth_y, 0.3 * unit, drop * 0.42 * unit, ember + 2); }
		}
		stamp(grid, 1, mip, cx, cy, c, s, unit, drop);
		stamp(grid, 0, mip, cx, cy, c, s, unit, 0.0);
		if (eye_glow < 0.03) { return; }
		float bright = eye_glow > 2.0 ? 2.0 : eye_glow;
		float radius = (0.07 + 0.07 * bright) * unit;
		for (int side = -1; side <= 1; side += 2) {
			float lx = side * 0.38 + gaze_x * 0.07;
			float ly = 0.02 + gaze_y * 0.06;
			float ex = cx + (c * lx - s * ly) * unit;
			float ey = cy - (s * lx + c * ly) * unit;
			if (eye_glow < 0.6) {
				oval(grid, ex, ey, radius, radius, ember + 2 + math::itrunc(eye_glow * 6.0));
				continue;
			}
			oval(grid, ex, ey, radius, radius, ember + 6);
			oval(grid, ex + gaze_x * radius * 0.1, ey - gaze_y * radius * 0.1, radius * 0.75, radius * 0.75, ember + 8);
			oval(grid, ex + gaze_x * radius * 0.2, ey - gaze_y * radius * 0.2, radius * 0.45, radius * 0.45, ember + 10);
			strand(grid, ex + gaze_x * radius * 0.3, ey - gaze_y * radius * 0.3, ex + gaze_x * radius * 0.3, ey - gaze_y * radius * 0.3, ember + 11);
		}
	}

	void paint_sparks(var& grid) {
		int width = w;
		int height = h;
		float scale = zoom;
		float mid_x = width * 0.5 - cam_x * scale;
		float mid_y = height * 0.5 + cam_y * scale;
		for (int p = 0; p < 40; ++p) {
			if (spark_life[p] <= 0.0) { continue; }
			float x = mid_x + spark_x[p] * scale;
			float y = mid_y - spark_y[p] * scale;
			if (x >= 0.0 && y >= 0.0 && x < width && y < height) {
				grid[math::itrunc(y) * width + math::itrunc(x)] = spark_colour[p] - (spark_life[p] < 0.15 ? 2 : 0);
			}
		}
	}
}
