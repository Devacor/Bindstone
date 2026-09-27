// Gator Quest - the title screen: the live world drifts by behind the logo
// while you name your gator and pick its color.

import "gq_save.jai";

string CHOSEN_NAME = "";
int CHOSEN_HUE = 0;

array<string> HUE_NAMES = ["Swamp Green", "Teal", "Olive", "Mud Brown", "Blue Steel", "Brick Red", "Royal Purple", "Mint",
	"Tan", "Charcoal", "Albino", "Emerald", "Crimson", "Moss", "Gold", "Ocean"];

array<string> title_pane(int w, int rows) {
	array<string> out;
	pane_begin(w);
	pane_add("1;38;2;40;30;10;48;2;120;190;70", " \xe2\x98\xbc GATOR QUEST ");
	pane_add("38;2;120;190;70", " " + string::repeat("\xe2\x96\x93", imax(0, w - 16)));
	out.push(pane_end());
	array<string> story = [
		"", "The swamp is in trouble.", "",
		"Six tyrants have seized the six realms beyond the Moongates: Big Mama Snapper, the Drowned King, Count Crocula, the Glacier Queen, Emberjaw the Wyrm and the Thunder Roc.",
		"", "Each hoards a Relic Scale. Bring all six to the Shrine of the Six and become the GATOR AVATAR.",
		"", "Fight, fish, mine, craft, cast. Die a little. Your ghost walks back to the healer.", ""];
	for (int i = 0; i < story.size(); ++i) {
		array<string> wl = wrap(story[i], w - 2);
		if (wl.size() == 0) { out.push(pane_line(SGR_DIM, "", w)); }
		for (int k = 0; k < wl.size(); ++k) { out.push(pane_line(SGR_TEXT, " " + wl[k], w)); }
	}
	out.push(rule("CONTROLS", w));
	array<string> keys = ["arrows/WASD move, bump to fight", "SPACE use  F target  G grab", "1-9 cast  I pack  ? help"];
	for (int i = 0; i < keys.size(); ++i) { out.push(pane_line(SGR_DIM, " " + keys[i], w)); }
	while (out.size() < rows) { out.push(pane_line(SGR_DIM, "", w)); }
	return out;
}

// true = start with CHOSEN_NAME / CHOSEN_HUE; false = quit
bool title_screen(bool has_save, int auto_frames) {
	CV = new ClientView();
	CV.slot = -1;
	string name = "";
	int hue = 0;
	int last = clock_ms();
	int acc = 0;
	float ang = 0.0;
	int cx = W_ARRIVE_X[0];
	int cy = W_ARRIVE_Y[0];
	music_play(-1);
	int frames = 0;
	while (true) {
		NOW_MS = clock_ms();
		int dt = imin(250, NOW_MS - last);
		last = NOW_MS;
		acc = acc + dt;
		frames = frames + 1;
		if (auto_frames > 0 && frames > auto_frames) {
			CHOSEN_NAME = "Demo Gator";
			CHOSEN_HUE = 5;
			return true;
		}
		string k = read_key();
		while (k != "") {
			if (k == "enter") {
				CHOSEN_NAME = name == "" ? "Chomps" : name;
				CHOSEN_HUE = hue;
				return true;
			}
			if (k == "esc") { return false; }
			if (k == "left") { hue = (hue + 15) % 16; }
			else if (k == "right") { hue = (hue + 1) % 16; }
			else if (k == "backspace") { if (name.size() > 0) { name = name.substr(0, name.size() - 1); } }
			else if (k == "space") { if (name.size() > 0 && name.size() < 14) { name = name + " "; } }
			else if (k.size() == 1 && name.size() < 14 && (k[0] >= 'a' && k[0] <= 'z' || k[0] >= '0' && k[0] <= '9')) {
				string ch = k;
				if (name.size() == 0 || name.ends_with(" ")) { ch.to_upper(); }
				name = name + ch;
			}
			k = read_key();
		}
		while (acc >= TICK_MS) {
			// a slow circle over Swampton keeps the townsfolk and critters awake
			ang = ang + 0.004;
			TITLE_X = cx + math::itrunc(math::cos(ang) * 14.0);
			TITLE_Y = cy + math::itrunc(math::sin(ang) * 9.0);
			CAMERA_W = 0;
			CAMERA_X = TITLE_X;
			CAMERA_Y = TITLE_Y;
			sim_tick();
			CV.rx_ms = NOW_MS;
			cv_apply(CV, snapshot_view(-1, 0, TITLE_X, TITLE_Y));
			acc = acc - TICK_MS;
		}
		process_events(CV);
		particles_update();
		render_world(CV);
		// the logo, the gator preview, the prompt
		int scale = VW >= 150 ? 3 : 2;
		string logo = "GATOR QUEST";
		int lw = text_px_width(logo, scale);
		int ly = VH / 6;
		draw_text_px(logo, (VW - lw) / 2 + 1, ly + 1, rgb(20, 40, 10), -1, scale);
		draw_text_px(logo, (VW - lw) / 2, ly, FRAME % 40 < 20 ? rgb(130, 220, 80) : rgb(160, 240, 100), COL_BLACK, scale);
		string sub = "A SWAMP IN NEED OF A HERO";
		draw_text_px(sub, (VW - text_px_width(sub, 1)) / 2, ly + 6 * scale + 3, rgb(250, 220, 120), COL_BLACK, 1);
		int off = sprite_compiled(sprite_of("gator"), "hue" + to_string(hue), crgb("leather"), crgb("straw"), crgb("steel"));
		int gx = VW / 2 - TS;
		int gy = VH / 2 - TS / 2;
		for (int py = 0; py < TS * 2; ++py) {
			for (int px = 0; px < TS * 2; ++px) {
				int c = SPRPIX[off + (py / 2) * TS + px / 2];
				if (c >= 0) { pset(gx + px, gy + py - (FRAME / 8) % 2, c); }
			}
		}
		string prompt = "NAME: " + (name == "" ? "CHOMPS" : string::to_upper(name)) + (FRAME % 20 < 10 ? "~" : " ");
		draw_text_px(prompt, (VW - text_px_width(prompt, 1)) / 2, gy + TS * 2 + 4, rgb(255, 255, 255), COL_BLACK, 1);
		string huel = "< " + string::to_upper(HUE_NAMES[hue]) + " >";
		draw_text_px(huel, (VW - text_px_width(huel, 1)) / 2, gy + TS * 2 + 11, rgb(200, 230, 180), COL_BLACK, 1);
		string go = has_save ? "ENTER CONTINUES YOUR SAVED GAME" : "TYPE A NAME, ENTER TO BEGIN";
		draw_text_px(go, (VW - text_px_width(go, 1)) / 2, VH - 8, FRAME % 30 < 20 ? rgb(255, 230, 120) : rgb(200, 170, 80), COL_BLACK, 1);
		array<string> pane = title_pane(PANE_WIDTH, VIEW_ROWS);
		string out = "";
		for (int r = 0; r < VIEW_ROWS; ++r) { out += canvas_row(r) + pane[r] + "\n"; }
		pane_begin(SCREEN_COLS);
		pane_add(SGR_DIM, " type your name   left/right: gator color   enter: start   esc: quit");
		out += pane_end();
		print(HOME + out, skip_newline);
		FRAME = FRAME + 1;
		int spent = clock_ms() - NOW_MS;
		if (spent < 33) { sleep_ms(33 - spent); }
	}
	return false;
}
