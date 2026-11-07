// Data: weapons, bestiary, pickup tables, and every sprite as pixel art.
// Art format: rows of chars, '.' = transparent, everything else indexes the
// art's own tiny rgb palette (one char per entry of `keys`, three ints each in
// `rgb`). parse_arts() flattens each into code arrays at boot; the renderer
// maps codes -> palette indices via per-(art, shade, flash) LUTs.

// ---------------------------------------------------------------- weapons ---
// kind: 0 hitscan, 1 projectile
class WeaponDef {
	string name;
	int ammo;
	int cd;
	int pellets;
	double spread;
	int lo;
	int hi;
	int kind;

	WeaponDef(const string &in name_, int ammo_, int cd_, int pellets_, double spread_, int lo_, int hi_, int kind_) {
		name = name_;
		ammo = ammo_;
		cd = cd_;
		pellets = pellets_;
		spread = spread_;
		lo = lo_;
		hi = hi_;
		kind = kind_;
	}
}

array<WeaponDef@> WEAPONS = {
	WeaponDef("PISTOL",     0, 11, 1, (12.0 / 1000.0), 6,  12, 0),
	WeaponDef("SCATTERGUN", 1, 28, 6, (11.0 / 100.0),  3,  7,  0),
	WeaponDef("HEXCASTER",  2, 22, 1, 0.0,   24, 40, 1)
};
array<string> AMMO_NAMES = {"BULLET", "SHELL", "CELL"};
array<int> AMMO_MAX = {200, 50, 120};

// ---------------------------------------------------------------- bestiary --
// kind indexes: 0 grunt (melee rusher), 1 spitter (ranged), 2 turret, 3 warden (boss)
class EnemyDef {
	string name;
	int hp;
	double speed;
	double radius;
	int pain;
	double size;
	int melee_lo;
	int melee_hi;

	EnemyDef(const string &in name_, int hp_, double speed_, double radius_, int pain_, double size_, int melee_lo_, int melee_hi_) {
		name = name_;
		hp = hp_;
		speed = speed_;
		radius = radius_;
		pain = pain_;
		size = size_;
		melee_lo = melee_lo_;
		melee_hi = melee_hi_;
	}
}

array<EnemyDef@> ENEMY_DEFS = {
	EnemyDef("GRUNT",   30,  (31.0 / 10.0), (34.0 / 100.0), 55, (72.0 / 100.0), 4,  10),
	EnemyDef("SPITTER", 45,  (22.0 / 10.0), (36.0 / 100.0), 40, (78.0 / 100.0), 3,  7),
	EnemyDef("TURRET",  60,  0.0, (38.0 / 100.0), 15, (6.0 / 10.0), 0,  0),
	EnemyDef("WARDEN",  520, 2.0, (45.0 / 100.0), 8,  (105.0 / 100.0), 10, 22)
};

// ---------------------------------------------------------------- pickups ---
// kind: 0 stim 1 medkit 2 armor 3 clip 4 shells 5 cells 6 redkey 7 bluekey
//       8 scattergun 9 hexcaster 10 soul orb
class ItemDef {
	string name;
	string art;
	double size;

	ItemDef(const string &in name_, const string &in art_, double size_) {
		name = name_;
		art = art_;
		size = size_;
	}
}

array<ItemDef@> ITEM_DEFS = {
	ItemDef("stimpack",     "it_stim",   (22.0 / 100.0)),
	ItemDef("medikit",      "it_medkit", (28.0 / 100.0)),
	ItemDef("flak vest",    "it_armor",  (3.0 / 10.0)),
	ItemDef("bullet clip",  "it_clip",   (2.0 / 10.0)),
	ItemDef("shell box",    "it_shell",  (22.0 / 100.0)),
	ItemDef("hex cells",    "it_cell",   (24.0 / 100.0)),
	ItemDef("RED keycard",  "it_rkey",   (22.0 / 100.0)),
	ItemDef("BLUE keycard", "it_bkey",   (22.0 / 100.0)),
	ItemDef("SCATTERGUN",   "it_sgun",   (26.0 / 100.0)),
	ItemDef("HEXCASTER",    "it_hgun",   (28.0 / 100.0)),
	ItemDef("soul orb",     "it_orb",    (3.0 / 10.0))
};

// a placed pickup in the world (per-frame billboard/automap/pickup scans)
class Item {
	int kind;
	double x;
	double y;
	bool taken = false;

	Item(int kind_, double x_, double y_) {
		kind = kind_;
		x = x_;
		y = y_;
	}
}

// ------------------------------------------------------------- sprite art ---
// Each entry: rows + a tiny palette. Kept chunky on purpose: at half-block
// resolution a 14-wide sprite fills a third of the screen point-blank.
class ArtSrc {
	string keys;
	array<int> rgb;
	array<string> rows;
}

// parsed at boot: w x h codes (0 transparent) + rgb triples per code
class Art {
	int w;
	int h;
	array<int> pix;
	array<int> rgb;
}

dictionary ART_SRC;
dictionary ARTS;

void add_art(const string &in name, const string &in keys, const array<int> &in rgb, const array<string> &in rows) {
	ArtSrc src;
	src.keys = keys;
	src.rgb = rgb;
	src.rows = rows;
	@ART_SRC[name] = src;
}

void define_arts() {
	// GRUNT - a hunched green rusher, all shoulders and appetite
	add_art("grunt_w0", "hdEmcl", {86, 158, 62,  52, 104, 40,  255, 60, 40,  240, 226, 190,  220, 210, 170,  46, 70, 36}, {
		"....hhhhh.....",
		"...hhhhhhh....",
		"..hhEhhhEhh...",
		"..hhhhhhhhh...",
		"...hmmmmmh....",
		"...hcmcmch....",
		"dhhhhhhhhhhd..",
		"dhhhhhhhhhhd..",
		"chdhhhhhhdhc..",
		"..dhhhhhhd....",
		"..dhh..hhd....",
		"..lhh..hhl....",
		"..ll....ll....",
		".lll....lll..."});
	add_art("grunt_w1", "hdEmcl", {86, 158, 62,  52, 104, 40,  255, 60, 40,  240, 226, 190,  220, 210, 170,  46, 70, 36}, {
		"....hhhhh.....",
		"...hhhhhhh....",
		"..hhEhhhEhh...",
		"..hhhhhhhhh...",
		"...hmmmmmh....",
		"...hcmcmch....",
		".dhhhhhhhhhd..",
		".dhhhhhhhhhd..",
		".chdhhhhhdhc..",
		"...dhhhhhd....",
		"...dhh.hhd....",
		"...lhh..hhl...",
		"..ll......ll..",
		"..lll....lll.."});
	add_art("grunt_at", "hdEmcl", {96, 170, 66,  52, 104, 40,  255, 220, 60,  180, 30, 24,  235, 226, 190,  46, 70, 36}, {
		"....hhhhh.....",
		"...hhhhhhh....",
		"..hhEhhhEhh...",
		"..hhhhhhhhh...",
		"...hmmmmmh....",
		"...cmmmmmc....",
		"chhhhhhhhhhhc.",
		".chhhhhhhhhc..",
		".hdhhhhhhdh...",
		"..dhhhhhhd....",
		"..dhh..hhd....",
		"..lhh..hhl....",
		"..ll....ll....",
		".lll....lll..."});
	add_art("grunt_d0", "hdErl", {86, 148, 62,  52, 96, 40,  90, 30, 24,  190, 40, 30,  46, 70, 36}, {
		"..............",
		"....hhhhh.....",
		"...hhhhhhh....",
		"..hhEhhhEhh...",
		"..rhhhhhhhr...",
		"...rhhhhhr....",
		".dhhhhhhhhhd..",
		".rdhhhhhhhdr..",
		"..dhhhhhhd....",
		"..rhhhhhhr....",
		"...hh..hh.....",
		"...lh..hl.....",
		"..............",
		".............."});
	add_art("grunt_d1", "hdrb", {74, 120, 56,  48, 82, 38,  180, 38, 28,  120, 26, 20}, {
		"..............",
		"..............",
		"..............",
		"....hhhh......",
		"...hhhhhhr....",
		"..rhhhhhhhr...",
		".rdhhhhhhdr...",
		"..brhhhhrb....",
		"...rbhhbr.....",
		"....rbbr......",
		"..............",
		"..............",
		"..............",
		".............."});
	add_art("grunt_dead", "hrb", {64, 100, 50,  150, 34, 26,  96, 22, 18}, {
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"....rbbr......",
		"..rbhhhhbr....",
		".rbhhhhhhbr...",
		"..brrbbrrb....",
		"..............",
		".............."});

	// SPITTER - stilt-legged violet thing with a glowing green gullet
	add_art("spit_w0", "vdEgl", {148, 74, 190,  96, 46, 128,  255, 210, 70,  110, 250, 90,  70, 40, 90}, {
		"....vvvvvv....",
		"...vvvvvvvv...",
		"..vvEvvvvEvv..",
		"..vvvvvvvvvv..",
		"...vvggggvv...",
		"...vvvggvvv...",
		"..dvvvvvvvvd..",
		"..dvvvvvvvvd..",
		"...dvvvvvvd...",
		"....l.vv.l....",
		"...l..vv..l...",
		"...l..ll..l...",
		"..l...ll...l..",
		"..l...ll...l.."});
	add_art("spit_w1", "vdEgl", {148, 74, 190,  96, 46, 128,  255, 210, 70,  110, 250, 90,  70, 40, 90}, {
		"....vvvvvv....",
		"...vvvvvvvv...",
		"..vvEvvvvEvv..",
		"..vvvvvvvvvv..",
		"...vvggggvv...",
		"...vvvggvvv...",
		"..dvvvvvvvvd..",
		"..dvvvvvvvvd..",
		"...dvvvvvvd...",
		"....l.vv.l....",
		"....l.vv.l....",
		"...l..ll...l..",
		"...l..ll...l..",
		"..l...ll....l."});
	add_art("spit_at", "vdEgGl", {162, 84, 205,  96, 46, 128,  255, 240, 120,  160, 255, 120,  230, 255, 190,  70, 40, 90}, {
		"....vvvvvv....",
		"...vvvvvvvv...",
		"..vvEvvvvEvv..",
		"..vvggggggvv..",
		"..vggGGGGggv..",
		"..vvgGGGGgvv..",
		".dvvvggggvvvd.",
		"..dvvvvvvvvd..",
		"...dvvvvvvd...",
		"....l.vv.l....",
		"...l..vv..l...",
		"...l..ll..l...",
		"..l...ll...l..",
		"..l...ll...l.."});
	add_art("spit_d0", "vdgrl", {140, 70, 178,  92, 44, 120,  110, 220, 90,  186, 44, 34,  70, 40, 90}, {
		"..............",
		"....vvvvvv....",
		"...vvvvvvvv...",
		"..vvrvvvvrvv..",
		"...vvggggvv...",
		"..rvvvvvvvvr..",
		"..dvvvvvvvvd..",
		"...dvvvvvvd...",
		"....rvvvvr....",
		".....l..l.....",
		"....l....l....",
		"..............",
		"..............",
		".............."});
	add_art("spit_d1", "vgrb", {118, 60, 150,  96, 190, 80,  176, 40, 30,  110, 26, 22}, {
		"..............",
		"..............",
		"..............",
		"....vvvvv.....",
		"...vvvvvvvr...",
		"..rvvggvvvr...",
		"...rvvvvvr....",
		"....rbvbr.....",
		".....rbr......",
		"..............",
		"..............",
		"..............",
		"..............",
		".............."});
	add_art("spit_dead", "vgbr", {96, 50, 122,  80, 150, 70,  96, 24, 20,  150, 34, 26}, {
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"....rbbvr.....",
		"..rvvbgbvvr...",
		"..brvvvvvrb...",
		"...bbrrbb.....",
		"..............",
		".............."});

	// TURRET - a wall-fed gun pod; wakes up mean
	add_art("tur_idle", "sdkr", {130, 134, 142,  78, 82, 92,  52, 54, 62,  120, 30, 30}, {
		"..............",
		"....ssssss....",
		"...sddddddss..",
		"..sdkkkkkkds..",
		"..sdkkrrkkds..",
		"..sdkkkkkkds..",
		"...sddddddss..",
		"....ssssss....",
		".....kddk.....",
		".....kddk.....",
		"....kkddkk....",
		"...kkkkkkkk...",
		"..............",
		".............."});
	add_art("tur_fire", "sdkry", {150, 154, 162,  88, 92, 102,  52, 54, 62,  255, 80, 50,  255, 230, 120}, {
		"..............",
		"....ssssss....",
		"...sddddddss..",
		"..sdkkkkkkds..",
		"..sdkryyrkds..",
		"..sdkkkkkkds..",
		"...sddddddss..",
		"....ssssss....",
		".....kddk.....",
		".....kddk.....",
		"....kkddkk....",
		"...kkkkkkkk...",
		"..............",
		".............."});
	add_art("tur_d0", "sdkyr", {120, 122, 130,  72, 74, 84,  48, 50, 58,  255, 200, 90,  230, 90, 40}, {
		"..............",
		"..y.ssssss.y..",
		"...sdyddydss..",
		"..sdkkrrkkds..",
		"..sdryyyyrds..",
		"..sdkkrrkkds..",
		"...sdyddydss..",
		"..y.ssssss.y..",
		".....kddk.....",
		".....kddk.....",
		"....kkddkk....",
		"...kkkkkkkk...",
		"..............",
		".............."});
	add_art("tur_d1", "skyr", {104, 106, 114,  46, 48, 56,  220, 160, 70,  180, 70, 34}, {
		"..............",
		"..............",
		"..............",
		"...r.ssss.r...",
		"....skkyks....",
		"...skyrrks....",
		"....skkkks....",
		".....ssss.....",
		".....kkkk.....",
		"....kkkkkk....",
		"...kkkkkkkk...",
		"..............",
		"..............",
		".............."});
	add_art("tur_dead", "skr", {88, 90, 98,  42, 44, 52,  120, 50, 30}, {
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"..............",
		"......ss......",
		"....skkrks....",
		"...skkkkkks...",
		"...kkkkkkkk...",
		"..kkkkkkkkkk..",
		"..............",
		".............."});

	// WARDEN - the episode's landlord. Big, horned, unhappy.
	add_art("war_w0", "rdkEhf", {190, 44, 36,  126, 30, 26,  64, 20, 18,  255, 230, 90,  230, 220, 200,  255, 140, 40}, {
		"..h..........h..",
		".hh...rrrr...hh.",
		".h..rrrrrrrr..h.",
		".h.rrErrrrErr.h.",
		"...rrrrrrrrrr...",
		"....rffffffr....",
		"....rrffffrr....",
		"..drrrrrrrrrrd..",
		".ddrrrrrrrrrrdd.",
		".kdrrrrrrrrrrdk.",
		".kddrrrrrrrrddk.",
		"..kdrrrrrrrrdk..",
		"...ddrr..rrdd...",
		"...kdrr..rrdk...",
		"...kdd....ddk...",
		"..kkd......dkk..",
		"..kkk......kkk..",
		".kkk........kkk."});
	add_art("war_w1", "rdkEhf", {190, 44, 36,  126, 30, 26,  64, 20, 18,  255, 230, 90,  230, 220, 200,  255, 140, 40}, {
		"..h..........h..",
		".hh...rrrr...hh.",
		".h..rrrrrrrr..h.",
		".h.rrErrrrErr.h.",
		"...rrrrrrrrrr...",
		"....rffffffr....",
		"....rrffffrr....",
		"..drrrrrrrrrrd..",
		".ddrrrrrrrrrrdd.",
		".kdrrrrrrrrrrdk.",
		".kddrrrrrrrrddk.",
		"..kdrrrrrrrrdk..",
		"...ddrr..rrdd...",
		"...kdrr..rrdk...",
		"...dd......kk...",
		"..kdd......dkk..",
		"..kk........kk..",
		".kkk........kkkk"});
	add_art("war_at", "rdkEhfF", {210, 50, 40,  126, 30, 26,  64, 20, 18,  255, 255, 160,  235, 226, 205,  255, 200, 80,  255, 250, 200}, {
		"..h..........h..",
		".hh...rrrr...hh.",
		".h..rrrrrrrr..h.",
		".h.rrErrrrErr.h.",
		"...rrrrrrrrrr...",
		"....rffFFffr....",
		"....rfFFFFfr....",
		"..drrrfFFfrrrd..",
		".ddrrrrffrrrrdd.",
		".kdrrrrrrrrrrdk.",
		".kddrrrrrrrrddk.",
		"..kdrrrrrrrrdk..",
		"...ddrr..rrdd...",
		"...kdrr..rrdk...",
		"...kdd....ddk...",
		"..kkd......dkk..",
		"..kkk......kkk..",
		".kkk........kkk."});
	add_art("war_d0", "rdkhb", {176, 42, 34,  118, 28, 24,  60, 18, 16,  215, 206, 188,  255, 90, 40}, {
		"................",
		"..h..........h..",
		".hh...rrrr...hh.",
		".h..rrbrrbrr..h.",
		"...rrrrrrrrrr...",
		"....rbbbbbbr....",
		"..drrrrrrrrrrd..",
		".bdrrrrrrrrrrdb.",
		".kdrrrbbrrrrrdk.",
		"..kddrrrrrrddk..",
		"...kdrrrrrrdk...",
		"...ddbr..rbdd...",
		"....dd....dd....",
		"....k......k....",
		"................",
		"................",
		"................",
		"................"});
	add_art("war_d1", "rdkby", {150, 36, 30,  104, 26, 22,  54, 16, 14,  230, 80, 36,  255, 170, 70}, {
		"................",
		"................",
		"................",
		"................",
		"...y..rrrr..y...",
		"..brrrrrrrrrb...",
		"..rbrybbyrbrr...",
		".drrrrbbrrrrrd..",
		".kdrrrrrrrrrdk..",
		"..kdbrrrrrbdk...",
		"...kddrrrddk....",
		"....kbd..dbk....",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................"});
	add_art("war_dead", "rkbh", {110, 28, 24,  50, 14, 12,  150, 50, 26,  170, 160, 145}, {
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"................",
		"..h..........h..",
		"...bkrrrrrrkb...",
		"..brrkkkkkkrrb..",
		".bkrrrrrrrrrrkb.",
		"..bbkkrrrrkkbb..",
		"................",
		"................"});

	// pickups (small props; feet on the floor line)
	add_art("it_stim", "wbr", {230, 235, 240,  70, 120, 230,  220, 60, 50}, {
		"..bb..",
		".wwww.",
		".wrrw.",
		".wrrw.",
		".wwww."});
	add_art("it_medkit", "wrd", {235, 238, 240,  225, 50, 40,  150, 152, 158}, {
		"wwwwwwww",
		"wwwrrwww",
		"wrrrrrrw",
		"wwwrrwww",
		"dwwwwwwd"});
	add_art("it_armor", "gdk", {70, 190, 90,  40, 110, 55,  26, 60, 34}, {
		"gg...gg",
		"ggggggg",
		"gdddddg",
		"gddkddg",
		".ddddd.",
		".dd.dd."});
	add_art("it_clip", "bdk", {200, 170, 70,  140, 116, 50,  90, 76, 36}, {
		".bbb.",
		".bbb.",
		"ddddd",
		"dkkkd",
		"ddddd"});
	add_art("it_shell", "ryk", {210, 70, 40,  235, 200, 90,  110, 50, 26}, {
		"ryryryr",
		"ryryryr",
		"kkkkkkk",
		"ryryryr",
		"kkkkkkk"});
	add_art("it_cell", "cdw", {90, 240, 220,  40, 140, 130,  220, 255, 250}, {
		".cddc.",
		"cwccdc",
		"cccwcc",
		"dccccd",
		".dccd."});
	add_art("it_rkey", "rw", {255, 70, 60,  255, 200, 195}, {
		"rrr..",
		"rwr..",
		"rrrrr",
		"..r.r"});
	add_art("it_bkey", "bw", {80, 130, 255,  200, 215, 255}, {
		"bbb..",
		"bwb..",
		"bbbbb",
		"..b.b"});
	add_art("it_sgun", "kws", {70, 62, 54,  150, 120, 80,  180, 184, 190}, {
		"ssssssssk.",
		"kssssssskk",
		"...wwwkkk.",
		"..www.....",
		".ww......."});
	add_art("it_hgun", "pdc", {180, 90, 240,  110, 50, 160,  120, 255, 140}, {
		".pppppp.c.",
		"ppcccppppc",
		"dppppppdd.",
		"..ddd.....",
		".dd......."});
	add_art("it_orb", "bwd", {110, 160, 255,  235, 245, 255,  60, 90, 190}, {
		".bbbb.",
		"bwwbbb",
		"bwbbbd",
		"bbbbdd",
		".bddd."});

	// projectiles
	add_art("pr_spit", "gw", {130, 255, 100,  220, 255, 190}, {
		".gg.",
		"gwwg",
		"gwwg",
		".gg."});
	add_art("pr_hex", "pwc", {200, 110, 255,  245, 225, 255,  130, 255, 150}, {
		".pp.",
		"pwcp",
		"pcwp",
		".pp."});
	add_art("pr_fire", "oyr", {255, 140, 40,  255, 230, 120,  230, 60, 30}, {
		".oo.",
		"oyyo",
		"ryyr",
		".rr."});
}

void parse_arts() {
	ART_SRC.deleteAll();
	ARTS.deleteAll();
	define_arts();
	array<string>@ names = ART_SRC.getKeys();
	for (uint n = 0; n < names.length(); ++n) {
		ArtSrc@ src = cast<ArtSrc>(ART_SRC[names[n]]);
		Art art;
		art.h = int(src.rows.length());
		art.w = len(src.rows[0]);
		art.rgb = {0, 0, 0};
		dictionary codes;   // char -> code
		int next_code = 1;
		for (int y = 0; y < art.h; ++y) {
			string row = src.rows[y];
			for (int x = 0; x < art.w; ++x) {
				string ch = row.substr(x, 1);
				if (ch == ".") {
					art.pix.insertLast(0);
					continue;
				}
				if (!codes.exists(ch)) {
					codes.set(ch, int64(next_code));
					next_code = next_code + 1;
					int k = src.keys.findFirst(ch) * 3;
					art.rgb.insertLast(src.rgb[k]);
					art.rgb.insertLast(src.rgb[k + 1]);
					art.rgb.insertLast(src.rgb[k + 2]);
				}
				int64 code;
				codes.get(ch, code);
				art.pix.insertLast(int(code));
			}
		}
		@ARTS[names[n]] = art;
	}
}

// ------------------------------------------------------- weapon HUD art -----
// 5 rows each, ASCII, drawn at the HUD's right edge with a walk-bob shift.
array<array<string>> WGFX_IDLE = {
	{
	"      ____   ",
	"  ___|__  |  ",
	" |__    | |  ",
	"    |___|_|  ",
	"      |_|    "},
	{
	" ________    ",
	" \\_______\\==.",
	"  |__ __--'| ",
	"    |_|----' ",
	"    |_|      "},
	{
	"   .-<*>-.   ",
	"  / _/|\\_ \\  ",
	" |=( (#) )=| ",
	"  \\__\\|/__/  ",
	"     |=|     "}
};
array<array<string>> WGFX_FIRE = {
	{
	" \\!/  ____   ",
	" -*-_|__  |  ",
	" |__    | |  ",
	"    |___|_|  ",
	"      |_|    "},
	{
	"\\\\!//____    ",
	"=*=______\\==.",
	"//|\\_ __--'| ",
	"    |_|----' ",
	"    |_|      "},
	{
	" *~.-<*>-.~* ",
	"  ~/ _|_ \\~  ",
	" |=( (@) )=| ",
	"  \\__\\|/__/  ",
	"     |=|     "}
};
