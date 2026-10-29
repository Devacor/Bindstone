#!/usr/bin/env bash
# Report every git worktree with a verdict; with --apply, remove the ones that hold nothing master lacks.
#
#   Tools/Landing/prune_worktrees.sh [--repo <path>]           # dry run: one row per worktree, nothing changes
#   Tools/Landing/prune_worktrees.sh --apply [--repo <path>]   # remove the REMOVABLE rows, prune entries whose directory is gone
#   Tools/Landing/prune_worktrees.sh --selftest                # one throwaway worktree per verdict, both modes checked
#
# REMOVABLE only when all hold: not the main checkout, not on master, not locked, not holding another worktree, not the
# worktree the caller runs in or this script lives in, no commit missing from master by patch identity (git cherry, plus any
# merge commit master lacks, which git cherry never lists; an empty commit counts as landed only when master has its
# subject), git status --porcelain empty, every ignored path build output (build*/, Source/JaiScript/build*/, out/build*,
# .cache/, *.jaibite), no running process image inside it and no file modified in the last 12 hours. HEAD is read live, not
# from the worktree list. Every other worktree is KEEP with its reasons; one whose directory is gone is PRUNABLE (git
# worktree prune drops the entry, the branch stays). --apply re-checks a row right before removing it, compares HEAD once
# more, and re-checks the branch right before deleting it; any failed remove, branch delete or prune exits 1. *_scratch
# folders and unregistered worktree directories beside the worktrees are listed, never touched.
#
# The selftest's throwaway repositories go under JAI_SELFTEST_TMP, default tempclaudefolders beside the main checkout
# (D:/git/tempclaudefolders for D:/git/Bindstone), which must exist.
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
MASTER=master
RECENT_HOURS=12
HAS_CYGPATH=0
command -v cygpath > /dev/null 2>&1 && HAS_CYGPATH=1

# mixed_path <path>: the D:/x form git prints, so bash paths and git paths compare as strings
mixed_path() {
	if [ "$HAS_CYGPATH" = 1 ]; then cygpath -m "$1"; else echo "$1"; fi
}

# path_key <path>: case-folded where the file system folds case
path_key() {
	if [ "$HAS_CYGPATH" = 1 ]; then printf '%s' "${1,,}"; else printf '%s' "$1"; fi
}

# ps -W lists every process image, C:\x for native ones and a POSIX path (/d/x, /usr/bin/x, /tmp/x) for MSYS ones; cygpath
# turns the POSIX ones into D:/x. Elsewhere this stays empty and the in-use check is skipped.
snapshot_processes() {
	PROCS=$(ps -W 2> /dev/null | awk 'NR > 1' | tr -d '\r')
	local native posix
	native=$(printf '%s\n' "$PROCS" | grep -oE '[A-Za-z]:\\.*$' | tr '\\' '/')
	posix=$(printf '%s\n' "$PROCS" | grep -vE '[A-Za-z]:\\' | grep -oE ' /.*$' | sed 's/^ //')
	[ -n "$posix" ] && posix=$(printf '%s\n' "$posix" | cygpath -m -f - 2> /dev/null)
	IMAGES=$(printf '%s\n%s\n' "$native" "$posix" | grep . | tr '[:upper:]' '[:lower:]')
}

selftest_tmp() {
	local common
	common="$(git -C "$(dirname "$SELF")" rev-parse --path-format=absolute --git-common-dir 2> /dev/null)" || return 1
	printf '%s' "${JAI_SELFTEST_TMP:-$(dirname "$(dirname "$common")")/tempclaudefolders}"
}

selftest() {
	local tmp fails=0 out rc wt n realgit
	busy_pid=""; holder_pid=""; probe_pid=""
	tmp="$(selftest_tmp)" && [ -d "$tmp" ] || { echo "selftest: no temp folder ${tmp:-(none)}; make it or set JAI_SELFTEST_TMP"; return 1; }
	root="$(mixed_path "$(mktemp -d "$tmp/prune_selftest.XXXXXX")")" || return 1
	trap 'for p in $busy_pid $holder_pid $probe_pid; do kill "$p" 2> /dev/null; done; cd /; rm -rf "$root"' EXIT
	wt="$root/wt"
	mkdir -p "$wt"
	git init -q -b "$MASTER" "$root/main" || return 1
	cd "$root/main" || return 1
	git config user.email selftest@example.invalid
	git config user.name selftest
	git config core.autocrlf false
	printf 'build*/\nout/\n.cache/\n*.jaibite\n.internal/\n' > .gitignore
	printf 'one\n' > f.txt
	mkdir -p Tools/Landing && cp "$SELF" Tools/Landing/prune_worktrees.sh
	git add . && git commit -qm base
	printf 'two\n' >> f.txt && git commit -qam second

	git worktree add -q -b picked "$wt/picked" "$MASTER~1"
	(cd "$wt/picked" && printf 'picked\n' > p.txt && git add p.txt && git commit -qm "pick me")
	git cherry-pick picked > /dev/null
	git worktree add -q -b behind "$wt/behind" "$MASTER~1"
	git worktree add -q --detach "$wt/detached" "$MASTER~1"
	git worktree add -q -b ignored "$wt/ignored" "$MASTER"
	mkdir -p "$wt/ignored/build" "$wt/ignored/build-lsp" "$wt/ignored/out/build-x" "$wt/ignored/.cache" "$wt/ignored/sub"
	printf 'obj\n' > "$wt/ignored/build/a.obj"
	printf 'obj\n' > "$wt/ignored/build-lsp/b.o"
	printf 'obj\n' > "$wt/ignored/out/build-x/c.obj"
	printf 'c\n' > "$wt/ignored/.cache/d"
	printf 'c\n' > "$wt/ignored/sub/e.jaibite"
	git worktree add -q -b ledger "$wt/ledger" "$MASTER"
	mkdir -p "$wt/ledger/.internal" && printf 'notes\n' > "$wt/ledger/.internal/notes.md"
	git worktree add -q -b logs "$wt/logs" "$MASTER"
	mkdir -p "$wt/logs/out/build" && printf 'log\n' > "$wt/logs/out/run.log"
	git worktree add -q -b outer "$wt/outer" "$MASTER"
	git worktree add -q -b inner "$wt/outer/build/inner" "$MASTER"
	printf 'precious\n' >> "$wt/outer/build/inner/f.txt"
	git worktree add -q -b unlanded "$wt/unlanded" "$MASTER"
	(cd "$wt/unlanded" && printf 'new\n' > u.txt && git add u.txt && git commit -qm "not landed")
	git worktree add -q -b resolved "$wt/resolved" "$MASTER"
	(cd "$wt/resolved" && printf 'mine\n' > r.txt && git add r.txt && git commit -qm "landed with a resolution")
	printf 'resolved differently\n' > r.txt && git add r.txt && git commit -qm "landed with a resolution"
	git worktree add -q -b merged "$wt/merged" "$MASTER"
	(cd "$wt/merged" && git merge -q --no-ff -m "evil merge" picked > /dev/null)
	git worktree add -q --detach "$wt/detached_unlanded" "$MASTER"
	(cd "$wt/detached_unlanded" && printf 'd\n' > d.txt && git add d.txt && git commit -qm "detached work")
	git worktree add -q --detach "$wt/late" "$MASTER"
	git worktree add -q -b dirty "$wt/dirty" "$MASTER"
	printf 'edit\n' >> "$wt/dirty/f.txt"
	git worktree add -q -b untracked "$wt/untracked" "$MASTER"
	printf 'loose\n' > "$wt/untracked/loose.txt"
	git worktree add -q -b locked "$wt/locked" "$MASTER"
	git worktree lock --reason "agent busy" "$wt/locked"
	git worktree add -q -b missing "$wt/missing" "$MASTER"
	rm -rf "$wt/missing"
	git worktree add -q -b here "$wt/here" "$MASTER"
	git worktree add -q -b scriptwt "$wt/scriptwt" "$MASTER"
	git worktree add -q -b held "$wt/held" "$MASTER"
	mkdir -p "$wt/held/build" && printf 'obj\n' > "$wt/held/build/a.obj"
	git worktree add -q -b emptyb "$wt/emptyb" "$MASTER"
	git -C "$wt/emptyb" commit -q --allow-empty -m "Experiment: Success - result only in this message"
	git worktree add -q -b emptyland "$wt/emptyland" "$MASTER"
	git -C "$wt/emptyland" commit -q --allow-empty -m "empty on master"
	git commit -q --allow-empty -m "empty on master"
	mkdir -p "$wt/picked_scratch"
	mkdir -p "$wt/orphan" && printf 'gitdir: %s/main/.git/worktrees/orphan\n' "$root" > "$wt/orphan/.git"
	if ps -W > /dev/null 2>&1; then
		git worktree add -q -b busy "$wt/busy" "$MASTER"
		mkdir -p "$wt/busy/build" && cp "$(command -v sleep)" "$wt/busy/build/busy_sleep.exe"
	fi
	# everything so far is days old; fresh is made after
	find "$wt" "$root/main/.git/worktrees" -exec touch -d '3 days ago' {} + 2> /dev/null
	git worktree add -q -b fresh "$wt/fresh" "$MASTER"
	if [ -e "$wt/busy" ]; then
		"$wt/busy/build/busy_sleep.exe" 120 &
		busy_pid=$!
		(cd "$wt/held/build" && exec ping -n 120 127.0.0.1 > /dev/null) &
		holder_pid=$!
		sleep 1
	fi
	# a git that commits in late the first time the script looks inside it, after the worktree list was read
	realgit="$(command -v git)"
	mkdir -p "$root/shim"
	cat > "$root/shim/git" <<-EOF
		#!/usr/bin/env bash
		if [ "\$1" = -C ] && [ "\$2" = "$wt/late" ] && [ ! -e "$root/shim/done" ]; then
			touch "$root/shim/done"
			printf 'late\n' > "$wt/late/late.txt"
			"$realgit" -C "$wt/late" add late.txt
			"$realgit" -C "$wt/late" commit -qm "committed after the list was read"
		fi
		exec "$realgit" "\$@"
	EOF
	chmod +x "$root/shim/git"

	# expect <output> <verdict> <worktree name> [<reason text>]
	expect() {
		local row
		row=$(printf '%s\n' "$1" | grep -E "^$2 +$root/(wt/)?$3( |$)")
		if [ -n "$row" ] && { [ -z "${4:-}" ] || printf '%s\n' "$row" | grep -qF -- "$4"; }; then echo "ok:   $2 $3${4:+ ($4)}"
		else echo "FAIL: expected $2 $3${4:+ ($4)}"; fails=$((fails + 1)); fi
	}
	ok() { if [ "$1" = 0 ]; then echo "ok:   $2"; else echo "FAIL: $2"; fails=$((fails + 1)); fi; }
	exists() { if [ -e "$2" ]; then [ "$1" = yes ]; else [ "$1" = no ]; fi; ok $? "$3"; }
	branch_is() { if git show-ref -q --verify "refs/heads/$2"; then [ "$1" = yes ]; else [ "$1" = no ]; fi; ok $? "$3"; }

	if [ -n "$(command -v cygpath)" ] && ps -W > /dev/null 2>&1; then
		sleep 60 &
		probe_pid=$!
		sleep 1
		snapshot_processes
		printf '%s\n' "$IMAGES" | grep -qxF -- "$(cygpath -m "$(command -v sleep)" | tr '[:upper:]' '[:lower:]')"
		ok $? "an MSYS image outside a /<drive>/ mount (/usr/bin/sleep) is listed by its Windows path"
		kill "$probe_pid" 2> /dev/null; wait "$probe_pid" 2> /dev/null; probe_pid=""
	fi

	n=$(git worktree list --porcelain | grep -c '^worktree ')
	echo "--- dry run (a worktree commits after the list is read)"
	out=$(PATH="$(command -v cygpath > /dev/null && cygpath -u "$root/shim" || echo "$root/shim"):$PATH" bash "$SELF" --repo "$root/main"); rc=$?
	echo "$out"
	echo "---"
	ok "$rc" "dry run exits 0"
	expect "$out" KEEP main "main checkout"
	expect "$out" REMOVABLE picked
	expect "$out" REMOVABLE behind
	expect "$out" REMOVABLE detached
	expect "$out" REMOVABLE ignored
	expect "$out" REMOVABLE emptyland
	expect "$out" REMOVABLE here
	expect "$out" REMOVABLE scriptwt
	expect "$out" KEEP ledger "ignored: .internal/"
	expect "$out" KEEP logs "ignored: out/run.log"
	expect "$out" KEEP outer "contains worktree $wt/outer/build/inner"
	expect "$out" KEEP outer/build/inner "1 tracked"
	expect "$out" KEEP unlanded "1 unlanded"
	expect "$out" KEEP resolved "1 with a subject on master"
	expect "$out" KEEP merged "1 merge commit"
	expect "$out" KEEP detached_unlanded "1 unlanded"
	expect "$out" KEEP late "1 unlanded"
	expect "$out" KEEP emptyb "1 unlanded"
	expect "$out" KEEP dirty "1 tracked"
	expect "$out" KEEP untracked "1 untracked"
	expect "$out" KEEP locked "locked: agent busy"
	expect "$out" KEEP fresh "recent activity"
	expect "$out" PRUNABLE missing
	expect "$out" SCRATCH picked_scratch
	expect "$out" ORPHAN orphan
	[ -n "$busy_pid" ] && expect "$out" KEEP busy "in use: "
	[ -n "$holder_pid" ] && expect "$out" REMOVABLE held
	[ "$(git worktree list --porcelain | grep -c '^worktree ')" = "$n" ]; ok $? "dry run changed the worktree list by nothing"

	echo "--- dry run from inside here, with the script in scriptwt"
	out=$(cd "$wt/here" && bash "$wt/scriptwt/Tools/Landing/prune_worktrees.sh"); rc=$?
	ok "$rc" "dry run exits 0"
	expect "$out" KEEP here "current worktree"
	expect "$out" KEEP scriptwt "holds this script"

	echo "--- apply from inside here$([ -n "$holder_pid" ] && echo ", held's build/ in use as a process's working directory")"
	out=$(cd "$wt/here" && bash "$wt/scriptwt/Tools/Landing/prune_worktrees.sh" --apply); rc=$?
	echo "$out"
	echo "---"
	if [ -n "$holder_pid" ]; then
		[ "$rc" != 0 ]; ok $? "a failed remove exits non-zero"
		printf '%s\n' "$out" | grep -qF "remove failed; worktree partially deleted" ; ok $? "a failed remove says what is left"
		branch_is yes held "branch held kept after its failed remove"
		kill "$holder_pid" 2> /dev/null; wait "$holder_pid" 2> /dev/null; holder_pid=""
	else
		ok "$rc" "apply exits 0"
	fi
	local name
	for name in picked behind detached ignored emptyland; do exists no "$wt/$name" "$name removed"; done
	for name in picked behind ignored emptyland; do branch_is no "$name" "branch $name deleted"; done
	for name in here scriptwt ledger logs outer unlanded resolved merged detached_unlanded late emptyb dirty untracked locked fresh; do exists yes "$wt/$name" "$name kept"; done
	for name in here scriptwt ledger logs outer inner unlanded resolved merged emptyb dirty untracked locked missing fresh; do branch_is yes "$name" "branch $name kept"; done
	exists yes "$wt/outer/build/inner/f.txt" "inner's worktree kept inside outer"
	grep -q precious "$wt/outer/build/inner/f.txt" 2> /dev/null; ok $? "inner's uncommitted edit kept"
	exists yes "$wt/ledger/.internal/notes.md" "ledger's ignored notes kept"
	exists yes "$wt/logs/out/run.log" "logs' ignored log kept"
	[ "$(git -C "$wt/late" log -1 --format=%s HEAD 2> /dev/null)" = "committed after the list was read" ]; ok $? "late's commit kept with its worktree"
	git worktree list --porcelain | grep -q "^worktree $wt/missing$" && { echo "FAIL: missing entry not pruned"; fails=$((fails + 1)); } || echo "ok:   missing entry pruned"
	exists yes "$wt/picked_scratch" "scratch kept"
	exists yes "$wt/orphan/.git" "orphan kept"
	[ -n "$busy_pid" ] && exists yes "$wt/busy" "busy kept"

	echo "--- apply again"
	out=$(bash "$SELF" --apply --repo "$root/main"); rc=$?
	ok "$rc" "an apply with nothing failing exits 0"
	if [ -n "$busy_pid" ]; then
		kill "$busy_pid" 2> /dev/null
		wait "$busy_pid" 2> /dev/null
		busy_pid=""
	fi

	cd / && git -C "$root/main" worktree unlock "$wt/locked" > /dev/null 2>&1
	if [ "$fails" = 0 ]; then echo "SELFTEST PASS"; return 0; fi
	echo "SELFTEST FAIL ($fails)"
	return 1
}

unset GIT_DIR GIT_WORK_TREE GIT_COMMON_DIR GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE
START_DIR="$(mixed_path "$PWD")"
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
declare -A MASTER_SUBJECT_SET=()
while IFS= read -r subject; do [ -n "$subject" ] && MASTER_SUBJECT_SET["$subject"]=1; done < <(git log --format=%s "$MASTER")
START_KEY="$(path_key "$START_DIR")"
SELF_KEY="$(path_key "$(mixed_path "$(dirname "$SELF")")")"
snapshot_processes

# unlanded <rev>: sets UNLANDED (commits master lacks), SAME_SUBJECT (of those, how many share a subject with master) and MERGES
unlanded() {
	UNLANDED=0; SAME_SUBJECT=0; MERGES=0
	git merge-base --is-ancestor "$1" "$MASTER" 2> /dev/null && return 0
	local cherry line head rest mark c t parents subject
	cherry=$(git cherry "$MASTER" "$1" 2> /dev/null) || { UNLANDED="?"; return 1; }
	local -A tree=() first_parent=() subject_of=()
	# one log of the branch side with its boundary: every commit's tree, first parent and subject, and its parent's tree
	while IFS= read -r line; do
		head="${line%%$'\t'*}"; subject="${line#*$'\t'}"
		mark="${head%% *}"; rest="${head#* }"
		c="${rest%% *}"; rest="${rest#* }"
		t="${rest%% *}"; parents=""; [ "$rest" = "$t" ] || parents="${rest#* }"
		tree[$c]="$t"; first_parent[$c]="${parents%% *}"; subject_of[$c]="$subject"
		[ "$mark" != - ] && [ "${parents#* }" != "$parents" ] && MERGES=$((MERGES + 1))
	done < <(git log --boundary --format='%m %H %T %P%x09%s' "$MASTER..$1")
	while read -r mark c; do
		[ -n "$c" ] || continue
		subject="${subject_of[$c]-}"
		if [ "$mark" = - ]; then
			# git cherry finds every empty commit equal to any empty commit on master, so an empty one needs its subject there
			t="${first_parent[$c]-}"
			[ -n "$t" ] && [ "${tree[$c]-}" = "${tree[$t]-x}" ] || continue
			[ -n "$subject" ] && [ -n "${MASTER_SUBJECT_SET[$subject]+x}" ] && continue
			UNLANDED=$((UNLANDED + 1))
			continue
		fi
		UNLANDED=$((UNLANDED + 1))
		[ -n "$subject" ] && [ -n "${MASTER_SUBJECT_SET[$subject]+x}" ] && SAME_SUBJECT=$((SAME_SUBJECT + 1))
	done <<< "$cherry"
	return 0
}

# build_output <path relative to the worktree>: ignored paths --apply's --force may delete
build_output() {
	local r
	case "$1" in *.jaibite | .cache/* | */.cache/*) return 0 ;; esac
	for r in "" Source/JaiScript/ Source/JaiScriptComparisons/; do
		case "$1" in "$r"build*/* | "$r"out/build*) return 0 ;; esac
	done
	return 1
}

# worktree_status: sets TRACKED, UNTRACKED and NOT_BUILD (ignored paths that are not build output; an ignored directory
# that is not build output is judged by its entries, one level down); returns 1 when git status fails
worktree_status() {
	TRACKED=0; UNTRACKED=0; NOT_BUILD=()
	local status entry code path child
	status=$(git -C "$WT" --no-optional-locks status --porcelain -z --ignored 2> /dev/null | tr '\0' '\n') || return 1
	while IFS= read -r entry; do
		[ -n "$entry" ] || continue
		code="${entry:0:2}"; path="${entry:3}"
		case "$code" in
			'??') UNTRACKED=$((UNTRACKED + 1)) ;;
			'!!')
				build_output "$path" && continue
				case "$path" in
					*/)
						while IFS= read -r child; do
							child="$path$child"
							[ -d "$WT/$child" ] && child="$child/"
							build_output "$child" || NOT_BUILD+=("$child")
						done < <(ls -A "$WT/$path" 2> /dev/null)
						;;
					*) NOT_BUILD+=("$path") ;;
				esac
				;;
			*) TRACKED=$((TRACKED + 1)) ;;
		esac
	done <<< "$status"
	return 0
}

# recent_activity: the first file or directory in the worktree (four levels deep) or its git admin directory modified within RECENT_HOURS
recent_activity() {
	local admin
	admin=$(git -C "$WT" rev-parse --path-format=absolute --git-dir 2> /dev/null)
	{
		find "$WT" -maxdepth 4 -mmin "-$((RECENT_HOURS * 60))" -print -quit 2> /dev/null
		[ -n "$admin" ] && find "$admin" -maxdepth 2 -mmin "-$((RECENT_HOURS * 60))" -print -quit 2> /dev/null
	} | head -1
}

# classify: from WT, IS_MAIN, LOCKED, LOCK_REASON reads HEADREV and BRANCH live and sets VERDICT and REASON
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
	local add=() key j
	key="$(path_key "$WT")"
	case "$START_KEY/" in "$key/"*) add+=("current worktree") ;; esac
	case "$SELF_KEY/" in "$key/"*) add+=("holds this script") ;; esac
	for j in "${!ALL_KEYS[@]}"; do
		case "${ALL_KEYS[$j]}" in "$key/"*) add+=("contains worktree ${ALL_WT[$j]}"); break ;; esac
	done
	if HEADREV=$(git -C "$WT" rev-parse -q --verify HEAD 2> /dev/null); then
		BRANCH=$(git -C "$WT" symbolic-ref -q HEAD 2> /dev/null); BRANCH="${BRANCH#refs/heads/}"
		[ "$BRANCH" = "$MASTER" ] && add+=("on $MASTER")
		unlanded "$HEADREV"
		if [ "$UNLANDED" = "?" ]; then add+=("cannot compare with $MASTER")
		elif [ "$UNLANDED" -gt 0 ]; then add+=("$UNLANDED unlanded$([ "$SAME_SUBJECT" -gt 0 ] && echo " ($SAME_SUBJECT with a subject on $MASTER)")"); fi
		[ "$MERGES" -gt 0 ] && add+=("$MERGES merge commit$([ "$MERGES" -gt 1 ] && echo s) not on $MASTER")
	else
		HEADREV=""; BRANCH=""
		add+=("HEAD unreadable")
	fi
	if worktree_status; then
		[ $((TRACKED + UNTRACKED)) -gt 0 ] && add+=("dirty: $TRACKED tracked, $UNTRACKED untracked")
		[ "${#NOT_BUILD[@]}" -gt 0 ] && add+=("ignored: ${NOT_BUILD[*]:0:3}$([ "${#NOT_BUILD[@]}" -gt 3 ] && echo " (+$((${#NOT_BUILD[@]} - 3)) more)")")
	else
		add+=("git status failed")
	fi
	local image
	image=$(printf '%s\n' "$IMAGES" | grep -F -- "$(printf '%s' "$WT" | tr '[:upper:]' '[:lower:]')/" | head -1)
	[ -n "$image" ] && add+=("in use: $image")
	if [ "${#add[@]}" = 0 ] && [ -z "$REASON" ]; then
		local recent
		recent=$(recent_activity)
		[ -n "$recent" ] && add+=("recent activity: $recent (modified within $RECENT_HOURS h)")
	fi
	local r
	for r in "${add[@]+"${add[@]}"}"; do REASON="${REASON:+$REASON; }$r"; done
	if [ -z "$REASON" ]; then VERDICT=REMOVABLE; REASON="landed and clean"; fi
}

# left_behind: what a failed remove left of WT
left_behind() {
	local what=()
	if [ -d "$WT" ]; then
		what+=("$(ls -A "$WT" | grep -c .) entries left in $WT ($(ls -A "$WT" | head -3 | tr '\n' ' ' | sed 's/ $//'))")
		[ -e "$WT/.git" ] || what+=("its .git file gone")
	else
		what+=("directory gone")
	fi
	if git worktree list --porcelain | tr -d '\r' | grep -qxF "worktree $WT"; then what+=("still registered"); else what+=("no longer registered"); fi
	[ -n "$BRANCH" ] && what+=("branch $BRANCH kept")
	local IFS=';'
	printf '%s' "${what[*]}" | sed 's/;/; /g'
}

row() { printf '%-10s %-64s %-44s %s\n' "$1" "$2" "$3" "$4"; }

ENTRIES=()
entry=""
while IFS= read -r line; do
	if [ -z "$line" ]; then [ -n "$entry" ] && ENTRIES+=("$entry"); entry=""; else entry="$entry$line"$'\n'; fi
done < <(git worktree list --porcelain | tr -d '\r'; echo)
[ -n "$entry" ] && ENTRIES+=("$entry")
ALL_WT=()
ALL_KEYS=()
for e in "${ENTRIES[@]}"; do
	ALL_WT+=("$(printf '%s' "$e" | sed -n 's/^worktree //p')")
	ALL_KEYS+=("$(path_key "${ALL_WT[-1]}")")
done

declare -A IS_WORKTREE
PARENTS=()
COUNT_REMOVABLE=0; COUNT_KEEP=0; COUNT_PRUNABLE=0; REMOVED=0; FAILED=0
for i in "${!ENTRIES[@]}"; do
	e="${ENTRIES[$i]}"
	WT="${ALL_WT[$i]}"
	BRANCH=$(printf '%s' "$e" | sed -n 's|^branch refs/heads/||p')
	HEADREV=$(printf '%s' "$e" | sed -n 's/^HEAD //p')
	LOCKED=0; LOCK_REASON=""
	if printf '%s' "$e" | grep -q '^locked'; then LOCKED=1; LOCK_REASON=$(printf '%s' "$e" | sed -n 's/^locked \{0,1\}//p'); fi
	IS_MAIN=0; [ "$i" = 0 ] && IS_MAIN=1
	printf '%s' "$e" | grep -q '^bare$' && IS_MAIN=1
	IS_WORKTREE["$WT"]=1
	[ "$IS_MAIN" = 0 ] && case " ${PARENTS[*]+"${PARENTS[*]}"} " in *" $(dirname "$WT") "*) ;; *) PARENTS+=("$(dirname "$WT")") ;; esac
	classify
	row "$VERDICT" "$WT" "${BRANCH:-detached ${HEADREV:0:10}}" "$REASON"
	case "$VERDICT" in REMOVABLE) COUNT_REMOVABLE=$((COUNT_REMOVABLE + 1)) ;; PRUNABLE) COUNT_PRUNABLE=$((COUNT_PRUNABLE + 1)) ;; *) COUNT_KEEP=$((COUNT_KEEP + 1)) ;; esac
	[ "$APPLY" = 1 ] && [ "$VERDICT" = REMOVABLE ] || continue
	LOCKED=0
	snapshot_processes
	git worktree list --porcelain | tr -d '\r' | awk -v w="worktree $WT" '$0 == w { f = 1 } f && /^$/ { exit } f' | grep -q '^locked' && LOCKED=1
	classify
	[ "$VERDICT" = REMOVABLE ] || { echo "  not removed: changed since the report ($REASON)"; continue; }
	CHECKED_HEAD="$HEADREV"; CHECKED_BRANCH="$BRANCH"
	BRANCH=$(git -C "$WT" symbolic-ref -q HEAD 2> /dev/null); BRANCH="${BRANCH#refs/heads/}"
	if [ "$(git -C "$WT" rev-parse -q --verify HEAD 2> /dev/null)" != "$CHECKED_HEAD" ] || [ "$BRANCH" != "$CHECKED_BRANCH" ]; then
		echo "  not removed: HEAD moved while it was checked"
		continue
	fi
	if ! git worktree remove --force "$WT"; then
		FAILED=$((FAILED + 1))
		echo "  remove failed; worktree partially deleted: $(left_behind)"
		continue
	fi
	REMOVED=$((REMOVED + 1))
	if [ -n "$BRANCH" ]; then
		if unlanded "refs/heads/$BRANCH" && [ "$UNLANDED" = 0 ] && [ "$MERGES" = 0 ]; then
			if deleted=$(git branch -D "$BRANCH" 2>&1); then echo "  removed; $deleted"; else FAILED=$((FAILED + 1)); echo "  removed; branch delete failed: $deleted"; fi
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
	git worktree prune -v || { FAILED=$((FAILED + 1)); echo "git worktree prune failed"; }
	echo "applied: removed $REMOVED of $COUNT_REMOVABLE removable, pruned the gone entries; kept $COUNT_KEEP; $FAILED failed"
	[ "$FAILED" = 0 ] || exit 1
else
	echo "dry run: $COUNT_REMOVABLE removable, $COUNT_PRUNABLE prunable, $COUNT_KEEP kept (--apply removes the removable and prunes the gone)"
fi
