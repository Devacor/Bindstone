#include "profile.as"

bool is_prime(int n) {
	for (int i = 2; i < n; ++i) {
		if (n % i == 0) {
			return false;
		}
	}
	return true;
}

int primes(int n) {
	int count = 0;
	for (int i = 2; i <= n; ++i) {
		if (is_prime(i)) {
			++count;
		}
	}
	return count;
}

int result = 0;

void main() {
	profile_it("primes loop", function() { result = primes(14000); });
	print("checksum " + result);
}
