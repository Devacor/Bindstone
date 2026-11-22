#!/usr/bin/env bash
# JaiDOOM launcher (Linux, macOS, Git Bash): uses a jaiscript next to this file, then one on
# PATH, then the repository's build/bin (the game finds wads/ from its own
# location, so the working directory does not matter). Arguments pass through: ./play.sh -- freedoom2 MAP01
# It passes fetch, so a first run downloads the game data on its loading screen; jaidoom.jai run without it never does.
cd "$(dirname "$0")/scripts" || exit 1
[ "${1:-}" = "--" ] && shift
for cand in "${JAI_RUNNER:-}" ../jaiscript ../jaiscript.exe "$(command -v jaiscript 2>/dev/null)" \
            ../../../build/bin/jaiscript.exe ../../../build/bin/jaiscript; do
	if [ -n "$cand" ] && [ -x "$cand" ]; then exec "$cand" jaidoom.jai -- fetch "$@"; fi
done
echo "jaiscript runner not found: set JAI_RUNNER, put jaiscript next to play.sh, or build it (docs/BUILD.md)" >&2
exit 1
