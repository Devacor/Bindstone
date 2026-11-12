-- Generated from map.jai by tools/transpile.py; see PORTING.md.
require("wad")
MAP_NAME = ""
VX_X = _j_array({})
VX_Y = _j_array({})
LD_V1 = _j_array({})
LD_V2 = _j_array({})
LD_FLAGS = _j_array({})
LD_SPECIAL = _j_array({})
LD_TAG = _j_array({})
LD_SIDE0 = _j_array({})
LD_SIDE1 = _j_array({})
SD_XOFS = _j_array({})
SD_YOFS = _j_array({})
SD_SEC = _j_array({})
SD_TOP = _j_array({})
SD_BOT = _j_array({})
SD_MID = _j_array({})
SE_FLOORH = _j_array({})
SE_CEILH = _j_array({})
SE_LIGHT = _j_array({})
SE_SPECIAL = _j_array({})
SE_TAG = _j_array({})
SE_FLOORPIC = _j_array({})
SE_CEILPIC = _j_array({})
SG_V1 = _j_array({})
SG_V2 = _j_array({})
SG_ANG = _j_array({})
SG_LINE = _j_array({})
SG_DIR = _j_array({})
SG_OFS = _j_array({})
SS_COUNT = _j_array({})
SS_FIRST = _j_array({})
ND_X = _j_array({})
ND_Y = _j_array({})
ND_DX = _j_array({})
ND_DY = _j_array({})
ND_BBOX = _j_array({})
ND_RIGHT = _j_array({})
ND_LEFT = _j_array({})
TH_X = _j_array({})
TH_Y = _j_array({})
TH_ANG = _j_array({})
TH_TYPE = _j_array({})
TH_FLAGS = _j_array({})
BMAP = _j_array({})
BMAP_ORGX = 0
BMAP_ORGY = 0
BMAP_W = 0
BMAP_H = 0
MAP_MINX = 0
MAP_MINY = 0
MAP_MAXX = 0
MAP_MAXY = 0
function side_or_none(v)
	return ((v == 65535) and -1 or v)
end
function map_load(mapname)
	local marker = lump_num(mapname)
	if (marker < 0) then
		return false
	end
	MAP_NAME = mapname
	local lu = lump_num_from("VERTEXES", marker)
	local pos = LUMP_POS[lu + 1]
	local count = _j_idiv(LUMP_SIZE[lu + 1], 4)
	VX_X = _j_array({})
	VX_Y = _j_array({})
	MAP_MINX = 100000
	MAP_MINY = 100000
	MAP_MAXX = -100000
	MAP_MAXY = -100000
	for i = 0, (count - 1), 1 do
		do
			local x = i16(pos)
			local y = i16((pos + 2))
			table.insert(VX_X, x)
			table.insert(VX_Y, y)
			if (x < MAP_MINX) then
				MAP_MINX = x
			end
			if (x > MAP_MAXX) then
				MAP_MAXX = x
			end
			if (y < MAP_MINY) then
				MAP_MINY = y
			end
			if (y > MAP_MAXY) then
				MAP_MAXY = y
			end
			pos = (pos + 4)
		end
		::_jd_continue_1::
	end
	lu = lump_num_from("LINEDEFS", marker)
	pos = LUMP_POS[lu + 1]
	count = _j_idiv(LUMP_SIZE[lu + 1], 14)
	LD_V1 = _j_array({})
	LD_V2 = _j_array({})
	LD_FLAGS = _j_array({})
	LD_SPECIAL = _j_array({})
	LD_TAG = _j_array({})
	LD_SIDE0 = _j_array({})
	LD_SIDE1 = _j_array({})
	for i2 = 0, (count - 1), 1 do
		do
			table.insert(LD_V1, u16(pos))
			table.insert(LD_V2, u16((pos + 2)))
			table.insert(LD_FLAGS, u16((pos + 4)))
			table.insert(LD_SPECIAL, u16((pos + 6)))
			table.insert(LD_TAG, u16((pos + 8)))
			table.insert(LD_SIDE0, side_or_none(u16((pos + 10))))
			table.insert(LD_SIDE1, side_or_none(u16((pos + 12))))
			pos = (pos + 14)
		end
		::_jd_continue_2::
	end
	lu = lump_num_from("SIDEDEFS", marker)
	pos = LUMP_POS[lu + 1]
	count = _j_idiv(LUMP_SIZE[lu + 1], 30)
	SD_XOFS = _j_array({})
	SD_YOFS = _j_array({})
	SD_SEC = _j_array({})
	SD_TOP = _j_array({})
	SD_BOT = _j_array({})
	SD_MID = _j_array({})
	for i3 = 0, (count - 1), 1 do
		do
			table.insert(SD_XOFS, i16(pos))
			table.insert(SD_YOFS, i16((pos + 2)))
			table.insert(SD_TOP, name8((pos + 4)))
			table.insert(SD_BOT, name8((pos + 12)))
			table.insert(SD_MID, name8((pos + 20)))
			table.insert(SD_SEC, u16((pos + 28)))
			pos = (pos + 30)
		end
		::_jd_continue_3::
	end
	lu = lump_num_from("SECTORS", marker)
	pos = LUMP_POS[lu + 1]
	count = _j_idiv(LUMP_SIZE[lu + 1], 26)
	SE_FLOORH = _j_array({})
	SE_CEILH = _j_array({})
	SE_LIGHT = _j_array({})
	SE_SPECIAL = _j_array({})
	SE_TAG = _j_array({})
	SE_FLOORPIC = _j_array({})
	SE_CEILPIC = _j_array({})
	for i4 = 0, (count - 1), 1 do
		do
			table.insert(SE_FLOORH, i16(pos))
			table.insert(SE_CEILH, i16((pos + 2)))
			table.insert(SE_FLOORPIC, name8((pos + 4)))
			table.insert(SE_CEILPIC, name8((pos + 12)))
			table.insert(SE_LIGHT, i16((pos + 20)))
			table.insert(SE_SPECIAL, u16((pos + 22)))
			table.insert(SE_TAG, u16((pos + 24)))
			pos = (pos + 26)
		end
		::_jd_continue_4::
	end
	lu = lump_num_from("SEGS", marker)
	pos = LUMP_POS[lu + 1]
	count = _j_idiv(LUMP_SIZE[lu + 1], 12)
	SG_V1 = _j_array({})
	SG_V2 = _j_array({})
	SG_ANG = _j_array({})
	SG_LINE = _j_array({})
	SG_DIR = _j_array({})
	SG_OFS = _j_array({})
	for i5 = 0, (count - 1), 1 do
		do
			table.insert(SG_V1, u16(pos))
			table.insert(SG_V2, u16((pos + 2)))
			table.insert(SG_ANG, u16((pos + 4)))
			table.insert(SG_LINE, u16((pos + 6)))
			table.insert(SG_DIR, u16((pos + 8)))
			table.insert(SG_OFS, i16((pos + 10)))
			pos = (pos + 12)
		end
		::_jd_continue_5::
	end
	lu = lump_num_from("SSECTORS", marker)
	pos = LUMP_POS[lu + 1]
	count = _j_idiv(LUMP_SIZE[lu + 1], 4)
	SS_COUNT = _j_array({})
	SS_FIRST = _j_array({})
	for i6 = 0, (count - 1), 1 do
		do
			table.insert(SS_COUNT, u16(pos))
			table.insert(SS_FIRST, u16((pos + 2)))
			pos = (pos + 4)
		end
		::_jd_continue_6::
	end
	lu = lump_num_from("NODES", marker)
	pos = LUMP_POS[lu + 1]
	count = _j_idiv(LUMP_SIZE[lu + 1], 28)
	ND_X = _j_array({})
	ND_Y = _j_array({})
	ND_DX = _j_array({})
	ND_DY = _j_array({})
	ND_BBOX = _j_array({})
	ND_RIGHT = _j_array({})
	ND_LEFT = _j_array({})
	for i7 = 0, (count - 1), 1 do
		do
			table.insert(ND_X, i16(pos))
			table.insert(ND_Y, i16((pos + 2)))
			table.insert(ND_DX, i16((pos + 4)))
			table.insert(ND_DY, i16((pos + 6)))
			for b = 0, (8 - 1), 1 do
				do
					table.insert(ND_BBOX, i16(((pos + 8) + (b * 2))))
				end
				::_jd_continue_8::
			end
			table.insert(ND_RIGHT, u16((pos + 24)))
			table.insert(ND_LEFT, u16((pos + 26)))
			pos = (pos + 28)
		end
		::_jd_continue_7::
	end
	lu = lump_num_from("THINGS", marker)
	pos = LUMP_POS[lu + 1]
	count = _j_idiv(LUMP_SIZE[lu + 1], 10)
	TH_X = _j_array({})
	TH_Y = _j_array({})
	TH_ANG = _j_array({})
	TH_TYPE = _j_array({})
	TH_FLAGS = _j_array({})
	for i8 = 0, (count - 1), 1 do
		do
			table.insert(TH_X, i16(pos))
			table.insert(TH_Y, i16((pos + 2)))
			table.insert(TH_ANG, i16((pos + 4)))
			table.insert(TH_TYPE, i16((pos + 6)))
			table.insert(TH_FLAGS, i16((pos + 8)))
			pos = (pos + 10)
		end
		::_jd_continue_9::
	end
	lu = lump_num_from("BLOCKMAP", marker)
	pos = LUMP_POS[lu + 1]
	count = _j_idiv(LUMP_SIZE[lu + 1], 2)
	BMAP = _j_array({})
	for i9 = 0, (count - 1), 1 do
		do
			table.insert(BMAP, i16((pos + (i9 * 2))))
		end
		::_jd_continue_10::
	end
	BMAP_ORGX = BMAP[0 + 1]
	BMAP_ORGY = BMAP[1 + 1]
	BMAP_W = (BMAP[2 + 1] & 65535)
	BMAP_H = (BMAP[3 + 1] & 65535)
	return true
end
