-- Gator Quest (Lua port) - the content: the DSL tables are copied verbatim from gq_data.jai and parsed
-- at load exactly as the script parses them. Ids are 0-based positions, like the script's.

SKILL_NAME = { [0] =
	"Swordsmanship",
	"Mace Fighting",
	"Archery",
	"Wrestling",
	"Tactics",
	"Parrying",
	"Magery",
	"Meditation",
	"Resisting Spells",
	"Healing",
	"Fishing",
	"Mining",
	"Lumberjacking",
	"Blacksmithing",
	"Cooking",
	"Alchemy",
	"Stealth",
	"Swimming"
}

SKILL_KEY = { [0] =
	"swords",
	"maces",
	"archery",
	"wrestling",
	"tactics",
	"parrying",
	"magery",
	"meditation",
	"resist",
	"healing",
	"fishing",
	"mining",
	"lumber",
	"smithing",
	"cooking",
	"alchemy",
	"stealth",
	"swimming"
}

SKILL_TITLE = { [0] =
	"Swordsgator",
	"Brawler",
	"Archer",
	"Wrestler",
	"Tactician",
	"Shieldbearer",
	"Mage",
	"Mystic",
	"Warder",
	"Healer",
	"Angler",
	"Miner",
	"Lumberjack",
	"Blacksmith",
	"Chef",
	"Alchemist",
	"Sneak",
	"Swimmer"
}

RANK_NAME = { [0] =
	"Neophyte",
	"Novice",
	"Apprentice",
	"Journeyman",
	"Expert",
	"Adept",
	"Master",
	"Grandmaster"
}

SLOT_NAME = { [0] =
	"head",
	"neck",
	"body",
	"hands",
	"legs",
	"feet",
	"weapon",
	"offhand",
	"ring",
	"trinket"
}

KIND_NAME = { [0] =
	"weapon",
	"armor",
	"shield",
	"potion",
	"scroll",
	"food",
	"reagent",
	"tool",
	"resource",
	"ammo",
	"trinket",
	"junk",
	"gem",
	"quest",
	"gold"
}

RARITY_NAME = { [0] =
	"Common",
	"Uncommon",
	"Rare",
	"Epic",
	"Legendary"
}

EFF_NAME = { [0] =
	"none",
	"heal",
	"mana",
	"cure",
	"str",
	"dex",
	"int",
	"nightsight",
	"fireres",
	"regen",
	"learn",
	"recall",
	"food",
	"random",
	"firebreath",
	"bandage",
	"bottle",
	"squeak",
	"music",
	"bomb",
	"spellbook"
}

AF_NAME = { [0] =
	"dmg",
	"fire",
	"cold",
	"poison",
	"leech",
	"str",
	"dex",
	"int",
	"speed",
	"armor",
	"hp",
	"mana",
	"luck",
	"resist",
	"regen",
	"thorns",
	"swim",
	"crit",
	"gold",
	"mregen",
	"magery"
}

AI_NAME = { [0] =
	"none",
	"wander",
	"melee",
	"pack",
	"ranged",
	"caster",
	"coward",
	"thief",
	"ambush",
	"boss",
	"guard",
	"critter",
	"summon"
}

FAC_NAME = { [0] =
	"player",
	"npc",
	"monster",
	"animal",
	"guard"
}

STATION_NAME = { [0] =
	"",
	"forge",
	"anvil",
	"fire",
	"alchemy",
	"bank",
	"shrine",
	"gate",
	"stairs",
	"chest"
}

TILE_SRC = { [0] =
	"void|walk=3",
	"grass|walk=1",
	"tall grass|walk=1|hide",
	"flowers|walk=1",
	"dirt|walk=1",
	"road|walk=1",
	"sand|walk=1",
	"shallows|walk=6|res=4",
	"deep water|walk=2|res=4",
	"swamp water|walk=2|res=4",
	"mud|walk=6",
	"oak|walk=0|opaque|res=1|after=stump",
	"pine|walk=0|opaque|res=1|after=stump",
	"cypress|walk=0|opaque|res=1|after=stump",
	"dead tree|walk=0|res=1|after=stump",
	"bush|walk=6|hide",
	"boulder|walk=0|res=2|after=rubble",
	"ore vein|walk=0|res=3|after=rubble",
	"mountain|walk=0|opaque",
	"cave wall|walk=0|opaque",
	"cave floor|walk=1",
	"wood floor|walk=1",
	"stone floor|walk=1",
	"brick wall|walk=0|opaque",
	"wood wall|walk=0|opaque",
	"door|walk=1",
	"counter|walk=0",
	"anvil|walk=0|st=2",
	"forge|walk=0|st=1|light=4",
	"fire pit|walk=0|st=3|light=5",
	"alchemy table|walk=0|st=4|light=1",
	"bank chest|walk=0|st=5",
	"bed|walk=0",
	"fence|walk=0",
	"bridge|walk=1",
	"lava|walk=5|light=4",
	"obsidian|walk=1",
	"ash|walk=1",
	"ice|walk=4",
	"snow|walk=1",
	"ice wall|walk=0|opaque",
	"cloud|walk=1",
	"sky|walk=3",
	"rainbow|walk=1|light=2",
	"ruin floor|walk=1",
	"ruin wall|walk=0|opaque",
	"coral|walk=0",
	"kelp|walk=6|hide",
	"lily pad|walk=1",
	"reeds|walk=6|hide",
	"grave|walk=0",
	"bog|walk=6",
	"moongate|walk=1|st=7|light=5",
	"stairs down|walk=1|st=8|light=2",
	"stairs up|walk=1|st=8|light=2",
	"shrine|walk=0|st=6|light=5",
	"crystal|walk=0|res=5|light=3|after=rubble",
	"stump|walk=1",
	"rubble|walk=6",
	"chest|walk=0|st=9",
	"sign|walk=0",
	"sconce|walk=0|opaque|light=5",
	"sewage|walk=2",
	"sewer floor|walk=1",
	"carpet|walk=1",
	"well|walk=0",
	"basalt|walk=0|opaque",
	"pillar|walk=0",
	"gold pile|walk=1|light=1",
	"web|walk=6|hide"
}

ITEM_SRC = { [0] =
	"Rusty Dagger|weapon|dmg=2-5|spd=5|sk=swords|val=8|tier=1|spr=dagger|col=rust|d=Rust counts as poison, right?",
	"Short Sword|weapon|dmg=3-8|spd=7|sk=swords|val=25|tier=1|spr=sword|col=steel|d=Short, like your temper.",
	"Cutlass|weapon|dmg=5-11|spd=8|sk=swords|val=60|tier=2|spr=sword|col=steel|d=Arr.",
	"Broadsword|weapon|dmg=6-14|spd=9|sk=swords|val=110|tier=2|spr=sword|col=silver|d=Broad in every sense.",
	"Katana|weapon|dmg=7-15|spd=7|sk=swords|val=240|tier=3|spr=sword|col=silver|d=Folded one thousand times. Mostly by accident.",
	"Gator Tooth Blade|weapon|dmg=9-18|spd=8|sk=swords|val=420|tier=4|spr=sword|col=bone|d=Carved from a molar you lost as a hatchling.",
	"Obsidian Greatsword|weapon|dmg=14-26|spd=12|sk=swords|val=900|tier=5|spr=greatsword|col=obsidian|d=Heavy, sharp, and deeply emo.",
	"Club|weapon|dmg=3-7|spd=7|sk=maces|val=12|tier=1|spr=club|col=wood|d=The original user interface.",
	"Frying Pan|weapon|dmg=4-9|spd=8|sk=maces|val=30|tier=1|spr=pan|col=iron|d=BONK. Also cooks eggs.",
	"Mace|weapon|dmg=6-12|spd=9|sk=maces|val=90|tier=2|spr=mace|col=steel|d=Spiky persuasion.",
	"Fish Slapper|weapon|dmg=5-12|spd=6|sk=maces|val=150|tier=3|spr=fish|col=cyan|d=A frozen mackerel on a stick. Humiliating for all involved.",
	"War Hammer|weapon|dmg=10-20|spd=12|sk=maces|val=250|tier=3|spr=hammer|col=steel|d=Stop. Hammer time.",
	"Anchor Flail|weapon|dmg=12-24|spd=12|sk=maces|val=600|tier=5|spr=anchor|col=iron|d=Found at the bottom of Sogg harbor, still attached to a very surprised ship.",
	"Short Bow|weapon|dmg=3-9|spd=8|sk=archery|rng=7|val=40|tier=1|spr=bow|col=wood|d=Needs arrows. Gators forget this constantly.",
	"Long Bow|weapon|dmg=5-13|spd=10|sk=archery|rng=9|val=140|tier=2|spr=bow|col=wood|d=For hitting things you would rather not smell.",
	"Crossbow|weapon|dmg=8-16|spd=12|sk=archery|rng=8|val=300|tier=3|spr=crossbow|col=wood|d=Point the pointy end away from face.",
	"Harpoon Gun|weapon|dmg=10-20|spd=11|sk=archery|rng=7|val=700|tier=5|spr=crossbow|col=steel|d=For whales. And rude eels.",
	"Apprentice Wand|weapon|dmg=4-9|spd=9|sk=magery|rng=6|val=120|tier=2|spr=wand|col=purple|d=Shoots sparkles. Sparkles hurt.",
	"Staff of Sparks|weapon|dmg=6-14|spd=10|sk=magery|rng=7|val=480|tier=4|spr=staff|col=cyan|d=Zappity zap.",
	"Straw Hat|armor|slot=head|arm=1|val=5|tier=1|spr=hat|col=straw|d=Keeps the sun off your scales.",
	"Leather Cap|armor|slot=head|arm=2|val=15|tier=1|spr=cap|col=leather",
	"Bucket Helm|armor|slot=head|arm=3|val=20|tier=1|spr=helm|col=iron|d=Clang.",
	"Iron Helm|armor|slot=head|arm=5|val=80|tier=2|spr=helm|col=steel",
	"Wizard Hat|armor|slot=head|arm=1|val=120|tier=2|spr=wizhat|col=purple|aff=int:5,mana:10|d=Pointy. Mysterious. A little itchy.",
	"Leather Tunic|armor|slot=body|arm=4|val=30|tier=1|spr=tunic|col=leather",
	"Chainmail Shirt|armor|slot=body|arm=8|val=120|tier=2|spr=chain|col=steel",
	"Gator-Scale Vest|armor|slot=body|arm=10|val=220|tier=3|spr=tunic|col=green|d=Made from a distant cousin. Awkward at reunions.",
	"Plate Cuirass|armor|slot=body|arm=13|val=400|tier=4|spr=plate|col=silver",
	"Robe of the Mire|armor|slot=body|arm=3|val=260|tier=3|spr=robe|col=moss|aff=magery:8,mregen:2",
	"Leather Gloves|armor|slot=hands|arm=1|val=10|tier=1|spr=gloves|col=leather",
	"Chain Gloves|armor|slot=hands|arm=3|val=60|tier=2|spr=gloves|col=steel",
	"Plate Gauntlets|armor|slot=hands|arm=5|val=160|tier=4|spr=gloves|col=silver",
	"Leather Leggings|armor|slot=legs|arm=2|val=20|tier=1|spr=legs|col=leather",
	"Chain Leggings|armor|slot=legs|arm=5|val=90|tier=2|spr=legs|col=steel",
	"Plate Greaves|armor|slot=legs|arm=8|val=240|tier=4|spr=legs|col=silver",
	"Sandals|armor|slot=feet|arm=1|val=6|tier=1|spr=boots|col=straw",
	"Leather Boots|armor|slot=feet|arm=2|val=18|tier=1|spr=boots|col=leather",
	"Swamp Waders|armor|slot=feet|arm=2|val=90|tier=2|spr=boots|col=moss|aff=swim:20|d=Waterproof to the knee. Gators have no knees.",
	"Plate Boots|armor|slot=feet|arm=4|val=150|tier=4|spr=boots|col=silver",
	"Pot Lid|shield|slot=offhand|arm=2|val=8|tier=1|spr=shield|col=iron|d=Still smells like gumbo.",
	"Wooden Shield|shield|slot=offhand|arm=4|val=30|tier=1|spr=shield|col=wood",
	"Kite Shield|shield|slot=offhand|arm=7|val=140|tier=3|spr=shield|col=steel",
	"Turtle Shell Shield|shield|slot=offhand|arm=8|val=200|tier=3|spr=shield|col=moss|d=It is fine. The turtle was done with it.",
	"Tower Shield|shield|slot=offhand|arm=10|val=320|tier=4|spr=shield|col=silver",
	"Copper Amulet|armor|slot=neck|arm=0|val=40|tier=1|spr=amulet|col=copper",
	"Bone Charm|armor|slot=neck|arm=0|val=70|tier=2|spr=amulet|col=bone",
	"Pearl Necklace|armor|slot=neck|arm=0|val=160|tier=3|spr=amulet|col=pearl",
	"Copper Ring|armor|slot=ring|arm=0|val=35|tier=1|spr=ring|col=copper",
	"Silver Ring|armor|slot=ring|arm=0|val=90|tier=2|spr=ring|col=silver",
	"Gold Ring|armor|slot=ring|arm=0|val=200|tier=3|spr=ring|col=gold",
	"Lucky Frog Charm|trinket|slot=trinket|val=60|tier=1|spr=frog|col=green|aff=luck:10|d=Ribbit for luck.",
	"Rubber Duck|trinket|slot=trinket|val=500|tier=5|rar=legendary|eff=squeak|spr=duck|col=yellow|aff=luck:30,swim:30,hp:15|d=The single most powerful artifact in Gatorland. Squeaks.",
	"Kazoo|trinket|slot=trinket|val=25|tier=1|eff=music|spr=kazoo|col=red|d=Bzzzt bzzt bzzzzzt.",
	"Banjo|trinket|slot=trinket|val=180|tier=2|eff=music|spr=banjo|col=wood|aff=dex:3|d=Dueling optional.",
	"Minor Heal Potion|potion|eff=heal|pow=18|val=15|tier=1|stack|spr=potion|col=red",
	"Heal Potion|potion|eff=heal|pow=40|val=40|tier=2|stack|spr=potion|col=red",
	"Greater Heal Potion|potion|eff=heal|pow=80|val=110|tier=3|stack|spr=potion|col=crimson",
	"Mana Potion|potion|eff=mana|pow=35|val=45|tier=2|stack|spr=potion|col=blue",
	"Cure Potion|potion|eff=cure|val=30|tier=1|stack|spr=potion|col=orange",
	"Strength Potion|potion|eff=str|pow=10|val=60|tier=2|stack|spr=potion|col=white",
	"Night Sight Potion|potion|eff=nightsight|pow=1500|val=35|tier=1|stack|spr=potion|col=yellow",
	"Fire Resist Potion|potion|eff=fireres|pow=900|val=70|tier=3|stack|spr=potion|col=lava",
	"Swamp Tonic|potion|eff=regen|pow=300|val=55|tier=2|stack|spr=potion|col=moss|d=Tastes like the bottom of a pond. In a good way.",
	"Swamp Gas Bomb|potion|eff=bomb|pow=25|val=80|tier=3|stack|spr=bomb|col=slime|d=Do not light indoors.",
	"Scroll of Heal|scroll|eff=learn|pow=0|val=30|tier=1|stack|spr=scroll|col=paper",
	"Scroll of Magic Arrow|scroll|eff=learn|pow=1|val=30|tier=1|stack|spr=scroll|col=paper",
	"Scroll of Night Sight|scroll|eff=learn|pow=2|val=35|tier=1|stack|spr=scroll|col=paper",
	"Scroll of Cure|scroll|eff=learn|pow=3|val=40|tier=1|stack|spr=scroll|col=paper",
	"Scroll of Frost Bolt|scroll|eff=learn|pow=4|val=90|tier=2|stack|spr=scroll|col=paper",
	"Scroll of Fireball|scroll|eff=learn|pow=5|val=110|tier=2|stack|spr=scroll|col=paper",
	"Scroll of Teleport|scroll|eff=learn|pow=6|val=140|tier=2|stack|spr=scroll|col=paper",
	"Scroll of Poison|scroll|eff=learn|pow=7|val=120|tier=2|stack|spr=scroll|col=paper",
	"Scroll of Recall|scroll|eff=learn|pow=8|val=160|tier=2|stack|spr=scroll|col=paper",
	"Scroll of Lightning|scroll|eff=learn|pow=9|val=240|tier=3|stack|spr=scroll|col=paper",
	"Scroll of Greater Heal|scroll|eff=learn|pow=10|val=260|tier=3|stack|spr=scroll|col=paper",
	"Scroll of Summon Frog|scroll|eff=learn|pow=11|val=300|tier=3|stack|spr=scroll|col=paper",
	"Scroll of Resurrection|scroll|eff=learn|pow=12|val=500|tier=4|stack|spr=scroll|col=paper",
	"Scroll of Flamestrike|scroll|eff=learn|pow=13|val=600|tier=4|stack|spr=scroll|col=paper",
	"Scroll of Chain Lightning|scroll|eff=learn|pow=14|val=800|tier=5|stack|spr=scroll|col=paper",
	"Scroll of Meteor Swarm|scroll|eff=learn|pow=15|val=1200|tier=5|stack|spr=scroll|col=paper",
	"Recall Rune|scroll|eff=recall|val=50|tier=1|stack|spr=rune|col=blue|d=Snap it to go home. Works 60% of the time, every time.",
	"Spellbook|tool|eff=spellbook|val=40|tier=1|spr=book|col=purple|d=A slim volume. Your spells live here.",
	"Raw Fish|food|eff=food|pow=3|val=3|tier=1|stack|spr=fish|col=silver",
	"Cooked Fish|food|eff=food|pow=14|val=10|tier=1|stack|spr=fish|col=tan",
	"Catfish|food|eff=food|pow=4|val=6|tier=1|stack|spr=fish|col=brown",
	"Blackened Catfish|food|eff=food|pow=24|val=22|tier=2|stack|spr=fish|col=char",
	"Frog Legs|food|eff=food|pow=3|val=4|tier=1|stack|spr=meat|col=green",
	"Fried Frog Legs|food|eff=food|pow=18|val=16|tier=1|stack|spr=meat|col=tan|d=Tastes like chicken. Everything does.",
	"Crawdad|food|eff=food|pow=3|val=5|tier=1|stack|spr=crawdad|col=red",
	"Crawdad Etouffee|food|eff=food|pow=35|val=40|tier=2|stack|spr=bowl|col=orange|d=Mama's recipe. Do not tell Mama.",
	"Bread|food|eff=food|pow=10|val=5|tier=1|stack|spr=bread|col=tan",
	"Cheese Wheel|food|eff=food|pow=12|val=8|tier=1|stack|spr=cheese|col=yellow",
	"Moon Pie|food|eff=food|pow=25|val=15|tier=1|stack|spr=pie|col=tan|d=Once in a blue moon.",
	"Marshmallow|food|eff=food|pow=2|val=2|tier=1|stack|spr=marsh|col=white",
	"Toasted Marshmallow|food|eff=food|pow=16|val=9|tier=1|stack|spr=marsh|col=tan",
	"Hot Sauce|food|eff=firebreath|pow=20|val=30|tier=2|stack|spr=potion|col=red|d=Breathe fire at whatever is in front of you. Regret it later.",
	"Suspicious Mushroom|food|eff=random|val=12|tier=1|stack|spr=shroom|col=purple|d=What could possibly go wrong?",
	"Ice Fish|food|eff=food|pow=6|val=12|tier=2|stack|spr=fish|col=ice",
	"Cloud Trout|food|eff=food|pow=8|val=18|tier=3|stack|spr=fish|col=white",
	"Ghost Fish|food|eff=food|pow=6|val=20|tier=3|stack|spr=fish|col=ghost|d=You can see its lunch.",
	"Lava Eel|food|eff=food|pow=6|val=20|tier=3|stack|spr=fish|col=lava",
	"Smoked Eel|food|eff=food|pow=45|val=50|tier=3|stack|spr=fish|col=char",
	"Golden Carp|junk|val=750|tier=4|spr=fish|col=gold|d=A legend of the Bayou. Worth a fortune to the Fishmonger.",
	"Black Pearl|reagent|val=5|tier=1|stack|spr=pearl|col=black",
	"Bloodmoss|reagent|val=5|tier=1|stack|spr=herb|col=red",
	"Garlic|reagent|val=3|tier=1|stack|spr=garlic|col=white",
	"Ginseng|reagent|val=3|tier=1|stack|spr=root|col=tan",
	"Mandrake Root|reagent|val=4|tier=1|stack|spr=root|col=brown",
	"Nightshade|reagent|val=4|tier=1|stack|spr=herb|col=purple",
	"Spider Silk|reagent|val=4|tier=1|stack|spr=silk|col=white",
	"Sulfurous Ash|reagent|val=4|tier=1|stack|spr=ash|col=yellow",
	"Fishing Pole|tool|val=12|tier=1|spr=pole|col=wood",
	"Pickaxe|tool|val=20|tier=1|spr=pick|col=iron",
	"Hatchet|tool|val=18|tier=1|spr=axe|col=iron",
	"Smith Hammer|tool|val=25|tier=1|spr=hammer|col=iron",
	"Mortar and Pestle|tool|val=25|tier=1|spr=mortar|col=stone",
	"Bandage|tool|eff=bandage|pow=12|val=2|tier=1|stack|spr=bandage|col=white",
	"Arrow|ammo|val=1|tier=1|stack|spr=arrow|col=wood",
	"Iron Ore|resource|val=4|tier=1|stack|spr=ore|col=rust",
	"Silver Ore|resource|val=10|tier=2|stack|spr=ore|col=silver",
	"Mithril Ore|resource|val=30|tier=4|stack|spr=ore|col=mithril",
	"Iron Ingot|resource|val=10|tier=1|stack|spr=ingot|col=iron",
	"Silver Ingot|resource|val=25|tier=2|stack|spr=ingot|col=silver",
	"Mithril Ingot|resource|val=70|tier=4|stack|spr=ingot|col=mithril",
	"Log|resource|val=3|tier=1|stack|spr=log|col=wood",
	"Feather|resource|val=2|tier=1|stack|spr=feather|col=white",
	"Hide|resource|val=6|tier=1|stack|spr=hide|col=leather",
	"Bone|resource|val=3|tier=1|stack|spr=bone|col=bone",
	"Magma Core|resource|val=60|tier=5|stack|spr=orb|col=lava",
	"Frost Shard|resource|val=50|tier=4|stack|spr=shard|col=ice",
	"Storm Essence|resource|val=70|tier=5|stack|spr=orb|col=cyan",
	"Ectoplasm|resource|val=40|tier=3|stack|spr=orb|col=ghost",
	"Kelp|resource|val=3|tier=1|stack|spr=herb|col=moss",
	"Cloud Fluff|resource|val=20|tier=4|stack|spr=fluff|col=white",
	"Rat Tail|resource|val=1|tier=1|stack|spr=tail|col=pink",
	"Amethyst|gem|val=60|tier=2|stack|spr=gem|col=purple",
	"Ruby|gem|val=100|tier=3|stack|spr=gem|col=red",
	"Sapphire|gem|val=100|tier=3|stack|spr=gem|col=blue",
	"Emerald|gem|val=120|tier=3|stack|spr=gem|col=green",
	"Diamond|gem|val=250|tier=4|stack|spr=gem|col=white",
	"Gold Coins|gold|val=1|tier=1|stack|spr=coins|col=gold",
	"Old Boot|junk|val=1|tier=1|spr=boots|col=leather|d=Somebody lost a boot. Somebody is still looking.",
	"Tin Can|junk|val=1|tier=1|spr=can|col=silver",
	"Soggy Hat|junk|val=1|tier=1|spr=hat|col=moss",
	"Message in a Bottle|junk|eff=bottle|val=5|tier=1|spr=bottle|col=cyan",
	"Mossy Relic Scale|quest|val=0|tier=5|spr=scale|col=moss|d=Taken from Big Mama Snapper. It hums.",
	"Pearl Relic Scale|quest|val=0|tier=5|spr=scale|col=pearl|d=Taken from the Drowned King. It is damp.",
	"Shadow Relic Scale|quest|val=0|tier=5|spr=scale|col=ghost|d=Taken from Count Crocula. It whispers about blood types.",
	"Frost Relic Scale|quest|val=0|tier=5|spr=scale|col=ice|d=Taken from the Glacier Queen. Cold to the touch.",
	"Ember Relic Scale|quest|val=0|tier=5|spr=scale|col=lava|d=Taken from Emberjaw. Do not pocket.",
	"Storm Relic Scale|quest|val=0|tier=5|spr=scale|col=cyan|d=Taken from the Thunder Roc. Crackles.",
	"Rat King's Crown|armor|slot=head|arm=4|val=300|tier=3|rar=legendary|spr=crown|col=gold|aff=luck:20,gold:25|d=Slightly chewed.",
	"Big Mama's Shell|shield|slot=offhand|arm=14|val=1500|tier=5|rar=legendary|spr=shield|col=moss|aff=hp:30,thorns:5",
	"Trident of the Drowned King|weapon|dmg=12-24|spd=9|sk=maces|val=2000|tier=5|rar=legendary|spr=trident|col=pearl|aff=cold:8,mana:15",
	"Crocula's Cape|armor|slot=body|arm=9|val=2200|tier=5|rar=legendary|spr=robe|col=crimson|aff=leech:12,stealth:0",
	"Glacier Queen's Tiara|armor|slot=head|arm=4|val=2400|tier=5|rar=legendary|spr=crown|col=ice|aff=int:12,mregen:4,frost:0",
	"Emberjaw's Fang|weapon|dmg=15-28|spd=8|sk=swords|val=3000|tier=5|rar=legendary|spr=sword|col=lava|aff=fire:12,crit:8",
	"Stormcaller|weapon|dmg=11-22|spd=9|sk=archery|rng=10|val=3200|tier=5|rar=legendary|spr=bow|col=cyan|aff=speed:2,dex:10",
	"Crown of the Swamp|armor|slot=head|arm=8|val=9999|tier=5|rar=legendary|spr=crown|col=gold|aff=str:15,dex:15,int:15,hp:40,luck:25|d=Worn by the Gator Avatar. Fits perfectly."
}

AFFIX_SRC = { [0] =
	"Sharp|pre|weapon|dmg|10-25|1",
	"Ancient|pre|weapon|dmg|25-45|3",
	"Flaming|pre|weapon|fire|3-8|2",
	"Frosty|pre|weapon|cold|3-8|2",
	"Venomous|pre|weapon|poison|2-6|2",
	"Vampiric|pre|weapon|leech|5-12|3",
	"Swift|pre|weapon|speed|1-2|2",
	"Chompy|pre|weapon|crit|4-10|1",
	"Soggy|pre|any|dmg|-15--5|1",
	"Sturdy|pre|armor|armor|1-3|1",
	"Reinforced|pre|armor|armor|3-6|3",
	"Blessed|pre|armor|hp|8-20|2",
	"Lucky|pre|any|luck|5-15|1",
	"Nimble|pre|armor|dex|2-6|2",
	"Mighty|pre|any|str|2-6|2",
	"of the Bear|suf|any|str|3-8|1",
	"of the Fox|suf|any|dex|3-8|1",
	"of the Owl|suf|any|int|3-8|1",
	"of the Gator|suf|any|hp|10-25|2",
	"of Slaying|suf|weapon|dmg|15-30|3",
	"of Warding|suf|armor|resist|5-15|2",
	"of the Magi|suf|any|mana|8-20|2",
	"of Fortune|suf|any|gold|10-30|1",
	"of Regeneration|suf|armor|regen|1-3|3",
	"of Thorns|suf|armor|thorns|2-6|2",
	"of the Bayou|suf|any|swim|10-30|1",
	"of Sorcery|suf|any|magery|3-10|3",
	"of Clarity|suf|armor|mregen|1-3|3"
}

SPELL_SRC = { [0] =
	"Heal|circle=1|mana=4|reg=Garlic,Ginseng|tgt=0|eff=heal|pow=8-14|fx=heal|snd=heal|d=Knit flesh. Or scales.",
	"Magic Arrow|circle=1|mana=4|reg=Sulfurous Ash|tgt=1|eff=dmg|pow=5-10|fx=magic|snd=zap|d=A bolt of pure spite.",
	"Night Sight|circle=1|mana=5|reg=Spider Silk,Sulfurous Ash|tgt=8|eff=nightsight|pow=1500|fx=magic|snd=chime|d=See in the dark like a proper swamp predator.",
	"Cure|circle=2|mana=6|reg=Garlic,Ginseng|tgt=0|eff=cure|fx=heal|snd=heal|d=Purge poison.",
	"Frost Bolt|circle=2|mana=8|reg=Nightshade,Spider Silk|tgt=1|eff=frost|pow=8-15|fx=frost|snd=zap|d=Damage and slow a foe.",
	"Fireball|circle=3|mana=9|reg=Black Pearl|tgt=1|eff=fire|pow=10-18|fx=fire|snd=fire|d=The classic.",
	"Teleport|circle=3|mana=10|reg=Bloodmoss,Mandrake Root|tgt=5|eff=blink|pow=6|fx=warp|snd=warp|d=Blink forward up to six tiles.",
	"Poison|circle=3|mana=10|reg=Nightshade|tgt=1|eff=poison|pow=40-60|fx=poison|snd=hiss|d=Slow, green, and personal.",
	"Recall|circle=4|mana=12|reg=Black Pearl,Bloodmoss,Mandrake Root|tgt=6|eff=recall|fx=warp|snd=warp|d=Return home to your bound shrine.",
	"Lightning|circle=4|mana=14|reg=Mandrake Root,Sulfurous Ash|tgt=1|eff=dmg|pow=16-26|fx=zap|snd=thunder|d=Smite.",
	"Greater Heal|circle=5|mana=14|reg=Garlic,Ginseng,Mandrake Root,Spider Silk|tgt=0|eff=heal|pow=30-45|fx=heal|snd=heal|d=Mend almost anything.",
	"Summon Frog|circle=5|mana=16|reg=Bloodmoss,Mandrake Root,Spider Silk|tgt=4|eff=summon|fx=magic|snd=ribbit|d=Summon a loyal frog familiar. It is very brave.",
	"Resurrection|circle=6|mana=25|reg=Bloodmoss,Garlic,Ginseng|tgt=7|eff=res|fx=holy|snd=chime|d=Bring a ghostly friend back to the land of the scaly.",
	"Flamestrike|circle=6|mana=25|reg=Spider Silk,Sulfurous Ash|tgt=1|eff=fire|pow=30-45|fx=fire|snd=fire|d=A pillar of fire. Stand back.",
	"Chain Lightning|circle=7|mana=30|reg=Black Pearl,Bloodmoss,Mandrake Root,Sulfurous Ash|tgt=2|eff=dmg|pow=18-30|rad=3|fx=zap|snd=thunder|d=Arcs between every foe near your target.",
	"Meteor Swarm|circle=8|mana=40|reg=Bloodmoss,Mandrake Root,Spider Silk,Sulfurous Ash|tgt=2|eff=fire|pow=25-40|rad=4|fx=fire|snd=boom|d=Rocks. From. SPACE."
}

SPELL_EFF = { [0] =
	"heal",
	"dmg",
	"nightsight",
	"cure",
	"frost",
	"fire",
	"blink",
	"poison",
	"recall",
	"summon",
	"res"
}

FX_NAME = { [0] =
	"sparks",
	"blood",
	"splash",
	"fire",
	"frost",
	"zap",
	"heal",
	"smoke",
	"poison",
	"magic",
	"gold",
	"loot",
	"levelup",
	"warp",
	"bubbles",
	"bones",
	"dust",
	"holy"
}

MON_SRC = { [0] =
	"Townsgator|hp=100|ai=wander|fac=npc|move=1|spr=gator_npc|col=olive|quip=Mornin'!",
	"Frogfolk|hp=100|ai=wander|fac=npc|move=3|spr=frogman|col=green|quip=Ribbit. I mean, hello.",
	"Old Turtle|hp=100|ai=wander|fac=npc|move=1|spr=turtle_npc|col=moss|quip=Back in my day the swamp was wetter.",
	"Swampton Guard|hp=999|dmg=40-60|ai=guard|fac=guard|move=3|spd=2|aspd=6|skill=1000|spr=guard|col=steel|quip=Halt, scum!",
	"Frog|hp=6|dmg=1-1|ai=critter|fac=animal|move=3|spd=4|tier=1|drop=Frog Legs:800|spr=frog|col=green|snd=ribbit",
	"Crawdad|hp=6|dmg=1-2|ai=critter|fac=animal|move=3|spd=5|tier=1|drop=Crawdad:800|spr=crawdad|col=red",
	"Marsh Rabbit|hp=5|ai=critter|fac=animal|move=1|spd=2|tier=1|drop=Hide:500|spr=rabbit|col=tan",
	"Heron|hp=10|ai=critter|fac=animal|move=4|spd=3|tier=1|drop=Feather:900|spr=heron|col=white",
	"Cloud Sheep|hp=40|ai=critter|fac=animal|move=1|spd=4|tier=4|drop=Cloud Fluff:900|spr=sheep|col=white|quip=Baaaa.",
	"Mosquito Swarm|hp=6|dmg=1-2|spd=2|aspd=5|ai=pack|move=4|tier=1|fame=2|spr=mosquito|col=gray|snd=buzz|quip=BZZZZZ",
	"Swamp Rat|hp=10|dmg=1-4|spd=3|aspd=7|ai=melee|tier=1|fame=3|gold=0-3|drop=Rat Tail:500|spr=rat|col=brown|snd=squeak",
	"Raccoon Bandit|hp=14|dmg=1-3|spd=2|aspd=6|ai=thief|tier=1|fame=6|gold=2-10|spr=raccoon|col=gray|quip=Yoink!",
	"Feral Hog|hp=22|dmg=3-6|spd=3|aspd=8|ai=melee|tier=1|fame=6|drop=Hide:700|spr=hog|col=brown|snd=oink",
	"Timber Wolf|hp=20|dmg=3-7|spd=2|aspd=7|ai=pack|tier=1|fame=7|drop=Hide:600|spr=wolf|col=gray|snd=howl",
	"Goblin Poacher|hp=24|dmg=3-8|spd=3|aspd=9|ai=ranged|rng=6|tier=1|fame=9|gold=4-15|drop=Arrow:700,Short Bow:60|spr=goblin|col=green|quip=Gator boots! Gator boots!",
	"Giant Frog|hp=18|dmg=2-6|spd=3|aspd=8|ai=melee|move=3|tier=1|fame=5|drop=Frog Legs:900|spr=frog_big|col=green|snd=ribbit",
	"Cottonmouth|hp=16|dmg=2-5|spd=3|aspd=7|ai=melee|move=3|tier=1|fame=6|poisonous|spr=snake|col=olive|snd=hiss",
	"Bandit|hp=30|dmg=4-9|spd=3|aspd=8|ai=melee|tier=2|fame=10|gold=10-30|drop=Minor Heal Potion:200|spr=bandit|col=red|quip=Your gold or your scales!",
	"Treant|hp=60|dmg=6-12|armor=4|spd=5|aspd=11|ai=melee|tier=2|fame=15|drop=Log:1000|spr=treant|col=wood|quip=Hrrrmm. You stepped on my cousin.",
	"Sewer Rat|hp=12|dmg=2-4|spd=2|aspd=7|ai=pack|tier=1|fame=3|gold=0-4|drop=Rat Tail:600|spr=rat|col=gray|snd=squeak",
	"Green Slime|hp=30|dmg=3-6|spd=4|aspd=9|ai=melee|move=3|tier=1|fame=6|split=Slimelet|spr=slime|col=slime|snd=squish",
	"Slimelet|hp=9|dmg=1-3|spd=3|aspd=8|ai=melee|move=3|tier=1|fame=1|spr=slime_small|col=slime|snd=squish",
	"Giant Cockroach|hp=20|dmg=2-5|spd=1|aspd=6|ai=melee|tier=1|fame=4|spr=roach|col=brown|quip=It survived the apocalypse. It will survive you.",
	"The Rat King|hp=260|dmg=8-14|armor=3|spd=3|aspd=7|ai=boss|tier=2|fame=120|gold=80-160|boss|summon=Sewer Rat|drop=Rat King's Crown:1000|spr=ratking|col=gold|quip=MY KINGDOM FOR A CHEESE!",
	"Snapping Turtle|hp=40|dmg=5-10|armor=6|spd=4|aspd=9|ai=melee|move=3|tier=2|fame=12|drop=Turtle Shell Shield:30|spr=turtle|col=moss|snd=snap",
	"Voodoo Toad|hp=30|dmg=2-5|spd=3|aspd=10|ai=caster|move=3|spell=Poison|tier=2|fame=14|gold=5-20|drop=Nightshade:700,Bloodmoss:400|spr=toad|col=purple|quip=Hex-ribbit!",
	"Crawdad Knight|hp=45|dmg=6-11|armor=5|spd=3|aspd=8|ai=melee|move=3|tier=2|fame=15|gold=5-25|drop=Crawdad:900|spr=crawknight|col=red|quip=En garde, mudling!",
	"Bog Zombie|hp=35|dmg=5-9|spd=5|aspd=9|ai=melee|move=3|tier=2|fame=10|undead|drop=Bone:600|spr=zombie|col=moss|snd=groan",
	"Big Mama Snapper|hp=420|dmg=12-22|armor=8|spd=3|aspd=8|ai=boss|move=3|tier=3|fame=300|gold=200-400|boss|summon=Snapping Turtle|drop=Mossy Relic Scale:1000,Big Mama's Shell:1000|spr=bigmama|col=moss|light=2|quip=WHO'S BEEN SWIMMING IN MY BAYOU?",
	"Drowned Sailor|hp=50|dmg=7-13|spd=3|aspd=8|ai=melee|move=3|tier=3|fame=18|undead|gold=10-30|drop=Bone:500|spr=skeleton|col=bone|quip=Yo ho... ho...",
	"Electric Eel|hp=35|dmg=5-12|spd=1|aspd=6|ai=melee|move=2|tier=3|fame=16|spr=eel|col=cyan|light=2|snd=zap",
	"Merrow Spearman|hp=60|dmg=8-14|armor=3|spd=2|aspd=8|ai=pack|move=3|tier=3|fame=20|gold=10-30|drop=Kelp:600,Black Pearl:300|spr=merrow|col=teal",
	"Sea Witch|hp=55|dmg=3-6|spd=3|aspd=10|ai=caster|move=3|spell=Frost Bolt|tier=3|fame=24|gold=20-50|drop=Black Pearl:800,Scroll of Frost Bolt:80|spr=witch|col=teal|quip=Such pretty scales. MINE.",
	"Angler Lurker|hp=70|dmg=10-18|spd=3|aspd=9|ai=ambush|move=2|tier=3|fame=26|light=3|drop=Pearl Necklace:40|spr=angler|col=navy",
	"The Drowned King|hp=720|dmg=14-24|armor=8|spd=3|aspd=8|ai=boss|move=3|spell=Frost Bolt|tier=4|fame=500|gold=300-600|boss|summon=Drowned Sailor|drop=Pearl Relic Scale:1000,Trident of the Drowned King:1000|spr=drownking|col=pearl|light=3|quip=KNEEL BEFORE THE TIDE!",
	"Will-o'-Wisp|hp=30|dmg=3-6|spd=2|aspd=10|ai=coward|move=4|spell=Magic Arrow|tier=3|fame=16|light=4|drop=Ectoplasm:400|spr=wisp|col=wisp|quip=This way... this way...",
	"Swamp Zombie|hp=55|dmg=8-13|spd=5|aspd=10|ai=pack|move=3|tier=3|fame=14|undead|drop=Bone:600|spr=zombie|col=gray|snd=groan",
	"Banshee|hp=60|dmg=4-8|spd=2|aspd=11|ai=caster|move=4|spell=Lightning|tier=3|fame=25|undead|drop=Ectoplasm:700|spr=banshee|col=ghost|quip=EEEEEEEEE!",
	"Ghoul Gator|hp=80|dmg=10-16|spd=3|aspd=8|ai=melee|move=3|tier=3|fame=28|undead|leech=30|drop=Bone:500|spr=ghoulgator|col=ghoul|quip=We could have been friends.",
	"Bone Heron|hp=45|dmg=8-12|spd=2|aspd=8|ai=melee|move=4|tier=3|fame=18|undead|drop=Feather:800,Bone:500|spr=heron|col=bone",
	"Count Crocula|hp=900|dmg=18-30|armor=6|spd=2|aspd=8|ai=boss|move=3|spell=Poison|tier=4|fame=650|gold=400-800|boss|leech=40|summon=Will-o'-Wisp|drop=Shadow Relic Scale:1000,Crocula's Cape:1000|spr=crocula|col=crimson|quip=I vant to bite your... tail.",
	"Ice Bat|hp=30|dmg=5-10|spd=1|aspd=6|ai=pack|move=4|tier=4|fame=12|spr=bat|col=ice|snd=screech",
	"Yeti|hp=110|dmg=12-22|armor=4|spd=3|aspd=9|ai=melee|tier=4|fame=40|frostproof|drop=Hide:900,Frost Shard:200|spr=yeti|col=white|quip=RAAARGH (politely)",
	"Frost Wisp|hp=45|dmg=4-8|spd=2|aspd=10|ai=caster|move=4|spell=Frost Bolt|tier=4|fame=26|frostproof|light=2|drop=Frost Shard:500|spr=wisp|col=ice",
	"Penguin Mercenary|hp=50|dmg=9-14|armor=3|spd=2|aspd=7|ai=pack|move=3|tier=4|fame=22|frostproof|gold=15-40|drop=Raw Fish:800|spr=penguin|col=black|quip=Noot noot!",
	"Snow Troll|hp=140|dmg=14-24|armor=4|spd=4|aspd=10|ai=melee|tier=4|fame=45|frostproof|regen=2|drop=Hide:800|spr=troll|col=snow",
	"The Glacier Queen|hp=1100|dmg=15-26|armor=10|spd=3|aspd=8|ai=boss|move=1|spell=Frost Bolt|tier=5|fame=800|gold=500-900|boss|frostproof|summon=Frost Wisp|drop=Frost Relic Scale:1000,Glacier Queen's Tiara:1000|spr=glacierqueen|col=ice|light=4|quip=Your blood will make a lovely sorbet.",
	"Fire Imp|hp=55|dmg=4-8|spd=2|aspd=10|ai=coward|spell=Fireball|tier=5|fame=26|fireproof|light=2|drop=Sulfurous Ash:900|spr=imp|col=lava|quip=Hehehehe, crispy!",
	"Salamander|hp=80|dmg=12-20|spd=2|aspd=8|ai=melee|tier=5|fame=34|fireproof|light=2|drop=Magma Core:120|spr=salamander|col=orange",
	"Magma Golem|hp=200|dmg=18-30|armor=12|spd=6|aspd=12|ai=melee|tier=5|fame=60|fireproof|light=3|drop=Magma Core:500,Iron Ore:900|spr=golem|col=lava|snd=rumble",
	"Ash Bat|hp=40|dmg=8-14|spd=1|aspd=6|ai=pack|move=4|tier=5|fame=16|fireproof|spr=bat|col=char|snd=screech",
	"Cinder Cultist|hp=90|dmg=6-10|spd=3|aspd=10|ai=caster|spell=Flamestrike|tier=5|fame=40|fireproof|gold=30-80|drop=Scroll of Flamestrike:60,Sulfurous Ash:900|spr=cultist|col=crimson|quip=The mountain hungers!",
	"Emberjaw the Wyrm|hp=1500|dmg=18-30|armor=12|spd=3|aspd=8|ai=boss|spell=Fireball|tier=6|fame=1200|gold=800-1500|boss|fireproof|summon=Fire Imp|drop=Ember Relic Scale:1000,Emberjaw's Fang:1000|spr=emberjaw|col=lava|light=5|quip=A GATOR? I EAT GATORS FOR BRUNCH.",
	"Harpy|hp=70|dmg=10-16|spd=2|aspd=9|ai=ranged|rng=6|move=4|tier=5|fame=28|drop=Feather:900|spr=harpy|col=tan|quip=SKREEEE!",
	"Storm Elemental|hp=120|dmg=8-14|spd=2|aspd=11|ai=caster|move=4|spell=Lightning|tier=5|fame=44|light=3|drop=Storm Essence:400|spr=stormel|col=cyan|snd=thunder",
	"Griffon|hp=160|dmg=18-28|armor=4|spd=2|aspd=8|ai=melee|move=4|tier=5|fame=50|drop=Feather:1000|spr=griffon|col=gold",
	"Sky Pirate|hp=110|dmg=14-22|spd=3|aspd=10|ai=ranged|rng=7|tier=5|fame=40|gold=40-100|drop=Arrow:800,Crossbow:60|spr=pirate|col=navy|quip=Yarr, fall off my island!",
	"The Thunder Roc|hp=1800|dmg=19-32|armor=10|spd=2|aspd=8|ai=boss|move=4|spell=Chain Lightning|tier=6|fame=1500|gold=900-1800|boss|summon=Harpy|drop=Storm Relic Scale:1000,Stormcaller:1000|spr=roc|col=gold|light=3|quip=SKRAAAAAAWK!",
	"Frog Familiar|hp=60|dmg=6-12|spd=2|aspd=7|ai=summon|fac=player|move=3|tier=1|spr=frog_big|col=gold|quip=Ribbit! (determined)"
}

RECIPE_SRC = { [0] =
	"Iron Ingot|forge|smithing|0|Iron Ore:1|1",
	"Silver Ingot|forge|smithing|300|Silver Ore:1|1",
	"Mithril Ingot|forge|smithing|650|Mithril Ore:1|1",
	"Short Sword|anvil|smithing|0|Iron Ingot:3|1",
	"Frying Pan|anvil|smithing|50|Iron Ingot:2|1",
	"Cutlass|anvil|smithing|200|Iron Ingot:5|1",
	"Mace|anvil|smithing|250|Iron Ingot:6|1",
	"Broadsword|anvil|smithing|350|Iron Ingot:8|1",
	"War Hammer|anvil|smithing|450|Iron Ingot:10|1",
	"Katana|anvil|smithing|550|Silver Ingot:6,Iron Ingot:2|1",
	"Gator Tooth Blade|anvil|smithing|750|Mithril Ingot:5,Bone:4|1",
	"Obsidian Greatsword|anvil|smithing|900|Mithril Ingot:8,Magma Core:2|1",
	"Bucket Helm|anvil|smithing|50|Iron Ingot:3|1",
	"Iron Helm|anvil|smithing|300|Iron Ingot:5|1",
	"Chainmail Shirt|anvil|smithing|350|Iron Ingot:10|1",
	"Chain Gloves|anvil|smithing|300|Iron Ingot:4|1",
	"Chain Leggings|anvil|smithing|350|Iron Ingot:7|1",
	"Plate Cuirass|anvil|smithing|700|Iron Ingot:16,Silver Ingot:2|1",
	"Plate Gauntlets|anvil|smithing|650|Iron Ingot:6|1",
	"Plate Greaves|anvil|smithing|700|Iron Ingot:12|1",
	"Plate Boots|anvil|smithing|650|Iron Ingot:6|1",
	"Kite Shield|anvil|smithing|450|Iron Ingot:8|1",
	"Tower Shield|anvil|smithing|750|Iron Ingot:14|1",
	"Wooden Shield|anvil|lumber|100|Log:6|1",
	"Short Bow|anvil|lumber|150|Log:5|1",
	"Long Bow|anvil|lumber|450|Log:9,Feather:2|1",
	"Crossbow|anvil|lumber|650|Log:10,Iron Ingot:3|1",
	"Arrow|anvil|lumber|0|Log:1,Feather:1|10",
	"Club|anvil|lumber|0|Log:3|1",
	"Cooked Fish|fire|cooking|0|Raw Fish:1|1",
	"Blackened Catfish|fire|cooking|250|Catfish:1,Garlic:1|1",
	"Fried Frog Legs|fire|cooking|100|Frog Legs:1|1",
	"Crawdad Etouffee|fire|cooking|400|Crawdad:2,Garlic:1,Bread:1|1",
	"Toasted Marshmallow|fire|cooking|0|Marshmallow:1|1",
	"Smoked Eel|fire|cooking|600|Lava Eel:1|1",
	"Bread|fire|cooking|50|Ginseng:1|2",
	"Minor Heal Potion|anywhere|alchemy|0|Ginseng:1|1|Mortar and Pestle",
	"Cure Potion|anywhere|alchemy|100|Garlic:2|1|Mortar and Pestle",
	"Night Sight Potion|anywhere|alchemy|150|Spider Silk:1,Sulfurous Ash:1|1|Mortar and Pestle",
	"Heal Potion|anywhere|alchemy|300|Ginseng:2,Garlic:1|1|Mortar and Pestle",
	"Mana Potion|anywhere|alchemy|350|Black Pearl:1,Spider Silk:1|1|Mortar and Pestle",
	"Swamp Tonic|anywhere|alchemy|400|Kelp:2,Ginseng:1|1|Mortar and Pestle",
	"Strength Potion|anywhere|alchemy|500|Mandrake Root:2,Bloodmoss:1|1|Mortar and Pestle",
	"Greater Heal Potion|anywhere|alchemy|650|Ginseng:3,Garlic:1,Mandrake Root:1|1|Mortar and Pestle",
	"Fire Resist Potion|anywhere|alchemy|700|Magma Core:1,Garlic:2|1|Mortar and Pestle",
	"Swamp Gas Bomb|anywhere|alchemy|550|Sulfurous Ash:2,Kelp:1|1|Mortar and Pestle",
	"Bandage|anywhere|healing|0|Hide:1|5"
}

WORLD_SRC = { [0] =
	"Gatorland|w=128|h=96|gen=0|tier=1|amb=0|mus=0|wx=ccccrrft|col=green|b=The green heart of the swamp. Home.",
	"The Bayou|w=96|h=72|gen=1|tier=2|amb=0|mus=1|wx=crrftt|col=moss|boss=Big Mama Snapper|b=Cypress knees and bad intentions.",
	"Sunken City of Sogg|w=88|h=72|gen=2|tier=3|amb=7|mus=2|wx=cccf|col=teal|boss=The Drowned King|b=A drowned city. Hold your breath. Gators can.",
	"Gloomfen|w=96|h=72|gen=3|tier=3|amb=4|mus=3|wx=fffr|col=ghoul|boss=Count Crocula|b=The fog here has opinions.",
	"Frostfang Caverns|w=88|h=64|gen=4|tier=4|amb=5|mus=4|wx=sscs|col=ice|boss=The Glacier Queen|b=Cold-blooded is not a compliment down here.",
	"Mt. Scorchmaw|w=88|h=72|gen=5|tier=5|amb=9|mus=5|wx=aaca|col=lava|boss=Emberjaw the Wyrm|b=The mountain breathes. Try not to be what it exhales.",
	"Cloudreach|w=100|h=72|gen=6|tier=5|amb=14|mus=6|wx=cctt|col=sky|boss=The Thunder Roc|b=Islands in the sky. Mind the gap.",
	"Swampton Sewers|w=64|h=48|gen=7|tier=1|amb=3|mus=7|wx=c|col=slime|boss=The Rat King|b=It smells exactly how you think."
}

ROLE_SRC = { [0] =
	"Gus|the Weaponsmith|shop=0|lines=Sharp things! Get your sharp things!;Every gator needs a good blade. Or a frying pan.",
	"Marguerite|the Armorer|shop=1|lines=Scales are nice, but plate is nicer.;I once made a helm for a frog. Never again.",
	"Madame Hoodoo|the Alchemist|shop=2|lines=Potions, powders, reagents. No refunds on curses.;Bring me mandrake, cher.",
	"Boudreaux|the Provisioner|shop=3|lines=Tools, food, arrows. All you need for the swamp life.;You look hungry. You always look hungry.",
	"Thibodeaux|the Fishmonger|shop=4|lines=Fish! Fresh-ish fish!;I would give a fortune for a Golden Carp.",
	"Penny|the Banker|banks|lines=Your gold is safe with Swampton Savings and Loan.;We have not been robbed in almost a week.",
	"Sister Clementine|the Healer|heals|lines=Bless your scaly heart.;Ghosts to the front, please. No cutting.",
	"Mayor Beauregard|the Mayor|lines=Welcome to Swampton, finest swamp in the swamp!;We have a rat problem. And a raccoon problem. And a problem problem.",
	"Captain Fontenot|of the Guard|lines=Keep your claws to yourself in town.;Raccoons. I hate raccoons.",
	"Oracle Mireweather|the Shrine Keeper|heals|lines=Six realms, six scales, one gator.;The Moongates hum when a hero is near."
}

SHOP_SRC = { [0] =
	"Rusty Dagger,Short Sword,Cutlass,Broadsword,Club,Frying Pan,Mace,War Hammer,Short Bow,Long Bow,Arrow,Apprentice Wand",
	"Straw Hat,Leather Cap,Bucket Helm,Iron Helm,Leather Tunic,Chainmail Shirt,Leather Gloves,Leather Leggings,Chain Leggings,Sandals,Leather Boots,Swamp Waders,Pot Lid,Wooden Shield,Kite Shield",
	"Minor Heal Potion,Heal Potion,Mana Potion,Cure Potion,Night Sight Potion,Black Pearl,Bloodmoss,Garlic,Ginseng,Mandrake Root,Nightshade,Spider Silk,Sulfurous Ash,Spellbook,Scroll of Heal,Scroll of Magic Arrow,Scroll of Night Sight,Scroll of Cure,Scroll of Frost Bolt,Scroll of Fireball,Scroll of Recall,Mortar and Pestle,Recall Rune",
	"Bread,Cheese Wheel,Moon Pie,Marshmallow,Hot Sauce,Fishing Pole,Pickaxe,Hatchet,Smith Hammer,Bandage,Arrow,Kazoo",
	"Raw Fish,Catfish,Cooked Fish,Fishing Pole,Kelp"
}

QUEST_SRC = { [0] =
	"Rats in the Cellar|giver=Mayor Beauregard|type=0|target=Sewer Rat|count=8|gold=60|reward=Leather Cap|o=The sewers under town are crawling. Squash 8 Sewer Rats for me, would you?|d=Splendid! The cellar smells almost tolerable now.",
	"The Masked Menace|giver=Captain Fontenot|type=0|target=Raccoon Bandit|count=3|gold=80|reward=Lucky Frog Charm|o=Raccoon Bandits keep lifting purses outside town. Teach 3 of them some manners.|d=Ha! That will show the little burglars.",
	"Frog Legs Friday|giver=Boudreaux|type=1|target=Frog Legs|count=5|gold=50|reward=Fried Frog Legs|rq=3|o=Friday is frog leg night and I am plumb out. Bring me 5 Frog Legs?|d=Now THAT is a Friday. Here, have some on the house.",
	"Gone Fishin'|giver=Thibodeaux|type=5|count=10|gold=100|reward=Fish Slapper|o=Catch 10 fish, any fish, and I will teach you the ancient art of the slap.|d=A natural! Take this. Treat it with respect.",
	"Iron Will|giver=Gus|type=4|count=1|gold=40|reward=Iron Ingot|rq=10|o=Smelt some ore at the forge and make anything at the anvil. Show me what you have got.|d=Not bad for a gator with no thumbs.",
	"Reagent Run|giver=Madame Hoodoo|type=1|target=Mandrake Root|count=3|gold=70|reward=Scroll of Fireball|o=Bring me 3 Mandrake Root, cher. Reagents grow wild all over Gatorland.|d=Mmm, fresh. Here is a little something that goes whoosh.",
	"The Rat King|giver=Mayor Beauregard|type=2|target=The Rat King|count=1|gold=150|reward=Heal Potion|rq=3|need=0|o=The rats have a KING. Down in the deepest sewer. End his reign!|d=Long live... nobody! Swampton owes you.",
	"Tourist|giver=Oracle Mireweather|type=3|count=8|gold=200|reward=Rubber Duck|o=Walk every realm the Moongates touch. All eight. The swamp wants to be seen.|d=You have seen it all. Take this. It is more powerful than it looks.",
	"Big Mama|giver=Oracle Mireweather|type=2|target=Big Mama Snapper|count=1|gold=250|reward=Greater Heal Potion|rq=3|o=Big Mama Snapper rules the Bayou and she bites. Take her Relic Scale.|d=The first scale. Five remain.",
	"The Six Scales|giver=Oracle Mireweather|type=1|target=Mossy Relic Scale|count=1|gold=1000|reward=Crown of the Swamp|need=8|o=Gather all six Relic Scales from the realm tyrants and return to the Shrine. Become the Gator Avatar.|d=The swamp crowns you. Rise, Gator Avatar!"
}

-- ------------------------------------------------------------------ tiles ---
local function build_tiles()
	local out = {}
	local names = {}
	local n = alen(TILE_SRC)
	for i = 0, n - 1 do
		local p = split(TILE_SRC[i], "|")
		names[i] = p[0]
	end
	for i = 0, n - 1 do
		local p, pn = split(TILE_SRC[i], "|")
		local t = TileDef()
		t.name = p[0]
		t.walk = dsl_int(p, pn, "walk", 1)
		t.opaque = dsl_has(p, pn, "opaque")
		t.hide = dsl_has(p, pn, "hide")
		t.light = dsl_int(p, pn, "light", 0)
		t.res = dsl_int(p, pn, "res", 0)
		t.station = dsl_int(p, pn, "st", 0)
		local after = dsl_get(p, pn, "after", "")
		t.after = after == "" and -1 or index_of_name(names, after, -1)
		out[i] = t
	end
	return out
end

TILES_DEF = build_tiles()

function tid(name)
	for i = 0, alen(TILES_DEF) - 1 do
		if TILES_DEF[i].name == name then return i end
	end
	error("unknown tile: " .. name)
end

T_VOID = tid("void")
T_GRASS = tid("grass")
T_TALLGRASS = tid("tall grass")
T_FLOWERS = tid("flowers")
T_DIRT = tid("dirt")
T_ROAD = tid("road")
T_SAND = tid("sand")
T_SHALLOW = tid("shallows")
T_DEEP = tid("deep water")
T_SWAMP = tid("swamp water")
T_MUD = tid("mud")
T_OAK = tid("oak")
T_PINE = tid("pine")
T_CYPRESS = tid("cypress")
T_DEADTREE = tid("dead tree")
T_BUSH = tid("bush")
T_BOULDER = tid("boulder")
T_ORE = tid("ore vein")
T_MOUNTAIN = tid("mountain")
T_CAVEWALL = tid("cave wall")
T_CAVEFLOOR = tid("cave floor")
T_WOODFLOOR = tid("wood floor")
T_STONEFLOOR = tid("stone floor")
T_BRICK = tid("brick wall")
T_WOODWALL = tid("wood wall")
T_DOOR = tid("door")
T_COUNTER = tid("counter")
T_ANVIL = tid("anvil")
T_FORGE = tid("forge")
T_FIRE = tid("fire pit")
T_ALCHEMY = tid("alchemy table")
T_BANK = tid("bank chest")
T_BED = tid("bed")
T_FENCE = tid("fence")
T_BRIDGE = tid("bridge")
T_LAVA = tid("lava")
T_OBSIDIAN = tid("obsidian")
T_ASH = tid("ash")
T_ICE = tid("ice")
T_SNOW = tid("snow")
T_ICEWALL = tid("ice wall")
T_CLOUD = tid("cloud")
T_SKY = tid("sky")
T_RAINBOW = tid("rainbow")
T_RUINFLOOR = tid("ruin floor")
T_RUINWALL = tid("ruin wall")
T_CORAL = tid("coral")
T_KELP = tid("kelp")
T_LILY = tid("lily pad")
T_REEDS = tid("reeds")
T_GRAVE = tid("grave")
T_BOG = tid("bog")
T_GATE = tid("moongate")
T_STAIRS_DN = tid("stairs down")
T_STAIRS_UP = tid("stairs up")
T_SHRINE = tid("shrine")
T_CRYSTAL = tid("crystal")
T_STUMP = tid("stump")
T_RUBBLE = tid("rubble")
T_CHEST = tid("chest")
T_SIGN = tid("sign")
T_SCONCE = tid("sconce")
T_SEWAGE = tid("sewage")
T_SEWERFLOOR = tid("sewer floor")
T_CARPET = tid("carpet")
T_WELL = tid("well")
T_BASALT = tid("basalt")
T_PILLAR = tid("pillar")
T_GOLDPILE = tid("gold pile")
T_WEB = tid("web")

-- flat mirrors for the hot paths
T_WALK = {}
T_OPAQUE = {}
T_LIGHT = {}
for i = 0, alen(TILES_DEF) - 1 do
	T_WALK[i] = TILES_DEF[i].walk
	T_OPAQUE[i] = TILES_DEF[i].opaque and 1 or 0
	T_LIGHT[i] = TILES_DEF[i].light
end

-- ------------------------------------------------------------------ items ---
local function build_items()
	local out = {}
	local rarities = { [0] = "common", "uncommon", "rare", "epic", "legendary" }
	for i = 0, alen(ITEM_SRC) - 1 do
		local p, pn = split(ITEM_SRC[i], "|")
		local d = ItemDef()
		d.name = p[0]
		d.kind = index_of_name(KIND_NAME, p[1], 11)
		d.slot = index_of_name(SLOT_NAME, dsl_get(p, pn, "slot", ""), -1)
		if d.kind == Kind.weapon then d.slot = Slot.weapon end
		d.lo, d.hi = dsl_range(p, pn, "dmg", 0, 0)
		d.armor = dsl_int(p, pn, "arm", 0)
		d.speed = dsl_int(p, pn, "spd", 8)
		d.skill = index_of_name(SKILL_KEY, dsl_get(p, pn, "sk", "wrestling"), 3)
		d.range = dsl_int(p, pn, "rng", 1)
		d.value = dsl_int(p, pn, "val", 1)
		d.tier = dsl_int(p, pn, "tier", 1)
		d.eff = index_of_name(EFF_NAME, dsl_get(p, pn, "eff", "none"), 0)
		d.pow = dsl_int(p, pn, "pow", 0)
		d.stack = dsl_has(p, pn, "stack")
		d.rarity = index_of_name(rarities, dsl_get(p, pn, "rar", "common"), 0)
		d.spr = dsl_get(p, pn, "spr", "bag")
		d.col = dsl_get(p, pn, "col", "white")
		d.desc = dsl_get(p, pn, "d", "")
		local aff = dsl_get(p, pn, "aff", "")
		if aff ~= "" then
			local pairs_, npairs = split(aff, ",")
			for k = 0, npairs - 1 do
				local cv, cvn = split(pairs_[k], ":")
				local code = index_of_name(AF_NAME, cv[0], -1)
				if code >= 0 and cvn > 1 then
					d.aff_code[d.aff_n] = code
					d.aff_val[d.aff_n] = to_int(cv[1], 0)
					d.aff_n = d.aff_n + 1
				end
			end
		end
		out[i] = d
	end
	return out
end

ITEMS = build_items()
ITEM_N = alen(ITEMS)

function iid(name)
	for i = 0, ITEM_N - 1 do
		if ITEMS[i].name == name then return i end
	end
	error("unknown item: " .. name)
end

I_DAGGER = iid("Rusty Dagger")
I_SHORTSWORD = iid("Short Sword")
I_SHORTBOW = iid("Short Bow")
I_ARROW = iid("Arrow")
I_BANDAGE = iid("Bandage")
I_SPELLBOOK = iid("Spellbook")
I_FISHPOLE = iid("Fishing Pole")
I_PICKAXE = iid("Pickaxe")
I_HATCHET = iid("Hatchet")
I_HAMMER = iid("Smith Hammer")
I_MORTAR = iid("Mortar and Pestle")
I_MINORHEAL = iid("Minor Heal Potion")
I_HEAL = iid("Heal Potion")
I_MANA = iid("Mana Potion")
I_CURE = iid("Cure Potion")
I_BLACKPEARL = iid("Black Pearl")
I_BLOODMOSS = iid("Bloodmoss")
I_GARLIC = iid("Garlic")
I_GINSENG = iid("Ginseng")
I_MANDRAKE = iid("Mandrake Root")
I_NIGHTSHADE = iid("Nightshade")
I_SILK = iid("Spider Silk")
I_ASH = iid("Sulfurous Ash")
I_IRONORE = iid("Iron Ore")
I_SILVERORE = iid("Silver Ore")
I_MITHRILORE = iid("Mithril Ore")
I_IRONINGOT = iid("Iron Ingot")
I_LOG = iid("Log")
I_FEATHER = iid("Feather")
I_RAWFISH = iid("Raw Fish")
I_CATFISH = iid("Catfish")
I_ICEFISH = iid("Ice Fish")
I_CLOUDTROUT = iid("Cloud Trout")
I_GHOSTFISH = iid("Ghost Fish")
I_LAVAEEL = iid("Lava Eel")
I_GOLDCARP = iid("Golden Carp")
I_FROGLEGS = iid("Frog Legs")
I_CRAWDAD = iid("Crawdad")
I_OLDBOOT = iid("Old Boot")
I_TINCAN = iid("Tin Can")
I_SOGGYHAT = iid("Soggy Hat")
I_BOTTLE = iid("Message in a Bottle")
I_RATTAIL = iid("Rat Tail")
I_LEATHERTUNIC = iid("Leather Tunic")
I_STRAWHAT = iid("Straw Hat")
I_SANDALS = iid("Sandals")
I_POTLID = iid("Pot Lid")
I_BREAD = iid("Bread")
I_KELP = iid("Kelp")
I_RUBBERDUCK = iid("Rubber Duck")
I_GOLD = iid("Gold Coins")
I_CROWN_SWAMP = iid("Crown of the Swamp")
RELICS = { [0] = iid("Mossy Relic Scale"), iid("Pearl Relic Scale"), iid("Shadow Relic Scale"),
	iid("Frost Relic Scale"), iid("Ember Relic Scale"), iid("Storm Relic Scale") }
REAGENTS = { [0] = I_BLACKPEARL, I_BLOODMOSS, I_GARLIC, I_GINSENG, I_MANDRAKE, I_NIGHTSHADE, I_SILK, I_ASH }

-- ---------------------------------------------------------------- affixes ---
local function build_affixes()
	local out = {}
	for i = 0, alen(AFFIX_SRC) - 1 do
		local p = split(AFFIX_SRC[i], "|")
		local a = AffixDef()
		a.name = p[0]
		a.prefix = p[1] == "pre"
		a.applies = p[2] == "weapon" and 1 or (p[2] == "armor" and 2 or 0)
		a.code = index_of_name(AF_NAME, p[3], 0)
		local r = p[4]
		local dash = string.find(r, "-", 2, true) - 1   -- r.find("-", 1), 0-based
		a.lo = to_int(string.sub(r, 1, dash), 1)
		a.hi = to_int(string.sub(r, dash + 2), 1)
		a.tier = to_int(p[5], 1)
		out[i] = a
	end
	return out
end

AFFIXES = build_affixes()
AFFIX_N = alen(AFFIXES)

-- ---------------------------------------------------------------- spells ---
local function build_spells()
	local out = {}
	for i = 0, alen(SPELL_SRC) - 1 do
		local p, pn = split(SPELL_SRC[i], "|")
		local s = SpellDef()
		s.name = p[0]
		s.circle = dsl_int(p, pn, "circle", 1)
		s.mana = dsl_int(p, pn, "mana", 4)
		local regs, nregs = split(dsl_get(p, pn, "reg", ""), ",")
		for r = 0, nregs - 1 do
			if regs[r] ~= "" then
				s.reg[s.reg_n] = iid(regs[r])
				s.reg_n = s.reg_n + 1
			end
		end
		s.target = dsl_int(p, pn, "tgt", 0)
		s.eff = index_of_name(SPELL_EFF, dsl_get(p, pn, "eff", "dmg"), 1)
		s.lo, s.hi = dsl_range(p, pn, "pow", 0, 0)
		s.radius = dsl_int(p, pn, "rad", 0)
		s.range = dsl_int(p, pn, "rng", 8)
		s.fx = index_of_name(FX_NAME, dsl_get(p, pn, "fx", "magic"), 9)
		s.snd = dsl_get(p, pn, "snd", "zap")
		s.desc = dsl_get(p, pn, "d", "")
		out[i] = s
	end
	return out
end

SPELLS = build_spells()
SPELL_N = alen(SPELLS)

function sid(name)
	for i = 0, SPELL_N - 1 do
		if SPELLS[i].name == name then return i end
	end
	error("unknown spell: " .. name)
end

-- -------------------------------------------------------------- monsters ---
local function build_monsters()
	local out = {}
	local names = {}
	local n = alen(MON_SRC)
	for i = 0, n - 1 do
		local p = split(MON_SRC[i], "|")
		names[i] = p[0]
	end
	for i = 0, n - 1 do
		local p, pn = split(MON_SRC[i], "|")
		local m = MonDef()
		m.name = p[0]
		m.hp = dsl_int(p, pn, "hp", 10)
		m.lo, m.hi = dsl_range(p, pn, "dmg", 1, 3)
		m.armor = dsl_int(p, pn, "armor", 0)
		m.speed = dsl_int(p, pn, "spd", 3)
		m.aspeed = dsl_int(p, pn, "aspd", 8)
		m.ai = index_of_name(AI_NAME, dsl_get(p, pn, "ai", "melee"), 2)
		m.fac = index_of_name(FAC_NAME, dsl_get(p, pn, "fac", "monster"), 2)
		m.range = dsl_int(p, pn, "rng", 1)
		local sp = dsl_get(p, pn, "spell", "")
		m.spell = sp == "" and -1 or sid(sp)
		m.move = dsl_int(p, pn, "move", 1)
		m.tier = dsl_int(p, pn, "tier", 1)
		m.skill = dsl_int(p, pn, "skill", 250 + m.tier * 120)
		m.gold_lo, m.gold_hi = dsl_range(p, pn, "gold", 0, 0)
		m.fame = dsl_int(p, pn, "fame", 1)
		m.light = dsl_int(p, pn, "light", 0)
		local su = dsl_get(p, pn, "summon", "")
		m.summon = su == "" and -1 or index_of_name(names, su, -1)
		local spl = dsl_get(p, pn, "split", "")
		m.split = spl == "" and -1 or index_of_name(names, spl, -1)
		m.undead = dsl_has(p, pn, "undead")
		m.fireproof = dsl_has(p, pn, "fireproof")
		m.frostproof = dsl_has(p, pn, "frostproof")
		m.poisonous = dsl_has(p, pn, "poisonous")
		m.boss = dsl_has(p, pn, "boss")
		m.regen = dsl_int(p, pn, "regen", 0)
		m.leech = dsl_int(p, pn, "leech", 0)
		local drops = dsl_get(p, pn, "drop", "")
		if drops ~= "" then
			local dl, dln = split(drops, ",")
			for k = 0, dln - 1 do
				local ic, icn = split(dl[k], ":")
				m.drop_item[m.drop_n] = iid(ic[0])
				m.drop_chance[m.drop_n] = icn > 1 and to_int(ic[1], 500) or 500
				m.drop_n = m.drop_n + 1
			end
		end
		m.spr = dsl_get(p, pn, "spr", "rat")
		m.col = dsl_get(p, pn, "col", "gray")
		m.snd = dsl_get(p, pn, "snd", "")
		m.quip = dsl_get(p, pn, "quip", "")
		out[i] = m
	end
	return out
end

MONS = build_monsters()
MON_N = alen(MONS)

function mid(name)
	for i = 0, MON_N - 1 do
		if MONS[i].name == name then return i end
	end
	error("unknown monster: " .. name)
end

M_GATOR_NPC = mid("Townsgator")
M_FROGFOLK = mid("Frogfolk")
M_TURTLE_NPC = mid("Old Turtle")
M_GUARD = mid("Swampton Guard")
M_FAMILIAR = mid("Frog Familiar")

-- flat mirrors for the AI hot path
MON_SPEED = {}
MON_MOVE = {}
MON_AI = {}
MON_FAC = {}
MON_ASPEED = {}
for i = 0, MON_N - 1 do
	local m = MONS[i]
	MON_SPEED[i] = m.speed
	MON_MOVE[i] = m.move
	MON_AI[i] = m.ai
	MON_FAC[i] = m.fac
	MON_ASPEED[i] = m.aspeed
end

-- ---------------------------------------------------------------- recipes ---
local function build_recipes()
	local out = {}
	for i = 0, alen(RECIPE_SRC) - 1 do
		local p, pn = split(RECIPE_SRC[i], "|")
		local r = RecipeDef()
		r.out = iid(p[0])
		r.station = p[1] == "anywhere" and 0 or index_of_name(STATION_NAME, p[1], 0)
		r.skill = index_of_name(SKILL_KEY, p[2], 0)
		r.min = to_int(p[3], 0)
		local ins, insn = split(p[4], ",")
		for k = 0, insn - 1 do
			local iq = split(ins[k], ":")
			r.in_item[r.in_n] = iid(iq[0])
			r.in_qty[r.in_n] = to_int(iq[1], 1)
			r.in_n = r.in_n + 1
		end
		r.out_qty = to_int(p[5], 1)
		r.tool = pn > 6 and iid(p[6]) or -1
		out[i] = r
	end
	return out
end

RECIPES = build_recipes()
RECIPE_N = alen(RECIPES)

-- ---------------------------------------------------------------- worlds ---
local function build_worlds()
	local out = {}
	for i = 0, alen(WORLD_SRC) - 1 do
		local p, pn = split(WORLD_SRC[i], "|")
		local w = WorldDef()
		w.name = p[0]
		w.w = dsl_int(p, pn, "w", 64)
		w.h = dsl_int(p, pn, "h", 48)
		w.gen = dsl_int(p, pn, "gen", 0)
		w.tier = dsl_int(p, pn, "tier", 1)
		w.ambient = dsl_int(p, pn, "amb", 0)
		w.music = dsl_int(p, pn, "mus", 0)
		local b = dsl_get(p, pn, "boss", "")
		w.boss = b == "" and -1 or mid(b)
		w.weather = dsl_get(p, pn, "wx", "c")
		w.banner = dsl_get(p, pn, "b", "")
		w.col = dsl_get(p, pn, "col", "green")
		out[i] = w
	end
	return out
end

WORLDS = build_worlds()
WORLD_COUNT = alen(WORLDS)
W_GATORLAND = 0
W_SEWERS = 7

-- ------------------------------------------------------------- npc roles ---
local function build_roles()
	local out = {}
	for i = 0, alen(ROLE_SRC) - 1 do
		local p, pn = split(ROLE_SRC[i], "|")
		local r = NpcRole()
		r.name = p[0]
		r.title = p[1]
		r.shop = dsl_int(p, pn, "shop", -1)
		r.heals = dsl_has(p, pn, "heals")
		r.banks = dsl_has(p, pn, "banks")
		r.lines = dsl_get(p, pn, "lines", "")
		out[i] = r
	end
	return out
end

ROLES = build_roles()
ROLE_N = alen(ROLES)
R_WEAPONS = 0
R_ARMOR = 1
R_ALCHEMIST = 2
R_PROVISIONER = 3
R_FISHMONGER = 4
R_BANKER = 5
R_HEALER = 6
R_MAYOR = 7
R_CAPTAIN = 8
R_ORACLE = 9

local function build_shops()
	local out = {}
	for i = 0, alen(SHOP_SRC) - 1 do
		local stock = {}
		local names, nn = split(SHOP_SRC[i], ",")
		for k = 0, nn - 1 do stock[k] = iid(names[k]) end
		out[i] = stock
	end
	return out
end

SHOPS = build_shops()

-- ---------------------------------------------------------------- quests ---
local function build_quests()
	local out = {}
	for i = 0, alen(QUEST_SRC) - 1 do
		local p, pn = split(QUEST_SRC[i], "|")
		local q = QuestDef()
		q.name = p[0]
		local giver = dsl_get(p, pn, "giver", "")
		for r = 0, ROLE_N - 1 do
			if ROLES[r].name == giver then q.giver = r end
		end
		q.type = dsl_int(p, pn, "type", 0)
		local tgt = dsl_get(p, pn, "target", "")
		if tgt ~= "" then
			q.target = q.type == 1 and iid(tgt) or mid(tgt)
		end
		q.count = dsl_int(p, pn, "count", 1)
		q.gold = dsl_int(p, pn, "gold", 0)
		local rw = dsl_get(p, pn, "reward", "")
		q.reward = rw == "" and -1 or iid(rw)
		q.reward_qty = dsl_int(p, pn, "rq", 1)
		q.needs = dsl_int(p, pn, "need", -1)
		q.offer = dsl_get(p, pn, "o", "")
		q.done = dsl_get(p, pn, "d", "")
		out[i] = q
	end
	return out
end

QUESTS = build_quests()
QUEST_N = alen(QUESTS)
Q_SIX_SCALES = 9
