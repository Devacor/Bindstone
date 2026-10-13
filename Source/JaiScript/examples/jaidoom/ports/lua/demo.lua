-- Generated from demo.jai by tools/transpile.py; see internal/examples/jaidoom/ports/PORTING.md.
require("map")
require("things")
DEMO_REC = false
DEMO_PLAY = false
DEMO_TICS = _j_array({})
DEMO_POS = 0
DEMO_SEED = 0
DEMO_MAP = ""
DEMO_SKILL = 2
DEMO_NM = false
function demo_q(v)
	return _j_int(((v * 10000.0) + ((v < 0.0) and -0.5 or 0.5)))
end
function demo_rec_begin(mapname, skill, seed)
	DEMO_SEED = seed
	DEMO_MAP = mapname
	DEMO_SKILL = skill
	DEMO_NM = NIGHTMARE
	DEMO_TICS = _j_array({})
	DEMO_REC = true
	GAME_TIC = 0
	_j_random_seed(seed)
end
function demo_note()
	local qf = demo_q(CMD_FWD)
	local qs = demo_q(CMD_SIDE)
	local qt = demo_q(CMD_TURN)
	table.insert(DEMO_TICS, qf)
	table.insert(DEMO_TICS, qs)
	table.insert(DEMO_TICS, qt)
	table.insert(DEMO_TICS, (CMD_USE and 1 or 0))
	table.insert(DEMO_TICS, (CMD_FIRE and 1 or 0))
	table.insert(DEMO_TICS, CUR_WEAPON)
	CMD_FWD = (qf / 10000.0)
	CMD_SIDE = (qs / 10000.0)
	CMD_TURN = (qt / 10000.0)
end
function demo_feed()
	if ((DEMO_POS + 6) > (#DEMO_TICS)) then
		DEMO_PLAY = false
		return false
	end
	CMD_FWD = (DEMO_TICS[DEMO_POS + 1] / 10000.0)
	CMD_SIDE = (DEMO_TICS[(DEMO_POS + 1) + 1] / 10000.0)
	CMD_TURN = (DEMO_TICS[(DEMO_POS + 2) + 1] / 10000.0)
	CMD_USE = (DEMO_TICS[(DEMO_POS + 3) + 1] ~= 0)
	CMD_FIRE = (DEMO_TICS[(DEMO_POS + 4) + 1] ~= 0)
	CUR_WEAPON = DEMO_TICS[(DEMO_POS + 5) + 1]
	DEMO_POS = (DEMO_POS + 6)
	return true
end
function demo_write(path)
	path = data_path(path)
	local d = {["seed"]=DEMO_SEED, ["mapname"]=DEMO_MAP, ["skill"]=DEMO_SKILL, ["nightmare"]=DEMO_NM, ["tics"]=DEMO_TICS}
	write_file(path, to_json(d))
	return file_exists(path)
end
function demo_load(path)
	path = data_path(path)
	if (not file_exists(path)) then
		return false
	end
	local txt = read_file(path)
	if ((#txt) < 2) then
		return false
	end
	local d = from_json(txt)
	if ((((d == NULL) or (not (d["tics"] ~= nil))) or (not (d["seed"] ~= nil))) or (not (d["mapname"] ~= nil))) then
		return false
	end
	DEMO_SEED = _j_add(d["seed"], 0)
	DEMO_MAP = ("" .. _j_str(d["mapname"]))
	DEMO_SKILL = _j_add(d["skill"], 0)
	DEMO_NM = ((d["nightmare"] ~= nil) and (d["nightmare"] == true))
	DEMO_TICS = _j_array({})
	local ts = d["tics"]
	local n = (#ts)
	for i = 0, (n - 1), 1 do
		do
			table.insert(DEMO_TICS, _j_int((_j_at(ts, i) * 1.0)))
		end
		::_jd_continue_1::
	end
	DEMO_POS = 0
	return true
end
HASH_H = 0
function hfold(v)
	HASH_H = (((HASH_H ~ (v & 4294967295)) * 16777619) & 4294967295)
end
function demo_state_hash()
	HASH_H = 2166136261
	hfold(_j_int(GAME_TIC))
	hfold(_j_int((P_X * 256.0)))
	hfold(_j_int((P_Y * 256.0)))
	hfold(_j_int((P_ANG * 100000.0)))
	hfold(_j_int(P_HP))
	hfold(_j_int(P_ARMOR))
	hfold(_j_int(KILLS))
	hfold(_j_int(AMMO_BULLETS))
	local n = (#MO_X)
	for m = 0, (n - 1), 1 do
		do
			hfold(_j_int((MO_X[m + 1] * 256.0)))
			hfold(_j_int((MO_Y[m + 1] * 256.0)))
			hfold(MO_STATE[m + 1])
			hfold(MO_HP[m + 1])
		end
		::_jd_continue_2::
	end
	local ns = (#SE_FLOORH)
	for s = 0, (ns - 1), 1 do
		do
			hfold(SE_FLOORH[s + 1])
			hfold(SE_CEILH[s + 1])
		end
		::_jd_continue_3::
	end
	return HASH_H
end
