# Why JaiScript

*Written by Claude, after a week spent inside every layer of this language — optimizing its
VM, fuzzing its backends against each other, fixing what fell out, and then writing a
roguelike and a demoscene reel in it to see how it feels from the other side. This is the
case for JaiScript, made by the entity that spent that week finding everything wrong with it.*

## What it is

JaiScript is an embedded scripting language for C++ engines. C++-like syntax, script
classes with inheritance and hot reload, coroutines, lambdas with captures, gradual typing,
JSON serialization, and a two-line embedding story:

```cpp
auto e = jai::engine::make();
e->execute("print(\"hello\");");
```

It compiles as part of your project, binds C++ types in a fluent line or two
(`dynamic_binder`), and runs on one of two interchangeable backends: a tree-walking
interpreter and a bytecode VM with full behavioral parity.

## The pillars

### 1. The type ladder: gradual typing by declaration

```jaiscript
int hits = 0;        // enforced — assigning a string is an error
auto name = "Jai";   // inferred, then enforced — locked to string
var anything = 5;    // dynamic — the explicit, visible opt-out
anything = "now a string";  // fine; you asked for this
```

Three keywords, one philosophy: *the declaration is the contract*. `var` is not a default
you fall into — it is a choice you can see in a diff. This one decision keeps paying rent:
the optional static checker maps onto it exactly (typed code gets compile-time errors,
`var` opts out, like TypeScript's `any` but designed-in rather than retrofitted), the VM's
fast paths key off it, and reviewers can audit a script's dynamism at a glance.

### 2. Value semantics, references by declaration

Assignment, parameters, and returns copy. Nothing aliases at a distance; a function cannot
mutate your data unless its signature says so:

```jaiscript
function heal(Creature& c, int amount) { c.hp = c.hp + amount; }
heal(player, 5);          // & in the signature — mutation is visible at both ends
heal(party[0], 5);        // fields and elements bind by reference too
```

This is the right default for game code: deterministic saves, no spooky mutation, and
`to_json` of any value means what you think it means. When you want mutation, `&` makes it
explicit — and cheap.

### 3. Two implementations, one executable specification

Most languages have a spec nobody enforces. JaiScript has two independent implementations —
tree-walker and bytecode VM — required to agree byte-for-byte, including error text,
enforced by 1,500+ tests run against both, plus a differential fuzzer that generates
random programs and files a bug whenever the backends disagree. A 10,000-program campaign
ran with zero crashes; every divergence it ever found is fixed and pinned. When the
implementations must agree, the semantics cannot rot silently.

### 4. Zero global state

No statics, no singletons; every value carries its engine. Engines are fully isolated —
two engines in two threads need no locks, tests cannot contaminate each other, and the
planned parallel story (engine-per-worker isolates, in-script `parallel_for`) falls out of
this property rather than fighting it.

### 5. Hot reload that never bricks

Redefine a class mid-session and live instances migrate: fields keep their values,
changed field types convert permissively, new fields take their initializers — the reload
*always succeeds*, because iteration speed is the point. Functions on the live call stack
finish on their old bodies; the next call gets the new one. The roguelike in
`examples/roguelike` reloads its own scripts mid-game on a keypress.

### 6. Safe at the boundary, by construction

A script can only touch what the host registered — there is no built-in file, network, or
system access to sandbox away; the binding surface *is* the allowlist. On top of that:
integer overflow raises instead of wrapping (build-time policy), deep recursion is a
catchable error rather than a stack fault, an execution budget bounds runaway scripts, and
`engine::memory_cap` bounds runaway allocation. Budget and cap exhaustion are *terminal* —
a hostile script cannot catch-and-swallow its way past the host's limits. Untrusted
scripts are a configuration, not a rewrite.

### 7. Parse once, run anywhere in your pipeline

```cpp
auto bite = engine->jaibite(source);   // parse once
bite.execute();                        // run many
bite.save("level.jaibite");            // ship precompiled
auto loaded = other_engine->jaibite_load("level.jaibite");  // symbols relocate
```

The engine also caches source strings transparently, so even naive `execute(str)` in a
loop pays the parse once. Loading a `.jaibite` skips parsing entirely and re-interns symbols
into the loading engine — save on one engine, load on another.

## The receipts

- **1,500+ tests green on both backends**, every commit. Differential fuzzing in CI.
- **Performance**: the VM beats Squirrel on most head-to-head benchmarks (13W/6L) and
  holds its own against Lua 5.4 outside of call-dense recursion (a known gap with a
  planned fix — see `docs/parallel_design.md` siblings for the roadmap docs). Loops fuse
  into superinstructions; a 1000-iteration hot loop runs ~45µs.
- **`script_value` is 32 bytes**, static-assert-gated, two per cache line.
- **A complete roguelike** (`examples/roguelike`, ~2,700 lines of JaiScript) and a
  **demoscene reel** (`examples/demoreel`) are written in it — both double as
  cross-backend determinism tests: same seed, byte-identical state hashes on both engines.
- **Optional static checking** (`engine->static_checking(strict)`): accumulate-then-throw
  compiler-style diagnostics with line/column and related locations, before a single
  statement runs. `var` code stays free; typed code gets caught at parse time.

## The honest tradeoffs

- **Copies cost.** Value semantics means passing a big structure by value deep-copies it.
  The idiom is `&` parameters for anything heavy — cheap by design. If you write
  Python-shaped code expecting reference semantics, your first hour will surprise you;
  read the reference section above and it won't.
- **Call-dense recursion is slower than Lua** today (~5× on a fib microbenchmark). Most
  game scripts are loop- and method-shaped, where the gap is small to none — but the
  register-window VM redesign that closes it is on the roadmap, not a hope.
- **Two backends cost maintenance.** That's the price of the executable spec, and shared
  semantic kernels (`detail/ref_lvalue.hpp` pattern) are steadily shrinking the duplicated
  surface.

## Who it's for

Teams embedding scripting in a C++ engine who want: iteration speed (hot reload, script
classes), safety rails on by default (budgets, caps, checked arithmetic, no ambient
authority), types that scale from prototype (`var`) to production (`int` + strict
checking) *within the same file*, and a language whose behavior is enforced by two
implementations arguing with each other rather than a document nobody reads.

It was built alongside a real engine (MutedVision / Bindstone), hardened by adversarial
review and differential fuzzing, and dogfooded by building real, playable things in it.
The bugs those three waves found are all fixed, all pinned, and all still being guarded by
the machinery that found them.

*— Claude, 2026-07*
