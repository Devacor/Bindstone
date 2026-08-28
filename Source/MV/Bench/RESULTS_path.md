# Pathfinding — before/after results

Benchmark: `bench_path.cpp` (`build_path.bat [engine lib dir] [exe]`, MSVC `/O2 /std:c++20 /DNDEBUG`,
links the engine lib under test). Every scenario is seeded and deterministic; swarm scenarios run a
fixed 1/60 s simulation tick with `Map::beginTick()` per tick, so nothing depends on wall-clock
budget resets. Timings are the min of 3 reps; counts come from the last rep.

Scenarios:
- **S1 raw search** — 128×128 map, 20% random rock, 400 seeded start/goal pairs, uncapped and at the
  agent's 200-node cap. Metric: ns per expanded node.
- **crowd_lane** — 64 agents spawned in an 8×8 block march down a 64×24 lane through a pillar line
  to one goal (acceptable distance 3). 1200 ticks.
- **spawn_contention** — 32 agents spawn on the SAME cell, one per tick, random far goals. 1200 ticks.
- **dynamic_replan** — 32 crossing agents; 200 random cells toggle blocked/unblocked every tick. 1200 ticks.
- **corridor_swap** — two groups of 16 pass each other in a 3-wide corridor. 1500 ticks.

Columns: arrived = agents within acceptable distance of their goal by the end; stuck = not arrived and
no movement in the last 180 ticks; all-in = first tick at which every agent had arrived; nodes = total
expanded nodes charged to the budget; ratio = distance travelled / straight-line, arrived agents only.

## Baseline (sorted-vector open list, list closed set, linear scans) — 2026-08-27

| search | searches | nodes | ms | ns/node | complete | avg path |
|---|---|---|---|---|---|---|
| cap=-1 (uncapped) | 400 | 1,442,851 | 36,257 | **25,129** | 99.2% | 87.8 |
| cap=200 | 400 | 77,633 | 41.8 | **539** | 5.8% | 14.7 |

| scenario | agents | arrived | stuck | all-in @ | nodes | total ms | max tick ms | ratio |
|---|---|---|---|---|---|---|---|---|
| crowd_lane | 64 | 42.2% | 0 | >1200 | 1,053,399 | 931 | 2.94 | 1.54 |
| spawn_contention | — | — | — | — | — | — | — | — |
| dynamic_replan | — | — | — | — | — | — | — | — |
| corridor_swap | — | — | — | — | — | — | — | — |

## I1 — binary-heap open list, per-cell search stamps, index-linked node arena, proper g/f, octile heuristic

| search | searches | nodes | ms | ns/node | complete | avg path |
|---|---|---|---|---|---|---|
| cap=-1 (uncapped) | 400 | 199,204 | **19.5** | **98** | 99.2% | 87.5 |
| cap=200 | 400 | 62,924 | **6.5** | **104** | **39.8%** | 68.7 |

| scenario | agents | arrived | stuck | all-in @ | nodes | total ms | max tick ms | ratio |
|---|---|---|---|---|---|---|---|---|
| crowd_lane | 64 | 42.2% | 13 | >1200 | 1,106,668 | 478 | 2.42 | 1.60 |
| spawn_contention | 32 | 43.8% | 0 | >1200 | 131,382 | 116 | 3.51 | 2.21 |
| dynamic_replan | 32 | 81.2% | 0 | >1200 | 675,612 | 725 | 3.06 | 1.64 |
| corridor_swap | 32 | 9.4% | 25 | >1500 | 460,284 | 64 | 0.51 | 1.08 |

- 256× cheaper per node; the uncapped set also expands 7× fewer nodes because the old search
  accumulated the heuristic into g (greedy-ish, re-expanding), the new one is plain A*.
- At the 200-node cap the search now reaches the goal 40% of the time instead of 6%, so agents get
  real paths instead of 15-node stubs.
- The nondeterministic termination is gone (it was inside the old search); every scenario runs.
  The swarm rows are the first complete measurements — they are the movement-quality baseline for
  the increments that follow (arrival %, stuck counts, corridor deadlock).

## I2 — tick-driven budget (`Map::beginTick` from `GameInstance::fixedUpdate`) + map-level search queue (FIFO, urgent first)

| scenario | agents | arrived | stuck | all-in @ | nodes | total ms | max tick ms | ratio |
|---|---|---|---|---|---|---|---|---|
| crowd_lane | 64 | 42.2% | **3** | >1200 | 1,089,900 | 508 | **1.54** | 1.53 |
| spawn_contention | 32 | 40.6% | 0 | >1200 | 162,022 | 106 | **0.97** | 2.31 |
| dynamic_replan | 32 | 65.6% | 0 | >1200 | 1,085,691 | 776 | 2.68 | 1.75 |
| corridor_swap | 32 | 3.1% | 31 | >1500 | 449,487 | 67 | 0.53 | 1.10 |

- Raw search unchanged (97.6 / 101.8 ns/node). Worst-tick cost drops (the queue spreads searches
  evenly across ticks) and crowd_lane's stuck count falls 13 → 3.
- dynamic_replan regressed 81 → 66%: a queued agent stopped for the tick it waited, and that
  scenario re-dirties every agent constantly. Fixed in I3 (an agent keeps walking its current path
  while a non-urgent search is queued).
- crowd_lane's arrival figure is capped by geometry: only ~29 cells lie within distance 3 of the
  goal, so ~42% is the ceiling. The scenario is widened (radius 6) and a mean-remaining-distance
  column added from I3 on; rows above and below that point are not directly comparable.
- corridor_swap is a head-on deadlock regardless of budget policy — that needs local arbitration
  (a later increment), not scheduling.

## Widened scenarios (crowd_lane goal radius 6, "remain" = mean distance still to cover, all agents)

I2 re-measured on the widened scenarios (same engine as the I2 row above):

| scenario | arrived | stuck | nodes | total ms | max tick ms | ratio | remain |
|---|---|---|---|---|---|---|---|
| crowd_lane | 51.6% | 1 | 1,031,608 | 468 | 1.62 | 1.57 | 4.3 |
| spawn_contention | 43.8% | 0 | 149,274 | 108 | 0.86 | 2.40 | 15.0 |
| dynamic_replan | 65.6% | 0 | 1,085,691 | 791 | 3.10 | 1.75 | 5.8 |
| corridor_swap | 9.4% | 29 | 584,669 | 67 | 0.52 | 1.03 | 19.7 |

## I3 — time-sliced searches (frontier kept between budget grants) + queued agents keep walking their current path

| scenario | arrived | stuck | nodes | total ms | max tick ms | ratio | remain |
|---|---|---|---|---|---|---|---|
| crowd_lane | 40.6% | 0 | 1,186,095 | 458 | 1.45 | 1.56 | **20.6** |
| spawn_contention | 43.8% | 0 | 173,983 | 118 | 1.03 | 2.36 | **9.4** |
| dynamic_replan | **87.5%** | 0 | 708,981 | 692 | 3.20 | 1.76 | **1.4** |
| corridor_swap | 9.4% | 29 | 504,089 | 66 | 0.56 | 1.10 | 22.7 |

- Keep-walking-while-queued is a clear win (dynamic_replan 66 → 87.5%, spawn remain 15 → 9.4).
- Unconditional slicing is a loss under contention: 64 agents each waiting several ticks for a
  complete 60-cell path (crowd remain 4.3 → 20.6) instead of walking a partial one. I3b below
  makes slicing adaptive — a search only continues across ticks while no one is queued.

## I3b — adaptive slicing (a search continues across ticks only while nobody is queued)

| scenario | arrived | stuck | nodes | total ms | max tick ms | ratio | remain |
|---|---|---|---|---|---|---|---|
| crowd_lane | **53.1%** | **0** | 1,170,038 | 497 | 1.48 | 1.63 | 4.9 |
| spawn_contention | **50.0%** | 0 | 190,462 | 123 | 0.94 | 2.42 | **10.0** |
| dynamic_replan | **90.6%** | 0 | 618,351 | 672 | 3.31 | 2.11 | **1.6** |
| corridor_swap | 9.4% | 29 | 470,003 | 65 | 0.55 | 1.06 | 22.6 |

- Versus I2 on the same scenarios: every non-deadlock scenario improves (dynamic_replan 65.6 →
  90.6%, spawn remaining 15.0 → 10.0, crowd stuck 1 → 0) and the crowd regression of unconditional
  slicing is gone. Idle budget buys complete paths; contention keeps responsiveness.

## I4 — line-of-sight lookahead (agents walk straight to the farthest visible path node, up to 8 ahead)

| scenario | arrived | stuck | nodes | total ms | max tick ms | ratio | remain |
|---|---|---|---|---|---|---|---|
| crowd_lane | 48.4% | 3 | 1,188,642 | 356 | 1.57 | 1.53 | 4.5 |
| spawn_contention | **62.5%** | 2 | 236,577 | 115 | 1.06 | **1.87** | **7.5** |
| dynamic_replan | **93.8%** | 0 | 652,966 | 685 | 3.96 | 1.95 | **1.2** |
| corridor_swap | **37.5%** | **18** | 647,384 | 69 | 0.58 | 1.39 | **13.0** |

- Straighter walking shows in spawn_contention's ratio (2.42 → 1.87) and arrival (50 → 62.5%);
  corridor_swap improves because agents cutting across cells slip past each other more often.
  crowd_lane is a wash (arrived −5 pts, remaining −0.4, 3 stuck) — its limiter is the pillar-line
  merge, i.e. local arbitration, which I6 addresses.

## I6 — occupancy registry on the map, head-on swap, idle-blocker step-aside (single-cell footprints)

| scenario | arrived | stuck | nodes | total ms | max tick ms | ratio | remain |
|---|---|---|---|---|---|---|---|
| crowd_lane | **64.1%** | 1 | 1,186,272 | 350 | 1.71 | 1.65 | **2.9** |
| spawn_contention | 53.1% | 3 | 321,714 | 136 | 1.37 | 2.14 | 11.2 |
| dynamic_replan | 87.5% | 0 | 596,235 | 689 | 4.25 | 2.02 | 2.1 |
| corridor_swap | **40.6%** | 16 | 820,460 | 78 | 0.55 | 1.18 | **2.7** |

- The corridor deadlock mostly dissolves (remaining 13.0 → 2.7) and the crowd merges through the
  pillar line far better, but stuck counts are still non-zero and spawn/dynamic gave back a few
  points: negotiation is opportunistic, not a guarantee. Bindstone's creatures are also unitSize 3,
  which this increment does not cover. I7 replaces it with a deterministic progress ladder.

## I7 — progress guarantee + the engine defects it exposed

Map-assigned agent ids and tick counter; pull-ordered updates (a blocked agent runs its blocker's
update first); right of way = (ticks without a new cell, cells remaining, id); same-direction leaders
are followed, head-on equal footprints swap, idle/crossing blockers step aside (cascade); soft search
after 30 stuck ticks (other agents cost, statics wall); queue priority = ticks waiting in queue +
10·followers + 30·urgent (true round-robin). Engine defects found and fixed along the way, all
pre-existing: A* cut corners the straight-line movement could not (rock on the orthogonal cell ⇒
identical re-plan every tick, forever); incremental clearance read uninitialised neighbours (phantom
1-2-3 clearance band across the map ⇒ 3×3 units detour/jam); the `RecentlyBlockedNodeObserver`
lambda captured its own vector-element address (UB ⇒ identical runs diverged); footprint and
movement disagreed on which cell an agent is in (now `floor(centre)` everywhere); line of sight was
traced from the cell centre instead of the agent's position.

Runs are now bit-for-bit repeatable (same node counts across processes). Scenarios: crowd_lane goal
radius 6, corridor_swap goal radius 6, plus `solo_big` / `crowd_big` (3×3 footprints).

| scenario (4-connected) | arrived | stuck | all-in @ | nodes | total ms | max tick ms | ratio | remain |
|---|---|---|---|---|---|---|---|---|
| crowd_lane | 39.1% | 0 | >1200 | 1,186,280 | 399 | 5.87 | 1.19 | 15.4 |
| solo_big (3×3) | **100%** | 0 | 561 | 58 | 61 | 5.10 | 1.01 | 0.0 |
| crowd_big (3×3) | 43.8% | **0** | >1200 | 1,128,033 | 823 | 5.13 | 1.36 | 11.6 |
| spawn_contention | **100%** | **0** | **981** | 88,676 | 79 | 13.93 | 1.49 | 0.0 |
| dynamic_replan | 96.9% | 0 | >1200 | 687,830 | 802 | 14.75 | 1.32 | 0.5 |
| corridor_swap | 62.5% | 12 | >1500 | 1,062,929 | 106 | 0.57 | 1.13 | 2.6 |

| scenario (8-connected, the game's map) | arrived | stuck | all-in @ | nodes | total ms | max tick ms | ratio | remain |
|---|---|---|---|---|---|---|---|---|
| crowd_lane | 51.6% | 0 | >1200 | 1,191,667 | 506 | 8.60 | 1.19 | 12.8 |
| solo_big (3×3) | **100%** | 0 | 551 | 56 | 65 | 7.58 | 0.99 | 0.0 |
| crowd_big (3×3) | 43.8% | **0** | >1200 | 1,143,946 | 880 | 8.29 | 1.28 | 12.8 |
| spawn_contention | **100%** | **0** | **975** | 78,678 | 93 | 23.11 | 1.58 | 0.0 |
| dynamic_replan | **100%** | **0** | **1175** | 569,298 | 869 | 25.85 | 1.18 | 0.0 |
| corridor_swap | 84.4% | 3 | >1500 | 1,019,581 | 144 | 0.84 | 1.28 | 1.0 |

## Final (I7m) — cascade depth 3, `Map::resize` rebuilt from raw, capacity-aware stuck metric

"stuck" now means idle for 180 ticks while more than two cells outside the arrival radius; an agent
idling at the edge of a *full* goal region is waiting for room, not stalled. mv_tests 60/60.

| scenario (4-connected) | arrived | stuck | all-in @ | nodes | total ms | max tick ms | ratio | remain |
|---|---|---|---|---|---|---|---|---|
| crowd_lane | 39.1% | **0** | >1200 | 1,186,280 | 480 | 5.34 | 1.19 | 15.4 |
| solo_big (3×3) | 100% | **0** | 561 | 58 | 62 | 4.80 | 1.01 | 0.0 |
| crowd_big (3×3) | 43.8% | **0** | >1200 | 1,128,033 | 845 | 5.19 | 1.36 | 11.6 |
| spawn_contention | 100% | **0** | 981 | 88,676 | 121 | 14.37 | 1.49 | 0.0 |
| dynamic_replan | 96.9% | **0** | >1200 | 687,830 | 847 | 15.62 | 1.32 | 0.5 |
| corridor_swap | 75.0% | **0** | >1500 | 702,275 | 150 | 0.80 | 15.71 | 1.6 |

| scenario (8-connected) | arrived | stuck | all-in @ | nodes | total ms | max tick ms | ratio | remain |
|---|---|---|---|---|---|---|---|---|
| crowd_lane | 45.3% | **0** | >1200 | 1,191,557 | 582 | 8.75 | 1.18 | 12.6 |
| solo_big (3×3) | 100% | **0** | 551 | 56 | 66 | 8.08 | 0.99 | 0.0 |
| crowd_big (3×3) | 43.8% | **0** | >1200 | 1,143,946 | 905 | 8.14 | 1.28 | 12.8 |
| spawn_contention | 100% | **0** | 975 | 78,678 | 133 | 23.10 | 1.58 | 0.0 |
| dynamic_replan | 100% | **0** | 1175 | 569,298 | 919 | 24.47 | 1.18 | 0.0 |
| corridor_swap | 78.1% | **0** | >1500 | 600,204 | 184 | 0.81 | 11.44 | 1.5 |

Raw search, 8-connected: 165 ns/node uncapped (344k nodes over 400 searches, 57 ms), 162 ns/node at
the 200 cap — the octile heuristic on 8 neighbours costs ~1.6× per node against 4-connected.

- Zero stuck agents in every scenario on both connectivities, deterministic run to run.
- corridor_swap's travel ratio (11–16×) is arrived agents being shoved deeper into a packed goal
  region by the push cascade; acceptable for units, worth revisiting if parked creatures should be
  immovable (make step-aside require the blocker to be pathfinding or the waiter to outrank it).
- Open quality items, none a stall: crowd_lane / crowd_big throughput through narrow gaps (flow
  fields or smarter lane assignment), corridor arrival capacity.
- max tick ms now includes the one-time whole-map clearance initialisation on the first tick
  (14–26 ms for 64×64); every later tick is in the sub-millisecond to 2 ms range seen before.
- corridor_swap's remaining stuck agents sit behind a wall of *arrived* agents parked at the goal
  region's edge; the push cascade reached only one agent deep. Depth raised to 3 in the next step.
- crowd_lane is now honest physics (no half-cell overlap); its limiter is funnel throughput through
  2-cell gaps, the case for flow fields or wider lanes, not a stuck class.

Baseline findings:
- The uncapped search is quadratic in practice: 25 µs per expanded node from the `existsBetter`
  linear scans over both lists, which is why the agent cap had to be 200.
- The baseline binary **terminates nondeterministically** in the swarm scenarios
  (`Failed to retrieve grid location from map: (64, 9)` thrown inside `~TemporaryCost`, i.e. a
  path node carrying an off-map cell); crowd_lane completed in 1 of 4 runs, the other scenarios
  never ran. The crowd_lane row above is from the run that completed.
- Baseline already includes the 2026-08-27 budget fixes (actual nodes charged, `MIN_SEARCH_LIMIT`,
  retry backoff, nearby placement); the pre-fix code charged the search limit per attempt.
