# Bindstone on JaiScript — completion plan

Written 2026-08-26 from a full read of Bindstone, JaiScript, the git history since the
ChaiScript/Cereal excision, three exhaustive code sweeps (game runtime, MV
property/serialization/Node layer, Workbench/Editor/tests/JaiScript host surface), and a
set of live experiments on this machine. Every item below is tagged **[verified]** (I
reproduced it or read the exact code) or **[judgment]** (my inference — check before
relying on it).

## 1. Where things stand

**Done and green at HEAD (`3d72bdff`)** [verified this session]:
- ChaiScript and Cereal are gone from live code; zero leftovers in `Source/MV` + `Source/Game`.
- Client, GameServer, LobbyServer and `mutedvision.lib` build clean (Release, MSVC 18).
- `Bindstone.exe -verifyassets`: 58/58 (25 scenes/bindsnaps round-trip JSON→binary→JSON,
  2 NetworkAction wire round-trips, 3 catalogs, 30 gameplay scripts eval on both backends).
- `mv_tests`: 18/18. `-workbenchsmoke`: exit 0, zero script errors.
- Workbench (the new editor) loads/edits/saves scenes with a reflection-driven inspector,
  undo/redo, gizmos, hot-reloaded `.jai` panels.

**Review note (2026-08-26):** a second agent tried to refute every claim below. It confirmed
2.1.1, 2.2, 2.4 and all §4 anchors it checked; it refuted two of my mechanisms (2.1.2 "legacy
squeezed proportionally", 2.1.3 "baked UVs trusted") and one step (reordering
`childNodes`/`childComponents`), and found the P1-0 sequencing trap. The text below is the
corrected version.

**Not done** — the reason "the game doesn't fully work":
- The game side (`Source/Game`, `Source/MV`, `Source/Editor`, `Assets`) has had **no commit
  since 2026-07-13** while JaiScript took ~40 commits (member scoping, `::name`, jaibite v5,
  GCC/Clang port). Nothing broke the build, but no game-flow gate exists, so nothing would
  have told us if it had.
- The Login page inside `Game` renders wrong (Dev's report, reproduced + screenshotted).
- Login by button is dead, and the auto-login callback fails with an empty error, on both
  backends (reproduced with the real lobby).
- Nobody has completed a two-client match through the lobby since the migration; the last
  known blocker past login is the MapNode block-semaphore assert.
- Engine-side correctness debt from the port: several scene types are sliced on save,
  `Component::clone` copies one inheritance level, save filters only depth 1, three game
  Components can't be loaded through `shared_ptr<Component>`, and the Node has ~15 latent
  defects (details in §4).
- "Properties embedded" is ~70 `JAI_PROPERTY` declarations, all in `MV/Render/Scene/*.h`;
  Palette, Spine, ParticleChangeValues, the whole Collider family, Text's font/state, and every
  `Source/Game` type still hand-serialize or don't reflect at all.

## 2. Root causes behind the visible symptoms (Phase 1 targets)

### 2.1 Login page: no text, pinched 9-slice pills, wrong atlas region  [verified]

Screenshot of `Game` → Login (`Bindstone.exe -n test1 -p password1 -auto`, `PrintWindow`
capture): light-blue LoginBackground correct; **no text anywhere**; Email/Password boxes are
white pills with dark half-discs at both ends; LoginButton shows a blue swoosh (`slice.png`
region, not `button.png`); a stray white square where the labels belong.

The converted data is *not* the problem: a field-level diff of `Assets/Interface/Login/view.scene`
against the pre-conversion Cereal file (`git show 63e50434~1:...`) shows identical vertex
order, colors, indices and FormattedText state for every sprite/text. Three load-time
mechanisms are:

1. **Text glyph container is orphaned by property load order.**
   `property_manager::load` (JSON branch, `properties/property_serialization.hpp:1006-1016`;
   the binary branch at `:996-1005` reads file order, which `save` wrote from the same map)
   iterates `std::map` order, so a Node loads `childComponents` before `childNodes`.
   `Text::load_and_construct` (`MV/Render/Scene/text.h:220-226`) calls `initialize()`, and
   `Text::initialize` (`text.cpp:77-89`) does `owner()->add(formattedText->scene())` — the
   owner *is* resolvable because `construct::operator()` registers the shared_ptr before the
   body runs (`serialization/construct.hpp`, `archive_impl.hpp:1308-1310`). The owner's
   `childNodes` property then loads and **replaces the vector** (`archive_impl.hpp:986/1010`
   `value.clear()`), dropping the `TEXT_`/`CURSOR_` nodes while their `myParent` still points
   at the owner. Every `Text` loaded from disk renders into a parentless node → no text.
2. **9-slice interior collapses — and it is not a regression.** `button.png` is 128×64 with
   `button.slc` margins 0.3125 top/bottom; the input boxes are 35 px tall, so
   `TextureHandle::applySlicePosition` (`MV/Render/textures.cpp:828-867`) takes the "min > max →
   collapse both rows to the midpoint" branch (26.5/26.5 in today's file). That branch is
   byte-identical since 2018 (`git log -S applySlicePosition`; `471762af:textures.cpp:580-588`).
   The Cereal-era rows at 23.88/29.12 came from different *inputs*: `logicalSize()`
   (`textures.cpp:878-882`) multiplies a `size()`-relative `handlePercent` by `contentSize()`,
   and the old pack was 128×256 with content 128×192 → 47.6 px logical height → 14.88 px
   margins. Today's atlas is 256×128 with `size == contentSize` → 19.7 px margins, 2×19.7 > 35 →
   collapse. So the "old look" was an accident of a pack whose content was smaller than its
   texture, and `logicalSize()`'s size/contentSize mismatch is a real bug for any such pack.
   [review-verified: `git show 471762af` / `63e50434~1`]
3. **The "vertices in the wrong order" look — found by a mesh dump, not by reading:**
   `Drawable::localBounds` became `JAI_TRANSIENT_PROPERTY` in the port ("derived: recomputed
   from points", `drawable.h:344`) but nothing recomputes it after a load — `refreshBounds()`
   only runs when points change. So every loaded Drawable reports an EMPTY `worldBounds()`
   until touched. The LoginButton's three views anchor to their parent Clickable
   (`parentId: "LoginButton"`, anchors 0..1); `Anchors::apply` reads the parent's empty box,
   `boundsImplementation` zeroes the view's corner points p0–p3, and `Sprite::refreshBounds`
   sees "empty == empty" and never re-slices — corners at the origin, interior p4–p15 still
   laid out for 104×36 → triangles stretched to (0,0), the diagonal "swoosh". Cereal serialized
   `localBounds`, which is why the old engine never hit it. Fix: `Drawable::initialize()`
   ends with `refreshBounds()` (runs on every load_and_construct and runtime attach).
   [verified: `-loginshot` mesh dump, all three views p0..p3 == (0,0)]
4. **UVs are re-applied at load already** (my first reading was wrong): every Drawable-derived
   `load_and_construct` calls `initialize()` (`sprite.h:65-69`) → `Drawable::initialize`
   (`drawable.cpp:350-358`) → `texture()` → `Sprite::updateTextureCoordinates` (`sprite.cpp:191-205`),
   which recomputes UVs and slice rows from the live handle after
   `TextureHandle::postLoadInitialize` re-points it. The file's handle (x 0.502–0.998,
   y 0.004–0.496 on 256×128) is exactly `button.png`'s slot in today's `UI.png`. **Open:** the
   "swoosh" on LoginButton is more plausibly the collapsed 104×36 pill than a wrong region —
   confirm with a post-load `handlePercent` dump. Known gap: pack re-resolve overwrites
   `handlePercent` (`textures.cpp:718-720`) and so drops flipX/flipY (encoded as min > max).

### 2.2 Login by button is dead: persisted script receivers load inert  [verified]

`Clickable`'s six signals are `JAI_SIGNAL_PROPERTY` and serialize their script-text
receivers (`onAccept` = `eval_file("Login/loginButton.script");` in the scene). On load,
`signals/signal_serialization.hpp:78` does `engine* eng = ar.get_user_context<engine>()` and
builds `receiver<T>::make(script, eng, names)`. MV constructs readers as
`json_archive_reader ar(contents, engine)` + `ar.set_user_context<MV::Services>(...)`
(`MV/Render/Scene/node.cpp:229-235`); `archive_reader_impl` keeps the engine only in
`engine_ref_` and never registers it as a user context (`archive_impl.hpp:748`, contexts are a
`map<type_index, void*>` at `:694`). So `eng == nullptr`, `receiver::call_script` (`signal_impl.hpp:55`)
is a no-op, and `loginButton.script` never runs — its first-line `print` is absent from every
client log. (Memory item "binding-edges #3" recorded this as an upstream nicety; it is the
login blocker.)

### 2.3 Auto-login callback fails with an empty message  [verified, cause FOUND + fixed]

**Root cause (pinned by `Reentrant Callbacks::captured_parameter_shadowing_a_nulled_global_binds_the_parameter`,
red on both backends before the fix):** explicit captures resolved `environment_->contains(name)`
— which reaches the GLOBAL env — *before* the enclosing frame's parameter/local slots
(`interpreter.cpp` visit_lambda_expr explicit-capture loop; `vm_backend.cpp` exec_closure twin).
`MV::Script::eval` injects a global `self` while evaluating `initialize.script`, the hook's own
`[=](var& self)` parameter shadows it, and the inner `[self]` capture bound the global — which
`eval` nulls afterwards → a null receiver → `Type mismatch` at the `press()` line. Fix: slots
first, then env, in both backends. The empty message was a second bug (§4.5), fixed separately.
Also: a *nested* `execute(script, instance_variables)` from inside a running script pushed its
scope under the caller's frame and then swapped to global (E3's find) — receivers fired from
`press()` inside a script could not see `arg_0`; fixed with a backend-owned instance scope
rooted at global (`push_instance_scope`/`pop_instance_scope`).

*(earlier text, kept for the record)*

With the lobby up, both clients log:
```
JAISCRIPT: Connection Callback And Trying Login!
Script callback failed:
  at <lambda> (<script>:27)
  at <script> (<script>:0)
```
identically under the VM and `MV_SCRIPT_INTERPRETER=1`. Line 27 of
`Assets/Interface/Login/initialize.script` is `self.root().get("LoginButton").componentButton().press();`
inside the `messages.lobbyConnected` receiver (`[self]` capture of the `var& self` param).
`Client::update()` runs the callback on the main thread (`MV/Network/network.cpp:152-195`),
`press`/`get`/`componentButton` are bound, `Clickable::acceptUpClick` swallows nothing to
stderr, and a runner probe shows `[self]` capture of a `var&` param works on both backends.
The message is empty, which `engine_impl.hpp:33` produces when `result.message()` is empty —
**[judgment]** an error in the nested `press()` path that unwinds without a message.
`function_binder.hpp:652-660` symbolizes `e.what()` before building the `checked_result`, so a
C++ exception from `press()`/`get()` would carry text — a dangling-view message is unlikely
here. One cheap experiment: the `[self]` capture is of a `var&` param whose value came from a
call-local `std::vector<script_value>` in the converted-callback wrapper (`engine_impl.hpp:28-31`);
capture `game.gui().page("Login")` instead and see if the failure moves. Needs instrumentation
(§5, P1-4). Independent of 2.2, and must be fixed too: JaiScript should never surface an empty
error.

### 2.3b Lobby rejects every login: "Load Player Parse Fail"  [verified live, fixed]

Once the button fired, both clients got `Load Player Parse Fail: Contact Support`. The lobby's
`catch (...)` hid the reason (now logs `e.what()`). Two layers: (1) `MV::toJsonInline`
(`MV/Serialization/serialize.h`) stripped a Cereal-style `{"value0":` prefix by a fixed 10
characters, but JaiScript's `to_json` writes a non-pointer object *inline at the root* (the
row starts with `"_type_"`), so every seeded player row was corrupted (`e_": "",` …); (2)
`fromJsonInline` re-wrapped the object in `{"value0": …}` so the reader looked inside a key
that doesn't exist → `Property 'detailedRating' not found`. Only owning pointers use the
`value0` wrapper (the B6 convention). Fix: strip only when a `value0` root key exists; wrap
only for `shared_ptr`/`unique_ptr` T. Corrupt local rows must be deleted so the lobby re-seeds
them (`DELETE FROM players WHERE email IN ('test1@bindstone.local','test2@bindstone.local')`).

### 2.3c In the match: every hook fails "Script execution budget exceeded"  [verified live, fixed]

With login and matchmaking working (`Matching [test1] vs [test2]`, all 16 building scripts
loaded on the game server), every C++-invoked hook on server and clients started failing with
`Script execution budget exceeded` about a second in and never recovered. The budget deadline
(`arm_execution_deadline`) is armed only at top-level `execute()`/coroutine resume; converted
callbacks (`std::function` hooks, signal receivers) entered through `external_call_guard`
without re-arming, so wall time since the last `execute()` was charged to every callback.
Fix: `push_external_call_scope` re-arms the deadline when no script is in flight
(interpreter: `execute_depth_ == 0 && call_stack_.empty()`; VM: `frames_.empty()`). Also:
`-auto` now queues for a match (`DefaultLogin::autoMatch` → `AutoMatch` global →
`Main/initialize.script` `onShow` presses `Play`), and freshly created accounts get
`selectedLoadout = "Default"` (was `""`, logging a fallback warning per login).

### 2.4 After login: the known match-time blocker  [verified by reading, not yet reproduced]

`NavigationAgent::blockMap()`/`unblockMap()` (`MV/ArtificialIntelligence/pathfinding.h:732-768`)
each recompute the footprint from the *current* position and pair only on `isBlocking`; any
move while blocked desynchronizes the marked cells and the next unblock trips
`require(blockedSemaphore > 0, "Block Semaphore overextended in MapNode")` (`:880`).
`blockMap` also early-returns with `waitingForPlacement` without setting `isBlocking`, and the
lower loop bound is unclamped (negative `topLeft` indexes `squares[negative]`). Reproduced in
July inside the Workbench sim (`Source/Workbench/PROGRESS.md:338-343`); the sim was removed,
which only made it unobservable. `Source/Tests/pathfinding_tests.cpp` covers footprints but not
block/unblock across a moving footprint.

### 2.4b In the match: creature hooks fail "Cannot capture undefined variable 'target'"  [verified live, fixed]

Creature `main.script` hooks nest lambdas three deep (`[=](outer) { with_probe([outer](probe) {
stash([outer, probe]() {...}) }) }` shape). Two parser/runtime defects, both backends, both
pre-existing (reproduced in the runner against HEAD 3d72bdff):
- `parser::lookup_slot` walked EVERY enclosing function scope, so an identifier two lambdas
  deep was stamped with the *outer-outer* frame's slot; the runtime reads slots relative to the
  frame active at closure creation (the middle lambda's) → **silent wrong-variable reads**
  (`probe*10+outer` returned 77 instead of 73 — `outer` came from slot 0 of the wrong frame).
- Explicit `[name]` captures resolved only through identifiers used in the lambda *body*; a
  name that is only forwarded by a nested lambda's capture list fell through to the env and
  missed the enclosing param (`Cannot capture undefined variable`). Default `[=]` chains failed
  the same way (`Undefined variable 'k'`), because the shared `body_walker` never descends
  nested lambdas.
Fix: the parser owns scope resolution — a use resolves directly only to its own scope or its
immediate parent's slots; a deeper local becomes a *chain capture*: the lambda created in the
declaring frame captures the slot (`lambda_expr::chained_outer_slots`), lambdas between carry
the name through their default capture (`chained_outer_names`), the use resolves by name at
runtime. Explicit captures carry the parser-resolved slot (`capture::outer_slot`). Both backends
consume the same AST fields; jaibite v6 serializes them (stale `.jbite` siblings re-parse via the
existing `catch(...)`). Pinned in `Lambda Captures` (5 tests), `Jaibite Serialization`
(round-trip), and the reentrant-callback nested test; guide ch.6 documents nested lambdas with a
runner-verified example.

### 2.4c Creatures "stuck forever" under spawn contention  [reproduced in mv_tests, fixed]

Two independent stalls in `NavigationAgent` (`MV/ArtificialIntelligence/pathfinding.*`):
- **Budget starvation.** Every recalculation was charged its search *limit* (`min(remaining,
  200)`) rather than the nodes it visited, and a blocked agent retried every frame. Creature
  update order is fixed, so five permanently blocked agents (a spawn crowd) drained the
  1000-node frame budget before later agents ran; those never got to recalculate even with a
  clear path (`attemptToRecalculate` returns false without budget). The remaining scraps of
  budget also produced false "blocked" verdicts (a 3-node search finds nothing).
  Fix: charge `Path::nodesSearched()`, require `MIN_SEARCH_LIMIT` (50) before searching,
  exponential retry backoff after a failed search (1→30 frames) that any unblock within
  `NEARBY_UNBLOCK_RADIUS` (4) cells resets, cleared by `markDirty()` (goal changes) and by any
  successful search.
- **Placement wait.** An agent spawned onto an occupied square sets `waitingForPlacement` and
  `update()` returns early forever unless that exact square frees (a defender parked on the
  spawn never leaves). Fix: after `PLACEMENT_GRACE_FRAMES` (6) the agent takes the nearest
  clear cell within `PLACEMENT_SEARCH_RADIUS` (3), moving a goal that was still the spawn point.
Pinned by four `Pathfinding` tests (budget charge, starvation, nearby-unblock resume, spawn
placement). The earlier semaphore-assert class (2.4) stays closed by the remembered footprint.

### 2.4d Pathfinding campaign (bench-driven, Dev's "make it sing" + "stuck is critical")  [measured]

Ledger: `Source/MV/Bench/RESULTS_path.md` (harness `bench_path.cpp`, deterministic, 4- and
8-connected, 3x3 footprints). Landed on top of 2.4c: I1 heap/stamps/proper A* (25,129 -> 98 ns/node),
I2 tick-driven budget + map search queue, I3b adaptive time-slicing, I4 line-of-sight lookahead,
I7 progress ladder (map-assigned ids, pull-ordered convoys, right of way = waited/remaining/id,
swap / follow / step-aside cascade, soft search after 30 stuck ticks, queue aged by time waiting).
Pre-existing engine defects it exposed, all fixed: corner-cutting A* vs straight-line movement
(THE 8-connected "stuck forever"), uninitialised-neighbour clearance (phantom 1-2-3 band; 3x3
units detour/jam), `RecentlyBlockedNodeObserver` capturing its own vector-element address (UB,
nondeterministic runs), footprint/movement cell disagreement (now `floor(centre)`), LOS from the
cell centre instead of the agent, `Map::resize` never building new columns from y=0 (now rebuilt
from raw). Result: zero stuck agents in every scenario on both connectivities, spawn/dynamic 100%
arrived, mv_tests 60/60. Open quality (not stalls): narrow-gap throughput, goal-region capacity.

### 2.4e Live game: "creatures take turns" -> flow fields  [measured live, 2026-08-28]

Dev's report ("one or two move, walk a distance, pause, another set walks; most stand still") was
partial paths on a starved budget: the game map is 250x100 (25k cells, a lane is ~250 cells) and a
200-node search never reached the goal, so every creature walked a stub, queued, waited. The
MV_PATH_DIAG=1 server report (every 300 ticks: agents / moving / stalled / idle / queue / budget)
showed 325 live agents with 184 stalled and every search time-sliced and unfinished; budget
multiples only move the cliff (per-agent A* needs ~1M expansions/tick at that scale).
Landed: FRAME_NODE_BUDGET 128000 / DEFAULT_SEARCH_LIMIT 16000 (Dev: "quadruple"), spawn placement
radius scaled by footprint, and I5 flow fields - one shared distance field per (goal cell, radius,
unit size) over static obstacles, Dijkstra from the goal region, cached (16) and invalidated only by
static block/base-cost changes (temporary path costs were invalidating every field every tick in the
first build - an A* storm), at most one field computed per tick; agents descend the gradient for 12
nodes with no search and no budget, falling back to A* only while no fresh field exists. Live match
17: stalled 0-1 (was 89-184), 58-84 agents all moving or fighting, attacks per client 800 -> 5-7k.
Follow-up: creatures chasing individual targets still A* (distinct goal cells) - bucket chase goals
so nearby targets share a field. Rebuilt client/servers carry all of this; _run_local.bat Release.

## 3. Gates to build first (Phase 0) — so every later fix is provable

The existing gates prove "loads don't throw" and "scripts parse". None of them would have
caught 2.1–2.3. Add, in this order:

- **`-loginshot` / `-gameshot`** (client modes next to `-workbenchshot`): construct `Game`
  headless-window, show Login, run N frames, `glReadPixels` → BMP. Eyes-driven; the same
  capture loop the Workbench already uses (`main.cpp` `RunWorkbench` + `App::runFrames`).
  Prereq: `Game::update` must tolerate `ourLobbyClient == nullptr` (`game.cpp:165` derefs it
  unconditionally when `RUNNING_IN_HEADLESS` skipped `initializeClientConnection`).
- **`-verifyassets` extensions**: (a) eval/static-check the 8 Interface scripts (today only
  creature/building/effect scripts are checked); (b) after loading `Login/view.scene`, assert
  the `LoginButton` Clickable has an `onAccept` receiver *with an engine* (the 2.2 tripwire);
  (c) `save(load(x))` byte-equality (or structural equality) for the whole corpus — today
  nothing asserts round-trip fidelity, which is how the depth-1 save filter and the sliced
  types (§4) stay invisible.
- **mv_tests suites** (Foundry, `Source/MV/Tests`): Text-from-disk keeps its glyph container
  under the owner; saved output of a loaded Text scene contains no `TEXT_`/`CURSOR_` nodes;
  `postLoadStep` is idempotent (childAnchors count, `TEXT_` child count — the Workbench calls it
  per edit, `inspector.cpp:209-213`); a Button loaded from JSON has three non-null child views;
  16-point Sprite in a box smaller than its slice margins follows whatever rule P1-3 decides;
  a `TextureHandle` resolved through a pack preserves flipX/flipY; a loaded `Clickable` script
  receiver fires (on the game's real path — `Node::load` JSON via `loadChild`, not the
  `toBinaryString` round trip `-verifyassets` uses today); `Component::clone` copies every
  inheritance level. Also fold in `Source/Tests/pathfinding_tests.cpp` (orphaned, referenced by
  no build) and add the moving-footprint block/unblock case.
- **`-matchsmoke`** (later, Phase 2): the `_run_local.bat` flow scripted with `-auto`, both
  clients, N seconds, greps for `Script callback failed` / `Exception` / the semaphore
  assert; exit code is the gate. The manual version of this is what found 2.2/2.3 today.

## 4. Correctness debt inventory (Phases 3–4)

### 4.1 MV serialization / properties — CRITICAL/HIGH  [verified by reading]
1. `Stencil` (`stencil.h:11`) and `Palette` (`palette.h:11`) have no `property_owner` level, no
   registrar, no `register_relation` → saved as a sliced `Component`, reloaded as a bare
   `Component`; their `save`/`load`/`load_and_construct` are dead. Both are attached by the
   Workbench and the old editor.
2. `PathMap` has two `load_and_construct` overloads (`path.h:18-26` public+constrained,
   `:167-171` protected); overload resolution picks the constrained one, which loads only
   PathMap's own properties — `points`, textures, shader, anchors, `componentId` are lost.
3. `Component::clone` (`component.h:180-184`) calls the non-virtual `reflection()` of the
   `Component` level → one level cloned; every `cloneHelper` patches a few fields by hand.
   `Node::cloneInternal` routes through it. Fix: clone via the virtual whole-chain walk.
4. `saveJai`/`saveBinary` filter `serializable(false)` only at depth 1
   (`node.cpp:295-308`, `:349-367`); nested `Node::save` writes everything. `allowSerialize`
   itself is not a property, so a round trip resurrects filtered nodes.
5. `Creature`, `Building`, `BattleEffect` are `Component`s registered without
   `jai::bases<MV::Scene::Component>` (`creature.cpp:70`, `building.cpp:55`,
   `battleEffect.cpp:55`) → `is_assignable` rejects them through `shared_ptr<Component>`.
   Their nodes are `serializable(false)`, which item 4 defeats below depth 1. Pattern to copy:
   `Physics/collider.cpp:8,14`.
6. `Drawable::materialId` is saved and never resolved (`drawable.cpp:381`); `Anchors::operator=`
   drops `applyingPosition` and copies `selfReference` from the source (`drawable.cpp:399-424`).
7. `TextureHandle::postLoadInitialize` re-points the definition without unregistering from the
   old one (`textures.cpp:718-722`); a failed `loadFile` leaves a null entry in `globalLookup`
   forever (`textures.h:248-256`).
8. Every MV registrar calls `auto_bind()` twice (registrar already does; lambdas repeat it).
9. Three JSON/binary node loaders with different post-load policies (`Node::load`, `loadJai`,
   `loadBinary`, `node.cpp:225/381/262`); `LoadOptions`/`load_complete` only exist on `load`.
10. `TextureHandle::logicalSize()` (`textures.cpp:878-882`) multiplies `handlePercent` (relative
    to `size()`) by `contentSize()` — wrong for any pack whose content is smaller than its
    texture; this is what produced the Cereal-era slice rows (§2.1 item 2).
11. **Load order is a wire-format contract:** the writer inlines a shared object's payload at
    its *first* reference and writes `$ref` afterwards. `childComponents` is written before
    `childNodes`, so a Button's child views carry their payload under `activeView/idleView/
    disabledView` and the owner's `childNodes` entries are `$ref`-only (`Login/view.scene` ids
    30/38/34). Any loader that reads `childNodes` first gets nulls (`archive_impl.hpp:1093-1101`)
    and `fixChildOwnership` (`node.cpp:1068`) dereferences them. Same shape for
    `Scroller::contentView` and Slider's handle. Reordering reads means reordering writes *and*
    re-saving the corpus — don't.

### 4.2 The Node implementation  [verified by reading]
- Deserialized children bypass `add()` (`fixChildOwnership`, `node.cpp:1067-1080`): no
  `onAdd`/`onChildAdd`, no parent-alpha receiver hookup, no circular-ownership check; loaded
  components never get `onAttach`. Runtime-built and loaded trees are in different observer
  states.
- `unsilenceInternal` drops per-item replays for `onChildRemove`, `onChildOrderChange`,
  `onComponentUpdate` (the three `//TODO` at `node.cpp:1414/1418/1568`) — and the whole load
  path runs silenced.
- Ill-formed-but-uninstantiated templates: `Node::detach<T>(bool,bool)` (`node.h:233-248`,
  copies a deleted-copy `jai::property`, calls a nonexistent `.lock()`);
  `componentInChildren(const std::string&, …)` (`node.h:412-432`) recurses on `this` forever.
- `worldRotation(const AxisAngles&)` passes degrees to the radians setter (`node.cpp:1230`);
  `recalculateChildBounds` skips the visibility check for child 0 (`node.cpp:779-788`);
  `operator<<` tests the parent's `serializable()` (`node.cpp:1582`); `indexOf` returns
  `size()` on miss and `myIndex()` consumes it (`node.cpp:680-691`); `visible()/updating()/active()`
  walk the parent chain per call; `quietLocalMatrixFix` uses generic `*` where `recalculateMatrix`
  uses `affineMultiply`.
- `clone` vs `cloneInternal` are a duplicated 13-field manual copy; neither uses
  `clone_to_target`, so a new Node `JAI_PROPERTY` silently doesn't clone.
- Node's 22 signals are plain emitters: not serialized, not script-connectable, none bound in
  `node.cpp:22-66`.

### 4.3 Game logic  [verified by reading unless noted]
- `Building` script path uses the lowercase catalog id (`building.h:70-72`) while folders are
  `Buildings/Life`; works only on case-insensitive filesystems (`assetPath()` upper-firsts).
- `Assets/Buildings/Void/main.script` keys tiers `None/Basic/Eternal/Overwhelming`; the catalog
  tree is `T0→T1→{T2_0,T2_1}` → `getVar` null → per-tick failure for any `void` loadout.
- Only Life/Void of 8 catalogued buildings have assets; all 10 non-Life creature `main.script`s
  are byte-identical copies of Air_T1 (with 13 per-frame `print`s).
- `Building::spawnPoint` is read in the ctor before `initialize()` creates the bindsnap child
  that holds `spawn` (`building.cpp:75-95`) → always the building's own position.
- `InterfaceManager::update` is never called → every page `update` hook is dead.
- `Shared/selectText.script` reads `arg_0`/`pageId`, which are receiver *instance* locals that
  `eval_file`'s separate `execute` cannot see → error on every input click.
- `[&] this` captures stored on pool-owned network states outlive the component
  (`creature.cpp:373-381`, `building.h:157-162`, `battleEffect.cpp:179-187`) — UAF class.
- `Game::render` draws `worldScene` twice (child of `rootScene` + explicit draw,
  `game.cpp:224-233`); UI and world share `cameraId(1)`.
- `-auto` presses the Game button *inside the `GameEditor` constructor* (`gameEditor.cpp:153`),
  running the whole game loop re-entrantly before the menu finishes constructing.
- `PathAgent::hasFootprint()` returns the inverse (`path.h:334-336`, script-exposed).
- No win condition (`Team::health` never decremented); match result is forfeit-only.
- `main.cpp:746-950` runs debug/test code on every launch (`Hello world!`, save/load probes,
  a `NetworkObjectPool` demo) and carries ~100 lines of commented-out pqxx/SES code.
- Config naming is inverted (`gameServerAddress.config` holds the lobby address); works locally
  only because ports line up. `Scenes/map.scene` and `Assets/Scenes/map.scene` are byte-identical
  duplicates and the root copy always wins.
- `Wallet::values` is sized 3 by literal while `TOTAL == 3` is an enum end marker.
- **Open (server trust):** `RequestBuildingUpgrade::execute(GameUserConnectionState*, GameServer&)`
  (`gameServerActions.cpp:52-54`) calls `performUpgrade(slot, id)` without checking that the
  requesting user owns the slot or can afford the upgrade — the wallet check lives only in the
  client UI (`building.cpp:157`). Any client can upgrade any slot for free. Validate server-side.

### 4.4 Editor / build / docs  [verified by reading]
- Workbench has no authoring verbs (create/delete/duplicate node, attach/detach component,
  save selection as BindSnap, texture picker, anchor editor) — the legacy `Source/Editor`
  still provides them (`componentPanels.h`, `texturePicker.h`, `anchorEditor.h`), which is why
  both must stay compiled until parity.
- `Bindstone_Common.vcxitems` compiles **both editors into the game server, lobby server and
  Android client** (imported at `BindstoneGameServer_Windows.vcxproj:62`, `...LobbyServer...:62`,
  `BindstoneCommon_Android.vcxproj:106`).
- Reflection has no editor metadata (range/step/category/read-only/enum labels) — enum tables
  are hardcoded in `inspector.cpp:425-432`; anything vector/pointer/aggregate is a dead
  `"(TypeName)"` row; write-notify is `postLoadStep()` on the whole subtree per scrub tick.
- Root `AGENTS.md` still documents Cereal (`MV_PROPERTY`, `CEREAL_REGISTER_TYPE`, ~170 lines),
  says `#ifndef BINDSTONE_SERVER`, lists ChaiScript/cereal under `External/`; `Assets/*/AGENTS.md`
  say ChaiScript and `.prefab`; `Source/AGENTS.md` omits Workbench and JaiScript; root
  `CLAUDE.md` uses `C:\git\...` and `bindstone.sln` (file is `Bindstone.sln`, repo on `D:`).
- Dead files: `MV/Serialization/makeNvp.h`, `serialize.cpp` (0 bytes), `MV/Utility/properties.hpp`
  (pre-jai property system), `MV::Signal` in `signal.hpp`, `ai_jai_serialization.hpp`,
  `mv_jai_serialization_fwd.hpp`, `Audio/sound.*` (0 bytes, still in CMake), the four
  tombstone `*Hooks.cxx`, `Editor/lightEditorFactories.*` (in no project),
  `Source/Game/CMakeLists.txt` (lists a nonexistent `managers.cpp`), committed `.obj/.exp/.lib`
  in `Source/MV/Tests`, three `.cpp.disabled` JaiScript tests.
- Stale runtime line: `Script debugger listening ... (breakpoints need MV_SCRIPT_INTERPRETER=1)`
  contradicts CLAUDE.md (both backends debug).

### 4.5 JaiScript items this surfaced (upstream, small)
- Register the reader/writer ctor engine as an implicit user context (fixes 2.2 for every
  host; MV can also set it explicitly as belt-and-braces).
- Never report an empty error message (guard in `report_script_error` / the wrapper at
  `engine_impl.hpp:33`; fall back to the error-code name + stack).
- Release-review items still unverified on the live MV path: B4 (signal emit `std::forward`
  move-consumes args for 2nd+ receivers), binary `begin_array/begin_map` size validation on the
  scene-load path, re-entrant `execute()` inside converted callbacks (`MV::Script::eval` and
  `ScriptPanel::runScript` both do it).
- Nested-lambda capture resolution (2.4b): parser-owned chain captures, jaibite v6 — DONE.
- `property_owner` copy-ctor clones one level; `property<unique_ptr>` clone; ~2.5 s/TU
  archive_dispatch header tax (`component.h → textures.h → serialize.h → engine.hpp`).

## 5. The plan

Order is by "what unblocks the most, proven by a gate". Effort is **[judgment]** in
focused sessions (Claude + Dev verifying), not calendar time.

### Phase 0 — Tripwires (≈1 session)
P0-1 `-loginshot`/`-gameshot` + headless-safe `Game::update` (null lobby client) + the
      `PrintWindow`-style capture already used for Workbench.
P0-2 `-verifyassets`: Interface scripts, receiver-has-engine assertion, corpus round-trip
      equality, list of expected component `$type`s per file (catches slicing).
P0-3 mv_tests suites listed in §3; adopt `Source/Tests/pathfinding_tests.cpp`.
P0-4 Delete the launch-time test code in `main.cpp:746-950` (it pollutes every log the gates
      read) and the stale debugger log line.
Exit: all new gates RED on the known bugs, green everywhere else.

### Phase 1 — Login/interface path (≈2–3 sessions)
P1-0 **Recursive `serializable(false)` save filter first** (pulled forward from Phase 3): `Node::save`
      (`node.h:875-878`) writes `property_mgr` unfiltered; only the root is filtered. Today's
      files are clean only because the glyph containers are orphaned before any save — once
      P1-2 keeps them attached, every Workbench/Editor save and the P1-3 re-save would embed
      `TEXT_*`/`CURSOR_*` subtrees (dead `SurfaceTextureDefinition`s on reload, the July bug
      again). Persist `allowSerialize` through the filter. Gate: "saved output contains no
      `TEXT_`/`CURSOR_` ids".
P1-1 Receivers: implicit engine user context in `archive_reader_impl` (JaiScript) — prefer that
      alone; an MV-side explicit `set_user_context<jai::engine>` would have to cover
      `node.cpp:233/267/388`, `emitter.cpp:415` and `serialize.h:36-51`. Gate: P0-2(b) on the
      `Node::load` JSON path, `loginButton.script 0` in the log.
P1-2 Text: keep `Drawable::initialize` in `load_and_construct`; move the `owner()->add(...)` of
      the glyph container + cursor into `Text::postLoadInitialize` with an idempotence guard
      (`if (scene->parent() != owner())`) because the Workbench calls `postLoadStep` per edit.
      **Do not** reorder `childNodes`/`childComponents` (§4.1 item 11). No component `initialize()`
      depends on a sibling (`clickable.cpp:134-147`, `slider.cpp:99-105`, `path.cpp:160-175`,
      `parallax.cpp:35-46` already defers), so deferral alone is sufficient. Gate: mv_tests Text
      + Button-child-views cases, `-loginshot` shows labels.
P1-3 Sprites: (a) **decide** the small-box 9-slice rule — proportional squeeze is new behaviour;
      the alternative is re-authoring the 35/36 px boxes to ≥ 40 px or shrinking `button.slc` —
      and fix `logicalSize()`'s size/contentSize mismatch (§4.1 item 10); (b) persist flip state
      so pack re-resolve keeps flipX/flipY; (c) re-save the five Login/Main assets from the fixed
      engine (`-fixloginui` path, non-headless) **after** P1-0. Gate: `-loginshot` shows intact
      pills and a `button.png` LoginButton; atlas re-pack invariance (re-pack, reload, same shot).
P1-4 Empty-message callback: instrument `report_script_error` to dump the raw checked_result
      (code, symbol ids, C++ stack via the existing terminate tracer), try the capture experiment
      from §2.3, find the site, fix, and add the never-empty guard. Gate: the auto-login path
      reaches `Got Message:` in the log.
P1-5 `selectText.script`/`deselectText.script`: pass `pageId`/`arg_0` through `eval_file` (give
      it an optional locals map) or inline the two-liners into the receivers; wire deselect.
P1-6 Call `InterfaceManager::update` from `Game::update`.
Exit: `-loginshot` correct; two `-auto` clients authenticate against the local lobby with zero
`Script callback failed` lines.

### Phase 2 — A full match (≈2–3 sessions)
P2-1 `-matchsmoke` gate (§3) and run it; expect the MapNode assert. `-auto` now also queues
      for a match: `DefaultLogin::autoMatch` → script global `AutoMatch` → `Main/initialize.script`
      `onShow` presses `Play`, so two `-auto` clients against the local stack go login → queue →
      game server without a click.
P2-2 `NavigationAgent`: store the blocked footprint rect at `blockMap` time and unblock exactly
      it; set `isBlocking` consistently with `waitingForPlacement`; clamp the lower loop bound;
      moving-footprint test. Re-run the smoke until creatures fight for the full window.
P2-3 Game-logic fixes from §4.3 that affect the match: Building script-id case
      (`toUpperFirstChar` like `assetPath`), Void tier ids (or delete Void's script and share
      Life's), `Building::spawnPoint` read after `initialize()`, `hasFootprint` inversion,
      world double-draw + camera ids, `-auto` re-entrancy (press after construction), death
      hooks never authored (decide: keep C++ wiring, add no-op-tolerant behaviour).
P2-4 Lifetime: network-state `[&] this` captures → weak self + `receiver_owner`/`track()` so
      pool callbacks can't outlive the component.
P2-5 Win condition + `reportMatchResult` from gameplay (today forfeit-only). **[judgment]**
      optional for "the game works"; required for "a match ends".
Exit: `-matchsmoke` green for 5 minutes, both clients, no script errors, no asserts.

### Phase 3 — Serialization + Node correctness (≈3 sessions)
P3-1 §4.1 items 1, 2, 5 (sliced types, PathMap overload, game Component base relations) with
      the `$type` inventory gate from P0-2.
P3-2 §4.1 item 3: whole-chain clone via the virtual reflection walk — and it must run
      **before** `cloneHelper`'s hookups, not after (`component.h:180-184` runs
      `cloneImplementation` → `attach` → `initialize` → `cloneHelper` first): a late
      `clone_to_target` would replace `ourTextures` after `Drawable::cloneHelper` hooked its
      `sizeChange` watchers (`drawable.cpp:385-393`), run `Anchors::operator=` without
      `registerWithParent()`, and relayout Text outside the owner's silence. Signal receivers and
      Clickable priorities are not cloned today (no-op clone lambdas, `clickable.cpp:250-253`) —
      document or fix. Corpus round-trip equality gate.
P3-3 Loader unification: one JSON loader with `LoadOptions` semantics, `loadJai` a thin alias;
      `loadBinary` gets the same post-load contract; then the Node load path fires the same
      observer state as `add()`/`attach()` (or documents that it doesn't) and resolves the three
      `unsilenceInternal` TODOs.
P3-4 §4.2 defects (detach template, componentInChildren recursion, worldRotation units,
      child-0 bounds, operator<<, indexOf) + §4.1 items 6–9. Each gets a Foundry test.
P3-5 JaiScript upstream verifications from §4.5 (B4 forward, binary sizes, re-entrant execute).
Exit: round-trip equality on the corpus; mv_tests green; Workbench smoke/shot unchanged.

### Phase 4 — Properties embedded everywhere (≈3–4 sessions, parallelizable with 3)
P4-1 Convert hand-serialized state to `JAI_PROPERTY`: Palette (6 fields), Spine
      (`fileBundle`, `slotsToNodes`), `ParticleChangeValues`, Collider family (52 fields), Text
      (`fontIdentifier`, `formattedText` state), Scroller tunables, Button `currentView`,
      Node/Component `allowSerialize`. Rule: same wire names so assets don't move.
P4-2 `JAI_SIGNAL_PROPERTY` for the signals scripts should reach: Node (22), Text (2), Slider,
      Spine (10), PathAgent (4), Collider (7) — script-connectable and serializable in one line
      each (the Clickable pattern).
P4-3 `Source/Game` types: `Creature/Building/BattleEffect` state + `*Data` catalog records +
      `Wallet/Constants/Team` as `property_owner` with `JAI_PROPERTY` (the `jai_auto_bind`
      lambdas then shrink to methods only). Network states stay explicit archives.
P4-4 Editor metadata (the one engine design decision still open): attributes on
      `property_meta` (range/step/category/read_only/display) + an enum label registry, declared
      at the `JAI_PROPERTY` site; Workbench rows come from it instead of `inspector.cpp` tables.
      Per `docs/reflection_design.md` §6 this is the v2 attribute design — do it once, here.
P4-5 Inspector coverage: containers, texture handles (picker), anchors, `FormattedText`; per-
      component write-notify instead of subtree `postLoadStep`. Then the Workbench authoring
      verbs (create/delete/duplicate/attach/detach/save-as-BindSnap) → legacy Editor deletion.
Exit: every scene component's editable state visible in the inspector; `Source/Editor` gone.

### Phase 5 — Hygiene (≈1 session, can interleave)
Dead files and tombstones (§4.4), AGENTS.md rewrite (root serialization chapter → JaiScript),
CLAUDE.md paths, split the editor out of server/Android builds (own `.vcxitems`), remove the
duplicate `map.scene`, rename the inverted config files, commit-ignore `Source/MV/Tests` build
products, `Source/Game/CMakeLists.txt` either fixed or deleted.

## 6. Verification, end to end

```bat
:: build (repo is on D:)
msbuild VSProjects\BindstoneClient\BindstoneClient_Windows\BindstoneClient_Windows.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=D:\git\Bindstone\ /m
msbuild VSProjects\BindstoneGameServer_Windows\BindstoneGameServer_Windows.vcxproj  ...same props...
msbuild VSProjects\BindstoneLobbyServer_Windows\BindstoneLobbyServer_Windows.vcxproj ...same props...
:: gates (all from repo root)
Builds\Windows\x64\Release\Bindstone.exe -verifyassets
Source\MV\Tests\build_tests.bat            (call by full path; bare name fails)
Builds\Windows\x64\Release\Bindstone.exe -workbenchsmoke
Builds\Windows\x64\Release\Bindstone.exe -loginshot          (Phase 0+)
:: live flow (what found 2.2/2.3): postgres + lobby + gameserver + two -auto clients
_run_local.bat   (or start each with stdout redirected; grep "Script callback failed")
```
JaiScript side: `cmake --build out\build\x64-Debug --target jaiscript_tests` (both backends),
fuzz seeds as in `3d72bdff`. The JaiScript runner target is `jaiscript_runner`; the exe it
produces is `bin\jaiscript.exe`.

## 7. Assumptions I made (say if wrong)
- Client-side `GameInstance::spawnCreature` being a no-op is by design (server-authoritative);
  the client only mirrors the pool. Not treated as a bug.
- Engine-side fixes beat data-side patches: assets should never depend on baked UVs/slice
  positions or on a particular atlas layout; `-fixloginui`-style tools are one-offs.
- Correctness work is exempt from the 4 % perf bar; anything touching the draw/update loop
  still gets the GLOOM/timedemo tripwires.
- The Workbench stays standalone (no game sim), per the 2026-07-12 ruling; the match gate
  lives in the client (`-matchsmoke`), not the editor.
- Legacy `Source/Editor` is deleted only after P4-5 parity, not before.
