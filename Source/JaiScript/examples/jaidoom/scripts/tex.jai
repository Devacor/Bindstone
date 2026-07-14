// JaiDOOM tex.jai — graphics lumps: PLAYPAL/COLORMAP, flats, patches,
// TEXTURE1 composites. Everything decodes lazily into flat arrays; PALCM
// prefolds colormap band x palette index -> interned terminal color.

import "wad.jai";
import "vid.jai";

array<int> DPAL = [];         // doom palette idx -> interned vid idx (bank 0)
array<int> CMAP = [];         // 34 x 256 colormap bytes
array<int> PALCM = [];        // band*256+palidx -> interned vid idx (prefolded)

var TEX_NAMES = {};           // texture name -> id
var TEX_W = [];               // per id
var TEX_H = [];
var TEX_READY = [];           // 0 = not composited yet
var TEX_PIX = [];             // per id: array<int> w*h column-major (col*h+row)
var TEX_DEFOFS = [];          // per id: offset of the texture def in TEXTURE1 data
var PNAME_LUMP = [];          // patch index -> lump index

var FLAT_CACHE = {};          // flat name -> array<int> 4096
var SKY_TEX = -1;

var CUR_BANK = -1;

var TEX_WORKERS = 1;          // 0 = serial map-start texture warm; nonzero = parallel_transform

// PLAYPAL bank switch (pain reds 1-8, pickup golds 9-12, radsuit 13):
// re-intern the 256 colors and refold PALCM — only on actual change
function set_palette_bank(int b) -> void {
	if (b == CUR_BANK) { return; }
	CUR_BANK = b;
	int pp = LUMP_POS[lump_num("PLAYPAL")] + b * 768;
	DPAL = [];
	for (int i = 0; i < 256; ++i) {
		DPAL.push(rgb_idx(u8(pp + i * 3), u8(pp + i * 3 + 1), u8(pp + i * 3 + 2)));
	}
	int total = 34 * 256;
	PALCM = [];
	for (int k = 0; k < total; ++k) { PALCM.push(DPAL[CMAP[k]]); }
}

function gfx_init() -> void {
	int cm = LUMP_POS[lump_num("COLORMAP")];
	CMAP = [];
	int total = 34 * 256;
	var& w = WAD;
	for (int c = 0; c < total; ++c) { CMAP.push(w[cm + c] + 0); }
	CUR_BANK = -1;
	set_palette_bank(0);

	// PNAMES: count, then 8-byte names -> lump numbers
	int pn = LUMP_POS[lump_num("PNAMES")];
	int npatch = i32(pn);
	PNAME_LUMP = [];
	for (int p = 0; p < npatch; ++p) {
		var nm = name8(pn + 4 + p * 8).to_upper();
		PNAME_LUMP.push(lump_num(nm));
	}

	// TEXTURE1 directory: count, offsets, defs (name8, masked, w, h, colddir, npatches, patches...)
	int t1 = LUMP_POS[lump_num("TEXTURE1")];
	int ntex = i32(t1);
	TEX_NAMES = {};
	TEX_W = []; TEX_H = []; TEX_READY = []; TEX_PIX = []; TEX_DEFOFS = [];
	for (int t = 0; t < ntex; ++t) {
		int dof = t1 + i32(t1 + 4 + t * 4);
		var tname = name8(dof);
		TEX_NAMES[tname] = t;
		TEX_W.push(i16(dof + 12));
		TEX_H.push(i16(dof + 14));
		TEX_READY.push(0);
		TEX_PIX.push([]);
		TEX_DEFOFS.push(dof);
	}
	SKY_TEX = tex_id("SKY1");
	FLAT_CACHE = {};
}

function tex_id(string name) -> int {
	if (TEX_NAMES.has(name)) { return TEX_NAMES[name]; }
	return -1;
}

// composite ONE texture def -> w*h column-major grid. Written as a pure
// parallel_transform body (the map-start warm pass runs it over every wall
// texture the map references): captured globals touched by subscript reads
// only, all state local, no var& aliases of captured names. The SAME
// function serves the lazy serial tex_prepare.
function tex_composite(int id) -> array<int> {
	int texw = TEX_W[id];
	int texh = TEX_H[id];
	array<int> pix = [];
	int n = texw * texh;
	for (int i = 0; i < n; ++i) { pix.push(0); }
	int dof = TEX_DEFOFS[id];
	int npatch = i16(dof + 20);
	int known = PNAME_LUMP.size();
	for (int p = 0; p < npatch; ++p) {
		int pref = dof + 22 + p * 10;
		int ox = i16(pref);
		int oy = i16(pref + 2);
		int pidx = i16(pref + 4);
		if (pidx < 0 || pidx >= known) { continue; }
		int lumpidx = PNAME_LUMP[pidx];
		if (lumpidx < 0) { continue; }
		int pofs = LUMP_POS[lumpidx];
		int pw = i16(pofs);
		for (int col = 0; col < pw; ++col) {
			int dx = ox + col;
			if (dx < 0 || dx >= texw) { continue; }
			int colbase = dx * texh;
			int post = pofs + u32(pofs + 8 + col * 4);
			while (WAD[post] != 255) {
				int topdelta = WAD[post] + 0;
				int len = WAD[post + 1] + 0;
				int src = post + 3;
				for (int i2 = 0; i2 < len; ++i2) {
					int dy = oy + topdelta + i2;
					if (dy >= 0 && dy < texh) { pix[colbase + dy] = WAD[src + i2] + 0; }
				}
				post = post + len + 4;
			}
		}
	}
	return pix;
}

function tex_prepare(int id) -> void {
	if (id < 0 || TEX_READY[id] != 0) { return; }
	TEX_PIX[id] = tex_composite(id);
	TEX_READY[id] = 1;
}

// masked variant for two-sided mid textures (grates/meshes): -1 where no
// post covered a texel, so holes stay transparent like sprites
var MTEX_PIX = {};

function tex_masked(int id) -> void {
	if (MTEX_PIX.has(id)) { return; }
	int texw = TEX_W[id];
	int texh = TEX_H[id];
	array<int> pix = [];
	int n = texw * texh;
	for (int i = 0; i < n; ++i) { pix.push(-1); }
	int dof = TEX_DEFOFS[id];
	int npatch = i16(dof + 20);
	int known = PNAME_LUMP.size();
	for (int p = 0; p < npatch; ++p) {
		int pref = dof + 22 + p * 10;
		int ox = i16(pref);
		int oy = i16(pref + 2);
		int pidx = i16(pref + 4);
		if (pidx < 0 || pidx >= known) { continue; }
		int lumpidx = PNAME_LUMP[pidx];
		if (lumpidx < 0) { continue; }
		int pofs = LUMP_POS[lumpidx];
		int pw = i16(pofs);
		for (int col = 0; col < pw; ++col) {
			int dx = ox + col;
			if (dx < 0 || dx >= texw) { continue; }
			int colbase = dx * texh;
			int post = pofs + u32(pofs + 8 + col * 4);
			while (WAD[post] != 255) {
				int topdelta = WAD[post] + 0;
				int len = WAD[post + 1] + 0;
				int src = post + 3;
				for (int i2 = 0; i2 < len; ++i2) {
					int dy = oy + topdelta + i2;
					if (dy >= 0 && dy < texh) { pix[colbase + dy] = WAD[src + i2] + 0; }
				}
				post = post + len + 4;
			}
		}
	}
	MTEX_PIX[id] = pix;
}

function flat_pix(string name) -> array<int> {
	if (FLAT_CACHE.has(name)) { return FLAT_CACHE[name]; }
	int lu = lump_num(name);
	array<int> out = [];
	if (lu < 0 || LUMP_SIZE[lu] < 4096) {
		for (int i = 0; i < 4096; ++i) { out.push(96); }
	} else {
		int pos = LUMP_POS[lu];
		var& w = WAD;
		for (int i2 = 0; i2 < 4096; ++i2) { out.push(w[pos + i2] + 0); }
	}
	FLAT_CACHE[name] = out;
	return out;
}
