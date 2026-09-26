include "profile.jai";

function A(var i, var j) {
	var ij = i + j - 1;
	return 1.0 / (ij * (ij - 1) * 0.5 + i);
}

function Av(var& x, var& y, var n) {
	for (var i = 1; i <= n; ++i) {
		var a = 0.0;
		for (var j = 1; j <= n; ++j) { a = a + x[j - 1] * A(i, j); }
		y[i - 1] = a;
	}
}

function Atv(var& x, var& y, var n) {
	for (var i = 1; i <= n; ++i) {
		var a = 0.0;
		for (var j = 1; j <= n; ++j) { a = a + x[j - 1] * A(j, i); }
		y[i - 1] = a;
	}
}

function AtAv(var& x, var& y, var& t, var n) {
	Av(x, t, n);
	Atv(t, y, n);
}

function spectral_norm() {
	var n = 500;
	var u = [];
	var v = [];
	var t = [];
	for (var i = 0; i < n; ++i) {
		u.push(1.0);
		v.push(0.0);
		t.push(0.0);
	}
	for (var i = 1; i <= 2; ++i) {
		AtAv(u, v, t, n);
		AtAv(v, u, t, n);
	}
	var vBv = 0.0;
	var vv = 0.0;
	for (var i = 0; i < n; ++i) {
		var ui = u[i];
		var vi = v[i];
		vBv = vBv + ui * vi;
		vv = vv + vi * vi;
	}
	return sqrt(vBv / vv);
}

var result = 0.0;
profile_it("spectral norm", []() { result = spectral_norm(); });
print("checksum {:.9f}", result);
