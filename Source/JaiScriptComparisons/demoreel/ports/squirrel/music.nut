// MUSIC (Squirrel port of scenes/music.jai): each scene's cue composed at boot from the reel's seed and the
// scene's mood, and the queries the scenes ask it frame by frame. MIDI writing is not ported.

::MUSIC_BEAT <- 0.48387097;
::MUSIC_BAR <- 1.93548388;
::MUSIC_STEP <- 0.12096774;

// chord qualities: the four tones a chord is voiced and arpeggiated from, semitones over its root
::MUSIC_CHORDS <- [[0, 4, 7, 11], [0, 3, 7, 10], [0, 4, 7, 14], [0, 3, 7, 14], [0, 5, 7, 10], [0, 4, 7, 10]];
// the progressions: four chords, each [semitones over the key's tonic, quality]
::MUSIC_PROGRESSIONS <- [
	[[0, 3], [8, 0], [3, 2], [10, 2]],
	[[0, 3], [3, 2], [10, 2], [5, 1]],
	[[8, 0], [10, 2], [0, 3], [0, 1]],
	[[0, 1], [1, 0], [0, 3], [10, 1]],
	[[0, 3], [10, 2], [8, 0], [10, 4]],
	[[0, 3], [5, 1], [8, 0], [7, 1]],
	[[0, 1], [5, 5], [10, 2], [8, 0]],
	[[8, 0], [10, 2], [0, 2], [0, 0]]
];
// the arpeggio's sixteen steps, as chord tones over two octaves: one row a drum style
::MUSIC_ARPS <- [
	[0, 2, 4, 2, 1, 3, 5, 3, 0, 2, 4, 6, 5, 3, 4, 2],
	[0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1],
	[0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1],
	[0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1],
	[0, 4, 2, 4, 1, 4, 2, 4, 0, 5, 3, 5, 1, 5, 3, 5]
];
::MUSIC_ACCENT <- [true, false, false, true, false, false, true, false, true, false, false, true, false, false, true, false];
::MUSIC_GATE <- [127, 118, 44, 127, 118, 44, 127, 44, 127, 118, 44, 127, 118, 44, 127, 44];

::KIT_POWER <- 16;
::KIT_ELECTRONIC <- 24;
::KIT_808 <- 25;

// drum styles: 0 outrun, 1 breakbeat, 2 half time, 3 ostinato, 4 driving
class Mood {
	prog = 0;
	style = 0;
	root = 45;
	kit = 16;
	bass = 38;
	pad = 90;
	arp = 81;
	lead = 81;
	energy = 0.6;
	chord_bars = 2;
	cut_crash = true;

	constructor(prog_, style_, root_, kit_, bass_, pad_, arp_, lead_, energy_, chord_bars_) {
		prog = prog_;
		style = style_;
		root = root_;
		kit = kit_;
		bass = bass_;
		pad = pad_;
		arp = arp_;
		lead = lead_;
		energy = energy_.tofloat();
		chord_bars = chord_bars_;
	}
}

// one lane of events: steps (sorted), with a velocity and, for pitched lanes, a note and a length
class Lane {
	at = null;
	vel = null;
	note = null;
	len = null;

	constructor() {
		at = [];
		vel = [];
		note = [];
		len = [];
	}

	function add(step, v, n, l) {
		at.push(step);
		vel.push(v);
		note.push(n);
		len.push(l);
	}

	// the last event at or before step s, or -1
	function last(s) {
		local a = at;
		local lo = 0;
		local hi = a.len() - 1;
		local result = -1;
		while (lo <= hi) {
			local mid = (lo + hi) / 2;
			if (a[mid] <= s) {
				result = mid;
				lo = mid + 1;
			} else {
				hi = mid - 1;
			}
		}
		return result;
	}

	// 1 the moment an event lands (scaled by its velocity), decaying at `rate` per second
	function env(t, rate) {
		local s = t / ::MUSIC_STEP;
		local i = last(s + 0.0001);
		if (i < 0) { return 0.0; }
		local since = t - at[i] * ::MUSIC_STEP;
		return vel[i] / 127.0 * ::pow(2.718281828, 0.0 - since * rate);
	}
}

class Cue {
	bars = 8;
	mood = null;
	seed = 1;
	kick = null;
	snare = null;
	hat = null;
	crash = null;
	bass_lane = null;
	pad_lane = null;
	arp_lane = null;
	lead_lane = null;
	echo = null;
	stab = null;
	orch = null;
	swell = null;
	duck = null;
	gate = null;
	bright = null;
	energy_bar = null;
	chord_root = null;
	chord_q = null;
	hit_t = null;
	hit_level = null;
	big_steps = null;
	lcg_state = 1;

	constructor(bars_, mood_, seed_, hits, levels) {
		kick = ::Lane();
		snare = ::Lane();
		hat = ::Lane();
		crash = ::Lane();
		bass_lane = ::Lane();
		pad_lane = ::Lane();
		arp_lane = ::Lane();
		lead_lane = ::Lane();
		echo = ::Lane();
		stab = ::Lane();
		orch = ::Lane();
		swell = ::Lane();
		duck = [];
		gate = [];
		bright = [];
		energy_bar = [];
		chord_root = [];
		chord_q = [];
		big_steps = [];
		bars = bars_;
		mood = mood_;
		seed = seed_;
		// the reference's array<float> / array<int> parameters copy and convert
		hit_t = [];
		foreach (v in hits) { hit_t.push(v.tofloat()); }
		hit_level = [];
		foreach (v in levels) { hit_level.push(v.tointeger()); }
		compose();
	}

	// ------------------------------------------------------------ queries ---

	function beat_phase(t) {
		local b = t / ::MUSIC_BEAT;
		return b - ::floor(b);
	}

	function bar_phase(t) {
		local b = t / ::MUSIC_BAR;
		return b - ::floor(b);
	}

	function bar_index(t) { return ::ifloor(t / ::MUSIC_BAR); }

	// 1 on every beat, falling away before the next
	function pulse(t) {
		local f = beat_phase(t);
		return (1.0 - f) * (1.0 - f);
	}

	function kick_env(t) { return kick.env(t, 9.0); }
	function snare_env(t) { return snare.env(t, 7.0); }
	function hat_env(t) { return hat.env(t, 22.0); }
	function bass_env(t) { return bass_lane.env(t, 5.0); }
	function lead_env(t) { return lead_lane.env(t, 3.0); }
	function arp_env(t) { return arp_lane.env(t, 14.0); }

	function bass_note(t) {
		local i = bass_lane.last(t / ::MUSIC_STEP + 0.0001);
		return i < 0 ? mood.root : bass_lane.note[i];
	}

	function lead_note(t) {
		local i = lead_lane.last(t / ::MUSIC_STEP + 0.0001);
		return i < 0 ? 0 : lead_lane.note[i];
	}

	// the sounding chord's root as a pitch class, 0..11
	function chord(t) {
		local b = bar_index(t);
		b = b < 0 ? 0 : (b >= bars ? bars - 1 : b);
		return chord_root[b] % 12;
	}

	// the section's intensity, 0..1, eased between bars
	function energy(t) {
		local b = t / ::MUSIC_BAR;
		local i = ::ifloor(b);
		if (i < 0) { return energy_bar[0]; }
		if (i >= bars - 1) { return energy_bar[bars - 1]; }
		local f = b - i;
		return energy_bar[i] + (energy_bar[i + 1] - energy_bar[i]) * f * f * (3.0 - 2.0 * f);
	}

	// 1 at each of the scene's hits (a big one at full, an accent at half), decaying over about a second
	function hit(t) {
		local result = 0.0;
		local n = hit_t.len();
		for (local k = 0; k < n; ++k) {
			local since = t - hit_t[k];
			if (since < 0.0 || since > 3.0) { continue; }
			local e = (hit_level[k] >= 2 ? 1.0 : 0.5) * ::pow(2.718281828, 0.0 - since * 2.5);
			result = e > result ? e : result;
		}
		return result;
	}

	// 0..1 over the bar leading into each big hit
	function riser(t) {
		local result = 0.0;
		local n = hit_t.len();
		for (local k = 0; k < n; ++k) {
			if (hit_level[k] < 2) { continue; }
			local to = hit_t[k] - t;
			if (to < 0.0 || to > ::MUSIC_BAR) { continue; }
			local r = 1.0 - to / ::MUSIC_BAR;
			result = r > result ? r : result;
		}
		return result;
	}

	// ----------------------------------------------------------- composing ---

	function roll(n) {
		lcg_state = (lcg_state * 1103515245 + 12345) & 2147483647;
		return (lcg_state >> 16) % n;
	}

	// tone i (any integer, octaves above and below) of bar b's chord, semitones over its root
	function tone(b, i) {
		local q = ::MUSIC_CHORDS[chord_q[b]];
		local oct = ::ifloor(i / 4.0);
		return q[i - oct * 4] + oct * 12;
	}

	// sixteenths until the next big hit at or after step s, or a long way off
	function to_big(s) {
		local result = 9999;
		local n = big_steps.len();
		for (local k = 0; k < n; ++k) {
			local d = big_steps[k] - s;
			if (d > 0 && d < result) { result = d; }
		}
		return result;
	}

	function is_big(s) {
		local n = big_steps.len();
		for (local k = 0; k < n; ++k) {
			if (big_steps[k] == s) { return true; }
		}
		return false;
	}

	function compose() {
		lcg_state = (seed & 65535) | 1;
		local prog = ::MUSIC_PROGRESSIONS[mood.prog];
		local steps = bars * 16;
		for (local k0 = 0; k0 < hit_t.len(); ++k0) {
			if (hit_level[k0] >= 2) { big_steps.push((hit_t[k0] / ::MUSIC_STEP + 0.5).tointeger()); }
		}
		// the energy curve: the title bar is quiet, then the mood's level, a build into each big
		// hit and a drop on it, easing back down after
		for (local b = 0; b < bars; ++b) {
			local e = b == 0 ? mood.energy * 0.55 : mood.energy + (1.0 - mood.energy) * (b / (bars * 1.0)) * 0.35;
			energy_bar.push(e);
		}
		for (local k = 0; k < hit_t.len(); ++k) {
			local hb = ::ifloor(hit_t[k] / ::MUSIC_BAR + 0.0001);
			if (hb < 0 || hb >= bars) { continue; }
			if (hit_level[k] >= 2) {
				energy_bar[hb] = 1.0;
				if (hb > 0) { energy_bar[hb - 1] = energy_bar[hb - 1] * 0.7 + 0.25; }
				if (hb + 1 < bars) { energy_bar[hb + 1] = energy_bar[hb + 1] > 0.9 ? energy_bar[hb + 1] : 0.9; }
			} else {
				energy_bar[hb] = energy_bar[hb] + 0.12 > 1.0 ? 1.0 : energy_bar[hb] + 0.12;
			}
		}
		// every third pass through the progression starts from its second half
		for (local b2 = 0; b2 < bars; ++b2) {
			local pass = b2 / (mood.chord_bars * 4);
			local ch = prog[(b2 / mood.chord_bars + (pass % 4 == 2 ? 2 : 0)) % 4];
			local off = ch[0] > 6 ? ch[0] - 12 : ch[0];
			chord_root.push(mood.root + off);
			chord_q.push(ch[1]);
		}
		// the lead's motif: two bars of rhythm and chord tones from the seed
		local motif_at = [];
		local motif_deg = [];
		local motif_len = [];
		local walk = 2;
		for (local ms = 0; ms < 32; ms += 2) {
			local r0 = roll(16);
			if (ms == 0 || ms == 16 || r0 < 8) {
				walk = walk + (r0 % 5) - 2;
				walk = walk < 0 ? 1 : (walk > 7 ? 6 : walk);
				motif_at.push(ms);
				motif_deg.push(walk);
				motif_len.push(r0 < 4 ? 4 : (r0 < 6 ? 3 : 2));
			}
		}
		for (local s = 0; s < steps; ++s) {
			local b3 = s / 16;
			local st = s % 16;
			local e = energy_bar[b3];
			local croot = chord_root[b3];
			local tb = to_big(s);
			local gap = tb <= 4;
			drums(s, st, e, tb);
			local turning = (b3 + 1) % mood.chord_bars == 0 && b3 + 1 < bars && chord_root[b3 + 1] != croot;
			if (e > 0.3 && !gap && !is_big(s)) { bass(s, st, e, croot, turning); }
			// the pad holds the chord from each chord's first bar
			if (st == 0 && b3 % mood.chord_bars == 0) {
				for (local v = 0; v < 4; ++v) { pad_lane.add(s, (v == 0 ? 64 : 56) + (e * 30.0).tointeger(), croot + 24 + tone(b3, v), 16 * mood.chord_bars); }
			}
			// the arpeggio runs the chord over two octaves in sixteenths once the energy is up
			if (e > 0.5 && !gap) {
				local n = croot + 24 + tone(b3, ::MUSIC_ARPS[mood.style][st]);
				local v2 = 58 + (::MUSIC_ACCENT[st] ? 28 : 0) + (e * 14.0).tointeger();
				arp_lane.add(s, v2, n, 1);
				if (::MUSIC_ACCENT[st]) { echo.add(s + 3, v2 / 2, n, 1); }
			}
			// brass off the beat on every other bar of a peak
			if (e > 0.82 && !gap && tb > 16 && b3 % 2 == 1 && (st == 6 || st == 11) && (mood.style == 0 || mood.style == 4)) {
				for (local v7 = 0; v7 < 3; ++v7) { stab.add(s, st == 6 ? 88 : 76, croot + 24 + tone(b3, v7), 1); }
			}
			// the lead sings the motif at the peaks, a tone higher on every other pass
			if (e > 0.72 && !gap) {
				local ms2 = s % 32;
				for (local mi = 0; mi < motif_at.len(); ++mi) {
					if (motif_at[mi] != ms2) { continue; }
					local lift = (s / 64) % 2 == 1 && mi >= motif_at.len() / 2 ? 1 : 0;
					local n2 = croot + 24 + tone(b3, motif_deg[mi] + lift);
					lead_lane.add(s, 104, n2, motif_len[mi]);
					echo.add(s + 3, 56, n2, motif_len[mi]);
					echo.add(s + 6, 32, n2, motif_len[mi]);
				}
			}
		}
		// the hits: a crash, a low boom, an orchestra hit and brass on the chord
		for (local k2 = 0; k2 < hit_t.len(); ++k2) {
			local hs = (hit_t[k2] / ::MUSIC_STEP + 0.5).tointeger();
			if (hs < 0 || hs >= steps) { continue; }
			local hb3 = hs / 16;
			local hroot = chord_root[hb3];
			if (hit_level[k2] >= 2) {
				crash.add(hs, 127, 49, 1);
				crash.add(hs, 110, 57, 1);
				kick.add(hs, 127, 35, 1);
				bass_lane.add(hs, 127, bass_root(hroot), 8);
				for (local v3 = 0; v3 < 3; ++v3) { orch.add(hs, 120, hroot + 24 + tone(hb3, v3), 4); }
				for (local v4 = 0; v4 < 3; ++v4) { stab.add(hs, 112, hroot + 24 + tone(hb3, v4), 8); }
				if (hs >= 16) { swell.add(hs - 16, 112, 60, 16); }
			} else {
				crash.add(hs, 96, 57, 1);
				for (local v5 = 0; v5 < 3; ++v5) { stab.add(hs, 98, hroot + 24 + tone(hb3, v5), 2); }
			}
		}
		sort_lane(kick);
		sort_lane(crash);
		sort_lane(bass_lane);
		sort_lane(stab);
		sort_lane(echo);
		// the filters: as open as the section is loud, sweeping from shut to wide over the bar into a big hit
		for (local s3 = 0; s3 < steps; ++s3) {
			local tb2 = to_big(s3);
			local open = tb2 <= 16 ? 0.2 + 0.8 * (16 - tb2) / 16.0 : 0.3 + 0.55 * energy_bar[s3 / 16];
			bright.push(is_big(s3) ? 127 : (open * 127.0).tointeger());
		}
		// the sidechain: every kick ducks the chords and they swell back over the beat
		for (local s2 = 0; s2 < steps; ++s2) { duck.push(127); }
		local depth = 52.0 + mood.energy * 24.0;
		local shape = [1.0, 0.6, 0.28, 0.08];
		for (local ki = 0; ki < kick.at.len(); ++ki) {
			for (local j = 0; j < 4; ++j) {
				local at = kick.at[ki] + j;
				if (at >= steps) { break; }
				local v6 = 127 - (depth * shape[j] * kick.vel[ki] / 127.0).tointeger();
				if (v6 < duck[at]) { duck[at] = v6; }
			}
		}
		for (local s4 = 0; s4 < steps; ++s4) {
			local chopped = energy_bar[s4 / 16] > 0.7 && mood.style != 2 && mood.style != 3 && to_big(s4) > 16;
			gate.push(chopped ? duck[s4] * ::MUSIC_GATE[s4 % 16] / 127 : duck[s4]);
		}
	}

	// the chord's root an octave down, kept at E1 or above where a synth bass still speaks
	function bass_root(croot) { return croot - 12 < 28 ? croot : croot - 12; }

	function bass(s, st, e, croot, turning) {
		local lo = bass_root(croot);
		local style = mood.style;
		if ((style == 0 || style == 4) && turning && st >= 14 && e > 0.6) {
			bass_lane.add(s, 104, lo + (st == 14 ? 7 : 10), 1);
		} else if (style == 0) {
			local groove = [116, 72, 100, 80];
			if (e > 0.6 || st % 2 == 0) { bass_lane.add(s, groove[st % 4], lo + (st % 4 == 2 ? 12 : 0), 1); }
		} else if (style == 4) {
			if (st % 4 != 1) { bass_lane.add(s, st % 4 == 0 ? 118 : (st % 4 == 2 ? 88 : 98), lo + (st % 4 == 3 && e > 0.7 ? 12 : 0), st % 4 == 0 ? 2 : 1); }
		} else if (style == 1) {
			if (st == 0 || st == 3 || st == 6 || st == 10 || st == 12 || st == 14) { bass_lane.add(s, st == 0 ? 116 : 94, lo + (st == 6 || st == 14 ? 12 : 0), st == 0 ? 3 : 1); }
		} else if (style == 2) {
			if (st == 0 || st == 6 || st == 8 || st == 14) { bass_lane.add(s, st == 0 || st == 8 ? 112 : 88, lo + (st == 14 ? 12 : 0), st == 0 || st == 8 ? 5 : 2); }
		} else {
			if (st % 2 == 0) { bass_lane.add(s, 70 + (e * 36.0).tointeger() + (st % 8 == 0 ? 10 : 0), lo + (st % 8 == 6 ? 12 : 0), 1); }
		}
	}

	function drums(s, st, e, tb) {
		local style = mood.style;
		local four = style == 0 || style == 4;
		local gap = tb <= 4;
		if ((e > 0.4 || st == 0) && !gap) {
			local k = false;
			if (four) { k = st % 4 == 0 || (style == 4 && st == 14 && e > 0.8); }
			else if (style == 1) { k = st == 0 || st == 6 || st == 10; }
			else if (style == 2) { k = st == 0 || st == 10; }
			else { k = (st == 0 && s % 32 == 0) || (st == 8 && e > 0.6); }
			if (k) { kick.add(s, 118, 36, 1); }
		}
		// the backbeat: a gated snare and a clap together
		if (e > 0.45 && !gap) {
			local sn = false;
			if (four || style == 1) { sn = st == 4 || st == 12; }
			else if (style == 2) { sn = st == 8; }
			else { sn = st == 8 && (s / 16) % 2 == 1; }
			if (sn) {
				snare.add(s, 112, 38, 1);
				if (e > 0.55) { snare.add(s, 96, 39, 1); }
			}
			if (style == 1 && (st == 7 || st == 15) && e > 0.6) { snare.add(s, 52, 38, 1); }
		}
		// into a big hit: a snare roll climbing through the last half bar, through the silence
		if (tb <= 8) { snare.add(s, 64 + (8 - tb) * 8, 38, 1); }
		// a fill into every fourth bar: the toms falling through the last beat, through the last two into every eighth
		local from = (s / 16) % 8 == 7 ? 8 : 12;
		local fill = tb > 16 && e > 0.5 && (s / 16) % 4 == 3 && st >= from;
		if (fill) {
			local toms = [50, 50, 48, 50, 50, 48, 47, 45];
			snare.add(s, 84 + (st - 8) * 5, toms[st - 8] - (from == 8 && st >= 12 ? 2 : 0), 1);
		}
		if (e > 0.3 && !gap) {
			local sixteen = style == 4 || e > 0.85;
			local open = e > 0.6 && st % 4 == 2 && style != 3;
			if (open) { hat.add(s, 70, 46, 1); }
			else if (sixteen || st % 2 == 0) { hat.add(s, st % 4 == 0 ? 74 : 50, 42, 1); }
		}
		// a crash on the cut, on the scene's first full bar and on every fourth bar at the peaks
		if (s == 0 && mood.cut_crash) { crash.add(s, 108, 49, 1); }
		if (st == 0 && s > 0 && (s == 16 || (s % 64 == 0 && e > 0.7)) && !gap) { crash.add(s, s == 16 ? 116 : 100, 49, 1); }
	}

	// insertion order is time order for every lane but the ones the hits and echoes add to afterwards
	function sort_lane(lane) {
		local at = lane.at, vel = lane.vel, note = lane.note, len = lane.len;
		local n = at.len();
		for (local i = 1; i < n; ++i) {
			local j = i;
			while (j > 0 && at[j - 1] > at[j]) {
				local ta = at[j]; at[j] = at[j - 1]; at[j - 1] = ta;
				local tv = vel[j]; vel[j] = vel[j - 1]; vel[j - 1] = tv;
				local tn = note[j]; note[j] = note[j - 1]; note[j - 1] = tn;
				local tl = len[j]; len[j] = len[j - 1]; len[j - 1] = tl;
				--j;
			}
		}
	}
}

// each scene's mood, by its place in the reel
// [progression, drum style, tonic, kit, bass, pad, arpeggio, lead, base energy, bars a chord]
function music_mood(idx) {
	switch (idx) {
		case 0: return ::Mood(0, 0, 45, ::KIT_POWER, 39, 90, 81, 81, 0.76, 2);
		case 1: return ::Mood(1, 0, 38, ::KIT_POWER, 38, 90, 80, 84, 0.78, 2);
		case 2: return ::Mood(4, 4, 40, ::KIT_ELECTRONIC, 38, 95, 81, 81, 0.85, 1);
		case 3: return ::Mood(6, 2, 41, ::KIT_808, 38, 89, 98, 80, 0.64, 2);
		case 4: return ::Mood(5, 0, 43, ::KIT_POWER, 39, 90, 81, 81, 0.78, 2);
		case 5: return ::Mood(0, 2, 47, ::KIT_808, 38, 89, 98, 82, 0.58, 2);
		case 6: return ::Mood(3, 4, 36, ::KIT_ELECTRONIC, 39, 95, 87, 30, 0.85, 1);
		case 7: return ::Mood(2, 0, 46, ::KIT_POWER, 39, 50, 81, 84, 0.76, 2);
		case 8: return ::Mood(1, 0, 42, ::KIT_ELECTRONIC, 38, 89, 11, 81, 0.72, 2);
		case 9: return ::Mood(3, 3, 37, ::KIT_808, 38, 91, 14, 81, 0.5, 2);
		case 10: return ::Mood(4, 1, 44, ::KIT_808, 38, 94, 81, 80, 0.76, 2);
		case 11: return ::Mood(7, 0, 40, ::KIT_POWER, 39, 90, 81, 81, 0.82, 2);
	}
	// the cold open: F minor dark drive, the gallop, starting from silence
	local result = ::Mood(4, 4, 41, ::KIT_ELECTRONIC, 39, 95, 81, 81, 0.82, 1);
	result.cut_crash = false;
	return result;
}
