-- Generated from tex.jai by tools/transpile.py; see PORTING.md.
require("wad")
require("vid")
DPAL = _j_array({})
CMAP = _j_array({})
PALCM = _j_array({})
TEX_NAMES = {}
TEX_W = _j_array({})
TEX_H = _j_array({})
TEX_READY = _j_array({})
TEX_PIX = _j_array({})
TEX_DEFOFS = _j_array({})
PNAME_LUMP = _j_array({})
FLAT_CACHE = {}
SKY_TEX = -1
CUR_BANK = -1
TEX_WORKERS = 1
function set_palette_bank(b)
	if (b == CUR_BANK) then
		return
	end
	CUR_BANK = b
	local pp = (LUMP_POS[lump_num("PLAYPAL") + 1] + (b * 768))
	DPAL = _j_array({})
	for i = 0, (256 - 1), 1 do
		do
			table.insert(DPAL, rgb_idx(u8((pp + (i * 3))), u8(((pp + (i * 3)) + 1)), u8(((pp + (i * 3)) + 2))))
		end
		::_jd_continue_1::
	end
	local total = 8704
	PALCM = _j_array({})
	for k = 0, (total - 1), 1 do
		do
			table.insert(PALCM, DPAL[CMAP[k + 1] + 1])
		end
		::_jd_continue_2::
	end
end
function gfx_init()
	local cm = LUMP_POS[lump_num("COLORMAP") + 1]
	CMAP = _j_array({})
	local total = 8704
	local w = WAD
	for c = 0, (total - 1), 1 do
		do
			table.insert(CMAP, (string.byte(w, (cm + c) + 1) + 0))
		end
		::_jd_continue_3::
	end
	CUR_BANK = -1
	set_palette_bank(0)
	local pn = LUMP_POS[lump_num("PNAMES") + 1]
	local npatch = i32(pn)
	PNAME_LUMP = _j_array({})
	for p = 0, (npatch - 1), 1 do
		do
			local nm = string.upper(name8(((pn + 4) + (p * 8))))
			table.insert(PNAME_LUMP, lump_num(nm))
		end
		::_jd_continue_4::
	end
	local t1 = LUMP_POS[lump_num("TEXTURE1") + 1]
	local ntex = i32(t1)
	TEX_NAMES = {}
	TEX_W = _j_array({})
	TEX_H = _j_array({})
	TEX_READY = _j_array({})
	TEX_PIX = _j_array({})
	TEX_DEFOFS = _j_array({})
	for t = 0, (ntex - 1), 1 do
		do
			local dof = (t1 + i32(((t1 + 4) + (t * 4))))
			local tname = name8(dof)
			TEX_NAMES[tname] = t
			table.insert(TEX_W, i16((dof + 12)))
			table.insert(TEX_H, i16((dof + 14)))
			table.insert(TEX_READY, 0)
			table.insert(TEX_PIX, _j_array({}))
			table.insert(TEX_DEFOFS, dof)
		end
		::_jd_continue_5::
	end
	SKY_TEX = tex_id("SKY1")
	FLAT_CACHE = {}
end
function tex_id(name)
	if (TEX_NAMES[name] ~= nil) then
		return TEX_NAMES[name]
	end
	return -1
end
function tex_composite(id)
	local texw = TEX_W[id + 1]
	local texh = TEX_H[id + 1]
	local pix = _j_array({})
	local n = (texw * texh)
	for i = 0, (n - 1), 1 do
		do
			table.insert(pix, 0)
		end
		::_jd_continue_6::
	end
	local dof = TEX_DEFOFS[id + 1]
	local npatch = i16((dof + 20))
	local known = (#PNAME_LUMP)
	for p = 0, (npatch - 1), 1 do
		do
			local pref = ((dof + 22) + (p * 10))
			local ox = i16(pref)
			local oy = i16((pref + 2))
			local pidx = i16((pref + 4))
			if ((pidx < 0) or (pidx >= known)) then
				goto _jd_continue_7
			end
			local lumpidx = PNAME_LUMP[pidx + 1]
			if (lumpidx < 0) then
				goto _jd_continue_7
			end
			local pofs = LUMP_POS[lumpidx + 1]
			local pw = i16(pofs)
			for col = 0, (pw - 1), 1 do
				do
					local dx = (ox + col)
					if ((dx < 0) or (dx >= texw)) then
						goto _jd_continue_8
					end
					local colbase = (dx * texh)
					local post = (pofs + u32(((pofs + 8) + (col * 4))))
					do
						while (string.byte(WAD, post + 1) ~= 255) do
							do
								local topdelta = (string.byte(WAD, post + 1) + 0)
								local len = (string.byte(WAD, (post + 1) + 1) + 0)
								local src = (post + 3)
								for i2 = 0, (len - 1), 1 do
									do
										local dy = ((oy + topdelta) + i2)
										if ((dy >= 0) and (dy < texh)) then
											pix[(colbase + dy) + 1] = (string.byte(WAD, (src + i2) + 1) + 0)
										end
									end
									::_jd_continue_10::
								end
								post = ((post + len) + 4)
							end
							::_jd_continue_9::
						end
					end
				end
				::_jd_continue_8::
			end
		end
		::_jd_continue_7::
	end
	return pix
end
function tex_prepare(id)
	if ((id < 0) or (TEX_READY[id + 1] ~= 0)) then
		return
	end
	TEX_PIX[id + 1] = tex_composite(id)
	TEX_READY[id + 1] = 1
end
MTEX_PIX = {}
function tex_masked(id)
	if (MTEX_PIX[id] ~= nil) then
		return
	end
	local texw = TEX_W[id + 1]
	local texh = TEX_H[id + 1]
	local pix = _j_array({})
	local n = (texw * texh)
	for i = 0, (n - 1), 1 do
		do
			table.insert(pix, -1)
		end
		::_jd_continue_11::
	end
	local dof = TEX_DEFOFS[id + 1]
	local npatch = i16((dof + 20))
	local known = (#PNAME_LUMP)
	for p = 0, (npatch - 1), 1 do
		do
			local pref = ((dof + 22) + (p * 10))
			local ox = i16(pref)
			local oy = i16((pref + 2))
			local pidx = i16((pref + 4))
			if ((pidx < 0) or (pidx >= known)) then
				goto _jd_continue_12
			end
			local lumpidx = PNAME_LUMP[pidx + 1]
			if (lumpidx < 0) then
				goto _jd_continue_12
			end
			local pofs = LUMP_POS[lumpidx + 1]
			local pw = i16(pofs)
			for col = 0, (pw - 1), 1 do
				do
					local dx = (ox + col)
					if ((dx < 0) or (dx >= texw)) then
						goto _jd_continue_13
					end
					local colbase = (dx * texh)
					local post = (pofs + u32(((pofs + 8) + (col * 4))))
					do
						while (string.byte(WAD, post + 1) ~= 255) do
							do
								local topdelta = (string.byte(WAD, post + 1) + 0)
								local len = (string.byte(WAD, (post + 1) + 1) + 0)
								local src = (post + 3)
								for i2 = 0, (len - 1), 1 do
									do
										local dy = ((oy + topdelta) + i2)
										if ((dy >= 0) and (dy < texh)) then
											pix[(colbase + dy) + 1] = (string.byte(WAD, (src + i2) + 1) + 0)
										end
									end
									::_jd_continue_15::
								end
								post = ((post + len) + 4)
							end
							::_jd_continue_14::
						end
					end
				end
				::_jd_continue_13::
			end
		end
		::_jd_continue_12::
	end
	MTEX_PIX[id] = pix
end
function flat_pix(name)
	if (FLAT_CACHE[name] ~= nil) then
		return FLAT_CACHE[name]
	end
	local lu = lump_num(name)
	local out = _j_array({})
	if ((lu < 0) or (LUMP_SIZE[lu + 1] < 4096)) then
		for i = 0, (4096 - 1), 1 do
			do
				table.insert(out, 96)
			end
			::_jd_continue_16::
		end
	else
		local pos = LUMP_POS[lu + 1]
		local w = WAD
		for i2 = 0, (4096 - 1), 1 do
			do
				table.insert(out, (string.byte(w, (pos + i2) + 1) + 0))
			end
			::_jd_continue_17::
		end
	end
	FLAT_CACHE[name] = out
	return out
end
