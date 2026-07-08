// Particle pool: 288 fixed slots of [kind, x, y, z, vx, vy, vz, life] records,
// stepped by the pure gloom_particle body, round-robin cursor overwrite.

::PART_MAX <- 288;

class ParticlePool {
	pool = null;
	cursor = 0;

	constructor() { reset(); }

	function reset() {
		pool = [];
		for (local i = 0; i < ::PART_MAX; ++i) {
			pool.push([0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0]);
		}
		cursor = 0;
	}

	function spawn(kind, x, y, z, vx, vy, vz, life) {
		pool[cursor] = [kind, x, y, z, vx, vy, vz, life];
		cursor = (cursor + 1) % ::PART_MAX;
	}

	// the whole pool steps through the ONE pure function, serially
	function update() {
		local pl = pool;
		local n = pl.len();
		for (local i = 0; i < n; ++i) { pl[i] = ::gloom_particle(pl[i]); }
	}
}

// ------------------------------------------------------------ burst kit -----
// All velocity spreads come from RNG (the seeded host rng): deterministic.
function rnd_spread(scale) {
	return (::RNG.nextf() - 0.5) * 2.0 * scale;
}

function fx_muzzle(x, y, dirx, diry) {
	local mx = x + dirx * 0.5;
	local my = y + diry * 0.5;
	::G.fx.spawn(5, mx, my, 0.52, dirx * 2.0, diry * 2.0, 0.2, 3);
	for (local i = 0; i < 3; ++i) {
		::G.fx.spawn(1, mx, my, 0.5, dirx * 3.0 + ::rnd_spread(1.6), diry * 3.0 + ::rnd_spread(1.6), 0.6 + ::RNG.nextf(), 6);
	}
	::G.fx.spawn(4, mx, my, 0.55, dirx * 0.6, diry * 0.6, 0.3, 16);
}

function fx_wall_hit(x, y) {
	for (local i = 0; i < 5; ++i) {
		::G.fx.spawn(1, x, y, 0.45 + ::RNG.nextf() * 0.2, ::rnd_spread(2.2), ::rnd_spread(2.2), 0.8 + ::RNG.nextf() * 1.4, 8);
	}
	::G.fx.spawn(4, x, y, 0.5, ::rnd_spread(0.3), ::rnd_spread(0.3), 0.4, 14);
}

function fx_blood(x, y, z, n) {
	for (local i = 0; i < n; ++i) {
		::G.fx.spawn(2, x, y, z + ::RNG.nextf() * 0.25, ::rnd_spread(1.8), ::rnd_spread(1.8), 0.5 + ::RNG.nextf() * 1.6, 12);
	}
}

// death burst: meat + smoke; big enemies gib harder
function fx_gibs(x, y, n) {
	for (local i = 0; i < n; ++i) {
		::G.fx.spawn(3, x, y, 0.35 + ::RNG.nextf() * 0.3, ::rnd_spread(2.6), ::rnd_spread(2.6), 1.2 + ::RNG.nextf() * 2.4, 26);
	}
	for (local i = 0; i < 3; ++i) {
		::G.fx.spawn(4, x, y, 0.4, ::rnd_spread(0.5), ::rnd_spread(0.5), 0.5, 20);
	}
}

function fx_explosion(x, y) {
	::G.fx.spawn(5, x, y, 0.5, 0.0, 0.0, 0.0, 5);
	for (local i = 0; i < 10; ++i) {
		local a = ::RNG.nextf() * 6.2831853;
		local sp = 1.5 + ::RNG.nextf() * 3.0;
		::G.fx.spawn(1, x, y, 0.3 + ::RNG.nextf() * 0.5, ::cos(a) * sp, ::sin(a) * sp, 1.0 + ::RNG.nextf() * 2.0, 11);
	}
	for (local i = 0; i < 5; ++i) {
		::G.fx.spawn(4, x, y, 0.4, ::rnd_spread(0.9), ::rnd_spread(0.9), 0.6 + ::RNG.nextf(), 24);
	}
	for (local i = 0; i < 6; ++i) {
		::G.fx.spawn(6, x, y, 0.2 + ::RNG.nextf() * 0.6, ::rnd_spread(1.4), ::rnd_spread(1.4), 0.5, 20);
	}
}

function fx_door_puff(x, y) {
	for (local i = 0; i < 4; ++i) {
		::G.fx.spawn(4, x, y, 0.3 + ::RNG.nextf() * 0.5, ::rnd_spread(0.7), ::rnd_spread(0.7), 0.3, 18);
	}
}

function fx_secret_glitter(x, y) {
	for (local i = 0; i < 8; ++i) {
		::G.fx.spawn(6, x, y, 0.2 + ::RNG.nextf() * 0.8, ::rnd_spread(1.2), ::rnd_spread(1.2), 0.6, 24);
	}
}

// ------------------------------------------------------------- colors -------
// per-kind life-phase ramps, interned at boot (render reads
// PART_COLS[kind * 4 + phase]; phase 0 = fresh)
::PART_COLS <- [];

function build_particle_colors() {
	local ramps = [
		[[0, 0, 0], [0, 0, 0], [0, 0, 0], [0, 0, 0]],                          // 0 dead (unused)
		[[255, 240, 160], [255, 200, 90], [230, 130, 50], [140, 70, 30]],      // 1 spark
		[[210, 40, 30], [180, 34, 26], [140, 26, 20], [95, 18, 14]],           // 2 blood
		[[200, 60, 44], [170, 46, 34], [130, 34, 26], [90, 24, 18]],           // 3 gib
		[[130, 126, 122], [104, 102, 100], [80, 79, 78], [56, 56, 57]],        // 4 smoke
		[[255, 252, 220], [255, 236, 160], [255, 190, 90], [200, 120, 50]],    // 5 flash
		[[150, 255, 170], [110, 235, 200], [90, 190, 250], [80, 120, 220]]     // 6 hexmote
	];
	::PART_COLS = [];
	foreach (ramp in ramps) {
		foreach (rgb in ramp) {
			::PART_COLS.push(::rgb_idx(rgb[0], rgb[1], rgb[2]));
		}
	}
}
