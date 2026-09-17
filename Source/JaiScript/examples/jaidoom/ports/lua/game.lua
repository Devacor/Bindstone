-- Generated from game.jai by tools/transpile.py; see PORTING.md.
require("wad")
require("map")
require("vid")
require("sound")
require("mus")
require("things")
require("render")
require("spec")
require("anim")
require("demo")
P_X = 0.0
P_Y = 0.0
P_ANG = 0.0
P_MOMX = 0.0
P_MOMY = 0.0
P_FLOORZ = 0.0
P_VIEWZ = 41.0
P_RADIUS = 16.0
P_HEIGHT = 56.0
P_STEP = 24.0
P_Z = 0.0
P_MOMZ = 0.0
P_AIR = false
P_AIRZ0 = 0.0
CMD_FWD = 0.0
CMD_SIDE = 0.0
CMD_TURN = 0.0
CMD_USE = false
CMD_FIRE = false
USE_LATCH = false
P_HP = 100
P_ARMOR = 0
P_KEYS = 0
AMMO_BULLETS = 50
AMMO_SHELLS = 0
AMMO_ROCKETS = 0
AMMO_BULLETS_MAX = 200
AMMO_SHELLS_MAX = 50
AMMO_ROCKETS_MAX = 50
HAVE_SHOTGUN = false
HAVE_CHAINGUN = false
HAVE_LAUNCHER = false
P_BERSERK = false
BERSERK_TICS = 0
P_INVIS = 0
P_SUIT = 0
P_GOGGLES = 0
P_ALLMAP = false
P_BACKPACK = false
CUR_WEAPON = 2
P_GOD = false
P_NOCLIP = false
W_COOL = 0
FLASH_TICS = 0
PAIN_TICS = 0
BONUS_TICS = 0
GRIN_TICS = 0
FIRE_HELD = 0
FACE_SIDE = 0
FACE_PAIN_BIG = false
NET_GAME = false
NP_COOP = false
NP_IS_HOST = false
NP_FORWARD = false
GAME_MAP = ""
GAME_TIC = 0
LEVEL_START_TIC = 0
LEVEL_DONE = false
SECRET_EXIT = false
GAME_MSG = ""
MSG_TIMER = 0
DOORS = _j_array({})
function snd_sector(sec, name)
	if (not SND_ON) then
		return
	end
	local n = (#SEG_FSEC)
	for i = 0, (n - 1), 1 do
		do
			if (SEG_FSEC[i + 1] ~= sec) then
				goto _jd_continue_1
			end
			snd_play_at(name, ((VX_X[SG_V1[i + 1] + 1] + VX_X[SG_V2[i + 1] + 1]) * 0.5), ((VX_Y[SG_V1[i + 1] + 1] + VX_Y[SG_V2[i + 1] + 1]) * 0.5))
			return
		end
		::_jd_continue_1::
	end
	snd_play(name)
end
Door = {}
Door.__index = Door
function Door.new(s, st, stayopen)
	local self = setmetatable({}, Door)
	self.sec = 0
	self.state = 0
	self.timer = 0
	self.top = 0
	self.stay = 0
	self.sec = s
	self.state = st
	self.top = (lowest_neighbor_ceiling(s) - 4)
	self.stay = stayopen
	return self
end
function Door:tic()
	if (self.state == 0) then
		SE_CEILH[self.sec + 1] = (SE_CEILH[self.sec + 1] + 2)
		if (SE_CEILH[self.sec + 1] >= self.top) then
			SE_CEILH[self.sec + 1] = self.top
			if (self.stay ~= 0) then
				return true
			end
			self.state = 1
			self.timer = 150
		end
	else
		if (self.state == 1) then
			self.timer = (self.timer - 1)
			if (self.timer <= 0) then
				self.state = 2
				snd_sector(self.sec, "DSDORCLS")
			end
		else
			if (sector_at((P_X + 0.0), (P_Y + 0.0)) == self.sec) then
				self.state = 0
				snd_sector(self.sec, "DSDOROPN")
			else
				SE_CEILH[self.sec + 1] = (SE_CEILH[self.sec + 1] - 2)
				if (SE_CEILH[self.sec + 1] <= SE_FLOORH[self.sec + 1]) then
					SE_CEILH[self.sec + 1] = SE_FLOORH[self.sec + 1]
					return true
				end
			end
		end
	end
	return false
end
function set_msg(m)
	GAME_MSG = m
	MSG_TIMER = 70
end
function game_start_map(name)
	if (not map_load(name)) then
		return false
	end
	prepare_render()
	GAME_MAP = name
	LEVEL_START_TIC = GAME_TIC
	LEVEL_DONE = false
	SECRET_EXIT = false
	DOORS = _j_array({})
	weapons_init()
	P_BERSERK = false
	BERSERK_TICS = 0
	P_INVIS = 0
	P_SUIT = 0
	P_GOGGLES = 0
	P_ALLMAP = false
	P_MOMX = 0.0
	P_MOMY = 0.0
	local want_start = (((NET_GAME and NP_COOP) and (not NP_IS_HOST)) and 2 or 1)
	local nthings = (#TH_X)
	local placed = false
	for t = 0, (nthings - 1), 1 do
		do
			if (TH_TYPE[t + 1] == want_start) then
				P_X = TH_X[t + 1]
				P_Y = TH_Y[t + 1]
				P_ANG = ((TH_ANG[t + 1] * PI) / 180.0)
				placed = true
				break
			end
		end
		::_jd_continue_2::
	end
	if (not placed) then
		for t2 = 0, (nthings - 1), 1 do
			do
				if (TH_TYPE[t2 + 1] == 1) then
					P_X = (TH_X[t2 + 1] + 32.0)
					P_Y = TH_Y[t2 + 1]
					P_ANG = ((TH_ANG[t2 + 1] * PI) / 180.0)
					break
				end
			end
			::_jd_continue_3::
		end
	end
	P_FLOORZ = SE_FLOORH[sector_at((P_X + 0.0), (P_Y + 0.0)) + 1]
	P_Z = (P_FLOORZ * 1.0)
	P_MOMZ = 0.0
	P_AIR = false
	P_VIEWZ = (P_FLOORZ + 41.0)
	sprites_init()
	things_data_init()
	things_spawn_map()
	spec_init()
	anim_init()
	music_want(map_music(name))
	W_COOL = 0
	FLASH_TICS = 0
	PAIN_TICS = 0
	BONUS_TICS = 0
	set_msg(name)
	return true
end
function player_damage_from(dmg, sx, sy)
	if ((P_HP <= 0) or P_GOD) then
		return
	end
	local rel = (math.atan((sy - P_Y), (sx - P_X)) - P_ANG)
	do
		while (rel > PI) do
			do
				rel = (rel - TAU)
			end
			::_jd_continue_4::
		end
	end
	do
		while (rel < (0.0 - PI)) do
			do
				rel = (rel + TAU)
			end
			::_jd_continue_5::
		end
	end
	player_damage(dmg)
	if (rel > 0.39) then
		FACE_SIDE = -1
	else
		if (rel < -0.39) then
			FACE_SIDE = 1
		end
	end
end
function player_damage(dmg)
	if ((P_HP <= 0) or P_GOD) then
		return
	end
	FACE_SIDE = 0
	FACE_PAIN_BIG = (dmg >= 20)
	local soaked = ((P_ARMOR > 0) and _j_idiv(dmg, 3) or 0)
	if (soaked > P_ARMOR) then
		soaked = _j_int(P_ARMOR)
	end
	P_ARMOR = (P_ARMOR - soaked)
	P_HP = (P_HP - (dmg - soaked))
	PAIN_TICS = (PAIN_TICS + dmg)
	if (PAIN_TICS > 60) then
		PAIN_TICS = 60
	end
	if (P_HP <= 0) then
		P_HP = 0
		snd_play("DSPLDETH")
		set_msg("you died. enter restarts the map")
	else
		snd_play("DSPLPAIN")
	end
end
WEAPONS = _j_array({})
Weapon = {}
Weapon.__index = Weapon
function Weapon.new(n, sp, fl, am, dm, dr, pe, sd, rg, cd, ft)
	local self = setmetatable({}, Weapon)
	self.name = ""
	self.spr = ""
	self.flash = ""
	self.ammo = 0
	self.dmg_mul = 0
	self.dmg_roll = 1
	self.pellets = 1
	self.spread = 0.0
	self.range = 2048.0
	self.cooldown = 0
	self.flash_tics = 0
	self.missile = 0
	self.anim = ""
	self.snd = ""
	self.name = n
	self.spr = sp
	self.flash = fl
	self.ammo = am
	self.dmg_mul = dm
	self.dmg_roll = dr
	self.pellets = pe
	self.spread = sd
	self.range = rg
	self.cooldown = cd
	self.flash_tics = ft
	return self
end
function Weapon:view_frame()
	if (((W_COOL <= 0) or ((#self.anim) == 0)) or (self.cooldown <= 0)) then
		return "A"
	end
	local idx = _j_int(_j_div(((self.cooldown - W_COOL) * (#self.anim)), self.cooldown))
	if (idx >= (#self.anim)) then
		idx = ((#self.anim) - 1)
	end
	return _j_substr(self.anim, idx, 1)
end
function Weapon:fire()
	if (self.ammo == 1) then
		if (AMMO_BULLETS <= 0) then
			return
		end
		AMMO_BULLETS = (AMMO_BULLETS - 1)
	else
		if (self.ammo == 2) then
			if (AMMO_SHELLS <= 0) then
				return
			end
			AMMO_SHELLS = (AMMO_SHELLS - 1)
		else
			if (self.ammo == 3) then
				if (AMMO_ROCKETS <= 0) then
					return
				end
				AMMO_ROCKETS = (AMMO_ROCKETS - 1)
			end
		end
	end
	if (self.snd ~= "") then
		snd_play(self.snd)
	end
	local eyez = (P_FLOORZ + 41.0)
	if (self.missile ~= 0) then
		local rk = rocket_spawn((P_X + 0.0), (P_Y + 0.0), (P_FLOORZ + 32.0), P_ANG, -1)
		if NP_FORWARD then
			MO_NODMG[rk + 1] = 1
		end
	else
		local mul = self.dmg_mul
		if ((self.ammo == 0) and P_BERSERK) then
			mul = (mul * 10)
		end
		do
			local p = 0
			while (p < self.pellets) do
				do
					local aim = P_ANG
					if (self.spread > 0.0) then
						aim = (aim + ((_j_random() - 0.5) * self.spread))
					end
					shoot_hitscan((P_X + 0.0), (P_Y + 0.0), eyez, aim, (mul * _j_random(1, self.dmg_roll)), -1, self.range)
				end
				::_jd_continue_6::
				p = p + 1
			end
		end
	end
	W_COOL = self.cooldown
	if (self.flash_tics > 0) then
		FLASH_TICS = self.flash_tics
	end
end
function weapons_init()
	if ((#WEAPONS) > 0) then
		return
	end
	table.insert(WEAPONS, NULL)
	local fist = Weapon.new("fist", "PUNG", "PISF", 0, 2, 10, 1, 0.0, 64.0, 16, 0)
	fist.anim = "BCDCB"
	fist.snd = "DSPUNCH"
	table.insert(WEAPONS, fist)
	local pistol = Weapon.new("pistol", "PISG", "PISF", 1, 5, 3, 1, 0.04, 2048.0, 12, 3)
	pistol.anim = "BCB"
	pistol.snd = "DSPISTOL"
	table.insert(WEAPONS, pistol)
	local shotgun = Weapon.new("shotgun", "SHTG", "SHTF", 2, 5, 3, 7, 0.2, 2048.0, 32, 4)
	shotgun.anim = "AABCDCBA"
	shotgun.snd = "DSSHOTGN"
	table.insert(WEAPONS, shotgun)
	local chaingun = Weapon.new("chaingun", "CHGG", "CHGF", 1, 5, 3, 1, 0.06, 2048.0, 4, 2)
	chaingun.anim = "AB"
	chaingun.snd = "DSPISTOL"
	table.insert(WEAPONS, chaingun)
	local launcher = Weapon.new("rocket launcher", "MISG", "MISF", 3, 20, 8, 1, 0.0, 2048.0, 32, 4)
	launcher.anim = "BBAAAAAA"
	launcher.snd = "DSRLAUNC"
	launcher.missile = 1
	table.insert(WEAPONS, launcher)
end
function weapon_select(i)
	CUR_WEAPON = i
	local w = WEAPONS[i + 1]
	set_msg(w.name)
end
function player_fire()
	if ((W_COOL > 0) or (P_HP <= 0)) then
		return
	end
	local w = WEAPONS[CUR_WEAPON + 1]
	w:fire()
	if (NET_GAME and (W_COOL > 0)) then
		np_fire_send()
	end
end
function pickup_touch()
	local n = (#MO_X)
	for m = 0, (n - 1), 1 do
		do
			if (MO_STATE[m + 1] ~= 7) then
				goto _jd_continue_7
			end
			local dx = (MO_X[m + 1] - P_X)
			local dy = (MO_Y[m + 1] - P_Y)
			local rr = (P_RADIUS + MO_RAD[m + 1])
			if (((dx * dx) + (dy * dy)) >= (rr * rr)) then
				goto _jd_continue_7
			end
			if ((NET_GAME and NP_COOP) and (not NP_IS_HOST)) then
				np_take_request(m)
				goto _jd_continue_7
			end
			local applied = pickup_apply(m)
			if (((applied and NET_GAME) and NP_COOP) and NP_IS_HOST) then
				np_item_announce(m, true)
			end
		end
		::_jd_continue_7::
	end
end
function pickup_despawn(m)
	if (MO_STATE[m + 1] ~= 7) then
		return
	end
	MO_STATE[m + 1] = 0
	if (MO_CNT[m + 1] ~= 0) then
		ITEMS_TAKEN = (ITEMS_TAKEN + 1)
	end
end
function pickup_apply(m)
	if (MO_STATE[m + 1] ~= 7) then
		return false
	end
	local kind = MO_SPR[m + 1]
	local taken = true
	if (kind == "STIM") then
		if (P_HP >= 100) then
			taken = false
		else
			P_HP = imin100(_j_int((P_HP + 10)))
			set_msg("picked up a stimpack")
		end
	else
		if (kind == "MEDI") then
			if (P_HP >= 100) then
				taken = false
			else
				P_HP = imin100(_j_int((P_HP + 25)))
				set_msg("picked up a medikit")
			end
		else
			if (kind == "BON1") then
				if (P_HP < 200) then
					P_HP = (P_HP + 1)
				end
				set_msg("health bonus")
			else
				if (kind == "BON2") then
					if (P_ARMOR < 200) then
						P_ARMOR = (P_ARMOR + 1)
					end
					set_msg("armor bonus")
				else
					if (kind == "ARM1") then
						if (P_ARMOR >= 100) then
							taken = false
						else
							P_ARMOR = 100
							set_msg("picked up armor")
						end
					else
						if (kind == "ARM2") then
							if (P_ARMOR >= 200) then
								taken = false
							else
								P_ARMOR = 200
								set_msg("picked up megaarmor")
							end
						else
							if (kind == "CLIP") then
								AMMO_BULLETS = imin_ammo(_j_int((AMMO_BULLETS + 10)), _j_int(AMMO_BULLETS_MAX))
								set_msg("picked up a clip")
							else
								if (kind == "AMMO") then
									AMMO_BULLETS = imin_ammo(_j_int((AMMO_BULLETS + 50)), _j_int(AMMO_BULLETS_MAX))
									set_msg("picked up a box of bullets")
								else
									if (kind == "SHEL") then
										AMMO_SHELLS = imin_ammo(_j_int((AMMO_SHELLS + 4)), _j_int(AMMO_SHELLS_MAX))
										set_msg("picked up shells")
									else
										if (kind == "SBOX") then
											AMMO_SHELLS = imin_ammo(_j_int((AMMO_SHELLS + 20)), _j_int(AMMO_SHELLS_MAX))
											set_msg("picked up a box of shells")
										else
											if (kind == "ROCK") then
												AMMO_ROCKETS = imin_ammo(_j_int((AMMO_ROCKETS + 1)), _j_int(AMMO_ROCKETS_MAX))
												set_msg("picked up a rocket")
											else
												if (kind == "BROK") then
													AMMO_ROCKETS = imin_ammo(_j_int((AMMO_ROCKETS + 5)), _j_int(AMMO_ROCKETS_MAX))
													set_msg("picked up a box of rockets")
												else
													if (kind == "SHOT") then
														HAVE_SHOTGUN = true
														AMMO_SHELLS = imin_ammo(_j_int((AMMO_SHELLS + 8)), _j_int(AMMO_SHELLS_MAX))
														CUR_WEAPON = 3
														set_msg("you got the shotgun!")
													else
														if (kind == "MGUN") then
															HAVE_CHAINGUN = true
															AMMO_BULLETS = imin_ammo(_j_int((AMMO_BULLETS + 20)), _j_int(AMMO_BULLETS_MAX))
															CUR_WEAPON = 4
															set_msg("you got the chaingun!")
														else
															if (kind == "LAUN") then
																HAVE_LAUNCHER = true
																AMMO_ROCKETS = imin_ammo(_j_int((AMMO_ROCKETS + 2)), _j_int(AMMO_ROCKETS_MAX))
																CUR_WEAPON = 5
																set_msg("you got the rocket launcher!")
															else
																if (kind == "BKEY") then
																	P_KEYS = (P_KEYS | 1)
																	set_msg("picked up the blue keycard")
																else
																	if (kind == "RKEY") then
																		P_KEYS = (P_KEYS | 2)
																		set_msg("picked up the red keycard")
																	else
																		if (kind == "YKEY") then
																			P_KEYS = (P_KEYS | 4)
																			set_msg("picked up the yellow keycard")
																		else
																			if (kind == "SOUL") then
																				P_HP = imin_ammo(_j_int((P_HP + 100)), 200)
																				set_msg("supercharge!")
																			else
																				if (kind == "PSTR") then
																					P_BERSERK = true
																					BERSERK_TICS = 64
																					P_HP = imin100(_j_int((P_HP + 100)))
																					CUR_WEAPON = 1
																					set_msg("berserk!")
																				else
																					if (kind == "PINS") then
																						P_INVIS = 2100
																						set_msg("invisibility!")
																					else
																						if (kind == "SUIT") then
																							P_SUIT = 2100
																							set_msg("radiation shielding suit")
																						else
																							if (kind == "PMAP") then
																								P_ALLMAP = true
																								set_msg("computer area map")
																							else
																								if (kind == "PVIS") then
																									P_GOGGLES = 2100
																									set_msg("light amplification visor")
																								else
																									if (kind == "BPAK") then
																										if (not P_BACKPACK) then
																											P_BACKPACK = true
																											AMMO_BULLETS_MAX = (AMMO_BULLETS_MAX * 2)
																											AMMO_SHELLS_MAX = (AMMO_SHELLS_MAX * 2)
																											AMMO_ROCKETS_MAX = (AMMO_ROCKETS_MAX * 2)
																										end
																										AMMO_BULLETS = imin_ammo(_j_int((AMMO_BULLETS + 10)), _j_int(AMMO_BULLETS_MAX))
																										AMMO_SHELLS = imin_ammo(_j_int((AMMO_SHELLS + 4)), _j_int(AMMO_SHELLS_MAX))
																										AMMO_ROCKETS = imin_ammo(_j_int((AMMO_ROCKETS + 1)), _j_int(AMMO_ROCKETS_MAX))
																										set_msg("picked up a backpack full of ammo!")
																									else
																										taken = false
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
	if taken then
		MO_STATE[m + 1] = 0
		BONUS_TICS = 12
		if (MO_CNT[m + 1] ~= 0) then
			ITEMS_TAKEN = (ITEMS_TAKEN + 1)
		end
		local weapon = (((kind == "SHOT") or (kind == "MGUN")) or (kind == "LAUN"))
		local power = (((((kind == "PSTR") or (kind == "PINS")) or (kind == "SUIT")) or (kind == "PMAP")) or (kind == "PVIS"))
		if weapon then
			GRIN_TICS = 70
		end
		snd_play((weapon and "DSWPNUP" or (power and "DSGETPOW" or "DSITEMUP")))
	end
	return taken
end
function imin100(v)
	return ((v > 100) and 100 or v)
end
function imin_ammo(v, cap)
	return ((v > cap) and cap or v)
end
function line_blocks(ld, x, y)
	local v1 = LD_V1[ld + 1]
	local v2 = LD_V2[ld + 1]
	local x1 = (VX_X[v1 + 1] + 0.0)
	local y1 = (VX_Y[v1 + 1] + 0.0)
	local dx = (VX_X[v2 + 1] - x1)
	local dy = (VX_Y[v2 + 1] - y1)
	local len2 = ((dx * dx) + (dy * dy))
	if (len2 < 0.001) then
		return false
	end
	local t = ((((x - x1) * dx) + ((y - y1) * dy)) / len2)
	if (t < 0.0) then
		t = 0.0
	end
	if (t > 1.0) then
		t = 1.0
	end
	local cx = ((x1 + (dx * t)) - x)
	local cy = ((y1 + (dy * t)) - y)
	if (((cx * cx) + (cy * cy)) >= (P_RADIUS * P_RADIUS)) then
		return false
	end
	local s1 = LD_SIDE1[ld + 1]
	if (s1 < 0) then
		return true
	end
	if ((LD_FLAGS[ld + 1] & 1) ~= 0) then
		return true
	end
	local s0 = LD_SIDE0[ld + 1]
	local f0 = SE_FLOORH[SD_SEC[s0 + 1] + 1]
	local f1 = SE_FLOORH[SD_SEC[s1 + 1] + 1]
	local c0 = SE_CEILH[SD_SEC[s0 + 1] + 1]
	local c1 = SE_CEILH[SD_SEC[s1 + 1] + 1]
	local openbottom = (((f0 > f1) and f0 or f1) + 0.0)
	local opentop = (((c0 < c1) and c0 or c1) + 0.0)
	if ((opentop - openbottom) < P_HEIGHT) then
		return true
	end
	local feet = ((P_AIR and P_Z or P_FLOORZ) + 0.0)
	if ((openbottom - feet) > P_STEP) then
		return true
	end
	return false
end
function seg_dist2(ld, x, y)
	local x1 = (VX_X[LD_V1[ld + 1] + 1] + 0.0)
	local y1 = (VX_Y[LD_V1[ld + 1] + 1] + 0.0)
	local dx = (VX_X[LD_V2[ld + 1] + 1] - x1)
	local dy = (VX_Y[LD_V2[ld + 1] + 1] - y1)
	local len2 = ((dx * dx) + (dy * dy))
	if (len2 < 0.001) then
		return 1000000000.0
	end
	local t = ((((x - x1) * dx) + ((y - y1) * dy)) / len2)
	if (t < 0.0) then
		t = 0.0
	end
	if (t > 1.0) then
		t = 1.0
	end
	local cx = ((x1 + (dx * t)) - x)
	local cy = ((y1 + (dy * t)) - y)
	return ((cx * cx) + (cy * cy))
end
function line_blocks_from(ld, ox, oy, x, y)
	if (not line_blocks(ld, x, y)) then
		return false
	end
	if (not line_blocks(ld, ox, oy)) then
		return true
	end
	return (seg_dist2(ld, x, y) <= seg_dist2(ld, ox, oy))
end
BLOCK_LINE = -1
function pos_ok_scan(ox, oy, x, y, relative)
	BLOCK_LINE = -1
	local bx0 = _j_ifloor((((x - P_RADIUS) - BMAP_ORGX) / 128.0))
	local bx1 = _j_ifloor((((x + P_RADIUS) - BMAP_ORGX) / 128.0))
	local by0 = _j_ifloor((((y - P_RADIUS) - BMAP_ORGY) / 128.0))
	local by1 = _j_ifloor((((y + P_RADIUS) - BMAP_ORGY) / 128.0))
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
								if relative then
									if line_blocks_from(ld, ox, oy, x, y) then
										BLOCK_LINE = ld
										return false
									end
								else
									if line_blocks(ld, x, y) then
										BLOCK_LINE = ld
										return false
									end
								end
								i = (i + 1)
							end
							::_jd_continue_10::
						end
					end
				end
				::_jd_continue_9::
			end
		end
		::_jd_continue_8::
	end
	local nd = (#MO_X)
	for o = 0, (nd - 1), 1 do
		do
			if ((MO_STATE[o + 1] ~= 10) or (MO_RAD[o + 1] <= 0.0)) then
				goto _jd_continue_11
			end
			local dr = (MO_RAD[o + 1] + P_RADIUS)
			local ddx = (MO_X[o + 1] - x)
			local ddy = (MO_Y[o + 1] - y)
			if (((ddx * ddx) + (ddy * ddy)) >= (dr * dr)) then
				goto _jd_continue_11
			end
			if relative then
				local odx = (MO_X[o + 1] - ox)
				local ody = (MO_Y[o + 1] - oy)
				if (((odx * odx) + (ody * ody)) < (dr * dr)) then
					goto _jd_continue_11
				end
			end
			return false
		end
		::_jd_continue_11::
	end
	return true
end
function pos_ok(x, y)
	return pos_ok_scan(x, y, x, y, false)
end
function pos_ok_from(ox, oy, x, y)
	if P_NOCLIP then
		return true
	end
	return pos_ok_scan(ox, oy, x, y, true)
end
function p_move()
	local ox = (P_X + 0.0)
	local oy = (P_Y + 0.0)
	local nx = (P_X + P_MOMX)
	local ny = (P_Y + P_MOMY)
	if pos_ok_from(ox, oy, nx, ny) then
		P_X = nx
		P_Y = ny
	else
		local slid = false
		local bl = BLOCK_LINE
		if (bl >= 0) then
			local wdx = ((VX_X[LD_V2[bl + 1] + 1] - VX_X[LD_V1[bl + 1] + 1]) + 0.0)
			local wdy = ((VX_Y[LD_V2[bl + 1] + 1] - VX_Y[LD_V1[bl + 1] + 1]) + 0.0)
			local wl2 = ((wdx * wdx) + (wdy * wdy))
			if (wl2 > 0.001) then
				local dot = (((P_MOMX * wdx) + (P_MOMY * wdy)) / wl2)
				local sx2 = (wdx * dot)
				local sy2 = (wdy * dot)
				if ((((sx2 * sx2) + (sy2 * sy2)) > 0.01) and pos_ok_from(ox, oy, (P_X + sx2), (P_Y + sy2))) then
					P_X = (P_X + sx2)
					P_Y = (P_Y + sy2)
					P_MOMX = sx2
					P_MOMY = sy2
					slid = true
				end
			end
		end
		if (not slid) then
			if pos_ok_from(ox, oy, nx, (P_Y + 0.0)) then
				P_X = nx
				P_MOMY = 0.0
			else
				if pos_ok_from(ox, oy, (P_X + 0.0), ny) then
					P_Y = ny
					P_MOMX = 0.0
				else
					P_MOMX = 0.0
					P_MOMY = 0.0
				end
			end
		end
	end
	P_FLOORZ = SE_FLOORH[sector_at((P_X + 0.0), (P_Y + 0.0)) + 1]
	if ((ox ~= P_X) or (oy ~= P_Y)) then
		cross_check(ox, oy, (P_X + 0.0), (P_Y + 0.0))
	end
end
function lowest_neighbor_ceiling(sec)
	local best = SE_CEILH[sec + 1]
	local found = false
	local nlines = (#LD_V1)
	for i = 0, (nlines - 1), 1 do
		do
			local s0 = LD_SIDE0[i + 1]
			local s1 = LD_SIDE1[i + 1]
			if ((s0 < 0) or (s1 < 0)) then
				goto _jd_continue_12
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
			if (other < 0) then
				goto _jd_continue_12
			end
			if ((not found) or (SE_CEILH[other + 1] < best)) then
				best = SE_CEILH[other + 1]
				found = true
			end
		end
		::_jd_continue_12::
	end
	return best
end
function door_use(sec, stay)
	if mover_busy(sec) then
		return
	end
	local n = (#DOORS)
	for i = 0, (n - 1), 1 do
		do
			local d = DOORS[i + 1]
			if (d.sec == sec) then
				if (d.state == 2) then
					d.state = 0
					snd_sector(sec, "DSDOROPN")
				end
				return
			end
		end
		::_jd_continue_13::
	end
	table.insert(DOORS, Door.new(sec, 0, (stay and 1 or 0)))
	snd_sector(sec, "DSDOROPN")
end
function door_close(sec)
	if mover_busy(sec) then
		return
	end
	local n = (#DOORS)
	for i = 0, (n - 1), 1 do
		do
			local d = DOORS[i + 1]
			if (d.sec == sec) then
				d.state = 2
				return
			end
		end
		::_jd_continue_14::
	end
	table.insert(DOORS, Door.new(sec, 2, 0))
	snd_sector(sec, "DSDORCLS")
end
function door_busy(sec)
	local n = (#DOORS)
	for i = 0, (n - 1), 1 do
		do
			local d = DOORS[i + 1]
			if (d.sec == sec) then
				return true
			end
		end
		::_jd_continue_15::
	end
	return false
end
function doors_tic()
	local i = 0
	do
		while (i < (#DOORS)) do
			do
				local d = DOORS[i + 1]
				if d:tic() then
					table.remove(DOORS, i + 1)
				else
					i = (i + 1)
				end
			end
			::_jd_continue_16::
		end
	end
end
function p_use()
	if ((NET_GAME and NP_COOP) and (not NP_IS_HOST)) then
		np_use_send()
		return
	end
	local ux = math.cos(P_ANG)
	local uy = math.sin(P_ANG)
	local ex = (P_X + (ux * 64.0))
	local ey = (P_Y + (uy * 64.0))
	local nlines = (#LD_V1)
	local best_t = 2.0
	local best = -1
	for i = 0, (nlines - 1), 1 do
		do
			local x1 = (VX_X[LD_V1[i + 1] + 1] + 0.0)
			local y1 = (VX_Y[LD_V1[i + 1] + 1] + 0.0)
			local x2 = (VX_X[LD_V2[i + 1] + 1] + 0.0)
			local y2 = (VX_Y[LD_V2[i + 1] + 1] + 0.0)
			local d = (((x2 - x1) * (ey - P_Y)) - ((y2 - y1) * (ex - P_X)))
			if ((d > -0.0001) and (d < 0.0001)) then
				goto _jd_continue_17
			end
			local t = ((((P_X - x1) * (ey - P_Y)) - ((P_Y - y1) * (ex - P_X))) / d)
			local s = ((((P_X - x1) * (y2 - y1)) - ((P_Y - y1) * (x2 - x1))) / d)
			if ((((t < 0.0) or (t > 1.0)) or (s < 0.0)) or (s > 1.0)) then
				goto _jd_continue_17
			end
			if (s < best_t) then
				if ((LD_SPECIAL[i + 1] ~= 0) or (LD_SIDE1[i + 1] < 0)) then
					best_t = s
					best = i
				end
			end
		end
		::_jd_continue_17::
	end
	if (best < 0) then
		return
	end
	local special = LD_SPECIAL[best + 1]
	local s1 = LD_SIDE1[best + 1]
	local used = false
	if (special == 1) then
		if (s1 >= 0) then
			door_use(SD_SEC[s1 + 1], false)
			used = true
		end
	else
		if (((special == 26) or (special == 27)) or (special == 28)) then
			local need = ((special == 26) and 1 or ((special == 28) and 2 or 4))
			local keyname = ((special == 26) and "blue" or ((special == 28) and "red" or "yellow"))
			if ((P_KEYS & need) == 0) then
				set_msg((("you need a " .. keyname) .. " key"))
			else
				if (s1 >= 0) then
					door_use(SD_SEC[s1 + 1], false)
					used = true
				end
			end
		else
			if (special == 31) then
				if (s1 >= 0) then
					door_use(SD_SEC[s1 + 1], true)
					used = true
				end
			else
				if (((special == 32) or (special == 33)) or (special == 34)) then
					local need2 = ((special == 32) and 1 or ((special == 33) and 2 or 4))
					local keyname2 = ((special == 32) and "blue" or ((special == 33) and "red" or "yellow"))
					if ((P_KEYS & need2) == 0) then
						set_msg((("you need a " .. keyname2) .. " key"))
					else
						if (s1 >= 0) then
							door_use(SD_SEC[s1 + 1], true)
							used = true
						end
					end
				else
					if (special == 11) then
						snd_play("DSSWTCHN")
						LEVEL_DONE = true
						used = true
					else
						if (special == 51) then
							snd_play("DSSWTCHN")
							LEVEL_DONE = true
							SECRET_EXIT = true
							used = true
						else
							if (special ~= 0) then
								if spec_line(best, 0) then
									used = true
								else
									if (not spec_line_known(special)) then
										set_msg((("special " .. _j_str(special)) .. " not wired up yet"))
									end
								end
							end
						end
					end
				end
			end
		end
	end
	if used then
		switch_use(best)
		if ((NET_GAME and NP_COOP) and NP_IS_HOST) then
			np_switch_announce(best)
		end
	end
end
function mapxx(n)
	if (n < 10) then
		return ("MAP0" .. _j_str(n))
	end
	return ("MAP" .. _j_str(n))
end
function next_existing_map(after)
	local maps = map_list()
	do
		local _jd_range_18 = maps
		local _jd_index_18 = 1
		while _jd_index_18 <= #_jd_range_18 do
			do
				local m = _jd_range_18[_jd_index_18]
				if (m > after) then
					return m
				end
			end
			::_jd_continue_18::
			_jd_index_18 = _jd_index_18 + 1
		end
	end
	return ""
end
function next_map()
	local n = next_map_standard()
	if ((n ~= "") and (lump_num(n) < 0)) then
		n = next_existing_map(GAME_MAP)
	end
	return n
end
function next_map_standard()
	if _j_starts_with(GAME_MAP, "MAP") then
		local mn = _j_to_int(_j_substr(GAME_MAP, 3, 2), 1)
		if SECRET_EXIT then
			return ((mn == 31) and "MAP32" or "MAP31")
		end
		if (mn == 30) then
			return ""
		end
		if ((mn == 31) or (mn == 32)) then
			return "MAP16"
		end
		return mapxx((mn + 1))
	end
	if SECRET_EXIT then
		return (("E" .. _j_substr(GAME_MAP, 1, 1)) .. "M9")
	end
	local e = _j_to_int(_j_substr(GAME_MAP, 1, 1), 1)
	local m = _j_to_int(_j_substr(GAME_MAP, 3, 1), 1)
	if (m == 9) then
		return (("E" .. _j_str(e)) .. "M4")
	end
	if (m == 8) then
		return ""
	end
	return ((("E" .. _j_str(e)) .. "M") .. _j_str((m + 1)))
end
function game_tic()
	if DEMO_REC then
		demo_note()
	end
	GAME_TIC = (GAME_TIC + 1)
	P_ANG = (P_ANG + CMD_TURN)
	if (P_ANG > TAU) then
		P_ANG = (P_ANG - TAU)
	end
	if (P_ANG < 0.0) then
		P_ANG = (P_ANG + TAU)
	end
	if (not P_AIR) then
		local fc = math.cos(P_ANG)
		local fs = math.sin(P_ANG)
		P_MOMX = (P_MOMX + (((CMD_FWD * fc) + (CMD_SIDE * fs)) * 0.03125))
		P_MOMY = (P_MOMY + (((CMD_FWD * fs) - (CMD_SIDE * fc)) * 0.03125))
	end
	p_move()
	if (not P_AIR) then
		P_MOMX = (P_MOMX * 0.90625)
		P_MOMY = (P_MOMY * 0.90625)
	end
	if (P_Z < P_FLOORZ) then
		P_Z = (P_FLOORZ * 1.0)
		if (P_MOMZ < 0.0) then
			P_MOMZ = 0.0
		end
	else
		if (P_Z > P_FLOORZ) then
			if ((not P_AIR) and ((P_Z - P_FLOORZ) <= 4.5)) then
				P_Z = (P_FLOORZ * 1.0)
			else
				if (not P_AIR) then
					P_AIR = true
					P_AIRZ0 = P_Z
					P_MOMZ = 0.0
				end
				P_MOMZ = (P_MOMZ - 1.0)
				P_Z = (P_Z + P_MOMZ)
				if (P_Z <= P_FLOORZ) then
					P_Z = (P_FLOORZ * 1.0)
					P_AIR = false
					P_MOMZ = 0.0
					if ((P_AIRZ0 - P_Z) > 8.0) then
						P_VIEWZ = ((P_Z + 41.0) - 6.0)
						snd_play("DSOOF")
					end
				end
			end
		else
			P_AIR = false
			P_MOMZ = 0.0
		end
	end
	local vt = (P_Z + 41.0)
	if (P_VIEWZ < vt) then
		P_VIEWZ = (P_VIEWZ + 8.0)
		if (P_VIEWZ > vt) then
			P_VIEWZ = vt
		end
	else
		if (P_VIEWZ > vt) then
			P_VIEWZ = (P_VIEWZ - 8.0)
			if (P_VIEWZ < vt) then
				P_VIEWZ = vt
			end
		end
	end
	local bob = (((P_MOMX * P_MOMX) + (P_MOMY * P_MOMY)) * 0.25)
	if (bob > 16.0) then
		bob = 16.0
	end
	local ba = (GAME_TIC * 0.098)
	WBOB_X = ((bob * 0.5) * math.cos(ba))
	local bs = math.sin(ba)
	if (bs < 0.0) then
		bs = (-bs)
	end
	WBOB_Y = ((bob * 0.5) * bs)
	if CMD_USE then
		if (not USE_LATCH) then
			p_use()
			USE_LATCH = true
		end
	else
		USE_LATCH = false
	end
	if CMD_FIRE then
		player_fire()
		FIRE_HELD = (FIRE_HELD + 1)
	else
		FIRE_HELD = 0
	end
	if (W_COOL > 0) then
		W_COOL = (W_COOL - 1)
	end
	if (FLASH_TICS > 0) then
		FLASH_TICS = (FLASH_TICS - 1)
	end
	if (PAIN_TICS > 0) then
		PAIN_TICS = (PAIN_TICS - 1)
	end
	if (BONUS_TICS > 0) then
		BONUS_TICS = (BONUS_TICS - 1)
	end
	if (GRIN_TICS > 0) then
		GRIN_TICS = (GRIN_TICS - 1)
	end
	if (BERSERK_TICS > 0) then
		BERSERK_TICS = (BERSERK_TICS - 1)
	end
	if (P_INVIS > 0) then
		P_INVIS = (P_INVIS - 1)
		if (P_INVIS == 0) then
			set_msg("you feel visible again")
		else
			if ((P_INVIS < 140) and (math.fmod(P_INVIS, 35) == 0)) then
				set_msg("invisibility is wearing off")
			end
		end
	end
	if (P_SUIT > 0) then
		P_SUIT = (P_SUIT - 1)
		if (P_SUIT == 0) then
			set_msg("the suit dissolves")
		else
			if ((P_SUIT < 140) and (math.fmod(P_SUIT, 35) == 0)) then
				set_msg("the suit is wearing off")
			end
		end
	end
	if (P_GOGGLES > 0) then
		P_GOGGLES = (P_GOGGLES - 1)
	end
	pickup_touch()
	things_tic()
	if ((NET_GAME and NP_COOP) and (not NP_IS_HOST)) then
		spec_tic()
	else
		doors_tic()
		spec_tic()
	end
	anim_tic()
	if NET_GAME then
		np_tic()
	end
	if (MSG_TIMER > 0) then
		MSG_TIMER = (MSG_TIMER - 1)
	end
end
