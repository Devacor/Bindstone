// Gator Quest - dev-mode hot reload. A changed script is re-run in place while
// the world, the players and the monsters keep going.
//
// How: a script cannot re-run a file at global scope, and include() inside a
// function scopes the file's definitions to that function. So each reload
// writes a stub: the file's text with its imports blanked out, every top-level
// "T NAME = expr;" rewritten to "::NAME = expr;" (constants and tables are
// re-evaluated into the existing globals), every top-level "T NAME;" blanked
// (state is kept), and one "::fn = fn;" line per top-level function at the
// end (the fresh definitions replace the global bindings). Classes reload by
// themselves: a class redefinition migrates live instances. Line numbers stay
// aligned with the source, so an error in the stub names the right line.
//
// The convention this relies on, for every reloadable file: a top-level
// declaration WITH an initializer is a constant; WITHOUT one it is state.
// Enums do not reload (gq_enums.jai), nor do the two state files.

import "gq_save.jai";

bool DEV_ON;
array<string> DEV_FILES;
map<string, string> DEV_SEEN;
int DEV_STUBS;
int DEV_RELOADS;
string DEV_LAST;

array<string> DEV_SKIP = ["gq_state.jai", "gq_cstate.jai", "gq_enums.jai", "gq_dev.jai", "gatorquest.jai"];

bool ident_char(int c) { return c >= 97 && c <= 122 || c >= 65 && c <= 90 || c >= 48 && c <= 57 || c == 95; }

// the identifier that ends just before position `at` (skipping spaces)
string ident_before(string s, int at) {
	int e = at - 1;
	while (e >= 0 && (s[e] == ' ' || s[e] == '\t')) { e = e - 1; }
	int b = e;
	while (b >= 0 && ident_char(s[b] + 0)) { b = b - 1; }
	return e >= b + 1 ? s.substr(b + 1, e - b) : "";
}

// the stub text for one source file; function names land in `fns`, counts in `stats` [functions, constants, state]
string reload_stub(string src, array<string>& fns, array<int>& stats) {
	array<string> lines = src.split("\n");
	string out = "";
	stats = [0, 0, 0];
	for (int i = 0; i < lines.size(); ++i) {
		string line = lines[i];
		if (line.ends_with("\r")) { line = line.substr(0, line.size() - 1); }
		string keep = line;
		bool top = line.size() > 0 && line[0] != ' ' && line[0] != '\t';
		if (top && !line.starts_with("//") && !line.starts_with("}") && !line.starts_with("]") && !line.starts_with(")")
			&& !line.starts_with("class ") && !line.starts_with("nocopy ") && !line.starts_with("enum ") && !line.starts_with("[[")) {
			if (line.starts_with("import ") || line.starts_with("#import ")) {
				keep = "";
			} else {
				int eq = line.find("=");
				int par = line.find("(");
				if (eq > 0 && (par < 0 || eq < par) && line[eq + 1] != '=') {
					string name = ident_before(line, eq);
					keep = "::" + name + " =" + line.substr(eq + 1, line.size() - eq - 1);
					stats[1] = stats[1] + 1;
				} else if (par > 0) {
					string name = ident_before(line, par);
					if (name != "" && line.substr(0, par).trim().contains(" ")) {
						fns.push(name);
						stats[0] = stats[0] + 1;
					}
				} else if (line.trim().ends_with(";")) {
					keep = "";
					stats[2] = stats[2] + 1;
				}
			}
		}
		out += keep + "\n";
	}
	for (int k = 0; k < fns.size(); ++k) { out += "::" + fns[k] + " = " + fns[k] + ";\n"; }
	return out;
}

// re-run one file in place; "" on success, else the error
string dev_reload_file(string path) {
	string src = read_file(path);
	if (src == "") { return "could not read " + path; }
	array<string> fns;
	array<int> stats;
	string stub = reload_stub(src, fns, stats);
	make_dirs(data_root() + "/cache");
	DEV_STUBS = DEV_STUBS + 1;
	string stub_path = data_root() + "/cache/.reload_" + to_string(DEV_STUBS) + "_" + file_name(path);
	if (!write_file(stub_path, stub)) { return "could not write " + stub_path; }
	string err = "";
	try {
		include(stub_path);
	} catch (e) {
		err = to_string(e);
	}
	delete_file(stub_path);
	string bite = stub_path.substr(0, stub_path.size() - 4) + ".jaibite";
	if (file_exists(bite)) { delete_file(bite); }
	if (err != "") {
		err.replace_all("script:", file_name(path) + ":");
		return err;
	}
	DEV_RELOADS = DEV_RELOADS + 1;
	DEV_LAST = `${file_name(path)}: ${stats[0]} functions, ${stats[1]} constants re-run, ${stats[2]} state kept`;
	return "";
}

// watch every reloadable script in a folder (or an explicit list)
void dev_watch_dir(string dir) {
	array<string> names = list_dir(dir);
	array<string> files;
	for (int i = 0; i < names.size(); ++i) {
		string n = names[i];
		if (!n.ends_with(".jai") || !n.starts_with("gq_") || DEV_SKIP.contains(n)) { continue; }
		files.push(dir + "/" + n);
	}
	dev_watch(files);
}

void dev_watch(array<string> files) {
	DEV_FILES = files;
	DEV_SEEN = {};
	for (int i = 0; i < files.size(); ++i) { DEV_SEEN[files[i]] = read_file(files[i]); }
}

// reload whatever changed since the last poll; one message per reloaded file
array<string> dev_poll() {
	array<string> msgs;
	for (int i = 0; i < DEV_FILES.size(); ++i) {
		string f = DEV_FILES[i];
		string now = read_file(f);
		if (now == "" || now == DEV_SEEN.get(f, "")) { continue; }
		DEV_SEEN[f] = now;
		string err = dev_reload_file(f);
		if (err == "") {
			msgs.push("Reloaded " + DEV_LAST);
			dev_after_reload(file_name(f));
		} else {
			msgs.push("Reload FAILED, old code kept: " + err.split("\n")[0] + (err.split("\n").size() > 1 ? " " + err.split("\n")[1].trim() : ""));
		}
	}
	return msgs;
}

// derived client state that a reloaded file feeds
void dev_after_reload(string name) {
	if (name == "gq_art.jai" || name == "gq_vid.jai" || name == "gq_data.jai") {
		TS = 0;
		SHADE_WORLD = -1;
		if (VW > 0) { layout(SCREEN_COLS, SCREEN_ROWS); }
	}
}
