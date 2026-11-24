# Visual Studio Projects

MSVC project files used to build on Windows and Android.  Useful when locating build settings.

Contains subfolders for clients, servers and the MutedVision library as well as the SDL3 projects (SDL3, SDL3_image and SDL3_ttf, built from source; one
`SDL3_Static.lib` on Windows).
`JaiScript/JaiScript_Windows` builds JaiScript's `source/implementation/**/*.cpp` through one wildcard into
`jaiscript.lib` (the interpreter under `source/interpreter/` is outside it by design), compiled without `/GL` and
without the engine's precompiled header; `MutedVision_Windows` references it and folds it into `mutedvision.lib`.
`MutedVision_Android` compiles the same wildcard itself.
Shared source lists are `.vcxitems`: `MutedVision/MutedVision_Common.vcxitems` (engine),
`BindstoneClient/Bindstone_Common` (game code, client
and both servers) and `BindstoneClient/Bindstone_Editor` (Editor + Workbench, clients only). A new `.cpp`
outside the JaiScript wildcard must be added to the right `.vcxitems`.
Command-line builds: see the root `AGENTS.md`.
