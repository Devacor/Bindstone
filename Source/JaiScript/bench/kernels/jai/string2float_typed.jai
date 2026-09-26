include "profile.jai";

int TOTAL_NUMBERS = 10000;
int TOTAL_TIMES = 4;

float mk_float(int i) { return i + (cast<float>(i) / TOTAL_NUMBERS); }

float update(array<string>& nums) {
	float summ = 0.0;
	for (int t = 1; t <= TOTAL_TIMES; ++t) {
		for (auto& s : nums) { summ = summ + s.to_float(); }
	}
	return summ;
}

array<string> nums = [];
for (int i = 1; i <= TOTAL_NUMBERS; ++i) { nums.push(to_string(mk_float(i))); }

float result = 0.0;
profile_it("string2float", []() { result = update(nums); });
print("checksum {:.6f}", result);
