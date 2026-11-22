# MV Render Scene Graph

Contains the scene graph implementation used to render sprites and UI.

Key classes:
- `Node` (`node.*`) / `Component` (`component.*`) – base scene objects; `Drawable` is the base of everything drawn.
- `Sprite`, `Text`, `Button`, `Scroller`, `Slider`, `Clipped`, `Emitter`, `Spine` (`spineMV.*`), etc. for visual elements.
- `Light` (`light.*`) – a Sprite Lamp style point light. A drawable whose shader is lit (`Shaders/lit.*`, id `lit`) gets
  the lights of its scene that reach it (`Render::Lighting`, gathered once a frame) and a normal map: its texture slot 1,
  else `<image>_Normal` beside texture 0's file, else a flat surface. A light gives nothing inside its inner radius (set it
  to a painted glow's size), fades out on what is painted near white, and its sum passes a soft knee that never reaches white.
- Script bindings and the serialization identity live in each type's `.cpp` as a `jai::registrar`.
