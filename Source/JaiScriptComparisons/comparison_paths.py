"""Where the comparison runners find JaiScript: $JAISCRIPT_DIR, else the jaiscript/ submodule, else a ../JaiScript tree.

comparison_paths.cmake applies the same rule to the build.
"""
import os
import pathlib

COMPARISONS = pathlib.Path(__file__).resolve().parent


def jaiscript_root():
	configured = os.environ.get("JAISCRIPT_DIR")
	if configured:
		return pathlib.Path(configured).resolve()
	submodule = COMPARISONS / "jaiscript"
	if (submodule / "CMakeLists.txt").is_file():
		return submodule
	return (COMPARISONS.parent / "JaiScript").resolve()


JAISCRIPT = jaiscript_root()
TIMING_LOCK = JAISCRIPT / "tools" / "ci" / "timing_lock.sh"
BUILD = COMPARISONS / "out" / "release"
