// dasProfile's sampling model (lua/profile.lua): one call calibrates, each batch under the budget regrows the count
// (1.2x the projection, at most 100x); the batch that meets the budget is reported.
funcdef void Kernel();

const double PROFILE_BUDGET = 0.5;

double profile_it(const string &in name, Kernel@ fn) {
	int64 n = 1;
	double total = 0;
	while (true) {
		double start = clock();
		for (int64 i = 0; i < n; ++i) {
			fn();
		}
		total = clock() - start;
		if (total >= PROFILE_BUDGET || n >= 1000000000) {
			break;
		}
		double per = total / n;
		if (per < 1e-9) {
			per = 1e-9;
		}
		int64 next = int64(floor(PROFILE_BUDGET / per * 1.2));
		if (next > 100 * n) {
			next = 100 * n;
		}
		if (next < n + 1) {
			next = n + 1;
		}
		n = next < 1000000000 ? next : 1000000000;
	}
	double seconds = total / n;
	print("\"" + name + "\", " + formatFloat(seconds, "", 0, 8) + ", " + n);
	return seconds;
}
