# Large-entity simulation over a spatial hash: 100k entities (sized past the caches on purpose), a cell -> bucket map
# rebuilt every step, 3x3-cell neighbour queries steering each entity away from its crowd, bouncing position updates.
import time

ENTITIES = 100000
STEPS = 4
CELL = 1024
GRID = 256
WORLD = CELL * GRID
RADIUS = 1024
MAX_SPEED = 64


class Rng:
	def __init__(self, seed):
		self.state = seed

	def next(self):
		self.state = (self.state * 1103515245 + 12345) & 0x7FFFFFFF
		return self.state


class Entity:
	def __init__(self, x, y, vx, vy):
		self.x = x
		self.y = y
		self.vx = vx
		self.vy = vy


def sign(value):
	if value > 0:
		return 1
	if value < 0:
		return -1
	return 0


def clamp_speed(value):
	if value > MAX_SPEED:
		return MAX_SPEED
	if value < -MAX_SPEED:
		return -MAX_SPEED
	return value


def build_buckets(entities):
	buckets = {}
	for i, e in enumerate(entities):
		key = (e.y // CELL) * GRID + e.x // CELL
		bucket = buckets.get(key)
		if bucket is None:
			buckets[key] = [i]
		else:
			bucket.append(i)
	return buckets


def steer(entities, buckets):
	radius_squared = RADIUS * RADIUS
	contacts = 0
	for i, e in enumerate(entities):
		cx = e.x // CELL
		cy = e.y // CELL
		push_x = 0
		push_y = 0
		for gy in range(max(cy - 1, 0), min(cy + 1, GRID - 1) + 1):
			for gx in range(max(cx - 1, 0), min(cx + 1, GRID - 1) + 1):
				bucket = buckets.get(gy * GRID + gx)
				if bucket is None:
					continue
				for j in bucket:
					if j == i:
						continue
					other = entities[j]
					dx = e.x - other.x
					dy = e.y - other.y
					if dx * dx + dy * dy < radius_squared:
						contacts += 1
						push_x += dx
						push_y += dy
		e.vx = clamp_speed(e.vx + sign(push_x))
		e.vy = clamp_speed(e.vy + sign(push_y))
	return contacts


def move(entities):
	for e in entities:
		e.x += e.vx
		if e.x < 0:
			e.x = -e.x
			e.vx = -e.vx
		elif e.x >= WORLD:
			e.x = 2 * (WORLD - 1) - e.x
			e.vx = -e.vx
		e.y += e.vy
		if e.y < 0:
			e.y = -e.y
			e.vy = -e.vy
		elif e.y >= WORLD:
			e.y = 2 * (WORLD - 1) - e.y
			e.vy = -e.vy


def main():
	start = time.perf_counter()
	rng = Rng(20261024)
	entities = []
	for _ in range(ENTITIES):
		x = rng.next() % WORLD
		y = rng.next() % WORLD
		vx = rng.next() % (2 * MAX_SPEED + 1) - MAX_SPEED
		vy = rng.next() % (2 * MAX_SPEED + 1) - MAX_SPEED
		entities.append(Entity(x, y, vx, vy))
	result = 0
	for _ in range(STEPS):
		buckets = build_buckets(entities)
		result = (result * 31 + steer(entities, buckets)) & 0xFFFFFFFF
		move(entities)
	for e in entities:
		result = (result * 31 + e.x) & 0xFFFFFFFF
		result = (result * 31 + e.y) & 0xFFFFFFFF
		result = (result * 31 + e.vx) & 0xFFFFFFFF
		result = (result * 31 + e.vy) & 0xFFFFFFFF
	elapsed = (time.perf_counter() - start) * 1000.0
	print(f"result {result}")
	print(f"time_ms {elapsed:.3f}")


main()
