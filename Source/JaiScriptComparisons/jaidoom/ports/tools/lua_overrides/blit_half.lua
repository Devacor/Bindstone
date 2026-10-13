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
