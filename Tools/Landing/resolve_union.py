# Resolve every conflict block of the ledger as the union (ours then theirs), except that a ledger row (`| N | ...`) already
# present on our side or elsewhere in the file keeps our version: the cherry-picked ledger commits carry rows in their
# pre-landing state, and master's landed state wins. usage: resolve_union.py <file>
import re, sys
p = sys.argv[1]
raw = open(p, "rb").read(); crlf = b"\r\n" in raw
t = raw.decode("utf-8").replace("\r\n", "\n")
pat = re.compile(r"<<<<<<< [^\n]*\n(.*?)(?:\|\|\|\|\|\|\| [^\n]*\n.*?)?=======\n(.*?)>>>>>>> [^\n]*\n", re.S)
row_re = re.compile(r"^\| ([0-9]+(?:\.[0-9]+)*[a-z]?) \| ")
# the rows the file already has outside the conflicts' theirs parts (ours parts included)
outside = pat.sub(lambda m: m.group(1), t)
present = set()
for line in outside.split("\n"):
    m = row_re.match(line)
    if m: present.add(m.group(1))
n = 0; dropped = 0
def repl(m):
    global n, dropped; n += 1
    kept = []
    for line in m.group(2).split("\n"):
        r = row_re.match(line)
        if r and r.group(1) in present:
            dropped += 1; continue
        kept.append(line)
    theirs = "\n".join(kept)
    return m.group(1) + theirs
t = pat.sub(repl, t)
assert "<<<<<<<" not in t and ">>>>>>>" not in t, "unresolved markers remain"
open(p, "wb").write((t.replace("\n", "\r\n") if crlf else t).encode("utf-8"))
print(f"{p}: {n} conflict block(s) resolved as the union, {dropped} row(s) already present kept in our version")
