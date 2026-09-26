include "profile.jai";

int dict(map<string, int>& tab, array<string>& src) {
	tab.clear();
	int max_count = 0;
	for (auto& s : src) {
		int c = tab.get(s, 0) + 1;
		tab[s] = c;
		if (c > max_count) { max_count = c; }
	}
	return max_count;
}

int n = 500000;
array<string> src = [];
for (int i = 0; i < n; ++i) { src.push(to_string((271828183 ^ (i * 119)) % n)); }

map<string, int> tab = {};
int result = 0;
profile_it("dictionary", []() { result = dict(tab, src); });
print("checksum {} {}", result, tab.size());
