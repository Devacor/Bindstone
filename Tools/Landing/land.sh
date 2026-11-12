#!/usr/bin/env bash
# Land a branch on main: cherry-pick its commits above a base one by one, then gate the result.
#
#   Tools/Landing/land.sh <label> <range base> <branch> <expected main tip> [<base2>..<tip2>...]   # targeted tier
#   Tools/Landing/land.sh --battery <label> <range base> <branch> <expected main tip> [...]          # milestone tier
#   Tools/Landing/land.sh --battery-only <label>                                                      # milestone tier on main, no picks
#   Tools/Landing/land.sh --plan <rev-range>                                                          # the targeted choices, run nothing
#
# Picks: a commit whose subject main already carries is skipped (a mirrored commit) unless main reverted that twin; an
# empty pick is skipped; a conflict confined to the union files (LAND_UNION_FILES, default the perf ledger) resolves as the
# union; any other conflict aborts with main untouched by that pick.
#
# The targeted tier runs what the picked paths need: Source/JaiScript/tools/ci/targeted.sh for the package, the client build
# when MutedVision compiles a changed file, and -verifyassets when the property system, the archives, the engine or the
# assets changed. The milestone tier runs Source/JaiScript/tools/ci/battery.sh and the full client gate.
#
# Landings queue: one land.sh runs at a time per repository (bindstone-land.lock in its git directory; a lock whose owner
# died is cleared). Every heavy step waits while someone holds the timing lock. Logs go to LAND_LOG_DIR (default a folder of the run's own,
# bindstone-land/<time>-<pid>, under ../tempclaudefolders/tmp beside the checkout when that folder exists, else in the
# system temp directory; its run folders older than a week are removed). JAI_DEBUG_DIR and JAI_RELEASE_DIR default to the Visual Studio build directories under Source/JaiScript.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export JAI_DEBUG_DIR="${JAI_DEBUG_DIR:-out/build/x64-Debug}"
export JAI_RELEASE_DIR="${JAI_RELEASE_DIR:-out/build/x64-Release BENCHMARKS}"
export JAI_CMAKE_ARGS_RELEASE="${JAI_CMAKE_ARGS_RELEASE:--DJAISCRIPT_ENABLE_BENCHMARKS=ON}"
. "$REPO/Source/JaiScript/tools/ci/ci_lib.sh"
# A tempclaudefolders folder beside the checkout takes every temp file and log; the timing lock stays where other runs look
LAND_TEMP_ROOT="$(dirname "$REPO")/tempclaudefolders"
if [ -d "$LAND_TEMP_ROOT" ]; then
	export JAI_TIMING_LOCK="$LOCKQ_LOCK"
	mkdir -p "$LAND_TEMP_ROOT/tmp"
	export TMPDIR="$LAND_TEMP_ROOT/tmp"
	if command -v cygpath >/dev/null 2>&1; then export TEMP="$(cygpath -w "$TMPDIR")"; else export TEMP="$TMPDIR"; fi
	export TMP="$TEMP"
fi
# Two landings at once must never write one log: each run defaults to a folder of its own
LAND_LOG_ROOT="$(lockq_temp_dir)/bindstone-land"
if [ -d "$LAND_LOG_ROOT" ]; then find "$LAND_LOG_ROOT" -mindepth 1 -maxdepth 1 -type d -name "20[0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9]-*" -mtime +7 -exec rm -rf {} + 2>/dev/null; fi
LOG_DIR="${LAND_LOG_DIR:-$LAND_LOG_ROOT/$(date +%Y%m%d-%H%M%S)-$$}"
export LAND_LOG_DIR="$LOG_DIR" JAI_CI_LOG_DIR="$LOG_DIR"
mkdir -p "$LOG_DIR"
UNION_FILES="${LAND_UNION_FILES:-Source/JaiScript/docs/PERF_PASS_2026-09-02.md}"
# Generated from the site's own sources: a conflict in them is settled by regenerating once the sources merged
SEARCH_INDEX="Source/JaiScript/docs/site/assets/search-index.js Source/JaiScript/docs/site/assets/search-text.js"
API_OUTPUTS="Source/JaiScript/docs/site/reference/api.html Source/JaiScript/include/jaiscript/detail/builtin_api_arities.hpp"

# client_choice <rev-range>: sets CLIENT_BUILD_WHY and CLIENT_VERIFY_WHY from the paths the range touches
client_choice() {
	CLIENT_BUILD_WHY=""
	CLIENT_VERIFY_WHY=""
	local p
	while read -r p; do
		case "$p" in
			Source/JaiScript/include/jaiscript/properties/* | Source/JaiScript/include/jaiscript/properties.hpp | Source/JaiScript/include/jaiscript/serialization/* | \
			Source/JaiScript/source/implementation/serialization/* | Source/JaiScript/source/implementation/properties.cpp | Source/JaiScript/source/implementation/schema_binding.cpp)
				CLIENT_BUILD_WHY="${CLIENT_BUILD_WHY:-$p}"; CLIENT_VERIFY_WHY="${CLIENT_VERIFY_WHY:-the property system or the archives ($p)}" ;;
			Source/MV/* | Source/Game/* | Assets/*)
				CLIENT_BUILD_WHY="${CLIENT_BUILD_WHY:-$p}"; CLIENT_VERIFY_WHY="${CLIENT_VERIFY_WHY:-the engine, the game or the assets ($p)}" ;;
			Source/JaiScript/include/* | Source/JaiScript/source/implementation/* | Source/Workbench/* | Source/Editor/* | Source/SolutionSpecific/* | VSProjects/*)
				CLIENT_BUILD_WHY="${CLIENT_BUILD_WHY:-$p}" ;;
		esac
	done < <(git -C "$REPO" diff --name-only "$1" | tr -d '\r')
	if [ -n "$CLIENT_VERIFY_WHY" ]; then echo "chose: client build + -verifyassets, because $CLIENT_VERIFY_WHY"
	elif [ -n "$CLIENT_BUILD_WHY" ]; then echo "chose: client build, because MutedVision compiles a changed file ($CLIENT_BUILD_WHY)"
	else echo "chose: no client step (nothing the client compiles or loads changed)"; fi
}

TIER=targeted
case "${1:-}" in
	--battery) TIER=battery; shift ;;
	--battery-only) TIER=battery-only; shift ;;
	--plan)
		[ -n "${2:-}" ] || { echo "usage: land.sh --plan <rev-range>"; exit 2; }
		bash "$REPO/Source/JaiScript/tools/ci/targeted.sh" --plan "$2"
		client_choice "$2"
		exit 0
		;;
esac
L="${1:-}"
if [ "$TIER" = battery-only ]; then
	[ -n "$L" ] || { echo "usage: land.sh --battery-only <label>"; exit 2; }
else
	BASE="${2:-}"; BR="${3:-}"; TIP="${4:-}"
	[ -n "$L" ] && [ -n "$BASE" ] && [ -n "$BR" ] && [ -n "$TIP" ] || { echo "usage: land.sh [--battery] <label> <range base> <branch> <expected main tip> [<base>..<tip>...]"; exit 2; }
	shift 4
	EXTRA_RANGES=("$@")
fi

step() { lockq_wait; echo "[$(date +%H:%M)] step: $1"; }

# One landing at a time per repository: landings share main's checkout and its build trees, so a second one waits here
LAND_LOCK="$(cd "$REPO" && cd "$(git rev-parse --git-common-dir)" && pwd)/bindstone-land.lock"
LAND_LOCK_HELD=0
land_lock_release() {
	if [ "$LAND_LOCK_HELD" = 1 ] && grep -q " pid=$$ " "$LAND_LOCK/owner.txt" 2>/dev/null; then
		rm -f "$LAND_LOCK/owner.txt"
		rmdir "$LAND_LOCK" 2>/dev/null
	fi
	LAND_LOCK_HELD=0
}
land_lock_take() {
	local said="" owner pid
	while ! mkdir "$LAND_LOCK" 2>/dev/null; do
		owner=$(cat "$LAND_LOCK/owner.txt" 2>/dev/null)
		pid=$(printf '%s' "$owner" | sed -n 's/.* pid=\([0-9]*\) .*/\1/p')
		if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
			echo "clearing a landing lock its owner left behind ($owner)"
			rm -f "$LAND_LOCK/owner.txt"; rmdir "$LAND_LOCK" 2>/dev/null; continue
		fi
		[ -n "$said" ] || { echo "waiting for another landing to finish: ${owner:-(starting)}"; said=1; }
		sleep 10
	done
	echo "$1 pid=$$ winpid=$(cat /proc/$$/winpid 2>/dev/null) $(date +%H:%M)" > "$LAND_LOCK/owner.txt"
	LAND_LOCK_HELD=1
	trap 'land_lock_release' EXIT
	trap 'land_lock_release; exit 130' INT TERM HUP
}

cd "$REPO" || exit 1
land_lock_take "land.sh $L"
[ "$(git rev-parse --abbrev-ref HEAD)" = "main" ] || { echo "not on main"; exit 1; }
# the comparison harness edits under examples/, the regenerated atlases and the test library are never a pick's
DIRTY=$(git status --short | grep -v '^??' | grep -v ' Source/JaiScript/examples/' | grep -v ' Assets/Atlases/.*.png$' | grep -v ' Source/MV/Tests/mv_tests.lib$')
[ -z "$DIRTY" ] || { echo "main has tracked modifications outside examples/; not landing"; echo "$DIRTY" | head; exit 1; }
git status --short | grep -v '^??' | grep ' Source/JaiScript/examples/' | sed 's/^/left as is (not mine): /'

BEFORE=$(git rev-parse --short HEAD)
if [ "$TIER" != battery-only ]; then
	[ "$BEFORE" = "$TIP" ] || [ "$(git rev-parse --short "$TIP" 2>/dev/null)" = "$BEFORE" ] || { echo "main tip is $BEFORE, expected $TIP; not landing"; exit 1; }
	COMMITS=$(git rev-list --reverse "$BASE..$BR"; for r in "${EXTRA_RANGES[@]+"${EXTRA_RANGES[@]}"}"; do git rev-list --reverse "$r"; done)
	echo "picking $(echo "$COMMITS" | grep -c .) commits: $BASE..$BR${EXTRA_RANGES:+ ${EXTRA_RANGES[*]}}"
	MASTER_SUBJECTS=$(git log --format=%s -300 HEAD)
	for c in $COMMITS; do
		subject=$(git log -1 --format=%s "$c")
		twin=$(git log --format='%H %s' -300 HEAD | awk -v s="$subject" 'substr($0, 42) == s { print substr($1, 1, 8); exit }')
		if [ -n "$twin" ] && printf '%s\n' "$MASTER_SUBJECTS" | grep -q "^Revert .*$twin"; then echo "re-picking $(git log --oneline -1 "$c" | cut -c1-70) (its main twin $twin was reverted)"; twin=""; fi
		if [ -n "$twin" ]; then echo "skipped $(git log --oneline -1 "$c" | cut -c1-70) (its subject is on main: a mirrored commit)"; continue; fi
		if git cherry-pick "$c" > "$LOG_DIR/land_${L}_pick.log" 2>&1; then echo "picked $(git log --oneline -1 "$c" | cut -c1-70) -> $(git rev-parse --short HEAD)"; continue; fi
		conflicted=$(git diff --name-only --diff-filter=U)
		if [ -z "$conflicted" ] && grep -q "is now empty\|nothing to commit" "$LOG_DIR/land_${L}_pick.log"; then
			git cherry-pick --skip > /dev/null 2>&1 || { git cherry-pick --abort; echo "could not skip an empty pick; main untouched"; exit 1; }
			echo "skipped $(git log --oneline -1 "$c" | cut -c1-70) (already on main)"
			continue
		fi
		resolvable=1
		for f in $conflicted; do case " $UNION_FILES $SEARCH_INDEX $API_OUTPUTS " in *" $f "*) ;; *) resolvable=0 ;; esac; done
		if [ "$resolvable" = 1 ] && [ -n "$conflicted" ]; then
			ok=1
			regen_search=0
			regen_api=0
			for f in $conflicted; do
				case " $UNION_FILES " in
					*" $f "*) "$(jai_python)" "$REPO/Tools/Landing/resolve_union.py" "$f" > /dev/null || ok=0 ;;
					*)
						git checkout --theirs -- "$f" > /dev/null 2>&1 || ok=0
						case " $SEARCH_INDEX " in *" $f "*) regen_search=1 ;; *) regen_api=1 ;; esac
						;;
				esac
			done
			if [ "$regen_search" = 1 ]; then "$(jai_python)" "$REPO/Source/JaiScript/docs/site_tools/build_search_index.py" > /dev/null || ok=0; fi
			if [ "$regen_api" = 1 ]; then "$(jai_python)" "$REPO/Source/JaiScript/docs/site_tools/build_api_reference.py" > /dev/null || ok=0; fi
			for f in $conflicted; do git add "$f" || ok=0; done
			if [ "$ok" = 1 ] && GIT_EDITOR=true git cherry-pick --continue > /dev/null 2>&1; then
				echo "picked $(git log --oneline -1 "$c" | cut -c1-70) -> $(git rev-parse --short HEAD) (resolved $(echo $conflicted))"
				continue
			fi
		fi
		echo "CHERRY-PICK FAILED at $c"
		echo "$conflicted" | head -5
		git cherry-pick --abort
		# Picks that applied before the failure stay on main, so they are still checked below
		if [ "$(git rev-parse HEAD)" = "$(git rev-parse "$BEFORE")" ]; then exit 1; fi
		PARTIAL="stopped at $(git log --oneline -1 "$c" | cut -c1-60)"
		echo "checking the picks that applied ($PARTIAL)"
		break
	done
	echo "landed tip: $(git rev-parse --short HEAD)"
fi
HEADSHA=$(git rev-parse --short HEAD)

if [ "$TIER" = targeted ]; then
	if [ "$BEFORE" = "$HEADSHA" ]; then echo "$L LANDED (targeted) $HEADSHA (nothing new to check)"; exit 0; fi
	RANGE="$BEFORE..HEAD"
	step "targeted checks for $RANGE"
	if ! bash "$REPO/Source/JaiScript/tools/ci/targeted.sh" --build "$RANGE" | tee "$LOG_DIR/land_${L}_targeted.txt"; then
		echo "$L NOT GREEN: $(grep -aE 'TARGETED RED|BUILD FAILED' "$LOG_DIR/land_${L}_targeted.txt" | head -2 | tr '\n' ' ') (main holds the picks; fix forward)"
		exit 1
	fi
	grep -q "^green: tests-interp" "$LOG_DIR/land_${L}_targeted.txt" && grep -q "^green: tests-vm" "$LOG_DIR/land_${L}_targeted.txt" && touch "$LOG_DIR/debug_green_$HEADSHA"
	client_choice "$RANGE"
	if [ -n "$CLIENT_VERIFY_WHY" ]; then
		step "client gate"
		bash "$REPO/Tools/Landing/client_gate.sh" --no-loginshot "land_$L" | tee "$LOG_DIR/land_${L}_client.txt"
	elif [ -n "$CLIENT_BUILD_WHY" ]; then
		step "client build"
		bash "$REPO/Tools/Landing/client_gate.sh" --build-only "land_$L" | tee "$LOG_DIR/land_${L}_client.txt"
	else
		echo "CLIENT GATE DONE (not needed)" > "$LOG_DIR/land_${L}_client.txt"
	fi
	if grep -q "CLIENT GATE DONE" "$LOG_DIR/land_${L}_client.txt"; then
		echo "$L LANDED (targeted) $HEADSHA${PARTIAL:+ (PARTIAL: $PARTIAL)}"
		exit 0
	fi
	echo "$L NOT GREEN: $(grep -aE 'CLIENT GATE FAILED' "$LOG_DIR/land_${L}_client.txt" | head -1) (main holds the picks; fix forward)"
	exit 1
fi

step "battery at $HEADSHA"
[ -f "$LOG_DIR/debug_green_$HEADSHA" ] && export BATTERY_SKIP_DEBUG=1 && echo "debug suites: already green at $HEADSHA"
bash "$REPO/Source/JaiScript/tools/ci/battery.sh" --build | tee "$LOG_DIR/land_${L}_battery.txt"
grep -q "^BATTERY GREEN" "$LOG_DIR/land_${L}_battery.txt" && echo "MASTER GATES DONE"
step "client gate"
bash "$REPO/Tools/Landing/client_gate.sh" "land_$L" | tee "$LOG_DIR/land_${L}_client.txt"
if grep -q "^BATTERY GREEN" "$LOG_DIR/land_${L}_battery.txt" && grep -q "^CLIENT GATE DONE" "$LOG_DIR/land_${L}_client.txt"; then
	echo "$L BATTERY GREEN at $HEADSHA"
	exit 0
fi
echo "$L BATTERY RED at $HEADSHA: $(grep -aE 'BATTERY RED|BUILD FAILED|CLIENT GATE FAILED' "$LOG_DIR/land_${L}_battery.txt" "$LOG_DIR/land_${L}_client.txt" | head -3 | cut -d: -f2- | tr '\n' ' ')"
exit 1
