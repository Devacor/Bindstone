// The engine's worker: runs one game or one sandbox program to its end, off the page's thread in either mode.
// - shared (the page is cross-origin isolated and this worker can start the engine's threads, workers of its own): the
//   threaded worker build blocks in its run; sleep() waits in Atomics.wait, and the
//   page writes keys and the terminal size into an Int32Array they share:
//     [0] the word sleep() waits on, [1] columns, [2] rows, [3] key events written, [16 + n % RING] event n (code | down << 16)
// - otherwise: the Asyncify build runs one step per timer, every sleep() returns to this worker's event loop, and keys and
//   sizes arrive as messages between steps.
// Output goes back to the page as UTF-8 chunks and sound commands as {sound, bytes} (play-audio.js); {exit} ends the run.
"use strict";

const RING = 1024;

async function loadBundle(jai, game) {
	const response = await fetch(game);
	if (!response.ok) { throw new Error("cannot load " + game + " (" + response.status + ")"); }
	const bundle = await response.json();
	for (const [path, text] of Object.entries(bundle.files)) {
		const full = "/game/" + path;
		jai.FS.mkdirTree(full.slice(0, full.lastIndexOf("/")));
		jai.FS.writeFile(full, text);
	}
	jai.FS.chdir("/game");
	return { entry: bundle.entry, args: (bundle.args || []).join(String.fromCharCode(10)) };
}

const write = (bytes) => self.postMessage({ out: bytes }, [bytes.buffer]);
const sound = (op, id, bytes, value) => self.postMessage({ sound: [op, id, value], bytes }, bytes ? [bytes.buffer] : []);

async function runShared({ shared, game, source, budget }) {
	importScripts("engine/jaiscript_web_worker.js");
	const words = new Int32Array(shared);
	let read = 0;
	let cols = 0;
	let rows = 0;
	const jai = await createJaiScript({
		locateFile: (path) => "engine/" + path,
		jaiwebWrite: write,
		jaiwebSound: sound,
		jaiwebSleep: (ms) => { Atomics.wait(words, 0, 0, ms); },
		jaiwebPump: () => {
			const written = Atomics.load(words, 3);
			while (read < written) {
				const event = Atomics.load(words, 16 + (read % RING));
				jai._jaiweb_key(event & 0xffff, event >>> 16);
				read += 1;
			}
			const c = Atomics.load(words, 1);
			const r = Atomics.load(words, 2);
			if (c !== cols || r !== rows) {
				cols = c;
				rows = r;
				jai._jaiweb_resize(c, r);
			}
		},
	});
	if (game) {
		const { entry, args } = await loadBundle(jai, game);
		jai._jaiweb_resize(Atomics.load(words, 1), Atomics.load(words, 2));
		return jai.ccall("jaiweb_run_file", "number", ["string", "string"], [entry, args]);
	}
	return jai.ccall("jaiweb_run_source", "number", ["string", "number"], [source, budget]);
}

async function runStepped({ game, source, budget, cols, rows }) {
	importScripts("engine/jaiscript_web.js");
	const jai = await createJaiScript({ locateFile: (path) => "engine/" + path, jaiwebWrite: write, jaiwebSound: sound });
	self.onmessage = (message) => {
		const data = message.data;
		if (data.key !== undefined) { jai._jaiweb_key(data.key, data.down ? 1 : 0); }
		if (data.cols !== undefined) { jai._jaiweb_resize(data.cols, data.rows); }
	};
	jai._jaiweb_resize(cols, rows);
	if (game) {
		const { entry, args } = await loadBundle(jai, game);
		jai.ccall("jaiweb_start_file", null, ["string", "string"], [entry, args]);
	} else {
		jai.ccall("jaiweb_start_source", null, ["string", "number"], [source, budget]);
	}
	// a call that switches fibers returns early under Asyncify: ask whether the run goes on after it
	return new Promise((resolve) => {
		const step = () => {
			if (jai._jaiweb_running()) { jai._jaiweb_tick(); }
			if (!jai._jaiweb_running()) { resolve(jai._jaiweb_exit_code()); return; }
			setTimeout(step, Math.max(0, jai._jaiweb_wait_ms()));
		};
		step();
	});
}

// Whether this worker can start workers of its own, which the engine's threads are: answers {threads}
function probeThreads() {
	try {
		const inner = new Worker(self.location.href, { name: "jai-probe" });
		const answer = (ok) => { inner.terminate(); self.postMessage({ threads: ok }); };
		const timer = setTimeout(() => answer(false), 1500);
		inner.onmessage = () => { clearTimeout(timer); answer(true); };
		inner.onerror = () => { clearTimeout(timer); answer(false); };
	} catch (error) {
		self.postMessage({ threads: false });
	}
}

if (self.name === "em-pthread") {
	// the engine's threads start from the script of the worker that made them, this one: each runs the engine's thread
	importScripts("engine/jaiscript_web_worker.js");
} else if (self.name === "jai-probe") {
	self.postMessage("alive");
} else {
	self.onmessage = async (message) => {
		if (message.data.probe) { probeThreads(); return; }
		try {
			const code = message.data.shared ? await runShared(message.data) : await runStepped(message.data);
			self.postMessage({ exit: code });
		} catch (error) {
			self.postMessage({ failed: String(error && error.message || error) });
		}
	};
}
