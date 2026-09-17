-- Generated from vid.jai by tools/transpile.py; see PORTING.md.
require("wad")
ESC = ""
RESET = ""
HALFG = ""
HOME = ""
CLEAR = ""
PIX = _j_array({})
VW = 0
VH = 0
VIEWH = 0
BLIT_WORKERS = 0
ROWIDX = _j_array({})
PAL_KEYS = {}
PAL_FG = _j_array({})
PAL_BG = _j_array({})
PAL_LUM = _j_array({})
PAL_R = _j_array({})
PAL_G = _j_array({})
PAL_B = _j_array({})
PAL_BGR3 = _j_array({})
function utf8(cp)
	if (cp < 128) then
		return ("" .. string.char((cp & 255)))
	end
	if (cp < 2048) then
		return (("" .. string.char(((192 + (cp // 64)) & 255))) .. string.char(((128 + (cp & 63)) & 255)))
	end
	if (cp < 65536) then
		return ((("" .. string.char(((224 + (cp // 4096)) & 255))) .. string.char(((128 + ((cp // 64) & 63)) & 255))) .. string.char(((128 + (cp & 63)) & 255)))
	end
	return (((("" .. string.char(((240 + (cp // 262144)) & 255))) .. string.char(((128 + ((cp // 4096) & 63)) & 255))) .. string.char(((128 + ((cp // 64) & 63)) & 255))) .. string.char(((128 + (cp & 63)) & 255)))
end
function vid_init(w, h)
	ESC = "\027"
	RESET = (ESC .. "[0m")
	HALFG = utf8(9600)
	HOME = (ESC .. "[H")
	CLEAR = (((ESC .. "[2J") .. ESC) .. "[H")
	VW = w
	VH = h
	VIEWH = h
	PIX = _j_array({})
	do
		local i = 0
		while (i < (w * h)) do
			do
				table.insert(PIX, 0)
			end
			::_jd_continue_1::
			i = i + 1
		end
	end
	ROWIDX = _j_array({})
	local vrows = _j_idiv(h, 2)
	for r = 0, (vrows - 1), 1 do
		do
			table.insert(ROWIDX, r)
		end
		::_jd_continue_2::
	end
	PAL_KEYS = {}
	PAL_FG = _j_array({})
	PAL_BG = _j_array({})
	PAL_LUM = _j_array({})
	PAL_R = _j_array({})
	PAL_G = _j_array({})
	PAL_B = _j_array({})
	PAL_BGR3 = _j_array({})
	rgb_idx(0, 0, 0)
end
function rgb_idx(r, g, b)
	local key = (((r * 65536) + (g * 256)) + b)
	if (PAL_KEYS[key] ~= nil) then
		return PAL_KEYS[key]
	end
	local idx = (#PAL_FG)
	PAL_KEYS[key] = idx
	table.insert(PAL_FG, (((((((("" .. ESC) .. "[38;2;") .. _j_str(r)) .. ";") .. _j_str(g)) .. ";") .. _j_str(b)) .. "m"))
	table.insert(PAL_BG, (((((((("" .. ESC) .. "[48;2;") .. _j_str(r)) .. ";") .. _j_str(g)) .. ";") .. _j_str(b)) .. "m"))
	table.insert(PAL_LUM, (((r * 2) + (g * 3)) + b))
	table.insert(PAL_R, r)
	table.insert(PAL_G, g)
	table.insert(PAL_B, b)
	table.insert(PAL_BGR3, ((("" .. string.char((b & 255))) .. string.char((g & 255))) .. string.char((r & 255))))
	return idx
end
function le16(v)
	return (("" .. string.char(((v & 255) & 255))) .. string.char((((v // 256) & 255) & 255)))
end
function le32(v)
	return (((("" .. string.char(((v & 255) & 255))) .. string.char((((v // 256) & 255) & 255))) .. string.char((((v // 65536) & 255) & 255))) .. string.char((((v // 16777216) & 255) & 255)))
end
function bmp_write(path)
	local pad = math.fmod((4 - math.fmod((VW * 3), 4)), 4)
	local stride = ((VW * 3) + pad)
	local out = ((("BM" .. le32((54 + (stride * VH)))) .. le32(0)) .. le32(54))
	out = (out .. ((((((le32(40) .. le32(VW)) .. le32(VH)) .. le16(1)) .. le16(24)) .. le32(0)) .. le32((stride * VH))))
	out = (out .. (((le32(2835) .. le32(2835)) .. le32(0)) .. le32(0)))
	local padding = ""
	for p = 0, (pad - 1), 1 do
		do
			padding = (padding .. string.char((0 & 255)))
		end
		::_jd_continue_3::
	end
	local pix = PIX
	local bgr = PAL_BGR3
	for y = (VH - 1), 0, -1 do
		do
			local base = (y * VW)
			local row = ""
			do
				local x = 0
				while (x < VW) do
					do
						row = (row .. bgr[pix[(base + x) + 1] + 1])
					end
					::_jd_continue_5::
					x = x + 1
				end
			end
			out = (out .. (row .. padding))
		end
		::_jd_continue_4::
	end
	write_file(path, out)
end
function pix_clear(color)
	local n = (VW * VH)
	for i = 0, (n - 1), 1 do
		do
			PIX[i + 1] = color
		end
		::_jd_continue_6::
	end
end
WIPE_TICS = 0
WIPE_COL = _j_array({})
WIPE_OLD = _j_array({})
WIPE_SEED = 1
function wipe_rnd(n)
	WIPE_SEED = (((WIPE_SEED * 1103515245) + 12345) & 2147483647)
	return math.fmod((WIPE_SEED // 65536), n)
end
function wipe_begin()
	WIPE_OLD = PIX
	WIPE_SEED = ((clock_ms() & 65535) | 1)
	WIPE_COL = _j_array({})
	table.insert(WIPE_COL, (0 - wipe_rnd(16)))
	do
		local x = 1
		while (x < VW) do
			do
				local o = ((WIPE_COL[(x - 1) + 1] + wipe_rnd(3)) - 1)
				if (o < -15) then
					o = -15
				end
				if (o > 0) then
					o = 0
				end
				table.insert(WIPE_COL, o)
			end
			::_jd_continue_7::
			x = x + 1
		end
	end
	WIPE_TICS = 1
end
function wipe_tic()
	local done = true
	do
		local x = 0
		while (x < VW) do
			do
				local o = WIPE_COL[x + 1]
				if (o < 0) then
					WIPE_COL[x + 1] = (o + 1)
					done = false
				else
					if (o < VH) then
						WIPE_COL[x + 1] = (o + ((o < 16) and (o + 1) or 8))
						done = false
					end
				end
			end
			::_jd_continue_8::
			x = x + 1
		end
	end
	return done
end
function wipe_overlay()
	do
		local x = 0
		while (x < VW) do
			do
				local o = WIPE_COL[x + 1]
				if (o >= VH) then
					goto _jd_continue_9
				end
				if (o < 0) then
					o = 0
				end
				for y = (VH - 1), o, -1 do
					do
						PIX[((y * VW) + x) + 1] = WIPE_OLD[(((y - o) * VW) + x) + 1]
					end
					::_jd_continue_10::
				end
			end
			::_jd_continue_9::
			x = x + 1
		end
	end
end
function pix_set(x, y, c)
	if ((((x >= 0) and (x < VW)) and (y >= 0)) and (y < VH)) then
		PIX[((y * VW) + x) + 1] = c
	end
end
function draw_line(x0, y0, x1, y1, c)
	local dx = (x1 - x0)
	if (dx < 0) then
		dx = (-dx)
	end
	local dy = (y1 - y0)
	if (dy < 0) then
		dy = (-dy)
	end
	local sx = ((x0 < x1) and 1 or -1)
	local sy = ((y0 < y1) and 1 or -1)
	local err = (dx - dy)
	do
		while true do
			do
				pix_set(x0, y0, c)
				if ((x0 == x1) and (y0 == y1)) then
					break
				end
				local e2 = (err * 2)
				if (e2 > (-dy)) then
					err = (err - dy)
					x0 = (x0 + sx)
				end
				if (e2 < dx) then
					err = (err + dx)
					y0 = (y0 + sy)
				end
			end
			::_jd_continue_11::
		end
	end
end
-- Lua strings are immutable: collect the same ordered fragments, then join once.
function vid_row_half(cy)
	local parts = {}
	local n = 0
	local vw = VW
	local fg, bg = -1, -1
	local ti = cy * 2 * vw
	local bi = ti + vw
	for x = 0, vw - 1 do
		local top = PIX[ti + x + 1]
		local bot = PIX[bi + x + 1]
		if top ~= fg then n = n + 1; parts[n] = PAL_FG[top + 1]; fg = top end
		if bot ~= bg then n = n + 1; parts[n] = PAL_BG[bot + 1]; bg = bot end
		n = n + 1; parts[n] = HALFG
	end
	parts[n + 1] = RESET
	return table.concat(parts)
end
-- Preserve every row and separator; use Lua's native string builder idiom.
function blit_half()
	if BLIT_WORKERS ~= 0 then
		local rows = parallel_transform(ROWIDX, vid_row_half)
		return table.concat(rows, "\n") .. "\n"
	end
	local rows = {}
	for cy = 0, _j_idiv(VH, 2) - 1 do rows[cy + 1] = vid_row_half(cy) end
	return table.concat(rows, "\n") .. "\n"
end
