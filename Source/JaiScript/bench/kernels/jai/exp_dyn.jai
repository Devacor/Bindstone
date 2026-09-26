include "profile.jai";

function exp_loop(var n) {
	var sum = 0.0;
	for (var i = 0; i <= n; ++i) {
		sum += exp(1.0 / (1.0 + i));
	}
	return sum;
}

var result = 0.0;
profile_it("exp loop", []() { result = exp_loop(1000000); });
print("checksum {:.6f}", result);
