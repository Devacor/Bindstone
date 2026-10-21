#!/usr/bin/env bash
# The targeted tier: the checks a change needs, picked from the paths it touches.
#
#   tools/ci/targeted.sh [--plan] [--build] <rev-range>       # e.g. HEAD~1..HEAD, master..my-branch
#   tools/ci/targeted.sh [--plan] [--build] --files <path>...  # paths relative to the package root
#
# --plan prints the checks and why each was picked, and runs nothing. --build configures and builds what the checks run
# (tools/ci/ci_lib.sh names the build directories and the toolchain). The last line is the verdict: TARGETED GREEN or
# TARGETED RED. The milestone tier is tools/ci/battery.sh.
set -uo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/ci_lib.sh"

PLAN=0
BUILD=0
RANGE=""
FILES=()
while [ "$#" -gt 0 ]; do
	case "$1" in
		--plan) PLAN=1 ;;
		--build) BUILD=1 ;;
		--files) shift; FILES=("$@"); break ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) RANGE="$1" ;;
	esac
	shift
done
if [ -n "$RANGE" ]; then
	mapfile -t FILES < <(git -C "$JAI_ROOT" diff --name-only --relative "$RANGE" -- . | tr -d '\r')
fi
[ -n "$RANGE" ] || [ "${#FILES[@]}" -gt 0 ] || { echo "usage: targeted.sh [--plan] [--build] <rev-range> | --files <path>..." >&2; exit 2; }

CHECKS=""
REASONS=""
pick() {
	case " $CHECKS " in *" $1 "*) ;; *) CHECKS="$CHECKS $1" ;; esac
	REASONS="$REASONS"$'\n'"$1|$2"
}

hashes() {
	local game
	for game in gloom demoreel crawler gatorquest; do pick "$game" "$1"; done
}

classify() {
	local p="$1"
	case "$p" in
		docs/site/* | docs/site_tools/*) pick verify-site "the documentation site ($p)" ;;
		*.md | internal/* | docs/* | LICENSE* | .gitignore | .gitattributes) NOTES="$NOTES $p" ;;
		.github/*) pick ci-scripts "the CI workflow ($p)" ;;
		tools/ci/*) pick ci-scripts "the CI scripts ($p)" ;;
		tools/lsp/*) pick lsp "the language server ($p)" ;;
		tools/vscode-jaiscript/*) NOTES="$NOTES $p" ;;
		tools/jaiscript/*) pick gatorquest "the runner ($p)"; pick verify-site "the runner runs the site's samples ($p)" ;;
		examples/gloom/*) pick gloom "the GLOOM example ($p)" ;;
		examples/demoreel/*) pick demoreel "the demoreel ($p)" ;;
		examples/crawler/*) pick crawler "the crawler ($p)" ;;
		examples/gatorquest/*) pick gatorquest "Gator Quest ($p)"; pick lsp "the language server checks Gator Quest ($p)" ;;
		examples/embed_quickstart/*) pick embed-quickstart "the embedding quickstart ($p)" ;;
		examples/jaidoom/*) pick timedemo "JaiDOOM ($p)" ;;
		examples/*) NOTES="$NOTES $p" ;;
		source/tests/fuzz/*) pick fuzz "the fuzzer ($p)"; pick debug-suites "the fuzzer's suite ($p)" ;;
		source/tests/*) pick debug-suites "a test ($p)" ;;
		include/jaiscript/vm/* | source/implementation/vm/*)
			pick debug-suites "the VM or its compiler ($p)"; hashes "the VM or its compiler ($p)"; pick fuzz "the VM against its oracle ($p)" ;;
		source/implementation/interpreter.cpp | source/implementation/interpreter_dispatch.cpp | include/jaiscript/detail/interpreter.hpp)
			pick debug-suites "the interpreter ($p)"; hashes "the interpreter ($p)"; pick fuzz "the interpreter against the VM ($p)" ;;
		include/jaiscript/detail/* | source/implementation/parser.cpp | source/implementation/lexer.cpp | source/implementation/type_checker.cpp)
			pick debug-suites "a kernel both backends share ($p)"; hashes "a kernel both backends share ($p)"; pick fuzz "a kernel both backends share ($p)" ;;
		include/jaiscript/properties/* | include/jaiscript/properties.hpp | include/jaiscript/serialization/* | source/implementation/serialization/* | \
		source/implementation/properties.cpp | source/implementation/schema_binding.cpp)
			pick debug-suites "the property system or the archives ($p)"; HOST_ARCHIVES=1 ;;
		include/* | source/implementation/*) pick debug-suites "the library ($p)" ;;
		CMakeLists.txt | */CMakeLists.txt | cmake/* | source/jaiscript.cpp | CMakePresets.json)
			pick debug-suites "the build ($p)"; pick amalgamation "the build's file lists ($p)" ;;
		*) pick debug-suites "a path no rule names, so the suites ($p)" ;;
	esac
}

NOTES=""
HOST_ARCHIVES=0
for f in "${FILES[@]}"; do classify "$f"; done

echo "targeted: ${#FILES[@]} path(s)${RANGE:+ in $RANGE}"
if [ -z "$CHECKS" ]; then
	echo "chose: nothing to run (notes and prose only:${NOTES:- none})"
	echo "TARGETED GREEN (nothing to check)"
	exit 0
fi
for check in $CHECKS; do
	echo "chose: $check, because $(printf '%s\n' "$REASONS" | grep "^$check|" | head -n 3 | cut -d'|' -f2- | paste -sd ';' - | sed 's/;/; /g')"
done
[ -n "$NOTES" ] && echo "no check for:$NOTES"
[ "$HOST_ARCHIVES" = 1 ] && echo "host note: the property system or the archives changed; a host runs its asset round-trip check too"
[ "$PLAN" = 1 ] && exit 0

if [ "$BUILD" = 1 ]; then
	debug_targets=""
	release_targets=""
	for check in $CHECKS; do
		case "$check" in
			debug-suites) debug_targets="$debug_targets jaiscript_tests" ;;
			fuzz) release_targets="$release_targets jaiscript_fuzz" ;;
			gloom) release_targets="$release_targets jai_gloom" ;;
			demoreel) release_targets="$release_targets jai_demoreel" ;;
			crawler) release_targets="$release_targets jai_crawler" ;;
			gatorquest | timedemo) release_targets="$release_targets jaiscript_runner" ;;
			verify-site) release_targets="$release_targets jaiscript_runner jaiscript_tests" ;;
			lsp) release_targets="$release_targets jaiscript_lsp" ;;
			embed-quickstart) release_targets="$release_targets embed_quickstart" ;;
		esac
	done
	# shellcheck disable=SC2086
	[ -z "$debug_targets" ] || jai_build debug $(echo $debug_targets | tr ' ' '\n' | sort -u) || { echo "TARGETED RED: the Debug build failed"; exit 1; }
	# shellcheck disable=SC2086
	[ -z "$release_targets" ] || jai_build release $(echo $release_targets | tr ' ' '\n' | sort -u) || { echo "TARGETED RED: the Release build failed"; exit 1; }
fi

FAILED=""
for check in $CHECKS; do
	case "$check" in
		debug-suites) jai_debug_suites || FAILED="$FAILED debug-suites" ;;
		verify-site)
			runner="$JAI_RELEASE_DIR/bin/jaiscript"
			if (cd "$JAI_ROOT" && "$(jai_python)" docs/site_tools/verify_site.py --runner "$runner") > "$JAI_CI_LOG_DIR/verify_site.log" 2>&1; then
				echo "green: verify_site"
			else
				echo "red: verify_site (log: $JAI_CI_LOG_DIR/verify_site.log)"
				grep -aE "^FAIL|failed:" "$JAI_CI_LOG_DIR/verify_site.log" | head -8 | cut -c1-220
				FAILED="$FAILED verify-site"
			fi
			;;
		ci-scripts)
			bad=0
			for script in "$JAI_ROOT"/tools/ci/*.sh; do bash -n "$script" || bad=1; done
			if [ "$bad" = 0 ]; then echo "green: ci-scripts (every tools/ci script parses)"; else echo "red: ci-scripts"; FAILED="$FAILED ci-scripts"; fi
			;;
		*) jai_gates release "$check" || FAILED="$FAILED $check" ;;
	esac
done

if [ -z "$FAILED" ]; then echo "TARGETED GREEN:$CHECKS"; else echo "TARGETED RED:$FAILED"; exit 1; fi
