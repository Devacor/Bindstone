#!/usr/bin/env bash
# A current build of main's tip, for a program that runs it between landings (a server, a port, a long test).
#
#   Tools/Landing/build_main.sh [<cmake target>...]      # default: jaiscript_runner jai_spider
#
# Run it from the main checkout. Under the landing lock, so main's tree is a landed tip and no landing builds beside it,
# it builds the targets in the landing's Release tree (out/build/x64-Release-land, so the build is incremental and the
# next landing finds it current), copies that tree's bin folder to Source/JaiScript/out/main-current/<tip>/ and writes
# that folder's path to Source/JaiScript/out/main-current/latest.txt. The last line it prints is the folder.
#
# A folder per tip, never one folder rewritten: a program that is running cannot be replaced on Windows, and a reader
# of latest.txt always finds a whole build. The three newest folders are kept; one still in use stays until it is free.
# A landing trusts gate stamps and may skip its own build, so the landing tree lags main after a run of trusted
# landings; this is the way to bring it and a consumer up to date.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export JAI_RELEASE_DIR="${JAI_RELEASE_DIR:-out/build/x64-Release-land}"
export JAI_BUILD_JOBS="${JAI_BUILD_JOBS:-12}"
. "$REPO/Source/JaiScript/tools/ci/ci_lib.sh"
. "$REPO/Tools/Landing/land_lock.sh"

TARGETS=("$@")
[ ${#TARGETS[@]} -gt 0 ] || TARGETS=(jaiscript_runner jai_spider)
CURRENT="$REPO/Source/JaiScript/out/main-current"

cd "$REPO" || exit 1
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || { echo "build_main.sh builds the main checkout: this one is on $(git rev-parse --abbrev-ref HEAD)"; exit 2; }
land_lock_take "build_main.sh ${TARGETS[*]}"
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	echo "main has tracked modifications: what would be built is not a tip"
	exit 1
fi
TIP="$(git rev-parse --short=9 HEAD)"
echo "building main $TIP: ${TARGETS[*]}"
lockq_wait
jai_build release "${TARGETS[@]}" || exit 1

BIN="$(jai_dir_of release)/bin"
OUT="$CURRENT/$TIP"
mkdir -p "$OUT" || exit 1
cp -rf "$BIN/." "$OUT/" || { echo "the build could not be copied to $OUT (a program there is running: it already holds this tip)"; exit 1; }
printf '%s\n' "$OUT" > "$CURRENT/latest.txt"
# newest first by the time each folder was made; anything past the third goes, unless a running program holds it
ls -1dt "$CURRENT"/*/ 2>/dev/null | tail -n +4 | while IFS= read -r old; do rm -rf "$old" 2>/dev/null || true; done
echo "main $TIP built: ${TARGETS[*]}"
echo "$OUT"
