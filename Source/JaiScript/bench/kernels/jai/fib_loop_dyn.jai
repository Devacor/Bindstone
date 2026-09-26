include "profile.jai";

function fib_loop(var n) {
	var last = 0;
	var cur = 1;
	n = n - 1;
	while (n > 0) {
		n = n - 1;
		var tmp = cur;
		cur = (last + cur) & 0xFFFFFFFF;
		last = tmp;
	}
	return cur;
}

var result = 0;
profile_it("fibonacci loop", []() { result = fib_loop(6511134); });
print("checksum {}", result);
