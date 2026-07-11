# Dragon Perf Baseline — full language head-to-head (2026-07-10)

Machine: MSI 18 Dragon (Intel Core Ultra 9 285HX, 8P+16E/24T, High Performance plan,
AC). Tree: VM-perf @ clone-kernel commit (post worker-cap 630b9a7a + INDEX fusion
e67dfc2e). All runs Release BENCHMARKS, quiet machine, sequential. GLOOM: 600 ticks,
seed 666, `--smoke`; **every engine produced STATE_HASH 1503537018 + frame hash
dfd969ed5a336dca — the cross-language conformance contract held on all rows.**
Foundry µs rows are integer µs/iteration (±50% run variance; sub-µs not resolvable).

## GLOOM (whole-game workload), ms/tick — best of 2

| engine | ms/tick | vs Jai VM |
|---|---:|---:|
| **Python 3.11 (standalone port)** | **1.02** | 6.6× faster |
| Squirrel 3.x | 1.25 | 5.4× faster |
| JaiScript VM | 6.71 | — |
| JaiScript interpreter | 10.19 | 1.5× slower |
| Lua 5.4 (sol2) | N/A — vendored `Source/JaiScript/lua` + `sol2` not on this machine |
| ChaiScript 6.1 | N/A — vendored `External/ChaiScript-6.1.0` not on this machine |

Old-machine (2016 MBP) reference: Jai VM 18.6 → 6.7 here; Squirrel 5.57 → 1.25;
machine scaled Squirrel/Python far more than Jai (small hot loops feed modern
front-ends better) — the gap is the campaign target, now honestly ~5.4×.
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

## Restoring the missing columns

Copy from the old machine (untracked vendored sources): `Source/JaiScript/lua`,
`Source/JaiScript/sol2`, `External/ChaiScript-6.1.0`. Then configure with
`-DGLOOM_PORT_LUA=ON -DGLOOM_PORT_CHAI=ON` (gloom rows) — the Lua/ChaiScript
comparison SUITES also re-register then (explains the 2043-vs-2111 Release test count).
Python row runs standalone: `python gloom.py --smoke --ticks 600` in
`examples/gloom/ports/python` (CPython 3.11.0 here).
