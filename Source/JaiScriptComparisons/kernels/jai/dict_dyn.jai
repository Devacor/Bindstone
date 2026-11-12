include "profile.jai";

function dict(var& tab, var& src) {
	tab.clear();
	var max_count = 0;
	for (var& s : src) {
		var c = tab.get(s, 0) + 1;
		tab[s] = c;
		if (c > max_count) { max_count = c; }
	}
	return max_count;
}

var n = 500000;
var src = [];
for (var i = 0; i < n; ++i) { src.push(to_string((271828183 ^ (i * 119)) % n)); }

var tab = {};
var result = 0;
profile_it("dictionary", []() { result = dict(tab, src); });
print("checksum {} {}", result, tab.size());
