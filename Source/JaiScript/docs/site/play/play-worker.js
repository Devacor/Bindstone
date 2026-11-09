// The engine's worker: runs one game or one sandbox program to its end. The page shares an Int32Array with it:
//   [0] the word sleep() waits on, [1] columns, [2] rows, [3] key events written, [16 + n % RING] event n (code | down << 16)
// Output goes back to the page as UTF-8 chunks; {exit} ends the run.
"use strict";
importScripts("engine/jaiscript_web_worker.js");

const RING = 1024;

self.onmessage = async (message) => {
	const { shared, game, source, budget } = message.data;
	const words = new Int32Array(shared);
	let read = 0;
	let cols = 0;
	let rows = 0;
	try {
		const jai = await createJaiScript({
			locateFile: (path) => "engine/" + path,
			jaiwebWrite: (bytes) => self.postMessage({ out: bytes }, [bytes.buffer]),
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
		let code;
		if (game) {
			const response = await fetch(game);
			if (!response.ok) { throw new Error("cannot load " + game + " (" + response.status + ")"); }
			const bundle = await response.json();
			for (const [path, text] of Object.entries(bundle.files)) {
				const full = "/game/" + path;
				jai.FS.mkdirTree(full.slice(0, full.lastIndexOf("/")));
				jai.FS.writeFile(full, text);
			}
			jai.FS.chdir("/game");
			jai._jaiweb_resize(Atomics.load(words, 1), Atomics.load(words, 2));
			code = jai.ccall("jaiweb_run_file", "number", ["string", "string"], [bundle.entry, (bundle.args || []).join(String.fromCharCode(10))]);
		} else {
			code = jai.ccall("jaiweb_run_source", "number", ["string", "number"], [source, budget]);
		}
		self.postMessage({ exit: code });
	} catch (error) {
		self.postMessage({ failed: String(error && error.message || error) });
	}
};
