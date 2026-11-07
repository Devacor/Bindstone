#include "profile.as"

class Rng {
	array<int> m(18);
	int j = 1;
	Rng(int64 seed) {
		for (int i = 17; i >= 1; --i) {
			seed = (seed * 9069) % 2147483648;
			m[i] = int(seed);
		}
	}
	int next() {
		int h = j - 5;
		if (h < 1) {
			h = h + 17;
		}
		int k = m[h] - m[j];
		if (k < 0) {
			k = k + 2147483647;
		}
		m[j] = k;
		if (j < 17) {
			j = j + 1;
		} else {
			j = 1;
		}
		return k;
	}
}

funcdef bool Less(int a, int b);

// the array add-on sorts in quadratic time (sort(callback) is a selection sort, sortDesc an insertion sort), so the
// quicksort is the script's, calling the comparator through a function handle as Lua's table.sort calls cmp
void quick_sort(array<int>& a, int lo, int hi, Less@ less) {
	while (lo < hi) {
		int pivot = a[lo + (hi - lo) / 2];
		int i = lo;
		int j = hi;
		while (i <= j) {
			while (less(a[i], pivot)) {
				++i;
			}
			while (less(pivot, a[j])) {
				--j;
			}
			if (i <= j) {
				int swap = a[i];
				a[i] = a[j];
				a[j] = swap;
				++i;
				--j;
			}
		}
		if (j - lo < hi - i) {
			quick_sort(a, lo, j, less);
			lo = i;
		} else {
			quick_sort(a, i, hi, less);
			hi = j;
		}
	}
}

const int N = 100000;
array<int> t;

array<int>@ sorted_copy() {
	array<int> result = t;
	quick_sort(result, 0, result.length() - 1, function(lhs, rhs) { return lhs > rhs; });
	return result;
}

void main() {
	Rng rng(12345);
	t.reserve(N);
	for (int i = 0; i < N; ++i) {
		t.insertLast(rng.next());
	}
	array<int>@ check = sorted_copy();
	profile_it("sort", function() { sorted_copy(); });
	print("checksum " + check[0] + " " + check[N / 2] + " " + check[N - 1]);
}
