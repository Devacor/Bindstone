-- Generated from sound.jai by tools/transpile.py; see PORTING.md.
require("wad")
require("vid")
SND_ON = false
SND_READY = {}
function snd_wav_bytes(name)
	local lu = lump_num(name)
	if (lu < 0) then
		return ""
	end
	local pos = LUMP_POS[lu + 1]
	if ((LUMP_SIZE[lu + 1] < 10) or (u16(pos) ~= 3)) then
		return ""
	end
	local rate = u16((pos + 2))
	local count = i32((pos + 4))
	local ofs = 8
	if (count > 32) then
		ofs = 24
		count = (count - 32)
	end
	if (count > (LUMP_SIZE[lu + 1] - ofs)) then
		count = (LUMP_SIZE[lu + 1] - ofs)
	end
	if (count <= 0) then
		return ""
	end
	return (((((((((((("RIFF" .. le32((36 + count))) .. "WAVEfmt ") .. le32(16)) .. le16(1)) .. le16(1)) .. le32(rate)) .. le32(rate)) .. le16(1)) .. le16(8)) .. "data") .. le32(count)) .. _j_substr(WAD, (pos + ofs), count))
end
function snd_play(name)
	if (not SND_ON) then
		return
	end
	if (not (SND_READY[name] ~= nil)) then
		local path = ((audio_cache_dir() .. string.lower(("" .. name))) .. ".wav")
		if file_exists(path) then
			SND_READY[name] = path
		else
			local wav = snd_wav_bytes(name)
			if ((wav == "") or (not _j_truth(write_file(path, wav)))) then
				path = ""
			end
			SND_READY[name] = path
		end
	end
	local p = SND_READY[name]
	if (p ~= "") then
		play_wav(p)
	end
end
SND_POS_READY = {}
SPA_TIER = 0
SPA_PAN = 0
function snd_pos_pick(dist, rel)
	if (dist >= 1200.0) then
		return false
	end
	SPA_TIER = ((dist < 320.0) and 0 or ((dist < 704.0) and 1 or 2))
	SPA_PAN = ((rel > 0.5) and 1 or ((rel < -0.5) and 2 or 0))
	return true
end
function snd_wav_stereo(name, lnum, rnum, den)
	local lu = lump_num(name)
	if (lu < 0) then
		return ""
	end
	local pos = LUMP_POS[lu + 1]
	if ((LUMP_SIZE[lu + 1] < 10) or (u16(pos) ~= 3)) then
		return ""
	end
	local rate = u16((pos + 2))
	local count = i32((pos + 4))
	local ofs = 8
	if (count > 32) then
		ofs = 24
		count = (count - 32)
	end
	if (count > (LUMP_SIZE[lu + 1] - ofs)) then
		count = (LUMP_SIZE[lu + 1] - ofs)
	end
	if (count <= 0) then
		return ""
	end
	local data = ""
	local w = WAD
	local stop = ((pos + ofs) + count)
	for i = (pos + ofs), (stop - 1), 1 do
		do
			local s = ((string.byte(w, i + 1) + 0) - 128)
			data = (data .. string.char(((128 + _j_idiv((s * lnum), den)) & 255)))
			data = (data .. string.char(((128 + _j_idiv((s * rnum), den)) & 255)))
		end
		::_jd_continue_1::
	end
	return (((((((((((("RIFF" .. le32((36 + (count * 2)))) .. "WAVEfmt ") .. le32(16)) .. le16(1)) .. le16(2)) .. le32(rate)) .. le32((rate * 2))) .. le16(2)) .. le16(8)) .. "data") .. le32((count * 2))) .. data)
end
function snd_play_at(name, x, y)
	if (not SND_ON) then
		return
	end
	local dx = (x - P_X)
	local dy = (y - P_Y)
	local rel = (math.atan(dy, dx) - P_ANG)
	do
		while (rel > PI) do
			do
				rel = (rel - TAU)
			end
			::_jd_continue_2::
		end
	end
	do
		while (rel < (0.0 - PI)) do
			do
				rel = (rel + TAU)
			end
			::_jd_continue_3::
		end
	end
	if (not snd_pos_pick(math.sqrt(((dx * dx) + (dy * dy))), rel)) then
		return
	end
	if ((SPA_TIER == 0) and (SPA_PAN == 0)) then
		snd_play(name)
		return
	end
	local key = ((((("" .. name) .. "_t") .. _j_str(SPA_TIER)) .. "_p") .. _j_str(SPA_PAN))
	if (not (SND_POS_READY[key] ~= nil)) then
		local g = ((SPA_TIER == 0) and 16 or ((SPA_TIER == 1) and 8 or 4))
		local ln = ((SPA_PAN == 2) and _j_idiv(g, 2) or g)
		local rn = ((SPA_PAN == 1) and _j_idiv(g, 2) or g)
		local wav = snd_wav_stereo(name, ln, rn, 16)
		if (wav == "") then
			SND_POS_READY[key] = ""
		else
			local path = ((audio_cache_dir() .. string.lower(("" .. key))) .. ".wav")
			if (not _j_truth(write_file(path, wav))) then
				path = ""
			end
			SND_POS_READY[key] = path
		end
	end
	local p = SND_POS_READY[key]
	if (p ~= "") then
		play_wav(p)
	end
end
