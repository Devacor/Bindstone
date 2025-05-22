# Repository Overview

This repo contains the game **Bindstone** built with the **MutedVision (MV)** engine.  The MV engine was written by Devacor (Michael Hamilton).  Third party libraries reside under the `External` folder and include packages such as Box2D, ChaiScript, boolinq (LINQ), cereal, gl3w, glm and SDL2.

## Projects
- Windows client and Android client (see `Source/SolutionSpecific/BindstoneClient` and `BindstoneClient_Android`).
- Game server (`Source/SolutionSpecific/BindstoneGameServer`).
- Lobby server (`Source/SolutionSpecific/BindstoneLobbyServer`).

The `VSProjects` folder contains Visual Studio project files.  Source code lives under `Source` and game assets under `Assets`.

Each major subdirectory has its own `AGENTS.md` with more specific notes.
