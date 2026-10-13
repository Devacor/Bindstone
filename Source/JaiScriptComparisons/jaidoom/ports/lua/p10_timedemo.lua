-- Generated from p10_timedemo.jai by tools/transpile.py; see internal/examples/jaidoom/ports/PORTING.md.
require("wad")
require("map")
require("vid")
require("render")
require("things")
require("game")
require("hud")
require("save")
require("demo")
function td_player_reset()
	P_HP = 100
	P_ARMOR = 0
	P_KEYS = 0
	AMMO_BULLETS = 50
	AMMO_SHELLS = 0
	AMMO_ROCKETS = 0
	HAVE_SHOTGUN = false
	HAVE_CHAINGUN = false
	HAVE_LAUNCHER = false
	CUR_WEAPON = 2
	PAIN_TICS = 0
	BONUS_TICS = 0
	GRIN_TICS = 0
	FIRE_HELD = 0
end
function main()
	local wadpath = wad_path("doom1")
	local tape = ""
	local bw = 120
	local bh = 78
	local simonly = false
	do
		local a = 0
		while (a < (#ARGS)) do
			do
				local arg = ARGS[a + 1]
				if (arg == "simonly") then
					simonly = true
				else
					if _j_ends_with(arg, ".json") then
						tape = arg
					else
						local xat = _j_find(arg, "x")
						if (xat > 0) then
							local aw = _j_to_int(_j_substr(arg, 0, xat), 0)
							local ah = _j_to_int(_j_substr(arg, (xat + 1), (((#arg) - xat) - 1)), 0)
							if ((aw > 0) and (ah > 0)) then
								bw = aw
								bh = ah
							end
						end
					end
				end
			end
			::_jd_continue_1::
			a = a + 1
		end
	end
	if (tape == "") then
		tape = data_path("demo.json")
	end
	if (not wad_load(wadpath)) then
		print("wad_load failed")
		return 1
	end
	if (not demo_load(tape)) then
		print(("no demo at " .. tape))
		return 1
	end
	vid_init(bw, bh)
	hud_init()
	SKILL_BIT = DEMO_SKILL
	NIGHTMARE = DEMO_NM
	FAST_MONSTERS = DEMO_NM
	td_player_reset()
	GAME_TIC = 0
	_j_random_seed(DEMO_SEED)
	DEMO_POS = 0
	DEMO_PLAY = true
	if (not game_start_map(DEMO_MAP)) then
		print(("no map " .. DEMO_MAP))
		return 1
	end
	local ntics = _j_idiv((#DEMO_TICS), 6)
	print(((((((((((((((("timedemo: " .. tape) .. "  map ") .. DEMO_MAP) .. " skill ") .. _j_str(DEMO_SKILL)) .. "") .. (DEMO_NM and " NIGHTMARE" or "")) .. "  ") .. _j_str(ntics)) .. " tics  vid ") .. _j_str(bw)) .. "x") .. _j_str(bh)) .. "") .. (simonly and " SIMONLY" or "")))
	local sim_ms = 0
	local rend_ms = 0
	local blit_ms = 0
	local worst = 0
	local frame_bytes = 0
	local t_start = clock_ms()
	do
		while demo_feed() do
			do
				local t0 = clock_ms()
				game_tic()
				local t1 = clock_ms()
				local t2 = t1
				local t3 = t1
				if (not simonly) then
					EYE_Z = P_VIEWZ
					EXTRA_LIGHT = ((FLASH_TICS > 0) and 1 or 0)
					render_view((P_X + 0.0), (P_Y + 0.0), P_ANG)
					if (P_HP > 0) then
						local w = WEAPONS[CUR_WEAPON + 1]
						draw_weapon(w.spr, w:view_frame())
						if (FLASH_TICS > 0) then
							draw_weapon_flash(w.flash, "A")
						end
					end
					hud_draw()
					t2 = clock_ms()
					local frame = blit_half()
					frame_bytes = (#frame)
					t3 = clock_ms()
				end
				sim_ms = (sim_ms + (t1 - t0))
				rend_ms = (rend_ms + (t2 - t1))
				blit_ms = (blit_ms + (t3 - t2))
				if ((t3 - t0) > worst) then
					worst = (t3 - t0)
				end
				if LEVEL_DONE then
					break
				end
			end
			::_jd_continue_2::
		end
	end
	local wall = (clock_ms() - t_start)
	if (wall < 1) then
		wall = 1
	end
	local played = _j_int(GAME_TIC)
	local mspt = ((1.0 * wall) / imax(1, played))
	local realtime = (played / 35.0)
	local speedup = ((realtime * 1000.0) / wall)
	print((((((((((("played " .. _j_str(played)) .. " tics in ") .. _j_str(wall)) .. " ms  =  ") .. format_value(mspt, ".2")) .. " ms/tic  (") .. format_value(speedup, ".1")) .. "x realtime, ") .. _j_str(_j_int(((played * 1000.0) / wall)))) .. " tics/sec)"))
	print((((((((((("split: sim " .. format_value(((1.0 * sim_ms) / imax(1, played)), ".2")) .. "  render ") .. format_value(((1.0 * rend_ms) / imax(1, played)), ".2")) .. "  blit ") .. format_value(((1.0 * blit_ms) / imax(1, played)), ".2")) .. "  (ms/tic, aggregate)  worst tic ") .. _j_str(worst)) .. " ms  frame ") .. _j_str(frame_bytes)) .. " bytes"))
	print((("STATE_HASH: " .. _j_str(demo_state_hash())) .. "  (run twice: identical hash = uncapped replay is deterministic)"))
	return 0
end
function imax(a, b)
	return ((a > b) and a or b)
end
_jd_exit = main()
