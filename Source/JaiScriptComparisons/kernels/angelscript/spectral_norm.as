#include "profile.as"

double A(int i, int j) {
	int ij = i + j - 1;
	return 1.0 / (ij * (ij - 1) * 0.5 + i);
}

void Av(array<double>& x, array<double>& y, int n) {
	for (int i = 1; i <= n; ++i) {
		double a = 0;
		for (int j = 1; j <= n; ++j) {
			a = a + x[j - 1] * A(i, j);
		}
		y[i - 1] = a;
	}
}

void Atv(array<double>& x, array<double>& y, int n) {
	for (int i = 1; i <= n; ++i) {
		double a = 0;
		for (int j = 1; j <= n; ++j) {
			a = a + x[j - 1] * A(j, i);
		}
		y[i - 1] = a;
	}
}

void AtAv(array<double>& x, array<double>& y, array<double>& t, int n) {
	Av(x, t, n);
	Atv(t, y, n);
}

double spectral_norm() {
	int n = 500;
	array<double> u(n, 1.0);
	array<double> v(n);
	array<double> t(n);
	for (int i = 1; i <= 2; ++i) {
		AtAv(u, v, t, n);
		AtAv(v, u, t, n);
	}
	double vBv = 0;
	double vv = 0;
	for (int i = 0; i < n; ++i) {
		double ui = u[i];
		double vi = v[i];
		vBv = vBv + ui * vi;
		vv = vv + vi * vi;
	}
	return sqrt(vBv / vv);
}

double result = 0;

void main() {
	profile_it("spectral norm", function() { result = spectral_norm(); });
	print("checksum " + formatFloat(result, "", 0, 9));
}
