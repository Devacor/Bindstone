# `parallel_for` enforcement UX: prove-or-serial (companion proposal)

Status: RULED 2026-07-06 — **Dev ruled (A), pure contract semantics**: "B just makes the
language insecure. I'd rather error." Silent serialization (fork B, and the hybrid's GO/NO-GO
serial fallback) is dead: a `parallel_for` body either satisfies the contract or ERRORS —
never quietly runs serial. What survives fork-independently: the §3 checklist repurposed as
*early static diagnostics* for the same contract violations (erroring at parse time instead of
runtime is aligned with the ruling; silent fallback is not), the §4 prerequisites (ordered
reductions non-negotiable), and the §5 pmap-first sequencing. Body kept unedited below as the
decision record.

Was: COMPANION PROPOSAL to `parallel_design.md` (rewritten 30d3aa28) — not a replacement.
Everything RULED there stays ruled and is assumed here: one engine + `thread_storage` pads
(§1), static contiguous chunks (§3), the three-level binding annotation + ordered command
buffer (§5), iteration-order error selection (§6), `jai::thread_pool` (§7), deterministic pads
as the testable contract (§8). This note argues one remaining decision — the **enforcement
UX**: what a user experiences when a body is not (or cannot be proven) parallel-safe — and
proposes a hybrid that keeps the ruled semantics while adding a prove-or-serial execution
decision. Nothing below is implemented.

## 1. The fork for Dev to rule on

**(A) Contract semantics — the design as ruled.** A `parallel_for` body that writes enclosing
state is an ERROR, always. Calling an unannotated binding inside the region is an ERROR,
always. The construct is parallel-by-construction: writing `parallel_for` is a claim about the
body, the engine holds you to it, and the error teaches the model ("cannot write enclosing
state in a parallel body"). Deterministic, honest, and the rules never depend on how smart the
implementation is.

- Weakness: every sharp edge is a runtime error a designer can ship. The failure mode of
  "user just did *that*" is a thrown region at 2am, not a slow region.

**(B) Hint semantics — prove-or-serial as originally floated.** `parallel_for` means exactly
what `for` means; the engine fans out only when a static prover can show non-interference,
otherwise it silently runs serial and a warn-mode diagnostic says why (vectorization-report
UX). Impossible to hold wrong: the worst outcome is "no speedup."

- Weakness, and it is disqualifying on its own: if serial execution relaxes the rules, an
  outer write becomes **legal-when-serial**. Then `parallel_for` degrades to "`for` with a
  hint," program behavior depends on what the prover could see, and a smarter prover in a
  later release turns yesterday's working (serial) program into today's erroring (parallel)
  one. Semantics must never be a function of optimizer strength.

## 2. The composition insight, and the hybrid

The two forks are not actually opposed, because of a property the ruled design already has:
**the serial lowering of a region fills the SAME pads in the SAME order as the parallel
execution** — static chunks fix iteration→pad assignment before the fork (§3), the command
buffer applies in iteration order either way (§5), print buffering concatenates in chunk order
(§8). Sequencing step 2 of the design doc ("`parallel_for` sequential-semantics first, running
on ONE thread") is precisely this lowering, and §8's 1-thread-vs-N fuzz leg byte-compares it
against real fan-out. So under the existing contract, serial-vs-parallel is already
observationally identical — **the execution strategy decides SPEED, never MEANING — provided
the region rules are enforced identically in both modes.**

Hence the hybrid, which I recommend over either pure fork:

- **Contract semantics for the rules (fork A, as ruled).** The write wall, the binding
  annotations, pads, command buffer, no-`yield`, print buffering — enforced ALWAYS, at the
  same store/call chokepoints, whether the region happens to run on one thread or N. An outer
  write is an error even in serial mode. No legal-when-serial programs exist.
- **Prove-or-serial for the GO/NO-GO decision (fork B, demoted to an optimizer).** Before
  fanning out, a static pass over the body (the `check_mode` walker already exists and is
  amortized once per unique source) decides whether parallel execution is worth attempting.
  Anything it cannot prove — or anything below a cost threshold — runs the region through the
  step-2 serial lowering instead. Because the rules are enforced either way, the prover can be
  arbitrarily conservative at **zero semantic cost**: rejection is not a behavior change, it
  is a missed speedup with a diagnostic.

What the hybrid buys concretely:

- The "user did *that* and we busted" class splits cleanly: rule violations stay loud,
  deterministic errors (fork A's teaching), while *provability* failures — the long tail of
  "the analysis couldn't be sure" — degrade to serial instead of degrading to either raciness
  or spurious errors.
- Bodies the prover rejects still hit the runtime walls if they actually violate the contract
  — so the prover needs no soundness-for-safety burden at all. It is purely an admission
  filter; the chokepoints remain the enforcement.
- It reuses step 2's lowering as a permanent, legal execution strategy rather than scaffolding
  to be deleted — and gives the engine a knob for small-`n` regions where the barrier alias
  walk + first-touch gate (§4) cost more than the loop.
- Diagnostics ride the existing surface: warn mode explains "did not parallelize:
  <reason at line:col>" via `last_check_diagnostics()`, exactly like a compiler vectorization
  report; strict mode could optionally promote "would not parallelize" to an error for teams
  that want the claim checked at parse time.

## 3. The GO/NO-GO rejection checklist (the prover's admission filter)

Verbatim from the review that prompted this note — each item maps to the ruled mechanism that
keeps it safe-or-error regardless of the prover's verdict:

- **closure captures a mutable upvalue → reject.** (Runtime: reads are legal via the
  first-touch clone gate §4b; writes hit the wall §4c. The prover rejecting merely avoids
  paying the gate for a body that will error anyway if it writes.)
- **body touches the container being iterated (`arr.push` mid-loop) → reject.** (Runtime:
  enclosing-write wall.)
- **any host call not whitelisted → reject** — this is the big hammer; the ~76 registrar
  sites are unannotated on day one, so nearly every gameplay script rejects, correctly.
  (Runtime: unannotated bindings raise at the call site §5 — under the hybrid, "reject to
  serial-with-the-same-rules" is arguably the better day-one experience than "raise," and the
  annotation sweep then *unlocks speed* rather than *unbreaks scripts*.)
- **reads a non-const global → reject** (conservative tier; can be admitted later via the
  first-touch gate once hardened).
- **can't see the whole body (dynamic dispatch through `var`, function values from outer
  scope, anything `eval`-shaped) → reject.**

Conservatism is free. Start with "admit only bodies whose every call target is a script
function the checker can walk or a `self_only`/`read_world` binding, and whose every store is
pad/element-local"; widen tier by tier with the fuzz 1-vs-N leg as the gate.

## 4. Engine-internal prerequisites this leans on

Mostly restating §2/§6 obligations with three sharpenings:

- **Atomic or per-worker-rollup memory-cap accounting.** `execution_limits` is per-engine
  instance state and today reentrant executes share the outer allowance — under fan-out that
  field is contended. §2 already rules per-worker accounting that rolls up; flagging that the
  *cap raise* must also stay deterministic-enough to test (a worker's first-raise-catchable /
  second-raise-terminal dance is per-worker state).
- **`execution_budget` across workers needs a defined meaning.** Proposal: each worker carries
  the full per-execute budget clock for its chunk (region wall time is then bounded by the
  slowest worker), and a budget overrun in any worker fails the region through the §6
  iteration-order rule. Note honestly: budget overruns are wall-clock and therefore
  timing-dependent even single-threaded today — the 1-vs-N byte-compare contract must carve
  out budget-overrun runs or fuzz with budget disabled.
- **Iteration-order throw selection** — already ruled (§6); the hybrid adds nothing but
  inherits it identically in serial mode (first error in iteration order is trivially the
  first error encountered).
- **ORDERED float reductions are non-negotiable.** Float addition is non-associative; any
  built-in merge/reduce helper ever added over pads MUST combine in pad (= iteration) order
  regardless of thread count. The ruled design is naturally safe here — the merge is
  user-written serial code over the pad array — keep it that way, and never add an unordered
  tree-reduce "optimization." This is load-bearing for server lockstep AND for the
  byte-compare parity/fuzz culture (§8): one unordered sum poisons both.
- Everything else per §2 as ruled: per-worker execution contexts, env/value pools, interner
  locking, frozen parse cache/class defs/chunks during the region.

## 5. Sequencing recommendation: `pmap` first (v0)

Before `parallel_for` touches objects, ship the narrowest possible surface that still forces
the per-thread engine internals to exist and harden:

- A **`pmap`-style builtin** (or the `parallel_for` keyword restricted to this tier):
  value-semantic elements only (int64/double/string/char/bool and arrays/maps of the same — no
  objects, no references), body admitted only if it is a pure function of the element by the
  §3 checklist (no bindings at all in v0, or `self_only` on the element).
- Why this tier first: value-semantic elements make the barrier alias walk trivial or
  skippable (nothing reachable is shared), there is no annotation sweep dependency, no command
  buffer needed, and no object-identity questions — yet it still requires per-worker execution
  contexts, static chunking, per-worker budget/cap rails, pad determinism, and the 1-vs-N fuzz
  leg. Every scary part that remains after `pmap` is green is by construction an *object* /
  *aliasing* / *binding* problem, not an engine-internals problem.
- Fit against `parallel_design.md` §12: lands between step 3 (per-thread contexts) and step 4
  (alias walk + first-touch + full fan-out) — i.e. step 3.5, "real threads on the trivial
  domain" — and gives step 4 a working substrate to diff against.
- It also covers the honest near-term use case (math over big arrays); creature-update
  parallelism was never the frame win (§10).

## 6. Costs of the hybrid over pure contract semantics

- The GO/NO-GO pass itself: one extra AST walk per unique `parallel_for` source, amortized in
  the parse cache like `check_mode` — near-zero.
- Two execution strategies to keep observationally identical forever — but §8's 1-vs-N fuzz
  leg is *already* that test; the hybrid just makes the serial leg shipping behavior instead
  of test scaffolding.
- A risk to name: users may lean on serial fallback and never learn why their loops don't
  speed up. Mitigation is the diagnostic surface (§2 above) — and if that proves insufficient,
  strict mode's "would not parallelize is an error" gives back fork A's teaching without
  giving up the hybrid's safety.

## 7. The one decision needed

Rule on the fork: **(A) pure contract** (unprovable-but-legal bodies fan out and rely on
runtime walls alone), **(B) pure hint** (rejected for the legal-when-serial degradation), or
**(hybrid) contract rules + prove-or-serial GO/NO-GO** (recommended). Everything else in this
note is implementation detail under whichever ruling lands.

**RULED: (A).** Dev, 2026-07-06: "B just makes the language insecure. I'd rather error." One
construct, one meaning, one failure mode — an error. No execution-strategy ambiguity, nothing
whose behavior depends on analysis strength. The §3 checklist may still ship as strict-mode
*parse-time errors* for contract violations (earlier detection of the same error, never a
behavior fork); any purely-internal cost heuristic must preserve "legal body always fans out,
illegal body always errors."
