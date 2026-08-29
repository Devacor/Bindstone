"""Balance sampling on the real simulation: every two-element composition (four buildings of each)
against every other, run headless through `Bindstone.exe -arena`, scored by leaks.

    python Tools/Balance/arena.py [--seeds N] [--budget 8] [--seconds 150] [--workers 16] [--elements life,decay,...]
    python Tools/Balance/arena.py --report            # rebuild the charts from Tools/Balance/arena_results.jsonl

A leak is a creature reaching the enemy well (one point of well health). With a spawn budget per
building the waves are bounded, so the stronger side's survivors get through; with unlimited
spawns the front line holds forever and nothing leaks. Score for a matchup, from the left side's
view: leaks inflicted - leaks conceded. Output: arena_results.jsonl (one match per line) and
RESULTS.html (heatmap of every matchup, ranking of compositions and elements, per-element
bar charts) built with inline SVG, no dependencies.
"""
import argparse, itertools, json, os, subprocess, sys, statistics
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE = os.path.join(ROOT, "Builds", "Windows", "x64", "Release", "Bindstone.exe")
HERE = os.path.dirname(os.path.abspath(__file__))
RESULTS = os.path.join(HERE, "arena_results.jsonl")
REPORT = os.path.join(HERE, "RESULTS.html")
ELEMENTS = ["life", "decay", "void", "earth", "fire", "water", "air", "lightning"]

def compositions(elements):
    return [",".join(pair) for pair in itertools.combinations(elements, 2)]

def run_match(left, right, seconds, seed, budget):
    proc = subprocess.run([EXE, "-arena", left, right, str(seconds), str(seed), str(budget)], cwd=ROOT, capture_output=True, text=True, errors="replace")
    for line in reversed(proc.stdout.splitlines()):
        if line.startswith('{"arena"'):
            record = json.loads(line)
            record["stderrErrors"] = sum(1 for l in proc.stderr.splitlines() if "Script callback failed" in l)
            return record
    return {"arena": 0, "left": left, "right": right, "seed": seed, "failed": True, "tail": proc.stdout[-300:]}

def sample(args):
    comps = compositions(args.elements)
    jobs = [(l, r, seed) for l in comps for r in comps for seed in range(1, args.seeds + 1)]
    done = set()
    if os.path.exists(RESULTS) and not args.fresh:
        for line in open(RESULTS, encoding="utf-8"):
            try:
                rec = json.loads(line); done.add((rec["left"], rec["right"], rec["seed"]))
            except Exception:
                pass
    jobs = [j for j in jobs if j not in done]
    print(f"{len(comps)} compositions, {len(jobs)} matches to run ({len(done)} already recorded), {args.workers} workers", flush=True)
    with open(RESULTS, "a", encoding="utf-8") as out, ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(run_match, l, r, args.seconds, seed, args.budget): (l, r, seed) for (l, r, seed) in jobs}
        finished = 0
        for future in futures:
            rec = future.result()
            out.write(json.dumps(rec) + "\n"); out.flush()
            finished += 1
            if finished % 25 == 0 or finished == len(jobs):
                print(f"  {finished}/{len(jobs)}", flush=True)

def load_results():
    records = []
    if os.path.exists(RESULTS):
        for line in open(RESULTS, encoding="utf-8"):
            try:
                rec = json.loads(line)
                if rec.get("arena") == 1: records.append(rec)
            except Exception:
                pass
    return records

def analyse(records):
    comps = sorted({r["left"] for r in records} | {r["right"] for r in records}, key=lambda c: (compositions(ELEMENTS).index(c) if c in compositions(ELEMENTS) else 99))
    # score[a][b] = mean (leaks a inflicted on b - leaks b inflicted on a), pooling both seat orders
    pair_scores = {}
    for r in records:
        diff = r["leaksOnRight"] - r["leaksOnLeft"]
        pair_scores.setdefault((r["left"], r["right"]), []).append(diff)
        pair_scores.setdefault((r["right"], r["left"]), []).append(-diff)
    score = {a: {b: (statistics.mean(pair_scores[(a, b)]) if (a, b) in pair_scores else None) for b in comps} for a in comps}
    comp_avg = {a: statistics.mean([v for v in score[a].values() if v is not None]) for a in comps if any(v is not None for v in score[a].values())}
    element_avg = {}
    for e in ELEMENTS:
        vals = [comp_avg[c] for c in comp_avg if e in c.split(",")]
        if vals: element_avg[e] = statistics.mean(vals)
    errors = sum(r.get("scriptErrors", 0) for r in records)
    leaks_total = sum(r["leaksOnLeft"] + r["leaksOnRight"] for r in records)
    decided = sum(1 for r in records if r.get("winner") not in (None, "none"))
    return comps, score, comp_avg, element_avg, dict(matches=len(records), errors=errors, leaks=leaks_total, decided=decided,
        seconds=statistics.mean([r["seconds"] for r in records]) if records else 0, budget=records[0].get("budget") if records else None)

def color(v, vmax):
    if v is None: return "#333"
    t = max(-1.0, min(1.0, v / vmax)) if vmax else 0.0
    if t >= 0: r, g, b = int(255 - 155 * t), int(255 - 40 * t), int(255 - 155 * t)
    else: r, g, b = int(255 - 40 * -t), int(255 - 155 * -t), int(255 - 155 * -t)
    return "#%02x%02x%02x" % (r, g, b)

def heatmap_svg(comps, score):
    n = len(comps); cell = 24; left = 130; top = 130
    vmax = max([abs(v) for row in score.values() for v in row.values() if v is not None] + [1e-9])
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{left + n * cell + 20}" height="{top + n * cell + 20}" font-family="Segoe UI, sans-serif" font-size="10">']
    for j, b in enumerate(comps):
        parts.append(f'<text x="{left + j * cell + cell / 2}" y="{top - 6}" transform="rotate(-60 {left + j * cell + cell / 2},{top - 6})" text-anchor="start">{b}</text>')
    for i, a in enumerate(comps):
        parts.append(f'<text x="{left - 6}" y="{top + i * cell + cell * 0.7}" text-anchor="end">{a}</text>')
        for j, b in enumerate(comps):
            v = score[a][b]
            label = "" if v is None else ("%+.1f" % v if abs(v) >= 0.05 else "0")
            parts.append(f'<rect x="{left + j * cell}" y="{top + i * cell}" width="{cell - 1}" height="{cell - 1}" fill="{color(v, vmax)}"><title>{a} vs {b}: {label}</title></rect>')
            if label and abs(v) >= vmax * 0.35:
                parts.append(f'<text x="{left + j * cell + cell / 2}" y="{top + i * cell + cell * 0.7}" text-anchor="middle" font-size="8" fill="#222">{label}</text>')
    parts.append("</svg>")
    return "\n".join(parts)

def bars_svg(items, title, width=640):
    if not items: return ""
    vmax = max([abs(v) for _, v in items] + [1e-9])
    row = 22; left = 150; scale = (width - left - 60) / 2 / vmax; mid = left + (width - left - 60) / 2
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{len(items) * row + 40}" font-family="Segoe UI, sans-serif" font-size="12">',
             f'<text x="10" y="18" font-weight="bold">{title}</text>', f'<line x1="{mid}" y1="26" x2="{mid}" y2="{len(items) * row + 30}" stroke="#888"/>']
    for k, (name, v) in enumerate(items):
        y = 30 + k * row
        x = mid if v >= 0 else mid + v * scale
        parts.append(f'<text x="{left - 8}" y="{y + 15}" text-anchor="end">{name}</text>')
        parts.append(f'<rect x="{x}" y="{y + 3}" width="{abs(v) * scale}" height="{row - 6}" fill="{"#4caf50" if v >= 0 else "#e53935"}"/>')
        parts.append(f'<text x="{mid + (v * scale) + (6 if v >= 0 else -6)}" y="{y + 15}" text-anchor="{"start" if v >= 0 else "end"}">{v:+.2f}</text>')
    parts.append("</svg>")
    return "\n".join(parts)

def report(records):
    comps, score, comp_avg, element_avg, meta = analyse(records)
    comp_rank = sorted(comp_avg.items(), key=lambda kv: -kv[1])
    element_rank = sorted(element_avg.items(), key=lambda kv: -kv[1])
    html = f"""<!doctype html><html><head><meta charset="utf-8"><title>Bindstone T1 balance</title>
<style>body{{font-family:Segoe UI,sans-serif;margin:24px;color:#222}} h1,h2{{font-weight:600}} .meta{{color:#555}} table{{border-collapse:collapse}} td,th{{padding:2px 8px;border-bottom:1px solid #ddd;text-align:right}} td:first-child,th:first-child{{text-align:left}}</style></head><body>
<h1>T1 balance from headless arena sampling</h1>
<p class="meta">{meta['matches']} matches, budget {meta['budget']} spawns per building, {meta['seconds']:.0f} s each; {meta['leaks']} leaks total, {meta['decided']} matches with a defeated well, {meta['errors']} script errors. Score = leaks inflicted &minus; leaks conceded, averaged over seeds and both seat orders. Green = row beats column.</p>
<h2>Element ranking (mean score of every composition containing the element)</h2>
{bars_svg(element_rank, "element mean score")}
<h2>Composition ranking (mean score against all compositions)</h2>
{bars_svg(comp_rank, "composition mean score", 720)}
<h2>Every matchup</h2>
{heatmap_svg(comps, score)}
<h2>Table</h2>
<table><tr><th>composition</th><th>mean score</th><th>best against</th><th>worst against</th></tr>
"""
    for a, v in comp_rank:
        row = [(b, s) for b, s in score[a].items() if s is not None and b != a]
        best = max(row, key=lambda kv: kv[1]) if row else ("", 0); worst = min(row, key=lambda kv: kv[1]) if row else ("", 0)
        html += f"<tr><td>{a}</td><td>{v:+.2f}</td><td>{best[0]} ({best[1]:+.1f})</td><td>{worst[0]} ({worst[1]:+.1f})</td></tr>\n"
    html += "</table></body></html>\n"
    open(REPORT, "w", encoding="utf-8").write(html)
    print("element ranking:", ", ".join(f"{e} {v:+.2f}" for e, v in element_rank))
    print("top compositions:", ", ".join(f"{c} {v:+.2f}" for c, v in comp_rank[:5]))
    print("bottom compositions:", ", ".join(f"{c} {v:+.2f}" for c, v in comp_rank[-5:]))
    print("meta:", meta)
    print("report:", REPORT)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--seeds", type=int, default=1)
    parser.add_argument("--budget", type=int, default=8)
    parser.add_argument("--seconds", type=float, default=150)
    parser.add_argument("--workers", type=int, default=16)
    parser.add_argument("--elements", default=",".join(ELEMENTS))
    parser.add_argument("--fresh", action="store_true", help="ignore recorded matches")
    parser.add_argument("--report", action="store_true", help="only rebuild the report")
    args = parser.parse_args()
    args.elements = [e for e in args.elements.split(",") if e]
    if not args.report:
        if args.fresh and os.path.exists(RESULTS): os.remove(RESULTS)
        sample(args)
    report(load_results())
