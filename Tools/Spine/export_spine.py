"""Re-export every Spine project in Assets with the installed Spine editor's CLI.

    python Tools/Spine/export_spine.py            # all projects
    python Tools/Spine/export_spine.py Assets/Creatures/Fire_T1/Default/flicker.spine

Runs `Spine.com -u <version> -i project -o tmp -e json+pack`, then installs the result next to the
project under the base name the .bindsnap files reference (unit / building / life ...): the old
json, atlas and the pages the old atlas named are removed, the new json and atlas take the old
names, the new pages keep the exporter's names (the atlas is what references them).

The Spine editor must be installed and licensed (SPINE_COM env var overrides the default path).
"""
import os, sys, glob, shutil, subprocess, re, tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SPINE = os.environ.get("SPINE_COM", r"C:\Program Files\Spine\Spine.com")
VERSION = os.environ.get("SPINE_VERSION", "4.3")

# folder -> base name the scene files reference; anything else uses the project's own name
BASE_NAMES = {"Creatures": "unit", "Buildings": "building"}

def projects(selection):
    if selection:
        return [os.path.relpath(os.path.abspath(p), ROOT) for p in selection]
    found = []
    for pattern in (r"Assets\Creatures\*\*\*.spine", r"Assets\Buildings\*\*\*.spine", r"Assets\Spine\*\*.spine"):
        found += glob.glob(os.path.join(ROOT, pattern))
    return [os.path.relpath(p, ROOT) for p in sorted(found)]

def base_name(project):
    parts = project.replace("\\", "/").split("/")
    return BASE_NAMES.get(parts[1], os.path.splitext(os.path.basename(project))[0]) if len(parts) > 1 else os.path.splitext(os.path.basename(project))[0]

def atlas_pages(path):
    if not os.path.exists(path):
        return []
    lines = [l.strip() for l in open(path, encoding="utf-8", errors="ignore").read().splitlines()]
    return [l for i, l in enumerate(lines) if l.lower().endswith(".png") and (i == 0 or lines[i - 1] == "")]

def export(project):
    folder = os.path.join(ROOT, os.path.dirname(project))
    base = base_name(project)
    out = tempfile.mkdtemp(prefix="spine_export_")
    try:
        result = subprocess.run([SPINE, "-u", VERSION, "-i", os.path.join(ROOT, project), "-o", out, "-e", "json+pack"], capture_output=True, text=True)
        jsons, atlases = glob.glob(os.path.join(out, "*.json")), glob.glob(os.path.join(out, "*.atlas"))
        if result.returncode != 0 or not jsons or not atlases:
            return False, (result.stdout + result.stderr).strip()[-400:]
        old_atlas = os.path.join(folder, base + ".atlas")
        for page in atlas_pages(old_atlas):
            try: os.remove(os.path.join(folder, page))
            except OSError: pass
        for stale in (os.path.join(folder, base + ".json"), old_atlas):
            try: os.remove(stale)
            except OSError: pass
        shutil.copy(jsons[0], os.path.join(folder, base + ".json"))
        shutil.copy(atlases[0], old_atlas)
        for page in atlas_pages(atlases[0]):
            shutil.copy(os.path.join(out, page), os.path.join(folder, page))
        version = re.search(r'"spine":\s*"([^"]+)"', open(os.path.join(folder, base + ".json"), encoding="utf-8").read())
        return True, version.group(1) if version else "?"
    finally:
        shutil.rmtree(out, ignore_errors=True)

if __name__ == "__main__":
    if not os.path.exists(SPINE):
        sys.exit("Spine.com not found at " + SPINE + " (set SPINE_COM)")
    failures = 0
    for project in projects(sys.argv[1:]):
        ok, detail = export(project)
        print(("OK   " if ok else "FAIL ") + project + " -> " + detail)
        failures += 0 if ok else 1
    sys.exit(1 if failures else 0)
