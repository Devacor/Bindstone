# Source Directory

Top level source code.  Major components:

- `MV` – the MutedVision engine.  Contains rendering, networking, utilities, physics, scripting and other engine modules.
- `Game` – gameplay specific code such as `Game/game.h`, `building.h`, `player.h` and the network layer for game and lobby servers.
- `Editor` – level/editor GUI code.
- `SolutionSpecific` – entry points for each executable (client, Android client, game server, lobby server).

See the AGENTS files inside those folders for more details.
