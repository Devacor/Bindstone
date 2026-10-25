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
	int state = 0;

	Rng(int seed) {
		state = seed;
	}

	int next() {
		state = (state * 1103515245 + 12345) & 0x7FFFFFFF;
		return state;
	}
}

class Entity {
	int x = 0;
	int y = 0;
	int vx = 0;
	int vy = 0;

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

map<int, array<int>> build_buckets(array<Entity>& entities) {
	map<int, array<int>> buckets = {};
	int count = entities.size();
	for (int i = 0; i < count; ++i) {
		Entity& e = entities[i];
		int key = (e.y / CELL) * GRID + e.x / CELL;
		if (buckets.contains(key)) {
			buckets[key].push(i);
		} else {
			buckets[key] = [i];
		}
	}
	return buckets;
}

int steer(array<Entity>& entities, map<int, array<int>>& buckets) {
	int radius_squared = RADIUS * RADIUS;
	int contacts = 0;
	int count = entities.size();
	for (int i = 0; i < count; ++i) {
		Entity& e = entities[i];
		int cx = e.x / CELL;
		int cy = e.y / CELL;
		int push_x = 0;
		int push_y = 0;
		int gy_end = math::min(cy + 1, GRID - 1);
		int gx_end = math::min(cx + 1, GRID - 1);
		for (int gy = math::max(cy - 1, 0); gy <= gy_end; ++gy) {
			for (int gx = math::max(cx - 1, 0); gx <= gx_end; ++gx) {
				int key = gy * GRID + gx;
				if (!buckets.contains(key)) {
					continue;
				}
				array<int>& bucket = buckets[key];
				for (int j : bucket) {
					if (j == i) {
						continue;
					}
					Entity& other = entities[j];
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

void move(array<Entity>& entities) {
	for (Entity& e : entities) {
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

int simulate() {
	Rng rng = Rng(20261024);
	array<Entity> entities = [];
	for (int i = 0; i < ENTITIES; ++i) {
		int x = rng.next() % WORLD;
		int y = rng.next() % WORLD;
		int vx = rng.next() % (2 * MAX_SPEED + 1) - MAX_SPEED;
		int vy = rng.next() % (2 * MAX_SPEED + 1) - MAX_SPEED;
		entities.push(Entity(x, y, vx, vy));
	}
	int result = 0;
	for (int step = 0; step < STEPS; ++step) {
		map<int, array<int>> buckets = build_buckets(entities);
		result = (result * 31 + steer(entities, buckets)) & 0xFFFFFFFF;
		move(entities);
	}
	for (Entity& e : entities) {
		result = (result * 31 + e.x) & 0xFFFFFFFF;
		result = (result * 31 + e.y) & 0xFFFFFFFF;
		result = (result * 31 + e.vx) & 0xFFFFFFFF;
		result = (result * 31 + e.vy) & 0xFFFFFFFF;
	}
	return result;
}

time::stopwatch clock;
clock.start();
int result = simulate();
float elapsed = clock.elapsed() * 1000.0;
print("result {}", result);
print("time_ms {:.3f}", elapsed);
