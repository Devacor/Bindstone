# Workbench — new Bindstone editor (campaign state)

Brand-new editor replacing Source/Editor once validated (Dev ruling 2026-07-11: build clean,
validate, then delete old). Unity/Godot-style: dockable tab-dragging panels, dark theme,
property-reflection-driven inspector. Layering rule: Bindstone < MV < JaiScript — JaiScript
stays generic; MV-specific mapping lives here.

## Engine enablers (DONE, JaiScript, 2026-07-11)
- property_base: virtual value_type_id()/erased_value_ptr()/erased_assign() + value_as<T>()/assign_value()
  (properties/property.hpp); observable_property overrides erased_assign → on_change fires.
- property_owner: property_value<T>(name)/set_property_value(name,v)/property_type_id(name),
  const find_owned_property, and VIRTUAL visit_reflected_properties/find_reflected_property
  (base-pointer reflection over the most-derived chain) (properties/property_manager.hpp).
- Tests: "Property Reflection" suite in source/tests/properties/container_property_tests.cpp,
  6 tests; full suite 2013 green both backends (a 2014th failing test belongs to the concurrent
  VM-perf session: Script Class Tests::field_store_ic_reload_and_setter_shadow — NOT ours).

## Architecture (files in Source/Workbench/)
- theme.h — dark palette + metrics (namespace Workbench, struct Theme).
- focus.h — FocusRouter: single active Text field, activate(text, commitFn) commits on focus
  loss AND Enter; handleEvent routes SDL events; SDL_StartTextInput/Stop.
- widgets.h/.cpp — themed factories: label, button, textField (commit cb), toggle, cycleButton.
- dock.h/.cpp — Panel base (title + content node + resized/update/handleInput); DockNode
  (split tree: vertical?, ratio, a/b children OR leaf panels+active); DockSpace (layout math,
  chrome build: tab bars/backgrounds/splitter grips, splitter drag, tab click activate, tab
  DRAG re-dock: hover target center/L/R/T/B overlay, drop = retab or split; empty leaves
  collapse). Viewport = special "Scene" panel with no background (scene renders behind UI).
- sceneTreePanel.h/.cpp — hierarchy tree (expand/collapse/select; rebuild-on-change keeping
  expansion+selection).
- inspector.h/.cpp — InspectorPanel: node transform section (fluent setters) + per-component
  sections via comp->visit_reflected_properties + value_type_id→row-widget map; enum label
  tables (BlendMode, BoundsType, TextWrapMethod, TextJustification); write = assign_value →
  owner->postLoadStep() as coarse notify (v1) except transforms via node API.
- workbench.h/.cpp — App: owns visor(camera 99)/sceneRoot/uiRoot, TapDevice routing, camera
  pan/zoom, selection, DockSpace + panels, load/save toolbar (map.scene default), run loop
  (update/handleInput/render mirroring old Editor), runFrames(n) for smoke.
- Launch: BindstoneClient main.cpp subcommands -workbench (run) and -workbenchsmoke (120
  frames, exit 0). Files added to VSProjects\BindstoneClient\Bindstone_Common\Bindstone_Common.vcxitems
  (individual <ClCompile>/<ClInclude> entries — no globs).

## Build commands (this machine: D:\git\Bindstone)
- JaiScript tests: cmake --build out/build/x64-Debug --target jaiscript_tests (VsDevCmd -arch=x64)
- Client: msbuild "VSProjects\BindstoneClient\BindstoneClient_Windows\BindstoneClient_Windows.vcxproj"
  /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=D:\git\Bindstone\ /m

## v1 scope notes
- Tab drop outside any target returns to origin (floating windows = next increment).
- Write-notify contract v1 = postLoadStep coarse hook; refine per-component later.
- See scratchpad mv-api-cheatsheet.md for exact MV signatures (session temp dir).

## Status (v1 landed 2026-07-11)
- [x] JaiScript reflection bridge + tests (2013x2 backends green; +virtual visit_reflected_properties)
- [x] theme/focus/widgets  [x] dock system  [x] scene tree panel  [x] inspector
- [x] app shell + wiring + build (Debug green)  [x] smoke exit 0; -verifyassets 58/58; mv_tests 18/18
- ENGINE FIX (pre-existing, found via smoke exit-code gate): windowed sessions crashed at
  static teardown — SharedTextures::white()'s function-local static handle + surviving
  globalLookup entries destroyed device-backed textures through a dangling Device*. Fixed:
  SharedTextures::resetWhite() + LoadedTexture::releaseDeviceBackedEntries(device) called
  from Draw2D::~Draw2D while the context is current. Also fixes the shipping game's silent
  quit crash. build_tests.bat de-hardcoded from C:\git\Bindstone (%~dp0-relative).
- Criticisms review DONE (see scratchpad property-criticisms-review.md): top items =
  metadata unification w/ 3-step deletion plan; property<unique_ptr> COMPILE BREAK in
  clone_to_target; property_owner copy-ctor clones nothing; ~2.5s/TU archive_dispatch
  header tax (12x the operator surface cost); script-reflection phasing P0-P4.
- Cloud review routine still blocked on GitHub connect (/web-setup).

## Increment 2 (2026-07-11, in flight): input fixes + Unity parity
Full investigation results (3-agent workflow: defects w/ file:line fixes, SDL window-chrome
recipe, Unity parity spec + build order) live in:
C:\Users\maxmi\.claude\projects\D--git-Bindstone\eca8a614-c4f4-4cbc-93f3-dd5c338f965d\subagents\workflows\wf_3fbdf3c4-1ca\journal.jsonl
Critical fixes: dock leaf "body" node so Stencil stops clipping tab bar (ROOT CAUSE of dead
input); endTabDrag use-after-free (detach→insert→collapse-last); scroller re-enable +
DON'T wire content (press-cancel >5px drift) → wheel-scroll routing instead; focus.h only
consumes keyboard + click-away commits; engine: allowUserResize never set SDL_WINDOW_RESIZABLE
+ first param is maintainProportions (pass false!); Window::hitTest() new API for borderless
custom title bar (SDL_SetWindowHitTest verified in vendored 2.0.10; hint
"SDL_BORDERLESS_WINDOWED_STYLE"=1; edges before strip; carve out button rects; maximized →
NORMAL edges); Clickable enable/disable signals inverted (clickable.cpp:82).
Parity build order: TreeView widget → PropertyRow stretch + FoldoutSection → wheel scroll +
min clamps → SearchField → ProjectPanel (folder tree + breadcrumb + glyph tiles + dbl-click
.scene load) → thumbnails/enums/VectorRow → swatch/dragfield → drag-n-drop last.
Unity dock shape: split(false,.18, leaf({Hierarchy,File}), split(false,.72,
split(true,.70, leaf({Scene}), leaf({Project})), leaf({Inspector}))).

### Increment 2 status — critical fixes LANDED (build green, smoke exit 0)
- [x] dock body node (tab bar no longer stencil-clipped: draw + hit-test restored)
- [x] endTabDrag UAF fixed (detach → insert → collapse-last); Scroller removed from leaves
      (press-cancel defect) → DockSpace::wheelScroll + scrollOffsets per panel + Panel::contentHeight()
- [x] focus.h: only consumes keyboard/text; SDL_MOUSEBUTTONDOWN commits + routes on
- [x] wheel: zoom only over viewport leaf, else wheelScroll; min-size clamps (120h/80v)
- [x] ENGINE: allowUserResize/lockUserResize now set SDL_WINDOW_RESIZABLE (+live toggle);
      Window::hitTest(HitTester) + minimize/maximize/restore/maximized(); Clickable
      enable/disable signal inversion swapped (clickable.cpp:82)
- [x] Borderless chrome: SDL_BORDERLESS_WINDOWED_STYLE hint, hit-test lambda (8px edges
      unless maximized, 34px drag strip minus 3x46 button cluster), themed in-app title bar
      (title + _ [] x buttons) via App::buildTitleBar/layoutTitleBar; dock lays out below it
- [x] TreeView<TItem> widget (treeView.h, header-only, adapter model: children/label/hasChildren;
      full-width rows, zebra, selection, foldout glyphs, 350ms double-click) → Hierarchy rebuilt
      on it (fluid width via resized())
- [x] Inspector: fluid two-column rows (labelWidth = clamp(40%,90,160), field stretches),
      foldout section headers w/ persistent collapse (keyed type#id), BoundsType enum cycle row,
      builtHeight reported for wheel scroll
- [x] ProjectPanel (projectPanel.h/.cpp): Assets/ folder TreeView left (fixed 170px v1),
      breadcrumb buttons, glyph tile grid (colored letter tiles, column reflow from panel width),
      double-click: folder navigates, .scene/.prefab loads via App::loadScene
- [x] Dock shape now Unity: Hierarchy+File | (Scene / Project) | Inspector; title bar 34px
- All green: build clean, -workbenchsmoke exit 0.

### Increment 3 (Dev feedback batch) — LANDED, all green (smoke 0, verifyassets 58/58)
- Splitter resize cursors: DockSpace::cursorHintAt (gripRects) + SDL system cursors in App::update
- Scene navigation restored: left-drag pans over viewport leaf (TapDevice onLeftMouseDown/onMove/
  onLeftMouseUp receivers held as App members!), wheel zoom anchored at cursor (clamped .05-20x)
- 2x DPI-aware UI: Theme(float scale) multiplies all metrics; RunWorkbench builds
  Theme(2*uiScale) AFTER initialize, loads "wb"(28*ui)/"wb-small"(18*ui) fonts, hitTest constants
  from scaled theme (registered post-init — Window::hitTest works live)
- Waterfall/animated materials FIXED: the 6 game shaders (vortex/lillypad/wave/waterfall/pool/
  shimmer, from Game::initializeWindow game.cpp:137-142) now loaded in RunWorkbench
- Project browser: image thumbnails (SharedTextures file()->makeHandle + fitAspect, texturePicker
  pattern), .bindsnap tiles drag-out → App::dropPrefab (loadJai + add at sceneRoot->localFromScreen,
  select + tree refresh)
- RENAME: .prefab → .bindsnap EVERYWHERE (Dev ruling; "BindSnap" in UI copy). 22 asset files +
  componentPanels/battleEffect/building/creature/main.cpp/projectPanel. No script/JSON refs existed.

### Increment 4 (Unity-feel widgets) — LANDED, build clean + smoke 0
- makeScrubField (drag-to-scrub numerics, magnitude step max(.05,|v|*.01), click focuses) +
  makeVectorRow (axis-tinted X/Y/Z / R/G/B/A sub-fields, per-axis commits) in widgets
- Node transforms + Point<>/Scale props → VectorRows; all numeric props scrub
- Color rows: live swatch + RGBA vector + click swatch → MV::Palette popup on the dock popup
  layer (Panel::popups(), plumbed through dockAttach; popupLayer under dockOverlay)
- Enum rows wired w/ VERIFIED orders: BlendMode{Default,Multiply,Add,Screen},
  TextWrapMethod{None,Scale,Hard,Soft}, TextJustification{Left,Center,Right} (+BoundsType)
- TreeView filter (case-insensitive subtree match, force-expand) + Hierarchy search field
  (live via Text::onChange)
- Viewport click-select: press without drag (<=4px) picks topmost node by reverse-DFS
  screenBounds; syncs tree selection → inspector
- Project panel DPI fixes: treeWidth from theme.tabWidth, tiles/breadcrumb × scaleFactor()
### Increment 5 (hover + drag-reparent + polish) — LANDED, build clean + smoke 0
- TreeView hover tints: one onMove receiver per tree, row hit = localFromScreen + index math,
  base-color restore (zebra/selection aware); clipped/off-panel hovers are invisible = harmless
- TreeView drag-reparent (enabled when onReparent set): 6px threshold, Onto = drop-hint row
  tint, Before/After = 2px accent insertion marker; Hierarchy wires it w/ cycle guard
  (ancestor walk), root protected, reorder = depth(target ± .5) + parent normalizeDepth()
- Hierarchy search field stretches with panel width (bg/text/clickable re-bounds in resized)
- Project browser: double-click image → centered preview popup (fitAspect, 60% world, click
  closes) on the popup layer
### Increment 6 (undo/redo, GoF command pattern) — LANDED, build clean + smoke 0
- commands.h: Command base / LambdaCommand / CommandHistory (record = already-applied UI edit,
  execute only on redo; 200-deep; onAfterChange hook refreshes tree+inspector)
- Property edits: InspectorPanel::propertyApplier<T> (NAME-keyed via find_reflected_property —
  survives rebuilds) + recordPropertyEdit<T>; wired: bool/numeric/string/enum/Point/Scale/Color
  channels; makeScrubField gained onHistory (ONE command per drag gesture or typed commit —
  coalesced via committed-value state; suppressAccept replaces old dragging flag)
- Color popup: one command per picker session (recorded at close, colorAtOpen vs final)
- Node section: id/position/rotation/scale/alpha/depth all record via node-setter closures
- Structural: hierarchy reparent/reorder (undo restores old parent + depth + normalizeDepth),
  BindSnap drop (undo removeFromParent, redo re-add at drop position; prefab kept alive by
  command closures)
- Hotkeys: Ctrl+Z undo, Ctrl+Shift+Z / Ctrl+Y redo (skipped while a text field is focused);
  history cleared on scene load
### Increment 7 (visual tune + rounded corners) — LANDED, build clean + smoke 0
- Rescale: heights/text x0.8 (fonts wb 22px, wb-small 14px, x uiScale), widths x0.65 — new
  Theme base metrics (rowHeight 18 etc.); scaleFactor() denominator updated to 18
- Tab strip: 70%-transparent header-color backdrop behind each leaf's tab row (theme.tabStrip(),
  sized in layoutNode)
- ROUNDED CORNERS: Theme.cornerRadius (3px base, scales). widgets: roundedRectHandle(services,
  theme) = per-radius cached SurfaceTextureDefinition ("workbenchRounded_rN" via
  SharedTextures::surface — NOT function-local static; teardown-safe) + POT SDF coverage mask
  (straight alpha, premultiply shader) + handle->slice(R..S-R) 9-slice; attachRoundedRect()
  helper. Applied: buttons (active/idle), text/scrub field boxes, toggle bg+mark, panel body
  bg, tabs, drag ghost, color swatch. Resizing re-lays slice automatically (refreshBounds).
### Increment 8 (anchors retrofit + SCRIPT-HOSTED PANELS) — LANDED, smoke 0, hot reload VERIFIED
- Anchors retrofit (Dev directive: use them for stretch): dock leaf = hidden "sizer" sprite,
  bg/clip/tabStrip anchored (one bounds write per layout); title bar bg anchored to titleSizer;
  hierarchy search field bg/clickable/text anchored to searchSizer (resized() = 1 line now)
- ScriptPanel (scriptPanel.h/.cpp): Panel whose UI is a hot-reloaded .jai file. InterfaceManager
  hook convention: script assigns panel.build/tick/onResized (std::function props). Bound via
  registrar<ScriptPanel, MV::Services> "WorkbenchPanel": root/width/height/theme metrics/place/
  reportHeight, widget factories label/button/textField (x,y placement — no owner() dependency),
  loadScene/saveScene (App-injected std::functions). Engine = services jai::engine;
  add_global("panel") per execute; script captures `var self = panel` (global gets overwritten
  by other panels). Mtime poll 0.5s → reload; errors render IN-PANEL + stderr.
- DockSpace panels now shared_ptr (make_object needs it). File panel = Assets/Workbench/
  filePanel.jai — first dogfooded panel; script syntax verified (lambdas [](var x){}, signal
  .connect from script, std::function property assignment all work first try)
- VERIFIED LIVE: edited .jai while -workbench ran → "[workbench] reloading panel script" +
  clean rebuild. The no-recompile UI loop is real.
### Increment 9 (EYES): -workbenchshot screenshot capture — LANDED
- App::runFrames(n, path): last frame glReadPixels → row-flip → SDL_SaveBMP; main.cpp
  -workbenchshot writes workbench_shot.bmp; convert BMP→PNG via PowerShell System.Drawing
  to scratchpad and Read it. FIRST VISUAL: layout/chrome/rounding all correct and coherent.
- SCREENSHOT FINDINGS — ALL FIXED + re-shot verified: (1) per-leaf tab width clamp in
  layoutNode (min(tabWidth, (leafW-pad)/count - gap), re-bounds tabBg/label/clickable —
  Hierarchy|File both visible and labeled now); (2) the "+55px offset" was the overflow
  misread — gone with the width fix; (3) tile glyph = rowHeight-tall strip vertically
  centered in tile; (4) zebra flipped (row 0 = panelAlt) so single rows read against panel bg.
- Visual verification loop: -workbenchshot → BMP → PowerShell System.Drawing → PNG → Read.
  USE THIS after any visual change.

### Increment 12 (P1 live-sim: GameInstance embed) — LANDED, live-verified by Dev
- simInstance.h: SimGameInstance = ServerGameInstance's authoritative sim minus GameServer
  (initializeBuilding/spawnCreature copied; canUpgradeBuildingFor=false suppresses building
  Clickables; requestUpgrade->performUpgrade). Pool = the local in-memory one every
  GameInstance owns; no networking.
- startSim(): fabricated GameData(managers, isServer=true) + two InGamePlayers (distinct ids,
  8x "life" loadouts — only life/void have art), map.scene loads under sceneRoot, cameraId
  re-tagged 99 (recursive), all 16 slots upgraded to T1 so spawn timers run; creatures
  (Life_T1) spawn+pathfind via authoritative main.script AI. stopSim() on demand + loadScene.
- Sim's TapDevice is a dormant dedicated instance — GameInstance's own drag/zoom handlers
  never fire, Workbench keeps viewport pan/zoom.
- Sim clock unified: lastSimDelta computed ONCE in update() (step-flag consumed there),
  drives sim->update() and render()'s visor drawUpdate. Title-bar Sim toggle button (4th
  transport slot; main.cpp carve-out widened to 4).
- Viewport clip: visor gains viewportClip node + Stencil sized to DockSpace::viewportRect()
  (new; transparent leaf content rect below tab strip), synced per-frame — the sim/scene no
  longer bleeds behind transparent tab strips or panel gaps. Auto-frame on sim start
  (deferred until first layout via simFramePending).
- ENGINE FIX (MV, load path): Node::fixChildOwnership now also re-owns childComponents whose
  componentOwner != enclosing node. map.scene carried componentOwner:null for PathMap
  (conversion-era) -> first path()->owner() threw "Component owner has expired". This was a
  LATENT SHIPPING-GAME CRASH for any real battle. Deserialized componentOwner is never
  trusted now; repair fires only when broken (no reattachImplementation double-fire on
  healthy assets). Gates: -verifyassets 58/58, smoke+simshot exit 0. NOTE: mv_tests rebuild
  blocked on concurrent session's JaiScript WIP (deref_slow link skew) — re-run when settled.
- KNOWN COSMETIC: cyan square motes on Life buildings = AUTHORED untextured emitter
  particles (verified in pre-conversion cereal prefab: no textures record, teal beginColor).
  Not conversion damage. Fixable in-editor by assigning a Particles-pack handle (fluff.png).
- -workbenchsimshot: smoke variant that starts the sim and runs 480 frames for eyes-on.

### Increment 11 (P1 live-sim: time control) — LANDED, shot-verified
- Sim clock in App: simPaused/simSpeed/simStepQueued; render() splits visor->draw() (paused)
  vs drawUpdate(dt*speed); step = one 1/60 tick while paused; UI root always real-time.
- Title-bar transport: || (pause toggle, label swaps to >), >| (step), speed cycle
  (1x/2x/4x/.25x/.5x) at x = titleBarHeight*8; hit-test carve-out in main.cpp
  (workbenchTransportStart/Width) so clicks beat the DRAGGABLE strip — shot-verified aligned.
- P1 phase 2 pending: GameInstance embed (recipe agent running — GameData + InGamePlayer
  fabrication + WorkbenchGameInstance subclass + fixedUpdate drive; base class needs only
  canUpgradeBuildingFor override; watch: initialize() connects its own mouse-drag handlers
  which will fight Workbench pan; server-only spawn paths may need local pool).

### Increment 10 (lambda-capture investigation) — CONTRACT PINNED, latent bug fixed
- Empirical probes ("Lambda Capture Probes" suite, green BOTH backends): (1) globals resolve
  at CALL time — add_global rebinding is visible to previously created closures; (2)
  module-scope `var` shares global storage across execute() calls on one engine (clobberable
  by later files!); (3) function-local captures pin per-closure, survive function return,
  immune to global clobbering; (4) bare [] auto-capture COPIES at creation; (5) [&]
  cell-shares.
- LATENT BUG FIXED: filePanel.jai used module-scope `var self = panel;` — a second
  ScriptPanel would clobber it and File's deferred closures would drive the WRONG panel.
  Pattern now: deferred closures capture the build hook PARAM (function-local) only; rule
  documented in the .jai header. ScriptPanel convention: `panel` global is execution-time
  only; never read it inside connected/deferred closures.
- NEXT (dogfood expansion, Dev: spend real time here): more script API surface (toggle/scrub/
  vector/tree/rounded rect/colors), convert more panels (project? toolbar? inspector custom
  drawers), Theme → property_owner (script-tunable + serializable theme), script-side undo
  hooks; then gizmo port, old-editor deletion after parity.
- [ ] NEXT (full specs in workflow journal): VectorRow XYZ sub-fields; ColorSwatchField →
      Palette popup; NumberDragField scrub; SearchField (hierarchy + project); hover tints
      (HoverWatcher); image thumbnails via SharedTextures (texturePicker.h:77 pattern); enum
      tables for BlendMode/TextWrapMethod/TextJustification (verify value order first!);
      project left-pane splitter; inspector restretch-without-rebuild; drag-reparent + tile
      drag-instantiate; modal-loop render-during-resize via SDL_AddEventWatch (caveat #1).

### Increment 13 (gizmos port + tile slider) — LANDED, shot-verified
- gizmos.h/.cpp: GizmoLayer = the old editor's Editable*/ResizeHandles ported. Overlay node =
  sibling AFTER sceneRoot under viewportClip: draws above the scene, stencil-clipped, never
  serialized, unscaled (constant-pixel handles at any zoom; element worldBounds bake the zoom).
  Per-frame dirty-check re-syncs when the target moves (pan/zoom/sim/inspector/undo); a gizmo
  never self-syncs mid-drag. ALL gizmo geometry is WORLD space (worldBounds/worldFromLocal) —
  screenBounds diverges from world under DPI scaling and put handles off-stencil (shot-caught).
- NodeGizmo on selection (accent bounds outline + old-editor yellow move handle, drag =
  worldPosition write, ONE undo command per gesture recorded at release); BoundsGizmo =
  ResizeHandles port (4 corners + body drag, read/write strategy closures: generic bounds,
  Emitter min/max, PathMap body = CELL PAINTER with per-gesture paint undo + show/hide,
  Button/Clickable tint-while-editing restore); PointsGizmo = Drawable vertex handles w/
  weld-snap <0.75 local units (LSHIFT suppresses), whole-point-vector undo; HighlightGizmo
  passive ghost (Grid/Spine/Parallax).
- Activation: node gizmo always on selection (App::select chokepoint); component gizmo via
  the "G" button on inspector section headers (GizmoLayer::supports gates which sections get
  one; toggle wired through App, active shows "[G]"). loadScene re-adds the overlay (re-added
  roots take last+1 sortDepth). Pan/pick never fight handles: onHandlePressed clears App
  panning inside the same TapDevice pass (raw receivers run before exclusive Clickable
  resolution — priority can't suppress them); handles at globalPriority +100/+105/+110.
- ProjectPanel: footer strip (counter-scrolled child of contentNode — the dock wheel-scrolls
  the whole content node) with an MV::Slider driving tile size mix(32..112); below 12% the
  grid becomes a zebra LIST (icon + name rows); one wireEntryInteractions block keeps
  double-click/open/drag-out identical in both modes; rebuild throttled to integer size change.
- -workbenchgizmoshot: simshot + selects the node nearest the map center +
  App::frameSelectionInViewport pans it to viewport center → eyes-on gizmo verification.
- THE RABBIT HOLE (JaiScript boundary campaign this smoke exposed — File panel was failing
  at startup, each error masking the next): (1) cpp-bound METHODS got raw CELL-boxed args
  (escape-marked bare locals) — deref normalize at class_definition add_method/add_static_method,
  the builtins' hoisted-temp contract extended to boundary #14; (2) value.hpp extraction gated
  on type_info_->is_object() which var-decl flattening ('any' tag, by design) makes lie —
  now classifies by STORAGE (one deref + raw index), holder type-name check still guards;
  (3) const shared_ptr<T>& params REINTERPRETED the holder's T bytes as a shared_ptr<T> —
  hard segfault, fixed by materializing real handles (function_binder create_argument +
  value_converter<const T&> + as<T&> guard); (4) dynamic_binder::base_class SILENTLY skipped
  linking when the base wasn't registered yet (registrar map = type_index order!) — Button's
  whole chain orphaned, surfaced by the signal-view rework ("no member 'onAccept'") — now
  engine::defer_base_link resolves when the base arrives. Red-locked: Class Builder::
  bare_local_object_args_to_cpp_boundaries + Signal Auto Binding::inherited_signal_property_
  through_derived_registration (both registration orders).
- Gates: Debug 2096x2 + Release 2216x2 both backends, client build, smoke exit 0 with ZERO
  script errors (File panel fully healed), -verifyassets 58/58, gizmo shot verified.
- HANDOFF NOTE (signals session): -workbenchgizmoshot logs repeated "Cannot create cpp_object
  of unregistered class 'std::shared_ptr<Creature>'" during the sim — signal→script arg
  conversion in the creature-signal path (to-script direction; this increment only touched
  from-script). Sim runs regardless.
- Residue: aspect-lock (old Snap Aspect) not ported (needs a texture-size affordance);
  gizmo drag refreshes the inspector at gesture END (live per-tick field echo = later);
  Selection rubber band deferred until Workbench grows create-flows.

### Increment 14 (game-layer bindings + to-script handle routing) — LANDED
- Dev ask "bind Creature/Building/catalogs": Creature/Building/BattleEffect(+Data) were
  ALREADY registered — the sim's "unregistered shared_ptr<Creature>" spam was the TO-SCRIPT
  twin of increment 13's boundary family. Fixed at the terminal funnel
  (convert_custom/reference_with_registry + vector/map element converters + const T&/T&
  ::to routing): smart pointers share their pointee via make_object, never become objects
  of a class literally named "std::shared_ptr<U>". The make_value universal-reference
  overload outranks const shared_ptr<T>& for non-const lvalues — that's why the funnel
  guard, not just converter branches.
- make_object now prefers the DYNAMIC type's registration (dynamic_cast<void*> complete-
  object pointer): a shared_ptr<Creature> crossing any boundary surfaces as ServerCreature,
  so the AI's self.agent()/enemiesInRange chains resolve. Pinned:
  handles_expose_dynamic_type_surface + the shared_ptr shapes in bare_local_object_args.
- ACTUALLY missing bindings added: Catalog<CreatureData/BuildingData/BattleEffectData>
  ("CreatureCatalog" etc: data/has/ids/size — Catalog gained public has/ids/size/all),
  BuildingData (game exposed as live BuildTree& — it owns unique_ptr children, never copy),
  SkinData, Wallet (value/add/remove/hasEnough by int currency slot + onChange signals),
  Constants, GameData (buildings/creatures/battleEffects/constants), GameInstance.data().
- RunWorkbench: editor engine execution_budget(10s) — the sim's first spawn burst evals
  every creature script in one Debug budget window; the game keeps its tight default.
- Editor survives misbehaving game content: sim->update wrapped — a sim-side throw logs
  "sim update failed" + stopSim() instead of process death.
- RESULT: the creature AI runs LIVE in the Workbench sim for the first time (targeting,
  missiles, attacks — previously every hook died at the conversion boundary and the errors
  masked each other). Zero script errors in gizmoshot/smoke.
- OPEN (game logic, Dev): with the AI actually running, MV pathfinding asserts
  "Block Semaphore overextended in MapNode" seconds into the sim (repro: -workbenchgizmoshot;
  the editor now stops the sim cleanly and stays up). blockMap/unblockMap pair by isBlocking
  but assume the footprint POSITION is unchanged between them; suspicion: mass simultaneous
  T1 spawns / fall()-on-arrive interplay. Needs a game-behavior pass.
- Gates: Debug 2098x2 + Release 2221x2 both backends, client build, gizmoshot + smoke exit 0
  with zero script errors, verifyassets 58/58.

### Increment 15 (InEditMode, Dev directive) — LANDED, smoke-verified
- MV::editMode(bool)/editMode() (MV/Script/script.h, scriptDebugEnabled idiom): the Workbench
  sets it TRUE before engine creation (RunWorkbench); the shipping game never touches it.
- makeScriptEngine registers the InEditMode script global on every engine + logs
  "Script engine: edit mode" (the smoke's verification line); gameplay script hot-reload is
  ON in edit mode without MV_SCRIPT_HOT_RELOAD (creature/building .script files live-edit
  in the editor, matching ScriptPanel's own reload loop).
- The hook for opting game systems out of shipping assumptions when editor-hosted:
  C++ checks MV::editMode(), scripts check InEditMode. First consumers to migrate when
  touched: SimGameInstance's ad-hoc suppressions, GameInstance's own drag handlers, and
  whatever the MapNode-semaphore pass decides. The in-game editor (gameEditor) can call
  MV::editMode(true) at its state transition if it should count as edit mode.
- WHY (the old-editor evaluation): the old editor never entered the game's domain at all —
  it loaded scene files RAW (Node::load, componentPanels.cpp:1712) with no GameInstance.
  Buildings/creatures are NOT in map.scene (it carries the 1v1_0..15 anchor nodes; Building
  components exist only when Team::initialize attaches them under GameInstance), so a raw
  load shows the stage with no play. The Workbench's non-sim mode = the same model; the Sim
  button is the NEW capability that runs game code in an unforeseen host — hence the flag.

### Increment 16 (STANDALONE, Dev ruling) — LANDED, smoke + shot verified
- Dev ruling: the Workbench is a multi-project editor and must NOT sim the game itself —
  scene-level preview (particles/spine/shader time) + BindSnap drag-drop staging is the
  scope; a sim mode MAY return later as an opt-in.
- Game-sim embed REMOVED: simInstance.h deleted (git history keeps it; increments 11-12
  record the design), startSim/stopSim/frameSimInViewport/simMouse/GameData fabrication and
  the Sim button gone; loadScene no longer references the sim; vcxitems + -workbenchsimshot
  dropped; title-bar hit-test carve-out narrowed to 3 transport buttons.
- The transport (|| / >| / speed) now drives the SCENE-ANIMATION clock: viewport drawUpdate
  dt = paused ? 0 : dt*speed with 1/60 steps — emitters, spine, and shader-time preview
  (the old editor always animated; here it's pausable/steppable). Editor UI stays real-time.
- -workbenchgizmoshot = raw map.scene load (the old-editor model: anchors only, no game
  objects — see increment 15's evaluation) + center-nearest select. Shot verified: correct
  raw hierarchy, no buildings/creatures, animated materials. KNOWN QUIRK: the shot's
  auto-select found no node this run (20px self-bounds filter pre-first-layout?) — smoke
  tooling only; revisit if gizmo shots need it.
- The MapNode semaphore open item is now MOOT for the editor (no agents exist without a
  GameInstance); it remains a game/server-side behavior question.
- InEditMode (increment 15) unchanged and still correct for the standalone editor.
- Gates: client build clean, -workbenchsmoke exit 0 / 0 script errors / edit-mode line,
  -workbenchgizmoshot exit 0 + 240 frames + "Workbench loaded: Scenes/map.scene".

### Increment 17 (Phase 0 of the editor campaign: stop the bleeding) - LANDED, gated
- One selection: App owns it; the hierarchy highlights and reveals it (`SceneTreePanel::showSelection`) and holds
  rows as `SceneTreeItem` (identity pointer + weak handle), so nothing dereferences a node after a load or undo.
  Undo deselects a node that left the scene. The foldout toggle no longer needs two clicks below the first level.
- The editor camera is its own node (`viewCamera`, parent of the scene root): pan, zoom and framing never touch the
  document root, so they are never saved. A load frames the scene in the viewport.
- A loaded root keeps its id; a relative Save writes the asset file (`./Assets/...`), not the player-preferences folder.
- `dropPrefab` parents before `postLoadStep` (the `loadChild` order).
- Script panels: a throwing `tick`/`onResized` is reported once and the hooks stop until the file changes.
- Inspector: a rebuild commits the field being typed in (only when it is the inspector's own); the colour picker
  closes on a press outside it or a selection change (one undo command), and Escape restores the colour it opened with.
- The smoke modes count load failures, script errors and caught frame-loop exceptions and exit nonzero on any;
  `-workbenchselftest` drives load, select, save, BindSnap drop and undo/redo and checks each result.
  `Tools/Landing/client_gate.sh` runs `-workbenchsmoke`, `-workbenchselftest` and `-workbenchgizmoshot`.
- Parallax (Dev ruling): the editor puts every Parallax it loads into authoring (`Parallax::authoring`): an enabled
  layer rests at `localOffset` whatever the camera does, and a layer moved at rest carries `localOffset` with it. The
  Node section's `parallax` button (rest / preview) displaces the layers on the selected node and below it as a run
  would (`authoringPreview`); the preview ends when the selection changes, and a save writes the layers at rest.

### Increment 18 (sizes and panel shape against Unreal 5.6) - LANDED, gated
- `Theme` metrics are logical pixels scaled by the window's uiScale alone (the 2x factor is gone): 22 px rows and
  fields, 26 px tabs and property rows, 32 px title bar, 4 px gaps, pad and corners. `Theme::px()` scales every
  size that used to be a raw pixel constant (tree glyphs, inspector offsets, gizmo handles, script panels' `p.px`).
- Panels are flush squares in the panel colour on the window background; the active tab is panel-coloured with a
  rounded top and square foot so it joins its panel, inactive tabs have no fill. Rounding is left to controls.
- Palette follows Unreal's neutrals (`#151515` background, `#242424` panel, `#2f2f2f` header, `#0f0f0f` input,
  `#383838` button, `#0070e0` accent).
- The editor opens maximized; the smoke modes open at 1600x900 logical pixels on any display (the world is sized
  before the window, since the window's resize rebuilds the projection from it).
- The title uses the label font. The 26 px title font drew the letters whose glyph image is 16x32 (n, d, o, k, b,
  h) garbled: SDL_ttf pads its glyphs' rows, and a surface already power-of-two and in the engine's format skipped
  the copy that makes rows tight. Fixed in MV (`convertToPowerOfTwoSurface` / `convertToBGRSurface`).
- Still missing against Unreal: row icons, softer label colour (Text has no colour API), input outlines, hover states.

### Increment 19 (transform gizmos) - gated
- Tools on Unity's keys, acting only when no text field has focus: Q select (no handles), W move (the default),
  E rotate, R scale; `GizmoLayer::tool` / `App::gizmoTool` for a toolbar or menu. WASD no longer pans: the arrow
  keys do, and so does a middle or right drag on the viewport (a left drag on empty space still pans, a click picks).
- Move: a red X arrow and a green Y arrow along the parent's axes (Y points down, the scene's +Y) and a centre
  square for a free move. Rotate: a ring about the pivot; the drag turns the node by the angle the cursor sweeps
  in parent space, with the swept pie and its angle shown. Scale: square-ended handles along the node's own axes
  and a centre square for both; one handle length of drag doubles the scale. The handle under the cursor and the
  one being dragged turn yellow. Sizes are logical pixels through `Theme::px`.
- Ctrl snaps the result, not the delta: position to multiples of 10 parent units, rotation to 15 degrees, scale
  to 0.1 (`GizmoLayer::snapSteps`).
- One undo command per gesture (`move`/`rotate`/`scale <id>`), rotation kept in radians so undo restores it
  exactly. `onEditing` fires for each change a drag applies, `onEdited` at its end.
- The handles hang off one node at the pivot: a drag moves that node with the target and never rebuilds the
  Clickable being dragged. A Clickable hit-tests one box, so the ring's hit area is a fan of 32 boxes. Dropping the
  handles cancels a press first, since a pressed Clickable keeps itself alive through its move receiver.
- The selection outline is the node's bounds as four transformed corners, so it turns and scales with the node
  and follows a drag.
- A component gizmo that takes input (bounds, points, the PathMap painter) hides the transform handles until it
  is switched off, as Unity's collider edit mode does; the passive highlight does not.
- F fits the selection in the viewport (70% fill, zoom clamped to 0.05..20), or the whole scene when nothing is
  selected; `App::frameNode` is the one fit both use. A frame request waits for one draw, because Parallax layers
  settle to rest in the update a draw runs: a request made before the first frame measured them displaced.
- `GizmoLayer::moveSelected` / `rotateSelected` / `scaleSelected` run a whole gesture through the begin, apply
  and end a handle drag uses. `-workbenchselftest` checks each with its undo, then drags the real handles through
  `TapDevice::simulate` (X arrow, Ctrl-snapped Y arrow, centre square, a quarter of the ring, the X scale handle),
  pans with the middle and right buttons, presses Q/W/E/R/F and checks the framed bounds sit inside the viewport.
- Not done: multi-selection, a local/global axis toggle for move, snapping settings in the UI, a live inspector
  during a drag (`onEditing` is there to hook).
### Increment 20 (menu bar, readable inspector, control polish; Dev's annotated review) - LANDED, gated
- Menu bar in the title bar (`menu.h/.cpp`): File (New Scene, Open..., Save, Save As..., Exit), Edit (Undo, Redo),
  Window (one entry per panel, brings its tab forward). Dropdowns have an outline, a drop shadow, row hover and
  right-aligned shortcuts; moving across the titles switches menus; Escape or a press elsewhere closes. Ctrl+O,
  Ctrl+S, Ctrl+Shift+S. Open and Save As use the platform's picker (`fileDialog.h/.cpp`: Windows only so far).
  The title shows the open file; `App` tracks it as `documentPath`.
- Inspector: values it cannot edit print as plain text, not a mangled type name: node references by id, `None`,
  `4 points`, `6 indices`, bounds as `x, y  w x h`, anchors as `to <id>`; textures get a thumbnail and name per
  slot; `componentOwner` and retired keys are not shown; uint16/int16/uint8/uint64 are numeric fields. Numbers
  drop digits to fit their field (Text has no clipping).
- Expanders are drawn triangles (`makeExpanderArrow`), in the trees and on the inspector's section headers.
- Controls follow Unreal's construction: inputs have a 1 px outline, buttons a dark ring (`attachOutlinedRect`);
  buttons, section headers and the Project footer shade slightly towards their foot (`shadeHandle`). Unreal's own
  controls are flat; the shade is Dev's ask.
- Every docked panel has a scrollbar thumb when its content overflows (drag or wheel), and lays out beside a
  10 px gutter. The Project footer is a status bar across the whole panel; tiles spread across the width.

### Increment 21 (Scene toolbar, grid) - LANDED, gated
- A toolbar along the top of the Scene view (`sceneToolbar.h/.cpp`): select, move, rotate, scale | snap, grid |
  frame selected. It reports presses; `App` owns the state, so Q/W/E/R and the buttons always agree. Presses on it
  do not pan or pick in the scene behind.
- The magnet sets `GizmoLayer::snapAlways`: gestures snap without Ctrl, and Ctrl then lifts the snap for a gesture.
- `SceneGrid` (`sceneGrid.h/.cpp`): lines behind the scene at the move snap step, widening by tens as the camera
  zooms out, every tenth brighter, the two axes tinted. One pixel wide at any zoom (an unscaled node laid out from
  the camera).
- Icons are drawn, not typed (`icons.h/.cpp`: supersampled shapes at the theme's scale, `IconButton`).
- The inspector follows a gizmo drag as it happens (`GizmoLayer::onEditing`).
- The self-test presses the toolbar buttons and the File menu on the simulated mouse (`TapDevice::simulate`).
- No perspective/orthographic switch: the scene camera is 2D only.

### Increment 22 (reflected editors: references, anchors, textures, lists, nested owners) - LANDED, gated
- `inspectorEditors.cpp`: `addCustomEditor` takes a property before the plain fields do.
  - Node references (`shared_ptr`/`weak_ptr<Node>`): a chip that selects the node when pressed; a node dragged out
    of the Hierarchy and released on it is assigned (`TreeView::onDropOutside` -> `InspectorPanel::dropNode`); x
    clears. `componentOwner` shows as a read-only `owner` chip.
  - Anchors: a group with the parent (a dropped node gives its first Drawable), anchor and offset min/max, pivot
    and use-position, each through the `Anchors` setters with undo.
  - Textures: a row per slot (thumbnail, name, x to clear); a press opens a picker over the loose image files and
    each atlas pack (source button, pages of 24), which sets the slot through `Drawable::texture`.
  - `points` and `vertexIndices`: groups with a row per point (X/Y/Z through `setPoint`) or per triangle
    (through `setPoints`), twelve to a page.
  - `subdivisions` goes through `Sprite::subdivide`, so the grid rebuilds; `drawType` is a named choice; `Size` is
    a W/H row.
  - Nested property owners (`EmitterSpawnProperties`, `Spine::FileBundle`) open as a group of their own rows; a
    nested property is addressed by path (`spawnProperties/maximumSpawnRate`, `resolveProperty`), so the same
    fields, appliers and undo serve it. Register a new nested type in `nestedOwners()`.
- Groups are closed until opened and show a summary beside the title (`addGroupHeader`).
- Numbers print in fixed notation with the decimals that fit (`formatNumberCompact`): no `-0.000244141`.
- The self-test scrubs subdivisions, picks a texture, edits and re-parents anchors, and edits a nested emitter value,
  each with undo, on the simulated mouse (`InspectorPanel::rowScreenPoint`).
- Not yet: `ParticleChangeValues` (the emitter's min/max change values; it wants the particle editor's degree
  handling), `slotsToNodes`, `std::pair` rows, text content, per-point colour and UV.

## Increment 23: the history (2026-11-16)
- `CommandHistory` (`commands.h`):
  - `group(label)` returns a scope; every `record()` made while it lives joins one step (`CommandGroup`: redone in
    order, undone in reverse). Nested scopes join the outermost; a scope nothing was recorded in is no step.
  - `undoLabel()` / `redoLabel()`; the Edit menu shows "Undo move Sky" and greys a row with nothing behind it
    (`MenuItem::liveLabel` / `enabled`, read when the menu opens; `[[c|r:g:b]]` text markup draws the grey).
  - `markSaved()` / `modified()`: each step carries a serial, the saved mark is the serial of the state on disk, so
    undo or redo back to it reads unmodified and a saved state no undo can reach stays modified. `onMoved` fires when
    the position or the mark moves; the title stars a modified document.
- The rule for references: a command points at the objects it edits. Nothing leaves the scene except through a
  command that keeps the removed object and puts the same object back, so a step recorded before a delete still
  applies after the delete is undone. No id table.
- Delete (Del) and Duplicate (Ctrl+D) on the selected node, both in Edit. Neither takes the scene's root.
  - Delete is one group: `clearReferencesInto` clears what the rest of the scene holds of the leaving subtree
    (`InspectorPanel::nodeReferences`: a component's node properties, nested owners included, and its anchor
    parent), then the node is removed. A Button whose view was deleted would otherwise write the view into the file.
    Undo re-adds the node at its depth, restores the references and selects it.
  - Duplicate is `Node::clone` beside the original, its Parallax layers put into authoring, selected.
- Unsaved changes: New Scene, Open, a scene opened from a panel, Exit and the window's close go through
  `App::request*`, which asks `askAboutUnsavedChanges` (Save / Don't Save / Cancel, an SDL message box) when the
  document is modified. `newScene` / `loadScene` themselves never ask; the smoke modes answer Don't Save.
- The self-test presses Edit > Undo on the simulated mouse and checks the labels and the disabled row, a group as
  one step undone last first, delete / undo / redo with a step from before the delete, the cleared and restored
  Button view with a save between, duplicate, the saved mark and star, and all three answers to the question.
- Not yet: multi-selection (the first caller of `group` besides delete), a History panel, selection changes as
  steps.

## Increment 24: the history strip (2026-11-16)
- The Scene toolbar's last button (`Icon::History`) opens a second row under it: `HistoryStrip` (`historyStrip.h`).
  One pip per step, light when applied and dim when undone, a handle at the history's position, a green mark at the
  saved position, and a caption ("2 / 4   move Sky"). Edit > Show History / Hide History toggles the same row.
- A press on the row moves the handle there and a drag carries it: `CommandHistory::moveTo(position)` undoes or
  redoes across the steps between and reports once, so a drag over many steps refreshes the panels once per move.
  `undo()` and `redo()` are `moveTo` by one.
- `CommandHistory` also gives `position()`, `length()`, `labelAt(step)` and `savedPosition()`.
- The row is as wide as the view allows up to 560 (`SceneToolbar::viewWidth`), so it cannot reach over the panel
  beside the Scene; under 360 the caption goes and the pips stay. Pips are dots while they fit and thin bars when
  the history is long (200 steps).
- The strip builds its pips when the step count changes and only recolours and moves the handle otherwise, so the
  Clickable being dragged outlives the drag.
- The self-test opens it from the button, drags the handle back and forward over steps, presses at both ends,
  checks the saved mark, the pips a new step drops, and the Edit row.
- Not yet: a pip's label on hover (Clickable has no hover signal), a list view of the steps.

## Increment 25: Settings, snap units and the history row's controls (2026-11-16)
- History row: the pips are bars, orange when applied, grey when undone, and the step the scene is at is green and
  taller. A step-back and a step-forward arrow sit left of the bars; right of them the position is a field over the
  total ("[2] / 4"), which moves the history to a typed position on Enter. The handle is white and the saved mark blue.
- Settings: a tab beside the Inspector (`SettingsPanel`, rows of label and field given by the app), opened from the
  Settings menu or Window. Fields apply on Enter only; left any other way they show the value in use again.
  - Undo steps (`CommandHistory::limit`, default 200, 1 to 10000). A number below the steps the history holds asks
    Okay / Cancel first (`App::askToConfirm`, a native box unless replaced); Okay drops the oldest applied steps,
    then the undone ones furthest from a redo.
  - Grid X and Grid Y (default 100): the scene grid's cell. Lines sit a tenth of the cell apart and a snapped move
    lands on those, each axis on its own step (`GizmoSnapSteps::moveX` / `moveY`, `SceneGrid::spacing(Size)`).
  - Rotation snap in degrees (default 5).
  - Kept in `workbench_settings.txt` in the player-preferences folder as `name = value` lines
    (`App::useSettingsFile`); only the interactive run reads or writes it.
- Snapping: Shift or Ctrl held during a move, rotate or scale drag snaps it; the toolbar magnet inverts that.
- The move and scale gizmos draw their vertical handle up the screen (towards -y); a drag up on the scale handle
  grows the scale.
- `FocusRouter`: a field may pass an abandon callback, which makes it Enter-only (`makeTextField(..., a_enterOnly)`).
  `Text::disableCursor` emits `onEnter`, so the router clears its state before it calls it. Before this, Escape and a
  press elsewhere both ran the commit through that signal.
- The self-test types into fields with pushed text and key events, steps with the arrows, checks each setting, the
  Okay and Cancel answers, the settings file, a Shift-snapped drag and the upward handles.

## Increment 26: hover, and the history row across the view (2026-11-16)
- Engine: `TapDevice::trackHover(true)` turns hover on for a pointer. While on, each move emits `onHoverQuery`,
  every `Clickable` under the pointer answers with its click priority, and the device keeps the top-most one (with
  any above it that let touches through), so what covers a control also takes its hover. `Clickable::onHoverEnter`,
  `onHoverLeave` and `hovered()`. Hover holds through a press and settles on release; `refreshHover()` asks again
  when the scene moved under a still pointer. The signals are not serialized. Off by default, so the game pays
  nothing; with `map.scene` open in the Workbench a pointer move costs about 73 microseconds
  (`Source/MV/Tests/input_tests.cpp`, suite "MV Hover").
- The Workbench tracks hover: `makeButton` buttons and `IconButton`s light under the pointer.
- History row: the step-back arrow sits left of the bars and the step-forward arrow right of them. The row is as
  wide as the Scene view. Bars are never thinner than a logical pixel with one between: a longer history keeps
  that width and a thin scroller appears under the bars, which follows the handle and can be dragged. The pointer
  over a bar turns it white and names it ("step 4: move Sky").
- The self-test hovers toolbar buttons and a bar, and records 900 steps to check the scrolling row.

## Increment 27: text that knows its width (2026-11-16)
- Engine: `FontDefinition::measure(text)` gives the size a string takes on one line, from the font alone (no
  renderer, no Text), and it equals what a Text lays the string out to. `FontDefinition::fit(text, width,
  ellipsis)` gives the text, or its longest start that fits with the ellipsis after it. `Text::contentSize()` and
  `FormattedText::contentSize()` give the laid-out size. Suite "MV Text Measure" in `Source/MV/Tests/text_tests.cpp`.
- Workbench: `makeLabel` and `makeButton` never draw past the width they are made with; text too long ends in an
  ellipsis (`fitLabelText`, `setLabelText` for a label whose text changes). A text field clips what is typed at its
  edge with a `Stencil` on the text's node. Menu widths, the history row's step name and the scrub fields' digits
  come from `measure`; the 7.4-pixels-a-character guess is gone.
- Not yet: a text field does not scroll to keep its cursor in view, so a long entry shows its start; markup
  (`[[c|...]]`) is measured as written, so `fitLabelText` leaves marked-up text alone.

## Increment 28: inspector and Settings rows on Grid (2026-11-16)
- The inspector's content node carries an `MV::Scene::Grid` (one column, a cell the panel's width and a row high,
  a gap beneath each). Every row, section header and group header is a child of that node and the Grid places it;
  the running `a_y` that every `add...` function carried is gone, and the panel's scroll height is the Grid's
  bounds. The Settings tab stacks its rows the same way.
- What Grid does not do yet, found while moving onto it:
  - No fill or stretch: a cell is one fixed size or its child's size, so "fixed label, field takes the rest" is
    still computed when a row is built, and a width change rebuilds the rows.
  - `Grid::positionChildNode` stepped down a line by `cellPadding.first.x + cellPadding.second.y`. It now uses
    `first.y` (suite "MV Grid" in `Source/MV/Tests/grid_tests.cpp`). No shipped grid had padding that differs in x
    and y, so nothing moved.
  - With one column the first cell starts one padding step down: `bumpToNextLine` reads
    `a_index > 0 && width || columns`, so the `a_index > 0` guard does not cover the column test. Left as it is:
    correcting it moves `gameEditor.cpp`'s one-column grid up by 4.
- Next for Grid: the toolbar groups, the Project tiles (a wrapping Grid by `gridWidth`), tree rows.

## Increment 29: folder tiles (2026-11-16)
- The Project panel draws a folder as a folder: `folderHandle` (`icons.cpp`) is a generated texture in its own
  colours, the back with its tab, the front leaning forward, a soft shadow beneath. Tiles and list rows both use
  it, and the `[=]` text over the old brown square is gone.

## Increment 30: dock layouts kept, saved, loaded and reset (2026-11-16)
- `DockSpace::describeLayout` writes the arrangement as one line: each split with its ratio, each leaf with its
  tabs by title and the one in front. `H 0.18 (L 0 [Hierarchy,File]) (H 0.72 (...) (L 0 [Inspector,Settings]))`.
  `applyLayout` rebuilds from such a line: an unknown title is skipped, a panel the line does not name joins the
  first leaf (so no panel is ever lost), and text that does not read as a layout changes nothing.
- The arrangement in use is the `layout` line of the settings file, written when a tab is dropped somewhere new, a
  splitter is let go or another tab comes to the front (`DockSpace::onLayoutChanged`), and read at start.
- Window menu: Save Layout... and Load Layout... (`.bindlayout`, native pickers) and Reset Layout, which returns
  to the arrangement the editor starts with.
- The self-test drags a tab onto another panel's edge and back, drags the outer splitter, saves, resets, loads,
  feeds it broken and partial text, and reads the layout back from a settings file. These are the first checks of
  tab and splitter dragging.
- Not yet: floating windows (a tab dropped on empty space still does nothing), closing a panel and bringing it
  back, more than one named layout in a menu.

## Increment 31: closing panels and floating them (2026-11-16)
- Every tab in front carries an x that closes its panel (`DockSpace::closePanel`). The Window menu marks a closed
  panel "(closed)" and choosing it brings it back (`show`): beside the panel it was closed next to, or floating
  when that one is gone. The last panel docked in the main area cannot be closed or dragged out.
- Where a dragged tab lands follows Unity: on a panel's tab bar it joins as a tab, on an edge it splits, and in the
  middle it floats there. (Before, the middle joined as a tab.)
- A floating panel is a leaf drawn over the dock, inside the editor's window: the engine draws one OS window, so
  these are not separate windows. The bare part of its tab bar moves it, its corner resizes it, a press brings it
  to the front, and a press on it never reaches what it covers. Its tab drags back onto any tab bar or edge.
  It lives in its own layer (`DockSpace::floatingHost`), above the Scene toolbar and below the menus.
- The layout text carries both: ` F x y w h (L 0 [Settings])` for a floating leaf, in fractions of the dock's
  area, and ` X [File,Settings]` for closed panels. Save, load, reset and the settings file follow.
- The editor starts with File and Settings closed. File's load and save are the File menu's Open, Save and Save
  As; it stays available from Window as the example of a hot-reloaded `.jai` panel. Settings > Editor Settings
  opens Settings floating.
- Gizmo handles take no presses unless the pointer is over open scene (`GizmoLayer::pointerOverScene`). They
  outrank ordinary controls, so a handle under the toolbar or a floating panel used to take the press meant for
  the control above it.
- The self-test closes a tab by its x, reopens it from Window, floats a tab, moves and resizes it, checks that a
  press on it does not pick in the scene, round-trips a layout with a floating panel, and docks it again.
- Not yet: separate OS windows, splitting inside a floating panel, a floating panel snapping to others.

## Increment 32: hover, tooltips and the last text buttons (2026-11-16)
- `hoverTint(clickable, sprite, idle, hover)` and `hoverShow` (`widgets.h`) are how a control answers the pointer.
  In use on: every `makeButton` and `makeIconButton`, text and scrub field outlines, toggles, tabs behind the front
  one, tab close buttons, dock splitters (accent), scroll thumbs, a floating panel's corner, inspector section
  headers, and Project tiles and list rows.
- Tooltips: `Tooltips` (`tooltips.h`) shows one label beside the pointer once it has rested 0.45 s on a control
  that has a tip, and drops it on a press or when the pointer leaves. A control asks for one with
  `tooltip(services, clickable, text)`; `IconButton` and `makeIconButton` take it as an argument. Toolbar tools,
  history arrows, the transport, the window buttons, tab close and the inspector's small buttons have tips.
- `makeIconButton` is `makeButton` with a drawn icon. It replaces the text on the window buttons (`_ [] x`, the
  close one red under the pointer), the transport (`||` and `>|`; pause swaps to play), the inspector's `G` (now a
  handles icon, accent while its gizmo is on), the `x` clear buttons and the `<` `>` pagers. New icons: Minimize,
  Maximize, Pause, Play, Step, Handles, Document.
- Project file tiles are a page in the file type's colour with its short name on it.
- Rotate sweep: the wedge fanned the wrong way after the vertical handles were turned to point up, because its
  mirrored-parent check read the handle's angle. It reads the parent's own axes now, and the self-test checks the
  sweep's direction while the ring is still held.
- The self-test waits on tips (shown after resting, dropped on moving on, on a press and over open scene) and
  presses the icon pause button.
- Not yet: tips on inspector rows and fields, cursor changes beyond splitters, hover on tree rows through the new
  signals (the tree still tracks the pointer itself), a restore icon when the window is maximized.

## Increment 33: the Console and the status bar (2026-11-16)
- `MessageLog` (`messageLog.h`) holds what has been reported, at three levels (info, warning, failure), the same
  line twice in a row counted on one entry, the last 1000 kept. The editor adds to it (`App::reportError`, and a
  line for each load, save and placed BindSnap). The engine's `MV::info`, `MV::warning` and `MV::error` reach it
  through a `Logger` listener: those can come from loader threads, so they are posted and join the log on the main
  thread each frame (`post` / `drain`).
- Console panel (`consolePanel.h`): the log newest first, each line in its level's colour, fitted to the panel's
  width, with a Clear button. It is a tab behind Project in the starting layout. It rebuilds once per frame at most.
- Status bar: a strip along the bottom of the window with the latest message on the left and the count of errors
  and warnings on the right. A press on it brings the Console to the front. The dock gives up its height.
- The self-test adds messages at each level, logs one through the engine, checks the status line, the counts and
  the Console's rows, opens the Console from the status bar and clears it.
- Not yet: filters by level, a search box, the engine's direct `std::cout` lines (only `Logger` output is caught),
  copying a line, and a jump from a script error to its file.

## Increment 34: keyboard focus, text selection and right-press menus
- Focus lives in the engine, so authored UI carries it. `TapDevice` holds one focused Clickable and moves it:
  `focusNext` / `focusPrevious` (by `focusOrder`, then place in the scene), `focusMove` in eight directions (the
  nearest focusable that way on screen; a diagonal with nothing that way tries its sideways part, then its
  up-or-down part), `focusActivate`, `focusClear`, each optionally kept under one node.
- `Clickable` saves four properties: `focusable` (a Button starts focusable), `focusOrder`, `pressOnFocus` and
  `focusLinks` (a neighbour by node id for next, previous or a direction). A link is followed back from the other
  end unless that end names its own; a move with no link is worked out from the other focusable controls.
- `Text` has a selection: Shift with the arrows, Home and End, Ctrl+A, and typing, paste, backspace and delete
  replace it. `copyToClipboard`, `cutToClipboard` and `pasteFromClipboard` are calls as well as keys.
- Workbench: fields and buttons are focusable. Tab and Shift+Tab walk the controls of the panel the focus is in and
  wrap; a field that starts taking keys has its text selected; Enter or Space presses a focused button; Escape lets
  go. A focused button's outline takes the accent colour.
- Inspector: a Clickable's `focusLinks` is a group with a node-reference row per link; a drop on a row is one undo step.
- A right press and release in one place opens a menu at the pointer (`MenuBar::openContext`): Frame, Duplicate and
  Delete on a Hierarchy row or in the scene (which selects what is under the pointer first), Close and Float on a
  dock tab, Cut, Copy, Paste and Select All on a text field. A right drag in the scene still pans.
- Tests: `focus_tests.cpp` (order, scope, directions, diagonals falling back, links and their way back, activation)
  and the selection cases in `text_tests.cpp`; the self-test drives Tab, the selection, a focus link and its undo,
  and each of the four menus.
- Not yet: controller input (nothing maps a stick or pad onto `focusMove`), arrow keys moving the focus between
  buttons, a visible focus ring on authored UI, dragging to select text with the pointer, and Rename or Create rows
  in the Hierarchy menu.

## Increment 35: game controllers (2026-11-16)
- `MV::ControllerDevice` (`MV/Interface/controllerDevice.h`) sits beside `TapDevice`: polled once a frame, with
  `simulate` for automation. Every attached pad reads as one. `down(button)`, `axis(axis)`, `leftStick()` and
  `rightStick()` (dead zone taken out), signals `onButtonDown`, `onButtonUp`, `onConnect`, `onDisconnect`. Face
  buttons are named by place (SOUTH, EAST, WEST, NORTH); a trigger pulled halfway counts as a button.
- `navigate(tapDevice, dt)` turns the pad into focus moves: the d-pad (eight ways) or the left stick (a wide slice
  for each of the four directions, a narrow one for each diagonal) moves the focus and repeats while held, SOUTH
  presses the focused Clickable, the shoulders step to the previous and next.
- The game owns one (`Game::controller()`, in `MV::Services`) and navigates its UI with it. The Workbench does the
  same; its EAST button closes an open menu, else leaves a field without committing, else lets the focus go.
- A field stops being typed in when the focus moves off it, however it moved.
- Tests: `controller_tests.cpp`, and the self-test moves between Settings fields with the d-pad and the stick.
- Not yet: nothing draws the focus on authored UI, so a pad moves through a game screen unseen; no rumble, no
  per-player pads, no script binding (TapDevice has none either), and the hardware path has only been run with
  whatever was plugged in during the self-test.

## Increment 36: the highlight state and the controller's cursor (2026-11-17)
- `Clickable::highlighted()` is hovered or focused, with `onHighlightEnter` / `onHighlightLeave`: one state for
  "this is the one", whichever device chose it.
- `Button` saves a fourth view, `highlightView` (`highlightNode`), shown in place of the idle view while the button
  is highlighted and neither pressed nor disabled. A button without one keeps its idle view. One saved while
  highlighted loads at rest.
- The Workbench's hover looks follow the highlight, so a focused control looks as it does under the pointer; the
  separate accent outline for focus is gone.
- `ControllerDevice::cursor(true)` is cursor mode: the left stick moves a pointer of the controller's own (slow near
  the middle, quick at the rim, kept inside `cursorBounds`) and SOUTH presses under it. `navigate` feeds it to the
  TapDevice as a mouse, so hover, presses and drags follow. The d-pad and shoulders still move the focus, and
  moving the real mouse takes the pointer back.
- `MV::Scene::ControllerCursor` draws that pointer: a dot by default, or an image with a hotspot. Its art is made
  the first time it shows and it is never saved.
- The game tracks hover now, owns a cursor drawn in its UI, and turns cursor mode on with `game.controllerCursor(true)`
  from script or `MV_CONTROLLER_CURSOR=1`. The Workbench has a "Controller cursor" setting (on/off, kept in the
  settings file) and keeps the pad's focus moves to its own UI.
- Tests: highlight views and the saved-while-highlighted load, cursor movement, bounds, press and hand-back, the
  drawn cursor; the self-test turns the setting on, moves the stick and checks the dot shows.
- Not yet: no shipped button has a highlight view authored, the right stick does nothing, and a real pad has not
  been tried.

## Increment 37: the pad's two modes, and play (2026-11-17)
- Snapping and cursor are two modes of `ControllerDevice::navigate`, and BACK (Select, View or Share on the pad)
  swaps them; `cursorToggle(button or none)` chooses the button.
- Snapping: the d-pad or left stick moves the focus between elements; a button shows its highlight view, a text box
  starts taking keys and shows its blinking cursor.
- Cursor: the left stick moves the pointer, the d-pad nudges it, SOUTH presses under it, and nothing moves the
  focus. Going in, the pointer starts on what held the focus and the focus is let go. Coming out, the focus snaps
  to the element under the pointer or nearest it (`TapDevice::focusNearest`, `focusedScreenCentre`).
- `navigation(false)` is for play away from the menus: no focus moves, no pointer, only the buttons, sticks and
  signals. Coming back, nothing still held counts as a press. The game has `game.controllerNavigation(bool)`.
- Game text boxes: a page's text box that can hold the focus is typed in only while it does (`Interface::focus`
  listens for the focus leaving). The Login page's two text boxes are focusable and press on focus.
- The Workbench's "Controller cursor" setting follows the pad's own toggle, and its field menu is for its own
  fields only, never a text box in the scene being edited.
- Not yet: selection contexts (windows a trigger would step between), a default and a remembered focus per screen,
  a virtual keyboard, and the game's Login page driven by a real pad.

## Increment 38: selection contexts (2026-11-17)
- `MV::Scene::FocusGroup` is a component that makes its node a selection context. It saves `kind` (WINDOW or TABS),
  `order`, `modal`, `defaultFocus` (a node id) and the script receivers of its `onBack` signal.
- `TapDevice::focusContext(scope)` settles the window in use and returns the scope moves keep to: a modal window
  when one shows (the top-most), else the innermost window the focus is in, else the one last stepped to. Called
  once a frame, it also puts the focus into a modal window as it shows, back where it was when that hides, and onto
  something else in the window when what held the focus is hidden or disabled. Each group remembers its last focus.
- `focusWindowNext` / `focusWindowPrevious` step windows (remembered, else default, else the first focusable that
  is not a tab header). `focusTabNext` / `focusTabPrevious` press the next or previous header of the tab strip in
  the window in use, as a pointer would, leaving the focus where it is. `focusBack` emits the window's `onBack`.
- The pad: triggers step windows, shoulders step tabs, EAST backs out, and the d-pad and stick stay inside the
  window in use. The shoulders no longer step to the next and previous control. Contexts are only settled while a
  pad is attached and snapping, so a mouse-only player never has the focus moved for them.
- `Clickable::press()` no longer takes the focus: only a pointer's press does. `canTakeFocus` is public.
- Scripts reach a node's group with `componentFocusGroup()`.
- Tests: `focus_group_tests.cpp` (scoped moves, stepping, modal take and return, focus lost, tabs, back, the pad's
  mapping) and a save and load of both kinds.
- Not yet: nothing can add a FocusGroup to a node except C++ and a hand-edited scene (the Workbench has no
  attach-component verb, and scripts cannot attach), no shipped screen has one, and keyboard Tab does not use them.

## Increment 39: selection contexts nest (2026-11-17)
- A FocusGroup on a parent node holds FocusGroups on nodes under it. The group in use is the innermost one the
  focus is in, so a move from a parent's control onto a child group's control goes down a level.
- Stepping (`focusWindowNext` / `focusWindowPrevious`, the pad's triggers) is between the groups side by side under
  the same parent. A group with none beside it steps the level above. With nothing in use it starts at the first
  top group.
- Back (`focusBack`, the pad's east button) emits the group's `onBack` when something listens for it. Otherwise it
  goes up a level, onto a control of the parent group that is in no group of its own (the one the parent
  remembers, else its first). A parent with no such control is backed out of in turn. At the top with nothing
  listening it does nothing and returns false, for the host to use.
- A window remembers only what it holds itself, not what a group under it holds.
- A modal window is the top of its own levels: groups inside it step and back as anywhere, and nothing steps or
  backs out past it.
- Tests: three nesting cases in `focus_group_tests.cpp`.

## Increment 40: Explicit Select, and plain groups are contiguous (2026-11-17)
- `FocusGroup` saves `explicitSelect` (off unless set) and `highlightView`.
- Without it a group is contiguous with what is around it. `TapDevice::focusMoveWithin` looks inside the group in
  use first, so the group's own layout decides, and with nowhere to go there looks a level out: a move flows into
  a group, between its controls, and out the other side. A plain group no longer holds the d-pad in.
- With it the group is one stop. A move from outside selects the group as a whole (`selected()`, `onHighlightEnter`
  / `onHighlightLeave`, and its `highlightView` shows), activating it goes in (`focusEnter`: remembered, else
  default, else first), no move leaves it, and backing out selects it as a whole again and emits `onBack`.
  Stepping to it with the triggers selects it as a whole. Its controls are never a stop for a move made outside
  it, for Tab order or for `focusNearest`; one chosen directly, as a pointer does, is inside at once.
- A focus candidate may now be a group: `FocusCandidate::group`, and `under(scope)` beside `canTake(scope)`, so
  nothing casts a subject to find where it is.
- The pad's d-pad and stick use `focusMoveWithin`; SOUTH on a selected group goes in and EAST comes back out.
- The window last stepped to counts as in use only while nothing holds the focus.
- Tests: contiguous moves, the explicit group's whole life, the pad going in and out, and the save and load of
  both new keys. `MV Network`'s unread-timeout test failed once in a full run and passed alone: a timing test.

## Increment 41: the nearest control, by its edges (2026-11-17)
- A straight focus move measures the gap between the facing edges of where it starts and each candidate, and how
  far the two miss each other sideways (counted double), with the distance between middles only to break a tie. A
  move into a row lands on the end it came from, a move into a column from the side lands on the one level with
  where it came from, and a wide control is as near as its edge is.
- Something squarely ahead (wholly past the edge and overlapping sideways) is a candidate however far off its
  middle is, so a small button over the end of a wide bar reaches the bar and comes back.
- Diagonal moves still go by middles. A named `focusLink` still comes first, for the layout this gets wrong.

## Increment 42: creating nodes, adding and removing components (2026-11-17)
- `App::createChildNode`: a new empty node under the selected one, or under the scene's root with nothing
  selected, named as no node beside it is (node, node_2), and selected. It is in the Edit menu (Create Child Node)
  and at the top of the right-press menu of a Hierarchy row and of the scene (Create Child).
- The inspector ends with an Add Component button that lists the kinds: Sprite, Text, Clickable, Button,
  FocusGroup, Stencil, Grid, Parallax. `App::attachComponent(kind)` makes one on the selected node as the scene
  loader would, sized so it can be seen and pressed at once.
- Each component's header has a remove button. `App::removeComponent` takes it off its node; undo puts it back in
  the place it had among the node's components, not last.
- Each of the three is one undo step, and the inspector and Hierarchy follow.
- A FocusGroup's `kind` is a choice row (Window, Tabs).
- The self-test makes nodes, adds a Sprite through the button and its list, removes it from its header, undoes
  and redoes, adds every kind, removes one from the middle and checks undo restores the order.
- Not yet: creating a node from a prefab or a primitive with art already on it, reordering components, Scroller,
  Spine, Emitter and the other kinds, and a shortcut key for Create Child Node.

## Increment 43: a node's bounds take in its children where they are (2026-11-17)
- `Node::bounds(true)` and `childBounds()` carry each child's box through the child's position, rotation and
  scale. Before, each child's box was joined as it stood in the child's own space, so a parent of two children
  set apart reported one box at its origin.
- A child moving, scaling or turning, and a child added or removed, mark the child bounds of every node above it
  stale, as showing and hiding already did.
- `Slider` measures its track from its node's own box (`bounds(false)`): its handle is a child, and with the
  children counted the track would stretch as the handle travelled. A handle taller than its track is now centred
  on it, which is the one thing that looks different (the Project panel's tile-size slider).
- A FocusGroup's place on screen is its node's `screenBounds()` again.
- The inspector's `rowScreenPoint` aims at the first control in a field, where it used to rely on the old bounds
  putting a field's middle on its first part.
- Tests: `bounds_tests.cpp` (position, own box with children, grandparents, following a move, a scale, a turn,
  hide and show, add and remove, place on screen, the slider). The Login screenshot is the same pixel for pixel.

## Increment 44: the focus scrolls to what it lands on (2026-11-17)
- `Component::focusOnChild(node)` is a component's turn to bring a node under its owner into view; it does nothing
  unless a component says otherwise. `Node::focusInParents()` asks every component on every node above a node,
  nearest first. A FocusCandidate carries it as `reveal`, which `TapDevice::focus` runs when a move, a walk in
  order or `Clickable::focus()` brings the focus. A pointer's press does not: the list stays under the pointer.
- `Scroller::focusOnChild` scrolls its content as far as it takes to show the node and no further; something
  longer than the view shows from its start. It is bound to script, as is `Node::focusInParents`.
- `Scroller` keeps content longer than its view covering the view from end to end, and leaves content that fits
  where it was put. Before, it held the content inside the view, so content that fitted slid about in it and
  content longer than it did not scroll. Its sums are in its own node's space, so a turned scroller scrolls true.
- `Node::visibleScreenBounds()` is what shows of a node on screen: its children cut to a Clipped or Stencil on it,
  and all of it to those above. `bounds()` and `screenBounds()` stay the whole extent, which laying out and
  scrolling measure. `Node::clippedAwayBy(box)` names the node whose clipping hides a box altogether.
- A focus candidate is, for a move, where what shows of it is. One scrolled wholly out of view keeps its true
  place and names the node hiding it (`hiddenUnder`): a move by direction reaches it only from under that node,
  and `focusNearest` never picks it. So up from a control under a list lands on the last row showing, and down
  from that row goes on to the hidden one and scrolls to it. A FocusGroup on a list is as big as its view.
- Tests: `scroller_tests.cpp` (a drag to each end, content that fits, focusOnChild, a move and a walk scrolling,
  a pointer's press not scrolling, hidden rows from outside, what shows of a node, an explicit group on a list).
- Not yet: the scroll is at once, not eased; no margin is kept round what is scrolled to; a Scroller has no
  wheel or stick scrolling of its own.

## Known gaps (next increments)
- Write-notify = postLoadStep coarse hook; refine per-component.
- Authoring verbs still only in Source/Editor: save selection as BindSnap, and the component kinds Add Component
  does not list — old-editor deletion awaits that parity (Dev sign-off).
- Gizmo residue (increment 13): aspect-lock not ported; inspector refreshes at gesture end;
  selection rubber band deferred.
- Bindstone_Common.vcxitems is shared with the Android client project — Workbench compiles
  there too; verify Android build when next touched.
- Landed and no longer gaps: enum rows + Palette color picker + VectorRows (increment 4),
  tree hover + drag-reparent (5), undo/redo (6), gizmos (13), move/rotate/scale tools and F framing (19).
