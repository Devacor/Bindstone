#!/usr/bin/env bash
# The milestone tier: every gate, at milestone depth, on both tiers of tools/ci/targeted.sh. The Debug suites on the
# interpreter, the VM and the VM reference, then on the Release build the same suites, a 3000-program run of each
# differential (BATTERY_FUZZ_ITERATIONS: the interpreter against the VM, the VM against the VM reference), every example's
# hashes on both backends (GLOOM with and without workers and at 600 ticks, the demoreel, the crawler, Gator Quest,
# JaiDOOM's timedemo where the checkout has it), the embedding quickstart, the language server and the one-file build's
# include list.
#
#   tools/ci/battery.sh [--build]
#
# --build configures and builds every target first (tools/ci/ci_lib.sh names the build directories and the toolchain);
# BATTERY_SKIP_DEBUG=1 skips the Debug suites when they already passed at this commit.
# The last line is the verdict: BATTERY GREEN or BATTERY RED. The per-change tier is tools/ci/targeted.sh.
set -uo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/ci_lib.sh"

if [ "${1:-}" = "--build" ]; then
	jai_build debug jaiscript_tests || { echo "BATTERY RED: the Debug build failed"; exit 1; }
	jai_build release jaiscript_tests jaiscript_runner jai_gloom jai_crawler jai_demoreel jaiscript_fuzz jaiscript_lsp embed_quickstart ||
		{ echo "BATTERY RED: the Release build failed"; exit 1; }
fi

FAILED=""
if [ "${BATTERY_SKIP_DEBUG:-0}" = 1 ]; then
	echo "skipped: the Debug suites (already green at this commit)"
else
	jai_debug_suites interp vm vm-reference || FAILED="$FAILED debug-suites"
fi
export HASH_BACKENDS="vm interp"
for gate in tests-interp tests-vm tests-vm-reference fuzz fuzz-vm-reference gloom gloom-workers gloom-long demoreel crawler gatorquest timedemo embed-quickstart lsp lsp-latency amalgamation; do
	FUZZ_ITERATIONS="${BATTERY_FUZZ_ITERATIONS:-3000}" jai_gates release "$gate" || FAILED="$FAILED $gate"
done

if [ -z "$FAILED" ]; then echo "BATTERY GREEN"; else echo "BATTERY RED:$FAILED"; exit 1; fi
