-- Generated from p7_bench.jai by tools/transpile.py; see PORTING.md.
require("wad")
require("map")
require("vid")
require("render")
require("things")
require("game")
require("hud")
FRAMES = 300
FAILS = 0
function check(ok, what)
	if ok then
		print(("  ok    " .. what))
	else
		FAILS = (FAILS + 1)
		print(("  FAIL  " .. what))
	end
end
function wake_nearest(count)
	local woken = 0
	do
		while (woken < count) do
			do
				local best = -1
				local bestd = 1000000000.0
				do
					local m = 0
					while (m < (#MO_X)) do
						do
							if (MO_STATE[m + 1] ~= 1) then
								goto _jd_continue_2
							end
							local dx = (MO_X[m + 1] - P_X)
							local dy = (MO_Y[m + 1] - P_Y)
							local d2 = ((dx * dx) + (dy * dy))
							if (d2 < bestd) then
								bestd = d2
								best = m
							end
						end
						::_jd_continue_2::
						m = m + 1
					end
				end
				if (best < 0) then
					return
				end
				MO_STATE[best + 1] = 2
				woken = (woken + 1)
			end
			::_jd_continue_1::
		end
	end
end
function first_px()
	local n = (VW * VH)
	for i = 0, (n - 1), 1 do
		do
			if (PIX[i + 1] ~= 0) then
				return i
			end
		end
		::_jd_continue_3::
	end
	return -1
end
function main()
	local path = wad_path("doom1")
	if (not wad_load(path)) then
		print("wad_load failed")
		return 1
	end
	local bw = 120
	local bh = 78
	do
		local a = 0
		while (a < (#ARGS)) do
			do
				local arg = ARGS[a + 1]
				if (arg == "serial") then
					BLIT_WORKERS = 0
				else
					if (arg == "parallel") then
						BLIT_WORKERS = 1
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
			::_jd_continue_4::
			a = a + 1
		end
	end
	vid_init(bw, bh)
	hud_init()
	if (not game_start_map("E1M1")) then
		print("no E1M1")
		return 1
	end
	wake_nearest(6)
	local simms = 0
	local renderms = 0
	local blitms = 0
	local worst = 0
	do
		local f = 0
		while (f < FRAMES) do
			do
				CMD_FWD = 50.0
				CMD_SIDE = 0.0
				CMD_TURN = ((f < 100) and 0.0 or (((_j_idiv(f, 50) & 1) == 0) and 0.04 or -0.04))
				CMD_FIRE = (math.fmod(f, 40) < 6)
				CMD_USE = false
				local t0 = clock_ms()
				game_tic()
				local t1 = clock_ms()
				EYE_Z = P_VIEWZ
				EXTRA_LIGHT = ((FLASH_TICS > 0) and 1 or 0)
				render_view((P_X + 0.0), (P_Y + 0.0), P_ANG)
				draw_weapon("PISG", "A")
				if (FLASH_TICS > 0) then
					draw_weapon("PISF", "A")
				end
				hud_draw()
				msg_draw()
				local t2 = clock_ms()
				blit_half()
				local t3 = clock_ms()
				simms = (simms + (t1 - t0))
				renderms = (renderms + (t2 - t1))
				blitms = (blitms + (t3 - t2))
				if ((t3 - t0) > worst) then
					worst = (t3 - t0)
				end
			end
			::_jd_continue_5::
			f = f + 1
		end
	end
	local total = ((simms + renderms) + blitms)
	fps_note(_j_idiv((total + _j_idiv(FRAMES, 2)), FRAMES))
	fps_draw()
	bmp_write("p7_view.bmp")
	local perframe = ((total * 1.0) / FRAMES)
	print((((("P7 BENCH: " .. format_value(perframe, ".2")) .. " ms/frame over ") .. _j_str(FRAMES)) .. " frames (budget 28.5)"))
	print(((((((("  sim " .. format_value(((simms * 1.0) / FRAMES), ".2")) .. "  render ") .. format_value(((renderms * 1.0) / FRAMES), ".2")) .. "  blit ") .. format_value(((blitms * 1.0) / FRAMES), ".2")) .. "  worst ") .. _j_str(worst)))
	print((((((((((((("  view " .. _j_str(VW)) .. "x") .. _j_str(VIEWH)) .. "+") .. _j_str(BAR_H)) .. " bar, sprites last frame ") .. _j_str(SPRITES_DRAWN)) .. ", pos (") .. format_value(P_X, ".0")) .. ", ") .. format_value(P_Y, ".0")) .. ")"))
	check((perframe <= 28.5), "under the 35fps budget")
	P_X = 1056.0
	P_Y = -3550.0
	P_ANG = (PI / 2.0)
	P_MOMX = 0.0
	P_MOMY = 0.0
	P_FLOORZ = SE_FLOORH[sector_at((P_X + 0.0), (P_Y + 0.0)) + 1]
	CMD_FIRE = false
	CMD_TURN = 0.0
	CMD_FWD = 50.0
	local bobpeak = 0.0
	for b = 0, (20 - 1), 1 do
		do
			game_tic()
			local mag = ((WBOB_X * WBOB_X) + (WBOB_Y * WBOB_Y))
			if (mag > bobpeak) then
				bobpeak = mag
			end
		end
		::_jd_continue_6::
	end
	check((bobpeak > 4.0), (("bob swings while running (peak mag2 " .. format_value(bobpeak, ".1")) .. ")"))
	CMD_FWD = 0.0
	for b2 = 0, (80 - 1), 1 do
		do
			game_tic()
		end
		::_jd_continue_7::
	end
	check((((WBOB_X * WBOB_X) + (WBOB_Y * WBOB_Y)) < 0.1), "bob settles at rest")
	WBOB_X = 0.0
	WBOB_Y = 0.0
	pix_clear(0)
	draw_weapon("PISG", "A")
	local p0 = first_px()
	WBOB_X = 20.0
	pix_clear(0)
	draw_weapon("PISG", "A")
	local p1 = first_px()
	check((((p0 >= 0) and (p1 >= 0)) and (p0 ~= p1)), (((("bob shifts the weapon overlay (" .. _j_str(p0)) .. " -> ") .. _j_str(p1)) .. ")"))
	WBOB_X = 0.0
	P_MOMX = 0.0
	P_MOMY = 0.0
	game_tic()
	P_VIEWZ = ((P_FLOORZ + 41.0) - 24.0)
	game_tic()
	check((P_VIEWZ == ((P_FLOORZ + 41.0) - 16.0)), (("viewz eased up 8 (" .. format_value(((P_VIEWZ - P_FLOORZ) - 41.0), ".0")) .. " to go)"))
	game_tic()
	game_tic()
	check((P_VIEWZ == (P_FLOORZ + 41.0)), "viewz settled at floor + 41")
	P_VIEWZ = ((P_FLOORZ + 41.0) + 20.0)
	game_tic()
	check((P_VIEWZ == ((P_FLOORZ + 41.0) + 12.0)), "viewz eases down too")
	for v = 0, (5 - 1), 1 do
		do
			game_tic()
		end
		::_jd_continue_8::
	end
	EXTRA_LIGHT = 0
	local b0 = light_band(160, 0.0)
	EXTRA_LIGHT = 1
	check((light_band(160, 0.0) == (b0 - 1)), (((("flash brightens one band (" .. _j_str(b0)) .. " -> ") .. _j_str((b0 - 1))) .. ")"))
	check((light_band(255, 0.0) == 0), "full bright stays clamped")
	EXTRA_LIGHT = 0
	EYE_Z = (P_FLOORZ + 10.0)
	render_view((P_X + 0.0), (P_Y + 0.0), P_ANG)
	check((VIEW_Z == (P_FLOORZ + 10.0)), "render_view takes the explicit eye height")
	EYE_Z = -100000.0
	render_view((P_X + 0.0), (P_Y + 0.0), P_ANG)
	check((VIEW_Z == (SE_FLOORH[sector_at((P_X + 0.0), (P_Y + 0.0)) + 1] + 41.0)), "sentinel derives eye height from the sector")
	FPS_EMA = 0.0
	fps_note(20)
	check((FPS_EMA == 20.0), "first frame seeds the EMA")
	fps_note(30)
	check((FPS_EMA == 21.0), (("EMA blends (" .. format_value(FPS_EMA, ".1")) .. ")"))
	pix_clear(0)
	fps_draw()
	check((first_px() >= 0), "fps counter draws on the hud")
	print("")
	print(((FAILS == 0) and "P7 GATE: ALL GREEN" or (("P7 GATE: " .. _j_str(FAILS)) .. " FAILURES")))
	return FAILS
end
_jd_exit = main()
