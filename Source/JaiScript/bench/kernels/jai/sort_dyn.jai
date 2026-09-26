include "profile.jai";

class Rng {
	var m = [];
	var j = 1;
	Rng(var seed) {
		for (var i = 0; i <= 17; ++i) { m.push(0); }
		for (var i = 17; i >= 1; --i) {
			seed = (seed * 9069) % 2147483648;
			m[i] = seed;
		}
	}
	function next() {
		var h = j - 5;
		if (h < 1) { h = h + 17; }
		var k = m[h] - m[j];
		if (k < 0) { k = k + 2147483647; }
		m[j] = k;
		if (j < 17) { j = j + 1; } else { j = 1; }
		return k;
	}
}

var n = 100000;
var rng = Rng(12345);
var t = [];
for (var i = 0; i < n; ++i) { t.push(rng.next()); }

function sorted_copy() {
	var c = t;
	c.sort([](var lhs, var rhs) { return lhs > rhs; });
	return c;
}

var check = sorted_copy();
profile_it("sort", []() { sorted_copy(); });
print("checksum {} {} {}", check[0], check[n / 2], check[n - 1]);
