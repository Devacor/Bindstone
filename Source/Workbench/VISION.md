# Workbench: world-class — the North Star

Goal: not "Unity parity" — Unity has 20 years and 5000 people. Win by exploiting the three
structural advantages this codebase has that Unity architecturally CANNOT copy, and by
being the best 2D creature-game editor in existence rather than a general 3D tool.

## The three unfair advantages

1. **No mode wall.** Unity's edit-mode/play-mode split (domain reloads, state loss, compile
   waits) is its most-hated wall — and it's structural (C# domains). We have hot reload WITH
   instance migration, zero static state, and one property bridge into live objects. The
   game should simply ALWAYS be running in the viewport: "play" is unpausing, editing is
   mutating live state, and nothing is ever lost.
2. **One reflection substrate.** Unity juggles SerializedProperty vs Mono reflection vs
   UI bindings, and they disagree. Here, ONE property declaration feeds the inspector,
   serialization, script, undo, networking — diffs, overrides, animation, and replication
   are all the same operation on the same records.
3. **The editor is made of the thing it edits.** Panels are .jai + engine widgets, hot
   reloaded. Game UI (InterfaceManager pages) uses the same scene format the editor edits.
   Unity's editor UI is a different universe from its runtime UI; ours is the same one —
   so every editor improvement is a game-UI improvement and vice versa.

## Pillars (prioritized)

P1 — LIVE SIM VIEWPORT (the mode-wall killer). GameInstance running inside the editor:
    pause / step-frame / speed control; select and edit live creatures mid-battle through
    the inspector; script hot reload applying mid-sim. This single pillar is the demo that
    makes people say "Unity can't do that."
P2 — GIZMO SUITE. Port + surpass the old editor's handles: multi-select, rect transform,
    snapping (grid/vertex), PathMap paint, Emitter live preview, all undo-integrated
    (drag = one command, same coalescing as scrub fields).
P3 — BINDSNAP OVERRIDES. Nested BindSnaps with per-instance property overrides = Unity's
    beloved prefab variants, implemented as property DIFFS (the bridge makes diffing two
    owners trivial). Overrides shown bold in the inspector, revertable per-property.
P4 — COMMAND PALETTE (steal from Blender/VS Code): Ctrl+P fuzzy-searches everything —
    nodes, assets, properties, AND commands. We already have the Command pattern; every
    editor action becomes a named, searchable, bindable, scriptable operator.
P5 — PROPERTY TIMELINE. Generic keyframe animation over ANY reflected property (one
    animator for transforms, colors, emitter params...). The property bridge means one
    implementation animates everything; Spine preview docks beside it.
P6 — IN-EDITOR SCRIPT DEBUGGER. The DAP server already exists; dock a client panel:
    breakpoints in creature scripts AND in the editor's own .jai panels — debug the editor
    inside the editor.
P7 — EYES-DRIVEN QUALITY GATES. -workbenchshot golden images diffed in CI; verifyassets +
    smoke as the merge gate. The editor tests itself visually.

## Steal list
- Godot: scene dock ergonomics, bottom-panel animation editor, @export-style hints syntax.
- Unity: prefab-override UX (bold + right-click revert), drag-everything ergonomics.
- Blender: operators-first design (palette = P4), workspace tabs.
- Figma (horizon): the NetworkObject delta layer already exists — multiplayer scene editing
  with live cursors is a real (if distant) possibility no game editor ships.

## Already banked (foundation)
Dock/tabs/hot-reload panels, reflection inspector w/ scrub+vectors+swatches+enums, undo/redo
command history, project browser w/ thumbnails + BindSnap drag-in, click-select + pan/zoom,
custom chrome, DPI theme, screenshot loop, capture-semantics contract + lint.
