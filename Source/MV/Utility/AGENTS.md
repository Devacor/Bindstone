# MV Utility

General helper classes used across the project.

Highlights:
- `initializeThreadPool` (`log.h`): the logging and park-at-once setup MV applies to every `jai::thread_pool` it owns.
- Logging (`log.h` / `log.cpp`).
- Task system (`task.h`, `task.cpp`).
- Service registry (`services.hpp`), stopwatch timers, localization, string/UTF-8 utilities, optional calls.
