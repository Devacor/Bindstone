// FINALE (Squirrel port of scenes/finale.jai): the fire scene with the word
// JAISCRIPT stamped as a heat-source mask, burning rainbow, then afterglow.

class Finale extends Fire {
	cap_t = 13.0;

	constructor() {
		base.constructor();
	}

	function build() {
		name = "FINALE";
		handle = "jaiscript forever . rainbow-fire salute";
		build_fire();
		mask = ::gfx.make_grid(w * (h + 2));
		local text = "JAISCRIPT";
		local s = w >= 98 ? 2 : 1;
		local pitch = s == 2 ? 11 : 6;
		local x0 = (w - (text.len() * pitch - 1)) / 2;
		local y0 = h / 2 - 7 * s - 2;
		for (local li = 0; li < text.len(); ++li) {
			local bits = ::FONT5[text.slice(li, li + 1)];
			for (local ry = 0; ry < 7 * s; ++ry) {
				local b = bits[ry / s];
				for (local rx = 0; rx < 5 * s; ++rx) {
					if (((b >> (4 - rx / s)) & 1) == 1) {
						mask[(y0 + ry) * w + x0 + li * pitch + rx] = 1;
					}
				}
			}
		}
	}

	function update(t, dt) {
		tt = t;
		fr = fr + 1;
		rainbow_on = t > 4.5 ? 1 : 0;
		if (rainbow_on == 1) { hue_t += dt * 3.0; }
		local amp = 42;
		amp = t > 10.5 ? (42.0 - (t - 10.5) * 19.0).tointeger() : amp;
		amp = amp < 0 ? 0 : amp;
		seed_rows(amp, amp / 2 + 2);
		convect(t > 2.2);
		move_sparks(dt, amp > 12);
	}

	function render(grid) {
		local b = bank();
		local n = w * h;
		local ht = heat;
		local mk = mask;
		local lit = tt > 2.2;
		for (local i = 0; i < n; ++i) {
			local v = ht[i];
			grid[i] = (v > 72 ? 72 : v) + b;
		}
		if (lit) {
			local ww = w;
			for (local i2 = ww; i2 < n - ww; ++i2) {
				if (mk[i2] == 1) {
					grid[i2] = b + 64 + ((i2 * 7 + fr) & 7);
				} else if (mk[i2 - 1] == 1 || mk[i2 + 1] == 1 || mk[i2 - ww] == 1 || mk[i2 + ww] == 1) {
					local v2 = ht[i2] / 3;
					grid[i2] = b + v2;
				}
			}
		}
		draw_sparks(grid, b);
	}

	function caption() {
		if (tt > cap_t + 1.8) { return "vm + interpreter . byte-identical frames . 16 scenes"; }
		if (tt > cap_t) { return "every pixel of this reel is jaiscript"; }
		return "";
	}
}
