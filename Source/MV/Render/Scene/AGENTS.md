# MV Render Scene Graph

Contains the scene graph implementation used to render sprites and UI.

Key classes:
- `Node` (`node.*`) / `Component` (`component.*`) – base scene objects; `Drawable` is the base of everything drawn.
- `Sprite`, `Text`, `Button`, `Scroller`, `Slider`, `Clipped`, `Emitter`, `Spine` (`spineMV.*`), etc. for visual elements.
- Script bindings and the serialization identity live in each type's `.cpp` as a `jai::registrar`.
