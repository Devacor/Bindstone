include "profile.jai";

int TOTAL_NUMBERS = 10000;
int TOTAL_TIMES = 4;

float mk_float(int i) { return i + (cast<float>(i) / TOTAL_NUMBERS); }

int update(array<float>& nums) {
	int summ = 0;
	for (int t = 1; t <= TOTAL_TIMES; ++t) {
		for (float f : nums) { summ = summ + to_string(f).size(); }
	}
	return summ;
}

array<float> nums = [];
for (int i = 1; i <= TOTAL_NUMBERS; ++i) { nums.push(mk_float(i)); }

int result = 0;
profile_it("float2string", []() { result = update(nums); });
print("checksum {}", result);
