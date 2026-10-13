# Visual Studio Projects

MSVC project files used to build on Windows and Android.  Useful when locating build settings.

Contains subfolders for clients, servers and the MutedVision library as well as SDL2 projects.
Shared source lists are `.vcxitems`: `MutedVision/MutedVision_Common.vcxitems` (engine, plus JaiScript's
`source/implementation/**/*.cpp` through one wildcard), `BindstoneClient/Bindstone_Common` (game code, client
and both servers) and `BindstoneClient/Bindstone_Editor` (Editor + Workbench, clients only). A new `.cpp`
outside the JaiScript wildcard must be added to the right `.vcxitems`.
Command-line builds: see the root `AGENTS.md`.
