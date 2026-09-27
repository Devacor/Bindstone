// Gator Quest - the snapshot protocol. Every tick the host writes each player
// a text snapshot of what that player can know: the mobiles and items around
// them, the events they should see, their own vitals, and their character
// sheet when it changed. The local player reads the same text as a remote
// one; the renderer only ever sees a ClientView filled from it.
//
// One record per line, fields split by '|', text fields last:
//   S|tick|world|myser|weather|tod|day          Y|hp|hpmax|mp|mpmax|target|action|left|ghost
//   M|ser|kind|x|y|px|py|mt|md|dir|hp%|flags|role|slot|hue|name
//   I|ser|base|rarity|x|y|qty                   T|index|tile (after TC: terrain overrides)
//   E|kind|w|x|y|a|b|c|text                     C|... sheet, K skills, P pack, W worn, B bank, L spells, J journal

import "gq_bot.jai";

int VIEW_R = 24;               // snapshot radius in tiles

string clean_text(string s) {
	string out = s;
	out.replace_all("|", "/");
	out.replace_all("\n", " ");
	return out;
}

string enc_item(Item it) {
	return `${it.base}.${it.qty}.${it.rarity}.${it.prefix}.${it.suffix}.${it.pmag}.${it.smag}.${it.bonus}`;
}

Item dec_item(string s) {
	array<string> f = s.split(".");
	Item it = Item();
	if (f.size() < 8) { return it; }
	it.base = f[0].to_int(-1);
	it.qty = f[1].to_int(0);
	it.rarity = f[2].to_int(0);
	it.prefix = f[3].to_int(-1);
	it.suffix = f[4].to_int(-1);
	it.pmag = f[5].to_int(0);
	it.smag = f[6].to_int(0);
	it.bonus = f[7].to_int(0);
	if (it.base < 0 || it.base >= ITEMS.size()) { it.base = -1; }
	if (it.prefix >= AFFIXES.size()) { it.prefix = -1; }
	if (it.suffix >= AFFIXES.size()) { it.suffix = -1; }
	return it;
}

string enc_items(array<Item>& items) {
	string out = "";
	for (int i = 0; i < items.size(); ++i) {
		if (i > 0) { out += ","; }
		out += enc_item(items[i]);
	}
	return out;
}

array<Item> dec_items(string s) {
	array<Item> out;
	if (s == "") { return out; }
	array<string> parts = s.split(",");
	for (int i = 0; i < parts.size(); ++i) { out.push(dec_item(parts[i])); }
	return out;
}

string enc_ints(array<int>& v) {
	string out = "";
	for (int i = 0; i < v.size(); ++i) {
		if (i > 0) { out += ","; }
		out += to_string(v[i]);
	}
	return out;
}

array<int> dec_ints(string s) {
	array<int> out;
	if (s == "") { return out; }
	array<string> parts = s.split(",");
	for (int i = 0; i < parts.size(); ++i) { out.push(parts[i].to_int(0)); }
	return out;
}

// ---------------------------------------------------------------- host ---
bool event_for(int e, int slot, int w, int x, int y) {
	int to = EV_TO[e];
	if (to >= 0) { return to == slot; }
	if (EV_KIND[e] == Ev.chat) {
		// party chat reaches the party anywhere; open chat reaches the same realm
		if (EV_C[e] == 1) { return EV_B[e] >= 0 && CHARS[slot].party == EV_B[e]; }
		return EV_W[e] == w;
	}
	if (EV_W[e] < 0) { return true; }
	return EV_W[e] == w && cheb(EV_X[e], EV_Y[e], x, y) <= VIEW_R + 6;
}

string snapshot_for(int slot) {
	while (NET_SHEET_SENT.size() <= slot) { NET_SHEET_SENT.push(-1); }
	Character& c = CHARS[slot];
	int me = c.mob;
	return snapshot_view(slot, MOB_WORLD[me], MOB_X[me], MOB_Y[me]);
}

// what a camera at (w, x, y) sees; slot < 0 = a spectator (title screen) with no sheet or vitals
string snapshot_view(int slot, int w, int x, int y) {
	int me = slot >= 0 ? CHARS[slot].mob : -1;
	string out = `S|${TICK}|${w}|${me >= 0 ? MOB_SER[me] : -1}|${W_WEATHER[w]}|${time_of_day()}|${day_number()}\n`;
	if (me >= 0) {
		Character& c = CHARS[slot];
		int target = MOB_TARGET[me];
		int tser = mob_live(target) ? MOB_SER[target] : -1;
		int left = c.action != 0 ? imax(0, c.action_t - TICK) : 0;
		out += `Y|${MOB_HP[me]}|${MOB_HPMAX[me]}|${MOB_MP[me]}|${MOB_MPMAX[me]}|${tser}|${c.action}|${left}|${mob_ghost(me) ? 1 : 0}\n`;
	}
	int ww = W_W[w];
	int wh = W_H[w];
	int x0 = imax(0, x - VIEW_R);
	int x1 = imin(ww - 1, x + VIEW_R);
	int y0 = imax(0, y - VIEW_R);
	int y1 = imin(wh - 1, y + VIEW_R);
	for (int ty = y0; ty <= y1; ++ty) {
		int row = W_OFF[w] + ty * ww;
		for (int tx = x0; tx <= x1; ++tx) {
			int m = OCC[row + tx] - 1;
			if (m < 0) { continue; }
			out += mob_record(m, slot);
		}
	}
	// ghosts do not occupy tiles; players who died still show
	for (int s = 0; s < CHARS.size(); ++s) {
		int pm = CHARS[s].mob;
		if (s == slot || !CHARS[s].active || !mob_live(pm) || !mob_ghost(pm) || MOB_WORLD[pm] != w) { continue; }
		if (cheb(MOB_X[pm], MOB_Y[pm], x, y) <= VIEW_R) { out += mob_record(pm, slot); }
	}
	if (me >= 0 && mob_ghost(me)) { out += mob_record(me, slot); }
	if (slot < 0) {
		for (int i = 0; i < GI_ITEM.size(); ++i) {
			if (GI_ITEM[i].base >= 0 && GI_W[i] == w && cheb(GI_X[i], GI_Y[i], x, y) <= VIEW_R) {
				out += `I|${GI_SER[i]}|${GI_ITEM[i].base}|${GI_ITEM[i].rarity}|${GI_X[i]}|${GI_Y[i]}|${GI_ITEM[i].qty}\n`;
			}
		}
		return out;
	}
	Character& c = CHARS[slot];
	for (int i = 0; i < GI_ITEM.size(); ++i) {
		if (GI_ITEM[i].base < 0 || GI_W[i] != w || cheb(GI_X[i], GI_Y[i], x, y) > VIEW_R) { continue; }
		Item it = GI_ITEM[i];
		out += `I|${GI_SER[i]}|${it.base}|${it.rarity}|${GI_X[i]}|${GI_Y[i]}|${it.qty}\n`;
	}
	if (TICK % 10 == 0 || c.version != NET_SHEET_SENT[slot]) {
		out += "TC\n";
		for (int ty = y0; ty <= y1; ++ty) {
			for (int tx = x0; tx <= x1; ++tx) {
				int i = W_OFF[w] + ty * ww + tx;
				if (REGROW[i] > TICK) { out += `T|${ty * ww + tx}|${tile_now(w, tx, ty)}\n`; }
			}
		}
	}
	for (int e = 0; e < EV_KIND.size(); ++e) {
		if (!event_for(e, slot, w, x, y)) { continue; }
		out += `E|${EV_KIND[e]}|${EV_W[e]}|${EV_X[e]}|${EV_Y[e]}|${EV_A[e]}|${EV_B[e]}|${EV_C[e]}|${clean_text(EV_TEXT[e])}\n`;
	}
	if (c.version != NET_SHEET_SENT[slot]) {
		NET_SHEET_SENT[slot] = c.version;
		out += sheet_record(slot);
	}
	return out;
}

string mob_record(int m, int viewer) {
	int hpp = MOB_HPMAX[m] > 0 ? MOB_HP[m] * 100 / MOB_HPMAX[m] : 0;
	int slot = MOB_PLAYER[m];
	string name = slot >= 0 ? clean_text(CHARS[slot].name) : "";
	int hue = slot >= 0 ? CHARS[slot].hue : 0;
	int flags = MOB_FLAGS[m];
	// hidden things are hidden (except from themselves)
	if ((flags & MF_HIDDEN) != 0 && slot != viewer) { return ""; }
	return `M|${MOB_SER[m]}|${MOB_KIND[m]}|${MOB_X[m]}|${MOB_Y[m]}|${MOB_PX[m]}|${MOB_PY[m]}|${MOB_MOVE_T[m]}|${MOB_MOVE_D[m]}|${MOB_DIR[m]}|${hpp}|${flags}|${MOB_ROLE[m]}|${slot}|${hue}|${name}\n`;
}

string sheet_record(int slot) {
	Character& c = CHARS[slot];
	string out = `C|${clean_text(c.name)}|${clean_text(player_title(slot))}|${c.hue}|${player_str(slot)}|${player_dex(slot)}|${player_int(slot)}`;
	out += `|${c.gold}|${c.bank_gold}|${c.fame}|${c.karma}|${player_armor(slot)}|${player_luck(slot)}|${c.pvp ? 1 : 0}|${c.party}`;
	out += `|${imax(0, c.buff_night - TICK)}|${imax(0, c.buff_fire - TICK)}|${imax(0, c.buff_regen - TICK)}|${c.version}\n`;
	out += "K|" + enc_ints(c.skill) + "\n";
	out += "P|" + enc_items(c.pack) + "\n";
	out += "W|" + enc_items(c.worn) + "\n";
	out += "B|" + enc_items(c.bank) + "\n";
	out += "L|" + enc_ints(c.spells) + "\n";
	array<string> j = journal_lines(slot);
	for (int i = 0; i < j.size(); ++i) { out += "J|" + clean_text(j[i]) + "\n"; }
	string party = "";
	for (int s = 0; s < CHARS.size(); ++s) {
		if (s != slot && CHARS[s].active && c.party >= 0 && CHARS[s].party == c.party) {
			int pm = CHARS[s].mob;
			int hpp = mob_live(pm) && MOB_HPMAX[pm] > 0 ? MOB_HP[pm] * 100 / MOB_HPMAX[pm] : 0;
			party += `${clean_text(CHARS[s].name)}:${hpp},`;
		}
	}
	out += "Q|" + party + "\n";
	return out;
}

// ---------------------------------------------------------------- client ---
void cv_log(ClientView& v, string text, int col) {
	v.log.push(text);
	v.log_col.push(col);
	if (v.log.size() > 200) {
		v.log.remove(0);
		v.log_col.remove(0);
	}
}

void cv_apply(ClientView& v, string snap) {
	array<string> lines = snap.lines();
	bool mobs_reset = false;
	for (int li = 0; li < lines.size(); ++li) {
		string line = lines[li];
		if (line.size() < 1) { continue; }
		array<string> f = line.split("|");
		string k = f[0];
		if (k == "S" && f.size() >= 7) {
			v.tick = f[1].to_int(0);
			int nw = f[2].to_int(0);
			if (nw != v.world) { v.tover = {}; }
			v.world = nw;
			v.my_ser = f[3].to_int(-1);
			v.weather = f[4].to_int(0);
			v.tod = f[5].to_int(0);
			v.day = f[6].to_int(1);
			v.snaps = v.snaps + 1;
			// mobiles and items are a full picture each snapshot
			v.m_ser = [];
			v.m_kind = [];
			v.m_x = [];
			v.m_y = [];
			v.m_px = [];
			v.m_py = [];
			v.m_mt = [];
			v.m_md = [];
			v.m_dir = [];
			v.m_hp = [];
			v.m_flags = [];
			v.m_role = [];
			v.m_slot = [];
			v.m_hue = [];
			v.m_name = [];
			v.i_ser = [];
			v.i_base = [];
			v.i_rar = [];
			v.i_x = [];
			v.i_y = [];
			v.i_qty = [];
			mobs_reset = true;
		} else if (k == "Y" && f.size() >= 9) {
			v.hp = f[1].to_int(0);
			v.hpmax = imax(1, f[2].to_int(1));
			v.mp = f[3].to_int(0);
			v.mpmax = imax(1, f[4].to_int(1));
			v.target_ser = f[5].to_int(-1);
			v.action = f[6].to_int(0);
			v.action_left = f[7].to_int(0);
			v.ghost = f[8].to_int(0);
		} else if (k == "M" && f.size() >= 16) {
			v.m_ser.push(f[1].to_int(0));
			v.m_kind.push(iclamp(f[2].to_int(0), 0, MONS.size() - 1));
			v.m_x.push(f[3].to_int(0));
			v.m_y.push(f[4].to_int(0));
			v.m_px.push(f[5].to_int(0));
			v.m_py.push(f[6].to_int(0));
			v.m_mt.push(f[7].to_int(0));
			v.m_md.push(imax(1, f[8].to_int(1)));
			v.m_dir.push(f[9].to_int(4));
			v.m_hp.push(f[10].to_int(100));
			v.m_flags.push(f[11].to_int(0));
			v.m_role.push(f[12].to_int(-1));
			v.m_slot.push(f[13].to_int(-1));
			v.m_hue.push(f[14].to_int(0));
			v.m_name.push(f[15]);
		} else if (k == "I" && f.size() >= 7) {
			int base = f[2].to_int(-1);
			if (base < 0 || base >= ITEMS.size()) { continue; }
			v.i_ser.push(f[1].to_int(0));
			v.i_base.push(base);
			v.i_rar.push(f[3].to_int(0));
			v.i_x.push(f[4].to_int(0));
			v.i_y.push(f[5].to_int(0));
			v.i_qty.push(f[6].to_int(1));
		} else if (k == "TC") {
			v.tover = {};
		} else if (k == "T" && f.size() >= 3) {
			v.tover[f[1].to_int(0)] = f[2].to_int(0);
		} else if (k == "E" && f.size() >= 9) {
			int kind = f[1].to_int(0);
			string text = f[8];
			for (int extra = 9; extra < f.size(); ++extra) { text = text + "/" + f[extra]; }
			if (kind == Ev.log) { cv_log(v, text, f[5].to_int(0)); }
			else if (kind == Ev.chat) { cv_log(v, text, f[7].to_int(0) == 1 ? 5 : 0); }
			v.e_kind.push(kind);
			v.e_w.push(f[2].to_int(0));
			v.e_x.push(f[3].to_int(0));
			v.e_y.push(f[4].to_int(0));
			v.e_a.push(f[5].to_int(0));
			v.e_b.push(f[6].to_int(0));
			v.e_c.push(f[7].to_int(0));
			v.e_text.push(text);
			if (kind == Ev.dialog) { v.dlg_ser = f[5].to_int(-1); v.dlg_role = f[6].to_int(-1); v.dlg_pending = true; }
		} else if (k == "C" && f.size() >= 19) {
			v.name = f[1];
			v.title = f[2];
			v.hue = f[3].to_int(0);
			v.str = f[4].to_int(0);
			v.dex = f[5].to_int(0);
			v.intel = f[6].to_int(0);
			v.gold = f[7].to_int(0);
			v.bank_gold = f[8].to_int(0);
			v.fame = f[9].to_int(0);
			v.karma = f[10].to_int(0);
			v.armor = f[11].to_int(0);
			v.luck = f[12].to_int(0);
			v.pvp = f[13] == "1";
			v.party = f[14].to_int(-1);
			v.buff_night = f[15].to_int(0);
			v.buff_fire = f[16].to_int(0);
			v.buff_regen = f[17].to_int(0);
			v.sheet_version = f[18].to_int(0);
			v.journal = [];
		} else if (k == "K") {
			v.skills = dec_ints(f.size() > 1 ? f[1] : "");
		} else if (k == "P") {
			v.pack = dec_items(f.size() > 1 ? f[1] : "");
		} else if (k == "W") {
			v.worn = dec_items(f.size() > 1 ? f[1] : "");
		} else if (k == "B") {
			v.bank = dec_items(f.size() > 1 ? f[1] : "");
		} else if (k == "L") {
			v.spells = dec_ints(f.size() > 1 ? f[1] : "");
		} else if (k == "J" && f.size() > 1) {
			v.journal.push(f[1]);
		} else if (k == "Q") {
			v.party_names = [];
			v.party_hp = [];
			array<string> ps = f.size() > 1 ? f[1].split(",") : [];
			for (int i = 0; i < ps.size(); ++i) {
				if (ps[i] == "") { continue; }
				array<string> nh = ps[i].split(":");
				v.party_names.push(nh[0]);
				v.party_hp.push(nh.size() > 1 ? nh[1].to_int(0) : 0);
			}
		}
	}
}

// the index of a mobile in the view by serial (-1 = not visible)
int cv_find(ClientView& v, int ser) {
	for (int i = 0; i < v.m_ser.size(); ++i) {
		if (v.m_ser[i] == ser) { return i; }
	}
	return -1;
}

int cv_me(ClientView& v) { return cv_find(v, v.my_ser); }

// terrain as this client knows it
int cv_tile(ClientView& v, int x, int y) {
	int w = v.world;
	if (x < 0 || y < 0 || x >= W_W[w] || y >= W_H[w]) { return tile_at(w, x, y); }
	int i = y * W_W[w] + x;
	if (v.tover.size() > 0 && v.tover.has(i)) { return v.tover[i]; }
	return TILES[W_OFF[w] + i];
}
