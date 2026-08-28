# Game Interface Helpers

GUI related helpers used in the client.

Key files:
- `interfaceManager.*` – `Interface` (one page: a loaded `view.scene` plus `initialize`/`onShow`/`onHide`
  script hooks) and `InterfaceManager` (page registry). Their script bindings are the `jai::registrar`s
  at the top of `interfaceManager.cpp`.
- `guiFactories.*` – helpers to create GUI controls.
