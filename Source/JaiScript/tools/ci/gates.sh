#!/usr/bin/env bash
# The CI gates, one per argument, so the workflow can give each its own step and a
# local run can reproduce any of them:
#
#   BIN=build/bin tools/ci/gates.sh tests-interp tests-vm fuzz gloom demoreel crawler gatorquest embed-quickstart lsp lsp-latency
#   tools/ci/gates.sh all
#
# BIN is the directory holding the built executables (default: build/bin under the
# repository root; a relative BIN is taken from the repository root too).
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
	run "$BIN/jaiscript_fuzz" --seed-start 1 --iterations 300 --max-depth 3 --timeout-ms 250 --isolate
}

gate_gloom() {
	cd "$ROOT/examples/gloom"
	run_logged "$BIN/jai_gloom" --smoke --ticks 300 --seed 666 --workers 0
	for backend in interpreter vm; do
		want "GLOOM $backend: expected frame hash $GLOOM_FRAME and STATE_HASH $GLOOM_STATE" \
			"^$backend +\\|.*\\| $GLOOM_FRAME +\\| $GLOOM_STATE\$"
	done
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

[ "$#" -gt 0 ] || { echo "usage: $0 <gate>... (${ALL[*]} all)" >&2; exit 2; }
[ "$1" = all ] && set -- "${ALL[@]}"

for gate in "$@"; do
	case "$gate" in
		tests-interp | tests-vm | fuzz | gloom | demoreel | crawler | gatorquest | embed-quickstart | lsp | lsp-latency) ;;
		*) echo "unknown gate: $gate (${ALL[*]} all)" >&2; exit 2 ;;
	esac
	echo "== gate: $gate"
	( "gate_${gate//-/_}" )
done
