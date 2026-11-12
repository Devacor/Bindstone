include "profile.jai";

var N = 8;
var solutions = 0;

function is_place_ok(var& a, var n, var c) {
	for (var i = 1; i < n; ++i) {
		var ai = a[i];
		if (ai == c || ai - i == c - n || ai + i == c + n) { return false; }
	}
	return true;
}

function add_queen(var& a, var n) {
	if (n > N) {
		++solutions;
		return;
	}
	for (var c = 1; c <= N; ++c) {
		if (is_place_ok(a, n, c)) {
			a[n] = c;
			add_queen(a, n + 1);
		}
	}
}

function run() {
	solutions = 0;
	var a = [0, 0, 0, 0, 0, 0, 0, 0, 0];
	add_queen(a, 1);
}

profile_it("queen", []() { run(); });
print("checksum {}", solutions);
