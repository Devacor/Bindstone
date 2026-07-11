# Dragon Perf Baseline — full language head-to-head (2026-07-10)

Machine: MSI 18 Dragon (Intel Core Ultra 9 285HX, 8P+16E/24T, High Performance plan,
AC). Tree: VM-perf @ clone-kernel commit (post worker-cap 630b9a7a + INDEX fusion
e67dfc2e). All runs Release BENCHMARKS, quiet machine, sequential. GLOOM: 600 ticks,
seed 666, `--smoke`; **every engine produced STATE_HASH 1503537018 — the cross-language conformance
contract held on all rows** (frame hash dfd969ed5a336dca byte-exact on all engines
except ChaiScript; see footnote).
Foundry µs rows are integer µs/iteration (±50% run variance; sub-µs not resolvable).

## GLOOM (whole-game workload), ms/tick — best of 2

| engine | ms/tick | vs Jai VM |
|---|---:|---:|
| **Lua 5.4.8 (sol2)** | **0.42** | 16× faster |
| Python 3.11 (standalone port) | 1.02 | 6.6× faster |
| Squirrel 3.x | 1.25 | 5.4× faster |
| JaiScript VM | 6.71 | — |
| JaiScript interpreter | 10.19 | 1.5× slower |
| ChaiScript 6.1 | 67.9 | 10× slower |

ChaiScript footnote: its STATE_HASH matches (1503537018) and its own state/frame parity
gates pass, but its frame hash column (101bb2aa63a41304) differs from the byte-identical
value every other engine produces — the chai port's render path is equivalent-but-not
byte-exact. All other engines: byte-exact frames.

Old-machine (2016 MBP) reference: Jai VM 18.6 → 6.7 here; Squirrel 5.57 → 1.25;
Lua (docs) 1.41 → 0.42. The machine scaled the small tight interpreters (Lua 3.3×,
Squirrel 4.4×, Python) far more than Jai (1.6×) — small hot loops feed modern
front-ends better. Campaign yardsticks: 5.4× to Squirrel, 16× to Lua.
Note: raw auto worker count was WORSE than serial on this 24-thread hybrid until the
cap (630b9a7a); Jai numbers here use the capped default (6 workers).

## Microbench head-to-head: JaiScript VM vs Squirrel (same-suite rows, Dragon)

| row | Jai VM | Squirrel | verdict |
|---|---:|---:|---|
| Integer Addition | 0 | 1 | win |
| Float Multiplication | 0 | 1 | win |
| Variable Operations | 0 | 2 | win |
| Function Calls | 0 | 2 | win (tie precompiled 0/0) |
| Array Push/Pop | 1 | 3 | win |
| Map/Table Insert+Lookup | 1 | 3 | win |
| Class Creation | 2 | 2 | tie |
| Method Invocation | 1 | 2 | win (loss precompiled 1/0) |
| For Loop | 2 | 3 | win |
| Range-For/Foreach (10) | 0 | 3 | win |
| String Concat | 2 | 4 | win |
| Null Check | 0 | 2 | win |
| Hot Loop (1000 iter) | 16 | 10 | **loss** (precompiled 17/7) |
| BST 15 nodes (by-ref script) | 94 | 13 | **loss** (naive by-value 425/16; C++-bound 19/12) |

Same shape as the published PERFORMANCE.md story: JaiScript sweeps trivial/statement
rows and containers, loses sustained loop density and allocation/pointer-heavy pure-
script structures — exactly the dispatch-count + call-machinery gap the 4-point plan
attacks (GLOOM's 5.4× lives in those two loss rows).

## Core JaiScript VM anchors (Dragon values — the new gate baselines)

| row | Dragon | old MBP |
|---|---:|---:|
| fib(15) | 197 µs | 687–741 µs |
| fib(15) int params | 208 µs | — |
| Hot Loop 1000 | 18 µs | 44–48 µs |
| Recurse 10 Locals (d=15) | 15 µs | — |

Flatstack gate bands re-anchor to these (hot loop must not move in any stage).

## Selected cross-engine microbench rows (VM backend, Dragon)

| row | Jai VM | Lua 5.4.8 | Squirrel | ChaiScript |
|---|---:|---:|---:|---:|
| Integer Addition | 0-1 | 3 (0 precompiled) | 1 | 1 |
| Variable Operations | 0 | 0 | 2 | 5 |
| Function Calls | 0 | 0 | 2 | 4 |
| Array Push/Pop | 1 | 2 (0 pre) | 3 | 17 |
| Map/Table Insert+Lookup | 1 | 1 | 3 | 11 |
| Method Invocation | 1 | 1 (0 pre) | 2 | 14 |
| For Loop | 2 | 1 | 3 | 12 |
| fib(15) | 205 | **34** | — | — |
| Recurse 10 Locals (d=15) | 15 | — | — | — |

Same story at both scales: Jai owns trivial/statement/container rows, Lua owns
call-dense recursion (fib 6×) and sustained loops — dispatch count + call machinery,
i.e. the 4-point plan. ChaiScript loses every row by 4-50× (BST row skipped at 52.7ms).

## Provenance of the restored columns (2026-07-10)

Lua 5.4.8 pulled from lua.org, sol2 v3.3.0 from the official GitHub release into
`Source/JaiScript/lua` + `sol2` (untracked vendored sources, as before). ChaiScript
6.1.0 revived from Bindstone history (`git restore --source=a04ee58c^ --worktree`,
working tree only — deliberately not re-committed; it was removed from the build in
a04ee58c). The comparison suites re-register when the sources exist.
Python row runs standalone: `python gloom.py --smoke --ticks 600` in
`examples/gloom/ports/python` (CPython 3.11.0 here).
