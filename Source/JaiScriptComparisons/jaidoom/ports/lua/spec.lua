-- Generated from spec.jai by tools/transpile.py; see internal/examples/jaidoom/ports/PORTING.md.
require("wad")
require("map")
SECRETS_FOUND = 0
SECRET_TOTAL = 0
BOSS_PEND = 0
SPEC_TIC0 = 0
CROSS_SIDE = 0
CLOSE30_SECS = _j_array({})
OPEN300_SECS = _j_array({})
MV_SEC = _j_array({})
MOVERS = _j_array({})
Mover = {}
Mover.__index = Mover
function Mover.new(s, pl, t, sp, w, r)
	local self = setmetatable({}, Mover)
	self.sec = 0
	self.plane = 0
	self.target = 0
	self.speed = 0
	self.wait = 0
	self.ret = 0
	self.co = NULL
	self.sec = s
	self.plane = pl
	self.target = t
	self.speed = sp
	self.wait = w
	self.ret = r
	self.co = mover_run(s, pl, t, sp, w, r)
	return self
end
function Mover:tic()
	self.co:resume()
	return self.co:done()
end
SPEC_IMPL = {}
SPEC_COSMETIC = {}
SECSPEC_IMPL = {}
SECSPEC_COSMETIC = {}
TABLES_BUILT = false
function spec_tables()
	if TABLES_BUILT then
		return
	end
	TABLES_BUILT = true
	local li = _j_array({1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 16, 18, 20, 21, 22, 23, 26, 27, 28, 29, 31, 32, 33, 34, 35, 36, 38, 39, 42, 46, 51, 52, 61, 62, 63, 70, 76, 79, 80, 81, 82, 86, 88, 90, 91, 97, 98, 102, 103, 119, 121})
	do
		local i = 0
		while (i < (#li)) do
			do
				SPEC_IMPL[li[i + 1]] = 1
			end
			::_jd_continue_1::
			i = i + 1
		end
	end
	SPEC_COSMETIC[48] = 1
	local si = _j_array({1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17})
	do
		local j = 0
		while (j < (#si)) do
			do
				SECSPEC_IMPL[si[j + 1]] = 1
			end
			::_jd_continue_2::
			j = j + 1
		end
	end
end
function spec_line_known(s)
	return ((SPEC_IMPL[s] ~= nil) or (SPEC_COSMETIC[s] ~= nil))
end
function spec_sector_known(s)
	return ((SECSPEC_IMPL[s] ~= nil) or (SECSPEC_COSMETIC[s] ~= nil))
end
function spec_init()
	spec_tables()
	MV_SEC = _j_array({})
	MOVERS = _j_array({})
	SECRETS_FOUND = 0
	SECRET_TOTAL = 0
	CLOSE30_SECS = _j_array({})
	OPEN300_SECS = _j_array({})
	LT_SEC = _j_array({})
	LT_KIND = _j_array({})
	LT_MAX = _j_array({})
	LT_MIN = _j_array({})
	LT_CNT = _j_array({})
	LT_AUX = _j_array({})
	SPEC_TIC0 = GAME_TIC
	local nsec = (#SE_SPECIAL)
	for s = 0, (nsec - 1), 1 do
		do
			local sp = SE_SPECIAL[s + 1]
			if (sp == 9) then
				SECRET_TOTAL = (SECRET_TOTAL + 1)
			else
				if (sp == 10) then
					table.insert(CLOSE30_SECS, s)
				else
					if (sp == 14) then
						table.insert(OPEN300_SECS, s)
					else
						if (((((((sp == 1) or (sp == 2)) or (sp == 3)) or (sp == 8)) or (sp == 12)) or (sp == 13)) or (sp == 17)) then
							light_spawn(s, sp)
						end
					end
				end
			end
		end
		::_jd_continue_3::
	end
	BOSS_PEND = 0
	if (GAME_MAP == "E1M8") then
		local nm = (#MO_TYPE)
		for m = 0, (nm - 1), 1 do
			do
				if (MO_TYPE[m + 1] == 3003) then
					BOSS_PEND = (BOSS_PEND + 1)
				end
			end
			::_jd_continue_4::
		end
	end
end
function neighbor_floor(sec, mode)
	local own = SE_FLOORH[sec + 1]
	local best = own
	if (mode == 1) then
		best = -500
	end
	if (mode == 2) then
		best = 32767
	end
	local nlines = (#LD_V1)
	for i = 0, (nlines - 1), 1 do
		do
			local s0 = LD_SIDE0[i + 1]
			local s1 = LD_SIDE1[i + 1]
			if ((s0 < 0) or (s1 < 0)) then
				goto _jd_continue_5
			end
			local a = SD_SEC[s0 + 1]
			local b = SD_SEC[s1 + 1]
			local other = -1
			if (a == sec) then
				other = b
			else
				if (b == sec) then
					other = a
				end
			end
			if ((other < 0) or (other == sec)) then
				goto _jd_continue_5
			end
			local f = SE_FLOORH[other + 1]
			if (mode == 0) then
				if (f < best) then
					best = f
				end
			else
				if (mode == 1) then
					if (f > best) then
						best = f
					end
				else
					if ((f > own) and (f < best)) then
						best = f
					end
				end
			end
		end
		::_jd_continue_5::
	end
	if ((mode == 2) and (best == 32767)) then
		best = own
	end
	return best
end
function neighbor_light(sec)
	local best = SE_LIGHT[sec + 1]
	local nlines = (#LD_V1)
	for i = 0, (nlines - 1), 1 do
		do
			local s0 = LD_SIDE0[i + 1]
			local s1 = LD_SIDE1[i + 1]
			if ((s0 < 0) or (s1 < 0)) then
				goto _jd_continue_6
			end
			local a = SD_SEC[s0 + 1]
			local b = SD_SEC[s1 + 1]
			local other = -1
			if (a == sec) then
				other = b
			else
				if (b == sec) then
					other = a
				end
			end
			if ((other < 0) or (other == sec)) then
				goto _jd_continue_6
			end
			if (SE_LIGHT[other + 1] > best) then
				best = SE_LIGHT[other + 1]
			end
		end
		::_jd_continue_6::
	end
	return best
end
function neighbor_light_min(sec)
	local best = SE_LIGHT[sec + 1]
	local nlines = (#LD_V1)
	for i = 0, (nlines - 1), 1 do
		do
			local s0 = LD_SIDE0[i + 1]
			local s1 = LD_SIDE1[i + 1]
			if ((s0 < 0) or (s1 < 0)) then
				goto _jd_continue_7
			end
			local a = SD_SEC[s0 + 1]
			local b = SD_SEC[s1 + 1]
			local other = -1
			if (a == sec) then
				other = b
			else
				if (b == sec) then
					other = a
				end
			end
			if ((other < 0) or (other == sec)) then
				goto _jd_continue_7
			end
			if (SE_LIGHT[other + 1] < best) then
				best = SE_LIGHT[other + 1]
			end
		end
		::_jd_continue_7::
	end
	return best
end
LT_SEC = _j_array({})
LT_KIND = _j_array({})
LT_MAX = _j_array({})
LT_MIN = _j_array({})
LT_CNT = _j_array({})
LT_AUX = _j_array({})
function light_spawn(s, sp)
	local base = SE_LIGHT[s + 1]
	local mn = neighbor_light_min(s)
	if (sp == 1) then
		table.insert(LT_SEC, s)
		table.insert(LT_KIND, 1)
		table.insert(LT_MAX, base)
		table.insert(LT_MIN, mn)
		table.insert(LT_CNT, _j_random(1, 64))
		table.insert(LT_AUX, 0)
	else
		if (sp == 8) then
			table.insert(LT_SEC, s)
			table.insert(LT_KIND, 3)
			table.insert(LT_MAX, base)
			table.insert(LT_MIN, mn)
			table.insert(LT_CNT, 0)
			table.insert(LT_AUX, -1)
		else
			if (sp == 17) then
				table.insert(LT_SEC, s)
				table.insert(LT_KIND, 4)
				table.insert(LT_MAX, base)
				table.insert(LT_MIN, (mn + 16))
				table.insert(LT_CNT, 4)
				table.insert(LT_AUX, 0)
			else
				if (mn == base) then
					mn = 0
				end
				table.insert(LT_SEC, s)
				table.insert(LT_KIND, 2)
				table.insert(LT_MAX, base)
				table.insert(LT_MIN, mn)
				table.insert(LT_CNT, (((sp == 12) or (sp == 13)) and 1 or _j_random(1, 8)))
				table.insert(LT_AUX, (((sp == 2) or (sp == 13)) and 15 or 35))
			end
		end
	end
end
function lights_tic()
	local n = (#LT_SEC)
	for i = 0, (n - 1), 1 do
		do
			local s = LT_SEC[i + 1]
			local kind = LT_KIND[i + 1]
			if (kind == 3) then
				local l = SE_LIGHT[s + 1]
				if (LT_AUX[i + 1] < 0) then
					l = (l - 8)
					if (l <= LT_MIN[i + 1]) then
						l = LT_MIN[i + 1]
						LT_AUX[i + 1] = 1
					end
				else
					l = (l + 8)
					if (l >= LT_MAX[i + 1]) then
						l = LT_MAX[i + 1]
						LT_AUX[i + 1] = -1
					end
				end
				SE_LIGHT[s + 1] = l
				goto _jd_continue_8
			end
			LT_CNT[i + 1] = (LT_CNT[i + 1] - 1)
			if (LT_CNT[i + 1] > 0) then
				goto _jd_continue_8
			end
			if (kind == 1) then
				if (SE_LIGHT[s + 1] == LT_MAX[i + 1]) then
					SE_LIGHT[s + 1] = LT_MIN[i + 1]
					LT_CNT[i + 1] = _j_random(1, 8)
				else
					SE_LIGHT[s + 1] = LT_MAX[i + 1]
					LT_CNT[i + 1] = _j_random(1, 64)
				end
			else
				if (kind == 2) then
					if (SE_LIGHT[s + 1] == LT_MIN[i + 1]) then
						SE_LIGHT[s + 1] = LT_MAX[i + 1]
						LT_CNT[i + 1] = 5
					else
						SE_LIGHT[s + 1] = LT_MIN[i + 1]
						LT_CNT[i + 1] = LT_AUX[i + 1]
					end
				else
					local l2 = (LT_MAX[i + 1] - (_j_random(0, 3) * 16))
					if (l2 < LT_MIN[i + 1]) then
						l2 = LT_MIN[i + 1]
					end
					SE_LIGHT[s + 1] = l2
					LT_CNT[i + 1] = 4
				end
			end
		end
		::_jd_continue_8::
	end
end
function mover_busy(sec)
	local n = (#MV_SEC)
	for i = 0, (n - 1), 1 do
		do
			if (MV_SEC[i + 1] == sec) then
				return true
			end
		end
		::_jd_continue_9::
	end
	return false
end
function sector_busy(sec)
	return (mover_busy(sec) or door_busy(sec))
end
function mover_start(sec, plane, target, speed, wait, ret)
	if sector_busy(sec) then
		return false
	end
	table.insert(MV_SEC, sec)
	table.insert(MOVERS, Mover.new(sec, plane, target, speed, wait, ret))
	return true
end
function step_toward(cur, target, speed)
	if (cur < target) then
		cur = (cur + speed)
		if (cur > target) then
			cur = target
		end
	else
		if (cur > target) then
			cur = (cur - speed)
			if (cur < target) then
				cur = target
			end
		end
	end
	return cur
end
function mover_run(sec, plane, target, speed, wait, ret)
	return _j_coroutine(function()
		local cur = ((plane == 0) and SE_FLOORH[sec + 1] or SE_CEILH[sec + 1])
		do
			while (cur ~= target) do
				do
					cur = step_toward(cur, target, speed)
					if (plane == 0) then
						SE_FLOORH[sec + 1] = cur
					else
						SE_CEILH[sec + 1] = cur
					end
					if ((cur == target) and (wait < 0)) then
						return
					end
					coroutine.yield()
				end
				::_jd_continue_10::
			end
		end
		if (wait < 0) then
			return
		end
		for t = 0, (wait - 1), 1 do
			do
				coroutine.yield()
			end
			::_jd_continue_11::
		end
		do
			while (cur ~= ret) do
				do
					cur = step_toward(cur, ret, speed)
					if (plane == 0) then
						SE_FLOORH[sec + 1] = cur
					else
						SE_CEILH[sec + 1] = cur
					end
					if (cur == ret) then
						return
					end
					coroutine.yield()
				end
				::_jd_continue_12::
			end
		end
	end)
end
function movers_tic()
	local i = 0
	do
		while (i < (#MOVERS)) do
			do
				local mv = MOVERS[i + 1]
				if mv:tic() then
					table.remove(MV_SEC, i + 1)
					table.remove(MOVERS, i + 1)
				else
					i = (i + 1)
				end
			end
			::_jd_continue_13::
		end
	end
end
function tag_doors(tag, kind)
	local any = false
	local nsec = (#SE_TAG)
	for s = 0, (nsec - 1), 1 do
		do
			if (SE_TAG[s + 1] ~= tag) then
				goto _jd_continue_14
			end
			if (kind == 0) then
				door_use(s, false)
			else
				if (kind == 1) then
					door_use(s, true)
				else
					if (kind == 2) then
						door_close(s)
					else
						mover_start(s, 1, SE_FLOORH[s + 1], 2, 1050, SE_CEILH[s + 1])
					end
				end
			end
			any = true
		end
		::_jd_continue_14::
	end
	return any
end
function tag_plats(tag)
	local any = false
	local nsec = (#SE_TAG)
	for s = 0, (nsec - 1), 1 do
		do
			if (SE_TAG[s + 1] ~= tag) then
				goto _jd_continue_15
			end
			local low = neighbor_floor(s, 0)
			if mover_start(s, 0, low, 4, 105, SE_FLOORH[s + 1]) then
				any = true
			end
		end
		::_jd_continue_15::
	end
	return any
end
function adopt_floorpic(s, from)
	SE_FLOORPIC[s + 1] = SE_FLOORPIC[from + 1]
	if ((s < (#SEC_FFLAT)) and (from < (#SEC_FFLAT))) then
		SEC_FFLAT[s + 1] = SEC_FFLAT[from + 1]
	end
end
function tag_floor(tag, mode, speed, texline)
	local any = false
	local nsec = (#SE_TAG)
	for s = 0, (nsec - 1), 1 do
		do
			if (SE_TAG[s + 1] ~= tag) then
				goto _jd_continue_16
			end
			if sector_busy(s) then
				goto _jd_continue_16
			end
			local cur = SE_FLOORH[s + 1]
			local t = cur
			if (mode == 0) then
				t = neighbor_floor(s, 0)
			else
				if (mode == 1) then
					t = neighbor_floor(s, 1)
					if ((t == -500) or (t > cur)) then
						t = cur
					end
				else
					if (mode == 2) then
						t = neighbor_floor(s, 2)
					else
						if (mode == 3) then
							t = neighbor_floor(s, 1)
							if (t == -500) then
								t = cur
							end
							if (t ~= cur) then
								t = (t + 8)
							end
							if (t > cur) then
								t = cur
							end
						else
							t = lowest_neighbor_ceiling(s)
							if (t > SE_CEILH[s + 1]) then
								t = SE_CEILH[s + 1]
							end
							if (t < cur) then
								t = cur
							end
						end
					end
				end
			end
			if (texline >= 0) then
				local fs = LD_SIDE0[texline + 1]
				if (fs >= 0) then
					adopt_floorpic(s, SD_SEC[fs + 1])
				end
			end
			mover_start(s, 0, t, speed, -1, 0)
			any = true
		end
		::_jd_continue_16::
	end
	return any
end
function tag_lights(tag, level)
	local any = false
	local nsec = (#SE_TAG)
	for s = 0, (nsec - 1), 1 do
		do
			if (SE_TAG[s + 1] ~= tag) then
				goto _jd_continue_17
			end
			if (level < 0) then
				SE_LIGHT[s + 1] = neighbor_light(s)
			else
				SE_LIGHT[s + 1] = level
			end
			any = true
		end
		::_jd_continue_17::
	end
	return any
end
function teleport_player(tag)
	if (CROSS_SIDE ~= 0) then
		return false
	end
	local nth = (#TH_TYPE)
	for t = 0, (nth - 1), 1 do
		do
			if (TH_TYPE[t + 1] ~= 14) then
				goto _jd_continue_18
			end
			local ds = sector_at((TH_X[t + 1] * 1.0), (TH_Y[t + 1] * 1.0))
			if (SE_TAG[ds + 1] ~= tag) then
				goto _jd_continue_18
			end
			P_X = (TH_X[t + 1] * 1.0)
			P_Y = (TH_Y[t + 1] * 1.0)
			P_ANG = ((TH_ANG[t + 1] * PI) / 180.0)
			P_MOMX = 0.0
			P_MOMY = 0.0
			P_FLOORZ = SE_FLOORH[ds + 1]
			P_Z = (P_FLOORZ * 1.0)
			P_MOMZ = 0.0
			P_AIR = false
			P_VIEWZ = (P_FLOORZ + 41.0)
			return true
		end
		::_jd_continue_18::
	end
	return false
end
function build_stairs(tag)
	local any = false
	local nsec = (#SE_TAG)
	local nlines = (#LD_V1)
	for s = 0, (nsec - 1), 1 do
		do
			if (SE_TAG[s + 1] ~= tag) then
				goto _jd_continue_19
			end
			if sector_busy(s) then
				goto _jd_continue_19
			end
			local tex = SE_FLOORPIC[s + 1]
			local h = (SE_FLOORH[s + 1] + 8)
			mover_start(s, 0, h, 1, -1, 0)
			any = true
			local cur = s
			local found = true
			do
				while found do
					do
						found = false
						for i = 0, (nlines - 1), 1 do
							do
								if (LD_SIDE1[i + 1] < 0) then
									goto _jd_continue_21
								end
								local front = SD_SEC[LD_SIDE0[i + 1] + 1]
								if (front ~= cur) then
									goto _jd_continue_21
								end
								local back = SD_SEC[LD_SIDE1[i + 1] + 1]
								if (SE_FLOORPIC[back + 1] ~= tex) then
									goto _jd_continue_21
								end
								if sector_busy(back) then
									goto _jd_continue_21
								end
								h = (h + 8)
								mover_start(back, 0, h, 1, -1, 0)
								cur = back
								found = true
								break
							end
							::_jd_continue_21::
						end
					end
					::_jd_continue_20::
				end
			end
		end
		::_jd_continue_19::
	end
	return any
end
function donut(tag)
	local any = false
	local nsec = (#SE_TAG)
	local nlines = (#LD_V1)
	for s = 0, (nsec - 1), 1 do
		do
			if (SE_TAG[s + 1] ~= tag) then
				goto _jd_continue_22
			end
			if sector_busy(s) then
				goto _jd_continue_22
			end
			local ring = -1
			for i = 0, (nlines - 1), 1 do
				do
					if (LD_SIDE1[i + 1] < 0) then
						goto _jd_continue_23
					end
					local a = SD_SEC[LD_SIDE0[i + 1] + 1]
					local b = SD_SEC[LD_SIDE1[i + 1] + 1]
					if ((a == s) and (b ~= s)) then
						ring = b
						break
					end
					if ((b == s) and (a ~= s)) then
						ring = a
						break
					end
				end
				::_jd_continue_23::
			end
			if ((ring < 0) or sector_busy(ring)) then
				goto _jd_continue_22
			end
			local outer = -1
			for j = 0, (nlines - 1), 1 do
				do
					if (LD_SIDE1[j + 1] < 0) then
						goto _jd_continue_24
					end
					local a2 = SD_SEC[LD_SIDE0[j + 1] + 1]
					local b2 = SD_SEC[LD_SIDE1[j + 1] + 1]
					local other = -1
					if ((a2 == ring) and (b2 ~= ring)) then
						other = b2
					else
						if ((b2 == ring) and (a2 ~= ring)) then
							other = a2
						end
					end
					if ((other < 0) or (other == s)) then
						goto _jd_continue_24
					end
					outer = other
					break
				end
				::_jd_continue_24::
			end
			if (outer < 0) then
				goto _jd_continue_22
			end
			local h = SE_FLOORH[outer + 1]
			adopt_floorpic(ring, outer)
			SE_SPECIAL[ring + 1] = 0
			mover_start(ring, 0, h, 1, -1, 0)
			mover_start(s, 0, h, 1, -1, 0)
			any = true
		end
		::_jd_continue_22::
	end
	return any
end
function spec_line(ld, act)
	local sp = LD_SPECIAL[ld + 1]
	local tag = LD_TAG[ld + 1]
	local ok = false
	local once = false
	if (act == 1) then
		if (sp == 2) then
			ok = tag_doors(tag, 1)
			once = true
		else
			if (sp == 3) then
				ok = tag_doors(tag, 2)
				once = true
			else
				if (sp == 4) then
					ok = tag_doors(tag, 0)
					once = true
				else
					if (sp == 5) then
						ok = tag_floor(tag, 4, 1, -1)
						once = true
					else
						if (sp == 8) then
							ok = build_stairs(tag)
							once = true
						else
							if ((sp == 10) or (sp == 121)) then
								ok = tag_plats(tag)
								once = true
							else
								if (sp == 12) then
									ok = tag_lights(tag, -1)
									once = true
								else
									if (sp == 13) then
										ok = tag_lights(tag, 255)
										once = true
									else
										if (sp == 16) then
											ok = tag_doors(tag, 3)
											once = true
										else
											if (sp == 22) then
												ok = tag_floor(tag, 2, 1, ld)
												once = true
											else
												if (sp == 35) then
													ok = tag_lights(tag, 35)
													once = true
												else
													if (sp == 36) then
														ok = tag_floor(tag, 3, 4, -1)
														once = true
													else
														if (sp == 38) then
															ok = tag_floor(tag, 0, 1, -1)
															once = true
														else
															if (sp == 39) then
																ok = teleport_player(tag)
																once = true
															else
																if (sp == 52) then
																	LEVEL_DONE = true
																	ok = true
																	once = true
																else
																	if (sp == 119) then
																		ok = tag_floor(tag, 2, 1, -1)
																		once = true
																	else
																		if (sp == 76) then
																			ok = tag_doors(tag, 3)
																		else
																			if (sp == 79) then
																				ok = tag_lights(tag, 35)
																			else
																				if (sp == 80) then
																					ok = tag_lights(tag, -1)
																				else
																					if (sp == 81) then
																						ok = tag_lights(tag, 255)
																					else
																						if (sp == 82) then
																							ok = tag_floor(tag, 0, 1, -1)
																						else
																							if (sp == 86) then
																								ok = tag_doors(tag, 1)
																							else
																								if (sp == 88) then
																									ok = tag_plats(tag)
																								else
																									if (sp == 90) then
																										ok = tag_doors(tag, 0)
																									else
																										if (sp == 91) then
																											ok = tag_floor(tag, 4, 1, -1)
																										else
																											if (sp == 97) then
																												ok = teleport_player(tag)
																											else
																												if (sp == 98) then
																													ok = tag_floor(tag, 3, 4, -1)
																												end
																											end
																										end
																									end
																								end
																							end
																						end
																					end
																				end
																			end
																		end
																	end
																end
															end
														end
													end
												end
											end
										end
									end
								end
							end
						end
					end
				end
			end
		end
	else
		if (act == 0) then
			if (sp == 7) then
				ok = build_stairs(tag)
				once = true
			else
				if (sp == 9) then
					ok = donut(tag)
					once = true
				else
					if (sp == 18) then
						ok = tag_floor(tag, 2, 1, -1)
						once = true
					else
						if (sp == 20) then
							ok = tag_floor(tag, 2, 1, ld)
							once = true
						else
							if (sp == 21) then
								ok = tag_plats(tag)
								once = true
							else
								if (sp == 23) then
									ok = tag_floor(tag, 0, 1, -1)
									once = true
								else
									if (sp == 29) then
										ok = tag_doors(tag, 0)
										once = true
									else
										if (sp == 102) then
											ok = tag_floor(tag, 1, 1, -1)
											once = true
										else
											if (sp == 103) then
												ok = tag_doors(tag, 1)
												once = true
											else
												if (sp == 42) then
													ok = tag_doors(tag, 2)
												else
													if (sp == 61) then
														ok = tag_doors(tag, 1)
													else
														if (sp == 62) then
															ok = tag_plats(tag)
														else
															if (sp == 63) then
																ok = tag_doors(tag, 0)
															else
																if (sp == 70) then
																	ok = tag_floor(tag, 3, 4, -1)
																end
															end
														end
													end
												end
											end
										end
									end
								end
							end
						end
					end
				end
			end
		else
			if (sp == 46) then
				ok = tag_doors(tag, 1)
			end
		end
	end
	if (ok and once) then
		LD_SPECIAL[ld + 1] = 0
	end
	return ok
end
function line_side_pt(ld, x, y)
	local x1 = (VX_X[LD_V1[ld + 1] + 1] + 0.0)
	local y1 = (VX_Y[LD_V1[ld + 1] + 1] + 0.0)
	local ldx = (VX_X[LD_V2[ld + 1] + 1] - x1)
	local ldy = (VX_Y[LD_V2[ld + 1] + 1] - y1)
	local cross = ((ldx * (y - y1)) - (ldy * (x - x1)))
	return ((cross < 0.0) and 0 or 1)
end
function cross_check(ox, oy, nx, ny)
	local lox = ((ox < nx) and ox or nx)
	local hix = ((ox > nx) and ox or nx)
	local loy = ((oy < ny) and oy or ny)
	local hiy = ((oy > ny) and oy or ny)
	local bx0 = _j_ifloor(((lox - BMAP_ORGX) / 128.0))
	local bx1 = _j_ifloor(((hix - BMAP_ORGX) / 128.0))
	local by0 = _j_ifloor(((loy - BMAP_ORGY) / 128.0))
	local by1 = _j_ifloor(((hiy - BMAP_ORGY) / 128.0))
	if (bx0 < 0) then
		bx0 = 0
	end
	if (by0 < 0) then
		by0 = 0
	end
	if (bx1 >= BMAP_W) then
		bx1 = (BMAP_W - 1)
	end
	if (by1 >= BMAP_H) then
		by1 = (BMAP_H - 1)
	end
	local seen = _j_array({})
	local bm = BMAP
	for cy = by0, by1, 1 do
		do
			for cx = bx0, bx1, 1 do
				do
					local i = (bm[((4 + (cy * BMAP_W)) + cx) + 1] & 65535)
					if (bm[i + 1] == 0) then
						i = (i + 1)
					end
					do
						while (bm[i + 1] ~= -1) do
							do
								local ld = (bm[i + 1] & 65535)
								i = (i + 1)
								if (LD_SPECIAL[ld + 1] == 0) then
									goto _jd_continue_27
								end
								local dup = false
								do
									local k = 0
									while (k < (#seen)) do
										do
											if (seen[k + 1] == ld) then
												dup = true
												break
											end
										end
										::_jd_continue_28::
										k = k + 1
									end
								end
								if dup then
									goto _jd_continue_27
								end
								table.insert(seen, ld)
								local oside = line_side_pt(ld, ox, oy)
								if (line_side_pt(ld, nx, ny) == oside) then
									goto _jd_continue_27
								end
								local x1 = (VX_X[LD_V1[ld + 1] + 1] + 0.0)
								local y1 = (VX_Y[LD_V1[ld + 1] + 1] + 0.0)
								local x2 = (VX_X[LD_V2[ld + 1] + 1] + 0.0)
								local y2 = (VX_Y[LD_V2[ld + 1] + 1] + 0.0)
								local d = (((x2 - x1) * (ny - oy)) - ((y2 - y1) * (nx - ox)))
								if ((d > -0.0001) and (d < 0.0001)) then
									goto _jd_continue_27
								end
								local u = ((((ox - x1) * (ny - oy)) - ((oy - y1) * (nx - ox))) / d)
								if ((u < 0.0) or (u > 1.0)) then
									goto _jd_continue_27
								end
								CROSS_SIDE = oside
								if ((NET_GAME and NP_COOP) and (not NP_IS_HOST)) then
									np_cross_send(ld, oside)
								else
									spec_line(ld, 1)
								end
							end
							::_jd_continue_27::
						end
					end
				end
				::_jd_continue_26::
			end
		end
		::_jd_continue_25::
	end
end
function boss_check()
	local nm = (#MO_TYPE)
	for m = 0, (nm - 1), 1 do
		do
			if (MO_TYPE[m + 1] ~= 3003) then
				goto _jd_continue_29
			end
			local st = MO_STATE[m + 1]
			if ((((st == 1) or (st == 2)) or (st == 3)) or (st == 4)) then
				return
			end
		end
		::_jd_continue_29::
	end
	BOSS_PEND = 0
	local nsec = (#SE_TAG)
	for s = 0, (nsec - 1), 1 do
		do
			if (SE_TAG[s + 1] ~= 666) then
				goto _jd_continue_30
			end
			local t = neighbor_floor(s, 0)
			mover_start(s, 0, t, 1, -1, 0)
		end
		::_jd_continue_30::
	end
	set_msg("the walls fall")
end
function spec_tic()
	local authority = (not ((NET_GAME and NP_COOP) and (not NP_IS_HOST)))
	if authority then
		movers_tic()
		local dt = _j_int((GAME_TIC - SPEC_TIC0))
		if (dt == 1050) then
			do
				local c = 0
				while (c < (#CLOSE30_SECS)) do
					do
						door_close(CLOSE30_SECS[c + 1])
					end
					::_jd_continue_31::
					c = c + 1
				end
			end
		end
		if (dt == 10500) then
			do
				local o = 0
				while (o < (#OPEN300_SECS)) do
					do
						door_use(OPEN300_SECS[o + 1], false)
					end
					::_jd_continue_32::
					o = o + 1
				end
			end
		end
	end
	if (P_HP > 0) then
		local ps = sector_at((P_X + 0.0), (P_Y + 0.0))
		local sp = SE_SPECIAL[ps + 1]
		if (sp == 9) then
			SECRETS_FOUND = (SECRETS_FOUND + 1)
			SE_SPECIAL[ps + 1] = 0
			set_msg("a secret is revealed!")
		else
			if (((sp ~= 0) and ((GAME_TIC & 31) == 0)) and (P_SUIT <= 0)) then
				if (sp == 7) then
					player_damage(5)
				else
					if (sp == 5) then
						player_damage(10)
					else
						if ((sp == 4) or (sp == 16)) then
							player_damage(20)
						else
							if (sp == 11) then
								player_damage(20)
								if (P_HP <= 10) then
									LEVEL_DONE = true
								end
							end
						end
					end
				end
			end
		end
	end
	if (authority and (BOSS_PEND > 0)) then
		boss_check()
	end
	lights_tic()
end
