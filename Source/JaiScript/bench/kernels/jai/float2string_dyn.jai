include "profile.jai";

var TOTAL_NUMBERS = 10000;
var TOTAL_TIMES = 4;

function mk_float(var i) { return i + (i * 1.0 / TOTAL_NUMBERS); }

function update(var& nums) {
	var summ = 0;
	for (var t = 1; t <= TOTAL_TIMES; ++t) {
		for (var f : nums) { summ = summ + to_string(f).size(); }
	}
	return summ;
}

var nums = [];
for (var i = 1; i <= TOTAL_NUMBERS; ++i) { nums.push(mk_float(i)); }

var result = 0;
profile_it("float2string", []() { result = update(nums); });
print("checksum {}", result);
