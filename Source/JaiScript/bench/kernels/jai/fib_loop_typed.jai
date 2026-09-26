include "profile.jai";

// int32 wraparound like the das reference: script ints are checked int64, so the mask spells it
int fib_loop(int n) {
	int last = 0;
	int cur = 1;
	n = n - 1;
	while (n > 0) {
		n = n - 1;
		int tmp = cur;
		cur = (last + cur) & 0xFFFFFFFF;
		last = tmp;
	}
	return cur;
}

int result = 0;
profile_it("fibonacci loop", []() { result = fib_loop(6511134); });
print("checksum {}", result);
