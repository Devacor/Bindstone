#include "profile.as"
#include "lua_number.as"

const int TOTAL_NUMBERS = 10000;
const int TOTAL_TIMES = 4;

double mk_float(int i) {
	return i + (double(i) / TOTAL_NUMBERS);
}

double update(array<string>& nums) {
	double summ = 0;
	for (int t = 1; t <= TOTAL_TIMES; ++t) {
		for (uint i = 0; i < nums.length(); ++i) {
			summ = summ + parseFloat(nums[i]);
		}
	}
	return summ;
}

array<string> nums;
double result = 0;

void main() {
	nums.reserve(TOTAL_NUMBERS);
	for (int i = 1; i <= TOTAL_NUMBERS; ++i) {
		nums.insertLast(lua_number(mk_float(i)));
	}
	profile_it("string2float", function() { result = update(nums); });
	print("checksum " + formatFloat(result, "", 0, 6));
}
