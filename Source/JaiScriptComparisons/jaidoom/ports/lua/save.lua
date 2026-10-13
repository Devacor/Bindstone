-- Generated from save.jai by tools/transpile.py; see internal/examples/jaidoom/ports/PORTING.md.
require("wad")
require("map")
require("tex")
require("render")
require("things")
require("spec")
require("anim")
require("game")
SAVE_PATH = "jaidoom_save.json"
SAVE_VERSION = 1
function game_save(path)
	path = data_path(path)
	if (GAME_MAP == "") then
		return false
	end
	local doors = _j_array({})
	do
		local i = 0
		while (i < (#DOORS)) do
			do
				local d = DOORS[i + 1]
				table.insert(doors, _j_array({d.sec, d.state, d.timer, d.top, d.stay}))
			end
			::_jd_continue_1::
			i = i + 1
		end
	end
	local movers = _j_array({})
	do
		local j = 0
		while (j < (#MOVERS)) do
			do
				local mv = MOVERS[j + 1]
				table.insert(movers, _j_array({mv.sec, mv.plane, mv.target, mv.speed, mv.wait, mv.ret}))
			end
			::_jd_continue_2::
			j = j + 1
		end
	end
	local st = {["version"]=SAVE_VERSION, ["mapname"]=GAME_MAP, ["skill"]=SKILL_BIT, ["nightmare"]=NIGHTMARE, ["fast"]=FAST_MONSTERS, ["tic"]=GAME_TIC, ["level_tic"]=LEVEL_START_TIC, ["spec_tic0"]=SPEC_TIC0, ["player"]={["x"]=P_X, ["y"]=P_Y, ["z"]=P_Z, ["ang"]=P_ANG, ["momx"]=P_MOMX, ["momy"]=P_MOMY, ["momz"]=P_MOMZ, ["floorz"]=(P_FLOORZ * 1.0), ["viewz"]=(P_VIEWZ * 1.0), ["air"]=P_AIR, ["hp"]=P_HP, ["armor"]=P_ARMOR, ["keys"]=P_KEYS, ["bullets"]=AMMO_BULLETS, ["shells"]=AMMO_SHELLS, ["rockets"]=AMMO_ROCKETS, ["bmax"]=AMMO_BULLETS_MAX, ["smax"]=AMMO_SHELLS_MAX, ["rmax"]=AMMO_ROCKETS_MAX, ["shotgun"]=HAVE_SHOTGUN, ["chaingun"]=HAVE_CHAINGUN, ["launcher"]=HAVE_LAUNCHER, ["berserk"]=P_BERSERK, ["invis"]=P_INVIS, ["suit"]=P_SUIT, ["goggles"]=P_GOGGLES, ["allmap"]=P_ALLMAP, ["backpack"]=P_BACKPACK, ["weapon"]=CUR_WEAPON, ["wcool"]=W_COOL, ["god"]=P_GOD, ["noclip"]=P_NOCLIP}, ["mo"]={["x"]=MO_X, ["y"]=MO_Y, ["z"]=MO_Z, ["ang"]=MO_ANG, ["momx"]=MO_MOMX, ["momy"]=MO_MOMY, ["momz"]=MO_MOMZ, ["rad"]=MO_RAD, ["type"]=MO_TYPE, ["state"]=MO_STATE, ["hp"]=MO_HP, ["frame"]=MO_FRAME, ["tic"]=MO_TIC, ["cool"]=MO_COOL, ["target"]=MO_TARGET, ["charge"]=MO_CHARGE, ["cnt"]=MO_CNT, ["shp"]=MO_SHP, ["resp"]=MO_RESP, ["anim"]=MO_ANIM, ["spr"]=MO_SPR}, ["sec"]={["floorh"]=SE_FLOORH, ["ceilh"]=SE_CEILH, ["light"]=SE_LIGHT, ["special"]=SE_SPECIAL, ["floorpic"]=SE_FLOORPIC, ["ffslot"]=SEC_FFLAT}, ["ld_special"]=LD_SPECIAL, ["seg"]={["mid"]=SEG_MIDT, ["top"]=SEG_TOPT, ["bot"]=SEG_BOTT}, ["doors"]=doors, ["movers"]=movers, ["spec"]={["secrets"]=SECRETS_FOUND, ["secret_total"]=SECRET_TOTAL, ["boss"]=BOSS_PEND}, ["anim"]={["phase"]=ANIM_PHASE, ["tick"]=ANIM_TICK, ["btn_ld"]=BTN_LD, ["btn_tics"]=BTN_TICS}, ["stats"]={["kills"]=KILLS, ["kill_total"]=KILL_TOTAL, ["items"]=ITEMS_TAKEN, ["item_total"]=ITEM_TOTAL}}
	write_file(path, to_json(st))
	return file_exists(path)
end
function ld_farr(src)
	local out = _j_array({})
	local n = (#src)
	for i = 0, (n - 1), 1 do
		do
			table.insert(out, (_j_at(src, i) * 1.0))
		end
		::_jd_continue_3::
	end
	return out
end
function ld_iarr(src)
	local out = _j_array({})
	local n = (#src)
	for i = 0, (n - 1), 1 do
		do
			table.insert(out, _j_int((_j_at(src, i) * 1.0)))
		end
		::_jd_continue_4::
	end
	return out
end
function ld_sarr(src)
	local out = _j_array({})
	local n = (#src)
	for i = 0, (n - 1), 1 do
		do
			table.insert(out, ("" .. _j_str(_j_at(src, i))))
		end
		::_jd_continue_5::
	end
	return out
end
function seg_tex_restore(saved, live)
	local n = (#live)
	if ((#saved) ~= n) then
		return
	end
	for i = 0, (n - 1), 1 do
		do
			local t = _j_int((_j_at(saved, i) * 1.0))
			if (t == live[i + 1]) then
				goto _jd_continue_6
			end
			live[i + 1] = t
			if (t >= 0) then
				tex_prepare(t)
			end
		end
		::_jd_continue_6::
	end
end
function game_load(path)
	path = data_path(path)
	if (not file_exists(path)) then
		return false
	end
	local txt = read_file(path)
	if ((#txt) < 2) then
		return false
	end
	local st = from_json(txt)
	if (((st == NULL) or (not (st["version"] ~= nil))) or (_j_add(st["version"], 0) ~= SAVE_VERSION)) then
		return false
	end
	SKILL_BIT = _j_add(st["skill"], 0)
	NIGHTMARE = ((st["nightmare"] ~= nil) and (st["nightmare"] == true))
	FAST_MONSTERS = ((st["fast"] ~= nil) and (st["fast"] == true))
	if (not game_start_map(("" .. _j_str(st["mapname"])))) then
		return false
	end
	GAME_TIC = _j_add(st["tic"], 0)
	LEVEL_START_TIC = _j_add(st["level_tic"], 0)
	SPEC_TIC0 = _j_add(st["spec_tic0"], 0)
	local p = st["player"]
	P_X = (_j_at(p, "x") * 1.0)
	P_Y = (_j_at(p, "y") * 1.0)
	P_Z = (_j_at(p, "z") * 1.0)
	P_ANG = (_j_at(p, "ang") * 1.0)
	P_MOMX = (_j_at(p, "momx") * 1.0)
	P_MOMY = (_j_at(p, "momy") * 1.0)
	P_MOMZ = (_j_at(p, "momz") * 1.0)
	P_FLOORZ = (_j_at(p, "floorz") * 1.0)
	P_VIEWZ = (_j_at(p, "viewz") * 1.0)
	P_AIR = (_j_at(p, "air") == true)
	P_HP = _j_add(_j_at(p, "hp"), 0)
	P_ARMOR = _j_add(_j_at(p, "armor"), 0)
	P_KEYS = _j_add(_j_at(p, "keys"), 0)
	AMMO_BULLETS = _j_add(_j_at(p, "bullets"), 0)
	AMMO_SHELLS = _j_add(_j_at(p, "shells"), 0)
	AMMO_ROCKETS = _j_add(_j_at(p, "rockets"), 0)
	AMMO_BULLETS_MAX = _j_add(_j_at(p, "bmax"), 0)
	AMMO_SHELLS_MAX = _j_add(_j_at(p, "smax"), 0)
	AMMO_ROCKETS_MAX = _j_add(_j_at(p, "rmax"), 0)
	HAVE_SHOTGUN = (_j_at(p, "shotgun") == true)
	HAVE_CHAINGUN = (_j_at(p, "chaingun") == true)
	HAVE_LAUNCHER = (_j_at(p, "launcher") == true)
	P_BERSERK = (_j_at(p, "berserk") == true)
	P_INVIS = _j_add(_j_at(p, "invis"), 0)
	P_SUIT = _j_add(_j_at(p, "suit"), 0)
	P_GOGGLES = _j_add(_j_at(p, "goggles"), 0)
	P_ALLMAP = (_j_at(p, "allmap") == true)
	P_BACKPACK = (_j_at(p, "backpack") == true)
	CUR_WEAPON = _j_add(_j_at(p, "weapon"), 0)
	W_COOL = _j_add(_j_at(p, "wcool"), 0)
	P_GOD = (_j_at(p, "god") == true)
	P_NOCLIP = (_j_at(p, "noclip") == true)
	local mo = st["mo"]
	MO_X = ld_farr(_j_at(mo, "x"))
	MO_Y = ld_farr(_j_at(mo, "y"))
	MO_Z = ld_farr(_j_at(mo, "z"))
	MO_ANG = ld_farr(_j_at(mo, "ang"))
	MO_MOMX = ld_farr(_j_at(mo, "momx"))
	MO_MOMY = ld_farr(_j_at(mo, "momy"))
	MO_MOMZ = ld_farr(_j_at(mo, "momz"))
	MO_RAD = ld_farr(_j_at(mo, "rad"))
	MO_TYPE = ld_iarr(_j_at(mo, "type"))
	MO_STATE = ld_iarr(_j_at(mo, "state"))
	MO_HP = ld_iarr(_j_at(mo, "hp"))
	MO_FRAME = ld_iarr(_j_at(mo, "frame"))
	MO_TIC = ld_iarr(_j_at(mo, "tic"))
	MO_COOL = ld_iarr(_j_at(mo, "cool"))
	MO_TARGET = ld_iarr(_j_at(mo, "target"))
	MO_CHARGE = ld_iarr(_j_at(mo, "charge"))
	MO_CNT = ld_iarr(_j_at(mo, "cnt"))
	MO_SHP = ld_iarr(_j_at(mo, "shp"))
	MO_ANIM = ld_sarr(_j_at(mo, "anim"))
	MO_SPR = ld_sarr(_j_at(mo, "spr"))
	MO_BRAIN = _j_array({})
	local nmo = (#MO_X)
	for b = 0, (nmo - 1), 1 do
		do
			table.insert(MO_BRAIN, NULL)
		end
		::_jd_continue_7::
	end
	MO_RESP = _j_array({})
	if (mo["resp"] ~= nil) then
		MO_RESP = ld_iarr(_j_at(mo, "resp"))
	else
		for r = 0, (nmo - 1), 1 do
			do
				table.insert(MO_RESP, 0)
			end
			::_jd_continue_8::
		end
	end
	local sec = st["sec"]
	SE_FLOORH = ld_iarr(_j_at(sec, "floorh"))
	SE_CEILH = ld_iarr(_j_at(sec, "ceilh"))
	SE_LIGHT = ld_iarr(_j_at(sec, "light"))
	SE_SPECIAL = ld_iarr(_j_at(sec, "special"))
	SE_FLOORPIC = ld_sarr(_j_at(sec, "floorpic"))
	SEC_FFLAT = ld_iarr(_j_at(sec, "ffslot"))
	LD_SPECIAL = ld_iarr(st["ld_special"])
	local seg = st["seg"]
	seg_tex_restore(_j_at(seg, "mid"), SEG_MIDT)
	seg_tex_restore(_j_at(seg, "top"), SEG_TOPT)
	seg_tex_restore(_j_at(seg, "bot"), SEG_BOTT)
	DOORS = _j_array({})
	local ds = st["doors"]
	do
		local di = 0
		while (di < (#ds)) do
			do
				local rec = _j_at(ds, di)
				local d = Door.new(_j_add(_j_at(rec, 0), 0), _j_add(_j_at(rec, 1), 0), _j_add(_j_at(rec, 4), 0))
				d.timer = _j_add(_j_at(rec, 2), 0)
				d.top = _j_add(_j_at(rec, 3), 0)
				table.insert(DOORS, d)
			end
			::_jd_continue_9::
			di = di + 1
		end
	end
	MV_SEC = _j_array({})
	MOVERS = _j_array({})
	local ms = st["movers"]
	do
		local mi = 0
		while (mi < (#ms)) do
			do
				local mrec = _j_at(ms, mi)
				mover_start(_j_int(_j_add(_j_at(mrec, 0), 0)), _j_int(_j_add(_j_at(mrec, 1), 0)), _j_int(_j_add(_j_at(mrec, 2), 0)), _j_int(_j_add(_j_at(mrec, 3), 0)), _j_int(_j_add(_j_at(mrec, 4), 0)), _j_int(_j_add(_j_at(mrec, 5), 0)))
			end
			::_jd_continue_10::
			mi = mi + 1
		end
	end
	local sp = st["spec"]
	SECRETS_FOUND = _j_add(_j_at(sp, "secrets"), 0)
	SECRET_TOTAL = _j_add(_j_at(sp, "secret_total"), 0)
	BOSS_PEND = _j_add(_j_at(sp, "boss"), 0)
	local an = st["anim"]
	ANIM_PHASE = _j_add(_j_at(an, "phase"), 0)
	ANIM_TICK = _j_add(_j_at(an, "tick"), 0)
	BTN_LD = ld_iarr(_j_at(an, "btn_ld"))
	BTN_TICS = ld_iarr(_j_at(an, "btn_tics"))
	local stats = st["stats"]
	KILLS = _j_add(_j_at(stats, "kills"), 0)
	KILL_TOTAL = _j_add(_j_at(stats, "kill_total"), 0)
	ITEMS_TAKEN = _j_add(_j_at(stats, "items"), 0)
	ITEM_TOTAL = _j_add(_j_at(stats, "item_total"), 0)
	return true
end
