# MV Render Scene Graph

Contains the scene graph implementation used to render sprites and UI.

Key classes:
- `Node` / `component.*` – base scene objects.
- `Sprite`, `Text`, `Button`, `Scroller`, etc. for visual elements.
- Script bindings live in each type's `.cpp` as a `jai::registrar` (`sceneHooks.cxx` is an empty tombstone; it registers nothing).
