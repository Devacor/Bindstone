"""demoreel music (scenes/music.jai): the score each scene hears. A Cue is composed at
boot from the reel's seed and the scene's Mood and answers the scene frame by frame
(the kick, the chord, the energy, the hits). MIDI writing is left out: the perf path
never renders the score to a file. Time is in sixteenth steps from the cue's first
downbeat, which is the scene's t = 0.
"""

import math

MUSIC_BEAT = 0.48387097
MUSIC_BAR = 1.93548388
MUSIC_STEP = 0.12096774

# chord qualities: the four tones a chord is voiced and arpeggiated from, semitones over its root
# (maj7, m7, add9, m add9, 7sus4, 7)
MUSIC_CHORDS = [[0, 4, 7, 11], [0, 3, 7, 10], [0, 4, 7, 14], [0, 3, 7, 14], [0, 5, 7, 10], [0, 4, 7, 10]]
# the progressions: four chords, each [semitones over the key's tonic, quality]
MUSIC_PROGRESSIONS = [
	[[0, 3], [8, 0], [3, 2], [10, 2]],
	[[0, 3], [3, 2], [10, 2], [5, 1]],
	[[8, 0], [10, 2], [0, 3], [0, 1]],
	[[0, 1], [1, 0], [0, 3], [10, 1]],
	[[0, 3], [10, 2], [8, 0], [10, 4]],
	[[0, 3], [5, 1], [8, 0], [7, 1]],
	[[0, 1], [5, 5], [10, 2], [8, 0]],
	[[8, 0], [10, 2], [0, 2], [0, 0]]
]
# the arpeggio's sixteen steps, as chord tones over two octaves: one row a drum style
MUSIC_ARPS = [
	[0, 2, 4, 2, 1, 3, 5, 3, 0, 2, 4, 6, 5, 3, 4, 2],
	[0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1],
	[0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1],
	[0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1],
	[0, 4, 2, 4, 1, 4, 2, 4, 0, 5, 3, 5, 1, 5, 3, 5]
]
# three, three and two: the sixteenths the arpeggio leans on and the pad's gate opens for
MUSIC_ACCENT = [True, False, False, True, False, False, True, False, True, False, False, True, False, False, True, False]
MUSIC_GATE = [127, 118, 44, 127, 118, 44, 127, 44, 127, 118, 44, 127, 118, 44, 127, 44]

KIT_POWER = 16
KIT_ELECTRONIC = 24
KIT_808 = 25


# drum styles: 0 outrun, 1 breakbeat, 2 half time, 3 ostinato, 4 driving
class Mood:
	__slots__ = ("prog", "style", "root", "kit", "bass", "pad", "arp", "lead", "energy", "chord_bars", "cut_crash")

	def __init__(self, prog_, style_, root_, kit_, bass_, pad_, arp_, lead_, energy_, chord_bars_):
		self.prog = prog_
		self.style = style_
		self.root = root_
		self.kit = kit_
		self.bass = bass_
		self.pad = pad_
		self.arp = arp_
		self.lead = lead_
		self.energy = float(energy_)
		self.chord_bars = chord_bars_
		self.cut_crash = True


# one lane of events: steps (sorted), with a velocity and, for pitched lanes, a note and a length
class Lane:
	__slots__ = ("at", "vel", "note", "len")

	def __init__(self):
		self.at = []
		self.vel = []
		self.note = []
		self.len = []

	def add(self, step, v, n, l):
		self.at.append(step)
		self.vel.append(v)
		self.note.append(n)
		self.len.append(l)

	# the last event at or before step s, or -1
	def last(self, s):
		at = self.at
		lo = 0
		hi = len(at) - 1
		result = -1
		while lo <= hi:
			mid = (lo + hi) // 2
			if at[mid] <= s:
				result = mid
				lo = mid + 1
			else:
				hi = mid - 1
		return result

	# 1 the moment an event lands (scaled by its velocity), decaying at `rate` per second
	def env(self, t, rate):
		s = t / MUSIC_STEP
		i = self.last(s + 0.0001)
		if i < 0:
			return 0.0
		since = t - self.at[i] * MUSIC_STEP
		return self.vel[i] / 127.0 * math.pow(2.718281828, 0.0 - since * rate)


class Cue:
	__slots__ = ("bars", "mood", "seed", "kick", "snare", "hat", "crash", "bass_lane", "pad_lane", "arp_lane",
		"lead_lane", "echo", "stab", "orch", "swell", "duck", "gate", "bright", "energy_bar", "chord_root",
		"chord_q", "hit_t", "hit_level", "big_steps", "lcg_state")

	def __init__(self, bars_, mood_, seed_, hits, levels):
		self.bars = bars_
		self.mood = mood_
		self.seed = seed_
		self.kick = Lane()
		self.snare = Lane()
		self.hat = Lane()
		self.crash = Lane()
		self.bass_lane = Lane()
		self.pad_lane = Lane()
		self.arp_lane = Lane()
		self.lead_lane = Lane()
		self.echo = Lane()
		self.stab = Lane()
		self.orch = Lane()
		self.swell = Lane()
		self.duck = []
		self.gate = []
		self.bright = []
		self.energy_bar = []
		self.chord_root = []
		self.chord_q = []
		self.hit_t = [float(x) for x in hits]
		self.hit_level = list(levels)
		self.big_steps = []
		self.lcg_state = 1
		self.compose()

	# ------------------------------------------------------------ queries ---

	def beat_phase(self, t):
		b = t / MUSIC_BEAT
		return b - math.floor(b)

	def bar_phase(self, t):
		b = t / MUSIC_BAR
		return b - math.floor(b)

	def bar_index(self, t):
		return math.floor(t / MUSIC_BAR)

	# 1 on every beat, falling away before the next
	def pulse(self, t):
		f = self.beat_phase(t)
		return (1.0 - f) * (1.0 - f)

	def kick_env(self, t):
		return self.kick.env(t, 9.0)

	def snare_env(self, t):
		return self.snare.env(t, 7.0)

	def hat_env(self, t):
		return self.hat.env(t, 22.0)

	def bass_env(self, t):
		return self.bass_lane.env(t, 5.0)

	def lead_env(self, t):
		return self.lead_lane.env(t, 3.0)

	def arp_env(self, t):
		return self.arp_lane.env(t, 14.0)

	def bass_note(self, t):
		i = self.bass_lane.last(t / MUSIC_STEP + 0.0001)
		return self.mood.root if i < 0 else self.bass_lane.note[i]

	def lead_note(self, t):
		i = self.lead_lane.last(t / MUSIC_STEP + 0.0001)
		return 0 if i < 0 else self.lead_lane.note[i]

	# the sounding chord's root as a pitch class, 0..11
	def chord(self, t):
		b = self.bar_index(t)
		bars = self.bars
		b = 0 if b < 0 else (bars - 1 if b >= bars else b)
		return self.chord_root[b] % 12

	# the section's intensity, 0..1, eased between bars
	def energy(self, t):
		energy_bar = self.energy_bar
		b = t / MUSIC_BAR
		i = math.floor(b)
		if i < 0:
			return energy_bar[0]
		if i >= self.bars - 1:
			return energy_bar[self.bars - 1]
		f = b - i
		return energy_bar[i] + (energy_bar[i + 1] - energy_bar[i]) * f * f * (3.0 - 2.0 * f)

	# 1 at each of the scene's hits (a big one at full, an accent at half), decaying over about a second
	def hit(self, t):
		result = 0.0
		hit_t = self.hit_t
		hit_level = self.hit_level
		for k in range(len(hit_t)):
			since = t - hit_t[k]
			if since < 0.0 or since > 3.0:
				continue
			e = (1.0 if hit_level[k] >= 2 else 0.5) * math.pow(2.718281828, 0.0 - since * 2.5)
			result = e if e > result else result
		return result

	# 0..1 over the bar leading into each big hit
	def riser(self, t):
		result = 0.0
		hit_t = self.hit_t
		hit_level = self.hit_level
		for k in range(len(hit_t)):
			if hit_level[k] < 2:
				continue
			to = hit_t[k] - t
			if to < 0.0 or to > MUSIC_BAR:
				continue
			r = 1.0 - to / MUSIC_BAR
			result = r if r > result else result
		return result

	# ----------------------------------------------------------- composing ---

	def roll(self, n):
		self.lcg_state = (self.lcg_state * 1103515245 + 12345) & 2147483647
		return (self.lcg_state >> 16) % n

	# tone i (any integer, octaves above and below) of bar b's chord, semitones over its root
	def tone(self, b, i):
		q = MUSIC_CHORDS[self.chord_q[b]]
		oct_ = math.floor(i / 4.0)
		return q[i - oct_ * 4] + oct_ * 12

	# sixteenths until the next big hit at or after step s, or a long way off
	def to_big(self, s):
		result = 9999
		for b in self.big_steps:
			d = b - s
			if d > 0 and d < result:
				result = d
		return result

	def is_big(self, s):
		for b in self.big_steps:
			if b == s:
				return True
		return False

	def compose(self):
		mood = self.mood
		hit_t = self.hit_t
		hit_level = self.hit_level
		bars = self.bars
		energy_bar = self.energy_bar
		chord_root = self.chord_root
		self.lcg_state = (self.seed & 65535) | 1
		prog = MUSIC_PROGRESSIONS[mood.prog]
		steps = bars * 16
		for k0 in range(len(hit_t)):
			if hit_level[k0] >= 2:
				self.big_steps.append(int(hit_t[k0] / MUSIC_STEP + 0.5))
		# the energy curve: the title bar is quiet, then the mood's level, a build into each big
		# hit and a drop on it, easing back down after
		for b in range(bars):
			e = mood.energy * 0.55 if b == 0 else mood.energy + (1.0 - mood.energy) * (b / (bars * 1.0)) * 0.35
			energy_bar.append(e)
		for k in range(len(hit_t)):
			hb = math.floor(hit_t[k] / MUSIC_BAR + 0.0001)
			if hb < 0 or hb >= bars:
				continue
			if hit_level[k] >= 2:
				energy_bar[hb] = 1.0
				if hb > 0:
					energy_bar[hb - 1] = energy_bar[hb - 1] * 0.7 + 0.25
				if hb + 1 < bars:
					energy_bar[hb + 1] = energy_bar[hb + 1] if energy_bar[hb + 1] > 0.9 else 0.9
			else:
				energy_bar[hb] = 1.0 if energy_bar[hb] + 0.12 > 1.0 else energy_bar[hb] + 0.12
		# every third pass through the progression starts from its second half
		for b2 in range(bars):
			pass_ = b2 // (mood.chord_bars * 4)
			ch = prog[(b2 // mood.chord_bars + (2 if pass_ % 4 == 2 else 0)) % 4]
			off = ch[0] - 12 if ch[0] > 6 else ch[0]
			chord_root.append(mood.root + off)
			self.chord_q.append(ch[1])
		# the lead's motif: two bars of rhythm and chord tones from the seed, sung over each
		# chord and varied on its repeats
		motif_at = []
		motif_deg = []
		motif_len = []
		walk = 2
		for ms in range(0, 32, 2):
			r0 = self.roll(16)
			if ms == 0 or ms == 16 or r0 < 8:
				walk = walk + (r0 % 5) - 2
				walk = 1 if walk < 0 else (6 if walk > 7 else walk)
				motif_at.append(ms)
				motif_deg.append(walk)
				motif_len.append(4 if r0 < 4 else (3 if r0 < 6 else 2))
		tone = self.tone
		pad_lane = self.pad_lane
		arp_lane = self.arp_lane
		echo = self.echo
		stab = self.stab
		lead_lane = self.lead_lane
		for s in range(steps):
			b3 = s // 16
			st = s % 16
			e = energy_bar[b3]
			croot = chord_root[b3]
			tb = self.to_big(s)
			gap = tb <= 4    # the beat of silence before the drop
			self.drums(s, st, e, tb)
			turning = (b3 + 1) % mood.chord_bars == 0 and b3 + 1 < bars and chord_root[b3 + 1] != croot
			if e > 0.3 and not gap and not self.is_big(s):
				self.bass(s, st, e, croot, turning)
			# the pad holds the chord from each chord's first bar
			if st == 0 and b3 % mood.chord_bars == 0:
				for v in range(4):
					pad_lane.add(s, (64 if v == 0 else 56) + int(e * 30.0), croot + 24 + tone(b3, v), 16 * mood.chord_bars)
			# the arpeggio runs the chord over two octaves in sixteenths once the energy is up
			if e > 0.5 and not gap:
				n = croot + 24 + tone(b3, MUSIC_ARPS[mood.style][st])
				v2 = 58 + (28 if MUSIC_ACCENT[st] else 0) + int(e * 14.0)
				arp_lane.add(s, v2, n, 1)
				if MUSIC_ACCENT[st]:
					echo.add(s + 3, v2 // 2, n, 1)
			# brass off the beat on every other bar of a peak
			if e > 0.82 and not gap and tb > 16 and b3 % 2 == 1 and (st == 6 or st == 11) and (mood.style == 0 or mood.style == 4):
				for v7 in range(3):
					stab.add(s, 88 if st == 6 else 76, croot + 24 + tone(b3, v7), 1)
			# the lead sings the motif at the peaks, a tone higher on every other pass, each note
			# repeating on the dotted eighth
			if e > 0.72 and not gap:
				ms2 = s % 32
				for mi in range(len(motif_at)):
					if motif_at[mi] != ms2:
						continue
					lift = 1 if (s // 64) % 2 == 1 and mi >= len(motif_at) // 2 else 0
					n2 = croot + 24 + tone(b3, motif_deg[mi] + lift)
					lead_lane.add(s, 104, n2, motif_len[mi])
					echo.add(s + 3, 56, n2, motif_len[mi])
					echo.add(s + 6, 32, n2, motif_len[mi])
		# the hits: a crash, a low boom, an orchestra hit and brass on the chord, the bass dropping
		# an octave under it, a reverse cymbal swelling over the bar before
		for k2 in range(len(hit_t)):
			hs = int(hit_t[k2] / MUSIC_STEP + 0.5)
			if hs < 0 or hs >= steps:
				continue
			hb3 = hs // 16
			hroot = chord_root[hb3]
			if hit_level[k2] >= 2:
				self.crash.add(hs, 127, 49, 1)
				self.crash.add(hs, 110, 57, 1)
				self.kick.add(hs, 127, 35, 1)
				self.bass_lane.add(hs, 127, self.bass_root(hroot), 8)
				for v3 in range(3):
					self.orch.add(hs, 120, hroot + 24 + tone(hb3, v3), 4)
				for v4 in range(3):
					stab.add(hs, 112, hroot + 24 + tone(hb3, v4), 8)
				if hs >= 16:
					self.swell.add(hs - 16, 112, 60, 16)
			else:
				self.crash.add(hs, 96, 57, 1)
				for v5 in range(3):
					stab.add(hs, 98, hroot + 24 + tone(hb3, v5), 2)
		self.sort_lane(self.kick)
		self.sort_lane(self.crash)
		self.sort_lane(self.bass_lane)
		self.sort_lane(stab)
		self.sort_lane(echo)
		# the filters: as open as the section is loud, sweeping from shut to wide over the bar into a big hit
		bright = self.bright
		for s3 in range(steps):
			tb2 = self.to_big(s3)
			open_ = 0.2 + 0.8 * (16 - tb2) / 16.0 if tb2 <= 16 else 0.3 + 0.55 * energy_bar[s3 // 16]
			bright.append(127 if self.is_big(s3) else int(open_ * 127.0))
		# the sidechain: every kick ducks the chords and they swell back over the beat
		duck = self.duck
		for s2 in range(steps):
			duck.append(127)
		depth = 52.0 + mood.energy * 24.0
		shape = [1.0, 0.6, 0.28, 0.08]
		kick = self.kick
		for ki in range(len(kick.at)):
			for j in range(4):
				at = kick.at[ki] + j
				if at >= steps:
					break
				v6 = 127 - int(depth * shape[j] * kick.vel[ki] / 127.0)
				if v6 < duck[at]:
					duck[at] = v6
		gate = self.gate
		for s4 in range(steps):
			chopped = energy_bar[s4 // 16] > 0.7 and mood.style != 2 and mood.style != 3 and self.to_big(s4) > 16
			gate.append(duck[s4] * MUSIC_GATE[s4 % 16] // 127 if chopped else duck[s4])

	# the chord's root an octave down, kept at E1 or above where a synth bass still speaks
	def bass_root(self, croot):
		return croot if croot - 12 < 28 else croot - 12

	# `turning`: the bar is the chord's last, and the four-on-the-floor styles walk its last two
	# sixteenths through the fifth and the seventh into the next chord
	def bass(self, s, st, e, croot, turning):
		lo = self.bass_root(croot)
		style = self.mood.style
		bass_lane = self.bass_lane
		if (style == 0 or style == 4) and turning and st >= 14 and e > 0.6:
			bass_lane.add(s, 104, lo + (7 if st == 14 else 10), 1)
		elif style == 0:
			# outrun: the root rolling in sixteenths, the octave bouncing on every off beat; eighths while the section is low
			groove = [116, 72, 100, 80]
			if e > 0.6 or st % 2 == 0:
				bass_lane.add(s, groove[st % 4], lo + (12 if st % 4 == 2 else 0), 1)
		elif style == 4:
			# driving: a gallop, an eighth and two sixteenths a beat, the octave on the last at the peaks
			if st % 4 != 1:
				bass_lane.add(s, 118 if st % 4 == 0 else (88 if st % 4 == 2 else 98), lo + (12 if st % 4 == 3 and e > 0.7 else 0), 2 if st % 4 == 0 else 1)
		elif style == 1:
			if st == 0 or st == 3 or st == 6 or st == 10 or st == 12 or st == 14:
				bass_lane.add(s, 116 if st == 0 else 94, lo + (12 if st == 6 or st == 14 else 0), 3 if st == 0 else 1)
		elif style == 2:
			if st == 0 or st == 6 or st == 8 or st == 14:
				bass_lane.add(s, 112 if st == 0 or st == 8 else 88, lo + (12 if st == 14 else 0), 5 if st == 0 or st == 8 else 2)
		else:
			# ostinato: eighths on the root, the octave on the last of each half bar
			if st % 2 == 0:
				bass_lane.add(s, 70 + int(e * 36.0) + (10 if st % 8 == 0 else 0), lo + (12 if st % 8 == 6 else 0), 1)

	def drums(self, s, st, e, tb):
		style = self.mood.style
		four = style == 0 or style == 4
		gap = tb <= 4
		if (e > 0.4 or st == 0) and not gap:
			k = False
			if four:
				k = st % 4 == 0 or (style == 4 and st == 14 and e > 0.8)
			elif style == 1:
				k = st == 0 or st == 6 or st == 10
			elif style == 2:
				k = st == 0 or st == 10
			else:
				k = (st == 0 and s % 32 == 0) or (st == 8 and e > 0.6)
			if k:
				self.kick.add(s, 118, 36, 1)
		# the backbeat: a gated snare and a clap together
		snare = self.snare
		if e > 0.45 and not gap:
			sn = False
			if four or style == 1:
				sn = st == 4 or st == 12
			elif style == 2:
				sn = st == 8
			else:
				sn = st == 8 and (s // 16) % 2 == 1
			if sn:
				snare.add(s, 112, 38, 1)
				if e > 0.55:
					snare.add(s, 96, 39, 1)
			if style == 1 and (st == 7 or st == 15) and e > 0.6:
				snare.add(s, 52, 38, 1)
		# into a big hit: a snare roll climbing through the last half bar, through the silence
		if tb <= 8:
			snare.add(s, 64 + (8 - tb) * 8, 38, 1)
		# a fill into every fourth bar: the toms falling through the last beat, through the last two into every eighth
		from_ = 8 if (s // 16) % 8 == 7 else 12
		fill = tb > 16 and e > 0.5 and (s // 16) % 4 == 3 and st >= from_
		if fill:
			toms = [50, 50, 48, 50, 50, 48, 47, 45]
			snare.add(s, 84 + (st - 8) * 5, toms[st - 8] - (2 if from_ == 8 and st >= 12 else 0), 1)
		if e > 0.3 and not gap:
			sixteen = style == 4 or e > 0.85
			open_ = e > 0.6 and st % 4 == 2 and style != 3
			if open_:
				self.hat.add(s, 70, 46, 1)
			elif sixteen or st % 2 == 0:
				self.hat.add(s, 74 if st % 4 == 0 else 50, 42, 1)
		# a crash on the cut, on the scene's first full bar and on every fourth bar at the peaks
		if s == 0 and self.mood.cut_crash:
			self.crash.add(s, 108, 49, 1)
		if st == 0 and s > 0 and (s == 16 or (s % 64 == 0 and e > 0.7)) and not gap:
			self.crash.add(s, 116 if s == 16 else 100, 49, 1)

	# insertion order is time order for every lane but the ones the hits and echoes add to afterwards
	def sort_lane(self, lane):
		at = lane.at
		vel = lane.vel
		note = lane.note
		ln = lane.len
		n = len(at)
		for i in range(1, n):
			j = i
			while j > 0 and at[j - 1] > at[j]:
				at[j], at[j - 1] = at[j - 1], at[j]
				vel[j], vel[j - 1] = vel[j - 1], vel[j]
				note[j], note[j - 1] = note[j - 1], note[j]
				ln[j], ln[j - 1] = ln[j - 1], ln[j]
				j -= 1


# each scene's mood, by its place in the reel: the keys travel, so no two neighbours share one
# [progression, drum style, tonic, kit, bass, pad, arpeggio, lead, base energy, bars a chord]
def music_mood(idx):
	if idx == 0:
		return Mood(0, 0, 45, KIT_POWER, 39, 90, 81, 81, 0.76, 2)
	if idx == 1:
		return Mood(1, 0, 38, KIT_POWER, 38, 90, 80, 84, 0.78, 2)
	if idx == 2:
		return Mood(4, 4, 40, KIT_ELECTRONIC, 38, 95, 81, 81, 0.85, 1)
	if idx == 3:
		return Mood(6, 2, 41, KIT_808, 38, 89, 98, 80, 0.64, 2)
	if idx == 4:
		return Mood(5, 0, 43, KIT_POWER, 39, 90, 81, 81, 0.78, 2)
	if idx == 5:
		return Mood(0, 2, 47, KIT_808, 38, 89, 98, 82, 0.58, 2)
	if idx == 6:
		return Mood(3, 4, 36, KIT_ELECTRONIC, 39, 95, 87, 30, 0.85, 1)
	if idx == 7:
		return Mood(2, 0, 46, KIT_POWER, 39, 50, 81, 84, 0.76, 2)
	if idx == 8:
		return Mood(1, 0, 42, KIT_ELECTRONIC, 38, 89, 11, 81, 0.72, 2)
	if idx == 9:
		return Mood(3, 3, 37, KIT_808, 38, 91, 14, 81, 0.5, 2)
	if idx == 10:
		return Mood(4, 1, 44, KIT_808, 38, 94, 81, 80, 0.76, 2)
	if idx == 11:
		return Mood(7, 0, 40, KIT_POWER, 39, 90, 81, 81, 0.82, 2)
	# the cold open: F minor dark drive, the gallop, starting from silence
	result = Mood(4, 4, 41, KIT_ELECTRONIC, 39, 95, 81, 81, 0.82, 1)
	result.cut_crash = False
	return result
