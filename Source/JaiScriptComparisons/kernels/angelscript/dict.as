#include "profile.as"

int dict(dictionary& tab, array<string>& src) {
	tab.deleteAll();
	int64 maxCount = 0;
	for (uint i = 0; i < src.length(); ++i) {
		int64 count;
		if (tab.get(src[i], count)) {
			count = count + 1;
		} else {
			count = 1;
		}
		tab.set(src[i], count);
		if (count > maxCount) {
			maxCount = count;
		}
	}
	return int(maxCount);
}

dictionary tab;
array<string> src;
int result = 0;

void main() {
	int n = 500000;
	src.reserve(n);
	for (int i = 0; i < n; ++i) {
		src.insertLast(formatInt((271828183 ^ (i * 119)) % n));
	}
	profile_it("dictionary", function() { result = dict(tab, src); });
	print("checksum " + result + " " + tab.getSize());
}
