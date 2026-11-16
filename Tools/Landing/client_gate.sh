#!/usr/bin/env bash
# The client gate: MutedVision and the Bindstone client built in Release from this checkout, then -verifyassets,
# -loginshot and the Workbench's smoke modes (-workbenchsmoke, -workbenchselftest, which drives a load, save, drop and
# undo, and -workbenchgizmoshot, which loads map.scene). A failed build or check prints CLIENT GATE FAILED (never DONE).
#
#   Tools/Landing/client_gate.sh [--build-only] [--no-loginshot] [--no-workbench] [--landing] [label]
#
# On a committed tree (no tracked change anywhere, no untracked file under Source, VSProjects, External or Assets) each step
# that passes writes a gate stamp (Source/JaiScript/tools/ci/ci_lib.sh): client-build, client-verifyassets, client-loginshot,
# client-workbenchsmoke, client-workbenchselftest, client-workbenchgizmoshot.
# --landing runs nothing when every step it would run has a matching stamp for HEAD's tree; otherwise it waits while someone
# holds the timing lock, then runs every step, since a skipped build leaves no client to check.
#
# LAND_LOG_DIR holds the logs (land.sh passes its run's folder; alone, the default is a folder of the run's own under
# bindstone-land in the system temp directory); CLIENT_BUILD_JOBS caps msbuild's
# parallel projects (default 4); VSDEVCMD overrides the Visual Studio vswhere finds.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
. "$REPO/Source/JaiScript/tools/ci/ci_lib.sh"

BUILD_ONLY=0
LOGINSHOT=1
WORKBENCH=1
LANDING=0
LABEL=client
for arg in "$@"; do
	case "$arg" in
		--build-only) BUILD_ONLY=1 ;;
		--no-loginshot) LOGINSHOT=0 ;;
		--no-workbench) WORKBENCH=0 ;;
		--landing) LANDING=1 ;;
		*) LABEL="$arg" ;;
	esac
done
LOG_DIR="${LAND_LOG_DIR:-$(lockq_temp_dir)/bindstone-land/$(date +%Y%m%d-%H%M%S)-$$}"
mkdir -p "$LOG_DIR"
JAI_CI_LOG_DIR="$LOG_DIR"
LOG="$LOG_DIR/${LABEL}_bindstone.log"
: > "$LOG"
STEPS=(client-build)
[ "$BUILD_ONLY" = 1 ] || STEPS+=(client-verifyassets)
[ "$BUILD_ONLY" = 1 ] || [ "$LOGINSHOT" = 0 ] || STEPS+=(client-loginshot)
[ "$BUILD_ONLY" = 1 ] || [ "$WORKBENCH" = 0 ] || STEPS+=(client-workbenchsmoke client-workbenchselftest client-workbenchgizmoshot)
STAMP_SCOPE=(':(top)Source' ':(top)VSProjects' ':(top)External' ':(top)Assets' ':(top,glob)Directory.Build.*' ':(top,glob)*.sln')

vsdev="$(jai_vsdevcmd)" || { echo "no Visual Studio found: set VSDEVCMD"; echo "CLIENT GATE FAILED (the client did not build)"; exit 1; }

# client_key <step>: a step's stamp key; MSBuild reads environment variables as properties, cl and link read CL and LINK
client_key() {
	local install vswhere version tools args=""
	install="$(cd "$(dirname "$vsdev")/../.." && pwd)"
	vswhere="$(printenv 'ProgramFiles(x86)' 2>/dev/null)/Microsoft Visual Studio/Installer/vswhere.exe"
	version="$("$vswhere" -path "$(cygpath -w "$install")" -property catalog_productSemanticVersion 2>/dev/null | tr -d '\r' | head -n 1)"
	tools="$(tr -d '\r\n' < "$install/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt" 2>/dev/null)"
	[ -n "$version" ] && [ -n "$tools" ] || return 1
	case "$1" in
		client-verifyassets) args=" args=-verifyassets --no-script-debug" ;;
		client-loginshot) args=" args=-loginshot 60 --no-script-debug" ;;
		client-workbenchsmoke) args=" args=-workbenchsmoke --no-script-debug" ;;
		client-workbenchselftest) args=" args=-workbenchselftest --no-script-debug" ;;
		client-workbenchgizmoshot) args=" args=-workbenchgizmoshot --no-script-debug" ;;
	esac
	printf 'config gate=%s configuration=Release platform=x64 fastiteration=%s%s%s\ntoolchain vs=%s msvc=%s\n' "$1" "${BindstoneFastIteration:-default}" \
		"$(env | grep -E '^(CL|_CL_|LINK|_LINK_)=' | LC_ALL=C sort | sed 's/^/ env:/' | tr -d '\n')" "$args" "$version" "$tools"
}

client_stamp() {
	local key
	key="$(client_key "$1")" && jai_stamp_write "$1" "$key"
}

if [ "$LANDING" = 1 ]; then
	tree="$(git -C "$REPO" rev-parse 'HEAD^{tree}')"
	trusted=""
	for step in "${STEPS[@]}"; do
		key="$(client_key "$step")" && line="$(jai_stamp_trusted "$tree" "$step" "$key")" || { trusted=""; break; }
		trusted="$trusted$line"$'\n'
	done
	if [ -n "$trusted" ]; then
		printf '%s' "$trusted"
		echo "CLIENT GATE DONE (every step trusted)"
		exit 0
	fi
	lockq_wait
fi
jai_stamp_begin "${STAMP_SCOPE[@]}"

echo "[$(date +%H:%M)] step: client build"
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
client_stamp client-build
[ "$BUILD_ONLY" = 1 ] && { echo "CLIENT GATE DONE (build only)"; exit 0; }

cd "$REPO" || exit 1
jai_watchdog
jai_watched ./Builds/Windows/x64/Release/Bindstone.exe -verifyassets --no-script-debug > "$LOG_DIR/${LABEL}_verify.txt" 2>&1
rc=$?
echo "verifyassets exit=$rc $(grep -aE '\[verify\]' "$LOG_DIR/${LABEL}_verify.txt" | tail -n 1 | cut -c1-140)"
[ "$rc" = 0 ] || { echo "CLIENT GATE FAILED (verifyassets)"; exit 1; }
client_stamp client-verifyassets
if [ "$LOGINSHOT" = 1 ]; then
	jai_watched ./Builds/Windows/x64/Release/Bindstone.exe -loginshot 60 --no-script-debug > "$LOG_DIR/${LABEL}_loginshot.txt" 2>&1
	rc=$?
	echo "loginshot exit=$rc"
	[ "$rc" = 0 ] || { echo "CLIENT GATE FAILED (loginshot)"; exit 1; }
	client_stamp client-loginshot
fi
if [ "$WORKBENCH" = 1 ]; then
	for mode in workbenchsmoke workbenchselftest workbenchgizmoshot; do
		jai_watched ./Builds/Windows/x64/Release/Bindstone.exe "-$mode" --no-script-debug > "$LOG_DIR/${LABEL}_$mode.txt" 2>&1
		rc=$?
		echo "$mode exit=$rc $(grep -a 'Workbench smoke completed' "$LOG_DIR/${LABEL}_$mode.txt" | tail -n 1 | cut -c1-140)"
		if [ "$rc" != 0 ]; then
			grep -aE '\[workbench\] error|Script callback failed' "$LOG_DIR/${LABEL}_$mode.txt" | head -5 | cut -c1-200
			echo "CLIENT GATE FAILED ($mode)"
			exit 1
		fi
		client_stamp "client-$mode"
	done
fi
echo "CLIENT GATE DONE"
