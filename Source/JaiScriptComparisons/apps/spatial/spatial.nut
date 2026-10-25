// Large-entity simulation over a spatial hash: 100k entities (sized past the caches on purpose), a cell -> bucket map
// rebuilt every step, 3x3-cell neighbour queries steering each entity away from its crowd, bouncing position updates.
const ENTITIES = 100000;
const STEPS = 4;
const CELL = 1024;
const GRID = 256;
const WORLD = 262144;
const RADIUS = 1024;
const MAX_SPEED = 64;

class Rng {
	state = 0;

	constructor(seed) {
		state = seed;
	}

	function next() {
		state = (state * 1103515245 + 12345) & 0x7FFFFFFF;
		return state;
	}
}

class Entity {
	x = 0;
	y = 0;
	vx = 0;
	vy = 0;

	constructor(a_x, a_y, a_vx, a_vy) {
		x = a_x;
		y = a_y;
		vx = a_vx;
		vy = a_vy;
	}
}

function sign(value) {
	if (value > 0) {
		return 1;
	}
	if (value < 0) {
		return -1;
	}
	return 0;
}

function clamp_speed(value) {
	if (value > MAX_SPEED) {
		return MAX_SPEED;
	}
	if (value < -MAX_SPEED) {
		return -MAX_SPEED;
	}
	return value;
}

function build_buckets(entities) {
	local buckets = {};
	foreach (i, e in entities) {
		local key = (e.y / CELL) * GRID + e.x / CELL;
		if (key in buckets) {
			buckets[key].append(i);
		} else {
			buckets[key] <- [i];
		}
	}
	return buckets;
}

function steer(entities, buckets) {
	local radius_squared = RADIUS * RADIUS;
	local contacts = 0;
	foreach (i, e in entities) {
		local cx = e.x / CELL;
		local cy = e.y / CELL;
		local push_x = 0;
		local push_y = 0;
		local gy_end = cy + 1 < GRID - 1 ? cy + 1 : GRID - 1;
		local gx_end = cx + 1 < GRID - 1 ? cx + 1 : GRID - 1;
		for (local gy = cy - 1 > 0 ? cy - 1 : 0; gy <= gy_end; gy++) {
			for (local gx = cx - 1 > 0 ? cx - 1 : 0; gx <= gx_end; gx++) {
				local key = gy * GRID + gx;
				if (!(key in buckets)) {
					continue;
				}
				foreach (j in buckets[key]) {
					if (j == i) {
						continue;
					}
					local other = entities[j];
					local dx = e.x - other.x;
					local dy = e.y - other.y;
					if (dx * dx + dy * dy < radius_squared) {
						contacts++;
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

function move(entities) {
	foreach (e in entities) {
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

local start = clock();
local rng = Rng(20261024);
local entities = [];
for (local i = 0; i < ENTITIES; i++) {
	local x = rng.next() % WORLD;
	local y = rng.next() % WORLD;
	local vx = rng.next() % (2 * MAX_SPEED + 1) - MAX_SPEED;
	local vy = rng.next() % (2 * MAX_SPEED + 1) - MAX_SPEED;
	entities.append(Entity(x, y, vx, vy));
}
local result = 0;
for (local step = 0; step < STEPS; step++) {
	local buckets = build_buckets(entities);
	result = (result * 31 + steer(entities, buckets)) & 0xFFFFFFFF;
	move(entities);
}
foreach (e in entities) {
	result = (result * 31 + e.x) & 0xFFFFFFFF;
	result = (result * 31 + e.y) & 0xFFFFFFFF;
	result = (result * 31 + e.vx) & 0xFFFFFFFF;
	result = (result * 31 + e.vy) & 0xFFFFFFFF;
}
local elapsed = (clock() - start) * 1000.0;
print(format("result %d\n", result));
print(format("time_ms %.3f\n", elapsed));
