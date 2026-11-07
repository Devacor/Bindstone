// HUD: status strip, vitals with bars, ammo rack, the face, the bobbing
// weapon art, plus the full-screen states (title, tally, death, victory).

const int FACE_OK = 0;
const int FACE_HURT = 1;
const int FACE_BAD = 2;
const int FACE_CRIT = 3;
const int FACE_PAIN = 4;
const int FACE_GRIN = 5;
const int FACE_DEAD = 6;

const array<array<string>> FACES = {
	{
		" .-----. ",
		" | o.o | ",
		" |  L  | ",
		" | --- | ",
		" '-----' "},
	{
		" .-----. ",
		" | o.o | ",
		" | ,L  | ",
		" | ~~~ | ",
		" '-----' "},
	{
		" .-----. ",
		" | x.o |,",
		" | ,L. |,",
		" | www | ",
		" '-----' "},
	{
		" .-----. ",
		" |,x.x,|,",
		" |,,L,,|,",
		" | mmm |,",
		" '-----' "},
	{
		" .-----. ",
		" | >.< | ",
		" | ,L, | ",
		" | OOO | ",
		" '-----' "},
	{
		" .-----. ",
		" | ^.^ | ",
		" |  L  | ",
		" | \\_/ | ",
		" '-----' "},
	{
		" .-----. ",
		" | x.x | ",
		" |  L  | ",
		" | ___ | ",
		" '-----' "}
};

int face_index() {
	if (G.mode == 3) { return FACE_DEAD; }
	if (G.face_pain > 0) { return FACE_PAIN; }
	if (G.face_grin > 0) { return FACE_GRIN; }
	if (G.hp > 70) { return FACE_OK; }
	if (G.hp > 40) { return FACE_HURT; }
	if (G.hp > 18) { return FACE_BAD; }
	return FACE_CRIT;
}

string meter(int val, int cap, int slots) {
	int fill = iclamp(val * slots / imax(1, cap), 0, slots);
	return rep("#", fill) + rep("-", slots - fill);
}

void show_msg(const string &in text, const string &in color) {
	G.msg = text;
	G.msg_color = color;
	G.msg_t = 75;
}

// the status strip above the panel: map, tallies, message
string hud_status_row() {
	string left = " " + G.map_name + "  KILLS " + G.kills + "/" + G.kill_total + "  SCRT " + G.secrets + "/" + G.secret_total;
	string m = G.msg_t > 0 ? G.msg : "";
	int room = G.w - len(left) - 2;
	if (len(m) > room) { m = m.substr(0, imax(0, room)); }
	string body = pad_to(left + rep(" ", imax(1, G.w - len(left) - len(m) - 1)) + m, G.w);
	string strip = ESC + "[48;5;235m" + ESC + "[38;5;250m";
	if (G.msg_t > 0) {
		strip = ESC + "[48;5;235m" + ESC + "[" + G.msg_color + "m";
	}
	return strip + body + RESET;
}

// five panel rows: vitals | ammo | face | weapon art
void hud_panel_rows(array<string>@ rows, double fps, double ms_sim, double ms_draw) {
	WeaponDef@ w = WEAPONS[G.weapon];
	const array<string>@ face = FACES[face_index()];

	// weapon art with walk-bob shift and fire frame
	const array<string>@ art = G.cooldown > w.cd - 4 || G.gun_kick > 0 ? WGFX_FIRE[G.weapon] : WGFX_IDLE[G.weapon];
	int bob_shift = 1 + itrunc((sin(G.bob) + 1.0) * (14.0 / 10.0));

	array<string> left = {
		" HP  [" + meter(G.hp, 100, 12) + "] " + G.hp,
		" ARM [" + meter(G.armor, 100, 12) + "] " + G.armor,
		" ",
		" " + (G.key_r ? "[R]" : " . ") + " " + (G.key_b ? "[B]" : " . ") + "  " + w.name,
		" " + (G.autopilot ? "AUTOPILOT" : "")
	};
	array<string> mid = {
		"BUL " + G.ammo[0] + "/" + AMMO_MAX[0],
		"SHL " + G.ammo[1] + "/" + AMMO_MAX[1],
		"CEL " + G.ammo[2] + "/" + AMMO_MAX[2],
		(G.have[1] == 1 ? "2:SCAT " : "       ") + (G.have[2] == 1 ? "3:HEX" : ""),
		fps > 0.0 ? formatFloat(fps, "", 0, 0) + "fps " + formatFloat(ms_sim + ms_draw, "", 0, 1) + "ms" : ""
	};

	int left_w = 34;
	int mid_w = 18;
	int face_w = 11;
	int art_w = 14;
	string hp_col = G.hp > 40 ? "97" : "91";
	for (int r = 0; r < 5; ++r) {
		string lseg = pad_to(left[r], left_w);
		string mseg = pad_to(mid[r], mid_w);
		string fseg = pad_to(face[r], face_w);
		int used = left_w + mid_w + face_w + art_w;
		int fill = imax(0, G.w - used);
		string aseg = pad_to(rep(" ", imin(fill > 0 ? bob_shift : 0, 2)) + art[r], art_w + fill);
		rows.insertLast(ESC + "[48;5;236m" + ESC + "[" + hp_col + "m" + lseg +
			ESC + "[38;5;179m" + mseg +
			ESC + "[38;5;114m" + fseg +
			ESC + "[38;5;250m" + aseg.substr(0, imax(0, G.w - left_w - mid_w - face_w)) + RESET);
	}
}

// ------------------------------------------------------- full-screen modes --
const array<string> LOGO = {
	"  ..|'''.|  '||'       ..|''||    ..|''||   '||    ||' ",
	" .|'     '   ||       .|'    ||  .|'    ||   |||  |||  ",
	" ||    ....  ||       ||      || ||      ||  |'|..'||  ",
	" '|.    ||   ||       '|.     || '|.     ||  | '|' ||  ",
	"  ''|...'|  .||.....|  ''|...|'   ''|...|'  .|. | .||. "
};

// center the payload block on a dark field, pad every row to full width
string screen_rows(const array<string> &in lines, const string &in tint) {
	array<string> rows;
	int total = G.h;
	int top = imax(0, (total - int(lines.length())) / 2);
	for (int y = 0; y < total; ++y) {
		int li = y - top;
		string body = "";
		if (li >= 0 && li < int(lines.length())) { body = lines[li]; }
		int padl = imax(0, (G.w - len(body)) / 2);
		rows.insertLast(ESC + "[48;5;233m" + tint + pad_to(rep(" ", padl) + body, G.w) + RESET);
	}
	return join(rows, "\n");
}

string render_title() {
	array<string> lines = LOGO;
	lines.insertLast("");
	lines.insertLast("a  t e r m i n a l  r a y c a s t e r ,  a l l  J a i S c r i p t");
	lines.insertLast("");
	lines.insertLast("");
	int blink = (G.tick / 12) % 2;
	lines.insertLast(blink == 0 ? ">>> press FIRE to descend <<<" : "");
	lines.insertLast("");
	lines.insertLast("w/s move  a/d strafe  arrows turn  SPACE fire  E use  1/2/3 weapons  m map");
	string tint = (G.tick / 4) % 3 == 0 ? ESC + "[38;5;203m" : ESC + "[38;5;209m";
	return screen_rows(lines, tint);
}

string render_tally() {
	int kp = G.kill_total > 0 ? G.kills * 100 / G.kill_total : 100;
	int sp = G.secret_total > 0 ? G.secrets * 100 / G.secret_total : 100;
	int secs = G.map_ticks / 30;
	int par_secs = G.par / 30;
	array<string> lines = {
		G.map_name,
		"",
		"=== FLOOR CLEARED ===",
		"",
		"KILLS    " + G.kills + " / " + G.kill_total + "   (" + kp + "%)",
		"SECRETS  " + G.secrets + " / " + G.secret_total + "   (" + sp + "%)",
		"TIME     " + secs + "s   par " + par_secs + "s",
		"",
		G.map_i + 1 < int(MAPS.length()) ? "press FIRE for the next floor" : "press FIRE to face the dark"
	};
	return screen_rows(lines, ESC + "[38;5;114m");
}

string render_dead() {
	array<string> lines = {
		"Y O U   D I E D",
		"",
		"slain by " + G.death_cause,
		"on " + G.map_name,
		"",
		"kills " + G.kills + "/" + G.kill_total + "   deaths " + G.deaths,
		"",
		"press FIRE to rise again"
	};
	return screen_rows(lines, ESC + "[38;5;196m");
}

string render_victory() {
	int mins = G.ep_ticks / 1800;
	int secs = (G.ep_ticks / 30) % 60;
	array<string> lines = LOGO;
	lines.insertLast("");
	lines.insertLast("T H E   W A R D E N   I S   D O W N");
	lines.insertLast("");
	lines.insertLast("episode kills    " + G.ep_kills + " / " + G.ep_kill_total);
	lines.insertLast("episode secrets  " + G.ep_secrets + " / " + G.ep_secret_total);
	lines.insertLast("episode time     " + mins + "m " + secs + "s   deaths " + G.deaths);
	lines.insertLast("");
	lines.insertLast("the gloom lifts. for now.        (ESC quits)");
	return screen_rows(lines, ESC + "[38;5;220m");
}

// the play-mode frame: world rows + status strip + panel
string render_play(double fps, double ms_sim, double ms_draw) {
	array<string>@ rows = render_view();
	rows.insertLast(hud_status_row());
	hud_panel_rows(rows, fps, ms_sim, ms_draw);
	return join(rows, "\n");
}

string render_frame(double fps, double ms_sim, double ms_draw) {
	if (G.mode == 0) { return render_title(); }
	if (G.mode == 2) { return render_tally(); }
	if (G.mode == 3) { return render_dead(); }
	if (G.mode == 4) { return render_victory(); }
	return render_play(fps, ms_sim, ms_draw);
}
