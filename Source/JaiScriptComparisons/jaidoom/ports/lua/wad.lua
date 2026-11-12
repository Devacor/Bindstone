-- Generated from wad.jai by tools/transpile.py; see PORTING.md.
WAD = ""
WAD_ID = ""
WAD_BASE = ""
WAD_FILES = _j_array({})
NUM_LUMPS = 0
LUMP_POS = _j_array({})
LUMP_SIZE = _j_array({})
LUMP_NAME = _j_array({})
LUMP_INDEX = {}
function u8(ofs)
	return (string.byte(WAD, ofs + 1) + 0)
end
function u16(ofs)
	return ((string.byte(WAD, ofs + 1) + 0) | (string.byte(WAD, (ofs + 1) + 1) << (8 & 63)))
end
function i16(ofs)
	local v = ((string.byte(WAD, ofs + 1) + 0) | (string.byte(WAD, (ofs + 1) + 1) << (8 & 63)))
	return ((v >= 32768) and (v - 65536) or v)
end
function u32(ofs)
	return ((((string.byte(WAD, ofs + 1) + 0) | (string.byte(WAD, (ofs + 1) + 1) << (8 & 63))) | (string.byte(WAD, (ofs + 2) + 1) << (16 & 63))) | (string.byte(WAD, (ofs + 3) + 1) << (24 & 63)))
end
function i32(ofs)
	local v = u32(ofs)
	return ((v >= 2147483648) and (v - 4294967296) or v)
end
function name8(ofs)
	local len = 0
	do
		while ((len < 8) and (string.byte(WAD, (ofs + len) + 1) ~= 0)) do
			do
				len = (len + 1)
			end
			::_jd_continue_1::
		end
	end
	return _j_substr(WAD, ofs, len)
end
GAME_ROOT = ""
DATA_ROOT = ""
function game_root()
	if (GAME_ROOT == "") then
		local roots = _j_array({parent_dir(root_script_dir()), root_script_dir(), bin_dir(), parent_dir(bin_dir())})
		GAME_ROOT = roots[0 + 1]
		do
			local _jd_range_2 = roots
			local _jd_index_2 = 1
			while _jd_index_2 <= #_jd_range_2 do
				do
					local r = _jd_range_2[_jd_index_2]
					if dir_exists((r .. "/wads")) then
						GAME_ROOT = r
						break
					end
				end
				::_jd_continue_2::
				_jd_index_2 = _jd_index_2 + 1
			end
		end
	end
	return GAME_ROOT
end
function data_root()
	if (DATA_ROOT == "") then
		DATA_ROOT = (writable_dir((game_root() .. "/wads")) and game_root() or app_data_dir("JaiDOOM"))
	end
	return DATA_ROOT
end
function wad_dir()
	return (data_root() .. "/wads/")
end
function wad_path(name)
	local file = (_j_ends_with(name, ".wad") and name or (name .. ".wad"))
	if file_exists(file) then
		return file
	end
	local base = file_name(file)
	if file_exists(((game_root() .. "/wads/") .. base)) then
		return ((game_root() .. "/wads/") .. base)
	end
	return (wad_dir() .. base)
end
function data_path(name)
	return ((file_name(name) == name) and ((data_root() .. "/") .. name) or name)
end
function audio_cache_dir()
	return (((data_root() .. "/cache/") .. WAD_BASE) .. "/")
end
function wad_add(path)
	local more = read_file(path)
	if ((#more) < 12) then
		return false
	end
	local id = _j_substr(more, 0, 4)
	if ((id ~= "IWAD") and (id ~= "PWAD")) then
		return false
	end
	local base = (#WAD)
	WAD = (WAD .. more)
	local count = i32((base + 4))
	local dir = (base + i32((base + 8)))
	for i = 0, (count - 1), 1 do
		do
			local e = (dir + (i * 16))
			table.insert(LUMP_POS, (base + i32(e)))
			table.insert(LUMP_SIZE, i32((e + 4)))
			local nm = name8((e + 8))
			table.insert(LUMP_NAME, nm)
			LUMP_INDEX[nm] = (NUM_LUMPS + i)
		end
		::_jd_continue_3::
	end
	NUM_LUMPS = (NUM_LUMPS + count)
	table.insert(WAD_FILES, path)
	return true
end
function map_list()
	local names = _j_array({})
	local seen = {}
	for i = (NUM_LUMPS - 1), 0, -1 do
		do
			local nm = LUMP_NAME[i + 1]
			local episode = ((((((((#nm) == 4) and (string.byte(nm, 0 + 1) == 69)) and (string.byte(nm, 2 + 1) == 77)) and (string.byte(nm, 1 + 1) >= 49)) and (string.byte(nm, 1 + 1) <= 57)) and (string.byte(nm, 3 + 1) >= 49)) and (string.byte(nm, 3 + 1) <= 57))
			local level = (((((((#nm) == 5) and _j_starts_with(nm, "MAP")) and (string.byte(nm, 3 + 1) >= 48)) and (string.byte(nm, 3 + 1) <= 57)) and (string.byte(nm, 4 + 1) >= 48)) and (string.byte(nm, 4 + 1) <= 57))
			if ((not (episode or level)) or (seen[nm] ~= nil)) then
				goto _jd_continue_4
			end
			if (((i + 1) >= NUM_LUMPS) or (LUMP_NAME[(i + 1) + 1] ~= "THINGS")) then
				goto _jd_continue_4
			end
			seen[nm] = true
			table.insert(names, nm)
		end
		::_jd_continue_4::
	end
	table.sort(names)
	return names
end
function wad_load(path)
	WAD = read_file(path)
	if ((#WAD) < 12) then
		return false
	end
	WAD_FILES = _j_array({path})
	local slash = _j_find_last_of(path, "/\\")
	local file = ((slash >= 0) and _j_substr(path, (slash + 1)) or path)
	local dot = _j_rfind(file, ".")
	WAD_BASE = ((dot > 0) and _j_substr(file, 0, dot) or file)
	WAD_ID = _j_substr(WAD, 0, 4)
	if ((WAD_ID ~= "IWAD") and (WAD_ID ~= "PWAD")) then
		return false
	end
	NUM_LUMPS = i32(4)
	local dir = i32(8)
	LUMP_POS = _j_array({})
	LUMP_SIZE = _j_array({})
	LUMP_NAME = _j_array({})
	LUMP_INDEX = {}
	do
		local i = 0
		while (i < NUM_LUMPS) do
			do
				local e = (dir + (i * 16))
				table.insert(LUMP_POS, i32(e))
				table.insert(LUMP_SIZE, i32((e + 4)))
				local nm = name8((e + 8))
				table.insert(LUMP_NAME, nm)
				LUMP_INDEX[nm] = i
			end
			::_jd_continue_5::
			i = i + 1
		end
	end
	return true
end
function lump_num(name)
	if (LUMP_INDEX[name] ~= nil) then
		return LUMP_INDEX[name]
	end
	return -1
end
function lump_num_from(name, start)
	do
		local i = start
		while ((i < NUM_LUMPS) and (i < (start + 12))) do
			do
				if (LUMP_NAME[i + 1] == name) then
					return i
				end
			end
			::_jd_continue_6::
			i = i + 1
		end
	end
	return -1
end
function lump_bytes(idx)
	local out = _j_array({})
	local pos = LUMP_POS[idx + 1]
	local stop = (pos + LUMP_SIZE[idx + 1])
	local w = WAD
	for i = pos, (stop - 1), 1 do
		do
			table.insert(out, (string.byte(w, i + 1) + 0))
		end
		::_jd_continue_7::
	end
	return out
end
