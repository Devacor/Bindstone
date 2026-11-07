#include "profile.as"

// AngelScript's int is 32-bit and wraps, as in das
int fib_loop(int n) {
	int last = 0;
	int cur = 1;
	n = n - 1;
	while (n > 0) {
		n = n - 1;
		int tmp = cur;
		cur = last + cur;
		last = tmp;
	}
	return cur;
}

int result = 0;

void main() {
	profile_it("fibonacci loop", function() { result = fib_loop(6511134); });
	print("checksum " + formatUInt(uint(result)));
}
