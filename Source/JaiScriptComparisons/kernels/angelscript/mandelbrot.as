#include "profile.as"

// the Lua reference's repeat-until: step first, stop once |z| > 2 or 256 steps
int level(double x, double y) {
	double zr = x;
	double zi = y;
	int l = 0;
	while (true) {
		double nr = zr * zr - zi * zi + x;
		zi = zr * zi + zi * zr + y;
		zr = nr;
		l = l + 1;
		if (sqrt(zr * zr + zi * zi) > 2.0 || l > 255) {
			break;
		}
	}
	return l - 1;
}

int mandelbrot() {
	double xmin = -2.0;
	double xmax = 2.0;
	double ymin = -2.0;
	double ymax = 2.0;
	int n = 64;
	double dx = (xmax - xmin) / n;
	double dy = (ymax - ymin) / n;
	int s = 0;
	for (int i = 1; i <= n; ++i) {
		double x = xmin + (i - 1) * dx;
		for (int j = 1; j <= n; ++j) {
			double y = ymin + (j - 1) * dy;
			s += level(x, y);
		}
	}
	return s;
}

int result = 0;

void main() {
	profile_it("mandelbrot", function() { result = mandelbrot(); });
	print("checksum " + result);
}
