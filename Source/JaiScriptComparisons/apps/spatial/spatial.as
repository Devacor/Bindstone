// Large-entity simulation over a spatial hash: 100k entities (sized past the caches on purpose), a cell -> bucket map
// rebuilt every step, 3x3-cell neighbour queries steering each entity away from its crowd, bouncing position updates.
const int ENTITIES = 100000;
const int STEPS = 4;
const int CELL = 1024;
const int GRID = 256;
const int WORLD = CELL * GRID;
const int RADIUS = 1024;
const int MAX_SPEED = 64;

class Rng {
	int64 state;

	Rng(int64 seed) {
		state = seed;
	}

	int64 next() {
		state = (state * 1103515245 + 12345) & 0x7FFFFFFF;
		return state;
	}
}

class Entity {
	int x;
	int y;
	int vx;
	int vy;

	Entity(int a_x, int a_y, int a_vx, int a_vy) {
		x = a_x;
		y = a_y;
		vx = a_vx;
		vy = a_vy;
	}
}

int sign(int value) {
	if (value > 0) {
		return 1;
	}
	if (value < 0) {
		return -1;
	}
	return 0;
}

int clamp_speed(int value) {
	if (value > MAX_SPEED) {
		return MAX_SPEED;
	}
	if (value < -MAX_SPEED) {
		return -MAX_SPEED;
	}
	return value;
}

// dictionary keys are strings only, so the cell map is an array over the grid with a null handle for an empty cell
array<array<int>@>@ build_buckets(array<Entity@>@ entities) {
	array<array<int>@> buckets(GRID * GRID);
	foreach (Entity@ e, int i : entities) {
		int key = (e.y / CELL) * GRID + e.x / CELL;
		array<int>@ bucket = buckets[key];
		if (bucket is null) {
			@buckets[key] = {i};
		} else {
			bucket.insertLast(i);
		}
	}
	return buckets;
}

int steer(array<Entity@>@ entities, array<array<int>@>@ buckets) {
	int radius_squared = RADIUS * RADIUS;
	int contacts = 0;
	foreach (Entity@ e, int i : entities) {
		int cx = e.x / CELL;
		int cy = e.y / CELL;
		int push_x = 0;
		int push_y = 0;
		int gy_end = cy + 1 < GRID - 1 ? cy + 1 : GRID - 1;
		int gx_end = cx + 1 < GRID - 1 ? cx + 1 : GRID - 1;
		for (int gy = cy - 1 > 0 ? cy - 1 : 0; gy <= gy_end; ++gy) {
			for (int gx = cx - 1 > 0 ? cx - 1 : 0; gx <= gx_end; ++gx) {
				array<int>@ bucket = buckets[gy * GRID + gx];
				if (bucket is null) {
					continue;
				}
				foreach (int j : bucket) {
					if (j == i) {
						continue;
					}
					Entity@ other = entities[j];
					int dx = e.x - other.x;
					int dy = e.y - other.y;
					if (dx * dx + dy * dy < radius_squared) {
						++contacts;
						push_x += dx;
						push_y += dy;
					}
				}
			}
		}
		e.vx = clamp_speed(e.vx + sign(push_x));
		e.vy = clamp_speed(e.vy + sign(push_y));
	}
	return contacts;
}

void move(array<Entity@>@ entities) {
	foreach (Entity@ e : entities) {
		e.x += e.vx;
		if (e.x < 0) {
			e.x = -e.x;
			e.vx = -e.vx;
		} else if (e.x >= WORLD) {
			e.x = 2 * (WORLD - 1) - e.x;
			e.vx = -e.vx;
		}
		e.y += e.vy;
		if (e.y < 0) {
			e.y = -e.y;
			e.vy = -e.vy;
		} else if (e.y >= WORLD) {
			e.y = 2 * (WORLD - 1) - e.y;
			e.vy = -e.vy;
		}
	}
}

int64 simulate() {
	Rng rng(20261024);
	array<Entity@> entities;
	for (int i = 0; i < ENTITIES; ++i) {
		int x = int(rng.next() % WORLD);
		int y = int(rng.next() % WORLD);
		int vx = int(rng.next() % (2 * MAX_SPEED + 1)) - MAX_SPEED;
		int vy = int(rng.next() % (2 * MAX_SPEED + 1)) - MAX_SPEED;
		entities.insertLast(Entity(x, y, vx, vy));
	}
	int64 result = 0;
	for (int step = 0; step < STEPS; ++step) {
		array<array<int>@>@ buckets = build_buckets(entities);
		result = (result * 31 + steer(entities, buckets)) & 0xFFFFFFFF;
		move(entities);
	}
	foreach (Entity@ e : entities) {
		result = (result * 31 + e.x) & 0xFFFFFFFF;
		result = (result * 31 + e.y) & 0xFFFFFFFF;
		result = (result * 31 + e.vx) & 0xFFFFFFFF;
		result = (result * 31 + e.vy) & 0xFFFFFFFF;
	}
	return result;
}

void main() {
	double start = clock();
	int64 result = simulate();
	double elapsed = (clock() - start) * 1000.0;
	print("result " + result);
	print("time_ms " + formatFloat(elapsed, "", 0, 3));
}
