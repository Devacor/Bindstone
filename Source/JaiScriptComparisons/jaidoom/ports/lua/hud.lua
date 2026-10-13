-- Generated from hud.jai by tools/transpile.py; see internal/examples/jaidoom/ports/PORTING.md.
require("wad")
require("map")
require("vid")
require("tex")
require("things")
BAR_H = 0
BSX = 1.0
BSY = 1.0
HUD_AMMO_DRAWN = -1
HUD_HEALTH_DRAWN = -1
HUD_ARMOR_DRAWN = -1
HUD_FACE = ""
AM_ON = false
AM_ZOOM = 1.0
function hud_init()
	gfx_init()
	BAR_H = _j_idiv((32 * VW), 320)
	if (BAR_H < 4) then
		BAR_H = 4
	end
	if (BAR_H > _j_idiv(VH, 3)) then
		BAR_H = _j_idiv(VH, 3)
	end
	VIEWH = (VH - BAR_H)
	BSX = (VW / 320.0)
	BSY = (BAR_H / 32.0)
end
function draw_patch(lump, x0, y0, w, h)
	sprite_prepare(lump)
	local pw = SPR_W[lump]
	local ph = SPR_H[lump]
	if ((((w < 1) or (h < 1)) or (pw < 1)) or (ph < 1)) then
		return
	end
	local tp = SPR_PIX[lump]
	local pix = PIX
	local palcm = PALCM
	for sx = 0, (w - 1), 1 do
		do
			local px = (x0 + sx)
			if ((px < 0) or (px >= VW)) then
				goto _jd_continue_1
			end
			local base = (_j_idiv((sx * pw), w) * ph)
			for sy = 0, (h - 1), 1 do
				do
					local py = (y0 + sy)
					if ((py < 0) or (py >= VH)) then
						goto _jd_continue_2
					end
					local texel = tp[(base + _j_idiv((sy * ph), h)) + 1]
					if (texel >= 0) then
						pix[((py * VW) + px) + 1] = palcm[texel + 1]
					end
				end
				::_jd_continue_2::
			end
		end
		::_jd_continue_1::
	end
end
function draw_patch_name(name, x, y, w, h)
	local lu = lump_num(name)
	if (lu < 0) then
		return false
	end
	draw_patch(lu, x, y, w, h)
	return true
end
function bar_patch(name, bx, by)
	local lu = lump_num(name)
	if (lu < 0) then
		return
	end
	sprite_prepare(lu)
	local w = _j_int((SPR_W[lu] * BSX))
	local h = _j_int((SPR_H[lu] * BSY))
	if (w < 1) then
		w = 1
	end
	if (h < 1) then
		h = 1
	end
	draw_patch(lu, _j_int(((bx - SPR_LO[lu]) * BSX)), (VIEWH + _j_int(((by - SPR_TO[lu]) * BSY))), w, h)
end
function bar_num(pfx, val, rx, by)
	if (val < 0) then
		val = 0
	end
	do
		while true do
			do
				local nm = ((("" .. pfx) .. "") .. _j_str(math.fmod(val, 10)))
				local lu = lump_num(nm)
				if (lu < 0) then
					return
				end
				sprite_prepare(lu)
				local dw = SPR_W[lu]
				rx = (rx - dw)
				bar_patch(nm, rx, by)
				val = _j_idiv(val, 10)
				if (val == 0) then
					break
				end
			end
			::_jd_continue_3::
		end
	end
end
FACE_LOOK = 0
FACE_NEXT = -1
FACE_SEED = 4321
function face_rnd(n)
	FACE_SEED = (((FACE_SEED * 1103515245) + 12345) & 2147483647)
	return math.fmod((FACE_SEED // 65536), n)
end
function face_name()
	if (P_HP <= 0) then
		return "STFDEAD0"
	end
	if P_GOD then
		return "STFGOD0"
	end
	local idx = 4
	if (P_HP >= 80) then
		idx = 0
	else
		if (P_HP >= 60) then
			idx = 1
		else
			if (P_HP >= 40) then
				idx = 2
			else
				if (P_HP >= 20) then
					idx = 3
				end
			end
		end
	end
	if (GRIN_TICS > 0) then
		return ("STFEVL" .. _j_str(idx))
	end
	if (PAIN_TICS > 0) then
		if FACE_PAIN_BIG then
			return ("STFOUCH" .. _j_str(idx))
		end
		if (FACE_SIDE < 0) then
			return (("STFTL" .. _j_str(idx)) .. "0")
		end
		if (FACE_SIDE > 0) then
			return (("STFTR" .. _j_str(idx)) .. "0")
		end
		return ("STFKILL" .. _j_str(idx))
	end
	if (FIRE_HELD >= 70) then
		return ("STFKILL" .. _j_str(idx))
	end
	if (GAME_TIC >= FACE_NEXT) then
		FACE_LOOK = face_rnd(3)
		FACE_NEXT = ((GAME_TIC + 25) + face_rnd(46))
	end
	return ((("STFST" .. _j_str(idx)) .. "") .. _j_str(FACE_LOOK))
end
function face_draw(name)
	local lu = lump_num(name)
	if (lu < 0) then
		return
	end
	sprite_prepare(lu)
	local fill = (30.0 / SPR_H[lu])
	local w = _j_int((((SPR_W[lu] * fill) * BSX) + 0.5))
	local h = _j_int(((30.0 * BSY) + 0.5))
	if (w < 1) then
		w = 1
	end
	if (h < 1) then
		h = 1
	end
	local x = _j_int((((159.5 * BSX) - (w / 2.0)) + 0.5))
	local y = ((VIEWH + _j_int(((31.0 * BSY) + 0.5))) - h)
	draw_patch(lu, x, y, w, h)
end
HUD_ARMS_DRAWN = 0
HUD_FRAGS_DRAWN = -1
function arms_draw()
	if (NET_GAME and (not NP_COOP)) then
		bar_num("STTNUM", _j_int(NET_FRAGS_MINE), 138, 3)
		HUD_FRAGS_DRAWN = NET_FRAGS_MINE
		HUD_ARMS_DRAWN = -1
		return
	end
	HUD_FRAGS_DRAWN = -1
	bar_patch("STARMS", 104, 0)
	local arms = 0
	for i = 0, (6 - 1), 1 do
		do
			local slot = (i + 2)
			local owned = ((((slot == 2) or ((slot == 3) and HAVE_SHOTGUN)) or ((slot == 4) and HAVE_CHAINGUN)) or ((slot == 5) and HAVE_LAUNCHER))
			bar_patch((owned and ("STYSNUM" .. _j_str(slot)) or ("STGNUM" .. _j_str(slot))), (111 + (math.fmod(i, 3) * 12)), (4 + (_j_idiv(i, 3) * 10)))
			if owned then
				arms = (arms | (1 << (slot & 63)))
			end
		end
		::_jd_continue_4::
	end
	HUD_ARMS_DRAWN = arms
end
function hud_draw()
	if (BAR_H <= 0) then
		return
	end
	bar_patch("STBAR", 0, 0)
	arms_draw()
	if (CUR_WEAPON ~= 1) then
		local ammo = _j_int(AMMO_BULLETS)
		if (CUR_WEAPON == 3) then
			ammo = _j_int(AMMO_SHELLS)
		else
			if (CUR_WEAPON == 5) then
				ammo = _j_int(AMMO_ROCKETS)
			end
		end
		bar_num("STTNUM", ammo, 44, 3)
		HUD_AMMO_DRAWN = ammo
	else
		HUD_AMMO_DRAWN = -1
	end
	bar_patch("STTPRCNT", 90, 3)
	bar_num("STTNUM", _j_int(P_HP), 90, 3)
	HUD_HEALTH_DRAWN = P_HP
	bar_patch("STTPRCNT", 221, 3)
	bar_num("STTNUM", _j_int(P_ARMOR), 221, 3)
	HUD_ARMOR_DRAWN = P_ARMOR
	HUD_FACE = face_name()
	face_draw(HUD_FACE)
	if ((P_KEYS & 1) ~= 0) then
		bar_patch("STKEYS0", 239, 3)
	end
	if ((P_KEYS & 4) ~= 0) then
		bar_patch("STKEYS1", 239, 13)
	end
	if ((P_KEYS & 2) ~= 0) then
		bar_patch("STKEYS2", 239, 23)
	end
	bar_num("STYSNUM", _j_int(AMMO_BULLETS), 288, 5)
	bar_num("STYSNUM", _j_int(AMMO_BULLETS_MAX), 314, 5)
	bar_num("STYSNUM", _j_int(AMMO_SHELLS), 288, 11)
	bar_num("STYSNUM", _j_int(AMMO_SHELLS_MAX), 314, 11)
	bar_num("STYSNUM", _j_int(AMMO_ROCKETS), 288, 17)
	bar_num("STYSNUM", _j_int(AMMO_ROCKETS_MAX), 314, 17)
end
function stcfn_lump(code)
	if ((code >= 97) and (code <= 122)) then
		code = (code - 32)
	end
	if ((code < 33) or (code > 95)) then
		return -1
	end
	return lump_num(("STCFN0" .. _j_str(code)))
end
function text_scale()
	local sc = _j_idiv(VW, 320)
	if (sc < 1) then
		sc = 1
	end
	return sc
end
function text_width(s, fs)
	local w = 0.0
	do
		local i = 0
		while (i < (#s)) do
			do
				local lu = stcfn_lump((string.byte(s, i + 1) + 0))
				if (lu < 0) then
					w = (w + (4.0 * fs))
					goto _jd_continue_5
				end
				sprite_prepare(lu)
				local cw = SPR_W[lu]
				w = (w + ((cw + 1) * fs))
			end
			::_jd_continue_5::
			i = i + 1
		end
	end
	return _j_int(w)
end
function draw_text(s, x, y, fs)
	local fx = (x + 0.0)
	do
		local i = 0
		while (i < (#s)) do
			do
				local lu = stcfn_lump((string.byte(s, i + 1) + 0))
				if (lu < 0) then
					fx = (fx + (4.0 * fs))
					goto _jd_continue_6
				end
				sprite_prepare(lu)
				local cw = SPR_W[lu]
				local chh = SPR_H[lu]
				local pw = _j_int((cw * fs))
				local ph = _j_int((chh * fs))
				if (pw < 1) then
					pw = 1
				end
				if (ph < 1) then
					ph = 1
				end
				draw_patch(lu, _j_int(fx), y, pw, ph)
				fx = (fx + ((cw + 1) * fs))
			end
			::_jd_continue_6::
			i = i + 1
		end
	end
end
function draw_text_center(s, y, fs)
	draw_text(s, _j_idiv((VW - text_width(s, fs)), 2), y, fs)
end
function menu_scale()
	local ms = (VW / 320.0)
	if (ms < 0.5) then
		ms = 0.5
	end
	return ms
end
function msg_draw()
	if (MSG_TIMER > 0) then
		draw_text(GAME_MSG, 2, 2, (text_scale() + 0.0))
	end
end
FPS_EMA = 0.0
function fps_note(dtms)
	if (dtms < 1) then
		dtms = 1
	end
	if (FPS_EMA <= 0.0) then
		FPS_EMA = (dtms * 1.0)
	else
		FPS_EMA = ((FPS_EMA * 0.9) + (dtms * 0.1))
	end
end
function fps_draw()
	if (FPS_EMA <= 0.0) then
		return
	end
	local fps = _j_int(((1000.0 / FPS_EMA) + 0.5))
	local sc = text_scale()
	local s = (("" .. _j_str(fps)) .. " FPS")
	draw_text(s, ((VW - text_width(s, (sc + 0.0))) - 2), (VIEWH - (9 * sc)), (sc + 0.0))
end
function big_num(val, rx, y, fs)
	if (val < 0) then
		val = 0
	end
	do
		while true do
			do
				local lu = lump_num(("STTNUM" .. _j_str(math.fmod(val, 10))))
				if (lu < 0) then
					return
				end
				sprite_prepare(lu)
				local w = _j_int((SPR_W[lu] * fs))
				local h = _j_int((SPR_H[lu] * fs))
				if (w < 1) then
					w = 1
				end
				if (h < 1) then
					h = 1
				end
				rx = (rx - w)
				draw_patch(lu, rx, y, w, h)
				val = _j_idiv(val, 10)
				if (val == 0) then
					break
				end
			end
			::_jd_continue_7::
		end
	end
end
function big_pct(val, rx, y, fs)
	local lu = lump_num("STTPRCNT")
	if (lu >= 0) then
		sprite_prepare(lu)
		local w = _j_int((SPR_W[lu] * fs))
		local h = _j_int((SPR_H[lu] * fs))
		if (w < 1) then
			w = 1
		end
		if (h < 1) then
			h = 1
		end
		draw_patch(lu, rx, y, w, h)
	end
	big_num(val, rx, y, fs)
end
function title_draw()
	if (not draw_patch_name("TITLEPIC", 0, 0, VW, VH)) then
		pix_clear(0)
	end
	draw_text_center("PRESS ANY KEY", (VH - (12 * text_scale())), (text_scale() + 0.0))
end
function menu_lump(label)
	if (label == "NEW GAME") then
		return "M_NGAME"
	end
	if (label == "SAVE GAME") then
		return "M_SAVEG"
	end
	if (label == "LOAD GAME") then
		return "M_LOADG"
	end
	if (label == "QUIT") then
		return "M_QUITG"
	end
	if (label == "I'M TOO YOUNG TO DIE") then
		return "M_JKILL"
	end
	if (label == "HEY, NOT TOO ROUGH") then
		return "M_ROUGH"
	end
	if (label == "HURT ME PLENTY") then
		return "M_HURT"
	end
	if (label == "ULTRA-VIOLENCE") then
		return "M_ULTRA"
	end
	if (label == "NIGHTMARE!") then
		return "M_NMARE"
	end
	if (label == "CHOOSE A SKILL") then
		return "M_SKILL"
	end
	return ""
end
function fit_scale(s, want)
	local ms = want
	do
		while ((ms > 0.45) and (text_width(s, ms) > (VW - 8))) do
			do
				ms = (ms * 0.85)
			end
			::_jd_continue_8::
		end
	end
	return ms
end
function screen_row(s, y, want)
	if (s == "") then
		return y
	end
	local ms = fit_scale(s, want)
	draw_text_center(s, y, ms)
	return ((y + _j_int((10.0 * ms))) + 4)
end
function lobby_draw(title, line1, line2)
	pix_clear(0)
	local ms = menu_scale()
	local y = _j_idiv(VH, 5)
	y = (screen_row(title, y, (ms * 2.0)) + _j_int((6.0 * ms)))
	y = (screen_row(line1, y, (ms * 1.25)) + _j_int((6.0 * ms)))
	screen_row(line2, y, ms)
end
function ip_draw(addr)
	pix_clear(0)
	local ms = menu_scale()
	local y = _j_idiv(VH, 6)
	y = (screen_row("ENTER HOST ADDRESS", y, (ms * 2.0)) + _j_int((8.0 * ms)))
	y = (screen_row((addr .. "_"), y, (ms * 2.0)) + _j_int((10.0 * ms)))
	y = screen_row("LETTERS DIGITS . : -", y, ms)
	y = screen_row("ENTER JOINS", y, ms)
	screen_row("ESC BACKS OUT", y, ms)
end
function demo_name_draw(name)
	pix_clear(0)
	local ms = menu_scale()
	local y = _j_idiv(VH, 6)
	y = (screen_row("PLAY WHICH DEMO", y, (ms * 2.0)) + _j_int((8.0 * ms)))
	y = (screen_row((name .. "_"), y, (ms * 2.0)) + _j_int((10.0 * ms)))
	y = screen_row("LETTERS DIGITS . - _", y, ms)
	y = screen_row("ENTER PLAYS", y, ms)
	y = screen_row("ESC BACKS OUT", y, ms)
	if (MSG_TIMER > 0) then
		screen_row(GAME_MSG, (y + _j_int((6.0 * ms))), ms)
	end
end
function tape_draw(tag)
	local sc = text_scale()
	draw_text(tag, ((VW - text_width(tag, (sc + 0.0))) - 2), 2, (sc + 0.0))
end
function scoreboard_draw(lines)
	local ms = menu_scale()
	local y = 6
	y = (screen_row("FRAGS", y, (ms * 1.5)) + 2)
	do
		local i = 0
		while (i < (#lines)) do
			do
				local line = _j_at(lines, i)
				y = screen_row(line, y, ms)
				if (y > (VIEWH - 12)) then
					break
				end
			end
			::_jd_continue_9::
			i = i + 1
		end
	end
end
function menu_entry_draw(label, x, y, ms)
	local lname = menu_lump(label)
	local lu = ((lname == "") and -1 or lump_num(lname))
	if (lu >= 0) then
		sprite_prepare(lu)
		local w = _j_int((SPR_W[lu] * ms))
		local h = _j_int((SPR_H[lu] * ms))
		if (w < 1) then
			w = 1
		end
		if (h < 1) then
			h = 1
		end
		if (x < 0) then
			x = _j_idiv((VW - w), 2)
		end
		draw_patch(lu, x, y, w, h)
		return x
	end
	if (x < 0) then
		x = _j_idiv((VW - text_width(label, (ms * 2.0))), 2)
	end
	draw_text(label, x, y, (ms * 2.0))
	return x
end
function menu_draw(entries, sel, title)
	pix_clear(0)
	local ms = menu_scale()
	local y = _j_idiv(VH, 10)
	if (title == "") then
		local mlu = lump_num("M_DOOM")
		if (mlu >= 0) then
			sprite_prepare(mlu)
			local w = _j_idiv((SPR_W[mlu] * VW), 320)
			local h = _j_idiv((SPR_H[mlu] * VH), 200)
			if ((w > 0) and (h > 0)) then
				draw_patch(mlu, _j_idiv((VW - w), 2), y, w, h)
				y = ((y + h) + _j_int((10.0 * ms)))
			end
		else
			draw_text_center("JAIDOOM", y, (ms * 2.0))
			y = (y + _j_int((20.0 * ms)))
		end
	else
		menu_entry_draw(title, -1, y, ms)
		y = (y + _j_int((20.0 * ms)))
	end
	local step = _j_int((19.0 * ms))
	if (step < 10) then
		step = 10
	end
	do
		local i = 0
		while (i < (#entries)) do
			do
				local line = _j_at(entries, i)
				local x = menu_entry_draw(line, ((i == 0) and -1 or MENU_X), y, ms)
				if (i == 0) then
					MENU_X = x
				end
				if (i == sel) then
					local sk = lump_num("M_SKULL1")
					if (sk >= 0) then
						sprite_prepare(sk)
						local sw = _j_int((SPR_W[sk] * ms))
						local sh = _j_int((SPR_H[sk] * ms))
						if (sw < 1) then
							sw = 1
						end
						if (sh < 1) then
							sh = 1
						end
						draw_patch(sk, ((MENU_X - sw) - _j_int((8.0 * ms))), (y - _j_int((2.0 * ms))), sw, sh)
					else
						draw_text("*", (MENU_X - _j_int((12.0 * ms))), y, (ms * 2.0))
					end
				end
				y = (y + step)
			end
			::_jd_continue_10::
			i = i + 1
		end
	end
end
MENU_X = 0
PARS = {["E1M1"]=30, ["E1M2"]=75, ["E1M3"]=120, ["E1M4"]=90, ["E1M5"]=165, ["E1M6"]=180, ["E1M7"]=180, ["E1M8"]=30, ["E1M9"]=165}
function par_secs(mapname)
	if (PARS[mapname] ~= nil) then
		return PARS[mapname]
	end
	return 0
end
function mmss(label, secs)
	local mm = _j_idiv(secs, 60)
	local ss = math.fmod(secs, 60)
	if (ss < 10) then
		return ((((("" .. label) .. " ") .. _j_str(mm)) .. ":0") .. _j_str(ss))
	end
	return ((((("" .. label) .. " ") .. _j_str(mm)) .. ":") .. _j_str(ss))
end
function wi_draw(mapname, nextmap, kp, ip, sp, tsecs)
	if ((not draw_patch_name("WIMAP0", 0, 0, VW, VH)) and (not draw_patch_name("INTERPIC", 0, 0, VW, VH))) then
		pix_clear(0)
	end
	local sc = text_scale()
	local fs = (VW / 320.0)
	draw_text_center((("" .. mapname) .. " FINISHED"), _j_idiv(VH, 16), ((sc * 2) + 0.0))
	local lx = _j_idiv(VW, 5)
	local rx = _j_idiv((VW * 72), 100)
	local y1 = _j_idiv((VH * 30), 100)
	local dy = _j_idiv((VH * 13), 100)
	draw_text("KILLS", lx, y1, (sc + 0.0))
	big_pct(kp, rx, y1, fs)
	draw_text("ITEMS", lx, (y1 + dy), (sc + 0.0))
	big_pct(ip, rx, (y1 + dy), fs)
	draw_text("SECRET", lx, (y1 + (dy * 2)), (sc + 0.0))
	big_pct(sp, rx, (y1 + (dy * 2)), fs)
	draw_text(mmss("TIME", tsecs), lx, (y1 + (dy * 3)), (sc + 0.0))
	local par = par_secs(mapname)
	if (par > 0) then
		draw_text(mmss("PAR", par), _j_idiv((VW * 55), 100), (y1 + (dy * 3)), (sc + 0.0))
	end
	if (nextmap ~= "") then
		draw_text_center(("ENTERING " .. nextmap), (y1 + (dy * 4)), (sc + 0.0))
	end
	draw_text_center(((WI_FOOT ~= "") and WI_FOOT or "PRESS ANY KEY"), (VH - (10 * sc)), (sc + 0.0))
end
WI_FOOT = ""
function end_draw()
	pix_clear(0)
	local sc = text_scale()
	local y = _j_idiv(VH, 6)
	local title = "EPISODE ONE COMPLETE"
	local lines = _j_array({"THE ANOMALY IS SILENT.", "WHATEVER OPENED THE GATE", "WAITS PAST THE DEIMOS RELAY.", "YOUR TOUR OF PHOBOS IS OVER.", "", "THANKS FOR PLAYING JAIDOOM"})
	if _j_starts_with(GAME_MAP, "MAP") then
		title = "THE INVASION IS OVER"
		lines = _j_array({"THIRTY MAPS DOWN.", "THE ICON SPEAKS NO MORE,", "AND EARTH IS QUIET AGAIN.", "", "THANKS FOR PLAYING JAIDOOM"})
	end
	draw_text_center(title, y, ((sc * 2) + 0.0))
	y = (y + (24 * sc))
	do
		local i = 0
		while (i < (#lines)) do
			do
				local line = lines[i + 1]
				if (line ~= "") then
					draw_text_center(line, y, (sc + 0.0))
				end
				y = (y + (12 * sc))
			end
			::_jd_continue_11::
			i = i + 1
		end
	end
	draw_text_center("PRESS ANY KEY", (VH - (10 * sc)), (sc + 0.0))
end
AM_CHEAT = 0
AM_MARKS = _j_array({})
function am_mark_add()
	if ((#AM_MARKS) >= 10) then
		set_msg("no more marks")
		return
	end
	table.insert(AM_MARKS, _j_array({P_X, P_Y}))
	set_msg((("mark " .. _j_str((#AM_MARKS))) .. " dropped"))
end
function am_mark_clear()
	AM_MARKS = _j_array({})
	set_msg("marks cleared")
end
function am_marks_reset()
	AM_MARKS = _j_array({})
end
function am_zoom(f)
	AM_ZOOM = (AM_ZOOM * f)
	if (AM_ZOOM < 0.5) then
		AM_ZOOM = 0.5
	end
	if (AM_ZOOM > 8.0) then
		AM_ZOOM = 8.0
	end
end
function am_px(x, y, c, hlim)
	if ((((x >= 0) and (x < VW)) and (y >= 0)) and (y < hlim)) then
		PIX[((y * VW) + x) + 1] = c
	end
end
AM_T0 = 0.0
AM_T1 = 0.0
function am_clip(p, q)
	if (p == 0.0) then
		return (q >= 0.0)
	end
	local r = (q / p)
	if (p < 0.0) then
		if (r > AM_T1) then
			return false
		end
		if (r > AM_T0) then
			AM_T0 = r
		end
	else
		if (r < AM_T0) then
			return false
		end
		if (r < AM_T1) then
			AM_T1 = r
		end
	end
	return true
end
function am_line(x0, y0, x1, y1, c, hlim)
	AM_T0 = 0.0
	AM_T1 = 1.0
	local dx = (x1 - x0)
	local dy = (y1 - y0)
	if (not am_clip((-dx), x0)) then
		return
	end
	if (not am_clip(dx, ((VW - 1.0) - x0))) then
		return
	end
	if (not am_clip((-dy), y0)) then
		return
	end
	if (not am_clip(dy, ((hlim - 1.0) - y0))) then
		return
	end
	local ix0 = _j_int((x0 + (dx * AM_T0)))
	local iy0 = _j_int((y0 + (dy * AM_T0)))
	local ix1 = _j_int((x0 + (dx * AM_T1)))
	local iy1 = _j_int((y0 + (dy * AM_T1)))
	local adx = (ix1 - ix0)
	if (adx < 0) then
		adx = (-adx)
	end
	local ady = (iy1 - iy0)
	if (ady < 0) then
		ady = (-ady)
	end
	local sx = ((ix0 < ix1) and 1 or -1)
	local sy = ((iy0 < iy1) and 1 or -1)
	local err = (adx - ady)
	do
		while true do
			do
				am_px(ix0, iy0, c, hlim)
				if ((ix0 == ix1) and (iy0 == iy1)) then
					break
				end
				local e2 = (err * 2)
				if (e2 > (-ady)) then
					err = (err - ady)
					ix0 = (ix0 + sx)
				end
				if (e2 < adx) then
					err = (err + adx)
					iy0 = (iy0 + sy)
				end
			end
			::_jd_continue_12::
		end
	end
end
function automap_draw()
	local amh = VIEWH
	local pix = PIX
	local n = (VW * amh)
	for i = 0, (n - 1), 1 do
		do
			pix[i + 1] = 0
		end
		::_jd_continue_13::
	end
	local spanx = ((MAP_MAXX - MAP_MINX) * 1.0)
	local spany = ((MAP_MAXY - MAP_MINY) * 1.0)
	if (spanx < 1.0) then
		spanx = 1.0
	end
	if (spany < 1.0) then
		spany = 1.0
	end
	local sx = ((VW - 8.0) / spanx)
	local sy = ((amh - 8.0) / spany)
	local sc = (((sx < sy) and sx or sy) * AM_ZOOM)
	local cx = (VW / 2.0)
	local cy = (amh / 2.0)
	local c1 = rgb_idx(255, 96, 64)
	local c2 = rgb_idx(120, 120, 130)
	if (P_ALLMAP or (AM_CHEAT > 0)) then
		c2 = rgb_idx(200, 200, 215)
	end
	local cp = rgb_idx(255, 255, 255)
	local nl = (#LD_V1)
	for l = 0, (nl - 1), 1 do
		do
			local x0 = (cx + ((VX_X[LD_V1[l + 1] + 1] - P_X) * sc))
			local y0 = (cy - ((VX_Y[LD_V1[l + 1] + 1] - P_Y) * sc))
			local x1 = (cx + ((VX_X[LD_V2[l + 1] + 1] - P_X) * sc))
			local y1 = (cy - ((VX_Y[LD_V2[l + 1] + 1] - P_Y) * sc))
			local cline = c2
			if (LD_SIDE1[l + 1] < 0) then
				cline = c1
			end
			am_line(x0, y0, x1, y1, cline, amh)
		end
		::_jd_continue_14::
	end
	if (AM_CHEAT >= 2) then
		local ct = rgb_idx(96, 255, 96)
		local nmo = (#MO_X)
		for mo = 0, (nmo - 1), 1 do
			do
				local st = MO_STATE[mo + 1]
				if ((st == 0) or (st == 6)) then
					goto _jd_continue_15
				end
				local tx = (cx + ((MO_X[mo + 1] - P_X) * sc))
				local ty = (cy - ((MO_Y[mo + 1] - P_Y) * sc))
				am_line((tx - 2.0), ty, (tx + 2.0), ty, ct, amh)
				am_line(tx, (ty - 2.0), tx, (ty + 2.0), ct, amh)
			end
			::_jd_continue_15::
		end
	end
	do
		local mk = 0
		while (mk < (#AM_MARKS)) do
			do
				local pt = AM_MARKS[mk + 1]
				draw_text(_j_str((mk + 1)), _j_int((cx + ((pt[0 + 1] - P_X) * sc))), _j_int((cy - ((pt[1 + 1] - P_Y) * sc))), 1.0)
			end
			::_jd_continue_16::
			mk = mk + 1
		end
	end
	local hx = (cx + (math.cos(P_ANG) * 7.0))
	local hy = (cy - (math.sin(P_ANG) * 7.0))
	am_line((cx - (math.cos(P_ANG) * 4.0)), (cy + (math.sin(P_ANG) * 4.0)), hx, hy, cp, amh)
	am_line(hx, hy, (hx + (math.cos((P_ANG + 2.6)) * 4.0)), (hy - (math.sin((P_ANG + 2.6)) * 4.0)), cp, amh)
	am_line(hx, hy, (hx + (math.cos((P_ANG - 2.6)) * 4.0)), (hy - (math.sin((P_ANG - 2.6)) * 4.0)), cp, amh)
end
