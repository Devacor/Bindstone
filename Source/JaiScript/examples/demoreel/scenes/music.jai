// MUSIC — the soundtrack is procedural, and the scenes hear it. Each scene has a
// cue: its bars of music composed at boot from the reel's seed and the scene's mood
// (a scale and a chord progression, a drum style, instruments, an energy curve),
// with an impact on each of the scene's own big moments and a riser into the
// biggest. The score is data: it renders to a Standard MIDI File the runner plays,
// and it answers the scene frame by frame (how hard the kick just landed, which
// chord is sounding, how much energy the section has), so every scene moves with
// the music it is played to, with no audio analysis and to the last byte the same
// on every run. Time is in sixteenth steps from the cue's first downbeat, which is
// the scene's t = 0; the reel's 124 bpm makes a step 0.121 s and a bar 1.935 s.

float MUSIC_BEAT = 0.48387097;
float MUSIC_BAR = 1.93548388;
float MUSIC_STEP = 0.12096774;

// the moods: [scale, progression, drum style, bass, pad, arp, lead programs, root note, base energy]
// drum styles: 0 four on the floor, 1 breakbeat, 2 half time, 3 sparse, 4 driving sixteenths
var MUSIC_SCALES = [[0, 2, 3, 5, 7, 8, 10], [0, 2, 3, 5, 7, 9, 10], [0, 1, 3, 5, 7, 8, 10], [0, 2, 4, 5, 7, 9, 11], [0, 2, 4, 5, 7, 9, 10]];
var MUSIC_PROGRESSIONS = [[0, 5, 2, 6], [0, 3, 0, 6], [0, 1, 6, 0], [0, 4, 5, 3], [0, 6, 3, 0]];

class Mood {
	int scale = 0;       // into MUSIC_SCALES (minor, dorian, phrygian, major, mixolydian)
	int style = 0;
	int root = 45;       // the bass's tonic
	int bass = 38;       // General MIDI programs
	int pad = 89;
	int arp = 81;
	int lead = 80;
	float energy = 0.6;
	int chord_bars = 2;

	Mood(int scale_, int style_, int root_, int bass_, int pad_, int arp_, int lead_, float energy_, int chord_bars_) {
		scale = scale_;
		style = style_;
		root = root_;
		bass = bass_;
		pad = pad_;
		arp = arp_;
		lead = lead_;
		energy = energy_;
		chord_bars = chord_bars_;
	}
}

// one lane of events: steps (sorted), with a velocity and, for pitched lanes, a note and a length
class Lane {
	array<int> at = [];
	array<int> vel = [];
	array<int> note = [];
	array<int> len = [];

	void add(int step, int v, int n, int l) {
		at.push(step);
		vel.push(v);
		note.push(n);
		len.push(l);
	}

	// the last event at or before step s, or -1
	int last(float s) {
		int lo = 0;
		int hi = at.size() - 1;
		int result = -1;
		while (lo <= hi) {
			int mid = (lo + hi) / 2;
			if (at[mid] <= s) {
				result = mid;
				lo = mid + 1;
			} else {
				hi = mid - 1;
			}
		}
		return result;
	}

	// 1 the moment an event lands (scaled by its velocity), decaying at `rate` per second
	float env(float t, float rate) {
		float s = t / MUSIC_STEP;
		int i = last(s + 0.0001);
		if (i < 0) { return 0.0; }
		float since = t - at[i] * MUSIC_STEP;
		return vel[i] / 127.0 * math::pow(2.718281828, 0.0 - since * rate);
	}
}

class Cue {
	int bars = 8;
	var mood = null;
	int seed = 1;
	var kick = Lane();
	var snare = Lane();
	var hat = Lane();
	var crash = Lane();
	var bass_lane = Lane();
	var pad_lane = Lane();
	var arp_lane = Lane();
	var lead_lane = Lane();
	var stab = Lane();
	array<float> energy_bar = [];
	array<int> chord_root = [];    // per bar: the chord's root, a MIDI note
	array<float> hit_t = [];
	array<int> hit_level = [];

	Cue(int bars_, var mood_, int seed_, array<float> hits, array<int> levels) {
		bars = bars_;
		mood = mood_;
		seed = seed_;
		hit_t = hits;
		hit_level = levels;
		compose();
	}

	// ------------------------------------------------------------ queries ---

	float beat_phase(float t) {
		float b = t / MUSIC_BEAT;
		return b - math::floor(b);
	}

	float bar_phase(float t) {
		float b = t / MUSIC_BAR;
		return b - math::floor(b);
	}

	int bar_index(float t) { return math::ifloor(t / MUSIC_BAR); }

	// 1 on every beat, falling away before the next
	float pulse(float t) {
		float f = beat_phase(t);
		return (1.0 - f) * (1.0 - f);
	}

	float kick_env(float t) { return kick.env(t, 9.0); }
	float snare_env(float t) { return snare.env(t, 7.0); }
	float hat_env(float t) { return hat.env(t, 22.0); }
	float bass_env(float t) { return bass_lane.env(t, 5.0); }
	float lead_env(float t) { return lead_lane.env(t, 3.0); }
	float arp_env(float t) { return arp_lane.env(t, 14.0); }

	int bass_note(float t) {
		int i = bass_lane.last(t / MUSIC_STEP + 0.0001);
		return i < 0 ? mood.root : bass_lane.note[i];
	}

	int lead_note(float t) {
		int i = lead_lane.last(t / MUSIC_STEP + 0.0001);
		return i < 0 ? 0 : lead_lane.note[i];
	}

	// the sounding chord's root as a pitch class, 0..11
	int chord(float t) {
		int b = bar_index(t);
		b = b < 0 ? 0 : (b >= bars ? bars - 1 : b);
		return chord_root[b] % 12;
	}

	// the section's intensity, 0..1, eased between bars
	float energy(float t) {
		float b = t / MUSIC_BAR;
		int i = math::ifloor(b);
		if (i < 0) { return energy_bar[0]; }
		if (i >= bars - 1) { return energy_bar[bars - 1]; }
		float f = b - i;
		return energy_bar[i] + (energy_bar[i + 1] - energy_bar[i]) * f * f * (3.0 - 2.0 * f);
	}

	// 1 at each of the scene's hits (a big one at full, an accent at half), decaying over about a second
	float hit(float t) {
		float result = 0.0;
		for (int k = 0; k < hit_t.size(); ++k) {
			float since = t - hit_t[k];
			if (since < 0.0 || since > 3.0) { continue; }
			float e = (hit_level[k] >= 2 ? 1.0 : 0.5) * math::pow(2.718281828, 0.0 - since * 2.5);
			result = e > result ? e : result;
		}
		return result;
	}

	// 0..1 over the bar leading into each big hit
	float riser(float t) {
		float result = 0.0;
		for (int k = 0; k < hit_t.size(); ++k) {
			if (hit_level[k] < 2) { continue; }
			float to = hit_t[k] - t;
			if (to < 0.0 || to > MUSIC_BAR) { continue; }
			float r = 1.0 - to / MUSIC_BAR;
			result = r > result ? r : result;
		}
		return result;
	}

	// ----------------------------------------------------------- composing ---

	int lcg_state = 1;

	int roll(int n) {
		lcg_state = (lcg_state * 1103515245 + 12345) & 2147483647;
		return (lcg_state >> 16) % n;
	}

	// scale degree d (any integer) as a semitone offset from the tonic
	int degree(int d) {
		var sc = MUSIC_SCALES[mood.scale];
		int oct = math::ifloor(d / 7.0);
		return sc[d - oct * 7] + oct * 12;
	}

	void compose() {
		lcg_state = (seed & 65535) | 1;
		var prog = MUSIC_PROGRESSIONS[mood.scale];
		int steps = bars * 16;
		// the energy curve: the title bar is quiet, then the mood's level, a build into each big
		// hit and a drop on it, easing back down after
		for (int b = 0; b < bars; ++b) {
			float e = b == 0 ? mood.energy * 0.55 : mood.energy + (1.0 - mood.energy) * (b / (bars * 1.0)) * 0.35;
			energy_bar.push(e);
		}
		for (int k = 0; k < hit_t.size(); ++k) {
			int hb = math::ifloor(hit_t[k] / MUSIC_BAR + 0.0001);
			if (hb < 0 || hb >= bars) { continue; }
			if (hit_level[k] >= 2) {
				energy_bar[hb] = 1.0;
				if (hb > 0) { energy_bar[hb - 1] = energy_bar[hb - 1] * 0.7 + 0.25; }
				if (hb + 1 < bars) { energy_bar[hb + 1] = energy_bar[hb + 1] > 0.9 ? energy_bar[hb + 1] : 0.9; }
			} else {
				energy_bar[hb] = energy_bar[hb] + 0.12 > 1.0 ? 1.0 : energy_bar[hb] + 0.12;
			}
		}
		for (int b2 = 0; b2 < bars; ++b2) {
			int ci = (b2 / mood.chord_bars) % 4;
			chord_root.push(mood.root + degree(prog[ci]));
		}
		// the lead's motif: two bars of rhythm and scale steps from the seed, sung over each
		// chord from its root and varied on its repeats, so the tune has a shape to remember
		array<int> motif_at = [];
		array<int> motif_deg = [];
		array<int> motif_len = [];
		int walk = 0;
		for (int ms = 0; ms < 32; ms += 2) {
			int r0 = roll(16);
			if (ms == 0 || ms == 16 || r0 < 8) {
				walk = walk + (r0 % 5) - 2;
				walk = walk < -2 ? -1 : (walk > 6 ? 5 : walk);
				motif_at.push(ms);
				motif_deg.push(walk);
				motif_len.push(r0 < 4 ? 4 : 2);
			}
		}
		for (int s = 0; s < steps; ++s) {
			int b3 = s / 16;
			int st = s % 16;
			float e = energy_bar[b3];
			int croot = chord_root[b3];
			int ci2 = (b3 / mood.chord_bars) % 4;
			int deg = prog[ci2];
			bool riser_bar = false;
			for (int k = 0; k < hit_t.size(); ++k) {
				int hb2 = math::ifloor(hit_t[k] / MUSIC_BAR + 0.0001);
				if (hit_level[k] >= 2 && hb2 == b3 + 1) { riser_bar = true; }
			}
			drums(s, st, e, riser_bar);
			// the bass: the chord's root, pumping by style
			if (e > 0.3) {
				bool on = false;
				int style = mood.style;
				if (style == 0 || style == 4) { on = st % 2 == 0; }
				else if (style == 1) { on = st == 0 || st == 3 || st == 6 || st == 10 || st == 12; }
				else if (style == 2) { on = st == 0 || st == 8 || st == 14; }
				else { on = st == 0; }
				if (on) {
					int n = croot - 12 + (st % 8 == 4 && style == 0 ? 12 : 0);
					bass_lane.add(s, st % 4 == 0 ? 112 : 88, n, style == 3 ? 16 : 2);
				}
			}
			// the pad holds the chord with its seventh from each chord's first bar
			if (st == 0 && b3 % mood.chord_bars == 0) {
				for (int v = 0; v < 4; ++v) { pad_lane.add(s, (v == 3 ? 48 : 60) + itrunc(e * 30.0), mood.root + 12 + degree(deg + v * 2), 16 * mood.chord_bars); }
			}
			// the arpeggio climbs the chord in sixteenths once the energy is up
			if (e > 0.55) {
				var pat = [0, 1, 2, 3, 2, 1, 0, 3];
				int pi = pat[st % 8];
				int tone = pi == 3 ? degree(deg) + 12 : degree(deg + pi * 2);
				arp_lane.add(s, 64 + (st % 4 == 0 ? 22 : 0), mood.root + 24 + tone, 1);
			}
			// the lead sings the motif at the peaks, a step higher on every other pass
			if (e > 0.78) {
				int ms2 = s % 32;
				for (int mi = 0; mi < motif_at.size(); ++mi) {
					if (motif_at[mi] != ms2) { continue; }
					int lift = (s / 64) % 2 == 1 && mi >= motif_at.size() / 2 ? 2 : 0;
					lead_lane.add(s, 100, mood.root + 24 + degree(deg + motif_deg[mi] + lift), motif_len[mi]);
				}
			}
		}
		// the hits: a crash, a low boom and a brass stab on the chord
		for (int k2 = 0; k2 < hit_t.size(); ++k2) {
			int hs = itrunc(hit_t[k2] / MUSIC_STEP + 0.5);
			if (hs < 0 || hs >= steps) { continue; }
			int hb3 = hs / 16;
			int deg2 = prog[(hb3 / mood.chord_bars) % 4];
			if (hit_level[k2] >= 2) {
				crash.add(hs, 124, 49, 1);
				kick.add(hs, 127, 35, 1);
				for (int v2 = 0; v2 < 3; ++v2) { stab.add(hs, 118, mood.root + 12 + degree(deg2 + v2 * 2), 6); }
			} else {
				for (int v3 = 0; v3 < 3; ++v3) { stab.add(hs, 96, mood.root + 12 + degree(deg2 + v3 * 2), 2); }
			}
		}
		sort_lane(kick);
		sort_lane(crash);
		sort_lane(stab);
	}

	void drums(int s, int st, float e, bool riser_bar) {
		int style = mood.style;
		bool four = style == 0 || style == 4;
		if (e > 0.4 || st == 0) {
			bool k = false;
			if (four) { k = st % 4 == 0; }
			else if (style == 1) { k = st == 0 || st == 10; }
			else if (style == 2) { k = st == 0; }
			else { k = st == 0 && s % 32 == 0; }
			if (k) { kick.add(s, 118, 36, 1); }
		}
		if (e > 0.45) {
			bool sn = false;
			if (four || style == 1) { sn = st == 4 || st == 12; }
			else if (style == 2) { sn = st == 8; }
			if (sn) { snare.add(s, 106, style == 4 ? 39 : 38, 1); }
		}
		if (riser_bar && st >= 8) { snare.add(s, 60 + (st - 8) * 8, 38, 1); }
		// a fill into every fourth bar: the toms falling through the last beat
		bool fill = !riser_bar && e > 0.5 && (s / 16) % 4 == 3 && st >= 12;
		if (fill) {
			var toms = [50, 48, 47, 45];
			snare.add(s, 92 + (st - 12) * 6, toms[st - 12], 1);
		}
		if (e > 0.72 && (st == 4 || st == 12) && (style == 0 || style == 1 || style == 4)) { snare.add(s, 84, 39, 1); }
		if (e > 0.3) {
			bool sixteen = style == 4 || e > 0.85;
			if (sixteen || st % 2 == 0) { hat.add(s, st % 4 == 0 ? 76 : 54, e > 0.7 && st % 4 == 2 ? 46 : 42, 1); }
		}
		if (st == 0 && s % 64 == 0 && e > 0.7 && s > 0) { crash.add(s, 100, 49, 1); }
	}

	// insertion order is time order for every lane but the ones the hits add to afterwards
	void sort_lane(var& lane) {
		int n = lane.at.size();
		for (int i = 1; i < n; ++i) {
			int j = i;
			while (j > 0 && lane.at[j - 1] > lane.at[j]) {
				int ta = lane.at[j]; lane.at[j] = lane.at[j - 1]; lane.at[j - 1] = ta;
				int tv = lane.vel[j]; lane.vel[j] = lane.vel[j - 1]; lane.vel[j - 1] = tv;
				int tn = lane.note[j]; lane.note[j] = lane.note[j - 1]; lane.note[j - 1] = tn;
				int tl = lane.len[j]; lane.len[j] = lane.len[j - 1]; lane.len[j - 1] = tl;
				--j;
			}
		}
	}

	// ---------------------------------------------------------------- MIDI ---

	// the score as a Standard MIDI File: one track, 96 ticks a quarter, events in time order
	string midi() {
		var m = MidiTrack();
		int q = 96;
		int s16 = q / 4;
		m.ev(0, "" + to_char(255) + to_char(81) + to_char(3) + to_char(7) + to_char(98) + to_char(31));
		var programs = [mood.bass, mood.pad, mood.arp, mood.lead, 61];
		var volumes = [108, 74, 62, 92, 100];
		for (int ch = 0; ch < 5; ++ch) {
			m.ev(0, "" + to_char(192 | ch) + to_char(programs[ch]));
			m.ev(0, b3(176 | ch, 7, volumes[ch]));
		}
		var lanes = [kick, snare, hat, crash, bass_lane, pad_lane, arp_lane, lead_lane, stab];
		var channels = [9, 9, 9, 9, 0, 1, 2, 3, 4];
		array<int> cursor = [0, 0, 0, 0, 0, 0, 0, 0, 0];
		int steps = bars * 16;
		for (int s = 0; s < steps; ++s) {
			int tick = s * s16;
			m.flush(tick);
			for (int li = 0; li < lanes.size(); ++li) {
				var& lane = lanes[li];
				while (cursor[li] < lane.at.size() && lane.at[cursor[li]] == s) {
					int c = cursor[li];
					m.note(tick, channels[li], lane.note[c], lane.vel[c], lane.len[c] * s16 - 2);
					cursor[li] = c + 1;
				}
			}
		}
		m.flush(steps * s16);
		m.ev(steps * s16, "" + to_char(255) + to_char(47) + to_char(0));
		string head = "MThd" + be32(6) + "" + to_char(0) + to_char(0) + to_char(0) + to_char(1) + to_char(0) + to_char(q);
		return head + "MTrk" + be32(m.trk.size()) + m.trk;
	}
}

// ---------------------------------------------------------- MIDI writing ---

function vlq(int v) -> string {
	string out = "" + to_char(v & 127);
	int r = v >> 7;
	while (r > 0) {
		out = "" + to_char(128 | (r & 127)) + out;
		r = r >> 7;
	}
	return out;
}

function b3(int a, int b, int c) -> string { return "" + to_char(a) + to_char(b) + to_char(c); }
function be32(int v) -> string {
	return "" + to_char((v >> 24) & 255) + to_char((v >> 16) & 255) + to_char((v >> 8) & 255) + to_char(v & 255);
}

class MidiTrack {
	string trk = "";
	int last = 0;
	array<int> off_t = [];
	array<int> off_c = [];
	array<int> off_n = [];

	void ev(int tick, string bytes) {
		trk += vlq(tick - last) + bytes;
		last = tick;
	}

	void note(int tick, int ch, int n, int vel, int dur) {
		ev(tick, b3(144 | ch, n, vel));
		off_t.push(tick + (dur < 1 ? 1 : dur));
		off_c.push(ch);
		off_n.push(n);
	}

	// note-offs due by `tick`, emitted in time order (a delta is never negative)
	void flush(int tick) {
		while (true) {
			int pick = -1;
			for (int i = 0; i < off_t.size(); ++i) {
				if (off_t[i] <= tick && (pick < 0 || off_t[i] < off_t[pick])) { pick = i; }
			}
			if (pick < 0) { return; }
			ev(off_t[pick], b3(128 | off_c[pick], off_n[pick], 0));
			int last_i = off_t.size() - 1;
			off_t[pick] = off_t[last_i];
			off_c[pick] = off_c[last_i];
			off_n[pick] = off_n[last_i];
			off_t.pop();
			off_c.pop();
			off_n.pop();
		}
	}
}

// each scene's mood, by its place in the reel
function music_mood(int idx) -> var {
	switch (idx) {
		case 0: return Mood(1, 0, 38, 39, 90, 11, 80, 0.72, 2);    // DONUT: D dorian, four on the floor, vibes
		case 1: return Mood(2, 4, 40, 38, 95, 81, 81, 0.85, 1);    // JULIA: E phrygian, driving sixteenths
		case 2: return Mood(0, 2, 41, 33, 89, 98, 82, 0.62, 2);    // LAVALAMP: F minor, half time, crystal
		case 3: return Mood(0, 0, 45, 38, 90, 81, 80, 0.74, 2);    // PIPEDREAM: A minor synthwave
		case 4: return Mood(3, 3, 38, 33, 91, 98, 73, 0.5, 2);     // BOIDS: D major, sparse, choir and flute
		case 5: return Mood(0, 4, 36, 39, 95, 87, 30, 0.8, 1);     // GLOBHUNTER: C minor, driving, distortion
		case 6: return Mood(4, 2, 43, 38, 48, 46, 60, 0.64, 2);    // CANYONRUN: G mixolydian, half time, strings and horn
		case 7: return Mood(0, 0, 47, 38, 89, 11, 98, 0.7, 2);     // CHROME: B minor, four on the floor, crystal
		case 8: return Mood(2, 3, 45, 33, 91, 14, 53, 0.45, 2);    // OSSUARY: A phrygian, sparse, choir and bells
		case 9: return Mood(1, 1, 40, 38, 94, 81, 80, 0.7, 2);     // MAELSTROM: E dorian, breakbeat
		case 10: return Mood(0, 0, 45, 38, 89, 81, 80, 0.7, 2);    // FINALE: A minor, the reel's anthem
		default: return Mood(0, 4, 45, 38, 95, 81, 81, 0.8, 1);    // the cold open: A minor, driving sixteenths
	}
}
