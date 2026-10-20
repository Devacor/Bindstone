#!/usr/bin/env bash
# The CI gates, one per argument, so the workflow can give each its own step and a
# local run can reproduce any of them:
#
#   BIN=build/bin tools/ci/gates.sh tests-interp tests-vm fuzz gloom demoreel crawler gatorquest embed-quickstart lsp lsp-latency
#   tools/ci/gates.sh all
#
# BIN is the directory holding the built executables (default: build/bin under the
# repository root; a relative BIN is taken from the repository root too). FUZZ_ITERATIONS
# sets the fuzz run's program count (default 300) and FUZZ_CORPUS a directory it keeps divergent programs in. The milestone gates outside `all`
# (gloom-workers, gloom-long, timedemo, amalgamation) are the battery's (tools/ci/battery.sh).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN="${BIN:-build/bin}"
case "$BIN" in
	/* | [A-Za-z]:*) ;;
	*) BIN="$ROOT/$BIN" ;;
esac

# The reference hashes each example prints for its deterministic headless run.
GLOOM_FRAME=7fecc09815bb64e0
GLOOM_STATE=3580805725
DEMOREEL_STREAM=006d304a70772e5f
CRAWLER_STATE=3849293868
GATORQUEST_STATE=3218840367
GLOOM_LONG_FRAME=dfd969ed5a336dca
GLOOM_LONG_STATE=1503537018
TIMEDEMO_STATE=1746772898

# Deep-recursion tests outgrow Linux's 8 MB main-thread stack in Debug builds; the
# Windows binaries link a 128 MB stack, so match it where the shell allows.
ulimit -s 131072 2>/dev/null || true

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

run() {
	echo "+ $*"
	"$@"
}

# Runs the command with its output shown and a CR-stripped copy kept in $LOG.
run_logged() {
	echo "+ $*"
	"$@" 2>&1 | tr -d '\r' | tee "$LOG"
}

want() {
	grep -Eq -- "$2" "$LOG" || { echo "::error::$1"; exit 1; }
}

gate_tests_interp() { run "$BIN/jaiscript_tests"; }
gate_tests_vm() { run "$BIN/jaiscript_tests" --backend=vm; }

gate_fuzz() {
	local corpus=()
	[ -n "${FUZZ_CORPUS:-}" ] && corpus=(--corpus "$FUZZ_CORPUS")
	run_logged "$BIN/jaiscript_fuzz" --seed-start 1 --iterations "${FUZZ_ITERATIONS:-300}" --max-depth 3 --timeout-ms 250 --isolate "${corpus[@]+"${corpus[@]}"}"
	want "fuzz: expected no new divergence" "new divergences: ?0\\b"
}

gloom_hashes() {
	cd "$ROOT/examples/gloom"
	run_logged "$BIN/jai_gloom" --smoke --ticks "$1" --seed 666 --workers "$2"
	for backend in interpreter vm; do
		want "GLOOM $backend ($1 ticks, $2 workers): expected frame hash $3 and STATE_HASH $4" \
			"^$backend +\\|.*\\| $3 +\\| $4\$"
	done
}

gate_gloom() { gloom_hashes 300 0 "$GLOOM_FRAME" "$GLOOM_STATE"; }
gate_gloom_workers() { gloom_hashes 300 8 "$GLOOM_FRAME" "$GLOOM_STATE"; }
gate_gloom_long() { gloom_hashes 600 0 "$GLOOM_LONG_FRAME" "$GLOOM_LONG_STATE"; }

# JaiDOOM's timedemo, where the checkout carries the example (it is optional)
gate_timedemo() {
	local dir="$ROOT/examples/jaidoom/scripts"
	[ -f "$dir/p10_timedemo.jai" ] || { echo "timedemo: examples/jaidoom is not in this checkout, skipped"; return 0; }
	cd "$dir"
	for backend in vm interp; do
		rm -f ./*.jaibite
		run_logged "$BIN/jaiscript" --no-debug --no-pause --backend="$backend" p10_timedemo.jai -- 120x78
		want "timedemo $backend: expected STATE_HASH $TIMEDEMO_STATE" "STATE_HASH: $TIMEDEMO_STATE\\b"
	done
	rm -f ./*.jaibite
}

# source/jaiscript.cpp (the one-file build) includes every implementation file the library's CMake list names
gate_amalgamation() {
	local py
	py="$(python_cmd)"
	run "$py" - "$ROOT" <<'PYEOF'
import re, sys, pathlib
root = pathlib.Path(sys.argv[1])
listed = set(re.findall(r"implementation/[A-Za-z0-9_/]+\.cpp", (root / "source/CMakeLists.txt").read_text(encoding="utf-8", errors="replace")))
included = set(re.findall(r'#include\s+"(implementation/[A-Za-z0-9_/]+\.cpp)"', (root / "source/jaiscript.cpp").read_text(encoding="utf-8", errors="replace")))
separate = {"implementation/debug/connector.cpp", "implementation/debug/controller.cpp", "implementation/parallel_transform.cpp"}
missing = sorted(listed - included - separate)
if missing:
    print("::error::source/jaiscript.cpp must include every implementation file on the CMake list; missing: " + " ".join(missing))
    sys.exit(1)
print("amalgamation: every listed implementation file is included")
PYEOF
}

gate_demoreel() {
	run "$BIN/jai_demoreel" --smoke --expect "$DEMOREEL_STREAM"
}

gate_crawler() {
	for backend in vm interpreter; do
		run_logged "$BIN/jai_crawler" --smoke --seed 99 --turns 1500 --backend "$backend"
		want "crawler $backend: expected STATE_HASH $CRAWLER_STATE" "STATE_HASH: $CRAWLER_STATE\$"
	done
}

python_cmd() {
	local candidate
	for candidate in python3 python; do
		if "$candidate" -c "import sys; sys.exit(sys.version_info < (3, 8))" >/dev/null 2>&1; then
			echo "$candidate"
			return
		fi
	done
	echo "::error::the lsp gate needs Python 3.8 or later" >&2
	return 1
}

lsp_test() {
	local exe="$BIN/jaiscript_lsp" py
	[ -f "$exe.exe" ] && exe="$exe.exe"
	py="$(python_cmd)"
	run "$py" "$ROOT/tools/lsp/tests/lsp_test.py" --exe "$exe" "$@"
}

gate_lsp() {
	lsp_test --only fixtures
	lsp_test --only gatorquest
	lsp_test --only robustness
}

gate_lsp_latency() { lsp_test --only latency; }

gate_gatorquest() {
	cd "$ROOT/examples/gatorquest"
	for backend in vm interp; do
		run_logged "$BIN/jaiscript" --backend="$backend" scripts/gatorquest.jai -- smoke ticks 2000 bots 2
		want "Gator Quest $backend: expected STATE_HASH $GATORQUEST_STATE" "STATE_HASH: $GATORQUEST_STATE\$"
	done
}

gate_embed_quickstart() {
	local work
	work="$(mktemp -d)"
	cd "$work"
	for backend in vm interp; do
		local flags=()
		[ "$backend" = interp ] && flags=(--interp)
		run_logged "$BIN/embed_quickstart" "${flags[@]+"${flags[@]}"}"
		want "embed_quickstart $backend: expected every check to hold" "^all checks passed\$"
	done
	cd "$ROOT"
	rm -rf "$work"
}

ALL=(tests-interp tests-vm fuzz gloom demoreel crawler gatorquest embed-quickstart lsp lsp-latency)
MILESTONE=(gloom-workers gloom-long timedemo amalgamation)

[ "$#" -gt 0 ] || { echo "usage: $0 <gate>... (${ALL[*]} all; milestone: ${MILESTONE[*]})" >&2; exit 2; }
[ "$1" = all ] && set -- "${ALL[@]}"

for gate in "$@"; do
	case "$gate" in
		tests-interp | tests-vm | fuzz | gloom | demoreel | crawler | gatorquest | embed-quickstart | lsp | lsp-latency) ;;
		gloom-workers | gloom-long | timedemo | amalgamation) ;;
		*) echo "unknown gate: $gate (${ALL[*]} all; milestone: ${MILESTONE[*]})" >&2; exit 2 ;;
	esac
	echo "== gate: $gate"
	( "gate_${gate//-/_}" )
done
