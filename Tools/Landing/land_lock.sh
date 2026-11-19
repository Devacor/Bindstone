#!/usr/bin/env bash
# The landing lock, for land.sh and build_main.sh: source it with REPO set to the checkout. land_lock_take <who> waits its
# turn and releases on exit; land_lock_release frees it early.
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
