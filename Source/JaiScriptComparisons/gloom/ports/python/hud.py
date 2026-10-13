"""HUD: status strip, vitals with bars, ammo rack, the face, the bobbing
weapon art — plus the full-screen states (title, tally, death, victory).
NOTE: the title-screen tagline is kept byte-identical to the reference so the
frame-stream hash stays comparable across ports."""

from math import sin

import render
import state
from data import AMMO_MAX, MAPS, WEAPONS, WGFX_FIRE, WGFX_IDLE
from util import ESC, iclamp, pad_to, trunc

FACES = {
	"ok": (
		" .-----. ",
		" | o.o | ",
		" |  L  | ",
		" | --- | ",
		" '-----' "),
	"hurt": (
		" .-----. ",
		" | o.o | ",
		" | ,L  | ",
		" | ~~~ | ",
		" '-----' "),
	"bad": (
		" .-----. ",
		" | x.o |,",
		" | ,L. |,",
		" | www | ",
		" '-----' "),
	"crit": (
		" .-----. ",
		" |,x.x,|,",
		" |,,L,,|,",
		" | mmm |,",
		" '-----' "),
	"pain": (
		" .-----. ",
		" | >.< | ",
		" | ,L, | ",
		" | OOO | ",
		" '-----' "),
	"grin": (
		" .-----. ",
		" | ^.^ | ",
		" |  L  | ",
		" | \\_/ | ",
		" '-----' "),
	"dead": (
		" .-----. ",
		" | x.x | ",
		" |  L  | ",
		" | ___ | ",
		" '-----' "),
}


def face_name():
	g = state.G
	if g.mode == 3:
		return "dead"
	if g.face_pain > 0:
		return "pain"
	if g.face_grin > 0:
		return "grin"
	if g.hp > 70:
		return "ok"
	if g.hp > 40:
		return "hurt"
	if g.hp > 18:
		return "bad"
	return "crit"


def meter(val, cap, slots):
	fill = iclamp(val * slots // max(1, cap), 0, slots)
	return "#" * fill + "-" * (slots - fill)


def show_msg(text, color):
	g = state.G
	g.msg = text
	g.msg_color = color
	g.msg_t = 75


def hud_status_row():
	"""the status strip above the panel: map, tallies, message"""
	g = state.G
	left = f" {g.map_name}  KILLS {g.kills}/{g.kill_total}  SCRT {g.secrets}/{g.secret_total}"
	m = g.msg if g.msg_t > 0 else ""
	room = g.w - len(left) - 2
	if len(m) > room:
		m = m[:max(0, room)]
	body = pad_to(left + " " * max(1, g.w - len(left) - len(m) - 1) + m, g.w)
	if g.msg_t > 0:
		strip = f"{ESC}[48;5;235m{ESC}[{g.msg_color}m"
	else:
		strip = f"{ESC}[48;5;235m{ESC}[38;5;250m"
	return strip + body + render.RESET


def hud_panel_rows(fps, ms_sim, ms_draw):
	"""five panel rows: vitals | ammo | face | weapon art"""
	g = state.G
	w = WEAPONS[g.weapon]
	face = FACES[face_name()]

	# weapon art with walk-bob shift and fire frame
	if g.cooldown > w["cd"] - 4 or g.gun_kick > 0:
		art = WGFX_FIRE[g.weapon]
	else:
		art = WGFX_IDLE[g.weapon]
	bob_shift = 1 + trunc((sin(g.bob) + 1.0) * 1.4)

	left = (
		f" HP  [{meter(g.hp, 100, 12)}] {g.hp}",
		f" ARM [{meter(g.armor, 100, 12)}] {g.armor}",
		" ",
		" " + ("[R]" if g.key_r else " . ") + " " + ("[B]" if g.key_b else " . ") + "  " + w["name"],
		" " + ("AUTOPILOT" if g.autopilot else ""),
	)
	mid = (
		f"BUL {g.ammo[0]}/{AMMO_MAX[0]}",
		f"SHL {g.ammo[1]}/{AMMO_MAX[1]}",
		f"CEL {g.ammo[2]}/{AMMO_MAX[2]}",
		("2:SCAT " if g.have[1] == 1 else "       ") + ("3:HEX" if g.have[2] == 1 else ""),
		"%.0ffps %.1fms" % (fps, ms_sim + ms_draw) if fps > 0.0 else "",
	)

	rows = []
	left_w = 34
	mid_w = 18
	face_w = 11
	art_w = 14
	for r in range(5):
		lseg = pad_to(left[r], left_w)
		mseg = pad_to(mid[r], mid_w)
		fseg = pad_to(face[r], face_w)
		used = left_w + mid_w + face_w + art_w
		fill = max(0, g.w - used)
		aseg = pad_to(" " * min(bob_shift if fill > 0 else 0, 2) + art[r], art_w + fill)
		hp_col = "97" if g.hp > 40 else "91"
		rows.append(f"{ESC}[48;5;236m{ESC}[{hp_col}m{lseg}"
			f"{ESC}[38;5;179m{mseg}"
			f"{ESC}[38;5;114m{fseg}"
			f"{ESC}[38;5;250m{aseg[:max(0, g.w - left_w - mid_w - face_w)]}{render.RESET}")
	return rows


# ------------------------------------------------------- full-screen modes --
LOGO = (
	"  ..|'''.|  '||'       ..|''||    ..|''||   '||    ||' ",
	" .|'     '   ||       .|'    ||  .|'    ||   |||  |||  ",
	" ||    ....  ||       ||      || ||      ||  |'|..'||  ",
	" '|.    ||   ||       '|.     || '|.     ||  | '|' ||  ",
	"  ''|...'|  .||.....|  ''|...|'   ''|...|'  .|. | .||. ",
)


def screen_rows(lines, tint):
	"""center the payload block on a dark field, pad every row to full width"""
	g = state.G
	top = max(0, (g.h - len(lines)) // 2)
	rows = []
	for y in range(g.h):
		li = y - top
		body = lines[li] if 0 <= li < len(lines) else ""
		padl = max(0, (g.w - len(body)) // 2)
		rows.append(f"{ESC}[48;5;233m{tint}{pad_to(' ' * padl + body, g.w)}{render.RESET}")
	return rows


def render_title():
	g = state.G
	lines = list(LOGO)
	lines.append("")
	lines.append("a  t e r m i n a l  r a y c a s t e r ,  a l l  J a i S c r i p t")
	lines.append("")
	lines.append("")
	blink = (g.tick // 12) % 2
	lines.append(">>> press FIRE to descend <<<" if blink == 0 else "")
	lines.append("")
	lines.append("w/s move  a/d strafe  arrows turn  SPACE fire  E use  1/2/3 weapons  m map")
	tint = f"{ESC}[38;5;203m" if (g.tick // 4) % 3 == 0 else f"{ESC}[38;5;209m"
	return "\n".join(screen_rows(lines, tint))


def render_tally():
	g = state.G
	kp = g.kills * 100 // g.kill_total if g.kill_total > 0 else 100
	sp = g.secrets * 100 // g.secret_total if g.secret_total > 0 else 100
	secs = g.map_ticks // 30
	par_secs = g.par // 30
	lines = (
		g.map_name,
		"",
		"=== FLOOR CLEARED ===",
		"",
		f"KILLS    {g.kills} / {g.kill_total}   ({kp}%)",
		f"SECRETS  {g.secrets} / {g.secret_total}   ({sp}%)",
		f"TIME     {secs}s   par {par_secs}s",
		"",
		"press FIRE for the next floor" if g.map_i + 1 < len(MAPS) else "press FIRE to face the dark",
	)
	return "\n".join(screen_rows(lines, f"{ESC}[38;5;114m"))


def render_dead():
	g = state.G
	lines = (
		"Y O U   D I E D",
		"",
		f"slain by {g.death_cause}",
		f"on {g.map_name}",
		"",
		f"kills {g.kills}/{g.kill_total}   deaths {g.deaths}",
		"",
		"press FIRE to rise again",
	)
	return "\n".join(screen_rows(lines, f"{ESC}[38;5;196m"))


def render_victory():
	g = state.G
	mins = g.ep_ticks // 1800
	secs = (g.ep_ticks // 30) % 60
	lines = list(LOGO)
	lines.append("")
	lines.append("T H E   W A R D E N   I S   D O W N")
	lines.append("")
	lines.append(f"episode kills    {g.ep_kills} / {g.ep_kill_total}")
	lines.append(f"episode secrets  {g.ep_secrets} / {g.ep_secret_total}")
	lines.append(f"episode time     {mins}m {secs}s   deaths {g.deaths}")
	lines.append("")
	lines.append("the gloom lifts. for now.        (ESC quits)")
	return "\n".join(screen_rows(lines, f"{ESC}[38;5;220m"))


def render_play(fps, ms_sim, ms_draw):
	"""the play-mode frame: world rows + status strip + panel"""
	rows = render.render_view()
	rows.append(hud_status_row())
	rows.extend(hud_panel_rows(fps, ms_sim, ms_draw))
	return "\n".join(rows)


def render_frame(fps, ms_sim, ms_draw):
	mode = state.G.mode
	if mode == 0:
		return render_title()
	if mode == 2:
		return render_tally()
	if mode == 3:
		return render_dead()
	if mode == 4:
		return render_victory()
	return render_play(fps, ms_sim, ms_draw)
