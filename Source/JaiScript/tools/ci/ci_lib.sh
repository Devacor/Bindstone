#!/usr/bin/env bash
# Shared by targeted.sh and battery.sh: the package root, the build directories and the build step.
#
#   JAI_DEBUG_DIR, JAI_RELEASE_DIR   the Debug and Release build directories (default build-debug and build; relative to the
#                                    package root unless absolute). Each binary lives in <dir>/bin.
#   JAI_CMAKE_ARGS_DEBUG/_RELEASE    extra arguments for a first configure
#   JAI_BUILD_JOBS                   parallel build jobs (default 4)
#   JAI_CI_LOG_DIR                   where build and check logs go (default jaiscript-ci in the system temp directory)
#   VSDEVCMD                         on Windows, the VsDevCmd.bat a build runs under when cl is not on PATH (default: the
#                                    latest Visual Studio vswhere reports)

JAI_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$JAI_ROOT/tools/ci/timing_lock.sh"

jai_abs() {
	case "$1" in
		/* | [A-Za-z]:*) echo "$1" ;;
		*) echo "$JAI_ROOT/$1" ;;
	esac
}

JAI_DEBUG_DIR="$(jai_abs "${JAI_DEBUG_DIR:-build-debug}")"
JAI_RELEASE_DIR="$(jai_abs "${JAI_RELEASE_DIR:-build}")"
JAI_CI_LOG_DIR="${JAI_CI_LOG_DIR:-$(lockq_temp_dir)/jaiscript-ci}"
mkdir -p "$JAI_CI_LOG_DIR"

jai_python() {
	local candidate
	for candidate in python3 python; do
		"$candidate" -c "import sys; sys.exit(sys.version_info < (3, 8))" >/dev/null 2>&1 && { echo "$candidate"; return; }
	done
	echo python
}

jai_dir_of() { [ "$1" = debug ] && echo "$JAI_DEBUG_DIR" || echo "$JAI_RELEASE_DIR"; }

jai_on_windows() { case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) return 0 ;; *) return 1 ;; esac; }

jai_vsdevcmd() {
	[ -n "${VSDEVCMD:-}" ] && { echo "$VSDEVCMD"; return; }
	local vswhere install
	vswhere="$(printenv 'ProgramFiles(x86)' 2>/dev/null)/Microsoft Visual Studio/Installer/vswhere.exe"
	[ -f "$vswhere" ] || return 1
	install="$("$vswhere" -latest -products '*' -property installationPath 2>/dev/null | tr -d '\r' | head -n 1)"
	[ -n "$install" ] && [ -f "$install/Common7/Tools/VsDevCmd.bat" ] && echo "$install/Common7/Tools/VsDevCmd.bat"
}

# cmake with the compiler's environment: as is where cl (or any other toolchain) is already on PATH, else on Windows under
# VsDevCmd. Arguments are passed through; output goes to the log named first.
jai_cmake() {
	local log="$1"
	shift
	if ! jai_on_windows || command -v cl >/dev/null 2>&1; then
		cmake "$@" >> "$log" 2>&1
		return
	fi
	local vsdev bat arg line=""
	vsdev="$(jai_vsdevcmd)" || { echo "no Visual Studio found: set VSDEVCMD to its VsDevCmd.bat" >> "$log"; return 1; }
	for arg in "$@"; do
		case "$arg" in
			/*) arg="$(cygpath -w "$arg")" ;;
		esac
		line="$line \"$arg\""
	done
	bat="$(mktemp --suffix=.bat)"
	printf '@call "%s" -arch=x64 >nul 2>&1\r\n@cmake%s >> "%s" 2>&1\r\n@exit /b %%errorlevel%%\r\n' "$(cygpath -w "$vsdev")" "$line" "$(cygpath -w "$log")" > "$bat"
	cmd //c "$(cygpath -w "$bat")"
	local rc=$?
	rm -f "$bat"
	return $rc
}

# jai_build <debug|release> <target>...: configure (new test files are found at configure time), then build the targets
jai_build() {
	local cfg="$1" dir log type
	shift
	dir="$(jai_dir_of "$cfg")"
	log="$JAI_CI_LOG_DIR/build_${cfg}.log"
	: > "$log"
	if [ -f "$dir/CMakeCache.txt" ]; then
		jai_cmake "$log" -S "$JAI_ROOT" -B "$dir" || { jai_build_failed "$cfg" "$log"; return 1; }
	else
		type=$([ "$cfg" = debug ] && echo Debug || echo Release)
		local extra=()
		if [ "$cfg" = debug ]; then read -r -a extra <<< "${JAI_CMAKE_ARGS_DEBUG:-}"; else read -r -a extra <<< "${JAI_CMAKE_ARGS_RELEASE:-}"; fi
		jai_cmake "$log" -S "$JAI_ROOT" -B "$dir" -G Ninja "-DCMAKE_BUILD_TYPE=$type" "${extra[@]+"${extra[@]}"}" || { jai_build_failed "$cfg" "$log"; return 1; }
	fi
	local targets=() t
	for t in "$@"; do targets+=(--target "$t"); done
	jai_cmake "$log" --build "$dir" "${targets[@]}" -j "${JAI_BUILD_JOBS:-4}" || { jai_build_failed "$cfg" "$log"; return 1; }
	echo "built $cfg: $*"
}

jai_build_failed() {
	echo "BUILD FAILED: $1 (log: $2)"
	grep -E "error C[0-9]+|error LNK|error:|ninja: error|CMake Error" "$2" | head -6 | cut -c1-220
}

# jai_gates <debug|release> <gate>...: tools/ci/gates.sh against that build, its output in a log; prints one line per gate
jai_gates() {
	local cfg="$1" log
	shift
	log="$JAI_CI_LOG_DIR/gates_${cfg}_$(echo "$*" | tr ' ' '_').log"
	if BIN="$(jai_dir_of "$cfg")/bin" bash "$JAI_ROOT/tools/ci/gates.sh" "$@" > "$log" 2>&1; then
		echo "green: $* ($cfg)"
		return 0
	fi
	echo "red: $* ($cfg; log: $log)"
	grep -aE "::error::|^  x |Failed tests|FAIL" "$log" | head -8 | cut -c1-220
	return 1
}

# The Debug suites on both backends, side by side, each with its own temp directory; a red side reruns alone
jai_debug_suites() {
	local be pids=() rc=0
	for be in interp vm; do
		mkdir -p "$JAI_CI_LOG_DIR/tmp_$be"
		TEMP="$JAI_CI_LOG_DIR/tmp_$be" TMP="$JAI_CI_LOG_DIR/tmp_$be" TMPDIR="$JAI_CI_LOG_DIR/tmp_$be" jai_gates debug "tests-$be" > "$JAI_CI_LOG_DIR/suite_$be.line" &
		pids+=($!)
	done
	for be in 0 1; do wait "${pids[$be]}" || true; done
	for be in interp vm; do
		if grep -q "^green" "$JAI_CI_LOG_DIR/suite_$be.line"; then
			cat "$JAI_CI_LOG_DIR/suite_$be.line"
		else
			echo "rerunning the $be suites alone (the side-by-side run was red)"
			jai_gates debug "tests-$be" || rc=1
		fi
	done
	return $rc
}
