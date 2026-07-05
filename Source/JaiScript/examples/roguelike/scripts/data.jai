// Content tables: Kin species, classes, bestiary, items, affixes, flavor.
// All constant data — hot reload re-executes this file freely.

// ---------------------------------------------------------------- species ---
// The persistent folk of the world are anthro, digitigrade "Kin".
var SPECIES = [
	{
		"id": "jackodile", "name": "Jackodile-kin",
		"blurb": "Jackal grin, crocodile patience. JAI's own folk.",
		"hp": 0, "atk": 2, "def": 0, "eva": 2, "crit": 0, "focus": 0,
		"trait": "slipjaw", "trait_name": "Slip-Jaw",
		"trait_blurb": "+20 evasion while below a quarter health. Clever escapes are a birthright."
	},
	{
		"id": "drake", "name": "Drake-kin",
		"blurb": "Wingless dragonfolk. They insist the wings are 'in the mail'.",
		"hp": 6, "atk": 0, "def": 0, "eva": 0, "crit": 0, "focus": 0,
		"trait": "scaleblood", "trait_name": "Scaleblood",
		"trait_blurb": "Burning ticks deal half damage to you. Fire is practically a cousin."
	},
	{
		"id": "vulpen", "name": "Vulpen",
		"blurb": "Foxfolk. Three plans deep before you finish talking.",
		"hp": 0, "atk": 0, "def": 0, "eva": 3, "crit": 8, "focus": 0,
		"trait": "keeneye", "trait_name": "Keen Eye",
		"trait_blurb": "+8% critical chance. The soft spot practically glows."
	},
	{
		"id": "brockin", "name": "Brockin",
		"blurb": "Badgerfolk. Immovable opinions, immovable everything.",
		"hp": 3, "atk": 0, "def": 2, "eva": -1, "crit": 0, "focus": 0,
		"trait": "stubborn", "trait_name": "Too Stubborn To Die",
		"trait_blurb": "Once per floor, a killing blow leaves you at 1 HP instead."
	},
	{
		"id": "pangolix", "name": "Pangolix",
		"blurb": "Pangolinfolk. Walking argument for bringing a can opener.",
		"hp": 2, "atk": 0, "def": 1, "eva": 0, "crit": 0, "focus": 2,
		"trait": "curl", "trait_name": "Curl Up",
		"trait_blurb": "Dropping below a third health hardens your scales: +3 defense for 5 turns."
	}
];

// ---------------------------------------------------------------- classes ---
var CLASSES = [
	{
		"id": "warden", "name": "Warden",
		"blurb": "Shield-shaped opinions about who gets hurt today. Melee bruiser.",
		"hp": 8, "atk": 1, "def": 1, "eva": 0, "crit": 0, "focus": 6,
		"skill": "bulwark", "skill_name": "Bulwark Slam", "skill_cost": 5,
		"skill_blurb": "Slam an adjacent foe for 250% damage and knock the wind out of it."
	},
	{
		"id": "emberwright", "name": "Emberwright",
		"blurb": "Maker-caste fire-shaper; Devacor's distant apprentice line. Caster.",
		"hp": 0, "atk": 0, "def": 0, "eva": 0, "crit": 0, "focus": 12,
		"skill": "emberbolt", "skill_name": "Emberbolt", "skill_cost": 4,
		"skill_blurb": "Hurl fire at the nearest foe in sight (range 6) and set it burning."
	},
	{
		"id": "thornscout", "name": "Thornscout",
		"blurb": "Hedge-runner with a sling of fired thorns. Skirmisher.",
		"hp": 2, "atk": 0, "def": 0, "eva": 8, "crit": 4, "focus": 8,
		"skill": "thornvolley", "skill_name": "Thorn Volley", "skill_cost": 3,
		"skill_blurb": "Skewer the nearest foe in sight (range 5). Cheap, mean, repeatable."
	},
	{
		"id": "hexweaver", "name": "Hexweaver",
		"blurb": "Knits little curses out of spite and spare string. Debuffer.",
		"hp": 0, "atk": 0, "def": 0, "eva": 2, "crit": 0, "focus": 14,
		"skill": "wither", "skill_name": "Wither Hex", "skill_cost": 5,
		"skill_blurb": "Hex the nearest foe in sight (range 6): -2 attack/defense, and you sip 3 HP."
	}
];

// --------------------------------------------------------------- bestiary ---
// behavior: chase | ranged | pack | erratic | sluggish | mimic | boss
// tier feeds xp/gold; floors [min,max] gates spawning.
var BESTIARY = {
	"grublin": {
		"name": "Grublin", "glyph": "g", "color": "32", "floors": [1, 3],
		"hp": 6, "atk": 3, "def": 0, "eva": 5, "range": 1, "behavior": "chase", "withers": false, "burns": false, "tier": 1,
		"flavor": "A goblin that never washed. It brandishes something regrettable."
	},
	"slinger": {
		"name": "Grublin Slinger", "glyph": "s", "color": "92", "floors": [1, 4],
		"hp": 5, "atk": 3, "def": 0, "eva": 8, "behavior": "ranged", "range": 5, "withers": false, "burns": false, "tier": 1,
		"flavor": "Throws pebbles, insults, and the occasional cousin."
	},
	"gloop": {
		"name": "Gloop", "glyph": "o", "color": "36", "floors": [1, 4],
		"hp": 12, "atk": 2, "def": 1, "eva": 0, "range": 1, "behavior": "sluggish", "withers": false, "burns": false, "tier": 1,
		"flavor": "A slime with commitment issues. It jiggles ominously."
	},
	"snarlbat": {
		"name": "Snarlbat", "glyph": "b", "color": "35", "floors": [1, 4],
		"hp": 4, "atk": 3, "def": 0, "eva": 18, "range": 1, "behavior": "erratic", "withers": false, "burns": false, "tier": 1,
		"flavor": "All wing, no plan."
	},
	"scrappit": {
		"name": "Scrappit", "glyph": "k", "color": "33", "floors": [2, 6],
		"hp": 7, "atk": 4, "def": 1, "eva": 8, "range": 1, "behavior": "pack", "withers": false, "burns": false, "tier": 2,
		"flavor": "Kobold-adjacent. Braver in triplicate."
	},
	"rattlekin": {
		"name": "Rattlekin", "glyph": "r", "color": "37", "floors": [3, 6],
		"hp": 10, "atk": 5, "def": 1, "eva": 5, "range": 1, "behavior": "chase", "withers": false, "burns": false, "tier": 2,
		"flavor": "A skeleton held together by grudges and good posture."
	},
	"grinnest": {
		"name": "Grinnest", "glyph": "?", "color": "33", "floors": [3, 8],
		"hp": 14, "atk": 6, "def": 2, "eva": 0, "range": 1, "behavior": "mimic", "withers": false, "burns": false, "tier": 2,
		"flavor": "It looked like loot. It is mostly teeth arranged like a welcome mat."
	},
	"orglis": {
		"name": "Orglis", "glyph": "O", "color": "31", "floors": [4, 8],
		"hp": 18, "atk": 7, "def": 2, "eva": 3, "range": 1, "behavior": "chase", "withers": false, "burns": false, "tier": 3,
		"flavor": "An orc by any other spelling would smell as bad."
	},
	"hexmoth": {
		"name": "Hexmoth", "glyph": "m", "color": "95", "floors": [5, 8],
		"hp": 9, "atk": 5, "def": 0, "eva": 15, "behavior": "ranged", "range": 6, "burns": false, "tier": 3,
		"withers": true,
		"flavor": "Its wing-dust spells 'doom' in cursive."
	},
	"marrowmaw": {
		"name": "Marrowmaw", "glyph": "W", "color": "91", "floors": [6, 9],
		"hp": 26, "atk": 9, "def": 3, "eva": 2, "range": 1, "behavior": "chase", "withers": false, "burns": false, "tier": 4,
		"flavor": "A wolf-shaped appetite. The bones go in; nothing comes out."
	},
	"cinderwisp": {
		"name": "Cinderwisp", "glyph": "w", "color": "93", "floors": [7, 9],
		"hp": 10, "atk": 6, "def": 0, "eva": 20, "behavior": "ranged", "range": 5, "withers": false, "tier": 4,
		"burns": true,
		"flavor": "A candle flame that ran away to commit arson."
	},
	"drakelet": {
		"name": "Drakelet", "glyph": "d", "color": "31", "floors": [8, 10],
		"hp": 20, "atk": 9, "def": 3, "eva": 8, "range": 1, "behavior": "chase", "withers": false, "tier": 4,
		"burns": true,
		"flavor": "Vexadrach's brood. The fire is gray and wrong and still very hot."
	},
	"vexadrach": {
		"name": "Vexadrach the Hollow-Flame", "glyph": "V", "color": "90", "floors": [10, 10],
		"hp": 90, "atk": 11, "def": 4, "eva": 8, "behavior": "boss", "range": 4, "withers": false, "tier": 8,
		"burns": true,
		"flavor": "The wyrm who swallowed Devacor's rainbow fire and choked all the color out of it."
	}
};

// Spawn tables: floor -> weighted kinds (weights re-tuned during playtesting).
function spawn_table(int floor) -> var {
	var table = [];
	for (auto kv : BESTIARY) {
		var def = kv.second;   // NOT auto: inferred map types demand homogeneous values
		if (def["behavior"] == "boss") { continue; }
		if (floor >= def["floors"][0] && floor <= def["floors"][1]) {
			var weight = 10;
			if (def["behavior"] == "mimic") { weight = 3; }
			if (def["tier"] >= 3 && floor < 7) { weight = 6; }
			table.push([weight, kv.first]);
		}
	}
	return table;
}

// ------------------------------------------------------------------ items ---
var WEAPON_BASES = [
	{"name": "Shiv", "atk": 1}, {"name": "Fang Blade", "atk": 2},
	{"name": "Thorn Pike", "atk": 3}, {"name": "Ember Brand", "atk": 4},
	{"name": "Wyrmtooth Saber", "atk": 5}
];

var ARMOR_BASES = [
	{"name": "Patch Vest", "def": 1}, {"name": "Scale Jerkin", "def": 2},
	{"name": "Brockhide Coat", "def": 3}, {"name": "Warden Plate", "def": 4}
];

var TRINKET_BASES = [
	{"name": "Tooth Charm", "bonus": "crit", "power": 6},
	{"name": "Slip Anklet", "bonus": "eva", "power": 6},
	{"name": "Marrow Locket", "bonus": "hp", "power": 6},
	{"name": "Wick Ring", "bonus": "focus", "power": 5}
];

// rarity: [weight, id, affix_count, stat_bump]
var RARITIES = [
	[50, "common", 0, 0],
	[30, "uncommon", 1, 0],
	[14, "rare", 2, 1],
	[5, "epic", 2, 2],
	[1, "legendary", 2, 4]
];

var RARITY_COLORS = {
	"common": "37", "uncommon": "32", "rare": "94", "epic": "35", "legendary": "93"
};

var PREFIXES = [
	{"name": "Grubby", "atk": 0, "def": 0},
	{"name": "Polished", "atk": 1, "def": 0},
	{"name": "Jagged", "atk": 1, "def": 0},
	{"name": "Stalwart", "atk": 0, "def": 1},
	{"name": "Vicious", "atk": 2, "def": 0},
	{"name": "Bulwark", "atk": 0, "def": 2}
];

var SUFFIXES = [
	{"name": "of Nipping", "bonus": "atk", "power": 1},
	{"name": "of Warding", "bonus": "def", "power": 1},
	{"name": "of the Ember", "bonus": "burn", "power": 10},
	{"name": "of Slipping", "bonus": "eva", "power": 4},
	{"name": "of Vigor", "bonus": "hp", "power": 4},
	{"name": "of the Keen", "bonus": "crit", "power": 5}
];

var LEGENDARY_NAMES = {
	"weapon": "Jai's Sworn Tooth",
	"armor": "Devacor's Emberheart Scale",
	"trinket": "The Undimmed Prism"
};

// Potions: identify-by-use. Appearance labels are shuffled per run seed.
var POTIONS = {
	"mend": {"name": "Potion of Mending", "power": 10},
	"greatmend": {"name": "Potion of Great Mending", "power": 24},
	"focus": {"name": "Potion of Clear Focus", "power": 10},
	"rage": {"name": "Potion of Rattling Rage", "power": 3},
	"stoneskin": {"name": "Potion of Stoneskin", "power": 3},
	"regen": {"name": "Potion of Knitting Flesh", "power": 8}
};

var POTION_LOOKS = [
	"murky red", "fizzing gold", "gloopy green", "smoky violet",
	"pearlescent", "suspiciously chunky"
];

var SCROLLS = {
	"reveal": {"name": "Scroll of the Cartographer", "power": 0},
	"teleport": {"name": "Scroll of Elsewhere", "power": 0},
	"enfeeble": {"name": "Scroll of Group Discouragement", "power": 4},
	"lore": {"name": "Scroll of Lore", "power": 0}
};

var SCROLL_LOOKS = ["VEX MORT", "ZELGO MER", "HACKEM MUCHE", "GNIK SISI"];

// ----------------------------------------------------------------- flavor ---
var STATUE_JAI = [
	"A basalt statue of JAI the jackodile: half grin, half undertow.",
	"The plaque reads: 'Swift in, swifter out. Debt is for the slow.'",
	"JAI's stone tail curls around the pedestal like a drawn bracket."
];

var MURAL_DEVACOR = [
	"A mural of DEVACOR, the wingless dragon, breathing a ribbon of seven colors.",
	"Paint-flakes glitter: DEVACOR forging the first lantern out of a sunset.",
	"Someone has chalked under the mural: 'the fire is a gift, not a hoard.'"
];

var DEATH_QUIPS = [
	"The dungeon files you under 'snacks, assorted'.",
	"JAI's statue, somewhere above, grins a little wider.",
	"Your last thought is that the floor is surprisingly comfortable.",
	"Devacor's mural sheds one painted tear. Probably condensation."
];
