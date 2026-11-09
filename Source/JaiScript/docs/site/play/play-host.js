// Runs JaiScript programs on a page: a terminal (xterm.js) plus the engine as WebAssembly. Output's bare newlines become
// CRLF, as a tty's output processing makes them. Where the page is cross-origin
// isolated the engine runs in a worker (play-worker.js) and sleeps for real; elsewhere it runs on this thread through the
// Asyncify build, which hands control back to the page at every sleep.
"use strict";

const JaiPlay = (() => {
	const RING = 1024;
	const NAMED = { " ": 128, ArrowLeft: 129, ArrowRight: 130, ArrowUp: 131, ArrowDown: 132, Shift: 133, Control: 134,
		Alt: 135, Escape: 136, Enter: 137, Tab: 138, Backspace: 139 };
	const GAME_KEYS = new Set([" ", "ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Tab", "Backspace", "Enter"]);
	const isolated = typeof SharedArrayBuffer !== "undefined" && self.crossOriginIsolated === true;

	function keyCode(event) {
		if (event.key in NAMED) { return NAMED[event.key]; }
		if (event.key.length === 1) {
			const code = event.key.toLowerCase().charCodeAt(0);
			if (code > 32 && code < 127) { return code; }
		}
		return -1;
	}

	let mainThreadModule = null;
	function loadMainThreadEngine() {
		if (!mainThreadModule) {
			mainThreadModule = new Promise((resolve, reject) => {
				const script = document.createElement("script");
				script.src = "engine/jaiscript_web.js";
				script.onload = () => resolve();
				script.onerror = () => reject(new Error("cannot load the engine"));
				document.head.appendChild(script);
			});
		}
		return mainThreadModule;
	}

	class Session {
		// options: cols, rows, scrollback, onExit(code), onStatus(text)
		constructor(element, options) {
			this.element = element;
			this.options = options;
			this.decoder = new TextDecoder();
			this.held = new Map();
			this.terminal = new Terminal({
				cols: options.cols, rows: options.rows, scrollback: options.scrollback || 0, convertEol: true,
				disableStdin: true, cursorBlink: false, fontFamily: '"JetBrains Mono", Menlo, Consolas, monospace',
				fontSize: 14, theme: { background: "#05060a" },
			});
			this.terminal.open(element);
			try { this.terminal.loadAddon(new WebglAddon.WebglAddon()); } catch (error) { /* the DOM renderer draws instead */ }
			this.fit();
			window.addEventListener("resize", () => this.fit());
			element.tabIndex = 0;
			// capture: xterm's own key handler stops the event before it bubbles
			element.addEventListener("keydown", (event) => this.key(event, true), true);
			element.addEventListener("keyup", (event) => this.key(event, false), true);
			element.addEventListener("blur", () => this.releaseKeys());
			this.status(isolated ? "engine: worker" : "engine: page thread (slower: this page is not cross-origin isolated)");
		}

		status(text) { if (this.options.onStatus) { this.options.onStatus(text); } }

		// The font size that fits the columns across the stage and the rows into most of the window
		fit() {
			const width = this.element.parentElement.clientWidth - 16;
			const height = window.innerHeight * 0.82;
			const across = width / (this.options.cols * 0.6);
			const down = this.options.fitHeight === false ? across : height / (this.options.rows * 1.2);
			const size = Math.max(5, Math.min(18, Math.floor(Math.min(across, down))));
			if (this.terminal.options.fontSize !== size) { this.terminal.options.fontSize = size; }
		}

		write(bytes) { this.terminal.write(bytes); }

		key(event, down) {
			if (!this.running) { return; }
			const code = down ? keyCode(event) : this.held.get(event.code);
			if (code === undefined || code < 0) { return; }
			if (GAME_KEYS.has(event.key) || event.key.length === 1) { event.preventDefault(); }
			if (down) { this.held.set(event.code, code); } else { this.held.delete(event.code); }
			this.send(code, down);
		}

		releaseKeys() {
			for (const code of this.held.values()) { this.send(code, false); }
			this.held.clear();
		}

		send(code, down) {
			if (this.words) {
				const n = Atomics.load(this.words, 3);
				Atomics.store(this.words, 16 + (n % RING), code | ((down ? 1 : 0) << 16));
				Atomics.store(this.words, 3, n + 1);
			} else if (this.module) {
				this.module._jaiweb_key(code, down ? 1 : 0);
			}
		}

		finished(code) {
			this.running = false;
			this.stopLoop();
			if (this.options.onExit) { this.options.onExit(code); }
		}

		// game: a bundle URL ({entry, files}); source: program text run under budget seconds
		async start({ game, source, budget = 0 }) {
			this.stop();
			this.terminal.reset();
			this.running = true;
			this.element.focus();
			if (isolated) {
				this.words = new Int32Array(new SharedArrayBuffer(4 * (16 + RING)));
				this.words[1] = this.options.cols;
				this.words[2] = this.options.rows;
				this.worker = new Worker("play-worker.js");
				this.worker.onmessage = (message) => {
					const data = message.data;
					if (data.out) { this.write(data.out); }
					if (data.exit !== undefined) { this.finished(data.exit); }
					if (data.failed !== undefined) { this.write("\r\n" + data.failed + "\r\n"); this.finished(1); }
				};
				this.worker.postMessage({ shared: this.words.buffer, game, source, budget });
				return;
			}
			await loadMainThreadEngine();
			const module = await createJaiScript({ locateFile: (path) => "engine/" + path, jaiwebWrite: (bytes) => this.write(bytes) });
			this.module = module;
			module._jaiweb_resize(this.options.cols, this.options.rows);
			if (game) {
				const bundle = await (await fetch(game)).json();
				for (const [path, text] of Object.entries(bundle.files)) {
					const full = "/game/" + path;
					module.FS.mkdirTree(full.slice(0, full.lastIndexOf("/")));
					module.FS.writeFile(full, text);
				}
				module.FS.chdir("/game");
				module.ccall("jaiweb_start_file", null, ["string", "string"], [bundle.entry, (bundle.args || []).join(String.fromCharCode(10))]);
			} else {
				module.ccall("jaiweb_start_source", null, ["string", "number"], [source, budget]);
			}
			// a call that switches fibers returns early under Asyncify: ask whether the run goes on after it
			const tick = () => {
				this.timer = 0;
				if (module._jaiweb_running()) { module._jaiweb_tick(); }
				if (!module._jaiweb_running()) { this.finished(module._jaiweb_exit_code()); return; }
				const wait = module._jaiweb_wait_ms();
				this.timer = wait > 1 ? setTimeout(tick, wait) : setTimeout(tick, 0);
			};
			tick();
		}

		stopLoop() {
			if (this.timer) { clearTimeout(this.timer); this.timer = 0; }
		}

		stop() {
			this.stopLoop();
			if (this.worker) { this.worker.terminate(); this.worker = null; }
			if (this.module) { this.module._jaiweb_stop(); this.module = null; }
			this.words = null;
			this.held.clear();
			this.running = false;
		}
	}

	return { Session, isolated };
})();
