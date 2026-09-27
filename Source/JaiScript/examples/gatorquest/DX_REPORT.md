# Gator Quest: developer experience report

A running journal of what it is like to build and grow a real game in JaiScript. Entries are dated and
honest. Each friction point has a minimal example and a suggestion. Ratings are good / okay / bad, judged as
if JaiScript were a finished language, against what the same work would be in Lua, C# or C++.

Toolchain for the whole project: a frozen copy of `jaiscript.exe` (Release BENCHMARKS, built from master
98972ba2) in a scratch harness folder; I only edit and run scripts.

## Bugs hit (short list)

| # | date | what | status |
|---|------|------|--------|
| 1 | 09-27 | VM reports a run-time error raised while an imported file's top level runs (a global initializer) at the importing file's `import` line; the interpreter names the real file and line | worked around (rerun on `--backend=interp` to locate) |
| 2 | 09-27 | A parse error in an imported file is not reported when the root loads; the checker instead lists every name that file declares as "use of undeclared identifier" in the other files | worked around (run the imported file directly to see its parse error) |

## Summary so far (milestone a: first playable single player)

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
