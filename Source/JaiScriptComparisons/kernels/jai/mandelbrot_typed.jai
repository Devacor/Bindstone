include "profile.jai";

// the Lua reference's repeat-until: step first, stop once |z| > 2 or 256 steps
int level(float x, float y) {
	float zr = x;
	float zi = y;
	int l = 0;
	while (true) {
		float nr = zr * zr - zi * zi + x;
		zi = zr * zi + zi * zr + y;
		zr = nr;
		l = l + 1;
		if (math::sqrt(zr * zr + zi * zi) > 2.0 || l > 255) { break; }
	}
	return l - 1;
}

int mandelbrot() {
	float xmin = -2.0;
	float xmax = 2.0;
	float ymin = -2.0;
	float ymax = 2.0;
	int n = 64;
	float dx = (xmax - xmin) / n;
	float dy = (ymax - ymin) / n;
	int s = 0;
	for (int i = 1; i <= n; ++i) {
		float x = xmin + (i - 1) * dx;
		for (int j = 1; j <= n; ++j) {
			float y = ymin + (j - 1) * dy;
			s += level(x, y);
		}
	}
	return s;
}

int result = 0;
profile_it("mandelbrot", []() { result = mandelbrot(); });
print("checksum {}", result);
