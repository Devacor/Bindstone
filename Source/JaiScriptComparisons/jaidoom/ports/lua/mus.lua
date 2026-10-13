-- Generated from mus.jai by tools/transpile.py; see internal/examples/jaidoom/ports/PORTING.md.
require("wad")
require("sound")
function be16(v)
	return (("" .. string.char((((v // 256) & 255) & 255))) .. string.char(((v & 255) & 255)))
end
function be32(v)
	return (((("" .. string.char((((v // 16777216) & 255) & 255))) .. string.char((((v // 65536) & 255) & 255))) .. string.char((((v // 256) & 255) & 255))) .. string.char(((v & 255) & 255)))
end
function midi_varlen(v)
	if (v < 0) then
		v = 0
	end
	local out = ("" .. string.char(((v & 127) & 255)))
	v = (v // 128)
	do
		while (v > 0) do
			do
				out = (string.char(((128 | (v & 127)) & 255)) .. out)
				v = (v // 128)
			end
			::_jd_continue_1::
		end
	end
	return out
end
function mus_cc(cn)
	if (cn == 1) then
		return 0
	end
	if (cn == 2) then
		return 1
	end
	if (cn == 3) then
		return 7
	end
	if (cn == 4) then
		return 10
	end
	if (cn == 5) then
		return 11
	end
	if (cn == 6) then
		return 91
	end
	if (cn == 7) then
		return 93
	end
	if (cn == 8) then
		return 64
	end
	if (cn == 9) then
		return 67
	end
	return 7
end
function mus_midi_bytes(name)
	local lu = lump_num(name)
	if (lu < 0) then
		return ""
	end
	local pos = LUMP_POS[lu + 1]
	if (((LUMP_SIZE[lu + 1] < 16) or (_j_substr(WAD, pos, 3) ~= "MUS")) or (string.byte(WAD, (pos + 3) + 1) ~= 26)) then
		return ""
	end
	local slen = u16((pos + 4))
	local p = (pos + u16((pos + 6)))
	local stop = (p + slen)
	local lend = (pos + LUMP_SIZE[lu + 1])
	if (stop > lend) then
		stop = lend
	end
	local trk = ""
	local chvol = _j_array({})
	for c = 0, (16 - 1), 1 do
		do
			table.insert(chvol, 100)
		end
		::_jd_continue_2::
	end
	local delay = 0
	local done = false
	do
		while ((p < stop) and (not done)) do
			do
				local ev = u8(p)
				p = (p + 1)
				local last = (ev & 128)
				local etype = ((ev // 16) & 7)
				local mch = (ev & 15)
				local ch = ((mch == 15) and 9 or ((mch >= 9) and (mch + 1) or mch))
				local out = ""
				if (etype == 0) then
					local note = (u8(p) & 127)
					p = (p + 1)
					out = ((("" .. string.char(((128 | ch) & 255))) .. string.char((note & 255))) .. string.char((64 & 255)))
				else
					if (etype == 1) then
						local nb = u8(p)
						p = (p + 1)
						if ((nb & 128) ~= 0) then
							chvol[ch + 1] = (u8(p) & 127)
							p = (p + 1)
						end
						out = ((("" .. string.char(((144 | ch) & 255))) .. string.char(((nb & 127) & 255))) .. string.char((chvol[ch + 1] & 255)))
					else
						if (etype == 2) then
							local w = (u8(p) * 64)
							p = (p + 1)
							out = ((("" .. string.char(((224 | ch) & 255))) .. string.char(((w & 127) & 255))) .. string.char((((w // 128) & 127) & 255)))
						else
							if (etype == 3) then
								local sy = (u8(p) & 127)
								p = (p + 1)
								local cc = 120
								if (sy == 11) then
									cc = 123
								else
									if (sy == 12) then
										cc = 126
									else
										if (sy == 13) then
											cc = 127
										else
											if (sy == 14) then
												cc = 121
											end
										end
									end
								end
								out = ((("" .. string.char(((176 | ch) & 255))) .. string.char((cc & 255))) .. string.char((0 & 255)))
							else
								if (etype == 4) then
									local cn = (u8(p) & 127)
									p = (p + 1)
									local cv = (u8(p) & 127)
									p = (p + 1)
									if (cn == 0) then
										out = (("" .. string.char(((192 | ch) & 255))) .. string.char((cv & 255)))
									else
										out = ((("" .. string.char(((176 | ch) & 255))) .. string.char((mus_cc(cn) & 255))) .. string.char((cv & 255)))
									end
								else
									if (etype == 6) then
										done = true
									else
										if (etype == 7) then
											p = (p + 1)
										end
									end
								end
							end
						end
					end
				end
				if (out ~= "") then
					trk = (trk .. (midi_varlen(delay) .. out))
					delay = 0
				end
				if (last ~= 0) then
					local d = 0
					do
						while (p < stop) do
							do
								local b = u8(p)
								p = (p + 1)
								d = ((d << (7 & 63)) | (b & 127))
								if ((b & 128) == 0) then
									break
								end
							end
							::_jd_continue_4::
						end
					end
					delay = (delay + d)
				end
			end
			::_jd_continue_3::
		end
	end
	trk = (trk .. (((midi_varlen(delay) .. string.char((255 & 255))) .. string.char((47 & 255))) .. string.char((0 & 255))))
	return ((((((("MThd" .. be32(6)) .. be16(0)) .. be16(1)) .. be16(70)) .. "MTrk") .. be32((#trk))) .. trk)
end
MAPMUS = _j_array({"D_RUNNIN", "D_STALKS", "D_COUNTD", "D_BETWEE", "D_DOOM", "D_THE_DA", "D_SHAWN", "D_DDTBLU", "D_IN_CIT", "D_DEAD"})
function map_music(mapname)
	if (not _j_starts_with(mapname, "MAP")) then
		return ("D_" .. mapname)
	end
	local n = _j_to_int(_j_substr(mapname, 3, 2), 1)
	return MAPMUS[math.fmod((n - 1), 10) + 1]
end
MUSIC_CUR = ""
MUSIC_READY = {}
function music_want(name)
	if ((not SND_ON) or (MUSIC_CUR == name)) then
		return
	end
	if (not (MUSIC_READY[name] ~= nil)) then
		local path = ((audio_cache_dir() .. string.lower(("" .. name))) .. ".mid")
		if file_exists(path) then
			MUSIC_READY[name] = path
		else
			local mid = mus_midi_bytes(name)
			if ((mid == "") or (not _j_truth(write_file(path, mid)))) then
				path = ""
			end
			MUSIC_READY[name] = path
		end
	end
	MUSIC_CUR = name
	local p = MUSIC_READY[name]
	if (p ~= "") then
		play_midi(p)
	else
		stop_midi()
	end
end
function music_stop()
	MUSIC_CUR = ""
	stop_midi()
end
function music_tick()
	if ((not SND_ON) or (MUSIC_CUR == "")) then
		return
	end
	if (midi_status() == "stopped") then
		local p = _j_get(MUSIC_READY, MUSIC_CUR, "")
		if (p ~= "") then
			play_midi(p)
		end
	end
end
