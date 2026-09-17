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
