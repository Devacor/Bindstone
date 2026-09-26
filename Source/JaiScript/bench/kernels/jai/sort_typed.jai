include "profile.jai";

class Rng {
	array<int> m = [];
	int j = 1;
	Rng(int seed) {
		for (int i = 0; i <= 17; ++i) { m.push(0); }
		for (int i = 17; i >= 1; --i) {
			seed = (seed * 9069) % 2147483648;
			m[i] = seed;
		}
	}
	int next() {
		int h = j - 5;
		if (h < 1) { h = h + 17; }
		int k = m[h] - m[j];
		if (k < 0) { k = k + 2147483647; }
		m[j] = k;
		if (j < 17) { j = j + 1; } else { j = 1; }
		return k;
	}
}

int n = 100000;
Rng rng = Rng(12345);
array<int> t = [];
for (int i = 0; i < n; ++i) { t.push(rng.next()); }

array<int> sorted_copy() {
	array<int> c = t;
	c.sort([](int lhs, int rhs) -> bool { return lhs > rhs; });
	return c;
}

array<int> check = sorted_copy();
profile_it("sort", []() { sorted_copy(); });
print("checksum {} {} {}", check[0], check[n / 2], check[n - 1]);
