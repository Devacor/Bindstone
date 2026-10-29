#!/usr/bin/env bash
# Report every git worktree with a verdict; with --apply, remove the ones that hold nothing master lacks.
#
#   Tools/Landing/prune_worktrees.sh [--repo <path>]           # dry run: one row per worktree, nothing changes
#   Tools/Landing/prune_worktrees.sh --apply [--repo <path>]   # remove the REMOVABLE rows, prune entries whose directory is gone
#   Tools/Landing/prune_worktrees.sh --selftest                # one throwaway worktree per verdict, both modes checked
#
# REMOVABLE only when all hold: not the main checkout, not on master, not locked, no commit missing from master by patch
# identity (git cherry, plus any merge commit master lacks, which git cherry never lists), git status --porcelain empty
# (ignored build directories do not count) and no running process image inside it. Every other worktree is KEEP with its
# reasons; one whose directory is gone is PRUNABLE (git worktree prune drops the entry, the branch stays). --apply
# re-checks a row right before removing it and the branch right before deleting it. *_scratch folders and unregistered
# worktree directories beside the worktrees are listed, never touched.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
MASTER=master

# mixed_path <path>: the D:/x form git prints, so bash paths and git paths compare as strings
mixed_path() {
	if command -v cygpath > /dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi
}

selftest() {
	local root fails=0 out wt n
	root="$(mixed_path "$(mktemp -d)")"
	wt="$root/wt"
	mkdir -p "$wt"
	git init -q -b "$MASTER" "$root/main" || return 1
	cd "$root/main" || return 1
	git config user.email selftest@example.invalid
	git config user.name selftest
	git config core.autocrlf false
	printf 'build/\n' > .gitignore
	printf 'one\n' > f.txt
	git add . && git commit -qm base
	printf 'two\n' >> f.txt && git commit -qam second

	git worktree add -q -b picked "$wt/picked" "$MASTER~1"
	(cd "$wt/picked" && printf 'picked\n' > p.txt && git add p.txt && git commit -qm "pick me")
	git cherry-pick picked > /dev/null
	git worktree add -q -b behind "$wt/behind" "$MASTER~1"
	git worktree add -q --detach "$wt/detached" "$MASTER~1"
	git worktree add -q -b ignored "$wt/ignored" "$MASTER"
	mkdir -p "$wt/ignored/build" && printf 'obj\n' > "$wt/ignored/build/a.obj"
	git worktree add -q -b unlanded "$wt/unlanded" "$MASTER"
	(cd "$wt/unlanded" && printf 'new\n' > u.txt && git add u.txt && git commit -qm "not landed")
	git worktree add -q -b resolved "$wt/resolved" "$MASTER"
	(cd "$wt/resolved" && printf 'mine\n' > r.txt && git add r.txt && git commit -qm "landed with a resolution")
	printf 'resolved differently\n' > r.txt && git add r.txt && git commit -qm "landed with a resolution"
	git worktree add -q -b merged "$wt/merged" "$MASTER"
	(cd "$wt/merged" && git merge -q --no-ff -m "evil merge" picked > /dev/null)
	git worktree add -q --detach "$wt/detached_unlanded" "$MASTER"
	(cd "$wt/detached_unlanded" && printf 'd\n' > d.txt && git add d.txt && git commit -qm "detached work")
	git worktree add -q -b dirty "$wt/dirty" "$MASTER"
	printf 'edit\n' >> "$wt/dirty/f.txt"
	git worktree add -q -b untracked "$wt/untracked" "$MASTER"
	printf 'loose\n' > "$wt/untracked/loose.txt"
	git worktree add -q -b locked "$wt/locked" "$MASTER"
	git worktree lock --reason "agent busy" "$wt/locked"
	git worktree add -q -b missing "$wt/missing" "$MASTER"
	rm -rf "$wt/missing"
	mkdir -p "$wt/picked_scratch"
	mkdir -p "$wt/orphan" && printf 'gitdir: %s/main/.git/worktrees/orphan\n' "$root" > "$wt/orphan/.git"
	local busy_pid=""
	if ps -W > /dev/null 2>&1; then
		git worktree add -q -b busy "$wt/busy" "$MASTER"
		mkdir -p "$wt/busy/build" && cp "$(command -v sleep)" "$wt/busy/build/busy_sleep.exe"
		"$wt/busy/build/busy_sleep.exe" 120 &
		busy_pid=$!
		sleep 1
	fi

	# expect <output> <verdict> <worktree name> [<reason text>]
	expect() {
		local row
		row=$(printf '%s\n' "$1" | grep -E "^$2 +$root/(wt/)?$3( |$)")
		if [ -n "$row" ] && { [ -z "${4:-}" ] || printf '%s\n' "$row" | grep -qF "$4"; }; then echo "ok:   $2 $3${4:+ ($4)}"
		else echo "FAIL: expected $2 $3${4:+ ($4)}"; fails=$((fails + 1)); fi
	}
	exists() { if [ -e "$2" ]; then [ "$1" = yes ]; else [ "$1" = no ]; fi && echo "ok:   $3" || { echo "FAIL: $3"; fails=$((fails + 1)); }; }
	branch_is() { if git show-ref -q --verify "refs/heads/$2"; then [ "$1" = yes ]; else [ "$1" = no ]; fi && echo "ok:   $3" || { echo "FAIL: $3"; fails=$((fails + 1)); }; }

	n=$(git worktree list --porcelain | grep -c '^worktree ')
	echo "--- dry run"
	out=$(bash "$SELF" --repo "$root/main")
	echo "$out"
	echo "---"
	expect "$out" KEEP main "main checkout"
	expect "$out" REMOVABLE picked
	expect "$out" REMOVABLE behind
	expect "$out" REMOVABLE detached
	expect "$out" REMOVABLE ignored
	expect "$out" KEEP unlanded "1 unlanded"
	expect "$out" KEEP resolved "1 with a subject on master"
	expect "$out" KEEP merged "1 merge commit"
	expect "$out" KEEP detached_unlanded "1 unlanded"
	expect "$out" KEEP dirty "1 tracked"
	expect "$out" KEEP untracked "1 untracked"
	expect "$out" KEEP locked "locked: agent busy"
	expect "$out" PRUNABLE missing
	expect "$out" SCRATCH picked_scratch
	expect "$out" ORPHAN orphan
	[ -n "$busy_pid" ] && expect "$out" KEEP busy "in use: "
	[ "$(git worktree list --porcelain | grep -c '^worktree ')" = "$n" ] && echo "ok:   dry run changed nothing" || { echo "FAIL: dry run changed the worktree list"; fails=$((fails + 1)); }

	echo "--- apply"
	bash "$SELF" --apply --repo "$root/main"
	echo "---"
	local name
	for name in picked behind detached ignored; do exists no "$wt/$name" "$name removed"; done
	for name in picked behind ignored; do branch_is no "$name" "branch $name deleted"; done
	for name in unlanded resolved merged detached_unlanded dirty untracked locked; do exists yes "$wt/$name" "$name kept"; done
	for name in unlanded resolved merged dirty untracked locked missing; do branch_is yes "$name" "branch $name kept"; done
	git worktree list --porcelain | grep -q "^worktree $wt/missing$" && { echo "FAIL: missing entry not pruned"; fails=$((fails + 1)); } || echo "ok:   missing entry pruned"
	exists yes "$wt/picked_scratch" "scratch kept"
	exists yes "$wt/orphan/.git" "orphan kept"
	if [ -n "$busy_pid" ]; then
		exists yes "$wt/busy" "busy kept"
		kill "$busy_pid" 2> /dev/null
		wait "$busy_pid" 2> /dev/null
	fi

	cd / && git -C "$root/main" worktree unlock "$wt/locked" > /dev/null 2>&1
	rm -rf "$root"
	if [ "$fails" = 0 ]; then echo "SELFTEST PASS"; return 0; fi
	echo "SELFTEST FAIL ($fails)"
	return 1
}

APPLY=0
while [ $# -gt 0 ]; do
	case "$1" in
		--apply) APPLY=1; shift ;;
		--repo) [ -n "${2:-}" ] || { echo "usage: prune_worktrees.sh [--apply] [--repo <path>] | --selftest"; exit 2; }; REPO="$2"; shift 2 ;;
		--selftest) selftest; exit $? ;;
		*) echo "usage: prune_worktrees.sh [--apply] [--repo <path>] | --selftest"; exit 2 ;;
	esac
done

cd "$REPO" || exit 1
git rev-parse --verify -q "refs/heads/$MASTER" > /dev/null || { echo "no $MASTER branch in $REPO"; exit 1; }
COMMON_LOWER="$(mixed_path "$(git rev-parse --path-format=absolute --git-common-dir)" | tr '[:upper:]' '[:lower:]')"
MASTER_SUBJECTS=$(git log --format=%s "$MASTER")
# ps -W lists every process image, C:\x for native ones and /c/x for MSYS ones; elsewhere this stays empty and the in-use check is skipped
snapshot_processes() {
	PROCS=$(ps -W 2> /dev/null | awk 'NR > 1' | tr -d '\r')
	IMAGES=$(printf '%s\n' "$PROCS" | grep -oE '([A-Za-z]:\\.*| /[A-Za-z]/.*)$' | sed -E 's|^ /([A-Za-z])/|\1:/|' | tr '\\' '/' | tr '[:upper:]' '[:lower:]')
}
snapshot_processes

# unlanded <rev>: sets UNLANDED (commits master lacks), SAME_SUBJECT (of those, how many share a subject with master) and MERGES
unlanded() {
	UNLANDED=0; SAME_SUBJECT=0; MERGES=0
	git merge-base --is-ancestor "$1" "$MASTER" 2> /dev/null && return 0
	local cherry c
	cherry=$(git cherry "$MASTER" "$1" 2> /dev/null) || { UNLANDED="?"; return 1; }
	for c in $(printf '%s\n' "$cherry" | sed -n 's/^+ //p'); do
		UNLANDED=$((UNLANDED + 1))
		printf '%s\n' "$MASTER_SUBJECTS" | grep -qFx -- "$(git log -1 --format=%s "$c")" && SAME_SUBJECT=$((SAME_SUBJECT + 1))
	done
	MERGES=$(git rev-list --merges "$MASTER..$1" | grep -c .)
	return 0
}

# classify: from WT, BRANCH, HEADREV, IS_MAIN, LOCKED, LOCK_REASON sets VERDICT and REASON
classify() {
	VERDICT=KEEP
	REASON=""
	if [ "$IS_MAIN" = 1 ]; then REASON="main checkout"; return; fi
	if [ "$LOCKED" = 1 ]; then
		REASON="locked: ${LOCK_REASON:-no reason given}"
		local pid
		pid=$(printf '%s\n' "$LOCK_REASON" | sed -n 's/.*pid \([0-9][0-9]*\).*/\1/p')
		if [ -n "$pid" ] && [ -n "$PROCS" ] && ! printf '%s\n' "$PROCS" | awk -v p="$pid" '$4 == p { found = 1 } END { exit !found }'; then REASON="$REASON (pid $pid not running)"; fi
	fi
	if [ ! -d "$WT" ]; then
		[ "$LOCKED" = 1 ] && { REASON="$REASON; directory gone"; return; }
		VERDICT=PRUNABLE; REASON="directory gone; git worktree prune drops the entry, branch kept"; return
	fi
	local add=()
	[ "$BRANCH" = "$MASTER" ] && add+=("on $MASTER")
	unlanded "$HEADREV"
	if [ "$UNLANDED" = "?" ]; then add+=("cannot compare with $MASTER")
	elif [ "$UNLANDED" -gt 0 ]; then add+=("$UNLANDED unlanded$([ "$SAME_SUBJECT" -gt 0 ] && echo " ($SAME_SUBJECT with a subject on $MASTER)")"); fi
	[ "$MERGES" -gt 0 ] && add+=("$MERGES merge commit$([ "$MERGES" -gt 1 ] && echo s) not on $MASTER")
	local status tracked untracked
	if status=$(git -C "$WT" --no-optional-locks status --porcelain 2> /dev/null); then
		tracked=$(printf '%s\n' "$status" | grep -c '^[^?]')
		untracked=$(printf '%s\n' "$status" | grep -c '^??')
		[ $((tracked + untracked)) -gt 0 ] && add+=("dirty: $tracked tracked, $untracked untracked")
	else
		add+=("git status failed")
	fi
	local image
	image=$(printf '%s\n' "$IMAGES" | grep -F -- "$(printf '%s' "$WT" | tr '[:upper:]' '[:lower:]')/" | head -1)
	[ -n "$image" ] && add+=("in use: $image")
	local r
	for r in "${add[@]+"${add[@]}"}"; do REASON="${REASON:+$REASON; }$r"; done
	if [ -z "$REASON" ]; then VERDICT=REMOVABLE; REASON="landed and clean"; fi
}

row() { printf '%-10s %-64s %-44s %s\n' "$1" "$2" "$3" "$4"; }

ENTRIES=()
entry=""
while IFS= read -r line; do
	if [ -z "$line" ]; then [ -n "$entry" ] && ENTRIES+=("$entry"); entry=""; else entry="$entry$line"$'\n'; fi
done < <(git worktree list --porcelain | tr -d '\r'; echo)
[ -n "$entry" ] && ENTRIES+=("$entry")

declare -A IS_WORKTREE
PARENTS=()
COUNT_REMOVABLE=0; COUNT_KEEP=0; COUNT_PRUNABLE=0; REMOVED=0
for i in "${!ENTRIES[@]}"; do
	e="${ENTRIES[$i]}"
	WT=$(printf '%s' "$e" | sed -n 's/^worktree //p')
	HEADREV=$(printf '%s' "$e" | sed -n 's/^HEAD //p')
	BRANCH=$(printf '%s' "$e" | sed -n 's|^branch refs/heads/||p')
	LOCKED=0; LOCK_REASON=""
	if printf '%s' "$e" | grep -q '^locked'; then LOCKED=1; LOCK_REASON=$(printf '%s' "$e" | sed -n 's/^locked \{0,1\}//p'); fi
	IS_MAIN=0; [ "$i" = 0 ] && IS_MAIN=1
	printf '%s' "$e" | grep -q '^bare$' && IS_MAIN=1
	IS_WORKTREE["$WT"]=1
	[ "$IS_MAIN" = 0 ] && case " ${PARENTS[*]+"${PARENTS[*]}"} " in *" $(dirname "$WT") "*) ;; *) PARENTS+=("$(dirname "$WT")") ;; esac
	REF="${BRANCH:-detached ${HEADREV:0:10}}"
	classify
	row "$VERDICT" "$WT" "$REF" "$REASON"
	case "$VERDICT" in REMOVABLE) COUNT_REMOVABLE=$((COUNT_REMOVABLE + 1)) ;; PRUNABLE) COUNT_PRUNABLE=$((COUNT_PRUNABLE + 1)) ;; *) COUNT_KEEP=$((COUNT_KEEP + 1)) ;; esac
	[ "$APPLY" = 1 ] && [ "$VERDICT" = REMOVABLE ] || continue
	LOCKED=0
	snapshot_processes
	git worktree list --porcelain | tr -d '\r' | awk -v w="worktree $WT" '$0 == w { f = 1 } f && /^$/ { exit } f' | grep -q '^locked' && LOCKED=1
	classify
	[ "$VERDICT" = REMOVABLE ] || { echo "  not removed: changed since the report ($REASON)"; continue; }
	if ! git worktree remove --force "$WT"; then echo "  remove failed; left as is"; continue; fi
	REMOVED=$((REMOVED + 1))
	if [ -n "$BRANCH" ]; then
		if unlanded "refs/heads/$BRANCH" && [ "$UNLANDED" = 0 ] && [ "$MERGES" = 0 ]; then
			echo "  removed; $(git branch -D "$BRANCH")"
		else
			echo "  removed; branch $BRANCH kept (it moved: $UNLANDED unlanded, $MERGES merge commits)"
		fi
	else
		echo "  removed"
	fi
done

for p in "${PARENTS[@]+"${PARENTS[@]}"}"; do
	for d in "$p"/*/; do
		d="${d%/}"
		[ -d "$d" ] && [ -z "${IS_WORKTREE[$d]:-}" ] || continue
		case "$d" in
			*_scratch) row SCRATCH "$d" "" "scratch, not a worktree (left alone)" ;;
			*) [ -f "$d/.git" ] && tr '[:upper:]' '[:lower:]' < "$d/.git" | grep -qF -- "$COMMON_LOWER/worktrees/" && row ORPHAN "$d" "" "points at this repo but is not a registered worktree (left alone)" ;;
		esac
	done
done

if [ "$APPLY" = 1 ]; then
	git worktree prune -v
	echo "applied: removed $REMOVED of $COUNT_REMOVABLE removable, pruned the gone entries; kept $COUNT_KEEP"
else
	echo "dry run: $COUNT_REMOVABLE removable, $COUNT_PRUNABLE prunable, $COUNT_KEEP kept (--apply removes the removable and prunes the gone)"
fi
