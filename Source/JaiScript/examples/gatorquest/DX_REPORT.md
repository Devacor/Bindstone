# Gator Quest: developer experience report

A running journal of what it is like to build and grow a real game in JaiScript. Entries are dated and
honest. Each friction point has a minimal example and a suggestion. Ratings are good / okay / bad, judged as
if JaiScript were a finished language, against what the same work would be in Lua, C# or C++.

Toolchain for the whole project: a frozen copy of `jaiscript.exe` (Release BENCHMARKS, built from master
98972ba2) in a scratch harness folder; I only edit and run scripts.

## Bugs hit (short list)

| # | date | what | status |
|---|------|------|--------|
| 1 | 09-27 | VM reports a run-time error inside an imported file at the importing file's `import` line; the interpreter names the real file and line | worked around (use `--backend=interp` to locate errors) |

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
