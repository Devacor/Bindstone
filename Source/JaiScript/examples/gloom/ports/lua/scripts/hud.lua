-- HUD: status strip, vitals with bars, ammo rack, the face, the bobbing
-- weapon art — plus the full-screen states (title, tally, death, victory).

local sin, floor = math.sin, math.floor
local rep, format = string.rep, string.format
local concat = table.concat

FACES = {
	ok = {
		" .-----. ",
		" | o.o | ",
		" |  L  | ",
		" | --- | ",
		" '-----' "},
	hurt = {
		" .-----. ",
		" | o.o | ",
		" | ,L  | ",
		" | ~~~ | ",
		" '-----' "},
	bad = {
		" .-----. ",
		" | x.o |,",
		" | ,L. |,",
		" | www | ",
		" '-----' "},
	crit = {
		" .-----. ",
		" |,x.x,|,",
		" |,,L,,|,",
		" | mmm |,",
		" '-----' "},
	pain = {
		" .-----. ",
		" | >.< | ",
		" | ,L, | ",
		" | OOO | ",
		" '-----' "},
	grin = {
		" .-----. ",
		" | ^.^ | ",
		" |  L  | ",
		" | \\_/ | ",
		" '-----' "},
	dead = {
		" .-----. ",
		" | x.x | ",
		" |  L  | ",
		" | ___ | ",
		" '-----' "},
}

function face_name()
	local g = G
	if g.mode == 3 then return "dead" end
	if g.face_pain > 0 then return "pain" end
	if g.face_grin > 0 then return "grin" end
	if g.hp > 70 then return "ok" end
	if g.hp > 40 then return "hurt" end
	if g.hp > 18 then return "bad" end
	return "crit"
end

function meter(val, cap, slots)
	local fill = iclamp(val * slots // imax(1, cap), 0, slots)
	return rep("#", fill) .. rep("-", slots - fill)
end

function show_msg(text, color)
	local g = G
	g.msg = text
	g.msg_color = color
	g.msg_t = 75
end

-- the status strip above the panel: map, tallies, message
function hud_status_row()
	local g = G
	local left = " " .. g.map_name .. "  KILLS " .. g.kills .. "/" .. g.kill_total ..
		"  SCRT " .. g.secrets .. "/" .. g.secret_total
	local m = g.msg_t > 0 and g.msg or ""
	local room = g.w - #left - 2
	if #m > room then m = m:sub(1, imax(0, room)) end
	local body = pad_to(left .. rep(" ", imax(1, g.w - #left - #m - 1)) .. m, g.w)
	local strip = ESC .. "[48;5;235m" .. ESC .. "[38;5;250m"
	if g.msg_t > 0 then
		strip = ESC .. "[48;5;235m" .. ESC .. "[" .. g.msg_color .. "m"
	end
	return strip .. body .. RESET
end

-- five panel rows: vitals | ammo | face | weapon art
function hud_panel_rows(fps, ms_sim, ms_draw)
	local g = G
	local w = WEAPONS[g.weapon + 1]
	local face = FACES[face_name()]

	-- weapon art with walk-bob shift and fire frame
	local art
	if g.cooldown > w.cd - 4 or g.gun_kick > 0 then art = WGFX_FIRE[g.weapon + 1]
	else art = WGFX_IDLE[g.weapon + 1] end
	local bob_shift = 1 + trunc((sin(g.bob) + 1.0) * 1.4)

	local left = {
		" HP  [" .. meter(g.hp, 100, 12) .. "] " .. g.hp,
		" ARM [" .. meter(g.armor, 100, 12) .. "] " .. g.armor,
		" ",
		" " .. (g.key_r and "[R]" or " . ") .. " " .. (g.key_b and "[B]" or " . ") .. "  " .. w.name,
		" " .. (g.autopilot and "AUTOPILOT" or ""),
	}
	local mid = {
		"BUL " .. g.ammo[1] .. "/" .. AMMO_MAX[1],
		"SHL " .. g.ammo[2] .. "/" .. AMMO_MAX[2],
		"CEL " .. g.ammo[3] .. "/" .. AMMO_MAX[3],
		(g.have[2] == 1 and "2:SCAT " or "       ") .. (g.have[3] == 1 and "3:HEX" or ""),
		fps > 0.0 and format("%.0ffps %.1fms", fps, ms_sim + ms_draw) or "",
	}

	local rows = {}
	local left_w = 34
	local mid_w = 18
	local face_w = 11
	local art_w = 14
	for r = 1, 5 do
		local lseg = pad_to(left[r], left_w)
		local mseg = pad_to(mid[r], mid_w)
		local fseg = pad_to(face[r], face_w)
		local used = left_w + mid_w + face_w + art_w
		local fill = imax(0, g.w - used)
		local aseg = pad_to(rep(" ", imin(fill > 0 and bob_shift or 0, 2)) .. art[r], art_w + fill)
		local hp_col = g.hp > 40 and "97" or "91"
		rows[r] = ESC .. "[48;5;236m" .. ESC .. "[" .. hp_col .. "m" .. lseg ..
			ESC .. "[38;5;179m" .. mseg ..
			ESC .. "[38;5;114m" .. fseg ..
			ESC .. "[38;5;250m" .. aseg:sub(1, imax(0, g.w - left_w - mid_w - face_w)) .. RESET
	end
	return rows
end

-- ------------------------------------------------------- full-screen modes --
LOGO = {
	"  ..|'''.|  '||'       ..|''||    ..|''||   '||    ||' ",
	" .|'     '   ||       .|'    ||  .|'    ||   |||  |||  ",
	" ||    ....  ||       ||      || ||      ||  |'|..'||  ",
	" '|.    ||   ||       '|.     || '|.     ||  | '|' ||  ",
	"  ''|...'|  .||.....|  ''|...|'   ''|...|'  .|. | .||. ",
}

function screen_rows(lines, tint)
	-- center the payload block on a dark field, pad every row to full width
	local g = G
	local rows = {}
	local total = g.h
	local top = imax(0, (total - #lines) // 2)
	for y = 0, total - 1 do
		local li = y - top
		local body = ""
		if li >= 0 and li < #lines then body = lines[li + 1] end
		local padl = imax(0, (g.w - #body) // 2)
		rows[y + 1] = ESC .. "[48;5;233m" .. tint .. pad_to(rep(" ", padl) .. body, g.w) .. RESET
	end
	return rows
end

function render_title()
	local g = G
	local lines = {}
	for i = 1, #LOGO do lines[i] = LOGO[i] end
	lines[#lines + 1] = ""
	lines[#lines + 1] = "a  t e r m i n a l  r a y c a s t e r ,  a l l  J a i S c r i p t"
	lines[#lines + 1] = ""
	lines[#lines + 1] = ""
	local blink = (g.tick // 12) % 2
	lines[#lines + 1] = blink == 0 and ">>> press FIRE to descend <<<" or ""
	lines[#lines + 1] = ""
	lines[#lines + 1] = "w/s move  a/d strafe  arrows turn  SPACE fire  E use  1/2/3 weapons  m map"
	local tint = (g.tick // 4) % 3 == 0 and (ESC .. "[38;5;203m") or (ESC .. "[38;5;209m")
	return concat(screen_rows(lines, tint), "\n")
end

function render_tally()
	local g = G
	local kp = g.kill_total > 0 and g.kills * 100 // g.kill_total or 100
	local sp = g.secret_total > 0 and g.secrets * 100 // g.secret_total or 100
	local secs = g.map_ticks // 30
	local par_secs = g.par // 30
	local lines = {
		g.map_name,
		"",
		"=== FLOOR CLEARED ===",
		"",
		"KILLS    " .. g.kills .. " / " .. g.kill_total .. "   (" .. kp .. "%)",
		"SECRETS  " .. g.secrets .. " / " .. g.secret_total .. "   (" .. sp .. "%)",
		"TIME     " .. secs .. "s   par " .. par_secs .. "s",
		"",
		g.map_i + 1 < #MAPS and "press FIRE for the next floor" or "press FIRE to face the dark",
	}
	return concat(screen_rows(lines, ESC .. "[38;5;114m"), "\n")
end

function render_dead()
	local g = G
	local lines = {
		"Y O U   D I E D",
		"",
		"slain by " .. g.death_cause,
		"on " .. g.map_name,
		"",
		"kills " .. g.kills .. "/" .. g.kill_total .. "   deaths " .. g.deaths,
		"",
		"press FIRE to rise again",
	}
	return concat(screen_rows(lines, ESC .. "[38;5;196m"), "\n")
end

function render_victory()
	local g = G
	local mins = g.ep_ticks // 1800
	local secs = (g.ep_ticks // 30) % 60
	local lines = {}
	for i = 1, #LOGO do lines[i] = LOGO[i] end
	lines[#lines + 1] = ""
	lines[#lines + 1] = "T H E   W A R D E N   I S   D O W N"
	lines[#lines + 1] = ""
	lines[#lines + 1] = "episode kills    " .. g.ep_kills .. " / " .. g.ep_kill_total
	lines[#lines + 1] = "episode secrets  " .. g.ep_secrets .. " / " .. g.ep_secret_total
	lines[#lines + 1] = "episode time     " .. mins .. "m " .. secs .. "s   deaths " .. g.deaths
	lines[#lines + 1] = ""
	lines[#lines + 1] = "the gloom lifts. for now.        (ESC quits)"
	return concat(screen_rows(lines, ESC .. "[38;5;220m"), "\n")
end

-- the play-mode frame: world rows + status strip + panel
function render_play(fps, ms_sim, ms_draw)
	local rows = render_view()
	local n = #rows
	n = n + 1
	rows[n] = hud_status_row()
	local panel = hud_panel_rows(fps, ms_sim, ms_draw)
	for i = 1, 5 do rows[n + i] = panel[i] end
	return concat(rows, "\n")
end

function render_frame(fps, ms_sim, ms_draw)
	local mode = G.mode
	if mode == 0 then return render_title() end
	if mode == 2 then return render_tally() end
	if mode == 3 then return render_dead() end
	if mode == 4 then return render_victory() end
	return render_play(fps, ms_sim, ms_draw)
end
