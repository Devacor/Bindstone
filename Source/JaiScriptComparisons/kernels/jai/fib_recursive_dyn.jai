include "profile.jai";

function fib(var n) {
	if (n < 2) { return n; }
	return fib(n - 2) + fib(n - 1);
}

var result = 0;
profile_it("fibonacci recursive", []() { result = fib(31); });
print("checksum {}", result);
