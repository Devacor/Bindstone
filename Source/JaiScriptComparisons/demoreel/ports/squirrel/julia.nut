// JULIA (Squirrel port of scenes/julia.jai): morphing Julia set, continuous
// escape-time coloring plus an orbit trap. parallel_transform rows run serially.

::JU_CA <- 0.0;
::JU_CB <- 0.0;
::JU_SC <- 0.01;
::JU_X0 <- 0.0;
::JU_Y0 <- 0.0;
::JU_W <- 0;
::JU_MAXIT <- 48;
::JU_PHASE <- 0;
::JU_INNER <- 0;
::JU_ROWS <- [];

function julia_row(y) {
	local out = [];
	local ww = ::JU_W;
	local mi = ::JU_MAXIT;
	local ca = ::JU_CA;
	local cb = ::JU_CB;
	local sc = ::JU_SC;
	local x0 = ::JU_X0;
	local inner = ::JU_INNER;
	local phase = ::JU_PHASE;
	local sqrt = ::sqrt, log2 = ::log2;
	local zy0 = ::JU_Y0 + y * sc;
	for (local x = 0; x < ww; ++x) {
		local zx = x0 + x * sc;
		local zy = zy0;
		local x2 = zx * zx;
		local y2 = zy * zy;
		local trap = 9.0;
		local k = 0;
		while (k < mi && x2 + y2 < 64.0) {
			zy = 2.0 * zx * zy + cb;
			zx = x2 - y2 + ca;
			x2 = zx * zx;
			y2 = zy * zy;
			local m = x2 + y2;
			trap = m < trap ? m : trap;
			k = k + 1;
		}
		if (k >= mi) {
			local tl = (sqrt(trap) * 40.0).tointeger();
			out.push(inner + 31 - (tl > 31 ? 31 : tl));
		} else {
			local nu = k + 1.0 - log2(log2(x2 + y2) * 0.5);
			local ci = (sqrt(nu) * 52.0).tointeger();
			out.push((ci + phase) & 255);
		}
	}
	return out;
}

class Julia extends Scene {
	inner = 0;
	tt = 0.0;

	function build() {
		name = "JULIA";
		handle = "orbit morphing . by the type ladder";
		pal.gradient(256, [[8.0, 4.0, 30.0], [40.0, 20.0, 110.0], [170.0, 40.0, 170.0], [255.0, 110.0, 80.0],
			[255.0, 230.0, 130.0], [255.0, 255.0, 240.0], [100.0, 210.0, 255.0], [30.0, 70.0, 180.0], [8.0, 4.0, 30.0]]);
		inner = pal.gradient(32, [[2.0, 2.0, 10.0], [10.0, 26.0, 60.0], [20.0, 90.0, 120.0], [150.0, 240.0, 230.0]]);
	}

	function update(t, dt) {
		tt = t;
	}

	function render(grid) {
		local zoom = 1.05 + 0.3 * ::sin(tt * 0.4);
		::JU_CA = 0.7885 * ::cos(tt * 0.31 + 2.0);
		::JU_CB = 0.7885 * ::sin(tt * 0.23 + 2.0);
		::JU_SC = 3.3 / (w * zoom);
		::JU_X0 = 0.0 - w * 0.5 * ::JU_SC;
		::JU_Y0 = 0.0 - h * 0.5 * ::JU_SC;
		::JU_W = w;
		::JU_PHASE = (tt * 22.0).tointeger();
		::JU_INNER = inner;
		if (::JU_ROWS.len() != h) {
			::JU_ROWS = [];
			for (local r = 0; r < h; ++r) { ::JU_ROWS.push(r); }
		}
		local rows = [];
		foreach (r in ::JU_ROWS) { rows.push(::julia_row(r)); }
		local idx = 0;
		for (local y = 0; y < h; ++y) {
			local row = rows[y];
			for (local x = 0; x < w; ++x) {
				grid[idx] = row[x];
				idx = idx + 1;
			}
		}
	}
}
