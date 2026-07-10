# Handoff: 2016 MacBook Pro → 2025 MSI 18 Dragon (2026-07-10)

The final session on the old machine closed out a two-campaign day. This file is
self-sufficient: a fresh session on the new PC can proceed from here alone — but ALSO
copy `C:\Users\Devacor\.claude\projects\C--git-Bindstone\` (Claude's per-project memory:
campaign state, Dev rulings, measurement discipline) to the same path on the new machine;
it does not travel with git.

## Repo state

- Branch **VM-perf** at `f2ac1262`, ~68 commits ahead of origin (Dev pushes; Claude never does).
- Working tree: clean except pre-existing `Assets/Atlases/*.png` modifications (unrelated).
- Build dirs (`Source/JaiScript/out/build/*`) are UNTRACKED — reconfigure on the new
  machine (commands in `Source/JaiScript/CLAUDE.md`): `x64-Debug` (fast iterate),
  `x64-Release BENCHMARKS` (ship measurement), `x64-Profile` (adds
  `-DJAISCRIPT_VM_PROFILE`… copy the CMake flags from the old dir if in doubt:
  it is plain Release + `/DJAISCRIPT_VM_PROFILE`), optional `x64-PGO`
  (recipe in `docs/` + memory: /GL + "/LTCG /GENPROFILE" → train → /USEPROFILE, was
  worth −5-11% on GLOOM).
- Delete stray `.jaibite` sidecars after big parser/token changes (delete-and-regen
  discipline; version stays 1 pre-launch).

## EVERY absolute number resets on the new machine

All bands below are 2016-MacBook numbers. First session on the MSI: **re-baseline
everything in one sitting** (same-session = the only honest comparison) and record the
new table. What carries across machines: **the determinism gates** (STATE_HASH
1503537018 and frame hash dfd969ed5a336dca for GLOOM `--smoke --ticks 600` seed 666 —
these must match EXACTLY on the new machine; if they don't, something is broken, not
"different"), the **vm/interp ratio** (~1.35-1.40), and the **~3× gap to the Squirrel
port** (re-run the Squirrel adapter same-session for the new comparator).

Old-machine reference bands (quiet): GLOOM VM 18.3–18.7 ms/tick, interpreter ~25.5,
Squirrel port 5.57 same-day; fib(15) VM 687–741 µs (anchor 697), hot loop 44–48 µs
(must never move). Thermal drift was ±1–1.5 ms/tick on the old laptop — characterize
the new machine's drift before trusting any small delta.

Measurement discipline (unchanged, hard-won): profile COUNTS are truth, instrumented
CYCLES lie; Release A/B same-session with A/B/B/A bracketing; interpreter as drift
control for VM-only changes; min-of-3 for the Foundry µs harness; never claim sub-µs
wins from it.

## The 4-point plan (Dev-commissioned: "the road below 16") — status

1. **Typed flat array storage — COMPLETE** (c1b959bf, 7342e5d9, 7cb66f50, 954d6124).
   `array<int>`/`array<float>` on raw i64/f64 buffers behind
   `engine::typed_array_storage()` (scaffold flag, default OFF; GLOOM adapter enables
   it; DELETE the flag after bake + default flip). Stage 0 closed a real soundness
   hole (any array crossed into `array<T>` unvalidated). GLOOM grids typed
   (PIX/ROWIDX/mappack/zbuf/PAL_LUM), hashes held, measure = NEUTRAL on GLOOM —
   its INDEX heat is in the hetero WALL_WS worker recs (mixed records, untypeable by
   design). Wins live elsewhere: clone=memcpy, 4× bandwidth, free parallel proofs.
   Banked: typed interop bulk paths, typed raw_input for transform sources, interp
   typed fast tiers. Authority: `Source/JaiScript/docs/typed_array_design.md`.
2. **Register-window flat-stack — IN PROGRESS** (= flatstack_design.md stage 6;
   stages 1-2 windows landed earlier). Increment 1 landed (f2ac1262):
   dest-addressed fused binaries (`op_binary_fused_decl/_store`, sink-templated
   compute, store tail extracted+shared, emit_compound_store-style peepholes).
   GLOOM-neutral by count truth: 452 decl-fusions/tick, ~0 store-fusions.
   **Increment 2 is the GLOOM lever: fuse INDEX/INDEX_STORE operands** (container+
   index as fused_operands from slots — ~24k of GLOOM's 38.5k LOADs/tick feed these
   pairs), then call-arg window addressing (args land in the callee's arg slots),
   then widen binary_shape (9.4k/tick unfused BINARYs have call/member operands).
   Trap-lessons: new ops must join run_dispatch's exec_extended CASE GROUP (miss =
   abort) and the debugger's get_current_frame_locals proto scan (miss = locals
   vanish); new opcodes use VM_TRY_OP_SHARED (invariants §5 Debug frame ceiling).
3. **Method slot tables + self-contained-methods — PENDING.** Per-class vtable
   (method_id → index at class build; call sites cache class+epoch+index) + the
   Dev-commissioned parallel-safe method analysis (walker certifying self-only
   methods, cached on class_definition, epoch-validated, barrier-warmed) replacing
   the blanket worker method wall. CALL_METHOD 420/tick × ~7.3k cyc.
4. **Environment-ectomy — PENDING (endgame, multi-session).** Environments cease to
   exist at runtime: escape marks already computed at parse, non-escaping locals are
   register slots (true today), escaping locals become heap cells at declaration,
   closures capture cells; DELETE the env-cache/epoch/serial apparatus rather than
   port it. CALL_FROM_SCRATCH still owns ~43% of attributed VM wall — this plus
   point 3 is where that lives. Flatstack stages 3-5 are stepping stones.

## Standing rulings (memory has the full versions)

- checked_result STAYS (2026-07-10 ruling): script errors are routine control flow;
  optimize by STARVING it (fewer dispatches), then single-register success test,
  then out-of-band error details. Never propose exceptions/longjmp for script errors.
- Generality first; semantics are sacred (value/ref semantics never bend for perf);
  red-first reproducers; never route tests around broken behavior; byte-identical
  backend parity including error text (shared kernels in detail/ preferred over twins).
- GLOOM scripts stay SAFE-mode (never rely on allow_unsafe_parallel).
- Read `Source/JaiScript/docs/invariants.md` before touching value layout, strong_ptr,
  raw-index dispatch, or the vm run loop. One-line commit messages, no co-author
  trailers. Dev handles all git push/checkout.

## Gate recipe per commit (unchanged)

Debug suites ×2 backends → Release suites ×2 → fuzz 3000 (`jaiscript_fuzz.exe
--iterations 3000`; 20-21 suppressed seeds = known family) → GLOOM smoke 600t hash +
byte-identical frame parity, workers 0/2/8 → quiet timing A/B with interp control.
CRT asserts print to stderr in the test runner now (a Debug assert used to hang
headless runs behind a dialog).

## New-machine expectations

Single-thread should jump ~2-4×; the parallel regions get real cores (re-tune worker
sweet spots — the old machine's parallel amortization numbers are meaningless now).
The 16 ms GLOOM target was an old-machine number: re-derive the target from the new
Squirrel same-day comparator (the ~3× ratio is the honest yardstick, not the
milliseconds). Perf-focused, feature-focused — same discipline, faster iterations.
