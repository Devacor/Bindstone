-- Generated from p4_smoke.jai by tools/transpile.py; see internal/examples/jaidoom/ports/PORTING.md.
require("wad")
require("map")
require("vid")
require("render")
require("game")
function main()
	local path = wad_path("doom1")
	if (not wad_load(path)) then
		print("wad_load failed")
		return 1
	end
	vid_init(120, 56)
	if (not game_start_map("E1M1")) then
		print("no E1M1")
		return 1
	end
	render_view((P_X + 0.0), (P_Y + 0.0), P_ANG)
	local f1 = blit_half()
	print((((("E1M1 frame: " .. _j_str((#f1))) .. " bytes, palette ") .. _j_str((#PAL_FG))) .. " colors"))
	for i = 0, (35 - 1), 1 do
		do
			CMD_FWD = 25.0
			game_tic()
		end
		::_jd_continue_1::
	end
	render_view((P_X + 0.0), (P_Y + 0.0), P_ANG)
	blit_half()
	if (not game_start_map("E1M2")) then
		print("no E1M2")
		return 1
	end
	render_view((P_X + 0.0), (P_Y + 0.0), P_ANG)
	local f2 = blit_half()
	print((((("E1M2 frame: " .. _j_str((#f2))) .. " bytes, palette ") .. _j_str((#PAL_FG))) .. " colors"))
	print("P4 SMOKE: ALL GREEN")
	return 0
end
_jd_exit = main()
