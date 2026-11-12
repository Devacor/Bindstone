#!/usr/bin/env bash
# A new branch in its own worktree, beside the main checkout, made from a local branch (main unless named).
#
#   Tools/Landing/new_worktree.sh [--scratch] <name> [base]   # branch <name>, worktree <root>/<name>, base default main
#   Tools/Landing/new_worktree.sh --selftest                  # every check below, against a throwaway repository
#
# The root is JAI_WORKTREE_ROOT, else JaiScriptWorktrees beside the main checkout's folder (D:/git/JaiScriptWorktrees for
# D:/git/Bindstone), whichever worktree runs the script. A root that is a drive or share root, sits directly in one, is
# drive-relative or is inside the repository is refused; a missing root is made only when its parent exists. A remote-tracking
# base (origin/*, @{u}, @{push}) and FETCH_HEAD are refused, as are a name starting with @, an existing branch and an
# existing directory: nothing is overwritten or deleted. --scratch also makes <root>/<name>_scratch. Once made, the branch's
# merge-base with the base must be the base's tip. GIT_DIR and its relatives are ignored: the repository is the script's.
#
# The selftest's throwaway repository goes under JAI_SELFTEST_TMP, default tempclaudefolders beside the main checkout
# (D:/git/tempclaudefolders for D:/git/Bindstone), which must exist.
set -uo pipefail
SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"

fail() { echo "new_worktree: $*" >&2; exit 1; }

abs_path() {
	if command -v cygpath > /dev/null 2>&1; then cygpath -m -a "$1"
	elif realpath -m / > /dev/null 2>&1; then realpath -m "$1"
	elif command -v grealpath > /dev/null 2>&1; then grealpath -m "$1"
	else fail "cannot resolve $1: this needs cygpath or GNU realpath -m (on macOS, brew install coreutils provides grealpath)"; fi
}

# case-folded where the file system folds case, so D:/Git and D:/git compare equal
path_key() {
	local p="${1%/}"
	if command -v cygpath > /dev/null 2>&1; then printf '%s' "$p" | tr '[:upper:]' '[:lower:]'; else printf '%s' "$p"; fi
}

main_checkout() {
	local common
	common="$(git -C "$(dirname "$SCRIPT")" rev-parse --path-format=absolute --git-common-dir 2> /dev/null)" || return 1
	[ "$(basename "$common")" = .git ] || return 1
	abs_path "$(dirname "$common")"
}

selftest() {
	local tmp t repo root out rc main older pass=0 bad=0 victim before
	tmp="${JAI_SELFTEST_TMP:-$(dirname "$(main_checkout)")/tempclaudefolders}"
	[ -d "$tmp" ] || fail "selftest: no temp folder $tmp; make it or set JAI_SELFTEST_TMP"
	SELFTEST_DIR="$(mktemp -d "$tmp/new_worktree_selftest.XXXXXX")" || fail "selftest: no temp directory in $tmp"
	trap 'rm -rf "$SELFTEST_DIR"' EXIT
	t="$(abs_path "$SELFTEST_DIR")"
	repo="$t/git/Repo"
	root="$t/git/JaiScriptWorktrees"
	check() {
		if [ "$2" = 0 ]; then echo "ok: $1"; pass=$((pass + 1)); else echo "FAIL: $1"; bad=$((bad + 1)); fi
	}
	run() { out="$(env -u JAI_WORKTREE_ROOT "$@" 2>&1)"; rc=$?; }
	tg() { git -C "$repo" -c user.name=selftest -c user.email=selftest@invalid "$@" > /dev/null 2>&1; }
	no_branch() { ! git -C "$repo" show-ref -q --verify "refs/heads/$1"; }
	mkdir -p "$repo/Tools/Landing"
	git init -q -b main "$repo" && git -C "$repo" config core.autocrlf false || fail "selftest: git init failed"
	cp "$SCRIPT" "$repo/Tools/Landing/new_worktree.sh"
	echo one > "$repo/a.txt"
	tg add -A && tg commit -q -m "first"
	older="$(git -C "$repo" rev-parse HEAD)"
	git -C "$repo" update-ref refs/remotes/origin/main "$older"
	git -C "$repo" config remote.origin.url "$t/nowhere"
	git -C "$repo" config remote.origin.fetch "+refs/heads/*:refs/remotes/origin/*"
	git -C "$repo" config branch.main.remote origin
	git -C "$repo" config branch.main.merge refs/heads/main
	echo two > "$repo/a.txt"
	tg commit -q -am "second: the tip"
	main="$(git -C "$repo" rev-parse main)"
	local nw="$repo/Tools/Landing/new_worktree.sh"
	victim="$t/victim"
	git init -q -b main "$victim" && git -C "$victim" -c user.name=selftest -c user.email=selftest@invalid commit -q --allow-empty -m "victim" || fail "selftest: git init failed"
	victim_state() { git -C "$victim" for-each-ref; git -C "$victim" worktree list --porcelain; git -C "$victim" config --list --local; }
	before="$(victim_state)"

	run bash "$nw" one
	check "creates <root>/one from local main" "$([ "$rc" = 0 ] && [ -f "$root/one/a.txt" ] && [ "$(git -C "$repo" rev-parse one)" = "$main" ]; echo $?)"
	check "prints the path, the branch, the base sha with its subject and the build-debug hint" \
		"$(echo "$out" | grep -q "worktree: .*/JaiScriptWorktrees/one$" && echo "$out" | grep -q "^branch:   one$" && echo "$out" | grep -q "^base:     main $main second: the tip$" && echo "$out" | grep -q "cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug"; echo $?)"
	check "the default base is local main, not a stale origin/main" "$([ "$(git -C "$repo" rev-parse one)" != "$older" ]; echo $?)"
	check "makes no scratch directory unasked" "$([ ! -e "$root/one_scratch" ]; echo $?)"

	run bash "$nw" one
	check "refuses an existing branch" "$([ "$rc" != 0 ] && echo "$out" | grep -q "branch one exists"; echo $?)"

	mkdir -p "$root/two"
	touch "$root/two/keep"
	run bash "$nw" two
	check "refuses an existing directory and leaves it and the branch alone" "$([ "$rc" != 0 ] && [ -f "$root/two/keep" ] && no_branch two && echo "$out" | grep -q "exists"; echo $?)"

	run bash "$nw" three origin/main
	check "refuses a remote-tracking base" "$([ "$rc" != 0 ] && [ ! -e "$root/three" ] && no_branch three; echo $?)"
	local upstream
	for upstream in '@{u}' 'main@{upstream}' '@{push}' '@{u}~0' 'HEAD@{UPSTREAM}^0'; do
		run bash "$nw" three "$upstream"
		check "refuses the upstream shorthand $upstream" "$([ "$rc" != 0 ] && [ ! -e "$root/three" ] && no_branch three && echo "$out" | grep -q "remote-tracking"; echo $?)"
	done
	printf '%s\t\tbranch '"'"'main'"'"' of nowhere\n' "$older" > "$repo/.git/FETCH_HEAD"
	run bash "$nw" three FETCH_HEAD
	check "refuses FETCH_HEAD" "$([ "$rc" != 0 ] && [ ! -e "$root/three" ] && no_branch three && echo "$out" | grep -q "FETCH_HEAD"; echo $?)"

	run bash "$nw" four "$older"
	check "takes a named base" "$([ "$rc" = 0 ] && [ "$(git -C "$repo" rev-parse four)" = "$older" ] && echo "$out" | grep -q "^base:     $older $older first$"; echo $?)"

	run bash "$nw" five nosuchbase
	check "refuses an unknown base" "$([ "$rc" != 0 ] && [ ! -e "$root/five" ]; echo $?)"

	run bash "$nw" --scratch six
	check "--scratch makes <root>/six_scratch" "$([ "$rc" = 0 ] && [ -d "$root/six_scratch" ] && [ -d "$root/six" ] && echo "$out" | grep -q "^scratch:  .*/six_scratch$"; echo $?)"

	mkdir -p "$root/seven_scratch"
	run bash "$nw" --scratch seven
	check "refuses an existing scratch directory before making anything" "$([ "$rc" != 0 ] && [ ! -e "$root/seven" ] && no_branch seven; echo $?)"

	run bash "$nw" eight/nine
	check "refuses a name that is not one directory" "$([ "$rc" != 0 ] && [ ! -e "$root/eight" ]; echo $?)"

	run bash "$nw" "bad..name"
	check "refuses an invalid branch name" "$([ "$rc" != 0 ]; echo $?)"

	local at
	for at in @ @x; do
		run bash "$nw" "$at"
		check "refuses the name $at and makes no branch" "$([ "$rc" != 0 ] && no_branch "$at" && [ ! -e "$root/$at" ]; echo $?)"
	done

	tg tag tagname "$older"
	run bash "$nw" tagname
	check "a name that is also a tag is checked as the branch" "$([ "$rc" = 0 ] && [ "$(git -C "$repo" rev-parse refs/heads/tagname)" = "$main" ]; echo $?)"

	mkdir -p "$t/hooks" && printf '#!/bin/sh\nexit 1\n' > "$t/hooks/post-checkout" && chmod +x "$t/hooks/post-checkout"
	git -C "$repo" config core.hooksPath "$t/hooks"
	run bash "$nw" hookfail
	git -C "$repo" config --unset core.hooksPath
	check "a failed worktree add says the branch it left" "$([ "$rc" != 0 ] && echo "$out" | grep -q "branch hookfail now exists"; echo $?)"

	git -C "$repo" worktree add -q --detach "$root/Gone" main > /dev/null 2>&1
	rm -rf "$root/Gone"
	run bash "$nw" gone
	check "refuses a registered worktree whose folder is gone" "$([ "$rc" != 0 ] && echo "$out" | grep -q "registered worktree" && no_branch gone; echo $?)"

	run bash "$root/one/Tools/Landing/new_worktree.sh" ten
	check "run from a worktree, the root is still beside the main checkout" "$([ "$rc" = 0 ] && [ -d "$root/ten" ] && [ ! -e "$root/JaiScriptWorktrees" ]; echo $?)"

	run env JAI_WORKTREE_ROOT="$t/git/Elsewhere" bash "$nw" eleven
	check "JAI_WORKTREE_ROOT names the root" "$([ "$rc" = 0 ] && [ -d "$t/git/Elsewhere/eleven" ]; echo $?)"

	run env JAI_WORKTREE_ROOT="$t/git/NoParent/root" bash "$nw" twelve
	check "refuses a root whose parent is missing and makes neither" "$([ "$rc" != 0 ] && [ ! -e "$t/git/NoParent" ] && no_branch twelve; echo $?)"

	run env GIT_DIR="$victim/.git" bash "$nw" fourteen
	check "an inherited GIT_DIR is ignored: the branch is made here and the victim repository is untouched" \
		"$([ "$rc" = 0 ] && [ -d "$root/fourteen" ] && [ "$(victim_state)" = "$before" ] && [ ! -e "$t/JaiScriptWorktrees" ]; echo $?)"
	if [ -z "${NEW_WORKTREE_SELFTEST_NESTED:-}" ]; then
		out="$(GIT_DIR="$victim/.git" NEW_WORKTREE_SELFTEST_NESTED=1 JAI_SELFTEST_TMP="$t" bash "$nw" --selftest 2>&1)"; rc=$?
		check "the selftest under an inherited GIT_DIR passes and leaves the victim repository untouched" "$([ "$rc" = 0 ] && [ "$(victim_state)" = "$before" ]; echo $?)"
	fi

	# the root cases name an unknown base, so a script that lost its root check still stops before it makes anything
	run env JAI_WORKTREE_ROOT="$repo/wt" bash "$nw" twelve nosuchbase
	check "refuses a root inside the repository" "$([ "$rc" != 0 ] && [ ! -e "$repo/wt" ] && echo "$out" | grep -q "inside the repository"; echo $?)"

	local drive spelling
	drive="${t%%/*}/"
	for spelling in "$drive" "${drive}new_worktree_selftest_never_made" //nohost.invalid/share //nohost.invalid/share/wt / /. ; do
		run env JAI_WORKTREE_ROOT="$spelling" bash "$nw" thirteen nosuchbase
		check "refuses the root $spelling as a drive or share root, or directly in one" "$([ "$rc" != 0 ] && echo "$out" | grep -q "drive or share root"; echo $?)"
	done
	for spelling in "${drive%/}" "${drive%/}wt"; do
		run env JAI_WORKTREE_ROOT="$spelling" bash "$nw" thirteen nosuchbase
		check "refuses the drive-relative root $spelling" "$([ "$rc" != 0 ] && echo "$out" | grep -q "drive-relative"; echo $?)"
	done

	git -C "$repo" worktree list --porcelain | grep -c "^worktree " > "$t/count"
	check "made exactly the eight worktrees it was asked for (beside the main checkout and the gone one)" "$([ "$(cat "$t/count")" = 10 ]; echo $?)"

	if [ "$bad" = 0 ]; then echo "SELFTEST GREEN ($pass checks)"; return 0; fi
	echo "SELFTEST RED ($bad of $((pass + bad)) checks failed)"
	return 1
}

unset GIT_DIR GIT_WORK_TREE GIT_COMMON_DIR GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_NAMESPACE
SCRATCH=0
NAME=""
BASE=""
while [ "$#" -gt 0 ]; do
	case "$1" in
		--selftest) selftest; exit $? ;;
		--scratch) SCRATCH=1 ;;
		-*) fail "unknown option: $1" ;;
		*) if [ -z "$NAME" ]; then NAME="$1"; elif [ -z "$BASE" ]; then BASE="$1"; else fail "usage: new_worktree.sh [--scratch] <name> [base]"; fi ;;
	esac
	shift
done
[ -n "$NAME" ] || { echo "usage: new_worktree.sh [--scratch] <name> [base] | --selftest" >&2; exit 2; }
BASE="${BASE:-main}"

git -C "$(dirname "$SCRIPT")" rev-parse --git-dir > /dev/null 2>&1 || fail "not inside a git repository: $SCRIPT"
REPO="$(main_checkout)" || fail "the repository is not a checkout with a .git folder"

case "$NAME" in */* | *\\*) fail "the name names one directory, so it holds no slash: $NAME" ;; esac
case "$NAME" in @*) fail "a name starting with @ reads as a revision (@ alone is HEAD): $NAME" ;; esac
git check-ref-format --branch "$NAME" > /dev/null 2>&1 || fail "not a valid branch name: $NAME"

# is_top <resolved path>: a drive root, a //host or //host/share root, or the root / resolves to (git-bash's folder under MSYS)
FS_ROOT="$(abs_path /)" || exit 1
is_top() {
	case "$1" in "" | / | [A-Za-z]: | [A-Za-z]:/) return 0 ;; esac
	[ "$(path_key "$1")" = "$(path_key "$FS_ROOT")" ] && return 0
	case "$1" in //*) case "${1#//}" in */*/*) return 1 ;; *) return 0 ;; esac ;; esac
	return 1
}
ROOT_IN="${JAI_WORKTREE_ROOT:-$(dirname "$REPO")/JaiScriptWorktrees}"
case "$ROOT_IN" in [A-Za-z]: | [A-Za-z]:[!/\\]*) fail "the worktree root is drive-relative: $ROOT_IN (put a slash after the drive, e.g. $(dirname "$REPO")/JaiScriptWorktrees)" ;; esac
ROOT="$(abs_path "$ROOT_IN")" || exit 1
ROOT="${ROOT%/}"
is_top "$ROOT" && fail "the worktree root is a drive or share root: $ROOT_IN"
is_top "$(dirname "$ROOT")" && fail "the worktree root sits directly in a drive or share root: $ROOT (put it in a folder, e.g. $(dirname "$REPO")/JaiScriptWorktrees)"
case "$(path_key "$ROOT")/" in "$(path_key "$REPO")/"*) fail "the worktree root is inside the repository: $ROOT" ;; esac
[ -d "$ROOT" ] || [ -d "$(dirname "$ROOT")" ] || fail "the worktree root's parent $(dirname "$ROOT") does not exist; make it first (only the root itself is made)"

case "$(printf '%s' "$BASE" | tr '[:upper:]' '[:lower:]')" in *fetch_head*) fail "the base $BASE is whatever was fetched last; base on a local branch" ;; esac
for ref in "$BASE" "${BASE%%[~^]*}" "${BASE%%[~^@]*}"; do
	[ -n "$ref" ] || continue
	case "$(git -C "$REPO" rev-parse --symbolic-full-name "$ref" 2> /dev/null)" in
		refs/remotes/*) fail "the base $BASE is a remote-tracking ref; base on a local branch (origin/* can be far behind local main)" ;;
	esac
done
BASE_SHA="$(git -C "$REPO" rev-parse --verify --quiet "$BASE^{commit}")" || fail "unknown base: $BASE"

DIR="$ROOT/$NAME"
SCRATCH_DIR="$ROOT/${NAME}_scratch"
git -C "$REPO" show-ref --quiet --verify "refs/heads/$NAME" && fail "branch $NAME exists"
[ -e "$DIR" ] && fail "$DIR exists"
while read -r line; do
	case "$line" in
		"worktree "*) [ "$(path_key "${line#worktree }")" = "$(path_key "$DIR")" ] && fail "$DIR is a registered worktree (its folder is gone; git worktree prune clears it)" ;;
	esac
done < <(git -C "$REPO" worktree list --porcelain | tr -d '\r')
[ "$SCRATCH" = 1 ] && [ -e "$SCRATCH_DIR" ] && fail "$SCRATCH_DIR exists"

[ -d "$ROOT" ] || { mkdir "$ROOT" || fail "could not make the root $ROOT"; echo "made the root $ROOT"; }
if ! LOG="$(git -C "$REPO" worktree add --quiet -b "$NAME" "$DIR" "$BASE_SHA" 2>&1)"; then
	echo "$LOG" >&2
	if git -C "$REPO" show-ref --quiet --verify "refs/heads/$NAME"; then
		fail "git worktree add failed, and branch $NAME now exists at $(git -C "$REPO" rev-parse --short "refs/heads/$NAME") (left in place; git branch -D $NAME deletes it)"
	fi
	fail "git worktree add failed; no branch $NAME was made"
fi
[ "$SCRATCH" = 1 ] && { mkdir "$SCRATCH_DIR" || fail "the worktree is made, but not $SCRATCH_DIR"; }

TIP_NOW="$(git -C "$REPO" rev-parse --verify --quiet "$BASE^{commit}")"
MERGE_BASE="$(git -C "$REPO" merge-base "refs/heads/$NAME" "$BASE_SHA")"
[ -n "$MERGE_BASE" ] && [ -n "$TIP_NOW" ] && [ "$MERGE_BASE" = "$TIP_NOW" ] || fail "made $DIR at $BASE_SHA, but $BASE is now ${TIP_NOW:-unresolvable} (merge-base ${MERGE_BASE:-none}): it moved meanwhile; rebase the branch or make it again"

echo "worktree: $DIR"
echo "branch:   $NAME"
echo "base:     $BASE $BASE_SHA $(git -C "$REPO" log -1 --format=%s "$BASE_SHA")"
[ "$SCRATCH" = 1 ] && echo "scratch:  $SCRATCH_DIR"
echo "build-debug (from a Visual Studio developer prompt, VsDevCmd.bat -arch=x64):"
echo "  cd /d \"$DIR/Source/JaiScript\" && cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug"
