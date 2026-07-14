// JaiDOOM wad.jai — WAD file access in pure JaiScript.
// Bytes are language-native now (docs/char_semantics.md, both rounds landed
// off this project's feedback): WAD[ofs] + 0 reads the unsigned byte, char
// comparisons promote the same way, and to_char / \xNN literals mint bytes.
// The sort-bootstrap and base64-LUT eras are fully retired.

var WAD = "";                 // raw file bytes
var WAD_ID = "";              // "IWAD" | "PWAD"
var NUM_LUMPS = 0;
array<int> LUMP_POS = [];
array<int> LUMP_SIZE = [];
var LUMP_NAME = [];
var LUMP_INDEX = {};          // name -> index, later lumps win (W_GetNumForName scans backward)

function u8(int ofs) -> int { return WAD[ofs] + 0; }
function u16(int ofs) -> int { return (WAD[ofs] + 0) | (WAD[ofs + 1] << 8); }
function i16(int ofs) -> int {
	int v = (WAD[ofs] + 0) | (WAD[ofs + 1] << 8);
	return v >= 32768 ? v - 65536 : v;
}
function u32(int ofs) -> int {
	return (WAD[ofs] + 0) | (WAD[ofs + 1] << 8) | (WAD[ofs + 2] << 16) | (WAD[ofs + 3] << 24);
}
function i32(int ofs) -> int {
	int v = u32(ofs);
	return v >= 2147483648 ? v - 4294967296 : v;
}

// 8-byte lump name, NUL-padded
function name8(int ofs) -> string {
	int len = 0;
	while (len < 8 && WAD[ofs + len] != 0) { len = len + 1; }
	return WAD.substr(ofs, len);
}

function wad_load(string path) -> bool {
	WAD = read_file(path);
	if (WAD.size() < 12) { return false; }
	WAD_ID = WAD.substr(0, 4);
	if (WAD_ID != "IWAD" && WAD_ID != "PWAD") { return false; }
	NUM_LUMPS = i32(4);
	int dir = i32(8);
	LUMP_POS = [];
	LUMP_SIZE = [];
	LUMP_NAME = [];
	LUMP_INDEX = {};
	for (int i = 0; i < NUM_LUMPS; ++i) {
		int e = dir + i * 16;
		LUMP_POS.push(i32(e));
		LUMP_SIZE.push(i32(e + 4));
		var nm = name8(e + 8);
		LUMP_NAME.push(nm);
		LUMP_INDEX[nm] = i;
	}
	return true;
}

function lump_num(string name) -> int {
	if (LUMP_INDEX.has(name)) { return LUMP_INDEX[name]; }
	return -1;
}

// first lump named `name` at or after `start` (map sub-lumps live after their marker)
function lump_num_from(string name, int start) -> int {
	for (int i = start; i < NUM_LUMPS && i < start + 12; ++i) {
		if (LUMP_NAME[i] == name) { return i; }
	}
	return -1;
}

// whole lump decoded to a flat byte array (decode once, then hot code reads ints only)
function lump_bytes(int idx) -> array<int> {
	array<int> out = [];
	int pos = LUMP_POS[idx];
	int stop = pos + LUMP_SIZE[idx];
	var& w = WAD;
	for (int i = pos; i < stop; ++i) { out.push(w[i] + 0); }
	return out;
}
