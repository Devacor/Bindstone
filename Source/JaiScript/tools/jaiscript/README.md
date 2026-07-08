# jaiscript — the standalone runner / REPL

`jaiscript.exe` is JaiScript's `python.exe`: run a script from the command line, type code
into a live session, or point it at a file and re-run on every save. It is also the
**reference embedding** — the host is deliberately thin, and every engine call in
`main.cpp` is the canonical shape from `docs/site/embedding`.

Built by the `jaiscript_runner` CMake target (on by default, `JAISCRIPT_BUILD_RUNNER`);
the binary lands at `out/build/<config>/bin/jaiscript.exe`.

## Mode 1 — file runner

```
jaiscript file.jai [options] [-- args...]
```

Runs the script and exits. The exit code is the script's result if it is an int
(a trailing expression works: end your script with `some_int_expr`), otherwise 0;
script errors print message + stack trace to stderr and exit 1; usage errors exit 2.

- `include`/`import` resolve relative to the script's own directory.
- Everything after `--` is the global `ARGS` (array of strings).
- **Drag-drop a `.jai` file onto the exe works** — and when the console window is
  exclusively ours (drag-drop / double-click launch) the runner pauses on
  "press Enter to close" so the output is readable. `--pause` / `--no-pause` override.
- A UTF-8 BOM (Notepad's default) is stripped before parsing.

## Mode 2 — REPL

```
jaiscript
```

A persistent engine: globals, functions and classes accumulate across lines.

```
JaiScript 0.1.0 - vm backend, stdlib loaded, budget off. :help for commands.
debugger: DAP listening on 127.0.0.1:52477 (VS Code: Attach to JaiScript)
jai> int hp = 100;
jai> hp - 58
42
jai> int add(int x, int y) {
...> return x + y;
...> }
jai> add(4, 5)
9
```

- **Multi-line**: input keeps collecting while `()`, `[]`, `{}`, `/* */` or a string is
  open (the prompt becomes `...>`); a blank line force-submits what you have.
- **Auto-print**: a non-null result prints. Strings print quoted (`"5"` vs `5` stay
  distinguishable), chars quoted with `'`, arrays/maps as compact JSON
  (`[1,2,3]`, `{"k":7}`); `print(...)` and other null-returning statements print nothing.
- **Errors don't kill the session** — the message prints and the engine stays usable.
- **Paste/drop a path**: a line that is a (optionally quoted) path to an existing `.jai`
  file runs that file into the session — this is exactly what dropping a file onto an
  open console window produces.
- **Piping works**: `echo 1+1 | jaiscript` (prompts and banner only print on a real console).

Meta-commands (`:help` inside):

| command | effect |
|---|---|
| `:load <path>` | run a `.jai` file into this session |
| `:reset` | fresh engine (keeps backend, budget, debugger) |
| `:backend vm\|interp` | switch backend — resets the engine (backends can't swap mid-engine) |
| `:time on\|off` | print wall time per execute |
| `:quit` / `:exit` | leave (Ctrl+Z+Enter or Ctrl+C also work) |

## Mode 3 — live-coding watch

```
jaiscript --watch file.jai
```

Runs the file, then re-runs it every time you save — **edit in your editor, save, see
output**. The console clears between runs and a status line reports run #, duration,
ok/ERROR and the time. Ctrl+C quits. Each run gets a **fresh engine** (deterministic
world per save); because of that, the DAP listener is off in watch mode.

## Options

```
--backend=vm|interp   execution backend (default vm)
--budget=N            execution budget in seconds per execute (default 0 = unlimited;
                      the embedded-engine default of 1.0s is deliberately lifted —
                      long loops are the user's business in a runner)
--time                print wall time for each run
--no-debug            don't stand up the DAP debug listener
--debug-port=N        first port the listener tries (default 52477, probing +10)
--pause / --no-pause  force / suppress the exit hold after a file run
--selftest            built-in smoke tests (balancer, file mode, scripted REPL session)
--version / --help
```

## Environment the runner provides

The full JaiScript stdlib is always registered (`jai::stdlib::register_all`): formatted
`print` / `format` / `to_string` / `type_of`, math, `to_json` / `from_json`, container
helpers. (The bare engine only ships concatenating `print`, `thread_count`,
`parallel_transform` and the builtin container/string methods — the runner opts into
everything.) On top, the host binds file IO so scripts can actually save things:

```
read_file(path) -> string      write_file(path, text) -> bool
file_exists(path) -> bool      delete_file(path) -> bool
```

plus `ARGS` (strings after `--`).

## Debugging

A DAP listener is on by default (localhost only, costs nothing until a session attaches) in
file and REPL modes — you can attach VS Code to a *running REPL session* and set breakpoints
in files you `:load`. Install `tools/vscode-jaiscript`, F5 → "Attach to JaiScript" with the
logged port. Opt out with `--no-debug`.
