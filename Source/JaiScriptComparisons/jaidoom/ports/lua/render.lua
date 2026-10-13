-- Generated from render.jai by tools/transpile.py; see internal/examples/jaidoom/ports/PORTING.md.
require("wad")
require("map")
require("vid")
require("tex")
require("things")
VIEW_X = 0.0
VIEW_Y = 0.0
VIEW_Z = 0.0
VIEW_ANG = 0.0
VSIN = 0.0
VCOS = 0.0
FOCAL = 0.0
FOCALY = 0.0
CENTERX = 0
CENTERY = 0
PXASPECT = 1.2
EYE_Z = -100000.0
EXTRA_LIGHT = 0
WBOB_X = 0.0
WBOB_Y = 0.0
SEG_FSEC = _j_array({})
SEG_BSEC = _j_array({})
SEG_MIDT = _j_array({})
SEG_TOPT = _j_array({})
SEG_BOTT = _j_array({})
SEG_XOFS = _j_array({})
SEG_YOFS = _j_array({})
SEG_PEGT = _j_array({})
SEG_PEGB = _j_array({})
SEG_LEN = _j_array({})
FLAT_SLOT = {}
FLATS = _j_array({})
SEC_FFLAT = _j_array({})
SEC_CFLAT = _j_array({})
SEC_SKY = _j_array({})
CLIPTOP = _j_array({})
CLIPBOT = _j_array({})
COLDONE = _j_array({})
WALLZ = _j_array({})
CLIPS_N = _j_array({})
CLIPS_D = _j_array({})
CLIPS_T = _j_array({})
CLIPS_B = _j_array({})
REMAIN = 0
MSEG = _j_array({})
MSEG_D = _j_array({})
MASKED_DRAWN = 0
function flat_slot(name)
	if (FLAT_SLOT[name] ~= nil) then
		return FLAT_SLOT[name]
	end
	local idx = (#FLATS)
	FLAT_SLOT[name] = idx
	table.insert(FLATS, flat_pix(name))
	return idx
end
function sky_name(mapname)
	if _j_starts_with(mapname, "MAP") then
		local n = _j_to_int(_j_substr(mapname, 3, 2), 1)
		if (n <= 11) then
			return "SKY1"
		end
		if (n <= 20) then
			return "SKY2"
		end
		return "SKY3"
	end
	if (_j_starts_with(mapname, "E") and ((#mapname) >= 2)) then
		return ("SKY" .. _j_substr(mapname, 1, 1))
	end
	return "SKY1"
end
function prepare_render()
	gfx_init()
	local sk = tex_id(sky_name(MAP_NAME))
	if (sk >= 0) then
		SKY_TEX = sk
	end
	DARK_MAP = {}
	SEG_FSEC = _j_array({})
	SEG_BSEC = _j_array({})
	SEG_MIDT = _j_array({})
	SEG_TOPT = _j_array({})
	SEG_BOTT = _j_array({})
	SEG_XOFS = _j_array({})
	SEG_YOFS = _j_array({})
	SEG_PEGT = _j_array({})
	SEG_PEGB = _j_array({})
	SEG_LEN = _j_array({})
	local nsegs = (#SG_V1)
	for i = 0, (nsegs - 1), 1 do
		do
			local ld = SG_LINE[i + 1]
			local dir = SG_DIR[i + 1]
			local fside = 0
			local bside = 0
			if (dir == 0) then
				fside = LD_SIDE0[ld + 1]
				bside = LD_SIDE1[ld + 1]
			else
				fside = LD_SIDE1[ld + 1]
				bside = LD_SIDE0[ld + 1]
			end
			table.insert(SEG_FSEC, ((fside >= 0) and SD_SEC[fside + 1] or 0))
			table.insert(SEG_BSEC, ((bside >= 0) and SD_SEC[bside + 1] or -1))
			table.insert(SEG_MIDT, ((fside >= 0) and tex_id(SD_MID[fside + 1]) or -1))
			table.insert(SEG_TOPT, ((fside >= 0) and tex_id(SD_TOP[fside + 1]) or -1))
			table.insert(SEG_BOTT, ((fside >= 0) and tex_id(SD_BOT[fside + 1]) or -1))
			table.insert(SEG_XOFS, (((fside >= 0) and SD_XOFS[fside + 1] or 0) + SG_OFS[i + 1]))
			table.insert(SEG_YOFS, ((fside >= 0) and SD_YOFS[fside + 1] or 0))
			local flags = LD_FLAGS[ld + 1]
			table.insert(SEG_PEGT, (((flags & 8) ~= 0) and 1 or 0))
			table.insert(SEG_PEGB, (((flags & 16) ~= 0) and 1 or 0))
			local dxw = ((VX_X[SG_V2[i + 1] + 1] - VX_X[SG_V1[i + 1] + 1]) + 0.0)
			local dyw = ((VX_Y[SG_V2[i + 1] + 1] - VX_Y[SG_V1[i + 1] + 1]) + 0.0)
			table.insert(SEG_LEN, math.sqrt(((dxw * dxw) + (dyw * dyw))))
		end
		::_jd_continue_1::
	end
	FLAT_SLOT = {}
	FLATS = _j_array({})
	SEC_FFLAT = _j_array({})
	SEC_CFLAT = _j_array({})
	SEC_SKY = _j_array({})
	local nsecs = (#SE_FLOORH)
	for s = 0, (nsecs - 1), 1 do
		do
			table.insert(SEC_FFLAT, flat_slot(SE_FLOORPIC[s + 1]))
			table.insert(SEC_CFLAT, flat_slot(SE_CEILPIC[s + 1]))
			table.insert(SEC_SKY, ((SE_CEILPIC[s + 1] == "F_SKY1") and 1 or 0))
		end
		::_jd_continue_2::
	end
	tex_warm_map()
end
function tex_warm_note(tid, tids, seen)
	if ((tid < 0) or (TEX_READY[tid + 1] ~= 0)) then
		return
	end
	if (seen[tid] ~= nil) then
		return
	end
	seen[tid] = 1
	table.insert(tids, tid)
end
function tex_warm_map()
	local tids = _j_array({})
	local seen = {}
	tex_warm_note(SKY_TEX, tids, seen)
	local nsegs = (#SEG_MIDT)
	for i = 0, (nsegs - 1), 1 do
		do
			local mt = SEG_MIDT[i + 1]
			local tt = SEG_TOPT[i + 1]
			local bt = SEG_BOTT[i + 1]
			tex_warm_note(mt, tids, seen)
			tex_warm_note(tt, tids, seen)
			tex_warm_note(bt, tids, seen)
		end
		::_jd_continue_3::
	end
	local nt = (#tids)
	if (nt == 0) then
		return
	end
	if (TEX_WORKERS == 0) then
		for s = 0, (nt - 1), 1 do
			do
				local sid = tids[s + 1]
				tex_prepare(sid)
			end
			::_jd_continue_4::
		end
		return
	end
	local grids = parallel_transform(tids, tex_composite)
	for k = 0, (nt - 1), 1 do
		do
			local id = tids[k + 1]
			TEX_PIX[id + 1] = grids[k + 1]
			TEX_READY[id + 1] = 1
		end
		::_jd_continue_5::
	end
end
function light_band(light, dist)
	local band = ((((255 - light) // 8) + _j_int((dist / 512.0))) - EXTRA_LIGHT)
	if (band < 0) then
		band = 0
	end
	if (band > 31) then
		band = 31
	end
	return band
end
function point_side(x, y, n)
	local dx = (x - ND_X[n + 1])
	local dy = (y - ND_Y[n + 1])
	local cross = ((ND_DX[n + 1] * dy) - (ND_DY[n + 1] * dx))
	return ((cross > 0.0) and 1 or 0)
end
function sector_at(x, y)
	local n = ((#ND_X) - 1)
	do
		while ((n & 32768) == 0) do
			do
				if (point_side(x, y, n) == 0) then
					n = ND_RIGHT[n + 1]
				else
					n = ND_LEFT[n + 1]
				end
			end
			::_jd_continue_6::
		end
	end
	return SEG_FSEC[SS_FIRST[(n & 32767) + 1] + 1]
end
function draw_tex_col(x, y0, y1, tp, tw, th, ucol, vfx, dvfx, band)
	if (y0 < 0) then
		local skip = (-y0)
		vfx = (vfx + (dvfx * skip))
		y0 = 0
	end
	if (y1 >= VIEWH) then
		y1 = (VIEWH - 1)
	end
	local tc = math.fmod(ucol, tw)
	if (tc < 0) then
		tc = (tc + tw)
	end
	local base = (tc * th)
	local pcm = (band * 256)
	local pix = PIX
	local palcm = PALCM
	for y = y0, y1, 1 do
		do
			local iv = math.fmod((vfx // 65536), th)
			if (iv < 0) then
				iv = (iv + th)
			end
			pix[((y * VW) + x) + 1] = palcm[(pcm + tp[(base + iv) + 1]) + 1]
			vfx = (vfx + dvfx)
		end
		::_jd_continue_7::
	end
end
function draw_flat_col(x, y0, y1, flat, planeh, raydx, raydy, light)
	if (y0 < 0) then
		y0 = 0
	end
	if (y1 >= VIEWH) then
		y1 = (VIEWH - 1)
	end
	local dz = ((VIEW_Z - planeh) * FOCALY)
	local bandbase = (((255 - light) // 8) - EXTRA_LIGHT)
	local pix = PIX
	local palcm = PALCM
	for y = y0, y1, 1 do
		do
			local dy = (y - CENTERY)
			if (dy == 0) then
				goto _jd_continue_8
			end
			local z = (dz / dy)
			local wx = (VIEW_X + (raydx * z))
			local wy = (VIEW_Y + (raydy * z))
			local band = (bandbase + _j_int((z * 0.001953125)))
			if (band < 0) then
				band = 0
			end
			if (band > 31) then
				band = 31
			end
			pix[((y * VW) + x) + 1] = palcm[((band * 256) + flat[(((_j_int(wy) & 63) << (6 & 63)) + (_j_int(wx) & 63)) + 1]) + 1]
		end
		::_jd_continue_8::
	end
end
function draw_sky_col(x, y0, y1)
	if (y0 < 0) then
		y0 = 0
	end
	if (y1 >= VIEWH) then
		y1 = (VIEWH - 1)
	end
	if (SKY_TEX < 0) then
		return
	end
	local xfrac = ((x - CENTERX) / FOCAL)
	local adeg = (((VIEW_ANG - math.atan(xfrac, 1.0)) * 180.0) / PI)
	local ucol = _j_int((adeg * 2.8444444444444446))
	local tc = math.fmod(ucol, 256)
	if (tc < 0) then
		tc = (tc + 256)
	end
	local tp = TEX_PIX[SKY_TEX + 1]
	local th = TEX_H[SKY_TEX + 1]
	local base = (tc * th)
	local pix = PIX
	local palcm = PALCM
	for y = y0, y1, 1 do
		do
			local iv = _j_idiv((y * 128), VIEWH)
			if (iv >= th) then
				iv = (th - 1)
			end
			pix[((y * VW) + x) + 1] = palcm[tp[(base + iv) + 1] + 1]
		end
		::_jd_continue_9::
	end
end
function draw_seg(sg)
	local x1w = (VX_X[SG_V1[sg + 1] + 1] + 0.0)
	local y1w = (VX_Y[SG_V1[sg + 1] + 1] + 0.0)
	local x2w = (VX_X[SG_V2[sg + 1] + 1] + 0.0)
	local y2w = (VX_Y[SG_V2[sg + 1] + 1] + 0.0)
	local dx1 = (x1w - VIEW_X)
	local dy1 = (y1w - VIEW_Y)
	local dx2 = (x2w - VIEW_X)
	local dy2 = (y2w - VIEW_Y)
	local dep1 = ((dx1 * VCOS) + (dy1 * VSIN))
	local lat1 = ((dx1 * VSIN) - (dy1 * VCOS))
	local dep2 = ((dx2 * VCOS) + (dy2 * VSIN))
	local lat2 = ((dx2 * VSIN) - (dy2 * VCOS))
	local seglen = SEG_LEN[sg + 1]
	local u1 = 0.0
	local u2 = seglen
	local neareps = 0.05
	if ((dep1 < neareps) and (dep2 < neareps)) then
		return
	end
	if (dep1 < neareps) then
		local t = ((neareps - dep1) / (dep2 - dep1))
		dep1 = neareps
		lat1 = (lat1 + ((lat2 - lat1) * t))
		u1 = (t * seglen)
	else
		if (dep2 < neareps) then
			local t2 = ((neareps - dep2) / (dep1 - dep2))
			dep2 = neareps
			lat2 = (lat2 + ((lat1 - lat2) * t2))
			u2 = (seglen * (1.0 - t2))
		end
	end
	local x1f = (CENTERX + ((lat1 / dep1) * FOCAL))
	local x2f = (CENTERX + ((lat2 / dep2) * FOCAL))
	if (x2f <= x1f) then
		return
	end
	if ((x1f >= VW) or (x2f < 0.0)) then
		return
	end
	local fsec = SEG_FSEC[sg + 1]
	local bsec = SEG_BSEC[sg + 1]
	local fceil = (SE_CEILH[fsec + 1] + 0.0)
	local ffloor = (SE_FLOORH[fsec + 1] + 0.0)
	local light = SE_LIGHT[fsec + 1]
	local fsky = (SEC_SKY[fsec + 1] ~= 0)
	local solid = (bsec < 0)
	local bceil = 0.0
	local bfloor = 0.0
	local bsky = false
	if (not solid) then
		bceil = (SE_CEILH[bsec + 1] + 0.0)
		bfloor = (SE_FLOORH[bsec + 1] + 0.0)
		bsky = (SEC_SKY[bsec + 1] ~= 0)
		if (((bceil <= bfloor) or (bceil <= ffloor)) or (bfloor >= fceil)) then
			solid = true
		end
	end
	local midt = SEG_MIDT[sg + 1]
	local topt = SEG_TOPT[sg + 1]
	local bott = SEG_BOTT[sg + 1]
	if (((not solid) and (topt >= 0)) and (bceil < fceil)) then
		tex_prepare(topt)
	end
	if (((not solid) and (bott >= 0)) and (bfloor > ffloor)) then
		tex_prepare(bott)
	end
	if ((not solid) and (midt >= 0)) then
		table.insert(MSEG, sg)
		table.insert(MSEG_D, ((dep1 + dep2) * 0.5))
	end
	local xofs = SEG_XOFS[sg + 1]
	local yofs = SEG_YOFS[sg + 1]
	local amid = (((SEG_PEGB[sg + 1] ~= 0) and (midt >= 0)) and (ffloor + TEX_H[midt + 1]) or fceil)
	local atop = fceil
	if (((not solid) and (topt >= 0)) and (SEG_PEGT[sg + 1] == 0)) then
		atop = (bceil + TEX_H[topt + 1])
	end
	local abot = ((SEG_PEGB[sg + 1] ~= 0) and fceil or bfloor)
	local facet = midt
	local aface = amid
	if (solid and (bsec >= 0)) then
		if (topt >= 0) then
			facet = topt
			aface = ((SEG_PEGT[sg + 1] == 0) and (bceil + TEX_H[topt + 1]) or fceil)
		else
			if (bott >= 0) then
				facet = bott
				aface = ((SEG_PEGB[sg + 1] ~= 0) and fceil or bfloor)
			end
		end
	end
	if (solid and (facet >= 0)) then
		tex_prepare(facet)
	end
	local invz1 = (1.0 / dep1)
	local invz2 = (1.0 / dep2)
	local uz1 = (u1 * invz1)
	local uz2 = (u2 * invz2)
	local spanx = (x2f - x1f)
	local ctop = (fceil - VIEW_Z)
	local cbot = (ffloor - VIEW_Z)
	local xa = _j_int(((x1f < 0.0) and 0.0 or (x1f + 0.999)))
	local xb = _j_int(((x2f >= VW) and (VW - 1) or x2f))
	local cliptop = CLIPTOP
	local clipbot = CLIPBOT
	local done = COLDONE
	local clipsn = CLIPS_N
	local clipsd = CLIPS_D
	local clipst = CLIPS_T
	local clipsb = CLIPS_B
	local cslot = SEC_CFLAT[fsec + 1]
	local fslot = SEC_FFLAT[fsec + 1]
	local cflat = FLATS[cslot + 1]
	local fflat = FLATS[fslot + 1]
	local flat0 = FLATS[0 + 1]
	local ftp = TEX_PIX[((facet >= 0) and facet or 0) + 1]
	local ftw = ((facet >= 0) and TEX_W[facet + 1] or 64)
	local fth = ((facet >= 0) and TEX_H[facet + 1] or 64)
	local ttp = TEX_PIX[((topt >= 0) and topt or 0) + 1]
	local ttw = ((topt >= 0) and TEX_W[topt + 1] or 64)
	local tth = ((topt >= 0) and TEX_H[topt + 1] or 64)
	local btp = TEX_PIX[((bott >= 0) and bott or 0) + 1]
	local btw = ((bott >= 0) and TEX_W[bott + 1] or 64)
	local bth = ((bott >= 0) and TEX_H[bott + 1] or 64)
	for x = xa, xb, 1 do
		do
			if (done[x + 1] ~= 0) then
				goto _jd_continue_10
			end
			local t3 = ((x - x1f) / spanx)
			local invz = (invz1 + ((invz2 - invz1) * t3))
			local sc = (invz * FOCALY)
			local dist = (1.0 / invz)
			local u = ((uz1 + ((uz2 - uz1) * t3)) * dist)
			local ucol = (_j_int(u) + xofs)
			local band = light_band(light, dist)
			local dvfx = _j_int((65536.0 / sc))
			local yc = _j_int((CENTERY - (ctop * sc)))
			local yf = _j_int((CENTERY - (cbot * sc)))
			local ct = cliptop[x + 1]
			local cb = clipbot[x + 1]
			local xfrac = ((x - CENTERX) / FOCAL)
			local raydx = (VCOS + (VSIN * xfrac))
			local raydy = (VSIN - (VCOS * xfrac))
			local c1 = (yc - 1)
			if (c1 > (cb - 1)) then
				c1 = (cb - 1)
			end
			if ((ct + 1) <= c1) then
				if fsky then
					draw_sky_col(x, (ct + 1), c1)
				else
					draw_flat_col(x, (ct + 1), c1, cflat, fceil, raydx, raydy, light)
				end
			end
			local f0 = (yf + 1)
			if (f0 < (ct + 1)) then
				f0 = (ct + 1)
			end
			if (f0 <= (cb - 1)) then
				draw_flat_col(x, f0, (cb - 1), fflat, ffloor, raydx, raydy, light)
			end
			if solid then
				local w0 = yc
				if (w0 < (ct + 1)) then
					w0 = (ct + 1)
				end
				local w1 = yf
				if (w1 > (cb - 1)) then
					w1 = (cb - 1)
				end
				if (w0 <= w1) then
					if (facet >= 0) then
						local vfx = _j_int(((((aface - VIEW_Z) + yofs) - ((CENTERY - w0) / sc)) * 65536.0))
						draw_tex_col(x, w0, w1, ftp, ftw, fth, ucol, vfx, dvfx, band)
					else
						draw_tex_col(x, w0, w1, flat0, 64, 64, ucol, 0, dvfx, band)
					end
				end
				done[x + 1] = 1
				REMAIN = (REMAIN - 1)
				WALLZ[x + 1] = dist
				goto _jd_continue_10
			end
			local newct = ct
			if (bceil < fceil) then
				local ybc = _j_int((CENTERY - ((bceil - VIEW_Z) * sc)))
				local u0 = yc
				if (u0 < (ct + 1)) then
					u0 = (ct + 1)
				end
				local u1i = (ybc - 1)
				if (u1i > (cb - 1)) then
					u1i = (cb - 1)
				end
				if (u0 <= u1i) then
					if (fsky and bsky) then
						draw_sky_col(x, u0, u1i)
					else
						if (topt >= 0) then
							local vfxu = _j_int(((((atop - VIEW_Z) + yofs) - ((CENTERY - u0) / sc)) * 65536.0))
							draw_tex_col(x, u0, u1i, ttp, ttw, tth, ucol, vfxu, dvfx, band)
						end
					end
				end
				if ((ybc - 1) > newct) then
					newct = (ybc - 1)
				end
			else
				if ((yc - 1) > newct) then
					newct = (yc - 1)
				end
			end
			local newcb = cb
			if (bfloor > ffloor) then
				local ybf = _j_int((CENTERY - ((bfloor - VIEW_Z) * sc)))
				local l0 = ybf
				if (l0 < (newct + 1)) then
					l0 = (newct + 1)
				end
				local l1 = yf
				if (l1 > (cb - 1)) then
					l1 = (cb - 1)
				end
				if ((l0 <= l1) and (bott >= 0)) then
					local vfxl = _j_int(((((abot - VIEW_Z) + yofs) - ((CENTERY - l0) / sc)) * 65536.0))
					draw_tex_col(x, l0, l1, btp, btw, bth, ucol, vfxl, dvfx, band)
				end
				if (ybf < newcb) then
					newcb = ybf
				end
			else
				if ((yf + 1) < newcb) then
					newcb = (yf + 1)
				end
			end
			if ((newct ~= ct) or (newcb ~= cb)) then
				local cn = clipsn[x + 1]
				if (cn < 4) then
					local ci = ((x * 4) + cn)
					clipsd[ci + 1] = dist
					clipst[ci + 1] = newct
					clipsb[ci + 1] = newcb
					clipsn[x + 1] = (cn + 1)
				else
					local ci2 = ((x * 4) + 3)
					clipst[ci2 + 1] = newct
					clipsb[ci2 + 1] = newcb
				end
			end
			cliptop[x + 1] = newct
			clipbot[x + 1] = newcb
			if ((newct + 1) >= newcb) then
				done[x + 1] = 1
				REMAIN = (REMAIN - 1)
				WALLZ[x + 1] = dist
			end
		end
		::_jd_continue_10::
	end
end
function bsp_subsector(ss)
	local first = SS_FIRST[ss + 1]
	local stop = (first + SS_COUNT[ss + 1])
	for i = first, (stop - 1), 1 do
		do
			draw_seg(i)
		end
		::_jd_continue_11::
	end
end
function bsp_node(n)
	if (REMAIN <= 0) then
		return
	end
	if ((n & 32768) ~= 0) then
		bsp_subsector((n & 32767))
		return
	end
	local side = point_side(VIEW_X, VIEW_Y, n)
	if (side == 0) then
		bsp_node(ND_RIGHT[n + 1])
		if (REMAIN > 0) then
			bsp_node(ND_LEFT[n + 1])
		end
	else
		bsp_node(ND_LEFT[n + 1])
		if (REMAIN > 0) then
			bsp_node(ND_RIGHT[n + 1])
		end
	end
end
function render_view(px, py, ang)
	VIEW_X = px
	VIEW_Y = py
	VIEW_ANG = ang
	if (EYE_Z > -99999.0) then
		VIEW_Z = EYE_Z
	else
		VIEW_Z = (SE_FLOORH[sector_at(px, py) + 1] + 41.0)
	end
	VSIN = math.sin(ang)
	VCOS = math.cos(ang)
	CENTERX = _j_idiv(VW, 2)
	CENTERY = _j_idiv(VIEWH, 2)
	FOCAL = (1.0 * CENTERX)
	FOCALY = (FOCAL * PXASPECT)
	pix_clear(0)
	if ((#CLIPTOP) ~= VW) then
		CLIPTOP = _j_array({})
		CLIPBOT = _j_array({})
		COLDONE = _j_array({})
		WALLZ = _j_array({})
		do
			local i = 0
			while (i < VW) do
				do
					table.insert(CLIPTOP, -1)
					table.insert(CLIPBOT, VIEWH)
					table.insert(COLDONE, 0)
					table.insert(WALLZ, 1000000000.0)
				end
				::_jd_continue_12::
				i = i + 1
			end
		end
	else
		local rct = CLIPTOP
		local rcb = CLIPBOT
		local rcd = COLDONE
		local rwz = WALLZ
		do
			local i = 0
			while (i < VW) do
				do
					rct[i + 1] = -1
					rcb[i + 1] = VIEWH
					rcd[i + 1] = 0
					rwz[i + 1] = 1000000000.0
				end
				::_jd_continue_13::
				i = i + 1
			end
		end
	end
	if ((#CLIPS_N) ~= VW) then
		CLIPS_N = _j_array({})
		CLIPS_D = _j_array({})
		CLIPS_T = _j_array({})
		CLIPS_B = _j_array({})
		local total = (VW * 4)
		do
			local i2 = 0
			while (i2 < VW) do
				do
					table.insert(CLIPS_N, 0)
				end
				::_jd_continue_14::
				i2 = i2 + 1
			end
		end
		for i3 = 0, (total - 1), 1 do
			do
				table.insert(CLIPS_D, 0.0)
				table.insert(CLIPS_T, 0)
				table.insert(CLIPS_B, 0)
			end
			::_jd_continue_15::
		end
	else
		do
			local i4 = 0
			while (i4 < VW) do
				do
					CLIPS_N[i4 + 1] = 0
				end
				::_jd_continue_16::
				i4 = i4 + 1
			end
		end
	end
	MSEG = _j_array({})
	MSEG_D = _j_array({})
	MASKED_DRAWN = 0
	FUZZ_T = (1 - FUZZ_T)
	REMAIN = VW
	bsp_node(((#ND_X) - 1))
	draw_things()
end
SPRITES_DRAWN = 0
SPR_DBG_M = -1
SPR_DBG_YMAX = -1
function draw_things()
	SPRITES_DRAWN = 0
	local order = _j_array({})
	local depth = _j_array({})
	local n = (#MO_X)
	for m = 0, (n - 1), 1 do
		do
			if (MO_STATE[m + 1] == 0) then
				goto _jd_continue_17
			end
			local dx = (MO_X[m + 1] - VIEW_X)
			local dy = (MO_Y[m + 1] - VIEW_Y)
			local dep = ((dx * VCOS) + (dy * VSIN))
			if (dep < 4.0) then
				goto _jd_continue_17
			end
			local j = ((#order) - 1)
			table.insert(order, 0)
			table.insert(depth, 0.0)
			do
				while ((j >= 0) and (depth[j + 1] < dep)) do
					do
						order[(j + 1) + 1] = order[j + 1]
						depth[(j + 1) + 1] = depth[j + 1]
						j = (j - 1)
					end
					::_jd_continue_18::
				end
			end
			order[(j + 1) + 1] = m
			depth[(j + 1) + 1] = dep
		end
		::_jd_continue_17::
	end
	local nm = (#MSEG)
	for k = 0, (nm - 1), 1 do
		do
			local mdep = MSEG_D[k + 1]
			local j2 = ((#order) - 1)
			table.insert(order, 0)
			table.insert(depth, 0.0)
			do
				while ((j2 >= 0) and (depth[j2 + 1] < mdep)) do
					do
						order[(j2 + 1) + 1] = order[j2 + 1]
						depth[(j2 + 1) + 1] = depth[j2 + 1]
						j2 = (j2 - 1)
					end
					::_jd_continue_20::
				end
			end
			order[(j2 + 1) + 1] = (0 - (k + 1))
			depth[(j2 + 1) + 1] = mdep
		end
		::_jd_continue_19::
	end
	do
		local k2 = 0
		while (k2 < (#order)) do
			do
				local e = _j_int(order[k2 + 1])
				if (e >= 0) then
					draw_sprite(e)
				else
					draw_masked(MSEG[((0 - e) - 1) + 1])
				end
			end
			::_jd_continue_21::
			k2 = k2 + 1
		end
	end
end
function draw_masked(sg)
	local x1w = (VX_X[SG_V1[sg + 1] + 1] + 0.0)
	local y1w = (VX_Y[SG_V1[sg + 1] + 1] + 0.0)
	local x2w = (VX_X[SG_V2[sg + 1] + 1] + 0.0)
	local y2w = (VX_Y[SG_V2[sg + 1] + 1] + 0.0)
	local dx1 = (x1w - VIEW_X)
	local dy1 = (y1w - VIEW_Y)
	local dx2 = (x2w - VIEW_X)
	local dy2 = (y2w - VIEW_Y)
	local dep1 = ((dx1 * VCOS) + (dy1 * VSIN))
	local lat1 = ((dx1 * VSIN) - (dy1 * VCOS))
	local dep2 = ((dx2 * VCOS) + (dy2 * VSIN))
	local lat2 = ((dx2 * VSIN) - (dy2 * VCOS))
	local seglen = SEG_LEN[sg + 1]
	local u1 = 0.0
	local u2 = seglen
	local neareps = 0.05
	if ((dep1 < neareps) and (dep2 < neareps)) then
		return
	end
	if (dep1 < neareps) then
		local t = ((neareps - dep1) / (dep2 - dep1))
		dep1 = neareps
		lat1 = (lat1 + ((lat2 - lat1) * t))
		u1 = (t * seglen)
	else
		if (dep2 < neareps) then
			local t2 = ((neareps - dep2) / (dep1 - dep2))
			dep2 = neareps
			lat2 = (lat2 + ((lat1 - lat2) * t2))
			u2 = (seglen * (1.0 - t2))
		end
	end
	local x1f = (CENTERX + ((lat1 / dep1) * FOCAL))
	local x2f = (CENTERX + ((lat2 / dep2) * FOCAL))
	if (x2f <= x1f) then
		return
	end
	if ((x1f >= VW) or (x2f < 0.0)) then
		return
	end
	local fsec = SEG_FSEC[sg + 1]
	local bsec = SEG_BSEC[sg + 1]
	local midt = SEG_MIDT[sg + 1]
	tex_masked(midt)
	local mp = MTEX_PIX[midt]
	local tw = TEX_W[midt + 1]
	local th = TEX_H[midt + 1]
	local opentop = (((SE_CEILH[fsec + 1] < SE_CEILH[bsec + 1]) and SE_CEILH[fsec + 1] or SE_CEILH[bsec + 1]) + 0.0)
	local openbot = (((SE_FLOORH[fsec + 1] > SE_FLOORH[bsec + 1]) and SE_FLOORH[fsec + 1] or SE_FLOORH[bsec + 1]) + 0.0)
	if (opentop <= openbot) then
		return
	end
	local light = SE_LIGHT[fsec + 1]
	local aface = ((SEG_PEGB[sg + 1] ~= 0) and (openbot + th) or opentop)
	local xofs = SEG_XOFS[sg + 1]
	local yofs = SEG_YOFS[sg + 1]
	local invz1 = (1.0 / dep1)
	local invz2 = (1.0 / dep2)
	local uz1 = (u1 * invz1)
	local uz2 = (u2 * invz2)
	local spanx = (x2f - x1f)
	local xa = _j_int(((x1f < 0.0) and 0.0 or (x1f + 0.999)))
	local xb = _j_int(((x2f >= VW) and (VW - 1) or x2f))
	if (xb >= VW) then
		xb = (VW - 1)
	end
	local pix = PIX
	local palcm = PALCM
	local wallz = WALLZ
	local clipsn = CLIPS_N
	local clipsd = CLIPS_D
	local clipst = CLIPS_T
	local clipsb = CLIPS_B
	for x = xa, xb, 1 do
		do
			local t3 = ((x - x1f) / spanx)
			local invz = (invz1 + ((invz2 - invz1) * t3))
			local dist = (1.0 / invz)
			if (dist >= wallz[x + 1]) then
				goto _jd_continue_22
			end
			local sc = (invz * FOCALY)
			local y0 = _j_int((CENTERY - ((opentop - VIEW_Z) * sc)))
			local y1 = _j_int((CENTERY - ((openbot - VIEW_Z) * sc)))
			local cn = clipsn[x + 1]
			if (cn > 0) then
				local cbase = (x * 4)
				local wi = -1
				for ci = 0, (cn - 1), 1 do
					do
						if (clipsd[(cbase + ci) + 1] >= (dist - 0.01)) then
							break
						end
						wi = (cbase + ci)
					end
					::_jd_continue_23::
				end
				if (wi >= 0) then
					if (y0 < (clipst[wi + 1] + 1)) then
						y0 = (clipst[wi + 1] + 1)
					end
					if (y1 > (clipsb[wi + 1] - 1)) then
						y1 = (clipsb[wi + 1] - 1)
					end
				end
			end
			if (y0 < 0) then
				y0 = 0
			end
			if (y1 >= VIEWH) then
				y1 = (VIEWH - 1)
			end
			if (y0 > y1) then
				goto _jd_continue_22
			end
			local u = ((uz1 + ((uz2 - uz1) * t3)) * dist)
			local tc = math.fmod((_j_int(u) + xofs), tw)
			if (tc < 0) then
				tc = (tc + tw)
			end
			local base = (tc * th)
			local band = light_band(light, dist)
			local pcm = (band * 256)
			local vfx = _j_int(((((aface - VIEW_Z) + yofs) - ((CENTERY - y0) / sc)) * 65536.0))
			local dvfx = _j_int((65536.0 / sc))
			local painted = false
			for y = y0, y1, 1 do
				do
					local iv = (vfx // 65536)
					vfx = (vfx + dvfx)
					if ((iv < 0) or (iv >= th)) then
						goto _jd_continue_24
					end
					local texel = mp[(base + iv) + 1]
					if (texel >= 0) then
						pix[((y * VW) + x) + 1] = palcm[(pcm + texel) + 1]
						painted = true
					end
				end
				::_jd_continue_24::
			end
			if painted then
				MASKED_DRAWN = (MASKED_DRAWN + 1)
			end
		end
		::_jd_continue_22::
	end
end
function draw_sprite(m)
	local dx = (MO_X[m + 1] - VIEW_X)
	local dy = (MO_Y[m + 1] - VIEW_Y)
	local dep = ((dx * VCOS) + (dy * VSIN))
	local lat = ((dx * VSIN) - (dy * VCOS))
	local xc = (CENTERX + ((lat / dep) * FOCAL))
	local fi = MO_FRAME[m + 1]
	if (fi >= (#MO_ANIM[m + 1])) then
		fi = ((#MO_ANIM[m + 1]) - 1)
	end
	local frame = _j_substr(MO_ANIM[m + 1], fi, 1)
	local rel = (math.atan((VIEW_Y - MO_Y[m + 1]), (VIEW_X - MO_X[m + 1])) - MO_ANG[m + 1])
	do
		while (rel < 0.0) do
			do
				rel = (rel + TAU)
			end
			::_jd_continue_25::
		end
	end
	local rot = (_j_int(((rel + (PI / 8.0)) / (PI / 4.0))) & 7)
	local lump = sprite_lookup(MO_SPR[m + 1], frame, rot)
	if (lump < 0) then
		return
	end
	SPRITES_DRAWN = (SPRITES_DRAWN + 1)
	local flip = SPR_LAST_FLIP
	sprite_prepare(lump)
	local w = SPR_W[lump]
	local h = SPR_H[lump]
	local xscale = (FOCAL / dep)
	local yscale = (FOCALY / dep)
	local x0f = (xc - (SPR_LO[lump] * xscale))
	local topz = (MO_Z[m + 1] + SPR_TO[lump])
	local y0f = (CENTERY - ((topz - VIEW_Z) * yscale))
	local dv = (dep / FOCALY)
	local xa = _j_int(((x0f < 0.0) and 0.0 or (x0f + 0.999)))
	local xb = _j_int((x0f + (w * xscale)))
	if (xb >= VW) then
		xb = (VW - 1)
	end
	if (((xa > xb) or (xa >= VW)) or (xb < 0)) then
		return
	end
	local st = MO_STATE[m + 1]
	local band = 0
	if ((((st ~= 8) and (MO_SPR[m + 1] ~= "BEXP")) and (MO_SPR[m + 1] ~= "BAL1")) and (MO_SPR[m + 1] ~= "MISL")) then
		band = light_band(SE_LIGHT[sector_at(MO_X[m + 1], MO_Y[m + 1]) + 1], dep)
	end
	local pcm = (band * 256)
	local fuzz = (MO_TYPE[m + 1] == 58)
	local tp = SPR_PIX[lump]
	local pix = PIX
	local palcm = PALCM
	local wallz = WALLZ
	local clipsn = CLIPS_N
	local clipsd = CLIPS_D
	local clipst = CLIPS_T
	local clipsb = CLIPS_B
	for sx = xa, xb, 1 do
		do
			if (dep >= wallz[sx + 1]) then
				goto _jd_continue_26
			end
			local tc = _j_int(((sx - x0f) / xscale))
			if (tc < 0) then
				tc = 0
			end
			if (tc >= w) then
				tc = (w - 1)
			end
			if (flip ~= 0) then
				tc = ((w - 1) - tc)
			end
			local base = (tc * h)
			local y0 = _j_int(((y0f < 0.0) and 0.0 or (y0f + 0.999)))
			local y1 = _j_int((y0f + (h * yscale)))
			local cn = clipsn[sx + 1]
			if (cn > 0) then
				local cbase = (sx * 4)
				local wi = -1
				for ci = 0, (cn - 1), 1 do
					do
						if (clipsd[(cbase + ci) + 1] >= dep) then
							break
						end
						wi = (cbase + ci)
					end
					::_jd_continue_27::
				end
				if (wi >= 0) then
					if (y0 < (clipst[wi + 1] + 1)) then
						y0 = (clipst[wi + 1] + 1)
					end
					if (y1 > (clipsb[wi + 1] - 1)) then
						y1 = (clipsb[wi + 1] - 1)
					end
				end
			end
			if (y1 >= VIEWH) then
				y1 = (VIEWH - 1)
			end
			if ((m == SPR_DBG_M) and (y1 > SPR_DBG_YMAX)) then
				SPR_DBG_YMAX = y1
			end
			if fuzz then
				for fy = y0, y1, 1 do
					do
						local fiv = _j_int(((fy - y0f) * dv))
						if ((fiv < 0) or (fiv >= h)) then
							goto _jd_continue_28
						end
						if (tp[(base + fiv) + 1] < 0) then
							goto _jd_continue_28
						end
						if ((((sx + fy) + FUZZ_T) & 1) ~= 0) then
							goto _jd_continue_28
						end
						local fpi = ((fy * VW) + sx)
						pix[fpi + 1] = fuzz_dark(pix[fpi + 1])
					end
					::_jd_continue_28::
				end
				goto _jd_continue_26
			end
			for y = y0, y1, 1 do
				do
					local iv = _j_int(((y - y0f) * dv))
					if ((iv < 0) or (iv >= h)) then
						goto _jd_continue_29
					end
					local texel = tp[(base + iv) + 1]
					if (texel >= 0) then
						pix[((y * VW) + sx) + 1] = palcm[(pcm + texel) + 1]
					end
				end
				::_jd_continue_29::
			end
		end
		::_jd_continue_26::
	end
end
DARK_MAP = {}
FUZZ_T = 0
function fuzz_dark(c)
	if (DARK_MAP[c] ~= nil) then
		return DARK_MAP[c]
	end
	local d = rgb_idx(_j_idiv(PAL_R[c + 1], 2), _j_idiv(PAL_G[c + 1], 2), _j_idiv(PAL_B[c + 1], 2))
	DARK_MAP[c] = d
	return d
end
PSP_X0 = 0.0
PSP_Y0 = 0.0
function draw_psprite(lump, x0f, y0f, scale)
	local w = SPR_W[lump]
	local h = SPR_H[lump]
	local tp = SPR_PIX[lump]
	local pix = PIX
	local palcm = PALCM
	local xa = _j_int(((x0f < 0.0) and 0.0 or x0f))
	local xb = _j_int((x0f + (w * scale)))
	if (xb >= VW) then
		xb = (VW - 1)
	end
	for sx = xa, xb, 1 do
		do
			local tc = _j_int(((sx - x0f) / scale))
			if ((tc < 0) or (tc >= w)) then
				goto _jd_continue_30
			end
			local base = (tc * h)
			local ya = _j_int(((y0f < 0.0) and 0.0 or y0f))
			do
				local y = ya
				while (y < VIEWH) do
					do
						local iv = _j_int(((y - y0f) / scale))
						if ((iv < 0) or (iv >= h)) then
							goto _jd_continue_31
						end
						local texel = tp[(base + iv) + 1]
						if (texel >= 0) then
							pix[((y * VW) + sx) + 1] = palcm[texel + 1]
						end
					end
					::_jd_continue_31::
					y = y + 1
				end
			end
		end
		::_jd_continue_30::
	end
end
function psprite_place(lump, scale)
	local texmid = (68.5 + SPR_TO[lump])
	PSP_X0 = (((VW / 2.0) + ((-159.0 - SPR_LO[lump]) * scale)) + (WBOB_X * scale))
	PSP_Y0 = (((VIEWH / 2.0) - (texmid * scale)) + (WBOB_Y * scale))
end
function draw_weapon(spr4, frame)
	local lump = sprite_lookup(spr4, frame, 8)
	if (lump < 0) then
		return
	end
	sprite_prepare(lump)
	local scale = (VW / 320.0)
	psprite_place(lump, scale)
	draw_psprite(lump, PSP_X0, PSP_Y0, scale)
end
function draw_weapon_flash(spr4, frame)
	local lump = sprite_lookup(spr4, frame, 8)
	if (lump < 0) then
		return
	end
	sprite_prepare(lump)
	local scale = (VW / 320.0)
	psprite_place(lump, scale)
	draw_psprite(lump, PSP_X0, PSP_Y0, scale)
end
