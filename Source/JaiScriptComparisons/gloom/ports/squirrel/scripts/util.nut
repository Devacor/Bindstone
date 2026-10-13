// Small helpers: math, hashing, the truecolor palette intern, text padding.

::TICK <- 1.0 / 30.0;   // fixed sim timestep; --smoke feeds exactly this dt

function iabs(v) { return v < 0 ? -v : v; }
function imax(a, b) { return a > b ? a : b; }
function imin(a, b) { return a < b ? a : b; }
function iclamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
function fclamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

// FNV-1a, 32-bit lane inside an int64 (masked every round like the reference)
function mix32(h, v) {
	local x = h ^ (v & 0xFFFFFFFF);
	x = x ^ ((v >> 32) & 0xFFFFFFFF);
	return ((x & 0xFFFFFFFF) * 16777619) & 0xFFFFFFFF;
}

// s repeated n times (Squirrel has no string.repeat)
function rep(s, n) {
	if (n <= 0) { return ""; }
	local r = "";
	for (local i = 0; i < n; ++i) { r += s; }
	return r;
}

// join with pairwise merging: total copying is O(bytes * log n), not O(n^2)
// (Squirrel has no array-of-strings join and 3.2 concat copies both sides)
function join_arr(parts, sep) {
	local n = parts.len();
	if (n == 0) { return ""; }
	local arr = parts;
	if (sep != "") {
		local w = ::array(n * 2 - 1, sep);
		for (local i = 0; i < n; ++i) { w[i * 2] = parts[i]; }
		arr = w;
	}
	while (arr.len() > 1) {
		local m = arr.len();
		local half = (m + 1) / 2;
		local nxt = ::array(half, "");
		for (local i = 0; i + 1 < m; i += 2) { nxt[i / 2] = arr[i] + arr[i + 1]; }
		if (m % 2 == 1) { nxt[half - 1] = arr[m - 1]; }
		arr = nxt;
	}
	return arr[0];
}

// ------------------------------------------------ truecolor palette intern --
// Every color the game shows is minted at boot / map load through rgb_idx(),
// which interns exact 24-bit colors: pixel grids store small palette indices,
// the row builder emits prebuilt truecolor escapes, and PAL_LUM feeds the
// quadrant/sextant bright/dark cell partition.
::PAL_KEYS <- {};    // r<<16|g<<8|b -> index
::PAL_FG <- [];      // ESC[38;2;r;g;bm per index
::PAL_BG <- [];      // ESC[48;2;r;g;bm per index
::PAL_LUM <- [];     // 2r+3g+b per index

function rgb_idx(r, g, b) {
	r = ::iclamp(r, 0, 255);
	g = ::iclamp(g, 0, 255);
	b = ::iclamp(b, 0, 255);
	local key = r * 65536 + g * 256 + b;
	if (key in ::PAL_KEYS) { return ::PAL_KEYS[key]; }
	local idx = ::PAL_FG.len();
	::PAL_KEYS[key] <- idx;
	::PAL_FG.push(::ESC + "[38;2;" + r + ";" + g + ";" + b + "m");
	::PAL_BG.push(::ESC + "[48;2;" + r + ";" + g + ";" + b + "m");
	::PAL_LUM.push(r * 2 + g * 3 + b);
	return idx;
}

// pad/truncate plain text to n columns
function pad_to(s, n) {
	if (s.len() > n) { return s.slice(0, n); }
	return s + ::rep(" ", n - s.len());
}

// smallest signed angle from a to b, in (-pi, pi]
function ang_diff(a, b) {
	local d = b - a;
	while (d > 3.14159265) { d -= 6.2831853; }
	while (d < -3.14159265) { d += 6.2831853; }
	return d;
}

function dist2(ax, ay, bx, by) {
	local dx = ax - bx;
	local dy = ay - by;
	return dx * dx + dy * dy;
}
