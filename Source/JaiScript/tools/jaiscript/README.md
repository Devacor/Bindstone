# jaiscript — the standalone runner / REPL

`jaiscript.exe` is JaiScript's `python.exe`: run a script from the command line, type code
into a live session, or point it at a file and re-run on every save. It is also the
**reference embedding** — the host is deliberately thin, and every engine call in
`main.cpp` is the canonical shape from `docs/site/embedding`.

Built by the `jaiscript_runner` CMake target (on by default, `JAISCRIPT_BUILD_RUNNER`);
the binary lands in the build directory's `bin/` (`build/bin/jaiscript`; see `docs/BUILD.md`).

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
JaiScript 0.5.0-beta - vm backend, stdlib loaded, budget off. :help for commands.
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
--check=strict|warn|off
                      static checker mode (default strict: typed mistakes and undeclared
                      names fail the load; warn loads anyway and lists them on stderr;
                      off skips the walk's diagnostics)
--budget=N            execution budget in seconds per execute (default 0 = unlimited;
                      the embedded-engine default of 5.0s is deliberately lifted —
                      long loops are the user's business in a runner)
--time                print wall time for each run
--benchmark-main=N    execute the file as a warm-up, then call its repeatable main() N times
--disasm=<file>       write every chunk the VM compiles, disassembled with its regions' IR, to <file>
--manifest=<file>     write the same chunks' semantic manifests (every field, exact constant bits)
                      to <file>
--coverage=<file>     write the VM's execution-coverage census (cells / typed / general, the
                      reasons and their source sites) to <file> at exit (a build with
                      JAISCRIPT_VM_COVERAGE)
--miss-every=<n>      the VM misses at every nth plain cell instruction (a build with
                      JAISCRIPT_MISS_INJECTION)
--workers=<n>         the worker count of parallel_for, parallel_transform and parallel_reduce
                      (default: the engine's own)
--no-param-alias      bind every by-value parameter with a clone (the read-only elision off)
--no-local-alias      copy every by-value local declaration (the read-only local elision off)
--no-proofs           run without type proofs, typed ops or cell blocks (the general form)
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
`parallel_transform`, `parallel_reduce` and the builtin container/string methods — the runner opts into
everything.) On top, the host binds file IO so scripts can actually save things:

```
read_file(path) -> string      write_file(path, text) -> bool   (creates missing folders)
file_exists(path) -> bool      delete_file(path) -> bool
move_file(from, to) -> bool    (replaces `to`; false when `from` is not a file)
dir_exists(path) -> bool       make_dirs(path) -> bool           list_dir(path) -> array<string>
writable_dir(path) -> bool     (creates the folder if missing, writes and removes a probe)
sha256_file(path) -> string    zip_entries(zip) -> array<string>
zip_extract(zip, entry, dest) -> bool                             (CRC-32 verified)
```

`move_file` is the last step of a safe save: write `save.tmp`, check it, then
`move_file("save.tmp", "save.txt")`. On one volume the destination is replaced in a single
operation (`MoveFileEx` on Windows, `rename(2)` elsewhere), so a crash leaves the old file or
the new one, never half of either; across volumes it copies, then removes the source. Every
function above requires the engine's `filesystem` privilege. A file's metadata (size, created and
modified times) is the engine-core `file_data(path)` builtin, on every engine.

`read_file` returns the file's bytes exactly (binary mode: every byte value, NULs and `\r\n` as they
are; `""` for an existing empty file). A file that is missing or cannot be read raises a catchable
error, `Cannot read file '<path>': <reason>`, the reason one of `not found`, `not a file` (a folder),
`permission denied`, `cannot be opened` or `read failed`. Ask `file_data(path) != null` (or `file_exists(path)`)
first when absence is expected, or catch the error; uncaught, it ends the run with the usual trace. `write_file` writes
the bytes exactly and answers `false` when it cannot.

Where things are is not the host's to invent: every engine carries the location builtins
(`resolve_path`, `current_script_dir`, `root_script_dir`, `executable_dir` with alias
`bin_dir`, `app_data_dir`, `app_cache_dir`, `working_dir`, `parent_dir`, `file_name` and the
`_path` twins; the guide's Modules chapter, "Where files are"). Loading walks the import
ladder through `resolve_path`; saving picks an anchor and asks `writable_dir` whether it can
take a file. A script dropped onto the executable therefore runs the same from any working
directory. JaiDoom chooses the folder above `scripts/` when it is writable and
`app_data_dir("JaiDOOM")` otherwise.

The host also binds `Download`, an asynchronous download the script polls from its own loop,
the same shape as the `NetChannel` binding (`tools/jaiscript/fetch.hpp`):

```
auto d = Download();
d.start(url, path)          // http or https; false with d.error() set when it cannot begin
while (!d.pump()) { ... }   // d.received(), d.total() (-1 until known), d.fraction()
d.success()  d.error()  d.cancel()
```

The transport is the platform's own: WinHTTP on Windows, libcurl on Linux and macOS when
the build found it, otherwise the `curl` executable at run time. A failed transfer leaves no
partial file. JaiDoom's first run uses this to fetch and verify its own game data.

For hosting, `net_host_name()` returns this machine's name and `net_local_addresses()` its IPv4
addresses (loopback left out), which is what a player hosting a game tells the others to type.

The strings after `--` are the global `ARGS`.

## Debugging

A DAP listener is on by default (localhost only, costs nothing until a session attaches) in
file and REPL modes — you can attach VS Code to a *running REPL session* and set breakpoints
in files you `:load`. Install `tools/vscode-jaiscript`, F5 → "Attach to JaiScript" with the
logged port. Opt out with `--no-debug`.
