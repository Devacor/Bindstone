#include "profile.as"

const int N = 8;
int solutions = 0;

bool is_place_ok(array<int>& a, int n, int c) {
	for (int i = 1; i < n; ++i) {
		int ai = a[i];
		if (ai == c || ai - i == c - n || ai + i == c + n) {
			return false;
		}
	}
	return true;
}

void add_queen(array<int>& a, int n) {
	if (n > N) {
		++solutions;
		return;
	}
	for (int c = 1; c <= N; ++c) {
		if (is_place_ok(a, n, c)) {
			a[n] = c;
			add_queen(a, n + 1);
		}
	}
}

void run() {
	solutions = 0;
	array<int> a(N + 1);
	add_queen(a, 1);
	if (solutions != 92) {
		print("failed " + solutions);
	}
}

void main() {
	profile_it("queen", run);
	print("checksum " + solutions);
}
