// PLASMA: four summed sine fields over a 256-entry cyclic palette; the radial
// term reads a double-size distance table through a sliding window.

class Plasma extends Scene {
	st = null;
	dist = null;
	dw = 0;
	tt = 0.0;

	constructor() {
		st = [];
		dist = [];
	}

	function build() {
		name = "PLASMA";
		handle = "warmup loop . by the symbolizer";
		for (local i = 0; i < 1024; ++i) {
			st.push((::sin(i * 0.00613592315) * 1024.0).tointeger());
		}
		dw = w * 2;
		for (local y = 0; y < h * 2; ++y) {
			for (local x = 0; x < dw; ++x) {
				local dx = x - w * 1.0;
				local dy = y - h * 1.0;
				dist.push((::sqrt(dx * dx + dy * dy) * 26.0).tointeger() & 1023);
			}
		}
		pal.gradient(256, [[12.0, 6.0, 40.0], [68.0, 20.0, 150.0], [214.0, 40.0, 150.0], [255.0, 120.0, 60.0],
			[255.0, 226.0, 110.0], [120.0, 240.0, 200.0], [30.0, 140.0, 230.0], [20.0, 30.0, 110.0], [12.0, 6.0, 40.0]]);
	}

	function update(t, dt) {
		tt = t;
	}

	function render(grid) {
		local ww = w;
		local hh = h;
		local dww = dw;
		local s = st;
		local dt_ = dist;
		local t1 = (tt * 150.0).tointeger();
		local t2 = (tt * 97.0).tointeger();
		local t3 = (tt * -210.0).tointeger() + 4096;
		local t4 = (tt * 61.0).tointeger();
		local phase = (tt * 38.0).tointeger();
		local ox = (w * 0.5 + w * 0.42 * ::sin(tt * 0.61)).tointeger();
		local oy = (h * 0.5 + h * 0.40 * ::sin(tt * 0.83 + 1.3)).tointeger();
		local zoom = 17 + (6.0 * ::sin(tt * 0.35)).tointeger();
		local idx = 0;
		for (local y = 0; y < hh; ++y) {
			local ys = s[(y * 21 + t2) & 1023];
			local drow = (y + oy) * dww + ox;
			local yx = y * 13 + t4;
			for (local x = 0; x < ww; ++x) {
				local v = s[(x * zoom + t1) & 1023] + ys + s[(dt_[drow + x] + t3) & 1023] + s[(x * 11 + yx) & 1023];
				grid[idx] = ((v >> 4) + phase) & 255;
				idx = idx + 1;
			}
		}
	}
}
