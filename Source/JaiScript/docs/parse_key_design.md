# Parse-key design: the parse-avoidance ladder (+ hot-reload structural identity)

Status: Dev-approved and implemented on VM-perf ("let's go ahead and do all that").
This memo documents the taxonomy, the measurements that justify each rung, what the old
content-hash was actually paying for, and the compatibility analysis. Numbers: Release
BENCHMARKS, dedicated min-of-5 ns harness (`source/tests/performance/parse_key_bench.cpp`
— the Foundry µs suite cannot resolve this regime, invariants.md §7), quiet machine,
2026-07-08. "OLD" = pre-ladder HEAD (measured by stash-rebuilding the same bench).

## 1. The question

Every `execute(str)` built a `(len(path) ":" path + content)` key string (a memcpy of the
whole source into a reused buffer) and hashed it (MSVC `std::hash` = FNV-1a, ~1 B/cycle)
to look up a parse the engine almost always already had. Dev: "why hash every string
coming in? We know the filename if we load a file, we know the embedded string if we pull
an embedded string." The observation that settled the design: **the hash was only the
map's index — the map's equality already memcmps.** An index needs a cheap discriminator,
not a digest. A memcmp-verified discriminator preserves the semantics that matter: a
CHANGED string is still noticed byte-exactly (hot reload).

## 2. The taxonomy → the ladder (as implemented)

| Source kind | Identity | Key | Bytes touched on warm re-execute |
|---|---|---|---|
| C++ string literal | the pointer (immutable static storage, consteval-proven) | `(ptr, len)` map | **0** |
| File | the file itself | canonical path + `(mtime, size)` stamp from ONE stat | **0** (after a one-shot verify) |
| Dynamic string | its bytes | `(path_len, content_len, first/mid/last 8 bytes)` discriminator → flat scan ≤64 → **one memcmp** verifies | N (one SIMD memcmp) |
| Cold miss | — | parse (+ jaibite disk layer, unchanged) | N (lex+parse) |

Cache hit = the LIVE entry: AST for the interpreter + lazily compiled chunk for the VM.
The jaibite disk layer is never touched on the memory path (unchanged).

### 2.1 Literal lane (`jai::script_source`)
`template<size_t N> consteval explicit script_source(const char (&)[N])` — construction
from a literal happens at compile time, so a literal-tagged instance is GUARANTEED to
point at immutable static storage; pointer+length IS content identity (a compiled-in
literal cannot change at runtime — no hot-reload concern by construction). A runtime
char array fails loudly and clearly (MSVC VS18):

    error C7595: 'jai::script_source::script_source': call to immediate function is not a constant expression
    note: failure was caused by taking the address of an object that does not have static storage duration
    note: see usage of 'buf'

Dynamics wrap in `std::string_view` (implicit ctor, `literal_ = false`) and fall through
to the content lane. The array ctor is **explicit** on purpose: bare `execute("...")`
keeps its historical `std::string` overload — an implicit array ctor would have made
every existing `execute("...")` call in the codebase ambiguous (two rival user-defined
conversions). Call sites opt in: `execute(script_source("..."))`. `std::string` args
still route to `execute(const std::string&)` (identity reference binding beats any UDC;
pinned by static_asserts + behavior tests in engine_tests). MSVC `/GF` pooling merges
identical literals to one address — one entry; without pooling, duplicate text =
harmless duplicate entries (bounded at 64). The pointer map is consulted ONLY for
tagged sources — zero cost when unused.

### 2.2 File lane (`execute_file`)
One `GetFileAttributesExW` (mtime FILETIME + size in a single syscall; std::filesystem
pair fallback elsewhere) gates a canonical-path-keyed node (`weakly_canonical` computed
once per new path spelling, cached):
- strictly-newer mtime OR size change → stale → legacy read + reparse + restamp;
- stamp-equal, unverified → ONE read+memcmp (the same-tick-edit hazard: an editor write
  within the mtime tick of the original read is invisible to the stat) → verified;
- stamp-equal, verified → reuse with **zero file reads**.
The node also pins the as-given path spelling: a different spelling of the same file
misses the fast lane, so attribution (the path stamped on AST nodes, error text,
breakpoints) stays byte-identical to the read path. Mirrors the jaibite disk cache's
"strictly newer = fresh" rule; the fast lane's one stat is now the only per-execute
filesystem cost. (Note: on this machine a metadata stat is ~25 µs — Defender-inflated —
which is now the file-lane floor; still 3.5-3.8× better than read+2 stats+key.)

### 2.3 Dynamic lane (the discriminator swap — replaces the hash everywhere)
`(path_len, content_len, head8, mid8, tail8)` + flat vector of ≤64 nodes (the old LRU
bound), linear scan of five u64 compares per node, then path+content memcmp on the
candidate. Exact change detection — the memcmp is the arbiter; a discriminator collision
(contrived) just keeps scanning the short bucket. This REPLACED the hash map: no
key-string build (was a full memcpy of the source), no FNV walk (~1 B/cycle over
path+content), and the map's final key-equality compare is now the ONLY full-content
pass. The earlier MRU+memcmp idea is subsumed (no special case needed). `engine::check`
and `execute_source` share the same two functions (`find_cached_script` /
`store_cached_script`), so every entry point swapped at once.

## 3. What the measurements say

### 3.1 Keying replicas (isolated: key-build+hash+find vs disc+scan+memcmp, 64-entry caches)

| size | OLD hash lane | NEW disc lane | speedup |
|---|---|---|---|
| 64 B | 120-146 ns | 52-62 ns | ~2.3× |
| 512 B | 668-813 ns | 68-149 ns | ~5.5-9.9× |
| 4 KB | 5,129-5,354 ns | 205-211 ns | ~25× |
| 32 KB | 41,131-42,358 ns | 1,598-2,500 ns | ~17-26× |

Raw components: std::hash 4,825 ns vs memcmp 159 ns at 4 KB (30×); 38,800 vs 1,589 ns at
32 KB (24×) — SIMD compare vs byte-at-a-time FNV, exactly the predicted shape.

### 3.2 End-to-end warm `execute(str)` hit (comment shape: execution ~constant, keying in the slope)

interpreter:
| size | OLD | NEW | speedup |
|---|---|---|---|
| 73 B | 984 ns | 722 ns | 1.4× |
| 521 B | 1,626 ns | 737 ns | 2.2× |
| 4 KB | 6,165 ns | 877 ns | 7.0× |
| 32 KB | 43,071 ns | 2,391 ns | **18×** |

vm: 701→374, 1,067→390, 5,848→958, 43,750→2,438 ns (same story; the vm's warm floor is
lower). The NEW slope above the ~720/370 ns floor is the one memcmp.

Realistic shape (defs: N function definitions re-executed per hit) at 4 KB:
OLD 55.6 µs → NEW 32.3 µs interpreter (44.5→29.9 µs vm) — keying was ~40% of a realistic
warm re-execute at this size; at 32 KB, 458→244 µs (~47%). (Defs rows carry re-execution
allocation churn variance; the comment-shape table is the clean keying signal.)

### 3.3 File lane, warm `execute_file`

| size | OLD (read + 2 fs-stats + key + run) | NEW (1 stat + run) | speedup |
|---|---|---|---|
| 4 KB | 244 µs | 64 µs | 3.8× |
| 32 KB | 995 µs | 286 µs | 3.5× |

Decomposition (OLD, 4 KB): raw read 102 µs + fs stat pair 63 µs + keying 5 µs + execution
~50 µs. NEW: one GetFileAttributesEx (~25 µs) + execution. The jaibite sibling stats and
the full file read are gone from the warm path.

### 3.4 Cold (never-seen 4 KB string)
OLD 913 µs → NEW 726 µs (parse dominates both; the ladder does not tax the cold path —
the delta is store-side churn plus run variance).

### 3.5 Bindstone frame estimate (~10 KB entry script re-executed every frame, 60 fps)
- From strings: warm overhead above execution drops ~13.9 µs → ~1.2 µs (interp,
  interpolated) — the old hash+key-build was ~40-50% of every warm re-execute; as frame
  budget it was 0.08%, i.e. only visible when many scripts re-execute per frame.
- From a file (the actual host shape): 244→64 µs at 4 KB, ~400→~100 µs extrapolated at
  10 KB — **~1.8% of a 16.67 ms frame recovered** per per-frame entry script.
- The class-redefinition half is the bigger per-frame story: see §5.

## 4. What relied on the content hash — the downstream checklist

Verified consumers of the old key/hash (grep + call-graph):
- `find_cached_script` / `store_cached_script` — the ONLY consumers. Swapped.
- jaibite disk cache: keyed by SIBLING PATH + mtime freshness + registration
  fingerprint — never by content hash. Unaffected. (If a future string-keyed disk cache
  wants `<hash>.jaibite` filenames, that hash is computed once at WRITE time, not per
  execute — a different budget; no conflict.)
- `registration_fingerprint`: hashes the C++ registration surface, not script content.
  Unrelated.
- Debugger/attribution `(path, content)` pins: path length is IN the discriminator, path
  bytes and content are memcmp-verified → identical text under two paths still gets two
  ASTs, each stamped with its own filename. Attribution holds exactly. File lane pins the
  as-given spelling explicitly.
- Static-check cache, epoch invalidation, LRU: live on the entry, not the key. Unchanged
  (epoch re-checked on every hit in all lanes; LRU stamp bumped in all lanes; literal and
  file lanes drop stale entries on epoch bumps).
- Dev's network-string position: wire strings are dynamics → discriminator lane (exact,
  cheap — one memcmp). The EXPLICIT zero-rekey lane for hosts is `engine::jaibite(content)`
  (parse once, hold the handle) — already shipped; nothing breaks.

## 5. Hot-reload half: structural AST identity at redefinition (Part 4)

### What the old mechanism actually was (the finding)
`class_definition::compute_fingerprint` hashed sorted field ids + method ids +
**`reinterpret_cast<size_t>(&func)` — the heap ADDRESS of each method's std::function**.
Every reload mints fresh function objects, so the fingerprint could never match for any
class with methods (or typed-field setter thunks) — the "identical" fast path was dead
code for real classes. Actual savings came only from the separate `fields_changed`
detection; identical reloads still paid O(instances) `set_class_definition` loops and
derived-class propagation every time. The `identical_class_fingerprint` test asserted a
wall-clock inequality (1 ms tolerance) between two runs of the SAME slow path — the
known flake, pinning nothing.

### The replacement (landed)
- Identity = **structural AST equality**: the incoming class_decl subtree serialized
  WITHOUT source locations (`detail::structural_node_key`, a mode of the battle-tested
  jaibite writer — one flag suppresses the two `location()` writes; symbol ids persist
  as first-seen string-table indices so engine-local id values never leak in). Byte-equal
  keys = structurally identical code: exact (no collision risk), and formatting /
  comment / line-shift insensitive by construction. Computing the new side is the same
  O(nodes) walk a fingerprint would need — minus the collisions.
- Checked at redefinition-EXECUTION time, not parse time (semantics preserved: field
  initializers still evaluate, side effects re-run, includes re-execute).
- O(1) discriminators first: stored (member count, base count) gate the O(nodes) walk.
- The key stores lazily per class on each redefinition (first redefinition seeds it,
  subsequent identical ones hit). No old-AST retention needed — the stored key is the
  definition-time snapshot; both sides are encoded at the same post-parse phase so
  parser-assigned slots agree and later runtime AST patches don't skew the compare.
  Serializer failure on either side = "not identical" (safe fallback).
- On match: **the new AST is adopted anyway** — methods were already re-minted from the
  fresh parse (add_script_method), and `redefine_class(..., structurally_identical=true)`
  swaps the method/static/field-default maps in — so debugger/error line info tracks
  cosmetic shifts. This fixes the latent staleness the old fast path had. What's skipped:
  per-instance migration, per-instance `set_class_definition` re-pointing (same
  definition object), and derived-class propagation.
- Deterministic observability: `class_definition::identical_redefinitions()` counter.
  The flaky test is now `identical_class_structural_identity`: counter == 0 after the
  seeding redefinition, == 1 after a byte-identical re-execute, == 2 after a
  comment-shifted re-execute, still == 2 after a real body change — with the changed
  method proven live. Wall-clock assertions deleted (the documented right-direction
  test change; `fields_unchanged_performance` remains timing-based but exercises the
  unchanged fields_changed mechanism).
- Both backends share the mechanism verbatim (twin call sites in
  `interpreter::visit_class_decl` / `vm_backend::exec_class_decl_node`); suites are green
  on both, byte-parity preserved.
- Top-level FUNCTION redefinition: no analogous heavy path exists — a function redefine
  is an env-slot overwrite + overload-set rebuild, O(1) in instances. Nothing to gate.

### Measured (identical class re-executed per frame-shaped iteration)

| case | OLD | NEW | speedup |
|---|---|---|---|
| small class (4 methods), 64 live instances | 25.0 µs | 16.5 µs | 1.5× |
| large class (24 methods), 1000 live instances | 807 µs | 69.6 µs | **11.6×** |
| (vm) large class, 1000 instances | 811 µs | 69.7 µs | 11.6× |

A Bindstone-shaped per-frame re-execute of a class-bearing entry script no longer scans
every live instance every frame: with 1000 instances that was 4.8% of frame budget,
now 0.4%.

## 6. Recommendation / migration

- The ladder as landed IS the design; the hash is gone from every per-execute path and
  nothing downstream consumed it. No host source changes required anywhere; semantics
  byte-identical (memcmp-exact change detection in both content lanes).
- **Literal lane: keep.** Even with the hash gone it's the only rung with a flat cost —
  ~700 ns (interp) / ~340 ns (vm) at ANY size vs 2.4 µs disc-lane at 32 KB — and its API
  also documents intent (a tagged compiled-in script). It's opt-in and zero-cost when
  unused. Ergonomics extension if wanted later: a `_jai` UDL or an `execute(script_source,
  instance_variables)` companion.
- Remaining floor: the file lane's single stat (~25 µs machine-dependent) now dominates
  its warm path; if that ever matters, the next rung is a host-driven "trust window"
  (skip the stat within a frame) — deliberately NOT built (freshness rule stays uniform).
- The reload fast path currently re-walks the new AST per redefinition (~µs); if a
  profile ever shows it, the next rung is caching the encoding on the class_decl node
  (same-node re-execution = zero walks), but per-node mutable caches interact with
  parallel workers — not built until proven needed.

## 7. Cross-compiler / toolchain notes

- consteval ctor: C++20; MSVC VS18 verified (C7595 diagnostics above); clang/gcc reject
  non-static-storage pointers in immediate invocations equivalently.
- MSVC `/GF` (optimized builds): identical literals pool to one address → one entry.
  Debug (no /GF): duplicate text at different addresses = duplicate entries, ≤64 bound,
  harmless.
- MSVC 14.51 ICE found while building the bench: `std::hash<std::string>{}(a)` invoked
  inline inside a nested lambda ICEs the front end (C1001 in p1's trees.cpp) under
  /O2 /GL; hoisting the hasher to a named local is the workaround (annotated in
  parse_key_bench.cpp).
- Windows file-lock note: the freshly linked test exe is intermittently held by Defender
  for ~1 min after a crashed link; relink succeeds after release (not a code issue).
