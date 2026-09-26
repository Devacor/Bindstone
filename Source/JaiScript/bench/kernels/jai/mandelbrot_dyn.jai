include "profile.jai";

function level(var x, var y) {
	var zr = x;
	var zi = y;
	var l = 0;
	while (true) {
		var nr = zr * zr - zi * zi + x;
		zi = zr * zi + zi * zr + y;
		zr = nr;
		l = l + 1;
		if (sqrt(zr * zr + zi * zi) > 2.0 || l > 255) { break; }
	}
	return l - 1;
}

function mandelbrot() {
	var xmin = -2.0;
	var xmax = 2.0;
	var ymin = -2.0;
	var ymax = 2.0;
	var n = 64;
	var dx = (xmax - xmin) / n;
	var dy = (ymax - ymin) / n;
	var s = 0;
	for (var i = 1; i <= n; ++i) {
		var x = xmin + (i - 1) * dx;
		for (var j = 1; j <= n; ++j) {
			var y = ymin + (j - 1) * dy;
			s += level(x, y);
		}
	}
	return s;
}

var result = 0;
profile_it("mandelbrot", []() { result = mandelbrot(); });
print("checksum {}", result);
