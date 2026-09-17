-- Generated from anim.jai by tools/transpile.py; see PORTING.md.
require("wad")
require("map")
require("tex")
require("render")
require("sound")
SWPAIR = {}
BTN_LD = _j_array({})
BTN_TICS = _j_array({})
ANIM_SLOTS = _j_array({})
ANIM_FRAMES = _j_array({})
ANIM_PHASE = 0
ANIM_TICK = 0
function anim_init()
	SWPAIR = {}
	do
		local _jd_range_1 = TEX_NAMES
		local _jd_index_1 = 1
		while _jd_index_1 <= #_jd_range_1 do
			do
				local kv = _jd_range_1[_jd_index_1]
				local nm = kv.first
				if (not _j_starts_with(nm, "SW1")) then
					goto _jd_continue_1
				end
				local other = tex_id(("SW2" .. _j_substr(nm, 3, ((#nm) - 3))))
				if (other >= 0) then
					SWPAIR[kv.second] = other
					SWPAIR[other] = kv.second
				end
			end
			::_jd_continue_1::
			_jd_index_1 = _jd_index_1 + 1
		end
	end
	BTN_LD = _j_array({})
	BTN_TICS = _j_array({})
	ANIM_SLOTS = _j_array({})
	ANIM_FRAMES = _j_array({})
	ANIM_PHASE = 0
	ANIM_TICK = 0
	local sets = _j_array({_j_array({"NUKAGE1", "NUKAGE2", "NUKAGE3"}), _j_array({"FWATER1", "FWATER2", "FWATER3", "FWATER4"}), _j_array({"SWATER1", "SWATER2", "SWATER3", "SWATER4"}), _j_array({"LAVA1", "LAVA2", "LAVA3", "LAVA4"}), _j_array({"BLOOD1", "BLOOD2", "BLOOD3"})})
	do
		local _jd_range_2 = sets
		local _jd_index_2 = 1
		while _jd_index_2 <= #_jd_range_2 do
			do
				local set = _jd_range_2[_jd_index_2]
				local have = _j_array({})
				do
					local _jd_range_3 = set
					local _jd_index_3 = 1
					while _jd_index_3 <= #_jd_range_3 do
						do
							local fn = _jd_range_3[_jd_index_3]
							if (lump_num(fn) >= 0) then
								table.insert(have, fn)
							end
						end
						::_jd_continue_3::
						_jd_index_3 = _jd_index_3 + 1
					end
				end
				if ((#have) < 2) then
					goto _jd_continue_2
				end
				do
					local _jd_range_4 = have
					local _jd_index_4 = 1
					while _jd_index_4 <= #_jd_range_4 do
						do
							local fn2 = _jd_range_4[_jd_index_4]
							if (FLAT_SLOT[fn2] ~= nil) then
								local slot = FLAT_SLOT[fn2]
								table.insert(ANIM_SLOTS, slot)
								table.insert(ANIM_FRAMES, have)
							end
						end
						::_jd_continue_4::
						_jd_index_4 = _jd_index_4 + 1
					end
				end
			end
			::_jd_continue_2::
			_jd_index_2 = _jd_index_2 + 1
		end
	end
end
function switch_use(ld)
	local nsegs = (#SG_LINE)
	local swapped = false
	for i = 0, (nsegs - 1), 1 do
		do
			if ((SG_LINE[i + 1] ~= ld) or (SG_DIR[i + 1] ~= 0)) then
				goto _jd_continue_5
			end
			local mt = SEG_MIDT[i + 1]
			if ((mt >= 0) and (SWPAIR[mt] ~= nil)) then
				SEG_MIDT[i + 1] = SWPAIR[mt]
				tex_prepare(SEG_MIDT[i + 1])
				swapped = true
			end
			local tt = SEG_TOPT[i + 1]
			if ((tt >= 0) and (SWPAIR[tt] ~= nil)) then
				SEG_TOPT[i + 1] = SWPAIR[tt]
				tex_prepare(SEG_TOPT[i + 1])
				swapped = true
			end
			local bt = SEG_BOTT[i + 1]
			if ((bt >= 0) and (SWPAIR[bt] ~= nil)) then
				SEG_BOTT[i + 1] = SWPAIR[bt]
				tex_prepare(SEG_BOTT[i + 1])
				swapped = true
			end
		end
		::_jd_continue_5::
	end
	if (not swapped) then
		return
	end
	snd_play("DSSWTCHN")
	if (LD_SPECIAL[ld + 1] ~= 0) then
		table.insert(BTN_LD, ld)
		table.insert(BTN_TICS, 35)
	end
end
function anim_tic()
	local i = 0
	do
		while (i < (#BTN_LD)) do
			do
				BTN_TICS[i + 1] = (BTN_TICS[i + 1] - 1)
				if (BTN_TICS[i + 1] <= 0) then
					switch_pop(BTN_LD[i + 1])
					table.remove(BTN_LD, i + 1)
					table.remove(BTN_TICS, i + 1)
				else
					i = (i + 1)
				end
			end
			::_jd_continue_6::
		end
	end
	ANIM_TICK = (ANIM_TICK + 1)
	if (ANIM_TICK >= 8) then
		ANIM_TICK = 0
		ANIM_PHASE = (ANIM_PHASE + 1)
		local n = (#ANIM_SLOTS)
		for a = 0, (n - 1), 1 do
			do
				local frames = ANIM_FRAMES[a + 1]
				local nm = frames[math.fmod(ANIM_PHASE, (#frames)) + 1]
				local slot = ANIM_SLOTS[a + 1]
				FLATS[slot + 1] = flat_pix(nm)
			end
			::_jd_continue_7::
		end
	end
end
function switch_pop(ld)
	local nsegs = (#SG_LINE)
	for i = 0, (nsegs - 1), 1 do
		do
			if ((SG_LINE[i + 1] ~= ld) or (SG_DIR[i + 1] ~= 0)) then
				goto _jd_continue_8
			end
			local mt = SEG_MIDT[i + 1]
			if ((mt >= 0) and (SWPAIR[mt] ~= nil)) then
				SEG_MIDT[i + 1] = SWPAIR[mt]
			end
			local tt = SEG_TOPT[i + 1]
			if ((tt >= 0) and (SWPAIR[tt] ~= nil)) then
				SEG_TOPT[i + 1] = SWPAIR[tt]
			end
			local bt = SEG_BOTT[i + 1]
			if ((bt >= 0) and (SWPAIR[bt] ~= nil)) then
				SEG_BOTT[i + 1] = SWPAIR[bt]
			end
		end
		::_jd_continue_8::
	end
end
