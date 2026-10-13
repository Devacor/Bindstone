-- Generated from things.jai by tools/transpile.py; see internal/examples/jaidoom/ports/PORTING.md.
require("wad")
require("map")
require("vid")
require("tex")
require("sound")
SPR_LUMP = {}
SPR_FLIP = {}
SPR_PIX = {}
SPR_W = {}
SPR_H = {}
SPR_LO = {}
SPR_TO = {}
function sprites_init()
	SPR_LUMP = {}
	SPR_FLIP = {}
	SPR_PIX = {}
	SPR_W = {}
	SPR_H = {}
	SPR_LO = {}
	SPR_TO = {}
	local s0 = lump_num("S_START")
	local s1 = lump_num("S_END")
	if ((s0 < 0) or (s1 < 0)) then
		return
	end
	for i = (s0 + 1), (s1 - 1), 1 do
		do
			local nm = LUMP_NAME[i + 1]
			if ((#nm) < 6) then
				goto _jd_continue_1
			end
			SPR_LUMP[_j_substr(nm, 0, 6)] = i
			SPR_FLIP[_j_substr(nm, 0, 6)] = 0
			if ((#nm) == 8) then
				local mirror = (_j_substr(nm, 0, 4) .. _j_substr(nm, 6, 2))
				SPR_LUMP[mirror] = i
				SPR_FLIP[mirror] = 1
			end
		end
		::_jd_continue_1::
	end
end
function sprite_prepare(lump)
	if (SPR_PIX[lump] ~= nil) then
		return
	end
	local pofs = LUMP_POS[lump + 1]
	local w = i16(pofs)
	local h = i16((pofs + 2))
	SPR_W[lump] = w
	SPR_H[lump] = h
	SPR_LO[lump] = i16((pofs + 4))
	SPR_TO[lump] = i16((pofs + 6))
	local pix = _j_array({})
	local n = (w * h)
	for i = 0, (n - 1), 1 do
		do
			table.insert(pix, -1)
		end
		::_jd_continue_2::
	end
	local wd = WAD
	for col = 0, (w - 1), 1 do
		do
			local colbase = (col * h)
			local post = (pofs + u32(((pofs + 8) + (col * 4))))
			do
				while (string.byte(wd, post + 1) ~= 255) do
					do
						local top = (string.byte(wd, post + 1) + 0)
						local len = (string.byte(wd, (post + 1) + 1) + 0)
						local src = (post + 3)
						for k = 0, (len - 1), 1 do
							do
								local dy = (top + k)
								if ((dy >= 0) and (dy < h)) then
									pix[(colbase + dy) + 1] = (string.byte(wd, (src + k) + 1) + 0)
								end
							end
							::_jd_continue_5::
						end
						post = ((post + len) + 4)
					end
					::_jd_continue_4::
				end
			end
		end
		::_jd_continue_3::
	end
	SPR_PIX[lump] = pix
end
SPR_LAST_FLIP = 0
function sprite_lookup(spr4, frame, rot)
	local key = ((spr4 .. frame) .. _j_str((rot + 1)))
	if (SPR_LUMP[key] ~= nil) then
		SPR_LAST_FLIP = SPR_FLIP[key]
		return SPR_LUMP[key]
	end
	key = ((spr4 .. frame) .. "0")
	if (SPR_LUMP[key] ~= nil) then
		SPR_LAST_FLIP = SPR_FLIP[key]
		return SPR_LUMP[key]
	end
	return -1
end
MINFO = {}
MISSILE_SPR = {}
TFOG_ANIM = ""
function anim_have(spr, anim)
	if ((#anim) == 0) then
		return false
	end
	do
		local i = 0
		while (i < (#anim)) do
			do
				if (sprite_lookup(spr, _j_substr(anim, i, 1), 0) < 0) then
					return false
				end
			end
			::_jd_continue_6::
			i = i + 1
		end
	end
	return true
end
function minfo_add(ty, mi)
	if (not anim_have(mi[0 + 1], (((("" .. _j_str(mi[6 + 1])) .. _j_str(mi[7 + 1])) .. _j_str(mi[8 + 1])) .. _j_str(mi[9 + 1])))) then
		return
	end
	MINFO[ty] = mi
end
function things_data_init()
	MINFO = {[3004]=_j_array({"POSS", 20, 2.0, 20.0, 56.0, 200, "ABCD", "EF", "G", "HIJKL", 0, 5, 1024.0}), [9]=_j_array({"SPOS", 30, 2.0, 20.0, 56.0, 170, "ABCD", "EF", "G", "HIJKL", 0, 12, 1024.0}), [3001]=_j_array({"TROO", 60, 2.0, 20.0, 56.0, 200, "ABCD", "EFG", "H", "IJKLM", 3, 8, 1024.0}), [3002]=_j_array({"SARG", 150, 2.5, 30.0, 56.0, 180, "ABCD", "EFG", "H", "IJKLMN", 1, 15, 128.0}), [58]=_j_array({"SARG", 150, 2.5, 30.0, 56.0, 180, "ABCD", "EFG", "H", "IJKLMN", 1, 15, 128.0}), [3006]=_j_array({"SKUL", 100, 3.0, 16.0, 56.0, 256, "AB", "CD", "E", "FGHIJK", 1, 8, 192.0}), [3003]=_j_array({"BOSS", 1000, 2.0, 24.0, 64.0, 50, "ABCD", "EFG", "H", "IJKLMNO", 3, 25, 1024.0})}
	minfo_add(3005, _j_array({"HEAD", 400, 2.0, 31.0, 56.0, 128, "A", "BCD", "E", "GHIJKL", 3, 10, 1024.0}))
	minfo_add(69, _j_array({"BOS2", 500, 2.0, 24.0, 64.0, 50, "ABCD", "EFG", "H", "IJKLMNO", 3, 25, 1024.0}))
	minfo_add(84, _j_array({"SSWV", 50, 2.0, 20.0, 56.0, 170, "ABCD", "EFG", "H", "IJKLM", 0, 12, 1024.0}))
	minfo_add(65, _j_array({"CPOS", 70, 2.0, 20.0, 56.0, 170, "ABCD", "EF", "G", "HIJKLMN", 0, 12, 1024.0}))
	MISSILE_SPR = {}
	if anim_have("BAL2", "ABCDE") then
		MISSILE_SPR[3005] = "BAL2"
	end
	if anim_have("BAL7", "ABCDE") then
		MISSILE_SPR[69] = "BAL7"
	end
	local tf = ""
	local tc = 65
	do
		while (tc <= 90) do
			do
				local tl = ("" .. string.char((tc & 255)))
				if (sprite_lookup("TFOG", tl, 0) < 0) then
					break
				end
				tf = (tf .. tl)
				tc = (tc + 1)
			end
			::_jd_continue_7::
		end
	end
	TFOG_ANIM = (((#tf) >= 4) and tf or "")
	PICKUPS = {[2011]="STIM", [2012]="MEDI", [2014]="BON1", [2015]="BON2", [2018]="ARM1", [2019]="ARM2", [2007]="CLIP", [2048]="AMMO", [2008]="SHEL", [2049]="SBOX", [2001]="SHOT", [2002]="MGUN", [2003]="LAUN", [2010]="ROCK", [2046]="BROK", [5]="BKEY", [13]="RKEY", [6]="YKEY", [2013]="SOUL", [2023]="PSTR", [2024]="PINS", [2025]="SUIT", [2026]="PMAP", [2045]="PVIS", [8]="BPAK"}
	xdeath_probe()
	decor_data_init()
end
PICKUPS = {}
DECOR = {}
function decor_add(ty, spr, base, rad, extend)
	if (not anim_have(spr, base)) then
		return
	end
	local anim = base
	if extend then
		local c = (string.byte(base, ((#base) - 1) + 1) + 1)
		do
			while (c <= 90) do
				do
					local letter = ("" .. string.char((c & 255)))
					if (sprite_lookup(spr, letter, 0) < 0) then
						break
					end
					anim = (anim .. letter)
					c = (c + 1)
				end
				::_jd_continue_8::
			end
		end
	end
	DECOR[ty] = _j_array({spr, anim, rad})
end
function decor_data_init()
	DECOR = {}
	decor_add(48, "ELEC", "A", 16.0, true)
	decor_add(2028, "COLU", "A", 16.0, true)
	decor_add(35, "CBRA", "A", 16.0, true)
	decor_add(30, "COL1", "A", 16.0, true)
	decor_add(10, "PLAY", "W", 0.0, false)
	decor_add(12, "PLAY", "W", 0.0, false)
	decor_add(15, "PLAY", "N", 0.0, false)
	decor_add(24, "POL5", "A", 0.0, false)
end
XDEATH = {}
function xdeath_probe()
	XDEATH = {}
	do
		local _jd_range_9 = MINFO
		local _jd_index_9 = 1
		while _jd_index_9 <= #_jd_range_9 do
			do
				local kv = _jd_range_9[_jd_index_9]
				local mi = kv.second
				local spr = _j_at(mi, 0)
				local danim = _j_at(mi, 9)
				local c = _j_int(_j_add(_j_at(danim, ((#danim) - 1)), 1))
				local xd = ""
				do
					while (c <= 90) do
						do
							local letter = ("" .. string.char((c & 255)))
							if (sprite_lookup(spr, letter, 0) < 0) then
								break
							end
							xd = (xd .. letter)
							c = (c + 1)
						end
						::_jd_continue_10::
					end
				end
				if ((#xd) >= 3) then
					XDEATH[kv.first] = xd
				end
			end
			::_jd_continue_9::
			_jd_index_9 = _jd_index_9 + 1
		end
	end
end
MO_X = _j_array({})
MO_Y = _j_array({})
MO_Z = _j_array({})
MO_ANG = _j_array({})
MO_MOMX = _j_array({})
MO_MOMY = _j_array({})
MO_MOMZ = _j_array({})
MO_RAD = _j_array({})
MO_TYPE = _j_array({})
MO_STATE = _j_array({})
MO_HP = _j_array({})
MO_FRAME = _j_array({})
MO_TIC = _j_array({})
MO_COOL = _j_array({})
MO_TARGET = _j_array({})
MO_CHARGE = _j_array({})
MO_CNT = _j_array({})
MO_SHP = _j_array({})
MO_RESP = _j_array({})
MO_NODMG = _j_array({})
MO_ANIM = _j_array({})
MO_SPR = _j_array({})
MO_BRAIN = _j_array({})
KILLS = 0
KILL_TOTAL = 0
ITEMS_TAKEN = 0
ITEM_TOTAL = 0
SKILL_BIT = 2
NIGHTMARE = false
FAST_MONSTERS = false
function mobj_spawn(x, y, type, ang, state, spr, anim, hp, rad)
	table.insert(MO_X, x)
	table.insert(MO_Y, y)
	table.insert(MO_Z, (SE_FLOORH[sector_at(x, y) + 1] * 1.0))
	table.insert(MO_ANG, ang)
	table.insert(MO_MOMX, 0.0)
	table.insert(MO_MOMY, 0.0)
	table.insert(MO_MOMZ, 0.0)
	table.insert(MO_RAD, rad)
	table.insert(MO_TYPE, type)
	table.insert(MO_STATE, state)
	table.insert(MO_HP, hp)
	table.insert(MO_FRAME, 0)
	table.insert(MO_TIC, (4 + _j_random(0, 6)))
	table.insert(MO_COOL, 0)
	table.insert(MO_TARGET, -1)
	table.insert(MO_CHARGE, 0)
	table.insert(MO_CNT, 0)
	table.insert(MO_SHP, hp)
	table.insert(MO_RESP, 0)
	table.insert(MO_NODMG, 0)
	table.insert(MO_ANIM, anim)
	table.insert(MO_SPR, spr)
	table.insert(MO_BRAIN, NULL)
	return ((#MO_X) - 1)
end
function things_spawn_map()
	MO_X = _j_array({})
	MO_Y = _j_array({})
	MO_Z = _j_array({})
	MO_ANG = _j_array({})
	MO_MOMX = _j_array({})
	MO_MOMY = _j_array({})
	MO_MOMZ = _j_array({})
	MO_RAD = _j_array({})
	MO_TYPE = _j_array({})
	MO_STATE = _j_array({})
	MO_HP = _j_array({})
	MO_FRAME = _j_array({})
	MO_TIC = _j_array({})
	MO_COOL = _j_array({})
	MO_TARGET = _j_array({})
	MO_CHARGE = _j_array({})
	MO_CNT = _j_array({})
	MO_SHP = _j_array({})
	MO_RESP = _j_array({})
	MO_NODMG = _j_array({})
	MO_ANIM = _j_array({})
	MO_SPR = _j_array({})
	MO_BRAIN = _j_array({})
	KILLS = 0
	KILL_TOTAL = 0
	ITEMS_TAKEN = 0
	ITEM_TOTAL = 0
	local n = (#TH_X)
	for t = 0, (n - 1), 1 do
		do
			local ty = TH_TYPE[t + 1]
			local fl = TH_FLAGS[t + 1]
			if (((fl & 16) ~= 0) and (not NET_GAME)) then
				goto _jd_continue_11
			end
			if ((fl & SKILL_BIT) == 0) then
				goto _jd_continue_11
			end
			if ((NET_GAME and (not NP_COOP)) and (MINFO[ty] ~= nil)) then
				goto _jd_continue_11
			end
			local ang = ((TH_ANG[t + 1] * PI) / 180.0)
			if (MINFO[ty] ~= nil) then
				local mi = MINFO[ty]
				local mst = (((NET_GAME and NP_COOP) and (not NP_IS_HOST)) and 12 or 1)
				mobj_spawn((TH_X[t + 1] * 1.0), (TH_Y[t + 1] * 1.0), ty, ang, mst, mi[0 + 1], mi[6 + 1], _j_int(mi[1 + 1]), (mi[3 + 1] + 0.0))
				KILL_TOTAL = (KILL_TOTAL + 1)
			else
				if (PICKUPS[ty] ~= nil) then
					local pk = mobj_spawn((TH_X[t + 1] * 1.0), (TH_Y[t + 1] * 1.0), ty, ang, 7, PICKUPS[ty], "A", 0, 20.0)
					MO_CNT[pk + 1] = 1
					ITEM_TOTAL = (ITEM_TOTAL + 1)
				else
					if (DECOR[ty] ~= nil) then
						local dd = DECOR[ty]
						mobj_spawn((TH_X[t + 1] * 1.0), (TH_Y[t + 1] * 1.0), ty, ang, 10, ("" .. _j_str(dd[0 + 1])), ("" .. _j_str(dd[1 + 1])), 0, (dd[2 + 1] * 1.0))
					else
						if (ty == 2035) then
							mobj_spawn((TH_X[t + 1] * 1.0), (TH_Y[t + 1] * 1.0), ty, ang, 9, "BAR1", "AB", 20, 10.0)
						end
					end
				end
			end
		end
		::_jd_continue_11::
	end
end
TRACE_LINE = -1
function trace_block_dz(x1, y1, x2, y2, z, dzfull)
	TRACE_LINE = -1
	local best = 1000000000.0
	local n = (#LD_V1)
	local rdx = (x2 - x1)
	local rdy = (y2 - y1)
	for i = 0, (n - 1), 1 do
		do
			local ax = (VX_X[LD_V1[i + 1] + 1] + 0.0)
			local ay = (VX_Y[LD_V1[i + 1] + 1] + 0.0)
			local bx = (VX_X[LD_V2[i + 1] + 1] + 0.0)
			local by = (VX_Y[LD_V2[i + 1] + 1] + 0.0)
			local d = (((bx - ax) * rdy) - ((by - ay) * rdx))
			if ((d > -0.0001) and (d < 0.0001)) then
				goto _jd_continue_12
			end
			local t = ((((x1 - ax) * rdy) - ((y1 - ay) * rdx)) / d)
			local s = ((((x1 - ax) * (by - ay)) - ((y1 - ay) * (bx - ax))) / d)
			if ((((t < 0.0) or (t > 1.0)) or (s < 0.0)) or (s >= best)) then
				goto _jd_continue_12
			end
			local s1 = LD_SIDE1[i + 1]
			if (s1 < 0) then
				best = s
				TRACE_LINE = i
				goto _jd_continue_12
			end
			local f0 = SE_FLOORH[SD_SEC[LD_SIDE0[i + 1] + 1] + 1]
			local f1 = SE_FLOORH[SD_SEC[s1 + 1] + 1]
			local c0 = SE_CEILH[SD_SEC[LD_SIDE0[i + 1] + 1] + 1]
			local c1 = SE_CEILH[SD_SEC[s1 + 1] + 1]
			local ob = (((f0 > f1) and f0 or f1) + 0.0)
			local ot = (((c0 < c1) and c0 or c1) + 0.0)
			local zl = (z + (dzfull * s))
			if ((zl <= ob) or (zl >= ot)) then
				best = s
				TRACE_LINE = i
			end
		end
		::_jd_continue_12::
	end
	return best
end
function trace_block_t(x1, y1, x2, y2, z)
	return trace_block_dz(x1, y1, x2, y2, z, 0.0)
end
function sight_clear(x1, y1, x2, y2, z)
	return (trace_block_t(x1, y1, x2, y2, z) >= 1.0)
end
function mobj_height(m)
	local ty = MO_TYPE[m + 1]
	if (MINFO[ty] ~= nil) then
		local mi = MINFO[ty]
		local h = (mi[4 + 1] + 0.0)
		return h
	end
	if (ty == 2035) then
		return 42.0
	end
	return 16.0
end
function sight_snd(ty)
	if (ty == 3001) then
		return "DSBGSIT1"
	end
	if ((ty == 3002) or (ty == 58)) then
		return "DSSGTSIT"
	end
	if (ty == 3003) then
		return "DSBRSSIT"
	end
	if (ty == 3006) then
		return ""
	end
	if (ty == 3005) then
		return "DSCACSIT"
	end
	if (ty == 69) then
		return "DSKNTSIT"
	end
	if (ty == 84) then
		return "DSSSSIT"
	end
	if (ty == 65) then
		return "DSPOSIT2"
	end
	return "DSPOSIT1"
end
function death_snd(ty)
	if (ty == 3001) then
		return "DSBGDTH1"
	end
	if ((ty == 3002) or (ty == 58)) then
		return "DSSGTDTH"
	end
	if (ty == 3003) then
		return "DSBRSDTH"
	end
	if (ty == 3006) then
		return "DSFIRXPL"
	end
	if (ty == 3005) then
		return "DSCACDTH"
	end
	if (ty == 69) then
		return "DSKNTDTH"
	end
	if (ty == 84) then
		return "DSSSDTH"
	end
	if (ty == 65) then
		return "DSPODTH2"
	end
	return "DSPODTH1"
end
function shot_z_hit(bz, mz, mh)
	return ((bz >= mz) and (bz <= (mz + mh)))
end
AIM_SLOPE = 0.625
function shoot_hitscan(x, y, z, ang, dmg, attacker, range)
	local ex = (x + (math.cos(ang) * range))
	local ey = (y + (math.sin(ang) * range))
	local rdx = (ex - x)
	local rdy = (ey - y)
	local best = 1.0
	local hit = -1
	local aimz = 0.0
	local n = (#MO_X)
	for m = 0, (n - 1), 1 do
		do
			if (m == attacker) then
				goto _jd_continue_13
			end
			local st = MO_STATE[m + 1]
			if ((((((st ~= 1) and (st ~= 2)) and (st ~= 3)) and (st ~= 4)) and (st ~= 9)) and (st ~= 11)) then
				goto _jd_continue_13
			end
			if (((st == 11) and (attacker ~= -1)) and (not (NP_COOP and (attacker >= 0)))) then
				goto _jd_continue_13
			end
			local px = (MO_X[m + 1] - x)
			local py = (MO_Y[m + 1] - y)
			local t = (((px * rdx) + (py * rdy)) / ((rdx * rdx) + (rdy * rdy)))
			if ((t <= 0.0) or (t >= best)) then
				goto _jd_continue_13
			end
			local d = (t * range)
			local mh = mobj_height(m)
			if ((((MO_Z[m + 1] - z) / d) > AIM_SLOPE) or ((((MO_Z[m + 1] + mh) - z) / d) < (0.0 - AIM_SLOPE))) then
				goto _jd_continue_13
			end
			local cx = ((x + (rdx * t)) - MO_X[m + 1])
			local cy = ((y + (rdy * t)) - MO_Y[m + 1])
			local rr = (MO_RAD[m + 1] + 12.0)
			if (((cx * cx) + (cy * cy)) < (rr * rr)) then
				best = t
				hit = m
				aimz = (MO_Z[m + 1] + (mh * 0.5))
			end
		end
		::_jd_continue_13::
	end
	if ((attacker >= 0) and (P_HP > 0)) then
		local ppx = (P_X - x)
		local ppy = (P_Y - y)
		local tp = (((ppx * rdx) + (ppy * rdy)) / ((rdx * rdx) + (rdy * rdy)))
		if ((tp > 0.0) and (tp < best)) then
			local pd = (tp * range)
			if ((((P_FLOORZ - z) / pd) <= AIM_SLOPE) and ((((P_FLOORZ + P_HEIGHT) - z) / pd) >= (0.0 - AIM_SLOPE))) then
				local pcx = ((x + (rdx * tp)) - P_X)
				local pcy = ((y + (rdy * tp)) - P_Y)
				local prr = (P_RADIUS + 12.0)
				if (((pcx * pcx) + (pcy * pcy)) < (prr * prr)) then
					best = tp
					hit = -2
					aimz = (P_FLOORZ + 28.0)
				end
			end
		end
	end
	local dzfull = 0.0
	if ((hit ~= -1) and (best > 0.0001)) then
		local slope = ((aimz - z) / (best * range))
		if (slope > AIM_SLOPE) then
			slope = AIM_SLOPE
		end
		if (slope < (0.0 - AIM_SLOPE)) then
			slope = (0.0 - AIM_SLOPE)
		end
		dzfull = (slope * range)
	end
	local twall = trace_block_dz(x, y, ex, ey, z, dzfull)
	if (twall <= best) then
		hit = -1
	end
	local gl = TRACE_LINE
	if (NP_FORWARD and (attacker == -1)) then
		if ((hit >= 0) and (MO_STATE[hit + 1] ~= 11)) then
			decor_spawn((x + (rdx * best)), (y + (rdy * best)), (z + (dzfull * best)), "BLUD", "CBA")
		else
			if ((hit == -1) and (twall < 1.0)) then
				local pt0 = (twall - (6.0 / range))
				if (pt0 < 0.0) then
					pt0 = 0.0
				end
				decor_spawn((x + (rdx * pt0)), (y + (rdy * pt0)), ((z + (dzfull * pt0)) - 8.0), "PUFF", "ABCD")
			end
		end
		return hit
	end
	if (hit >= 0) then
		if (MO_STATE[hit + 1] == 11) then
			if (attacker >= 0) then
				np_ghost_hit_mobj(hit, dmg, MO_X[attacker + 1], MO_Y[attacker + 1])
			else
				np_ghost_hit_mobj(hit, dmg, x, y)
			end
		else
			mobj_damage(hit, dmg, attacker)
		end
		decor_spawn((x + (rdx * best)), (y + (rdy * best)), (z + (dzfull * best)), "BLUD", "CBA")
	else
		if (hit == -2) then
			player_damage_from(dmg, x, y)
		else
			if (twall < 1.0) then
				local pt = (twall - (6.0 / range))
				if (pt < 0.0) then
					pt = 0.0
				end
				decor_spawn((x + (rdx * pt)), (y + (rdy * pt)), ((z + (dzfull * pt)) - 8.0), "PUFF", "ABCD")
				if (((attacker < 0) and (gl >= 0)) and (LD_SPECIAL[gl + 1] == 46)) then
					spec_line(gl, 2)
				end
			end
		end
	end
	return hit
end
function decor_spawn(x, y, z, spr, anim)
	local m = mobj_spawn(x, y, 0, 0.0, 5, spr, anim, 0, 4.0)
	MO_Z[m + 1] = z
	MO_TIC[m + 1] = 3
	return m
end
function mobj_damage(m, dmg, attacker)
	local st = MO_STATE[m + 1]
	if (((((st ~= 1) and (st ~= 2)) and (st ~= 3)) and (st ~= 4)) and (st ~= 9)) then
		return
	end
	MO_HP[m + 1] = (MO_HP[m + 1] - dmg)
	if (MO_STATE[m + 1] == 1) then
		MO_STATE[m + 1] = 2
	end
	if ((attacker >= 0) and (attacker ~= m)) then
		local ast = MO_STATE[attacker + 1]
		if (((MINFO[MO_TYPE[m + 1]] ~= nil) and ((MINFO[MO_TYPE[attacker + 1]] ~= nil) or (ast == 11))) and (((((ast == 1) or (ast == 2)) or (ast == 3)) or (ast == 4)) or (ast == 11))) then
			MO_TARGET[m + 1] = attacker
		end
	end
	if (MO_HP[m + 1] <= 0) then
		if (MO_STATE[m + 1] == 9) then
			barrel_explode(m)
			return
		end
		MO_STATE[m + 1] = 5
		local ty = MO_TYPE[m + 1]
		local mi = MINFO[ty]
		local gib = ((XDEATH[ty] ~= nil) and (MO_HP[m + 1] <= (0 - MO_SHP[m + 1])))
		if gib then
			MO_ANIM[m + 1] = XDEATH[ty]
		else
			MO_ANIM[m + 1] = mi[9 + 1]
		end
		MO_FRAME[m + 1] = 0
		MO_TIC[m + 1] = 5
		MO_MOMX[m + 1] = 0.0
		MO_MOMY[m + 1] = 0.0
		MO_CHARGE[m + 1] = 0
		MO_BRAIN[m + 1] = NULL
		KILLS = (KILLS + 1)
		snd_play_at((gib and "DSSLOP" or death_snd(ty)), MO_X[m + 1], MO_Y[m + 1])
		mobj_drop(m)
		return
	end
	if (((MINFO[MO_TYPE[m + 1]] ~= nil) and (MO_STATE[m + 1] ~= 9)) and (_j_random(0, 255) < MINFO[MO_TYPE[m + 1]][5 + 1])) then
		MO_STATE[m + 1] = 4
		MO_ANIM[m + 1] = MINFO[MO_TYPE[m + 1]][8 + 1]
		MO_FRAME[m + 1] = 0
		MO_TIC[m + 1] = 6
		MO_BRAIN[m + 1] = NULL
		snd_play_at("DSPOPAIN", MO_X[m + 1], MO_Y[m + 1])
	end
end
function mobj_drop(m)
	local ty = MO_TYPE[m + 1]
	local d = -1
	if (ty == 3004) then
		d = mobj_spawn(MO_X[m + 1], MO_Y[m + 1], 2007, 0.0, 7, "CLIP", "A", 0, 20.0)
	else
		if (ty == 9) then
			d = mobj_spawn(MO_X[m + 1], MO_Y[m + 1], 2001, 0.0, 7, "SHOT", "A", 0, 20.0)
		end
	end
	if ((((d >= 0) and NET_GAME) and NP_COOP) and NP_IS_HOST) then
		np_drop_announce(d)
	end
end
function barrel_boom_cosmetic(m)
	MO_STATE[m + 1] = 5
	MO_SPR[m + 1] = "BEXP"
	MO_ANIM[m + 1] = "ABCDE"
	MO_FRAME[m + 1] = 0
	MO_TIC[m + 1] = 5
	snd_play_at("DSBAREXP", MO_X[m + 1], MO_Y[m + 1])
end
function barrel_explode(m)
	barrel_boom_cosmetic(m)
	if ((NET_GAME and NP_COOP) and NP_IS_HOST) then
		np_boom_announce(m)
	end
	local bx = MO_X[m + 1]
	local by = MO_Y[m + 1]
	local n = (#MO_X)
	for o = 0, (n - 1), 1 do
		do
			if (o == m) then
				goto _jd_continue_14
			end
			local ost = MO_STATE[o + 1]
			local dx = (MO_X[o + 1] - bx)
			local dy = (MO_Y[o + 1] - by)
			local d2 = ((dx * dx) + (dy * dy))
			if (d2 >= 16384.0) then
				goto _jd_continue_14
			end
			local bdmg = _j_int((70.0 - (math.sqrt(d2) / 2.0)))
			if (ost == 11) then
				np_ghost_hit_mobj(o, bdmg, bx, by)
			else
				mobj_damage(o, bdmg, m)
			end
		end
		::_jd_continue_14::
	end
	local pdx = (P_X - bx)
	local pdy = (P_Y - by)
	local pd2 = ((pdx * pdx) + (pdy * pdy))
	if (pd2 < 16384.0) then
		player_damage_from(_j_int((70.0 - (math.sqrt(pd2) / 2.0))), bx, by)
	end
end
function projectile_spawn(x, y, z, ang, shooter, tmidz, dist, spr, anim, speed, fsnd)
	if (fsnd ~= "") then
		snd_play_at(fsnd, x, y)
	end
	local m = mobj_spawn(x, y, 0, ang, 8, spr, anim, 1, 6.0)
	MO_Z[m + 1] = z
	MO_MOMX[m + 1] = (math.cos(ang) * speed)
	MO_MOMY[m + 1] = (math.sin(ang) * speed)
	if (dist < 10.0) then
		dist = 10.0
	end
	MO_MOMZ[m + 1] = ((tmidz - z) / (dist / speed))
	MO_TARGET[m + 1] = shooter
	if NET_GAME then
		np_proj_announce(m, fsnd)
	end
	return m
end
function fireball_spawn(x, y, z, ang, shooter, tmidz, dist)
	return projectile_spawn(x, y, z, ang, shooter, tmidz, dist, "BAL1", "AB", 10.0, "DSFIRSHT")
end
function rocket_spawn(x, y, z, ang, shooter)
	return projectile_spawn(x, y, z, ang, shooter, z, 2048.0, "MISL", "A", 25.0, "")
end
function radius_damage(bx, by, radius, maxdmg, attacker)
	local n = (#MO_X)
	for o = 0, (n - 1), 1 do
		do
			local st = MO_STATE[o + 1]
			if ((((((st ~= 1) and (st ~= 2)) and (st ~= 3)) and (st ~= 4)) and (st ~= 9)) and (st ~= 11)) then
				goto _jd_continue_15
			end
			local dx = (MO_X[o + 1] - bx)
			local dy = (MO_Y[o + 1] - by)
			local d2 = ((dx * dx) + (dy * dy))
			if (d2 >= (radius * radius)) then
				goto _jd_continue_15
			end
			local sdmg = (maxdmg - _j_int(math.sqrt(d2)))
			if (sdmg <= 0) then
				goto _jd_continue_15
			end
			if (st == 11) then
				np_ghost_hit_mobj(o, sdmg, bx, by)
			else
				mobj_damage(o, sdmg, attacker)
			end
		end
		::_jd_continue_15::
	end
	if (P_HP <= 0) then
		return
	end
	local pdx = (P_X - bx)
	local pdy = (P_Y - by)
	local pd2 = ((pdx * pdx) + (pdy * pdy))
	if (pd2 < (radius * radius)) then
		local pd = math.sqrt(pd2)
		local pdmg = (maxdmg - _j_int(pd))
		if (pdmg > 0) then
			player_damage_from(pdmg, bx, by)
			if (pd > 1.0) then
				local shove = (pdmg * 0.02)
				P_MOMX = (P_MOMX + ((pdx / pd) * shove))
				P_MOMY = (P_MOMY + ((pdy / pd) * shove))
			end
		end
	end
end
function projectile_explode(m)
	MO_STATE[m + 1] = 5
	MO_FRAME[m + 1] = 0
	MO_TIC[m + 1] = 4
	MO_MOMX[m + 1] = 0.0
	MO_MOMY[m + 1] = 0.0
	MO_MOMZ[m + 1] = 0.0
	if NET_GAME then
		np_expl_announce(m)
	end
	if (MO_SPR[m + 1] == "MISL") then
		MO_ANIM[m + 1] = "BCD"
		snd_play_at("DSBAREXP", MO_X[m + 1], MO_Y[m + 1])
		if (MO_NODMG[m + 1] == 0) then
			radius_damage(MO_X[m + 1], MO_Y[m + 1], 128.0, 128, MO_TARGET[m + 1])
		end
	else
		MO_ANIM[m + 1] = "CDE"
		snd_play_at("DSFIRXPL", MO_X[m + 1], MO_Y[m + 1])
	end
end
function projectile_tic(m)
	local shooter = MO_TARGET[m + 1]
	local nx = (MO_X[m + 1] + MO_MOMX[m + 1])
	local ny = (MO_Y[m + 1] + MO_MOMY[m + 1])
	local nz = (MO_Z[m + 1] + MO_MOMZ[m + 1])
	local rocket = (MO_SPR[m + 1] == "MISL")
	local cosmetic = (MO_NODMG[m + 1] ~= 0)
	local streamed = (MO_NODMG[m + 1] == 2)
	if streamed then
		MO_COOL[m + 1] = (MO_COOL[m + 1] - 1)
		if (MO_COOL[m + 1] <= 0) then
			projectile_explode(m)
			return
		end
	end
	if (((not cosmetic) and (shooter >= 0)) and (P_HP > 0)) then
		local dx = (nx - P_X)
		local dy = (ny - P_Y)
		if ((((dx * dx) + (dy * dy)) < ((16.0 + MO_RAD[m + 1]) * (16.0 + MO_RAD[m + 1]))) and shot_z_hit(nz, (P_FLOORZ + 0.0), P_HEIGHT)) then
			player_damage_from((rocket and (20 * _j_random(1, 8)) or (8 * _j_random(1, 3))), MO_X[m + 1], MO_Y[m + 1])
			projectile_explode(m)
			return
		end
	end
	local n = (streamed and 0 or (#MO_X))
	for o = 0, (n - 1), 1 do
		do
			if ((o == m) or (o == shooter)) then
				goto _jd_continue_16
			end
			local st = MO_STATE[o + 1]
			if (((((((st ~= 1) and (st ~= 2)) and (st ~= 3)) and (st ~= 4)) and (st ~= 9)) and (st ~= 11)) and (st ~= 12)) then
				goto _jd_continue_16
			end
			if (((st == 11) and (shooter ~= -1)) and (not (NP_COOP and (shooter >= 0)))) then
				goto _jd_continue_16
			end
			if ((st == 12) and (not cosmetic)) then
				goto _jd_continue_16
			end
			local odx = (nx - MO_X[o + 1])
			local ody = (ny - MO_Y[o + 1])
			local orr = (MO_RAD[o + 1] + MO_RAD[m + 1])
			if (((odx * odx) + (ody * ody)) >= (orr * orr)) then
				goto _jd_continue_16
			end
			if (not shot_z_hit(nz, MO_Z[o + 1], mobj_height(o))) then
				goto _jd_continue_16
			end
			if cosmetic then
				projectile_explode(m)
				return
			end
			local pdmg = (rocket and (20 * _j_random(1, 8)) or (8 * _j_random(1, 3)))
			if (st == 11) then
				np_ghost_hit_mobj(o, pdmg, MO_X[m + 1], MO_Y[m + 1])
			else
				mobj_damage(o, pdmg, shooter)
			end
			projectile_explode(m)
			return
		end
		::_jd_continue_16::
	end
	if (trace_block_t(MO_X[m + 1], MO_Y[m + 1], nx, ny, MO_Z[m + 1]) < 1.0) then
		projectile_explode(m)
		return
	end
	local sec = sector_at(nx, ny)
	if ((nz <= SE_FLOORH[sec + 1]) or (nz >= SE_CEILH[sec + 1])) then
		projectile_explode(m)
		return
	end
	MO_X[m + 1] = nx
	MO_Y[m + 1] = ny
	MO_Z[m + 1] = nz
end
TGT_X = 0.0
TGT_Y = 0.0
TGT_MIDZ = 0.0
TGT_LIVE = false
function target_info(m)
	local t = MO_TARGET[m + 1]
	if (t >= 0) then
		local st = MO_STATE[t + 1]
		local ghost_live = ((st == 11) and np_tgt_live(t))
		if (((((st == 1) or (st == 2)) or (st == 3)) or (st == 4)) or ghost_live) then
			TGT_X = MO_X[t + 1]
			TGT_Y = MO_Y[t + 1]
			TGT_MIDZ = (MO_Z[t + 1] + (mobj_height(t) * 0.5))
			TGT_LIVE = true
			return
		end
		MO_TARGET[m + 1] = -1
	end
	if (((NET_GAME and NP_COOP) and NP_IS_HOST) and ((#NP_TGT_X) > 0)) then
		local pdx = (P_X - MO_X[m + 1])
		local pdy = (P_Y - MO_Y[m + 1])
		local best = ((P_HP > 0) and ((pdx * pdx) + (pdy * pdy)) or 1000000000.0)
		local pick = -1
		local ng = (#NP_TGT_X)
		for g = 0, (ng - 1), 1 do
			do
				local gdx = (_j_at(NP_TGT_X, g) - MO_X[m + 1])
				local gdy = (_j_at(NP_TGT_Y, g) - MO_Y[m + 1])
				local gd2 = ((gdx * gdx) + (gdy * gdy))
				if (gd2 < best) then
					best = gd2
					pick = g
				end
			end
			::_jd_continue_17::
		end
		if (pick >= 0) then
			local gm = _j_int(_j_at(NP_TGT_MOBJ, pick))
			TGT_X = MO_X[gm + 1]
			TGT_Y = MO_Y[gm + 1]
			TGT_MIDZ = (MO_Z[gm + 1] + 28.0)
			TGT_LIVE = true
			MO_TARGET[m + 1] = gm
			return
		end
	end
	TGT_X = P_X
	TGT_Y = P_Y
	TGT_MIDZ = (P_FLOORZ + 28.0)
	TGT_LIVE = (P_HP > 0)
end
function np_tgt_live(mobj)
	local n = (#NP_TGT_MOBJ)
	for i = 0, (n - 1), 1 do
		do
			if (_j_at(NP_TGT_MOBJ, i) == mobj) then
				return true
			end
		end
		::_jd_continue_18::
	end
	return false
end
function charge_start(m)
	snd_play_at("DSSKLATK", MO_X[m + 1], MO_Y[m + 1])
	MO_CHARGE[m + 1] = 1
	MO_MOMX[m + 1] = (math.cos(MO_ANG[m + 1]) * 20.0)
	MO_MOMY[m + 1] = (math.sin(MO_ANG[m + 1]) * 20.0)
	local mi = MINFO[MO_TYPE[m + 1]]
	MO_ANIM[m + 1] = mi[7 + 1]
	MO_FRAME[m + 1] = 0
	MO_COOL[m + 1] = (40 + _j_random(0, 20))
	if FAST_MONSTERS then
		MO_COOL[m + 1] = _j_idiv(MO_COOL[m + 1], 2)
	end
end
function charge_tic(m)
	target_info(m)
	local bit = false
	if TGT_LIVE then
		local t = MO_TARGET[m + 1]
		local tr = ((t >= 0) and MO_RAD[t + 1] or P_RADIUS)
		local dx = (TGT_X - MO_X[m + 1])
		local dy = (TGT_Y - MO_Y[m + 1])
		local rr = ((MO_RAD[m + 1] + tr) + 8.0)
		if (((dx * dx) + (dy * dy)) < (rr * rr)) then
			local dmg = ((_j_random(1, 8) + _j_random(1, 8)) + _j_random(1, 8))
			if (t >= 0) then
				mobj_damage(t, dmg, m)
			else
				player_damage_from(dmg, MO_X[m + 1], MO_Y[m + 1])
			end
			bit = true
		end
	end
	local nx = (MO_X[m + 1] + MO_MOMX[m + 1])
	local ny = (MO_Y[m + 1] + MO_MOMY[m + 1])
	local moved = false
	if mobj_pos_ok(m, nx, ny, true) then
		MO_X[m + 1] = nx
		MO_Y[m + 1] = ny
		moved = true
	else
		if mobj_pos_ok(m, nx, MO_Y[m + 1], true) then
			MO_X[m + 1] = nx
			moved = true
		else
			if mobj_pos_ok(m, MO_X[m + 1], ny, true) then
				MO_Y[m + 1] = ny
				moved = true
			end
		end
	end
	MO_Z[m + 1] = (SE_FLOORH[sector_at(MO_X[m + 1], MO_Y[m + 1]) + 1] * 1.0)
	MO_MOMX[m + 1] = (MO_MOMX[m + 1] * 0.9)
	MO_MOMY[m + 1] = (MO_MOMY[m + 1] * 0.9)
	local sp2 = ((MO_MOMX[m + 1] * MO_MOMX[m + 1]) + (MO_MOMY[m + 1] * MO_MOMY[m + 1]))
	if ((bit or (not moved)) or (sp2 < 16.0)) then
		MO_CHARGE[m + 1] = 0
		MO_MOMX[m + 1] = 0.0
		MO_MOMY[m + 1] = 0.0
		MO_STATE[m + 1] = 2
		local mi = MINFO[MO_TYPE[m + 1]]
		MO_ANIM[m + 1] = mi[6 + 1]
		MO_FRAME[m + 1] = 0
	end
end
function monster_brain(m)
	return _j_coroutine(function()
		local mi = MINFO[MO_TYPE[m + 1]]
		do
			while (MO_STATE[m + 1] == 1) do
				do
					local wdx = (P_X - MO_X[m + 1])
					local wdy = (P_Y - MO_Y[m + 1])
					if (((P_HP > 0) and (((wdx * wdx) + (wdy * wdy)) < 1440000.0)) and sight_clear(MO_X[m + 1], MO_Y[m + 1], (P_X + 0.0), (P_Y + 0.0), (MO_Z[m + 1] + 32.0))) then
						MO_STATE[m + 1] = 2
						MO_TIC[m + 1] = 8
						local ss = sight_snd(MO_TYPE[m + 1])
						if (ss ~= "") then
							snd_play_at(ss, MO_X[m + 1], MO_Y[m + 1])
						end
					end
					coroutine.yield()
				end
				::_jd_continue_19::
			end
		end
		do
			while true do
				do
					target_info(m)
					local dx = (TGT_X - MO_X[m + 1])
					local dy = (TGT_Y - MO_Y[m + 1])
					local dist = math.sqrt(((dx * dx) + (dy * dy)))
					local eyez = (MO_Z[m + 1] + 32.0)
					MO_ANG[m + 1] = math.atan(dy, dx)
					if (MO_COOL[m + 1] > 0) then
						MO_COOL[m + 1] = (MO_COOL[m + 1] - 1)
					end
					local kind = _j_int(mi[10 + 1])
					local melee_r = ((kind == 1) and (1.0 * mi[12 + 1]) or 64.0)
					local melee_range = (((kind == 1) or (kind == 3)) and (dist < (melee_r + 20.0)))
					local can_missile = ((((kind == 0) or (kind == 2)) or (kind == 3)) and (dist < mi[12 + 1]))
					if (((TGT_LIVE and (MO_COOL[m + 1] == 0)) and (melee_range or can_missile)) and sight_clear(MO_X[m + 1], MO_Y[m + 1], (TGT_X + 0.0), (TGT_Y + 0.0), eyez)) then
						if (MO_TYPE[m + 1] == 3006) then
							charge_start(m)
							return
						end
						MO_STATE[m + 1] = 3
						MO_ANIM[m + 1] = mi[7 + 1]
						MO_FRAME[m + 1] = 0
						MO_TIC[m + 1] = 6
						if melee_range then
							MO_MOMX[m + 1] = (math.cos(MO_ANG[m + 1]) * 8.0)
							MO_MOMY[m + 1] = (math.sin(MO_ANG[m + 1]) * 8.0)
						end
						coroutine.yield()
						do
							while (MO_STATE[m + 1] == 3) do
								do
									if ((MO_MOMX[m + 1] ~= 0.0) or (MO_MOMY[m + 1] ~= 0.0)) then
										local lx = (MO_X[m + 1] + MO_MOMX[m + 1])
										local ly = (MO_Y[m + 1] + MO_MOMY[m + 1])
										if mobj_pos_ok(m, lx, ly, false) then
											MO_X[m + 1] = lx
											MO_Y[m + 1] = ly
										end
										MO_MOMX[m + 1] = (MO_MOMX[m + 1] * 0.65)
										MO_MOMY[m + 1] = (MO_MOMY[m + 1] * 0.65)
									end
									coroutine.yield()
								end
								::_jd_continue_21::
							end
						end
						goto _jd_continue_20
					end
					local sp = (mi[2 + 1] + 0.0)
					if FAST_MONSTERS then
						sp = (sp * 1.5)
					end
					local nx = (MO_X[m + 1] + (math.cos(MO_ANG[m + 1]) * sp))
					local ny = (MO_Y[m + 1] + (math.sin(MO_ANG[m + 1]) * sp))
					if mobj_pos_ok(m, nx, ny, false) then
						MO_X[m + 1] = nx
						MO_Y[m + 1] = ny
					else
						if mobj_pos_ok(m, nx, MO_Y[m + 1], false) then
							MO_X[m + 1] = nx
						else
							if mobj_pos_ok(m, MO_X[m + 1], ny, false) then
								MO_Y[m + 1] = ny
							end
						end
					end
					MO_Z[m + 1] = (SE_FLOORH[sector_at(MO_X[m + 1], MO_Y[m + 1]) + 1] * 1.0)
					coroutine.yield()
				end
				::_jd_continue_20::
			end
		end
	end)
end
function monster_tic(m)
	if (MO_CHARGE[m + 1] > 0) then
		charge_tic(m)
		return
	end
	if (MO_STATE[m + 1] == 4) then
		return
	end
	local b = MO_BRAIN[m + 1]
	if ((b == NULL) or b:done()) then
		MO_BRAIN[m + 1] = monster_brain(m)
	end
	MO_BRAIN[m + 1]:resume()
end
function mobj_pos_ok(m, x, y, fly)
	local r = MO_RAD[m + 1]
	local n = (#LD_V1)
	local fz = MO_Z[m + 1]
	for i = 0, (n - 1), 1 do
		do
			local ax = (VX_X[LD_V1[i + 1] + 1] + 0.0)
			local ay = (VX_Y[LD_V1[i + 1] + 1] + 0.0)
			local bx = (VX_X[LD_V2[i + 1] + 1] + 0.0)
			local by = (VX_Y[LD_V2[i + 1] + 1] + 0.0)
			local ddx = (bx - ax)
			local ddy = (by - ay)
			local len2 = ((ddx * ddx) + (ddy * ddy))
			if (len2 < 0.001) then
				goto _jd_continue_22
			end
			local t = ((((x - ax) * ddx) + ((y - ay) * ddy)) / len2)
			if (t < 0.0) then
				t = 0.0
			end
			if (t > 1.0) then
				t = 1.0
			end
			local cx = ((ax + (ddx * t)) - x)
			local cy = ((ay + (ddy * t)) - y)
			if (((cx * cx) + (cy * cy)) >= (r * r)) then
				goto _jd_continue_22
			end
			local s1 = LD_SIDE1[i + 1]
			if (s1 < 0) then
				return false
			end
			if ((LD_FLAGS[i + 1] & 1) ~= 0) then
				return false
			end
			local f0 = SE_FLOORH[SD_SEC[LD_SIDE0[i + 1] + 1] + 1]
			local f1 = SE_FLOORH[SD_SEC[s1 + 1] + 1]
			local c0 = SE_CEILH[SD_SEC[LD_SIDE0[i + 1] + 1] + 1]
			local c1 = SE_CEILH[SD_SEC[s1 + 1] + 1]
			local ob = (((f0 > f1) and f0 or f1) + 0.0)
			local ot = (((c0 < c1) and c0 or c1) + 0.0)
			if ((ot - ob) < 56.0) then
				return false
			end
			if fly then
				goto _jd_continue_22
			end
			if ((ob - fz) > 24.0) then
				return false
			end
			local lower = (((f0 < f1) and f0 or f1) + 0.0)
			if ((fz - lower) > 24.0) then
				return false
			end
		end
		::_jd_continue_22::
	end
	local nm = (#MO_X)
	for o = 0, (nm - 1), 1 do
		do
			if (o == m) then
				goto _jd_continue_23
			end
			local st = MO_STATE[o + 1]
			if ((((((st ~= 1) and (st ~= 2)) and (st ~= 3)) and (st ~= 4)) and (st ~= 9)) and (not ((st == 10) and (MO_RAD[o + 1] > 0.0)))) then
				goto _jd_continue_23
			end
			local odx = (MO_X[o + 1] - x)
			local ody = (MO_Y[o + 1] - y)
			local rr = (MO_RAD[o + 1] + r)
			if (((odx * odx) + (ody * ody)) < (rr * rr)) then
				return false
			end
		end
		::_jd_continue_23::
	end
	return true
end
function mobj_anim_tic(m)
	if (MO_TIC[m + 1] > 0) then
		MO_TIC[m + 1] = (MO_TIC[m + 1] - 1)
		return
	end
	local st = MO_STATE[m + 1]
	MO_FRAME[m + 1] = (MO_FRAME[m + 1] + 1)
	if (MO_FRAME[m + 1] >= (#MO_ANIM[m + 1])) then
		if (st == 5) then
			if ((MO_TYPE[m + 1] == 0) or (MO_TYPE[m + 1] == 2035)) then
				MO_STATE[m + 1] = 0
			else
				MO_STATE[m + 1] = 6
				if (NIGHTMARE and (MINFO[MO_TYPE[m + 1]] ~= nil)) then
					MO_RESP[m + 1] = 1050
				end
			end
			MO_FRAME[m + 1] = ((#MO_ANIM[m + 1]) - 1)
			return
		end
		if ((st == 4) or (st == 3)) then
			if (st == 3) then
				monster_attack(m)
			end
			local mi = MINFO[MO_TYPE[m + 1]]
			MO_STATE[m + 1] = 2
			MO_ANIM[m + 1] = mi[6 + 1]
		end
		MO_FRAME[m + 1] = 0
	end
	MO_TIC[m + 1] = ((st == 2) and 5 or 5)
end
function monster_attack(m)
	local mi = MINFO[MO_TYPE[m + 1]]
	local kind = _j_int(mi[10 + 1])
	target_info(m)
	local dx = (TGT_X - MO_X[m + 1])
	local dy = (TGT_Y - MO_Y[m + 1])
	local dist = math.sqrt(((dx * dx) + (dy * dy)))
	MO_COOL[m + 1] = (25 + _j_random(0, 20))
	if FAST_MONSTERS then
		MO_COOL[m + 1] = _j_idiv(MO_COOL[m + 1], 2)
	end
	MO_MOMX[m + 1] = 0.0
	MO_MOMY[m + 1] = 0.0
	if (not TGT_LIVE) then
		return
	end
	local t = MO_TARGET[m + 1]
	local melee_r = ((kind == 1) and (1.0 * mi[12 + 1]) or 64.0)
	if (((kind == 1) or (kind == 3)) and (dist < (melee_r + 24.0))) then
		snd_play_at(((kind == 1) and "DSSGTATK" or "DSCLAW"), MO_X[m + 1], MO_Y[m + 1])
		local mdmg = _j_int(_j_add(mi[11 + 1], _j_random(0, mi[11 + 1])))
		if ((t >= 0) and (MO_STATE[t + 1] == 11)) then
			np_ghost_hit_mobj(t, mdmg, MO_X[m + 1], MO_Y[m + 1])
		else
			if (t >= 0) then
				mobj_damage(t, mdmg, m)
			else
				player_damage_from(mdmg, MO_X[m + 1], MO_Y[m + 1])
			end
		end
		return
	end
	local eyez = (MO_Z[m + 1] + 32.0)
	local wob = (((t < 0) and (P_INVIS > 0)) and 0.9 or 0.12)
	if (kind == 0) then
		snd_play_at(((MO_TYPE[m + 1] == 9) and "DSSHOTGN" or "DSPISTOL"), MO_X[m + 1], MO_Y[m + 1])
		local aim = (math.atan(dy, dx) + ((_j_random() - 0.5) * wob))
		ATK_AIM = aim
		shoot_hitscan(MO_X[m + 1], MO_Y[m + 1], eyez, aim, (3 * _j_random(1, 5)), m, 2048.0)
	else
		if ((kind == 2) or (kind == 3)) then
			local maim = math.atan(dy, dx)
			if ((t < 0) and (P_INVIS > 0)) then
				maim = (maim + ((_j_random() - 0.5) * 0.9))
			end
			ATK_AIM = maim
			local bspr = "BAL1"
			if (MISSILE_SPR[MO_TYPE[m + 1]] ~= nil) then
				bspr = ("" .. MISSILE_SPR[MO_TYPE[m + 1]])
			end
			projectile_spawn((MO_X[m + 1] + (math.cos(MO_ANG[m + 1]) * 20.0)), (MO_Y[m + 1] + (math.sin(MO_ANG[m + 1]) * 20.0)), eyez, maim, m, TGT_MIDZ, dist, bspr, "AB", 10.0, "DSFIRSHT")
		end
	end
end
ATK_AIM = 0.0
function mobj_respawn_tic(m)
	MO_RESP[m + 1] = (MO_RESP[m + 1] - 1)
	if (MO_RESP[m + 1] > 0) then
		return
	end
	MO_RESP[m + 1] = 0
	local mi = MINFO[MO_TYPE[m + 1]]
	MO_STATE[m + 1] = 1
	MO_HP[m + 1] = MO_SHP[m + 1]
	MO_ANIM[m + 1] = mi[6 + 1]
	MO_FRAME[m + 1] = 0
	MO_TIC[m + 1] = 4
	MO_COOL[m + 1] = 0
	MO_CHARGE[m + 1] = 0
	MO_BRAIN[m + 1] = NULL
	snd_play_at("DSTELEPT", MO_X[m + 1], MO_Y[m + 1])
	if (TFOG_ANIM ~= "") then
		decor_spawn(MO_X[m + 1], MO_Y[m + 1], MO_Z[m + 1], "TFOG", TFOG_ANIM)
	end
end
function things_tic()
	local n = (#MO_X)
	for m = 0, (n - 1), 1 do
		do
			local st = MO_STATE[m + 1]
			if (st == 6) then
				if (NIGHTMARE and (MO_RESP[m + 1] > 0)) then
					mobj_respawn_tic(m)
				end
				goto _jd_continue_24
			end
			if (st == 10) then
				if ((#MO_ANIM[m + 1]) > 1) then
					mobj_anim_tic(m)
				end
				goto _jd_continue_24
			end
			if ((((st == 0) or (st == 7)) or (st == 11)) or (st == 12)) then
				goto _jd_continue_24
			end
			if (st == 8) then
				projectile_tic(m)
				mobj_anim_tic(m)
				goto _jd_continue_24
			end
			if (st == 9) then
				mobj_anim_tic(m)
				goto _jd_continue_24
			end
			if (st == 5) then
				mobj_anim_tic(m)
				goto _jd_continue_24
			end
			monster_tic(m)
			mobj_anim_tic(m)
		end
		::_jd_continue_24::
	end
end
