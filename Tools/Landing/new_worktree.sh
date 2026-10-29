#!/usr/bin/env bash
# A new branch in its own worktree, beside the main checkout, made from a local branch (master unless named).
#
#   Tools/Landing/new_worktree.sh [--scratch] <name> [base]   # branch <name>, worktree <root>/<name>, base default master
#   Tools/Landing/new_worktree.sh --selftest                  # every check below, against a throwaway repository
#
# The root is JAI_WORKTREE_ROOT, else JaiScriptWorktrees beside the main checkout's folder (D:/git/JaiScriptWorktrees for
# D:/git/Bindstone), whichever worktree runs the script. A root at a drive root, directly in one, or inside the repository is
# refused, as is a remote-tracking base, an existing branch and an existing directory: nothing is overwritten or deleted.
# --scratch also makes <root>/<name>_scratch. Once made, the branch's merge-base with the base must be the base's tip.
set -uo pipefail
SCRIPT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"

fail() { echo "new_worktree: $*" >&2; exit 1; }

abs_path() {
	if command -v cygpath >/dev/null 2>&1; then cygpath -m -a "$1"; else realpath -m "$1"; fi
}

# case-folded where the file system folds case, so D:/Git and D:/git compare equal
path_key() {
	local p="${1%/}"
	if command -v cygpath >/dev/null 2>&1; then printf '%s' "${p,,}"; else printf '%s' "$p"; fi
}

selftest() {
	local t repo root out rc master older pass=0 bad=0
	SELFTEST_DIR="$(mktemp -d)" || fail "selftest: no temp directory"
	trap 'rm -rf "$SELFTEST_DIR"' EXIT
	t="$SELFTEST_DIR"
	repo="$t/git/Repo"
	root="$t/git/JaiScriptWorktrees"
	check() {
		if [ "$2" = 0 ]; then echo "ok: $1"; pass=$((pass + 1)); else echo "FAIL: $1"; bad=$((bad + 1)); fi
	}
	run() { out="$(env -u JAI_WORKTREE_ROOT "$@" 2>&1)"; rc=$?; }
	tg() { git -C "$repo" -c user.name=selftest -c user.email=selftest@invalid "$@" > /dev/null 2>&1; }
	mkdir -p "$repo/Tools/Landing"
	git init -q -b master "$repo" && git -C "$repo" config core.autocrlf false || fail "selftest: git init failed"
	cp "$SCRIPT" "$repo/Tools/Landing/new_worktree.sh"
	echo one > "$repo/a.txt"
	tg add -A && tg commit -q -m "first"
	older="$(git -C "$repo" rev-parse HEAD)"
	git -C "$repo" update-ref refs/remotes/origin/master "$older"
	echo two > "$repo/a.txt"
	tg commit -q -am "second: the tip"
	master="$(git -C "$repo" rev-parse master)"
	local nw="$repo/Tools/Landing/new_worktree.sh"

	run bash "$nw" one
	check "creates <root>/one from local master" "$([ "$rc" = 0 ] && [ -f "$root/one/a.txt" ] && [ "$(git -C "$repo" rev-parse one)" = "$master" ]; echo $?)"
	check "prints the path, the branch, the base sha with its subject and the build-debug hint" \
		"$(echo "$out" | grep -q "worktree: .*/JaiScriptWorktrees/one$" && echo "$out" | grep -q "^branch:   one$" && echo "$out" | grep -q "^base:     master $master second: the tip$" && echo "$out" | grep -q "cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug"; echo $?)"
	check "the default base is local master, not a stale origin/master" "$([ "$(git -C "$repo" rev-parse one)" != "$older" ]; echo $?)"
	check "makes no scratch directory unasked" "$([ ! -e "$root/one_scratch" ]; echo $?)"

	run bash "$nw" one
	check "refuses an existing branch" "$([ "$rc" != 0 ] && echo "$out" | grep -q "branch one exists"; echo $?)"

	mkdir -p "$root/two"
	touch "$root/two/keep"
	run bash "$nw" two
	check "refuses an existing directory and leaves it and the branch alone" "$([ "$rc" != 0 ] && [ -f "$root/two/keep" ] && ! git -C "$repo" show-ref -q --verify refs/heads/two && echo "$out" | grep -q "exists"; echo $?)"

	run bash "$nw" three origin/master
	check "refuses a remote-tracking base" "$([ "$rc" != 0 ] && [ ! -e "$root/three" ] && ! git -C "$repo" show-ref -q --verify refs/heads/three; echo $?)"

	run bash "$nw" four "$older"
	check "takes a named base" "$([ "$rc" = 0 ] && [ "$(git -C "$repo" rev-parse four)" = "$older" ] && echo "$out" | grep -q "^base:     $older $older first$"; echo $?)"

	run bash "$nw" five nosuchbase
	check "refuses an unknown base" "$([ "$rc" != 0 ] && [ ! -e "$root/five" ]; echo $?)"

	run bash "$nw" --scratch six
	check "--scratch makes <root>/six_scratch" "$([ "$rc" = 0 ] && [ -d "$root/six_scratch" ] && [ -d "$root/six" ] && echo "$out" | grep -q "^scratch:  .*/six_scratch$"; echo $?)"

	mkdir -p "$root/seven_scratch"
	run bash "$nw" --scratch seven
	check "refuses an existing scratch directory before making anything" "$([ "$rc" != 0 ] && [ ! -e "$root/seven" ] && ! git -C "$repo" show-ref -q --verify refs/heads/seven; echo $?)"

	run bash "$nw" eight/nine
	check "refuses a name that is not one directory" "$([ "$rc" != 0 ] && [ ! -e "$root/eight" ]; echo $?)"

	run bash "$nw" "bad..name"
	check "refuses an invalid branch name" "$([ "$rc" != 0 ]; echo $?)"

	git -C "$repo" worktree add -q --detach "$root/Gone" master > /dev/null 2>&1
	rm -rf "$root/Gone"
	run bash "$nw" gone
	check "refuses a registered worktree whose folder is gone" "$([ "$rc" != 0 ] && echo "$out" | grep -q "registered worktree" && ! git -C "$repo" show-ref -q --verify refs/heads/gone; echo $?)"

	run bash "$root/one/Tools/Landing/new_worktree.sh" ten
	check "run from a worktree, the root is still beside the main checkout" "$([ "$rc" = 0 ] && [ -d "$root/ten" ] && [ ! -e "$root/JaiScriptWorktrees" ]; echo $?)"

	run env JAI_WORKTREE_ROOT="$t/git/Elsewhere" bash "$nw" eleven
	check "JAI_WORKTREE_ROOT names the root" "$([ "$rc" = 0 ] && [ -d "$t/git/Elsewhere/eleven" ]; echo $?)"

	# the root cases name an unknown base, so a script that lost its root check still stops before it makes anything
	run env JAI_WORKTREE_ROOT="$repo/wt" bash "$nw" twelve nosuchbase
	check "refuses a root inside the repository" "$([ "$rc" != 0 ] && [ ! -e "$repo/wt" ] && echo "$out" | grep -q "inside the repository"; echo $?)"

	local drive
	drive="$(abs_path "$t")"
	drive="${drive%%/*}/"
	run env JAI_WORKTREE_ROOT="$drive" bash "$nw" thirteen nosuchbase
	check "refuses a drive root ($drive)" "$([ "$rc" != 0 ] && echo "$out" | grep -q "drive root"; echo $?)"
	run env JAI_WORKTREE_ROOT="${drive}new_worktree_selftest_never_made" bash "$nw" thirteen nosuchbase
	check "refuses a root directly in a drive root" "$([ "$rc" != 0 ] && echo "$out" | grep -q "drive root"; echo $?)"

	git -C "$repo" worktree list --porcelain | grep -c "^worktree " > "$t/count"
	check "made exactly the five worktrees it was asked for (beside the main checkout and the gone one)" "$([ "$(cat "$t/count")" = 7 ]; echo $?)"

	if [ "$bad" = 0 ]; then echo "SELFTEST GREEN ($pass checks)"; return 0; fi
	echo "SELFTEST RED ($bad of $((pass + bad)) checks failed)"
	return 1
}

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
BASE="${BASE:-master}"

COMMON="$(git -C "$(dirname "$SCRIPT")" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)" || fail "not inside a git repository: $SCRIPT"
[ "$(basename "$COMMON")" = .git ] || fail "the repository is not a checkout with a .git folder: $COMMON"
REPO="$(abs_path "$(dirname "$COMMON")")"

case "$NAME" in */* | *\\*) fail "the name names one directory, so it holds no slash: $NAME" ;; esac
git check-ref-format --branch "$NAME" > /dev/null 2>&1 || fail "not a valid branch name: $NAME"

ROOT_IN="${JAI_WORKTREE_ROOT:-$(dirname "$REPO")/JaiScriptWorktrees}"
[ "$ROOT_IN" = / ] && fail "the worktree root is a drive root: $ROOT_IN"
ROOT="$(abs_path "$ROOT_IN")"
ROOT="${ROOT%/}"
case "$ROOT" in "" | [A-Za-z]:) fail "the worktree root is a drive root: $ROOT_IN" ;; esac
case "$(dirname "$ROOT")" in / | [A-Za-z]: | [A-Za-z]:/) fail "the worktree root sits directly in a drive root: $ROOT (put it in a folder, e.g. $(dirname "$REPO")/JaiScriptWorktrees)" ;; esac
case "$(path_key "$ROOT")/" in "$(path_key "$REPO")/"*) fail "the worktree root is inside the repository: $ROOT" ;; esac

BASE_REF="${BASE%%[~^@]*}"
case "$(git -C "$REPO" rev-parse --symbolic-full-name "$BASE_REF" 2>/dev/null)" in
	refs/remotes/*) fail "the base $BASE is a remote-tracking ref; base on a local branch (origin/* can be far behind local master)" ;;
esac
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

[ -d "$ROOT" ] || { mkdir -p "$ROOT" || fail "could not make the root $ROOT"; echo "made the root $ROOT"; }
if ! LOG="$(git -C "$REPO" worktree add --quiet -b "$NAME" "$DIR" "$BASE_SHA" 2>&1)"; then
	echo "$LOG" >&2
	fail "git worktree add failed"
fi
[ "$SCRATCH" = 1 ] && { mkdir "$SCRATCH_DIR" || fail "the worktree is made, but not $SCRATCH_DIR"; }

TIP_NOW="$(git -C "$REPO" rev-parse --verify --quiet "$BASE^{commit}")"
MERGE_BASE="$(git -C "$REPO" merge-base "$NAME" "$BASE")"
[ "$MERGE_BASE" = "$TIP_NOW" ] || fail "made $DIR at $BASE_SHA, but $BASE is now $TIP_NOW (merge-base $MERGE_BASE): it moved meanwhile; rebase the branch or make it again"

echo "worktree: $DIR"
echo "branch:   $NAME"
echo "base:     $BASE $BASE_SHA $(git -C "$REPO" log -1 --format=%s "$BASE_SHA")"
[ "$SCRATCH" = 1 ] && echo "scratch:  $SCRATCH_DIR"
echo "build-debug (from a Visual Studio developer prompt, VsDevCmd.bat -arch=x64):"
echo "  cd /d \"$DIR/Source/JaiScript\" && cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug"
