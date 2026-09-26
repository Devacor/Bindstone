include "profile.jai";

float exp_loop(int n) {
	float sum = 0.0;
	for (int i = 0; i <= n; ++i) {
		sum += exp(1.0 / (1.0 + i));
	}
	return sum;
}

float result = 0.0;
profile_it("exp loop", []() { result = exp_loop(1000000); });
print("checksum {:.6f}", result);
