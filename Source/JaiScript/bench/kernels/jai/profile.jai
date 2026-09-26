// dasProfile's sampling model: one call calibrates, each batch under the budget regrows the
// count (1.2x the projection, at most 100x); the batch that meets the budget is reported.
float PROFILE_BUDGET_MS = 500.0;

function profile_it(string name, function fn) -> float {
	int n = 1;
	int total = 0;
	while (true) {
		int start = clock_ms();
		for (int i = 0; i < n; ++i) { fn(); }
		total = clock_ms() - start;
		if (total >= PROFILE_BUDGET_MS || n >= 1000000000) { break; }
		float per = max(cast<float>(total) / n, 0.000001);
		int next = cast<int>(PROFILE_BUDGET_MS / per * 1.2);
		next = min(next, 100 * n);
		next = max(next, n + 1);
		n = min(next, 1000000000);
	}
	float seconds = total / 1000.0 / n;
	print("\"{}\", {:.8f}, {}", name, seconds, n);
	return seconds;
}
