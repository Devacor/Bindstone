# Hardening 2: exception-safe parallel region lifetime

Status: implemented and verified on both backends; baseline is Task 1 commit
`0c2a452f`. No numeric or source-resolution policy changes.

## Decisions

- Extend the existing region cleanup into one owner for the thread table,
  activation, prior symbolizer/table state, submitted-work join obligation and
  borrowed captures/cursors. Acquire the join obligation before a possibly
  partial batch submission. Join before destroying storage; restore state before
  fallible instance registration. Preserve a host's already-frozen symbolizer.
- Failed setup discards partially prepared worker slots. Private worker callable
  environments are explicitly cleared before their epoch sinks die, breaking
  ownership cycles during failed construction, replacement and normal teardown.
- Prepare reusable worker argument vectors at the main-thread barrier. Their
  allocation can no longer escape outside the worker's error handling and be
  silently swallowed by the pool. The same region owner clears their references.
- A partially accepted batch wakes its accepted tasks even when a later append
  throws. Partial thread-pool construction uses the same stop/join cleanup as
  normal pool destruction and rethrows the original exception.
- One instance-registration append kernel publishes the instance's registry index
  only after its vector append succeeds. Normal and post-region registration use
  the same kernel, preserving the normal path's existing lock.
- Registration drains consume only their successful/expired prefix. If a join or
  partial submission fails, the surviving suffix stays engine-owned and is retried
  before the next region can replace/evict slots, or before class reload can omit
  those survivors. Both backend reload builders enter the same `clear_asts` hook
  before their first old-class mutation; direct host `redefine_class` calls retain
  a cold safeguard too. No per-call, member-access or per-execute check is added.
  A second failure in this recovery step preserves the old constructor/method;
  this is not a new promise of transactional rollback for arbitrary later reloads.
- Fault injection is instance-owned, opt-in and disabled by default; no mutable
  static or thread-local state. Checkpoints name source-level allocation and
  submission stages, not every internal allocation performed by STL or backend
  execution. Production exception texts are not replaced with test-only messages.
- Existing in-place regions remain nontransactional on failure: completed writes
  are not rolled back. Integer arithmetic, permitted wrapping, numeric types and
  source-resolution policies are untouched.

## Verified by running

The following regressions were observed on the unfixed implementation on both
backends before their respective fixes:

| Regression | Observed failure |
| --- | --- |
| task storage after activation | dangling active-region table |
| partial batch submission | accepted prefix needed a later rescue wake |
| partial thread startup | isolated process aborted in `std::thread::~thread` |
| worker argument allocation | exception swallowed, region incorrectly succeeded |
| instance registry append | failed instance destruction unregistered a later survivor |
| failed worker provisioning | host callable remained alive through an environment cycle |
| retry after failed registry drain | one survivor registered twice (513 instead of 512 entries) |
| different region after partial submission | 64 surviving copies missing (448 instead of 512 entries) |
| hot reload before another region | 191 surviving copies missing (321 instead of 512 entries) |
| repeated failure at a late reload hook | old constructor/method changed from `5\|100` to `1005\|1000` |

The prior-frozen-state regression was also observed failing on the VM before the
guard fix. Targeted pool (10 tests), worker-argument and registry tests then passed
on both backends. The first named-stage sweeps passed on interpreter (40.967 s)
and VM (25.319 s), including fresh/reused slots and recovery through the identical
compiled region. The first complete Debug runs passed 2,624 tests per backend
(interpreter 111.837 s, VM 89.728 s, concurrent with builds), and the Release
integration asset gate passed 76/76. These runs predate the two extra immediate
reload regressions. The final implementation passed:

| Gate | Interpreter | VM |
| --- | --- | --- |
| Debug full suite | 2,626 tests, 102.827 s | 2,626 tests, 80.165 s |
| Release full suite | 2,746 tests, 56.252 s | 2,746 tests, 29.137 s |
| Final registry/reload fault suite (Debug) | 5 tests, 0.316 s | 5 tests, 0.222 s |

Full runs include 13 added tests: three pool tests, five region fault tests and
five registration/reload fault tests. The ordinal sweep injects each checkpoint
visit reached by three region shapes (primitive transform, primitive in-place,
class-valued transform), each with fresh and reused slots. Dedicated pool tests
cover first/partial batch acceptance and partial thread construction. This is not
an allocator-replacement campaign over every nested allocation or every possible
script body. Suite elapsed times are correctness-run observations, not A/B data:
the Debug runs overlapped the final link and each backend pair ran concurrently.

The final Release Bindstone integration build completed; `-verifyassets` passed
76/76. Both GLOOM backends at workers 0 and 8 retained frame
`7fecc09815bb64e0` and state `3580805725` in all 12 A/B invocations (24 backend
rows). `git diff --check` passed. No production error text was added; existing
host exception translation remains shared by both backends.

## Baseline for controlled measurements

Previous commit: `0c2a452f`. Release binaries were preserved before Task 2 edits
under `out/hardening-baselines/0c2a452f/`.

- `jaiscript.exe` SHA-256:
  `622C72EFF6EDA52A142D7C6185F12E4CE4DF4870B137F9ACC0518B2B935E1B94`
- `jai_gloom.exe` SHA-256:
  `11E7DC3B1935F95C2723B892A67610797AC9C4D4FFEF6D21DE5116BADD7EB44E`

The standalone pool A probe uses the exact previous-commit header, not the
instrumented working-tree header. Measurement workloads validate task counts or
the required GLOOM state/frame hashes. Run A/B/A/B/A/B on the same idle machine;
report every observation and the minimum of three for each version.

### Pool batch/wait probe

Same-machine alternating A/B/A/B/A/B; no concurrent build/test workload. Both
header-only binaries use the same `/O2 /DNDEBUG /MT /std:c++20` options. Each runs
128 warmups and 20,000 timed batch/wait iterations; task counts are checked.
Nanoseconds per batch (lower is better):

| Workers | Trial | A: previous commit | B: Task 2 |
| --- | --- | ---: | ---: |
| 1 | 1 | 800.560 | 792.615 |
| 1 | 2 | 784.515 | 872.535 |
| 1 | 3 | 712.115 | 633.795 |
| 1 | minimum | 712.115 | 633.795 |
| 8 | 1 | 9703.000 | 9723.640 |
| 8 | 2 | 9704.015 | 9599.760 |
| 8 | 3 | 9674.705 | 9522.810 |
| 8 | minimum | 9674.705 | 9522.810 |

No evident pool regression in this probe; the descending single-worker timings
are not evidence of a reliable speedup. End-to-end region measurements remain the
more relevant gate and are reported separately.

### GLOOM end-to-end region gate

`--smoke --ticks 300 --seed 666`, from `examples/gloom`, with workers 0 then 8.
Each worker setting ran A/B/A/B/A/B on the same idle machine after all builds and
suites finished. Values are the executable's reported milliseconds for the
300-frame loop, including hashing but excluding session loading/boot:

| Workers | Backend | Version | Trial 1 | Trial 2 | Trial 3 | Minimum |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| 0 | interpreter | A | 2725.1 | 2721.9 | 2720.1 | 2720.1 |
| 0 | interpreter | B | 2754.4 | 2754.8 | 2751.7 | 2751.7 |
| 0 | VM | A | 690.5 | 688.5 | 687.4 | 687.4 |
| 0 | VM | B | 695.2 | 696.6 | 690.4 | 690.4 |
| 8 | interpreter | A | 1685.8 | 1685.7 | 1697.1 | 1685.7 |
| 8 | interpreter | B | 1707.4 | 1708.8 | 1707.1 | 1707.1 |
| 8 | VM | A | 554.5 | 558.7 | 562.6 | 554.5 |
| 8 | VM | B | 560.1 | 566.1 | 560.4 | 560.1 |

Minimum-time differences B versus A are +1.16% / +0.44% at workers 0
(interpreter / VM), and +1.27% / +1.01% at workers 8. These runs show a small
slowdown, not a zero-cost fix or a performance win; no attribution to noise or
binary layout is established. The safety change is retained with this measured
cost explicitly reported. Pool-only results above do not override this result.

Raw stdout/stderr, executable/source hashes, all rows and minima are in
`out/hardening-parallel/gloom-ab-20260905-191929157-43992/`. The reproducible runner
is `out/hardening-parallel/run_gloom_ab.ps1`; generated measurement artifacts are
not part of the source commit.

## Reading-only findings outside this change

Pool calibration currently moves its no-op callables into the first batch and
reuses the moved-from callables for its later two measurements. Correcting that
scheduling heuristic is separate from the lifetime fix; it is not silently changed
here. Admission also still checks host-call whitelist names when unsafe parallel
mode is enabled; the existing broader header description needs a separate policy
decision. Neither observation is claimed as fixed by this task.
