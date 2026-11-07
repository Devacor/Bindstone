#include "profile.as"

double exp_loop(int n) {
	double sum = 0;
	for (int i = 0; i <= n; ++i) {
		sum += exp(1.0 / (1.0 + i));
	}
	return sum;
}

double result = 0;

void main() {
	profile_it("exp loop", function() { result = exp_loop(1000000); });
	print("checksum " + formatFloat(result, "", 0, 6));
}
