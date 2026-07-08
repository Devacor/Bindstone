-- Data tables live in data.lua (generated from the reference so the art and
-- maps are byte-exact); this file owns the art parser.
-- parsed at boot: name -> {w, h, pix (flat codes, 0 transparent, 1-based
-- positions), rgb (rgb[code] = {r,g,b} for code 1..n)}
ARTS = {}

function parse_arts()
	ARTS = {}
	for name, src in pairs(ART_SRC) do
		local rows = src.rows
		local pal = src.pal
		local codes = {}     -- char -> code
		local rgb = {}       -- code -> {r,g,b}
		local next_code = 1
		local h = #rows
		local w = #rows[1]
		local pix = {}
		local np = 0
		for y = 1, h do
			local row = rows[y]
			for x = 1, w do
				local ch = row:sub(x, x)
				np = np + 1
				if ch == "." then
					pix[np] = 0
				else
					local c = codes[ch]
					if not c then
						c = next_code
						codes[ch] = c
						next_code = next_code + 1
						rgb[c] = pal[ch]
					end
					pix[np] = c
				end
			end
		end
		ARTS[name] = {w = w, h = h, pix = pix, rgb = rgb}
	end
end
