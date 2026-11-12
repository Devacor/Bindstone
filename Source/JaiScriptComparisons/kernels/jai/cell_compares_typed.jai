include "profile.jai";

// a breadth-first search's inner compares (Gator Quest's bfs_path): an element read against a typed global, a local against a
// typed global, and a bool's truth test, in a counted loop
array<int> SEEN = [];
int STAMP = 0;
int LIMIT = 3000;
int CELLS = 4096;

int sweep(int reps) {
	int hits = 0;
	for (int r = 0; r < reps; ++r) {
		STAMP = STAMP + 1;
		for (int i = 0; i < CELLS; ++i) {
			int ni = (i * 7 + r) % CELLS;
			if (SEEN[ni] == STAMP) { hits = hits + 1; } else { SEEN[ni] = STAMP; }
			if (ni >= LIMIT) { hits = hits + 2; }
			bool odd = (ni & 1) == 1;
			if (odd) { hits = hits + 3; }
		}
	}
	return hits;
}

for (int i = 0; i < CELLS; ++i) { SEEN.push(0); }
int check = sweep(16);
profile_it("cell compares", []() { sweep(16); });
print("checksum {}", check);
