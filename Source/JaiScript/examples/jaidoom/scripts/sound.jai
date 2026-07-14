// JaiDOOM sound.jai — the WAD's own voice, zero audio libraries. DS* lumps
// are DMX: u16 format(3), u16 sample rate, u32 length (incl. 16+16 pad
// bytes), then 8-bit unsigned PCM — which is exactly WAV's 8-bit encoding,
// so the samples pass through VERBATIM behind a minted 44-byte RIFF header.
// The runner's play_wav does the playback (async, one voice, no-op off
// Windows). Lumps convert lazily to cached .wav files on first play.

import "wad.jai";
import "vid.jai";

var SND_ON = false;           // play() flips it on; gates and benches stay silent
var SND_READY = {};           // lump name -> wav path ("" = missing/undecodable)

function snd_wav_bytes(string name) -> string {
	int lu = lump_num(name);
	if (lu < 0) { return ""; }
	int pos = LUMP_POS[lu];
	if (LUMP_SIZE[lu] < 10 || u16(pos) != 3) { return ""; }
	int rate = u16(pos + 2);
	int count = i32(pos + 4);
	int ofs = 8;
	if (count > 32) { ofs = 24; count = count - 32; }   // strip the DMX lead/tail pads
	if (count > LUMP_SIZE[lu] - ofs) { count = LUMP_SIZE[lu] - ofs; }
	if (count <= 0) { return ""; }
	return "RIFF" + le32(36 + count) + "WAVEfmt " + le32(16) + le16(1) + le16(1)
		+ le32(rate) + le32(rate) + le16(1) + le16(8) + "data" + le32(count)
		+ WAD.substr(pos + ofs, count);
}

function snd_play(string name) -> void {
	if (!SND_ON) { return; }
	if (!SND_READY.has(name)) {
		var wav = snd_wav_bytes(name);
		if (wav == "") { SND_READY[name] = ""; }
		else {
			var path = ("" + name).to_lower() + ".wav";   // copy first: to_lower mutates in place
			if (!write_file(path, wav)) { path = ""; }
			SND_READY[name] = path;
		}
	}
	var p = SND_READY[name];
	if (p != "") { play_wav(p); }
}

// ---------------------------------------------------------- positional audio ---
// Pure-script 3D-ish sound with zero host changes: distance picks an
// attenuation tier, bearing picks a pan, and each (name, tier, pan) mints a
// cached STEREO wav variant whose per-ear gains were baked into the samples
// at generation time. ~25 sounds x at most 9 variants, lazily created.

var SND_POS_READY = {};       // "NAME_tN_pP" -> wav path ("" = missing/undecodable)
var SPA_TIER = 0;             // 0 full <320, 1 mid <704, 2 far <1200
var SPA_PAN = 0;              // 0 center, 1 left, 2 right (bearing beyond ±0.5 rad)

function snd_pos_pick(float dist, float rel) -> bool {
	if (dist >= 1200.0) { return false; }
	SPA_TIER = dist < 320.0 ? 0 : (dist < 704.0 ? 1 : 2);
	SPA_PAN = rel > 0.5 ? 1 : (rel < -0.5 ? 2 : 0);
	return true;
}

// the mono decode's twin: same DMX header walk, but every sample lands twice
// (interleaved LR) scaled 128 + (s-128)*num/den per ear — 8-bit PCM stays 8-bit
function snd_wav_stereo(string name, int lnum, int rnum, int den) -> string {
	int lu = lump_num(name);
	if (lu < 0) { return ""; }
	int pos = LUMP_POS[lu];
	if (LUMP_SIZE[lu] < 10 || u16(pos) != 3) { return ""; }
	int rate = u16(pos + 2);
	int count = i32(pos + 4);
	int ofs = 8;
	if (count > 32) { ofs = 24; count = count - 32; }   // strip the DMX lead/tail pads
	if (count > LUMP_SIZE[lu] - ofs) { count = LUMP_SIZE[lu] - ofs; }
	if (count <= 0) { return ""; }
	var data = "";
	var& w = WAD;
	int stop = pos + ofs + count;
	for (int i = pos + ofs; i < stop; ++i) {
		int s = (w[i] + 0) - 128;
		data += to_char(128 + s * lnum / den);
		data += to_char(128 + s * rnum / den);
	}
	return "RIFF" + le32(36 + count * 2) + "WAVEfmt " + le32(16) + le16(1) + le16(2)
		+ le32(rate) + le32(rate * 2) + le16(2) + le16(8) + "data" + le32(count * 2) + data;
}

function snd_play_at(string name, float x, float y) -> void {
	if (!SND_ON) { return; }
	float dx = x - P_X;
	float dy = y - P_Y;
	float rel = math::atan2(dy, dx) - P_ANG;
	while (rel > PI) { rel = rel - TAU; }
	while (rel < 0.0 - PI) { rel = rel + TAU; }
	if (!snd_pos_pick(math::sqrt(dx * dx + dy * dy), rel)) { return; }
	if (SPA_TIER == 0 && SPA_PAN == 0) { snd_play(name); return; }   // close and ahead: the plain lump
	var key = `${name}_t${SPA_TIER}_p${SPA_PAN}`;
	if (!SND_POS_READY.has(key)) {
		int g = SPA_TIER == 0 ? 16 : (SPA_TIER == 1 ? 8 : 4);
		int ln = SPA_PAN == 2 ? g / 2 : g;
		int rn = SPA_PAN == 1 ? g / 2 : g;
		var wav = snd_wav_stereo(name, ln, rn, 16);
		if (wav == "") { SND_POS_READY[key] = ""; }
		else {
			var path = ("" + key).to_lower() + ".wav";   // copy first: to_lower mutates in place
			if (!write_file(path, wav)) { path = ""; }
			SND_POS_READY[key] = path;
		}
	}
	var p = SND_POS_READY[key];
	if (p != "") { play_wav(p); }
}
