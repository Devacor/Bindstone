# Gator Quest: developer experience report

A running journal of what it is like to build and grow a real game in JaiScript. Entries are dated and
honest. Each friction point has a minimal example and a suggestion. Ratings are good / okay / bad, judged as
if JaiScript were a finished language, against what the same work would be in Lua, C# or C++.

Toolchain for the whole project: a frozen copy of `jaiscript.exe` (Release BENCHMARKS, built from master
98972ba2) in a scratch harness folder; I only edit and run scripts.

## Bugs hit (short list)

| # | date | what | status |
|---|------|------|--------|
| 1 | 09-27 | A run-time error raised while an imported file's top level runs loses its location. Repro: `c.jai`: `int boom() { array<int> a = [1]; return a[5]; }` / `int X = boom();`, `d.jai`: `import "c.jai";`. VM: `at <script> (d.jai:1)` (the import line); interpreter: `at <script> (c.jai:2)`. Neither shows the `boom` frame (c.jai:1). Errors inside functions called later have full traces on both | worked around (rerun on `--backend=interp` to locate) |
| 2 | 09-27 | A parse error in an imported file is not reported when the root loads; the checker instead lists every name that file declares as "use of undeclared identifier" in the other files | worked around (run the imported file directly to see its parse error) |
| 3 | 09-27 | **Backend divergence.** On the interpreter, an error raised inside a file run by `include(path)` (a `throw`, a run-time error, or a static-check failure of the included file) escapes the `try` around the include and ends the program; the VM catches it. Repro below (milestone b entry). Also: the static-check listing of an included file says `script:L:C`, not the file's path | reported, not fixed (engine batch); the hot-reload test skips its two broken-edit steps on the interpreter |
| 4 | 09-27 | A runtime `include` inside a function declares an `enum` only in that function's scope, silently: re-running `enum Color { red, green, blue }` leaves the global `Color` unchanged, with no warning | by design as far as I can tell (include is textual); worked around by keeping enums in a file that is never reloaded |

## Summary at milestone b (saves + hot reload)

New since milestone a: versioned plain-text saves with checksums and backups, a save/load round trip inside the
smoke that is invisible to the simulation (same final STATE_HASH with and without it, on both backends), a save
robustness suite (15 checks), dev-mode hot reload in the live game, and a headless hot-reload test (23 checks on
the VM, 20 on the interpreter). Ratings for this stretch: **saves good**; **hot reload: classes good, functions and
constants okay (a generated stub), enums bad (silently not reloaded), error handling on the interpreter bad (bug 3)**.

Top friction points now, most costly first:

1. **Errors far from their cause** (bugs 1, 2 and the `script:L:C` half of 3). Still the biggest tax.
2. **Hot reload of code is a hand-built mechanism, and fragile in exactly the ways a language feature would not be**:
   a line-based stub generator that depends on my formatting, a naming convention the language cannot enforce (one
   forgotten initializer on a state variable silently resets it on the next reload), non-atomic application (a
   failure halfway leaves some constants re-run and no functions rebound), and on the interpreter a bad edit kills
   the session (bug 3). Class reload with instance migration, by contrast, is excellent and needed nothing from me.
3. **The checker misses certain, local type errors** (milestone a list), plus one new one: in the reload stub
   `::PACK_MAX = "lots";` is not a load error although `PACK_MAX` is a declared `int` (the same line spelled
   `int PACK_MAX = "lots";` in the original file is one).
4. **Value semantics as a silent cost** (unchanged).

## Summary at milestone a (first playable single player)

What it is like: productive. ~8,000 lines across 27 files in one working session, zero checker warnings, identical
STATE_HASH on both backends from the first run of the full sim. The C++-shaped syntax, typed classes with defaults,
typed arrays, reference declarations on container elements and template strings with format specs carry a game
like this without fighting the language. Performance is predictable and comfortably sufficient (VM: 1.5 ms per
10 Hz sim tick with ~520 mobiles; ~5 ms per 162x110-pixel lit frame; interpreter about 6x slower and still playable).

Top friction points, most costly first:

1. **Errors that surface far from their cause** (bugs 1 and 2): an imported file's parse error shows up as a cascade
   of "undeclared identifier" errors elsewhere; a run-time error in an imported file's top level points at the
   `import` line on the VM. Each cost a detour. This is the single biggest DX tax in a multi-file project.
2. **The checker misses certain, local type errors**: a literal passed to a `&` parameter, `string += char + char`
   (which is `string += int` by the documented char promotion), `string::repeat(char, n)`. All three failed only at
   run time, one with the bare message "Type mismatch" that names neither the operator nor the types.
3. **Hot reload of free functions needs a hand-rolled rebind stub** (see the first entry): classes reload
   beautifully, but a script cannot re-run a file at global scope.
4. **Value semantics as a silent cost**: `SpellDef s = SPELLS[i];` deep-copies a record with arrays on every call.
   Correct and documented, but easy to write in a hot path; `SpellDef& s = ...` is the fix.

## Journal

### 2026-09-27: first contact, content tables

**Good: the string library carries a data-driven design.** All content (70 tiles, 158 items, 59 monsters, 16
spells, 47 recipes, 10 quests, 8 worlds) is written as one line per record in a tiny DSL
(`"Swamp Rat|hp=10|dmg=1-4|spd=3|ai=melee|drop=Rat Tail:500"`) and parsed at load with `split`,
`starts_with`, `substr` and `to_int(default)`. `to_int(default)` in particular removes a whole class of
parse-error handling. The full data layer parses in ~40 ms on the VM and ~90 ms on the interpreter. In Lua this
would be table literals; here the DSL is arguably nicer because typos in names are caught by lookup helpers
that `throw` during load (`iid("Garlic")`), and a thrown string from a global initializer stops the program
with the message and file/line. Rating: good.

**Good: typed class records with defaults.** `class ItemDef { string name = ""; int lo = 0; ... array<int> aff_code; }`
reads like a C++ aggregate and every field defaults sensibly without a constructor. Rating: good.

**Bad: a literal passed to a reference parameter fails only at run time, and the VM hides where.**

```jaiscript
int index_of_name(array<string>& names, string n, int def) { ... }
int r = index_of_name(["common", "rare"], "rare", 0);   // Cannot pass non-lvalue to reference parameter
```

The checker knows the parameter is `&` and the argument is a literal, so this could be a load error. At run time
the VM reported it at `gatorquest.jai:6` (the `import "gq_data.jai"` line of the entry file); the interpreter
reported `gq_data.jai:444`, the real line. With twenty files, the VM location is useless and I had to rerun on
the interpreter to find the call. Suggestions: (1) make a non-lvalue argument to a `&` parameter of a
same-program function a static error; (2) carry the callee file/line into VM error locations for code in
imported units (parity with the interpreter). Rating: bad (location), okay (the rule itself is C++'s).

**Surprising: hot reload of free functions from inside a running program.** Probed before designing the game
(the design depends on it):

- Re-running a file with a class in it reloads the class everywhere, and live instances migrate (a new field
  appears with its initializer, existing fields keep their values, methods come from the new definition), on
  both backends, including instances inside typed arrays and behind `shared_ptr`. Rating: good.
- But a script cannot re-run a file at global scope from inside a function. `include(path)` inside a function
  runs the file in that function's scope (documented in `docs/global_shadowing.md`), so the re-defined free
  functions and constants are locals of the reload function and the program keeps calling the old ones. There
  is no script builtin that executes a file as a top-level unit (the host has `execute_file`; scripts do not).
- What works: include in the reload function, then rebind each name explicitly: `::f = f; ::SPEED = SPEED;`.
  The new functions close over the reload scope and resolve each other there, so the reloaded module is
  internally consistent. I generate that rebind stub per file by scanning the file's top-level declarations.

```jaiscript
void reload() {
	string p = LOGIC;        // computed path: runtime include (a literal one is spliced at parse time)
	include(p);              // defines f/SPEED as locals of reload()
	[[warn_mode]] ::f = f;   // rebind the globals by hand
	[[warn_mode]] ::SPEED = SPEED;
}
```

Suggestion: a script-level `reload(path)` / `execute_file(path)` that runs a file as its own top-level unit
(same unit identity as its original import, so "a reloaded file writes in place" applies). That would make
in-game hot reload a one-liner and match what the docs promise hosts. Rating: okay (it works, but only with
a generated stub and a naming convention that keeps state out of reloaded files).

### 2026-09-27: the sim core, bots, the client

**Good: the sim came up deterministic on both backends on its first real run.** Eight generated realms, 185
spawners, ~520 mobiles, combat, loot, skills, quests, crafting and two autopilot players: `STATE_HASH` identical on
`--backend=vm` and `--backend=interp` (2,000 ticks: 3596128203 on both). Nothing in the language had to be avoided to
get there; the only discipline was mine (one seeded RNG, no clock in the sim). Rating: good.

**Good: references into containers of class values.** `Character& c = CHARS[slot];`, `array<Item>& pack =
CHARS[slot].pack;`, `c.pack[i].qty -= 1;`, `c.worn[s] = Item();` all mutate in place, on both backends, and read
exactly like C++. This is what makes "flat arrays for hot data, classes for cold data" pleasant. Rating: good.

**Good: template strings with format specs for HUD text.** `` `${SKILL_NAME[k]:<17}${tenths(s):>5}` `` and
`` `${minute:02}` `` replaced a whole padding helper layer. Rating: good.

**Good: error messages that tell you the fix.** `'var' is a type keyword and cannot be used as a parameter name -
rename it (e.g. 'varname')`; `string::repeat expects a string first argument`; stack traces with file:line for
every frame on the VM for errors inside functions. Rating: good.

**Bad (bug 2): a parse error in an imported file is swallowed by the multi-file check.** Minimal repro:

```jaiscript
// b.jai
void f(int var) {}
// a.jai
import "b.jai";
f(1);
```

Running `a.jai` prints `error: use of undeclared identifier 'f'` (and, in the real project, 40 such errors across
four files) and never mentions b.jai's parse error. Running `b.jai` directly shows the real message. Suggestion:
when gathering the program, report an imported unit's parse errors first (and stop), naming the file.

**Bad: char arithmetic ambushes byte-string building, and the checker lets it through.** Chars promote to int
under `+` (documented), so

```jaiscript
string z = "";
z += to_char(1) + to_char(2);   // run time: "Type mismatch"
msg.push(to_char(0x90) + to_char(60) + to_char(96));   // pushes an int into array<string>: run time error
```

Both operand types are known to the checker; `string += int` and `array<string>.push(int)` are certain errors it
could report at load. The run-time text "Type mismatch" should name the operator and both types (`cannot apply
'+=' to string and int`). The idiom that works is `"" + to_char(a) + to_char(b)` (JaiDOOM does it everywhere, which
is how I recognized it). Rating: okay (rule), bad (diagnostics).

**Okay: checked integer overflow shapes hashing code.** xorshift, FNV and value noise all needed explicit
`& 0xFFFFFFFF` lanes so products stay below 2^63. Fine once known; I only later learned `math::wrap_*` exists for
deliberate wraparound. Suggestion: mention `math::wrap_*` in the operators chapter next to the overflow rule.

**Okay: no file timestamps in the runner.** For dev-mode hot reload I have to read and compare whole files to detect a
change. `file_mtime(path)` beside `file_exists` would make the watcher trivial. (Not a language issue.)

**Performance, measured (Release runner, VM unless noted).** Data layer parse 40 ms (interp 90 ms). Worldgen of
eight realms (55k tiles, noise, towns, roads) ~250 ms. Sim tick 1.2-1.5 ms with ~520 mobiles (interp ~8 ms).
Render of a 162x110-pixel view with per-tile light interpolated per pixel and Bayer dither: 3.5 ms; composing the
half-block frame plus a 38-column text pane: 1.2 ms (interp: 20 ms and 7.6 ms, still ~30 fps). The one hot spot
I hit was my own (a 5,000-node BFS per tick per bot); caching the path fixed it. No language-level cliffs so far.

### 2026-09-27: saves

**Good: a plain-text save format is a short, pleasant job.** World file, character files, checksum, `.tmp` write
with read-back verification, `.bak` rotation, fallback on load, version refusal, defaults for missing records: ~450
lines, most of it one line per field. `split`, `find`/`rfind`, `substr`, `starts_with`, `to_int(default)`,
`replace_all` and template strings are all it takes; `map<int, string>.keys()` comes back as a typed `array<int>`.
The whole robustness suite (missing, corrupt, truncated, newer-version, sparse old file, restart with gear and
position) passed on the second run on both backends. In Lua this would be the same amount of work; in C# it would
be more ceremony. Rating: good.

**Good: value semantics make "rebuild, then overwrite" loading clean.** Loading resets the sim, regenerates the
terrain from the seed and overwrites every pool slot for slot. Because arrays and records copy, there is no aliasing
to untangle, and the round trip is exact: the smoke saves and reloads mid-session and ends on the same STATE_HASH as
a run that never saved (3000 ticks: 2545348889 both ways, both backends). The one subtle requirement was mine (the
tile occupancy grid is path-dependent when a ghost shares a tile, so it is saved verbatim). Rating: good.

**Okay: the string splitting vocabulary.** My first save bug: records like `stats 35 25 25` went through a
comma-splitting decoder. `s.split()` with no argument splits into characters (Python's `split()` splits on
whitespace), and there is no whitespace split or skip-empty option, so I normalized with `replace_all(" ", ",")`.
Suggestion: `split_ws()` or `split(sep, skip_empty)`.

**Okay (host API): no atomic replace.** The safe-save idiom is "write temp, rename over". The runner has no
`rename_file`/`move_file`, so the save writes the real file a second time after verifying the temp copy; a crash in
that window is covered by the `.bak`/`.tmp` fallback, but a rename would make it one step. Suggestion: add
`move_file(from, to)` (and `file_mtime(path)`, which would also make the hot-reload watcher cheaper) to the runner.

Measured: world file 56 KB with ~540 mobiles; save 42 ms on the VM (66 ms interpreter), load 221 ms (1.4 s
interpreter), both dominated by regenerating eight realms from the seed rather than by parsing.

### 2026-09-27: hot reload in a real project

**The mechanism I built** (gq_dev.jai, ~150 lines): for a changed file, write a stub that is the file's text with
imports blanked, `T NAME = expr;` rewritten to `::NAME = expr;`, `T NAME;` blanked, and `::fn = fn;` appended for
every top-level function; `include(stub)` it from a function, inside `try`. Line numbers stay aligned so errors point
at the real source line (I also rewrite the listing's `script:` prefix to the file name, see bug 3). A watcher reads
the files once a second and compares contents (no `file_mtime`). In the live game an edit shows up in the HUD within a
second: `Reloaded gq_quests.jai: 14 functions, 0 constants re-run, 0 state kept`.

**How the include + `::f = f;` workaround feels to use**: it works, and better than I expected, but it is a mechanism
I had to design, test and now maintain, and it rests on conventions rather than on the language:

- It *works everywhere I tested*: rebinding a function's global binding is accepted by the strict checker, callers in
  other files pick up the new body immediately (no stale VM call-site state observed), a changed constant is seen by
  code that was not reloaded, and re-running `gq_data.jai` rebuilds every content table (185 constants) so the next
  Swamp Rat spawns with its new hit points. The headless test changes five files mid-session (a class gains a field
  and a method, a constant, a cross-file function, monster behavior, monster data) and the whole sim state hash is
  unchanged by the reload itself; 600 more ticks run on the new code, and the final hash matches across backends.
- It *needed a convention I had to retrofit*: in a reloadable file, "initializer = constant, no initializer = state".
  Forty state variables in logic files had initializers (tile size, palette counters, BFS stamps...). Missing one is
  silent: re-running `int BFS_STAMP = 0;` would reset a counter against a grid still holding old stamps, and the
  bots would path wrongly with no error anywhere. The language cannot flag it.
- It is *line-based*: declaration heads must sit on one line at column 0. My style guarantees it; someone else's
  might not.
- It is *not transactional*: a stub that fails halfway has re-run the constants above the failure and rebound no
  functions. Fine in practice (the error is reported and the next save retries), but a language primitive could
  check and evaluate into a staging scope and swap only on success.
- **Enums do not reload** (bug 4): re-declaring an enum in the stub declares a local enum, silently. I moved all enums
  to `gq_enums.jai`, which is never reloaded; a new enum member therefore needs a restart.
- **On the interpreter a bad edit ends the session** (bug 3): the VM rejects both a static type error (`cannot return
  'string' from 'buy_price' declared to return 'int'`, reported at `gq_actions.jai:262:27`) and a run-time failure
  (`Cannot assign string to 'PACK_MAX'`) and keeps the old code; the interpreter cannot catch either. Minimal repro:

```jaiscript
string bad = current_script_dir() + "/bad2.jai";
write_file(bad, "throw \"boom\";\n");
try { include(bad); } catch (e) { print("caught: " + e); }   // VM: caught: boom
print("after");                                                // interpreter: program ends with "boom" at bad2.jai:1
```

**Class reload with instance migration is the standout.** Adding `int reload_marks = 7;` and a method to `Character`
while the live character sits in a typed `array<Character>`: the live instance gains the field with its
initializer, existing fields keep their values, items already in backpacks (`array<Item>` inside each character)
gain their new field too, new instances get the defaults, and a second edit that changes the method again keeps the
field's live value (42). Zero work on my side, identical on both backends (an earlier probe showed the same for an instance behind a `shared_ptr`). The only friction: code already loaded
cannot name the new member through a typed receiver (the checker holds it to the old shape), so the test reads it
through `var&`, which is the documented escape hatch and felt right. Rating: good.

**Suggestion (to Dev, as a design question):** a script-level `reload(path)` that re-runs a file as its own unit,
redefining its functions and re-evaluating its initialized globals in place, would replace my stub generator, remove
the line-based fragility, and could be made transactional (check first, apply on success). The class half of hot
reload is already there; this is the function-and-constant half.
