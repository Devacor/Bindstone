# MV Render Scene Graph

Contains the scene graph implementation used to render sprites and UI.

Key classes:
- `Node` / `component.*` – base scene objects.
- `Sprite`, `Text`, `Button`, `Scroller`, etc. for visual elements.
- `sceneHooks.cxx` registers components with the MV script engine.
