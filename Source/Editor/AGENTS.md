# Editor Code (legacy)

The in-game editor reachable from the title menu (`Game/gameEditor.*`). `Source/Workbench` is its
replacement; this one stays compiled until the Workbench covers its authoring verbs (create/delete/
duplicate node, attach/detach component, save selection as BindSnap, texture picker, anchor editor).
It is listed in `Bindstone_Common.vcxitems`, so the servers and the Android client compile it too.

Important files:
- `editor.*` – main editor application logic.
- `componentPanels.*` / `editComponents.*` – UI panels for editing scene graph components.
- `editorSceneGraphPanel.*` – the scene hierarchy view.
- `texturePicker.h`, `anchorEditor.h` – the pickers the Workbench does not have.
- `editorFactories.*`, `editorControls.*`, `editorDefines.h` – widget factories and constants.
