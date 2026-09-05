#!/usr/bin/env bash
# JaiDOOM launcher (Linux, macOS, Git Bash). Runs from scripts/ so the game's relative WAD
# path (../wads/) resolves; uses a jaiscript next to this file, then one on PATH, then the
# repository's Release or Debug build. Arguments pass through: ./play.sh -- freedoom2 MAP01
cd "$(dirname "$0")/scripts" || exit 1
for cand in "${JAI_RUNNER:-}" ../jaiscript ../jaiscript.exe "$(command -v jaiscript 2>/dev/null)" \
            "../../../out/build/x64-Release BENCHMARKS/bin/jaiscript.exe" "../../../out/build/x64-Release BENCHMARKS/bin/jaiscript" \
            "../../../out/build/x64-Debug/bin/jaiscript.exe" "../../../out/build/x64-Debug/bin/jaiscript"; do
	if [ -n "$cand" ] && [ -x "$cand" ]; then exec "$cand" jaidoom.jai "$@"; fi
done
echo "jaiscript runner not found: set JAI_RUNNER, put jaiscript next to play.sh, or build it (docs/BUILD.md)" >&2
exit 1
