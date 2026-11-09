// Runs a script on the WebAssembly build under Node, the way the page drives it. The script's folder is mounted as the
// working directory, so its imports and files resolve as they do natively.
//   node tools/jaiscript_web/run_node.mjs <build>/bin/jaiscript_web.js <script.jai> [script args...]
// The Asyncify build is driven like the main-thread page: one jaiweb_tick per frame, waiting out each sleep. A build without
// Asyncify (jaiscript_web_worker.js) runs the script to its end in one call, the way the worker page does.
import path from "node:path";
import { pathToFileURL } from "node:url";

const [moduleJs, script, ...scriptArgs] = process.argv.slice(2);
if (!moduleJs || !script) {
	console.error("usage: node run_node.mjs <jaiscript_web.js> <script.jai> [args...]");
	process.exit(2);
}

const { default: createJaiScript } = await import(pathToFileURL(path.resolve(moduleJs)).href);
const jai = await createJaiScript({ jaiwebWrite: bytes => process.stdout.write(bytes) });

const folder = path.dirname(path.resolve(script));
jai.FS.mkdir("/work");
jai.FS.mount(jai.NODEFS, { root: folder }, "/work");
jai.FS.chdir("/work");
jai.ccall("jaiweb_resize", null, ["number", "number"], [process.stdout.columns || 100, process.stdout.rows || 40]);

const name = path.basename(script);
const args = scriptArgs.join("\n");
const finish = code => process.stdout.write("", () => process.exit(code));

if (path.basename(moduleJs).includes("worker")) {
	finish(jai.ccall("jaiweb_run_file", "number", ["string", "string"], [name, args]));
} else {
	jai.ccall("jaiweb_start_file", null, ["string", "string"], [name, args]);
	const tick = () => {
		if (jai._jaiweb_running()) { jai._jaiweb_tick(); }
		if (!jai._jaiweb_running()) {
			finish(jai.ccall("jaiweb_exit_code", "number", [], []));
			return;
		}
		setTimeout(tick, jai.ccall("jaiweb_wait_ms", "number", [], []));
	};
	tick();
}
