#!/usr/bin/env bash
# The client gate: MutedVision and the Bindstone client built in Release from this checkout, then -verifyassets and
# -loginshot. A failed build or check prints CLIENT GATE FAILED (never DONE).
#
#   Tools/Landing/client_gate.sh [--build-only] [--no-loginshot] [label]
#
# LAND_LOG_DIR holds the logs (default bindstone-land in the system temp directory); CLIENT_BUILD_JOBS caps msbuild's
# parallel projects (default 4); VSDEVCMD overrides the Visual Studio vswhere finds.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$REPO/Source/JaiScript/tools/ci/ci_lib.sh"

BUILD_ONLY=0
LOGINSHOT=1
LABEL=client
for arg in "$@"; do
	case "$arg" in
		--build-only) BUILD_ONLY=1 ;;
		--no-loginshot) LOGINSHOT=0 ;;
		*) LABEL="$arg" ;;
	esac
done
LOG_DIR="${LAND_LOG_DIR:-$(lockq_temp_dir)/bindstone-land}"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/${LABEL}_bindstone.log"
: > "$LOG"

echo "[$(date +%H:%M)] step: client build"
vsdev="$(jai_vsdevcmd)" || { echo "no Visual Studio found: set VSDEVCMD"; echo "CLIENT GATE FAILED (the client did not build)"; exit 1; }
bat="$(mktemp --suffix=.bat)"
printf '@call "%s" -arch=x64 >nul 2>&1\r\n@cd /d "%s"\r\n@msbuild "VSProjects\\BindstoneClient\\BindstoneClient_Windows\\BindstoneClient_Windows.vcxproj" /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=%s\\\\" /m:%s /nodeReuse:false /v:m >> "%s" 2>&1\r\n@exit /b %%errorlevel%%\r\n' \
	"$(cygpath -w "$vsdev")" "$(cygpath -w "$REPO")" "$(cygpath -w "$REPO")" "${CLIENT_BUILD_JOBS:-4}" "$(cygpath -w "$LOG")" > "$bat"
cmd //c "$(cygpath -w "$bat")"
rc=$?
rm -f "$bat"
errs=$(grep -c 'error C\|error LNK' "$LOG")
echo "bindstone build: exit=$rc errors=$errs"
grep -E "error C[0-9]+|error LNK" "$LOG" | head -5 | cut -c1-200
if [ "$rc" != 0 ] || [ "$errs" != 0 ]; then echo "CLIENT GATE FAILED (the client did not build)"; exit 1; fi
[ "$BUILD_ONLY" = 1 ] && { echo "CLIENT GATE DONE (build only)"; exit 0; }

cd "$REPO" || exit 1
./Builds/Windows/x64/Release/Bindstone.exe -verifyassets --no-script-debug > "$LOG_DIR/${LABEL}_verify.txt" 2>&1
rc=$?
echo "verifyassets exit=$rc $(grep -aE '\[verify\]' "$LOG_DIR/${LABEL}_verify.txt" | tail -n 1 | cut -c1-140)"
[ "$rc" = 0 ] || { echo "CLIENT GATE FAILED (verifyassets)"; exit 1; }
if [ "$LOGINSHOT" = 1 ]; then
	./Builds/Windows/x64/Release/Bindstone.exe -loginshot 60 --no-script-debug > "$LOG_DIR/${LABEL}_loginshot.txt" 2>&1
	rc=$?
	echo "loginshot exit=$rc"
	[ "$rc" = 0 ] || { echo "CLIENT GATE FAILED (loginshot)"; exit 1; }
fi
echo "CLIENT GATE DONE"
