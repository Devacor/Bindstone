# Interface Assets

UI pages: a `view.scene` per page plus the JaiScript that drives it.

- `Login/` – `view.scene`, `initialize.script` (page hooks), `loginButton.script` (the button's
  `onAccept` receiver, persisted inside the scene).
- `Main/` – `view.scene`, `initialize.script`, `playButton.script`.
- `interfaceManager.script` – creates the pages at runtime through `InterfaceManager`
  (`Source/Game/Interface/interfaceManager.cpp` binds it).
