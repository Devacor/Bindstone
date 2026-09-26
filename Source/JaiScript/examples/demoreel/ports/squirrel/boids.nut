// BOIDS (Squirrel port of scenes/boids.jai): a starling murmuration at sunset and one hawk.

class Boid {
	x = 0.0;
	y = 0.0;
	vx = 0.0;
	vy = 0.0;
	px = 0.0;
	py = 0.0;
}

class Boids extends Scene {
	flock = null;
	trail = null;
	sky = null;
	nb = 80;
	tones = 32;
	hx = 10.0;
	hy = 10.0;
	hvx = 8.0;
	hvy = 0.0;
	tt = 0.0;

	constructor() {
		flock = [];
		trail = [];
		sky = [];
	}

	function build() {
		name = "BOIDS";
		handle = "murmuration . by the vm crew";
		trail = ::gfx.make_grid(w * h);
		sky = ::gfx.make_grid(w * h);
		local stops = [[10.0, 12.0, 40.0], [48.0, 30.0, 90.0], [150.0, 60.0, 120.0], [240.0, 110.0, 90.0], [255.0, 190.0, 110.0], [255.0, 244.0, 200.0]];
		for (local t = 0; t < tones; ++t) {
			local f = t * 5.0 / (tones - 1.0);
			local k = f.tointeger();
			k = k > 4 ? 4 : k;
			local u = f - k;
			local a = stops[k];
			local b = stops[k + 1];
			local ar = a[0], ag = a[1], ab = a[2];
			local cr = ar + (b[0] - ar) * u;
			local cg = ag + (b[1] - ag) * u;
			local cb = ab + (b[2] - ab) * u;
			for (local d = 0; d < 8; ++d) {
				local k2 = 1.0 - d / 7.0 * 0.93;
				pal.add(cr * k2 + 6.0 * (1.0 - k2), cg * k2 + 4.0 * (1.0 - k2), cb * k2 + 10.0 * (1.0 - k2));
			}
		}
		local sunx = w * 0.68;
		local suny = h * 0.80;
		local sr = h * 0.13;
		for (local y = 0; y < h; ++y) {
			for (local x = 0; x < w; ++x) {
				local g = (y * 1.0 / h) * 9.0;
				local dx = x - sunx;
				local dy = (y - suny) * 1.3;
				local d = ::sqrt(dx * dx + dy * dy);
				local glow = d < sr ? 15.0 : 13.0 - (d - sr) * 0.22;
				local tone = glow > g ? glow : g;
				local it = (tone * 2.0).tointeger();
				sky[y * w + x] = (it > 31 ? 31 : it) * 8;
			}
		}
		local hx0 = 0;
		while (hx0 < w) {
			local bw = 4 + rng.next(9);
			local bh = h / 14 + rng.next(h / 7);
			for (local x2 = hx0; x2 < hx0 + bw && x2 < w; ++x2) {
				for (local y2 = h - bh; y2 < h; ++y2) {
					local lit = (x2 - hx0) % 3 == 1 && (y2 - (h - bh)) % 3 == 1 && rng.next(100) < 30;
					sky[y2 * w + x2] = lit ? 31 * 8 + 1 : 7;
				}
			}
			hx0 = hx0 + bw + rng.next(3);
		}
		for (local i = 0; i < nb; ++i) {
			local b2 = ::Boid();
			b2.x = w * 0.3 + rng.next(w * 4) * 0.1;
			b2.y = h * 0.2 + rng.next(h * 4) * 0.1;
			local a2 = rng.next(628) * 0.01;
			b2.vx = ::cos(a2) * 12.0;
			b2.vy = ::sin(a2) * 12.0;
			b2.px = b2.x;
			b2.py = b2.y;
			flock.push(b2);
		}
	}

	function update(t, dt) {
		tt = t;
		if (dt <= 0.0) { return; }
		local wx = w * (0.5 + 0.36 * ::sin(t * 0.37));
		local wy = h * (0.38 + 0.2 * ::sin(t * 0.53 + 1.7));
		hawk_update(dt);
		for (local i = 0; i < nb; ++i) {
			steer_one(i, wx, wy, dt);
		}
	}

	function hawk_update(dt) {
		local cx = 0.0;
		local cy = 0.0;
		for (local i = 0; i < nb; ++i) {
			local b = flock[i];
			cx += b.x;
			cy += b.y;
		}
		cx /= nb;
		cy /= nb;
		hvx += (cx - hx) * 0.6 * dt;
		hvy += (cy - hy) * 0.6 * dt;
		local psp = ::sqrt(hvx * hvx + hvy * hvy);
		if (psp > 15.0) { hvx *= 15.0 / psp; hvy *= 15.0 / psp; }
		hx += hvx * dt;
		hy += hvy * dt;
		if (hx < 3.0) { hx = 3.0; hvx = 0.0 - hvx; }
		if (hx > w - 4.0) { hx = w - 4.0; hvx = 0.0 - hvx; }
		if (hy < 3.0) { hy = 3.0; hvy = 0.0 - hvy; }
		if (hy > h * 0.72) { hy = h * 0.72; hvy = 0.0 - hvy; }
	}

	// classic three rules + wander + hawk fear, one boid per call
	function steer_one(i, wx, wy, dt) {
		local b = flock[i];
		local fl = flock;
		local bx = b.x;
		local by = b.y;
		local ax = 0.0;
		local ay = 0.0;
		local mx = 0.0;
		local my = 0.0;
		local vx_sum = 0.0;
		local vy_sum = 0.0;
		local near = 0;
		for (local j = 0; j < nb; ++j) {
			if (j == i) { continue; }
			local o = fl[j];
			local dx = o.x - bx;
			local dy = o.y - by;
			local d2 = dx * dx + dy * dy;
			if (d2 > 220.0) { continue; }
			near = near + 1;
			mx += o.x;
			my += o.y;
			vx_sum += o.vx;
			vy_sum += o.vy;
			if (d2 < 10.0 && d2 > 0.0001) {
				ax -= dx / d2 * 26.0;
				ay -= dy / d2 * 26.0;
			}
		}
		if (near > 0) {
			ax += (mx / near - bx) * 1.1;
			ay += (my / near - by) * 1.1;
			ax += (vx_sum / near - b.vx) * 1.7;
			ay += (vy_sum / near - b.vy) * 1.7;
		}
		ax += (wx - bx) * 0.45;
		ay += (wy - by) * 0.45;
		if (bx < 10.0) { ax += (10.0 - bx) * 6.0; }
		if (bx > w - 10.0) { ax -= (bx - w + 10.0) * 6.0; }
		if (by < 8.0) { ay += (8.0 - by) * 6.0; }
		if (by > h * 0.66) { ay -= (by - h * 0.66) * 6.0; }
		local fx = bx - hx;
		local fy = by - hy;
		local fd2 = fx * fx + fy * fy;
		if (fd2 < 260.0 && fd2 > 0.001) {
			ax += fx / fd2 * 240.0;
			ay += fy / fd2 * 240.0;
		}
		b.vx += ax * dt;
		b.vy += ay * dt;
		local sp = ::sqrt(b.vx * b.vx + b.vy * b.vy);
		if (sp > 24.0) { b.vx *= 24.0 / sp; b.vy *= 24.0 / sp; }
		if (sp < 7.0 && sp > 0.001) { b.vx *= 7.0 / sp; b.vy *= 7.0 / sp; }
		b.px = bx;
		b.py = by;
		b.x += b.vx * dt;
		b.y += b.vy * dt;
		if (b.x < 0.0) { b.x = 0.0; b.vx = 0.0 - b.vx; }
		if (b.x > w - 1.0) { b.x = w - 1.0; b.vx = 0.0 - b.vx; }
		if (b.y < 0.0) { b.y = 0.0; b.vy = 0.0 - b.vy; }
		if (b.y > h * 0.82) { b.y = h * 0.82; b.vy = 0.0 - b.vy * 0.5; }
	}

	function mark(x, y, lv) {
		if (x < 0 || x >= w || y < 0 || y >= h) { return; }
		local i = y * w + x;
		if (trail[i] < lv) { trail[i] = lv; }
	}

	function render(grid) {
		local n = w * h;
		local tr = trail;
		local sk = sky;
		local fade = ((tt * 30.0).tointeger() & 1) == 0;
		for (local i = 0; i < n; ++i) {
			local tv = tr[i];
			if (fade && tv > 0) { tv = tv - 1; tr[i] = tv; }
			local s = sk[i];
			local dk = s % 8;
			grid[i] = s + (tv > dk ? tv - dk : 0);
		}
		for (local k = 0; k < nb; ++k) {
			local b = flock[k];
			local ex = b.x;
			local ey = b.y;
			local sx = b.px;
			local sy = b.py;
			mark(sx.tointeger(), sy.tointeger(), 3);
			mark(((sx + ex) * 0.5).tointeger(), ((sy + ey) * 0.5).tointeger(), 4);
			mark(ex.tointeger(), ey.tointeger(), 5);
		}
		for (local k2 = 0; k2 < nb; ++k2) {
			local b2 = flock[k2];
			local bx = b2.x.tointeger();
			local by = b2.y.tointeger();
			local sp = ::sqrt(b2.vx * b2.vx + b2.vy * b2.vy) + 0.001;
			local nx = (b2.x + b2.vx / sp * 1.2).tointeger();
			local ny = (b2.y + b2.vy / sp * 1.2).tointeger();
			if (bx >= 0 && bx < w && by >= 0 && by < h) { grid[by * w + bx] = sk[by * w + bx] - sk[by * w + bx] % 8 + 7; }
			if (nx >= 0 && nx < w && ny >= 0 && ny < h) { grid[ny * w + nx] = sk[ny * w + nx] - sk[ny * w + nx] % 8 + 6; }
		}
		local hxi = hx.tointeger();
		local hyi = hy.tointeger();
		local flap = ((tt * 7.0).tointeger() % 4);
		local tip = flap == 0 ? -2 : (flap == 2 ? 1 : 0);
		for (local s2 = -4; s2 <= 4; ++s2) {
			local sa = s2 < 0 ? 0 - s2 : s2;
			local yy = hyi + (sa <= 1 ? 0 : (sa <= 3 ? (tip < 0 ? -1 : 0) : tip));
			local xx = hxi + s2;
			if (xx >= 0 && xx < w && yy >= 0 && yy < h) {
				local gi = yy * w + xx;
				grid[gi] = sk[gi] - sk[gi] % 8 + 7;
			}
		}
		if (hyi + 1 < h && hxi >= 0 && hxi < w) {
			local gb = (hyi + 1) * w + hxi;
			grid[gb] = sk[gb] - sk[gb] % 8 + 7;
		}
	}
}
