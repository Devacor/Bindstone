include "profile.jai";

function is_prime(var n) {
	for (var i = 2; i < n; ++i) {
		if (n % i == 0) { return false; }
	}
	return true;
}

function primes(var n) {
	var count = 0;
	for (var i = 2; i <= n; ++i) {
		if (is_prime(i)) { ++count; }
	}
	return count;
}

var result = 0;
profile_it("primes loop", []() { result = primes(14000); });
print("checksum {}", result);
