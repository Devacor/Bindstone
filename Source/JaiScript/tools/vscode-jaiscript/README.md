# JaiScript for VS Code

Language support for [JaiScript](../../), the embedded scripting language used by the
MutedVision engine / Bindstone.

## What it does

- **Syntax highlighting** (works now, standalone) — TextMate grammar for `.script`, `.jai`,
  and `.jaibite` files: keywords, the `int` / `auto` / `var` type ladder, `function` / `lambda`,
  strings and format-strings, comments, numbers.
- **Attach debugging** (once the C++ `debug_connector` ships) — breakpoints, stepping, call
  stack, and variable inspection/edit, over the Debug Adapter Protocol (DAP).

The debugger uses the **attach model with no bundled adapter process**: the DAP server lives
*inside* the C++ host (`jai::debug_connector`), which opens a raw TCP port. This extension just
points VS Code's DAP client straight at that socket via a `DebugAdapterServer` descriptor — there
is nothing to spawn.

## How to attach

1. Build the host with the debugger enabled (CMake `-DJAISCRIPT_ENABLE_DEBUGGER=ON`; the
   `debug_connector` and socket code are compiled out otherwise).
2. In the host, inject a connector on the engine:

   ```cpp
   engine->set_debug_connector(std::make_shared<jai::debug_connector>(1234));
   ```

3. Add a `.vscode/launch.json` (the `port` must match the one above):

   ```json
   {
     "version": "0.2.0",
     "configurations": [
       {
         "type": "jaiscript",
         "request": "attach",
         "name": "Attach to JaiScript",
         "host": "127.0.0.1",
         "port": 1234
       }
     ]
   }
   ```

4. Run the host, then start the "Attach to JaiScript" configuration. Set breakpoints in your
   `.jai` sources; VS Code auto-opens the top stack frame's file on each stop.

Breakpoints key on the original `.jai` source path, so they bind whether a unit was loaded from
source or from a pre-parsed `.jaibite`.

## Current status

- Grammar: works standalone, no host required.
- Debugger: requires the app built with `JAISCRIPT_ENABLE_DEBUGGER` and a running
  `debug_connector`. See `docs/DEBUGGER_DESIGN.md` for the design and phasing (interpreter
  backend first, VM parity later).

## Building the extension

```
npm install
npm run compile   # tsc -p ./  ->  ./out/extension.js
```

The grammar (`syntaxes/jaiscript.tmLanguage.json`) and `language-configuration.json` are
maintained separately and referenced from `package.json`.
