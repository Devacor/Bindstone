#!/usr/bin/env python3
"""Cross-language kernel suite runner (dasProfile protocol).

Each cell is the median of --runs samples, every sample its own process; a sample runs the kernel
as many times as fit a 0.5 s budget and reports the per-run time. Samples are interleaved across
implementations (round robin) so drift hits every column alike. The shared timing lock is taken
per kernel. Writes RESULTS_<date>.md beside this file.

    python run.py [--runs 5] [--kernels a,b] [--impls a,b] [--no-lock] [--out FILE]
"""
import argparse, datetime, os, re, statistics, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
BIN = os.path.join(HERE, "bin")
RUNNER = os.path.join(os.environ.get("JAI_BUILD") or os.path.join(ROOT, "build"), "bin", "jaiscript.exe")
HOST = os.path.join(BIN, "jai_kernel_host.exe")
LUA = os.path.join(BIN, "lua_kernel_host.exe")
LUAJIT = os.path.join(BIN, "luajit.exe")
LOCK = os.environ.get("TIMING_LOCK", "")

# kernel id, dasProfile board name
KERNELS = [
    ("fib_recursive", "fibonacci recursive"),
    ("fib_loop", "fibonacci loop"),
    ("primes", "primes loop"),
    ("queen", "queen"),
    ("mandelbrot", "mandelbrot"),
    ("nbodies", "n-bodies"),
    ("spectral_norm", "spectral norm"),
    ("particles", "particles kinematics"),
    ("sha256", "sha256"),
    ("tree", "tree"),
    ("sort", "sort"),
    ("dict", "dictionary"),
    ("exp", "exp loop"),
    ("string2float", "string2float"),
    ("float2string", "float2string"),
    ("interop", "interop host calls"),
]

# dasProfile's published interpreter board (DAS interpreter s, Lua 5.5.1 s), captured 2026-09-13
DAS_LINUX = {  # AMD Ryzen 7 PRO 8700GE, Clang 19, daslang 0.6.4
    "sha256": (0.002509608, 0.06854791), "dictionary": (0.011193277, 0.03877146),
    "n-bodies": (0.00916275, 0.04157742), "spectral norm": (0.023719809, 0.05783218),
    "mandelbrot": (0.001791051, 0.05691692), "exp loop": (0.006507309, 0.02374618),
    "string2float": (0.000608524, 0.00278879), "particles kinematics": (0.00094978, 0.03931226),
    "queen": (0.000771494, 0.00148335), "sort": (0.015449821, 0.0456287),
    "interop host calls": (0.003739501, 0.01535982), "primes loop": (0.026192544, 0.05906063),
    "tree": (0.023616579, 0.0311374), "fibonacci loop": (0.025913418, 0.05367023),
    "float2string": (0.001221966, 0.01325072), "fibonacci recursive": (0.048174447, 0.04874698),
}
DAS_DARWIN = {  # Apple M1 Max, AppleClang 21, daslang 0.6.4
    "sha256": (0.002791575, 0.06882362), "dictionary": (0.014329238, 0.0414324),
    "n-bodies": (0.009183574, 0.0417265), "spectral norm": (0.027047521, 0.06047456),
    "mandelbrot": (0.002049412, 0.06806538), "exp loop": (0.007419278, 0.0217657),
    "string2float": (0.000634234, 0.00256289), "particles kinematics": (0.000855408, 0.04433169),
    "queen": (0.00093346, 0.00140999), "interop host calls": (0.002486771, 0.01240102),
    "primes loop": (0.022037554, 0.07448375), "tree": (0.028921715, 0.034692),
    "sort": (0.015105207, 0.058141), "fibonacci loop": (0.032357732, 0.04650483),
    "float2string": (0.001219265, 0.01571243), "fibonacci recursive": (0.043297862, 0.067173),
}

IMPLS = ["jai_vm_typed", "jai_vm_dyn", "jai_interp_typed", "lua", "luajit_joff", "luajit"]
TITLES = {
    "jai_vm_typed": "Jai VM typed", "jai_vm_dyn": "Jai VM dyn", "jai_interp_typed": "Jai interp typed",
    "jai_interp_dyn": "Jai interp dyn", "lua": "Lua 5.5.1", "luajit_joff": "LuaJIT -joff", "luajit": "LuaJIT",
}


def command(impl, kernel):
    """(argv, cwd) for one sample."""
    if impl.startswith("jai_"):
        backend = "interp" if "_interp_" in impl else "vm"
        flavor = "dyn" if impl.endswith("_dyn") else "typed"
        script = os.path.join(HERE, "jai", f"{kernel}_{flavor}.jai")
        if kernel == "interop":
            return [HOST, f"--backend={backend}", script], os.path.join(HERE, "jai")
        return [RUNNER, "--no-debug", "--no-pause", f"--backend={backend}", script], os.path.join(HERE, "jai")
    lua_dir = os.path.join(HERE, "lua")
    if impl == "lua":
        return [LUA, f"{kernel}.lua"], lua_dir
    script = "sha256_luajit.lua" if kernel == "sha256" else f"{kernel}.lua"
    flags = ["-joff"] if impl == "luajit_joff" else []
    return [LUAJIT] + flags + [script], lua_dir


LINE = re.compile(r'^"([^"]+)",\s*([0-9.eE+-]+),\s*(\d+)')


def sample(impl, kernel):
    argv, cwd = command(impl, kernel)
    p = subprocess.run(argv, cwd=cwd, capture_output=True, text=True, timeout=900)
    secs, reps, checksum = None, None, None
    for line in p.stdout.splitlines():
        m = LINE.match(line.strip())
        if m:
            secs, reps = float(m.group(2)), int(m.group(3))
        elif line.startswith("checksum"):
            checksum = line[len("checksum"):].strip()
    if secs is None:
        err = (p.stderr or p.stdout).strip().splitlines()
        return None, None, "ERROR: " + (err[0] if err else f"exit {p.returncode}")
    return secs, reps, checksum


def take_lock():
    if not LOCK:
        return
    while True:
        try:
            os.mkdir(LOCK)
            return
        except FileExistsError:
            time.sleep(20)


def drop_lock():
    if LOCK:
        try:
            os.rmdir(LOCK)
        except OSError:
            pass


def fmt_ms(s):
    if s is None:
        return "-"
    ms = s * 1000.0
    return f"{ms:.3f}" if ms < 10 else f"{ms:.2f}" if ms < 100 else f"{ms:.1f}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--kernels", default="")
    ap.add_argument("--impls", default=",".join(IMPLS))
    ap.add_argument("--no-lock", action="store_true")
    ap.add_argument("--out", default="")
    args = ap.parse_args()
    global LOCK
    if args.no_lock:
        LOCK = ""
    kernels = [k for k in KERNELS if not args.kernels or k[0] in args.kernels.split(",")]
    impls = args.impls.split(",")

    results = {}
    for kernel, board in kernels:
        take_lock()
        try:
            per = {i: [] for i in impls}
            sums = {i: set() for i in impls}
            for _ in range(args.runs):
                for impl in impls:
                    secs, reps, checksum = sample(impl, kernel)
                    if secs is not None:
                        per[impl].append(secs)
                    sums[impl].add(checksum)
        finally:
            drop_lock()
        row = {}
        for impl in impls:
            xs = sorted(per[impl])
            med = statistics.median(xs) if xs else None
            spread = (xs[-1] - xs[0]) / 2 / med * 100 if xs and med else None
            row[impl] = (med, spread, sorted(s for s in sums[impl] if s is not None) or ["?"])
        results[kernel] = (board, row)
        cells = "  ".join(f"{i}={fmt_ms(row[i][0])}" for i in impls)
        print(f"{kernel:14s} {cells}", flush=True)
        for impl in impls:
            print(f"    {impl:16s} checksum {' | '.join(row[impl][2])}", flush=True)

    write_report(results, impls, args)


def machine():
    try:
        import platform
        cpu = subprocess.run(["powershell", "-NoProfile", "-Command",
                              "(Get-CimInstance Win32_Processor).Name"], capture_output=True, text=True).stdout.strip()
        return f"{cpu}, {platform.system()} {platform.release()}"
    except Exception:
        return "unknown"


def write_report(results, impls, args):
    date = datetime.date.today().isoformat()
    out = args.out or os.path.join(HERE, f"RESULTS_{date}.md")
    has_lua = "lua" in impls
    lines = [f"# Kernel suite results {date}", "",
             f"Machine: {machine()}. Median of {args.runs} processes per cell; each process runs the kernel "
             "as many times as fit a 0.5 s budget and reports the per-run time (dasProfile's protocol). "
             "Times in ms, lower is better; `±` is half the sample range as a share of the median.", ""]
    head = ["kernel"] + [TITLES.get(i, i) for i in impls]
    if has_lua:
        head += [f"{TITLES.get(i, i)} / Lua" for i in impls if i != "lua"]
        head += ["DAS interp est. (Linux)", "DAS interp est. (M1)"]
    lines.append("| " + " | ".join(head) + " |")
    lines.append("| --- |" + " ---: |" * (len(head) - 1))
    for kernel, (board, row) in results.items():
        cells = [kernel]
        for i in impls:
            med, spread, _ = row[i]
            cells.append(f"{fmt_ms(med)} ±{spread:.0f}%" if med else "-")
        if has_lua:
            lua = row["lua"][0]
            for i in impls:
                if i == "lua":
                    continue
                med = row[i][0]
                cells.append(f"{med / lua:.2f}" if med and lua else "-")
            for table in (DAS_LINUX, DAS_DARWIN):
                das, dlua = table.get(board, (None, None))
                cells.append(f"{fmt_ms(das / dlua * lua)} ({das / dlua:.2f})" if das and lua else "-")
        lines.append("| " + " | ".join(cells) + " |")
    lines += ["", "### Checksums", "", "| kernel | " + " | ".join(TITLES.get(i, i) for i in impls) + " |",
              "| --- |" + " --- |" * len(impls)]
    for kernel, (board, row) in results.items():
        lines.append(f"| {kernel} | " + " | ".join(" / ".join(row[i][2]) for i in impls) + " |")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("wrote", out)


if __name__ == "__main__":
    main()
