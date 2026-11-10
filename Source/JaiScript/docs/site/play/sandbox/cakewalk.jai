// CAKEWALK: a gator in a dark cave. Eat every cake to go deeper; a bat costs a heart.
// Arrows or WASD move, esc quits. Until a key is pressed the gator walks itself.
string ESC = "\x1b";
int W = math::min(term_cols() / 2, 48);   // a tile is two characters wide
int H = math::min(term_rows() - 2, 22);
int WALL = 0;
int FLOOR = 1;
int CAKE = 2;
int FRAMES_PER_STEP = 4;                  // the gator walks 7.5 tiles a second, a bat half that
map<string, int> STEPS = {"left": -1, "a": -1, "right": 1, "d": 1, "up": -W, "w": -W, "down": W, "s": W};   // a key's step, as a tile offset
int frame_limit = ARGS.size() > 0 ? ARGS[0].to_int() : 0;   // jaiscript cakewalk.jai -- 300: play 300 frames unpaced, then quit

class Bat {
	int at = 0;
	Bat(int at_) { at = at_; }
}

array<int> tiles;      // W * H of WALL, FLOOR or CAKE, row by row
array<bool> seen;      // the tiles the torch has lit so far
array<int> to_gator;   // steps from each tile to the gator, -1 where no path leads
array<int> to_cake;    // steps from each tile to the nearest cake
array<Bat> bats;
random rng(2026);
int gator = 0;         // the gator's tile: x + y * W
bool faces_left = false;
int level = 1;
int hearts = 3;
int cakes = 0;
int eaten = 0;
int hurt_frames = 0;   // the screen flashes red while this counts down
bool piloted = false;  // false until the first key press: the gator follows to_cake

// Breadth-first search: the steps from the start tiles to every tile a walk can reach
array<int> flood(array<int> starts) {
	array<int> steps;
	steps.resize(W * H, -1);
	for (int start : starts) { steps[start] = 0; }
	for (int head = 0; head < starts.size(); ++head) {
		int at = starts[head];
		for (int next : [at - 1, at + 1, at - W, at + W]) {   // the map's rim is wall, so no neighbour leaves it
			if (tiles[next] != WALL && steps[next] < 0) {
				steps[next] = steps[at] + 1;
				starts.push(next);
			}
		}
	}
	return steps;
}

// The neighbour of a tile that is one step closer by a flood's count, or the tile itself
int downhill(array<int>& steps, int at) {
	for (int next : [at - 1, at + 1, at - W, at + W]) {
		if (steps[next] >= 0 && steps[next] < steps[at]) { return next; }
	}
	return at;
}

array<int> tiles_where(int kind) {
	array<int> result;
	for (int i = 0; i < W * H; ++i) {
		if (tiles[i] == kind) { result.push(i); }
	}
	return result;
}

// A cave by cellular automaton: random rock, then four passes where a tile turns to what most of its nine neighbours are
void dig_cave() {
	tiles.resize(W * H);
	for (int i = 0; i < W * H; ++i) {
		tiles[i] = rng.next_int(0, 99) < 56 ? FLOOR : WALL;
	}
	for (int pass = 0; pass < 4; ++pass) {
		array<int> before = tiles;   // assignment copies: the pass reads the old cave while it writes the new one
		for (int y = 0; y < H; ++y) {
			for (int x = 0; x < W; ++x) {
				int walls = 0;
				for (int ny = y - 1; ny <= y + 1; ++ny) {
					for (int nx = x - 1; nx <= x + 1; ++nx) {
						bool outside = nx < 1 || ny < 1 || nx >= W - 1 || ny >= H - 1;
						if (outside || before[nx + ny * W] == WALL) { walls += 1; }
					}
				}
				tiles[x + y * W] = walls >= 5 ? WALL : FLOOR;
			}
		}
	}
}

void start_level() {
	array<int> open;
	while (open.size() < W * H / 3) {   // a cave whose biggest room is too small is dug again
		dig_cave();
		open = tiles_where(FLOOR);
		gator = open[open.size() / 2];
		to_gator = flood([gator]);
		open.filter([](int tile) { return to_gator[tile] >= 0; });
	}
	for (int i = 0; i < W * H; ++i) {
		if (to_gator[i] < 0) { tiles[i] = WALL; }   // seal the pockets no walk reaches
	}
	open.filter([](int tile) { return to_gator[tile] > 8; });
	rng.shuffle(open);
	cakes = math::min(5 + level, open.size() / 2);
	for (int i = 0; i < cakes; ++i) { tiles[open[i]] = CAKE; }
	bats.clear();
	for (int i = 0; i < 1 + level; ++i) { bats.push(Bat(open[cakes + i])); }
	seen.resize(W * H);
	seen.fill(false);
	to_cake = flood(tiles_where(CAKE));
}

void move_gator(int to) {
	if (to == gator || tiles[to] == WALL) { return; }
	faces_left = to == gator - 1 || (faces_left && to != gator + 1);
	gator = to;
	if (tiles[gator] == CAKE) {
		tiles[gator] = FLOOR;
		cakes -= 1;
		eaten += 1;
		to_cake = flood(tiles_where(CAKE));
	}
	to_gator = flood([gator]);
}

// A bat that can smell the gator (within 10 steps) closes in; any other flutters about
void move_bats() {
	for (auto& bat : bats) {
		int next = bat.at;
		if (to_gator[bat.at] <= 10) {
			next = downhill(to_gator, bat.at);
		} else {
			next = rng.pick([bat.at - 1, bat.at + 1, bat.at - W, bat.at + W]);
		}
		if (tiles[next] != WALL) { bat.at = next; }
	}
}

void check_bites() {
	for (auto& bat : bats) {
		if (bat.at == gator && hurt_frames == 0) {
			hearts -= 1;
			hurt_frames = 20;
			bat.at = rng.pick(tiles_where(FLOOR));
		}
	}
}

string ink(int r, int g, int b) { return `${ESC}[38;2;${r};${g};${b}m`; }
string paper(float r, float g, float b) { return `${ESC}[48;2;${math::itrunc(r)};${math::itrunc(g)};${math::itrunc(b)}m`; }

string draw(int frame) {
	float reach = 8.0 + 0.5 * math::sin(frame * 0.45);   // the torch flickers
	int gx = gator % W;
	int gy = gator / W;
	array<bool> bat_here;
	bat_here.resize(W * H);
	for (auto& bat : bats) { bat_here[bat.at] = true; }
	string result = ESC + "[H";
	for (int y = 0; y < H; ++y) {
		string style = "";
		for (int x = 0; x < W; ++x) {
			int i = x + y * W;
			float away = math::sqrt(cast<float>((x - gx) * (x - gx) + (y - gy) * (y - gy)));
			float light = math::max(0.0, 1.0 - away / reach);
			if (light > 0.0) { seen[i] = true; }
			float glow = seen[i] ? math::max(light, 0.2) * ((x + y) % 2 == 0 ? 1.0 : 0.88) : 0.0;   // remembered tiles stay dim; a checker shows the grid
			float red = hurt_frames > 0 ? 90.0 * light : 0.0;
			string back = tiles[i] == WALL ? paper(70 * glow + red, 80 * glow, 120 * glow) : paper(200 * glow + red, 130 * glow, 60 * glow);
			string tile = "  ";
			if (i == gator) {
				back = paper(70, 200, 90);
				tile = ink(10, 40, 20) + (faces_left ? "<:" : ":>");
			} else if (bat_here[i] && light > 0.0) {
				tile = ink(230, 120, 255) + "^^";
			} else if (tiles[i] == CAKE) {
				tile = ink(255, 150, 200) + "◢◣";   // a cake glows: it shows in the dark
			}
			if (back != style) {   // a colour is written only where it changes
				style = back;
				result += back;
			}
			result += tile;
		}
		result += ESC + "[0m\n";
	}
	string status = `CAKEWALK  cave ${level}  cakes left ${cakes}  ${string::repeat("♥", hearts)}`;
	status += piloted ? "   arrows or WASD move, esc quits" : "   the gator walks itself: press a key to take over";
	return result + status + ESC + "[K";
}

print(ESC + "[?25l" + ESC + "[2J", skip_newline);
start_level();
time::stopwatch clock;
clock.start();
float frame_time = frame_limit > 0 ? 0.0 : 1.0 / 30.0;
bool running = true;
int heading = 0;   // the step the keys ask for; 0 stands still
for (int frame = 0; running && (frame_limit == 0 || frame < frame_limit); ++frame) {
	float frame_start = clock.elapsed();
	for (string key = read_key(); key != ""; key = read_key()) {
		piloted = true;
		if (key == "esc") { running = false; }
		if (STEPS.has(key)) { heading = STEPS[key]; }   // a tap between two steps still moves a tile
	}
	if (frame % FRAMES_PER_STEP == 0) {
		for (auto [key, step] : STEPS) {
			if (key_down(key) && tiles[gator + step] != WALL) { heading = step; }   // a held key keeps walking
		}
		move_gator(piloted ? gator + heading : downhill(to_cake, gator));
		heading = 0;
		check_bites();
	}
	if (frame % (FRAMES_PER_STEP * 2) == 0) {
		move_bats();
		check_bites();
	}
	if (hurt_frames > 0) { hurt_frames -= 1; }
	if (hearts == 0) {   // out of hearts: back to the first cave
		level = 1;
		hearts = 3;
		eaten = 0;
		start_level();
	} else if (cakes == 0) {
		level += 1;
		start_level();
	}
	print(draw(frame), skip_newline);   // the whole frame in one write
	float spent = clock.elapsed() - frame_start;
	if (spent < frame_time) { sleep(frame_time - spent); }
}
print(ESC + "[0m" + ESC + "[?25h");
print(`The gator ate ${eaten} cakes and reached cave ${level}.`);
