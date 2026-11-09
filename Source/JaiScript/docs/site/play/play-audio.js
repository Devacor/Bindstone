// Plays what a script's Sound does, with Web Audio: WAV and MP3 through the browser's decoder, MIDI through a small General
// MIDI synth (a voice per instrument family, synthesized drums on channel 10). One master gain carries the page's volume
// and mute, remembered between visits.
"use strict";

const JaiAudio = (() => {
	const OP = { load: 0, play: 1, stop: 2, volume: 3, release: 4, stopAll: 5 };
	const FORMAT = { wav: 1, midi: 2, mp3: 3 };
	const LOOKAHEAD = 0.6;
	const STORE = "jai-play-audio";

	let context = null;
	let master = null;
	const settings = { volume: 0.7, muted: false };
	try { Object.assign(settings, JSON.parse(localStorage.getItem(STORE) || "{}")); } catch (error) { /* defaults */ }
	const sounds = new Map();
	let noise = null;

	function save() {
		try { localStorage.setItem(STORE, JSON.stringify(settings)); } catch (error) { /* not remembered */ }
	}

	function applyMaster() {
		if (master) { master.gain.setTargetAtTime(settings.muted ? 0 : settings.volume * settings.volume, context.currentTime, 0.02); }
	}

	// Called from a click: browsers start audio only after the page's user acts
	function unlock() {
		if (!context) {
			const Context = window.AudioContext || window.webkitAudioContext;
			if (!Context) { return; }
			context = new Context();
			master = context.createGain();
			master.connect(context.destination);
			applyMaster();
		}
		if (context.state === "suspended") { context.resume(); }
	}

	function noiseBuffer() {
		if (!noise) {
			noise = context.createBuffer(1, context.sampleRate, context.sampleRate);
			const data = noise.getChannelData(0);
			for (let i = 0; i < data.length; i += 1) { data[i] = Math.random() * 2 - 1; }
		}
		return noise;
	}

	// ------------------------------------------------------------------------------------------------ MIDI file

	function parseMidi(bytes) {
		const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
		let at = 0;
		const tag = () => String.fromCharCode(bytes[at], bytes[at + 1], bytes[at + 2], bytes[at + 3]);
		if (tag() === "RIFF") { at = 20; }
		if (tag() !== "MThd") { return null; }
		const headerLength = view.getUint32(at + 4);
		const division = view.getUint16(at + 12);
		at += 8 + headerLength;
		const events = [];
		while (at + 8 <= bytes.length) {
			const kind = tag();
			const length = view.getUint32(at + 4);
			let p = at + 8;
			const end = Math.min(bytes.length, p + length);
			at = end;
			if (kind !== "MTrk") { continue; }
			let tick = 0;
			let status = 0;
			const varint = () => {
				let value = 0;
				for (let i = 0; i < 4 && p < end; i += 1) {
					const b = bytes[p++];
					value = (value << 7) | (b & 0x7f);
					if (!(b & 0x80)) { break; }
				}
				return value;
			};
			while (p < end) {
				tick += varint();
				let b = bytes[p];
				if (b & 0x80) { status = b; p += 1; } else { b = status; }
				const type = status & 0xf0;
				if (status === 0xff) {
					const meta = bytes[p++];
					const size = varint();
					if (meta === 0x51 && size === 3) { events.push({ tick, tempo: (bytes[p] << 16) | (bytes[p + 1] << 8) | bytes[p + 2] }); }
					p += size;
				} else if (status === 0xf0 || status === 0xf7) {
					p += varint();
				} else if (type === 0xc0 || type === 0xd0) {
					events.push({ tick, type, channel: status & 15, a: bytes[p++] });
				} else {
					events.push({ tick, type, channel: status & 15, a: bytes[p], b: bytes[p + 1] });
					p += 2;
				}
			}
		}
		events.sort((x, y) => x.tick - y.tick);
		// ticks to seconds through the tempo map, then notes paired on and off
		let tempo = 500000;
		let lastTick = 0;
		let seconds = 0;
		const notes = [];
		const open = new Map();
		const program = new Array(16).fill(0);
		const volume = new Array(16).fill(1);
		for (const event of events) {
			seconds += ((event.tick - lastTick) * tempo) / (division * 1e6);
			lastTick = event.tick;
			if (event.tempo) { tempo = event.tempo; continue; }
			const key = event.channel * 128 + event.a;
			if (event.type === 0xc0) { program[event.channel] = event.a; }
			else if (event.type === 0xb0 && event.a === 7) { volume[event.channel] = event.b / 127; }
			else if (event.type === 0x90 && event.b > 0) {
				const note = { time: seconds, length: 0.25, channel: event.channel, note: event.a,
					velocity: (event.b / 127) * volume[event.channel], program: program[event.channel] };
				notes.push(note);
				if (!open.has(key)) { open.set(key, []); }
				open.get(key).push(note);
			} else if (event.type === 0x80 || (event.type === 0x90 && event.b === 0)) {
				const pending = open.get(key);
				if (pending && pending.length) {
					const note = pending.shift();
					note.length = Math.max(0.02, seconds - note.time);
				}
			}
		}
		return { notes, length: Math.max(seconds, notes.length ? notes[notes.length - 1].time + 0.25 : 0) };
	}

	// ------------------------------------------------------------------------------------------------ synth

	const frequency = (note) => 440 * Math.pow(2, (note - 69) / 12);

	function envelope(gain, time, peak, attack, hold, release) {
		gain.gain.setValueAtTime(0.0001, time);
		gain.gain.linearRampToValueAtTime(peak, time + attack);
		gain.gain.setValueAtTime(peak, time + Math.max(attack, hold));
		gain.gain.exponentialRampToValueAtTime(0.0001, time + Math.max(attack, hold) + release);
		return time + Math.max(attack, hold) + release;
	}

	function tone(out, time, note, velocity, length, program) {
		const family = program >> 3;
		let wave = "sawtooth";
		let attack = 0.01;
		let release = 0.12;
		let peak = 0.18 * velocity;
		let cutoff = 4000;
		let detune = 0;
		let decaying = false;
		if (program === 55) { return hit(out, time, note, velocity); }
		if (program >= 112 && program <= 119) { return swell(out, time, velocity, length); }
		switch (family) {
			case 0: wave = "triangle"; decaying = true; release = 0.4; break;
			case 1: wave = "sine"; decaying = true; release = 0.6; break;
			case 2: wave = "square"; peak *= 0.6; break;
			case 3: wave = "sawtooth"; decaying = true; release = 0.3; cutoff = 2500; break;
			case 4: wave = "sawtooth"; cutoff = 900; release = 0.08; peak *= 1.3; break;
			case 5: case 6: wave = "sawtooth"; attack = 0.12; release = 0.4; detune = 7; cutoff = 2800; peak *= 0.7; break;
			case 7: wave = "sawtooth"; attack = 0.04; cutoff = 2200; break;
			case 8: case 9: wave = "square"; peak *= 0.6; cutoff = 3500; break;
			case 10: wave = "sawtooth"; detune = 5; cutoff = 5000; peak *= 0.7; break;
			case 11: wave = "sawtooth"; attack = 0.3; release = 0.8; detune = 9; cutoff = 1800; peak *= 0.6; break;
			default: wave = "sine"; break;
		}
		const filter = context.createBiquadFilter();
		filter.type = "lowpass";
		filter.frequency.value = cutoff;
		const gain = context.createGain();
		filter.connect(gain);
		gain.connect(out);
		const hold = decaying ? 0.01 : length;
		const end = envelope(gain, time, peak, attack, hold, decaying ? release + length * 0.5 : release);
		for (const cents of detune ? [-detune, detune] : [0]) {
			const osc = context.createOscillator();
			osc.type = wave;
			osc.frequency.value = frequency(note);
			osc.detune.value = cents;
			osc.connect(filter);
			osc.start(time);
			osc.stop(end + 0.05);
		}
		return end;
	}

	function burst(out, time, length, peak, type, frequencyHz, q) {
		const source = context.createBufferSource();
		source.buffer = noiseBuffer();
		const filter = context.createBiquadFilter();
		filter.type = type;
		filter.frequency.value = frequencyHz;
		filter.Q.value = q || 0.7;
		const gain = context.createGain();
		source.connect(filter);
		filter.connect(gain);
		gain.connect(out);
		const end = envelope(gain, time, peak, 0.002, 0.005, length);
		source.start(time);
		source.stop(end + 0.05);
		return end;
	}

	function drop(out, time, from, to, length, peak) {
		const osc = context.createOscillator();
		osc.type = "sine";
		osc.frequency.setValueAtTime(from, time);
		osc.frequency.exponentialRampToValueAtTime(to, time + length * 0.6);
		const gain = context.createGain();
		osc.connect(gain);
		gain.connect(out);
		const end = envelope(gain, time, peak, 0.002, 0.01, length);
		osc.start(time);
		osc.stop(end + 0.05);
		return end;
	}

	function drum(out, time, note, velocity) {
		const v = velocity;
		if (note === 35 || note === 36) { return drop(out, time, 160, 45, 0.35, 0.9 * v); }
		if (note === 38 || note === 40 || note === 37) {
			drop(out, time, 220, 150, 0.12, 0.3 * v);
			return burst(out, time, 0.18, 0.45 * v, "bandpass", 2200, 0.8);
		}
		if (note === 39) { return burst(out, time, 0.15, 0.5 * v, "bandpass", 1500, 1.2); }
		if (note === 42 || note === 44) { return burst(out, time, 0.05, 0.25 * v, "highpass", 8000); }
		if (note === 46) { return burst(out, time, 0.3, 0.22 * v, "highpass", 7000); }
		if (note === 49 || note === 57 || note === 52 || note === 55) { return burst(out, time, 1.4, 0.25 * v, "highpass", 5000); }
		if (note === 51 || note === 59 || note === 53) { return burst(out, time, 0.7, 0.15 * v, "bandpass", 6000, 1.5); }
		if (note >= 41 && note <= 50) { return drop(out, time, 60 + (note - 41) * 18, 40 + (note - 41) * 10, 0.3, 0.6 * v); }
		return burst(out, time, 0.08, 0.2 * v, "bandpass", 3000);
	}

	function hit(out, time, note, velocity) {
		burst(out, time, 0.4, 0.3 * velocity, "lowpass", 3000);
		let end = time;
		for (const step of [0, 7, 12, 19]) { end = tone(out, time, note + step, velocity * 0.6, 0.05, 48); }
		return end;
	}

	function swell(out, time, velocity, length) {
		const source = context.createBufferSource();
		source.buffer = noiseBuffer();
		source.loop = true;
		const filter = context.createBiquadFilter();
		filter.type = "highpass";
		filter.frequency.value = 4000;
		const gain = context.createGain();
		source.connect(filter);
		filter.connect(gain);
		gain.connect(out);
		const span = Math.max(0.3, length);
		gain.gain.setValueAtTime(0.0001, time);
		gain.gain.exponentialRampToValueAtTime(0.3 * velocity, time + span);
		gain.gain.linearRampToValueAtTime(0.0001, time + span + 0.05);
		source.start(time);
		source.stop(time + span + 0.1);
		return time + span;
	}

	// ------------------------------------------------------------------------------------------------ sounds

	function stopVoices(sound) {
		if (sound.source) { try { sound.source.stop(); } catch (error) { /* already ended */ } sound.source = null; }
		if (sound.timer) { clearInterval(sound.timer); sound.timer = 0; }
		if (sound.bus) {
			sound.bus.gain.setTargetAtTime(0, context.currentTime, 0.01);
			const bus = sound.bus;
			setTimeout(() => bus.disconnect(), 200);
			sound.bus = null;
		}
		sound.playing = false;
	}

	function newBus(sound) {
		const bus = context.createGain();
		bus.gain.value = sound.volume;
		bus.connect(master);
		sound.bus = bus;
		return bus;
	}

	function finished(sound) {
		sound.playing = false;
		if (sound.released) { sounds.delete(sound.id); }
	}

	function playBuffer(sound) {
		const bus = newBus(sound);
		const source = context.createBufferSource();
		source.buffer = sound.buffer;
		source.loop = sound.loop;
		source.connect(bus);
		source.onended = () => { if (sound.source === source) { finished(sound); } };
		source.start();
		sound.source = source;
	}

	function playMidi(sound) {
		const bus = newBus(sound);
		const song = sound.midi;
		if (!song.notes.length) { finished(sound); return; }
		let origin = context.currentTime + 0.05;
		let next = 0;
		const schedule = () => {
			const horizon = context.currentTime + LOOKAHEAD;
			while (true) {
				if (next >= song.notes.length) {
					if (!sound.loop) {
						if (context.currentTime > origin + song.length) { stopVoices(sound); finished(sound); }
						return;
					}
					origin += song.length;
					next = 0;
				}
				const note = song.notes[next];
				const time = origin + note.time;
				if (time > horizon) { return; }
				next += 1;
				if (note.channel === 9) { drum(bus, time, note.note, note.velocity); }
				else { tone(bus, time, note.note, note.velocity, note.length, note.program); }
			}
		};
		schedule();
		sound.timer = setInterval(schedule, 150);
	}

	function start(sound) {
		if (!context || !sound.ready) { sound.pending = true; return; }
		stopVoices(sound);
		sound.playing = true;
		if (sound.midi) { playMidi(sound); } else if (sound.buffer) { playBuffer(sound); }
	}

	function handle(op, id, bytes, value) {
		if (op === OP.stopAll) {
			for (const sound of sounds.values()) { stopVoices(sound); sound.pending = false; }
			return;
		}
		if (op === OP.load) {
			const sound = { id, volume: 1, loop: false, ready: false, pending: false, playing: false, released: false };
			sounds.set(id, sound);
			if (!context) { unlock(); }
			if (!context || !bytes) { return; }
			if (value === FORMAT.midi) {
				sound.midi = parseMidi(bytes);
				sound.ready = !!sound.midi;
				if (sound.ready && sound.pending) { sound.pending = false; start(sound); }
			} else {
				context.decodeAudioData(bytes.buffer).then((buffer) => {
					sound.buffer = buffer;
					sound.ready = true;
					if (sound.pending) { sound.pending = false; start(sound); }
				}, () => { /* not a format this browser decodes: silent */ });
			}
			return;
		}
		const sound = sounds.get(id);
		if (!sound) { return; }
		if (op === OP.play) { sound.loop = value !== 0; start(sound); }
		else if (op === OP.stop) { sound.pending = false; stopVoices(sound); }
		else if (op === OP.volume) {
			sound.volume = value;
			if (sound.bus) { sound.bus.gain.setTargetAtTime(value, context.currentTime, 0.02); }
		} else if (op === OP.release) {
			sound.released = true;
			if (value === 0 || !sound.playing) { stopVoices(sound); sounds.delete(id); }
		}
	}

	function reset() {
		for (const sound of sounds.values()) { if (context) { stopVoices(sound); } }
		sounds.clear();
	}

	// ------------------------------------------------------------------------------------------------ controls

	// A speaker button that mutes and a slider for the volume, wired into a container on the page
	function controls(container) {
		const button = document.createElement("button");
		button.type = "button";
		button.className = "btn play-mute";
		const slider = document.createElement("input");
		slider.type = "range";
		slider.min = "0";
		slider.max = "100";
		slider.className = "play-volume";
		slider.setAttribute("aria-label", "Volume");
		const speaker = '<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true"><path fill="currentColor" d="M4 9h4l5-4v14l-5-4H4z"/>';
		const waves = '<path fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" d="M16 9a4 4 0 0 1 0 6M18.5 6.5a7.5 7.5 0 0 1 0 11"/></svg>';
		const cross = '<path fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" d="M16 9l5 6M21 9l-5 6"/></svg>';
		const show = () => {
			const silent = settings.muted || settings.volume === 0;
			button.innerHTML = speaker + (silent ? cross : waves);
			button.setAttribute("aria-label", settings.muted ? "Unmute" : "Mute");
			button.setAttribute("aria-pressed", settings.muted ? "true" : "false");
			button.title = settings.muted ? "Unmute" : "Mute";
			slider.value = String(Math.round(settings.volume * 100));
		};
		button.addEventListener("click", () => {
			settings.muted = !settings.muted;
			unlock();
			applyMaster();
			save();
			show();
		});
		slider.addEventListener("input", () => {
			settings.volume = Number(slider.value) / 100;
			if (settings.volume > 0) { settings.muted = false; }
			unlock();
			applyMaster();
			save();
			show();
		});
		show();
		container.append(button, slider);
	}

	return { handle, unlock, reset, controls };
})();
