// JaiDOOM audio extractor: every DS* sound lump to .wav and D_* music lump to .mid under
// ../cache/<wad>/ (boot.jai does this as part of the game's own bootstrap; this is the
// same step on its own).   jaiscript extract_audio.jai -- ../wads/doom1.wad ../wads/freedoom1.wad

import "boot.jai";

function main() -> int {
	if (ARGS.size() == 0) { print("usage: jaiscript extract_audio.jai -- <wad> [<wad> ...]"); return 2; }
	int total = 0;
	for (auto p : ARGS) {
		int n = audio_extract_wad(p);
		if (n < 0) { print(`   cannot read ${p}`); continue; }
		print(`   ${WAD_BASE}: ${n} sounds and songs -> ${audio_cache_dir()}`);
		total += n;
	}
	return total > 0 ? 0 : 1;
}
