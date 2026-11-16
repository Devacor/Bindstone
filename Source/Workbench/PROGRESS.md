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

## Known gaps (next increments)
- Tab drag to empty space = no-op (floating windows next).
- Write-notify = postLoadStep coarse hook; refine per-component.
- Authoring verbs still only in Source/Editor: create/delete/duplicate node, attach/detach
  component, save selection as BindSnap, texture picker, anchor editor — old-editor deletion
  awaits that parity (Dev sign-off).
- Gizmo residue (increment 13): aspect-lock not ported; inspector refreshes at gesture end;
  selection rubber band deferred.
- Bindstone_Common.vcxitems is shared with the Android client project — Workbench compiles
  there too; verify Android build when next touched.
- Landed and no longer gaps: enum rows + Palette color picker + VectorRows (increment 4),
  tree hover + drag-reparent (5), undo/redo (6), gizmos (13), move/rotate/scale tools and F framing (19).
