include "profile.jai";

float A(int i, int j) {
	int ij = i + j - 1;
	return 1.0 / (ij * (ij - 1) * 0.5 + i);
}

void Av(array<float>& x, array<float>& y, int n) {
	for (int i = 1; i <= n; ++i) {
		float a = 0.0;
		for (int j = 1; j <= n; ++j) { a = a + x[j - 1] * A(i, j); }
		y[i - 1] = a;
	}
}

void Atv(array<float>& x, array<float>& y, int n) {
	for (int i = 1; i <= n; ++i) {
		float a = 0.0;
		for (int j = 1; j <= n; ++j) { a = a + x[j - 1] * A(j, i); }
		y[i - 1] = a;
	}
}

void AtAv(array<float>& x, array<float>& y, array<float>& t, int n) {
	Av(x, t, n);
	Atv(t, y, n);
}

float spectral_norm() {
	int n = 500;
	array<float> u = [];
	array<float> v = [];
	array<float> t = [];
	for (int i = 0; i < n; ++i) {
		u.push(1.0);
		v.push(0.0);
		t.push(0.0);
	}
	for (int i = 1; i <= 2; ++i) {
		AtAv(u, v, t, n);
		AtAv(v, u, t, n);
	}
	float vBv = 0.0;
	float vv = 0.0;
	for (int i = 0; i < n; ++i) {
		float ui = u[i];
		float vi = v[i];
		vBv = vBv + ui * vi;
		vv = vv + vi * vi;
	}
	return math::sqrt(vBv / vv);
}

float result = 0.0;
profile_it("spectral norm", []() { result = spectral_norm(); });
print("checksum {:.9f}", result);
