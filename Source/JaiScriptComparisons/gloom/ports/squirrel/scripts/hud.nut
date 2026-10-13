// HUD: status strip, vitals with bars, ammo rack, the face, the bobbing
// weapon art — plus the full-screen states (title, tally, death, victory).
// Text content is byte-identical to the reference so the frame-stream hash
// stays comparable across ports.

::FACES <- {
	ok = [
		" .-----. ",
		" | o.o | ",
		" |  L  | ",
		" | --- | ",
		" '-----' "],
	hurt = [
		" .-----. ",
		" | o.o | ",
		" | ,L  | ",
		" | ~~~ | ",
		" '-----' "],
	bad = [
		" .-----. ",
		" | x.o |,",
		" | ,L. |,",
		" | www | ",
		" '-----' "],
	crit = [
		" .-----. ",
		" |,x.x,|,",
		" |,,L,,|,",
		" | mmm |,",
		" '-----' "],
	pain = [
		" .-----. ",
		" | >.< | ",
		" | ,L, | ",
		" | OOO | ",
		" '-----' "],
	grin = [
		" .-----. ",
		" | ^.^ | ",
		" |  L  | ",
		" | \\_/ | ",
		" '-----' "],
	dead = [
		" .-----. ",
		" | x.x | ",
		" |  L  | ",
		" | ___ | ",
		" '-----' "]
};

function face_name() {
	local g = ::G;
	if (g.mode == 3) { return "dead"; }
	if (g.face_pain > 0) { return "pain"; }
	if (g.face_grin > 0) { return "grin"; }
	if (g.hp > 70) { return "ok"; }
	if (g.hp > 40) { return "hurt"; }
	if (g.hp > 18) { return "bad"; }
	return "crit";
}

function meter(val, cap, slots) {
	local fill = ::iclamp(val * slots / ::imax(1, cap), 0, slots);
	return ::rep("#", fill) + ::rep("-", slots - fill);
}

function show_msg(text, color) {
	local g = ::G;
	g.msg = text;
	g.msg_color = color;
	g.msg_t = 75;
}

// the status strip above the panel: map, tallies, message
function hud_status_row() {
	local g = ::G;
	local left = " " + g.map_name + "  KILLS " + g.kills + "/" + g.kill_total + "  SCRT " + g.secrets + "/" + g.secret_total;
	local m = g.msg_t > 0 ? g.msg : "";
	local room = g.w - left.len() - 2;
	if (m.len() > room) { m = m.slice(0, ::imax(0, room)); }
	local body = ::pad_to(left + ::rep(" ", ::imax(1, g.w - left.len() - m.len() - 1)) + m, g.w);
	local strip = ::ESC + "[48;5;235m" + ::ESC + "[38;5;250m";
	if (g.msg_t > 0) {
		strip = ::ESC + "[48;5;235m" + ::ESC + "[" + g.msg_color + "m";
	}
	return strip + body + ::RESET;
}

// five panel rows: vitals | ammo | face | weapon art
function hud_panel_rows(fps, ms_sim, ms_draw) {
	local g = ::G;
	local w = ::WEAPONS[g.weapon];
	local face = ::FACES[::face_name()];

	// weapon art with walk-bob shift and fire frame
	local art = (g.cooldown > ::WEAPONS[g.weapon].cd - 4 || g.gun_kick > 0)
		? ::WGFX_FIRE[g.weapon] : ::WGFX_IDLE[g.weapon];
	local bob_shift = 1 + ((::sin(g.bob) + 1.0) * 1.4).tointeger();

	local left = [
		" HP  [" + ::meter(g.hp, 100, 12) + "] " + g.hp,
		" ARM [" + ::meter(g.armor, 100, 12) + "] " + g.armor,
		" ",
		" " + (g.key_r ? "[R]" : " . ") + " " + (g.key_b ? "[B]" : " . ") + "  " + w.name,
		" " + (g.autopilot ? "AUTOPILOT" : "")
	];
	local mid = [
		"BUL " + g.ammo[0] + "/" + ::AMMO_MAX[0],
		"SHL " + g.ammo[1] + "/" + ::AMMO_MAX[1],
		"CEL " + g.ammo[2] + "/" + ::AMMO_MAX[2],
		(g.have[1] == 1 ? "2:SCAT " : "       ") + (g.have[2] == 1 ? "3:HEX" : ""),
		fps > 0.0 ? ::format("%.0f", fps) + "fps " + ::format("%.1f", ms_sim + ms_draw) + "ms" : ""
	];

	local rows = [];
	local left_w = 34;
	local mid_w = 18;
	local face_w = 11;
	local art_w = 14;
	for (local r = 0; r < 5; ++r) {
		local lseg = ::pad_to(left[r], left_w);
		local mseg = ::pad_to(mid[r], mid_w);
		local fseg = ::pad_to(face[r], face_w);
		local used = left_w + mid_w + face_w + art_w;
		local fill = ::imax(0, g.w - used);
		local aseg = ::pad_to(::rep(" ", ::imin(fill > 0 ? bob_shift : 0, 2)) + art[r], art_w + fill);
		local hp_col = g.hp > 40 ? "97" : "91";
		local acut = ::imin(aseg.len(), ::imax(0, g.w - left_w - mid_w - face_w));
		rows.push(::ESC + "[48;5;236m" + ::ESC + "[" + hp_col + "m" + lseg +
			::ESC + "[38;5;179m" + mseg +
			::ESC + "[38;5;114m" + fseg +
			::ESC + "[38;5;250m" + aseg.slice(0, acut) + ::RESET);
	}
	return rows;
}

// ------------------------------------------------------- full-screen modes --
::LOGO <- [
	"  ..|'''.|  '||'       ..|''||    ..|''||   '||    ||' ",
	" .|'     '   ||       .|'    ||  .|'    ||   |||  |||  ",
	" ||    ....  ||       ||      || ||      ||  |'|..'||  ",
	" '|.    ||   ||       '|.     || '|.     ||  | '|' ||  ",
	"  ''|...'|  .||.....|  ''|...|'   ''|...|'  .|. | .||. "
];

function screen_rows(lines, tint) {
	// center the payload block on a dark field, pad every row to full width
	local g = ::G;
	local rows = [];
	local total = g.h;
	local top = ::imax(0, (total - lines.len()) / 2);
	for (local y = 0; y < total; ++y) {
		local li = y - top;
		local body = "";
		if (li >= 0 && li < lines.len()) { body = lines[li]; }
		local padl = ::imax(0, (g.w - body.len()) / 2);
		rows.push(::ESC + "[48;5;233m" + tint + ::pad_to(::rep(" ", padl) + body, g.w) + ::RESET);
	}
	return rows;
}

function render_title() {
	local g = ::G;
	local lines = [];
	foreach (l in ::LOGO) { lines.push(l); }
	lines.push("");
	lines.push("a  t e r m i n a l  r a y c a s t e r ,  a l l  J a i S c r i p t");
	lines.push("");
	lines.push("");
	local blink = (g.tick / 12) % 2;
	lines.push(blink == 0 ? ">>> press FIRE to descend <<<" : "");
	lines.push("");
	lines.push("w/s move  a/d strafe  arrows turn  SPACE fire  E use  1/2/3 weapons  m map");
	local tint = (g.tick / 4) % 3 == 0 ? ::ESC + "[38;5;203m" : ::ESC + "[38;5;209m";
	return ::join_arr(::screen_rows(lines, tint), "\n");
}

function render_tally() {
	local g = ::G;
	local kp = g.kill_total > 0 ? g.kills * 100 / g.kill_total : 100;
	local sp = g.secret_total > 0 ? g.secrets * 100 / g.secret_total : 100;
	local secs = g.map_ticks / 30;
	local par_secs = g.par / 30;
	local lines = [
		g.map_name,
		"",
		"=== FLOOR CLEARED ===",
		"",
		"KILLS    " + g.kills + " / " + g.kill_total + "   (" + kp + "%)",
		"SECRETS  " + g.secrets + " / " + g.secret_total + "   (" + sp + "%)",
		"TIME     " + secs + "s   par " + par_secs + "s",
		"",
		g.map_i + 1 < ::MAPS.len() ? "press FIRE for the next floor" : "press FIRE to face the dark"
	];
	return ::join_arr(::screen_rows(lines, ::ESC + "[38;5;114m"), "\n");
}

function render_dead() {
	local g = ::G;
	local lines = [
		"Y O U   D I E D",
		"",
		"slain by " + g.death_cause,
		"on " + g.map_name,
		"",
		"kills " + g.kills + "/" + g.kill_total + "   deaths " + g.deaths,
		"",
		"press FIRE to rise again"
	];
	return ::join_arr(::screen_rows(lines, ::ESC + "[38;5;196m"), "\n");
}

function render_victory() {
	local g = ::G;
	local mins = g.ep_ticks / 1800;
	local secs = (g.ep_ticks / 30) % 60;
	local lines = [];
	foreach (l in ::LOGO) { lines.push(l); }
	lines.push("");
	lines.push("T H E   W A R D E N   I S   D O W N");
	lines.push("");
	lines.push("episode kills    " + g.ep_kills + " / " + g.ep_kill_total);
	lines.push("episode secrets  " + g.ep_secrets + " / " + g.ep_secret_total);
	lines.push("episode time     " + mins + "m " + secs + "s   deaths " + g.deaths);
	lines.push("");
	lines.push("the gloom lifts. for now.        (ESC quits)");
	return ::join_arr(::screen_rows(lines, ::ESC + "[38;5;220m"), "\n");
}

// the play-mode frame: world rows + status strip + panel
function render_play(fps, ms_sim, ms_draw) {
	local rows = ::render_view();
	rows.push(::hud_status_row());
	foreach (r in ::hud_panel_rows(fps, ms_sim, ms_draw)) { rows.push(r); }
	return ::join_arr(rows, "\n");
}

function render_frame(fps, ms_sim, ms_draw) {
	local g = ::G;
	if (g.mode == 0) { return ::render_title(); }
	if (g.mode == 2) { return ::render_tally(); }
	if (g.mode == 3) { return ::render_dead(); }
	if (g.mode == 4) { return ::render_victory(); }
	return ::render_play(fps, ms_sim, ms_draw);
}
