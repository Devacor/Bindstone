// Gator Quest - saves: the world on the host and each character, as plain text
// files the script owns. Filled in by the saves milestone.

import "gq_net.jai";

// where the game keeps its files: beside the scripts when writable, else the per-user data folder
string DATA_ROOT = "";

string data_root() {
	if (DATA_ROOT != "") { return DATA_ROOT; }
	string beside = parent_dir(current_script_dir());
	DATA_ROOT = writable_dir(beside + "/saves") ? beside : app_data_dir("GatorQuest");
	return DATA_ROOT;
}

string save_world_and_chars() { return "Saving arrives with the next milestone."; }
string load_world_and_chars() { return "Loading arrives with the next milestone."; }
