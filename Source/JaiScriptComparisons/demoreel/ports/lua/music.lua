-- MUSIC (Lua 5.4 port of scenes/music.jai): each scene's cue, composed at boot from the
-- reel's seed and the scene's mood, answering the scene frame by frame (the kick, the
-- chord, the energy, the hits). The MIDI writer is not ported: the ports play no audio.
-- Time is in sixteenth steps from the cue's first downbeat, which is the scene's t = 0.

local floor = math.floor

MUSIC_BEAT = 0.48387097
MUSIC_BAR = 1.93548388
MUSIC_STEP = 0.12096774

-- chord qualities: the four tones a chord is voiced and arpeggiated from, semitones over its root
-- (maj7, m7, add9, m add9, 7sus4, 7)
MUSIC_CHORDS = {{0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 14}, {0, 3, 7, 14}, {0, 5, 7, 10}, {0, 4, 7, 10}}
-- the progressions: four chords, each {semitones over the key's tonic, quality}
MUSIC_PROGRESSIONS = {
	{{0, 3}, {8, 0}, {3, 2}, {10, 2}},     -- night drive: i(add9) bVImaj7 bIII(add9) bVII(add9)
	{{0, 3}, {3, 2}, {10, 2}, {5, 1}},     -- outrun: i(add9) bIII(add9) bVII(add9) iv7
	{{8, 0}, {10, 2}, {0, 3}, {0, 1}},     -- neon: bVImaj7 bVII(add9) i(add9) i7
	{{0, 1}, {1, 0}, {0, 3}, {10, 1}},     -- phrygian: i7 bIImaj7 i(add9) bvii7
	{{0, 3}, {10, 2}, {8, 0}, {10, 4}},    -- dark drive: i(add9) bVII(add9) bVImaj7 bVII7sus4
	{{0, 3}, {5, 1}, {8, 0}, {7, 1}},      -- chase: i(add9) iv7 bVImaj7 v7
	{{0, 1}, {5, 5}, {10, 2}, {8, 0}},     -- dorian: i7 IV7 bVII(add9) bVImaj7
	{{8, 0}, {10, 2}, {0, 2}, {0, 0}}      -- triumph: bVImaj7 bVII(add9) I(add9) Imaj7, the minor key turning major
}
-- the arpeggio's sixteen steps, as chord tones over two octaves: one row a drum style
MUSIC_ARPS = {
	{0, 2, 4, 2, 1, 3, 5, 3, 0, 2, 4, 6, 5, 3, 4, 2},
	{0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1},
	{0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1},
	{0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2, 4, 5, 4, 2, 1},
	{0, 4, 2, 4, 1, 4, 2, 4, 0, 5, 3, 5, 1, 5, 3, 5}
}
-- three, three and two: the sixteenths the arpeggio leans on and the pad's gate opens for
MUSIC_ACCENT = {true, false, false, true, false, false, true, false, true, false, false, true, false, false, true, false}
MUSIC_GATE = {127, 118, 44, 127, 118, 44, 127, 44, 127, 118, 44, 127, 118, 44, 127, 44}

-- General MIDI drum kits (the GS set on channel 10): the power kit's gated snare, the
-- electronic kit's pads, the 808
KIT_POWER = 16
KIT_ELECTRONIC = 24
KIT_808 = 25

-- drum styles: 0 outrun (four on the floor, a rolling octave bass), 1 breakbeat, 2 half time, 3 ostinato, 4 driving (a galloping bass)
Mood = {}
Mood.__index = Mood

function Mood.new(prog_, style_, root_, kit_, bass_, pad_, arp_, lead_, energy_, chord_bars_)
	local self = setmetatable({}, Mood)
	self.prog = prog_          -- into MUSIC_PROGRESSIONS
	self.style = style_
	self.root = root_          -- the key's tonic in the bass register
	self.kit = kit_
	self.bass = bass_          -- General MIDI programs
	self.pad = pad_
	self.arp = arp_
	self.lead = lead_
	self.energy = energy_ + 0.0
	self.chord_bars = chord_bars_
	self.cut_crash = true      -- a crash on the cue's first downbeat, where the picture cuts to the scene
	return self
end

-- one lane of events: steps (sorted), with a velocity and, for pitched lanes, a note and a length
Lane = {}
Lane.__index = Lane

function Lane.new()
	return setmetatable({at = {}, vel = {}, note = {}, len = {}}, Lane)
end

function Lane:add(step, v, n, l)
	local i = #self.at + 1
	self.at[i] = step
	self.vel[i] = v
	self.note[i] = n
	self.len[i] = l
end

-- the index of the last event at or before step s, or 0 (the original's -1)
function Lane:last(s)
	local at = self.at
	local lo = 1
	local hi = #at
	local result = 0
	while lo <= hi do
		local mid = (lo + hi) // 2
		if at[mid] <= s then
			result = mid
			lo = mid + 1
		else
			hi = mid - 1
		end
	end
	return result
end

-- 1 the moment an event lands (scaled by its velocity), decaying at `rate` per second
function Lane:env(t, rate)
	local s = t / MUSIC_STEP
	local i = self:last(s + 0.0001)
	if i < 1 then return 0.0 end
	local since = t - self.at[i] * MUSIC_STEP
	return self.vel[i] / 127.0 * (2.718281828 ^ (0.0 - since * rate))
end

Cue = {}
Cue.__index = Cue

function Cue.new(bars_, mood_, seed_, hits, levels)
	local self = setmetatable({}, Cue)
	self.bars = bars_
	self.mood = mood_
	self.seed = seed_
	self.kick = Lane.new()
	self.snare = Lane.new()
	self.hat = Lane.new()
	self.crash = Lane.new()
	self.bass_lane = Lane.new()
	self.pad_lane = Lane.new()
	self.arp_lane = Lane.new()
	self.lead_lane = Lane.new()
	self.echo = Lane.new()      -- the lead's and the arpeggio's dotted-eighth repeats
	self.stab = Lane.new()      -- brass on the accents
	self.orch = Lane.new()      -- the orchestra hit on the big hits
	self.swell = Lane.new()     -- a reverse cymbal into each big hit
	self.duck = {}              -- the chords' expression per step: the kick's sidechain pump
	self.gate = {}              -- the pad's expression per step: the pump, chopped three, three and two at the peaks
	self.bright = {}            -- the synths' filter per step: it follows the energy and opens over the bar into a big hit
	self.energy_bar = {}
	self.chord_root = {}        -- per bar: the chord's root, a MIDI note in the bass register
	self.chord_q = {}           -- per bar: the chord's quality, into MUSIC_CHORDS
	self.hit_t = {}
	self.hit_level = {}
	self.big_steps = {}
	self.lcg_state = 1
	for i = 1, #hits do self.hit_t[i] = hits[i] + 0.0 end
	for i = 1, #levels do self.hit_level[i] = levels[i] end
	self:compose()
	return self
end

-- ------------------------------------------------------------ queries ---

function Cue:beat_phase(t)
	local b = t / MUSIC_BEAT
	return b - floor(b)
end

function Cue:bar_phase(t)
	local b = t / MUSIC_BAR
	return b - floor(b)
end

function Cue:bar_index(t) return floor(t / MUSIC_BAR) end

-- 1 on every beat, falling away before the next
function Cue:pulse(t)
	local f = self:beat_phase(t)
	return (1.0 - f) * (1.0 - f)
end

function Cue:kick_env(t) return self.kick:env(t, 9.0) end
function Cue:snare_env(t) return self.snare:env(t, 7.0) end
function Cue:hat_env(t) return self.hat:env(t, 22.0) end
function Cue:bass_env(t) return self.bass_lane:env(t, 5.0) end
function Cue:lead_env(t) return self.lead_lane:env(t, 3.0) end
function Cue:arp_env(t) return self.arp_lane:env(t, 14.0) end

function Cue:bass_note(t)
	local i = self.bass_lane:last(t / MUSIC_STEP + 0.0001)
	return i < 1 and self.mood.root or self.bass_lane.note[i]
end

function Cue:lead_note(t)
	local i = self.lead_lane:last(t / MUSIC_STEP + 0.0001)
	return i < 1 and 0 or self.lead_lane.note[i]
end

-- the sounding chord's root as a pitch class, 0..11
function Cue:chord(t)
	local b = self:bar_index(t)
	local bars = self.bars
	b = b < 0 and 0 or (b >= bars and bars - 1 or b)
	return self.chord_root[b + 1] % 12
end

-- the section's intensity, 0..1, eased between bars
function Cue:energy(t)
	local energy_bar = self.energy_bar
	local b = t / MUSIC_BAR
	local i = floor(b)
	if i < 0 then return energy_bar[1] end
	if i >= self.bars - 1 then return energy_bar[self.bars] end
	local f = b - i
	return energy_bar[i + 1] + (energy_bar[i + 2] - energy_bar[i + 1]) * f * f * (3.0 - 2.0 * f)
end

-- 1 at each of the scene's hits (a big one at full, an accent at half), decaying over about a second
function Cue:hit(t)
	local hit_t, hit_level = self.hit_t, self.hit_level
	local result = 0.0
	for k = 1, #hit_t do
		local since = t - hit_t[k]
		if not (since < 0.0 or since > 3.0) then
			local e = (hit_level[k] >= 2 and 1.0 or 0.5) * (2.718281828 ^ (0.0 - since * 2.5))
			result = e > result and e or result
		end
	end
	return result
end

-- 0..1 over the bar leading into each big hit
function Cue:riser(t)
	local hit_t, hit_level = self.hit_t, self.hit_level
	local result = 0.0
	for k = 1, #hit_t do
		if hit_level[k] >= 2 then
			local to = hit_t[k] - t
			if not (to < 0.0 or to > MUSIC_BAR) then
				local r = 1.0 - to / MUSIC_BAR
				result = r > result and r or result
			end
		end
	end
	return result
end

-- ----------------------------------------------------------- composing ---

function Cue:roll(n)
	self.lcg_state = (self.lcg_state * 1103515245 + 12345) & 2147483647
	return (self.lcg_state >> 16) % n
end

-- tone i (any integer, octaves above and below) of bar b's chord, semitones over its root
function Cue:tone(b, i)
	local q = MUSIC_CHORDS[self.chord_q[b + 1] + 1]
	local oct = floor(i / 4.0)
	return q[i - oct * 4 + 1] + oct * 12
end

-- sixteenths until the next big hit at or after step s, or a long way off
function Cue:to_big(s)
	local big_steps = self.big_steps
	local result = 9999
	for k = 1, #big_steps do
		local d = big_steps[k] - s
		if d > 0 and d < result then result = d end
	end
	return result
end

function Cue:is_big(s)
	local big_steps = self.big_steps
	for k = 1, #big_steps do
		if big_steps[k] == s then return true end
	end
	return false
end

function Cue:compose()
	local mood = self.mood
	local bars = self.bars
	local hit_t, hit_level = self.hit_t, self.hit_level
	local energy_bar, chord_root, chord_q = self.energy_bar, self.chord_root, self.chord_q
	self.lcg_state = (self.seed & 65535) | 1
	local prog = MUSIC_PROGRESSIONS[mood.prog + 1]
	local steps = bars * 16
	for k0 = 1, #hit_t do
		if hit_level[k0] >= 2 then self.big_steps[#self.big_steps + 1] = itrunc(hit_t[k0] / MUSIC_STEP + 0.5) end
	end
	-- the energy curve: the title bar is quiet, then the mood's level, a build into each big
	-- hit and a drop on it, easing back down after
	for b = 0, bars - 1 do
		local e = b == 0 and mood.energy * 0.55 or mood.energy + (1.0 - mood.energy) * (b / (bars * 1.0)) * 0.35
		energy_bar[b + 1] = e
	end
	for k = 1, #hit_t do
		local hb = floor(hit_t[k] / MUSIC_BAR + 0.0001)
		if not (hb < 0 or hb >= bars) then
			if hit_level[k] >= 2 then
				energy_bar[hb + 1] = 1.0
				if hb > 0 then energy_bar[hb] = energy_bar[hb] * 0.7 + 0.25 end
				if hb + 1 < bars then energy_bar[hb + 2] = energy_bar[hb + 2] > 0.9 and energy_bar[hb + 2] or 0.9 end
			else
				energy_bar[hb + 1] = energy_bar[hb + 1] + 0.12 > 1.0 and 1.0 or energy_bar[hb + 1] + 0.12
			end
		end
	end
	-- every third pass through the progression starts from its second half
	for b2 = 0, bars - 1 do
		local pass = b2 // (mood.chord_bars * 4)
		local ch = prog[(b2 // mood.chord_bars + (pass % 4 == 2 and 2 or 0)) % 4 + 1]
		local off = ch[1] > 6 and ch[1] - 12 or ch[1]
		chord_root[b2 + 1] = mood.root + off
		chord_q[b2 + 1] = ch[2]
	end
	-- the lead's motif: two bars of rhythm and chord tones from the seed, sung over each
	-- chord and varied on its repeats, so the tune has a shape to remember
	local motif_at = {}
	local motif_deg = {}
	local motif_len = {}
	local walk = 2
	for ms = 0, 30, 2 do
		local r0 = self:roll(16)
		if ms == 0 or ms == 16 or r0 < 8 then
			walk = walk + (r0 % 5) - 2
			walk = walk < 0 and 1 or (walk > 7 and 6 or walk)
			local m = #motif_at + 1
			motif_at[m] = ms
			motif_deg[m] = walk
			motif_len[m] = r0 < 4 and 4 or (r0 < 6 and 3 or 2)
		end
	end
	for s = 0, steps - 1 do
		local b3 = s // 16
		local st = s % 16
		local e = energy_bar[b3 + 1]
		local croot = chord_root[b3 + 1]
		local tb = self:to_big(s)
		local gap = tb <= 4    -- the beat of silence before the drop
		self:drums(s, st, e, tb)
		local turning = (b3 + 1) % mood.chord_bars == 0 and b3 + 1 < bars and chord_root[b3 + 2] ~= croot
		if e > 0.3 and not gap and not self:is_big(s) then self:bass(s, st, e, croot, turning) end
		-- the pad holds the chord from each chord's first bar
		if st == 0 and b3 % mood.chord_bars == 0 then
			for v = 0, 3 do self.pad_lane:add(s, (v == 0 and 64 or 56) + itrunc(e * 30.0), croot + 24 + self:tone(b3, v), 16 * mood.chord_bars) end
		end
		-- the arpeggio runs the chord over two octaves in sixteenths once the energy is up
		if e > 0.5 and not gap then
			local n = croot + 24 + self:tone(b3, MUSIC_ARPS[mood.style + 1][st + 1])
			local v2 = 58 + (MUSIC_ACCENT[st + 1] and 28 or 0) + itrunc(e * 14.0)
			self.arp_lane:add(s, v2, n, 1)
			if MUSIC_ACCENT[st + 1] then self.echo:add(s + 3, v2 // 2, n, 1) end
		end
		-- brass off the beat on every other bar of a peak
		if e > 0.82 and not gap and tb > 16 and b3 % 2 == 1 and (st == 6 or st == 11) and (mood.style == 0 or mood.style == 4) then
			for v7 = 0, 2 do self.stab:add(s, st == 6 and 88 or 76, croot + 24 + self:tone(b3, v7), 1) end
		end
		-- the lead sings the motif at the peaks, a tone higher on every other pass, each note
		-- repeating on the dotted eighth
		if e > 0.72 and not gap then
			local ms2 = s % 32
			for mi = 0, #motif_at - 1 do
				if motif_at[mi + 1] == ms2 then
					local lift = (s // 64) % 2 == 1 and mi >= #motif_at // 2 and 1 or 0
					local n2 = croot + 24 + self:tone(b3, motif_deg[mi + 1] + lift)
					self.lead_lane:add(s, 104, n2, motif_len[mi + 1])
					self.echo:add(s + 3, 56, n2, motif_len[mi + 1])
					self.echo:add(s + 6, 32, n2, motif_len[mi + 1])
				end
			end
		end
	end
	-- the hits: a crash, a low boom, an orchestra hit and brass on the chord, the bass dropping
	-- an octave under it, a reverse cymbal swelling over the bar before
	for k2 = 1, #hit_t do
		local hs = itrunc(hit_t[k2] / MUSIC_STEP + 0.5)
		if not (hs < 0 or hs >= steps) then
			local hb3 = hs // 16
			local hroot = chord_root[hb3 + 1]
			if hit_level[k2] >= 2 then
				self.crash:add(hs, 127, 49, 1)
				self.crash:add(hs, 110, 57, 1)
				self.kick:add(hs, 127, 35, 1)
				self.bass_lane:add(hs, 127, self:bass_root(hroot), 8)
				for v3 = 0, 2 do self.orch:add(hs, 120, hroot + 24 + self:tone(hb3, v3), 4) end
				for v4 = 0, 2 do self.stab:add(hs, 112, hroot + 24 + self:tone(hb3, v4), 8) end
				if hs >= 16 then self.swell:add(hs - 16, 112, 60, 16) end
			else
				self.crash:add(hs, 96, 57, 1)
				for v5 = 0, 2 do self.stab:add(hs, 98, hroot + 24 + self:tone(hb3, v5), 2) end
			end
		end
	end
	self:sort_lane(self.kick)
	self:sort_lane(self.crash)
	self:sort_lane(self.bass_lane)
	self:sort_lane(self.stab)
	self:sort_lane(self.echo)
	-- the filters: as open as the section is loud, sweeping from shut to wide over the bar into a big hit
	local bright = self.bright
	for s3 = 0, steps - 1 do
		local tb2 = self:to_big(s3)
		local open = tb2 <= 16 and 0.2 + 0.8 * (16 - tb2) / 16.0 or 0.3 + 0.55 * energy_bar[s3 // 16 + 1]
		bright[s3 + 1] = self:is_big(s3) and 127 or itrunc(open * 127.0)
	end
	-- the sidechain: every kick ducks the chords and they swell back over the beat
	local duck = self.duck
	for s2 = 1, steps do duck[s2] = 127 end
	local depth = 52.0 + mood.energy * 24.0
	local shape = {1.0, 0.6, 0.28, 0.08}
	local kick = self.kick
	for ki = 1, #kick.at do
		for j = 0, 3 do
			local at = kick.at[ki] + j
			if at >= steps then break end
			local v6 = 127 - itrunc(depth * shape[j + 1] * kick.vel[ki] / 127.0)
			if v6 < duck[at + 1] then duck[at + 1] = v6 end
		end
	end
	local gate = self.gate
	for s4 = 0, steps - 1 do
		local chopped = energy_bar[s4 // 16 + 1] > 0.7 and mood.style ~= 2 and mood.style ~= 3 and self:to_big(s4) > 16
		gate[s4 + 1] = chopped and duck[s4 + 1] * MUSIC_GATE[s4 % 16 + 1] // 127 or duck[s4 + 1]
	end
end

-- the chord's root an octave down, kept at E1 or above where a synth bass still speaks
function Cue:bass_root(croot) return croot - 12 < 28 and croot or croot - 12 end

-- `turning`: the bar is the chord's last, and the four-on-the-floor styles walk its last two
-- sixteenths through the fifth and the seventh into the next chord
function Cue:bass(s, st, e, croot, turning)
	local lo = self:bass_root(croot)
	local style = self.mood.style
	local lane = self.bass_lane
	if (style == 0 or style == 4) and turning and st >= 14 and e > 0.6 then
		lane:add(s, 104, lo + (st == 14 and 7 or 10), 1)
	elseif style == 0 then
		-- outrun: the root rolling in sixteenths, the octave bouncing on every off beat; eighths while the section is low
		local groove = {116, 72, 100, 80}
		if e > 0.6 or st % 2 == 0 then lane:add(s, groove[st % 4 + 1], lo + (st % 4 == 2 and 12 or 0), 1) end
	elseif style == 4 then
		-- driving: a gallop, an eighth and two sixteenths a beat, the octave on the last at the peaks
		if st % 4 ~= 1 then lane:add(s, st % 4 == 0 and 118 or (st % 4 == 2 and 88 or 98), lo + (st % 4 == 3 and e > 0.7 and 12 or 0), st % 4 == 0 and 2 or 1) end
	elseif style == 1 then
		if st == 0 or st == 3 or st == 6 or st == 10 or st == 12 or st == 14 then lane:add(s, st == 0 and 116 or 94, lo + ((st == 6 or st == 14) and 12 or 0), st == 0 and 3 or 1) end
	elseif style == 2 then
		if st == 0 or st == 6 or st == 8 or st == 14 then lane:add(s, (st == 0 or st == 8) and 112 or 88, lo + (st == 14 and 12 or 0), (st == 0 or st == 8) and 5 or 2) end
	else
		-- ostinato: eighths on the root, the octave on the last of each half bar
		if st % 2 == 0 then lane:add(s, 70 + itrunc(e * 36.0) + (st % 8 == 0 and 10 or 0), lo + (st % 8 == 6 and 12 or 0), 1) end
	end
end

function Cue:drums(s, st, e, tb)
	local style = self.mood.style
	local four = style == 0 or style == 4
	local gap = tb <= 4
	if (e > 0.4 or st == 0) and not gap then
		local k = false
		if four then k = st % 4 == 0 or (style == 4 and st == 14 and e > 0.8)
		elseif style == 1 then k = st == 0 or st == 6 or st == 10
		elseif style == 2 then k = st == 0 or st == 10
		else k = (st == 0 and s % 32 == 0) or (st == 8 and e > 0.6) end
		if k then self.kick:add(s, 118, 36, 1) end
	end
	-- the backbeat: a gated snare and a clap together
	if e > 0.45 and not gap then
		local sn = false
		if four or style == 1 then sn = st == 4 or st == 12
		elseif style == 2 then sn = st == 8
		else sn = st == 8 and (s // 16) % 2 == 1 end
		if sn then
			self.snare:add(s, 112, 38, 1)
			if e > 0.55 then self.snare:add(s, 96, 39, 1) end
		end
		if style == 1 and (st == 7 or st == 15) and e > 0.6 then self.snare:add(s, 52, 38, 1) end
	end
	-- into a big hit: a snare roll climbing through the last half bar, through the silence
	if tb <= 8 then self.snare:add(s, 64 + (8 - tb) * 8, 38, 1) end
	-- a fill into every fourth bar: the toms falling through the last beat, through the last two into every eighth
	local from = (s // 16) % 8 == 7 and 8 or 12
	local fill = tb > 16 and e > 0.5 and (s // 16) % 4 == 3 and st >= from
	if fill then
		local toms = {50, 50, 48, 50, 50, 48, 47, 45}
		self.snare:add(s, 84 + (st - 8) * 5, toms[st - 7] - ((from == 8 and st >= 12) and 2 or 0), 1)
	end
	if e > 0.3 and not gap then
		local sixteen = style == 4 or e > 0.85
		local open = e > 0.6 and st % 4 == 2 and style ~= 3
		if open then self.hat:add(s, 70, 46, 1)
		elseif sixteen or st % 2 == 0 then self.hat:add(s, st % 4 == 0 and 74 or 50, 42, 1) end
	end
	-- a crash on the cut, on the scene's first full bar and on every fourth bar at the peaks
	if s == 0 and self.mood.cut_crash then self.crash:add(s, 108, 49, 1) end
	if st == 0 and s > 0 and (s == 16 or (s % 64 == 0 and e > 0.7)) and not gap then self.crash:add(s, s == 16 and 116 or 100, 49, 1) end
end

-- insertion order is time order for every lane but the ones the hits and echoes add to afterwards
function Cue:sort_lane(lane)
	local at, vel, note, len = lane.at, lane.vel, lane.note, lane.len
	for i = 2, #at do
		local j = i
		while j > 1 and at[j - 1] > at[j] do
			at[j], at[j - 1] = at[j - 1], at[j]
			vel[j], vel[j - 1] = vel[j - 1], vel[j]
			note[j], note[j - 1] = note[j - 1], note[j]
			len[j], len[j - 1] = len[j - 1], len[j]
			j = j - 1
		end
	end
end

-- each scene's mood, by its place in the reel: the keys travel, so no two neighbours share one
-- {progression, drum style, tonic, kit, bass, pad, arpeggio, lead, base energy, bars a chord}
local MOODS = {
	[0] = {0, 0, 45, KIT_POWER, 39, 90, 81, 81, 0.76, 2},          -- EIGHT: A minor night drive, the reel's theme
	{1, 0, 38, KIT_POWER, 38, 90, 80, 84, 0.78, 2},                -- DONUT: D minor outrun, charang lead
	{4, 4, 40, KIT_ELECTRONIC, 38, 95, 81, 81, 0.85, 1},           -- JULIA: E minor dark drive, the gallop
	{6, 2, 41, KIT_808, 38, 89, 98, 80, 0.64, 2},                  -- LAVALAMP: F dorian, half time, crystal
	{5, 0, 43, KIT_POWER, 39, 90, 81, 81, 0.78, 2},                -- PIPEDREAM: G minor chase, outrun saw
	{0, 2, 47, KIT_808, 38, 89, 98, 82, 0.58, 2},                  -- BOIDS: B minor night drive, half time
	{3, 4, 36, KIT_ELECTRONIC, 39, 95, 87, 30, 0.85, 1},           -- GLOBHUNTER: C phrygian, distortion lead
	{2, 0, 46, KIT_POWER, 39, 50, 81, 84, 0.76, 2},                -- CANYONRUN: B flat minor neon, synth strings
	{1, 0, 42, KIT_ELECTRONIC, 38, 89, 11, 81, 0.72, 2},           -- CHROME: F sharp minor outrun, vibes arpeggio
	{3, 3, 37, KIT_808, 38, 91, 14, 81, 0.5, 2},                   -- OSSUARY: C sharp phrygian, an ostinato under choir and bells
	{4, 1, 44, KIT_808, 38, 94, 81, 80, 0.76, 2},                  -- MAELSTROM: A flat minor dark drive, breakbeat
	{7, 0, 40, KIT_POWER, 39, 90, 81, 81, 0.82, 2},                -- FINALE: E triumph, the send-off
}

function music_mood(idx)
	local m = MOODS[idx]
	if m then return Mood.new(m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10]) end
	-- the cold open: F minor dark drive, the gallop, starting from silence
	local result = Mood.new(4, 4, 41, KIT_ELECTRONIC, 39, 95, 81, 81, 0.82, 1)
	result.cut_crash = false
	return result
end
