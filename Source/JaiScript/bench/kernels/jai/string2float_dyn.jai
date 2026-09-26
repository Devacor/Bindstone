include "profile.jai";

var TOTAL_NUMBERS = 10000;
var TOTAL_TIMES = 4;

function mk_float(var i) { return i + (i * 1.0 / TOTAL_NUMBERS); }

function update(var& nums) {
	var summ = 0.0;
	for (var t = 1; t <= TOTAL_TIMES; ++t) {
		for (var& s : nums) { summ = summ + s.to_float(); }
	}
	return summ;
}

var nums = [];
for (var i = 1; i <= TOTAL_NUMBERS; ++i) { nums.push(to_string(mk_float(i))); }

var result = 0.0;
profile_it("string2float", []() { result = update(nums); });
print("checksum {:.6f}", result);
