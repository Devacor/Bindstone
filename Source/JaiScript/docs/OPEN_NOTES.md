# Open design notes (internal)

Internal file — unresolved design tensions and current thinking, moved off the public docs
site (`site/`), where nothing may read as uncertain or in-progress. Uncertainty is welcome
here. Historical context: the docs were written from the test suite, and every behavior that
looked accidental was dragged to a ruling — fixed on both backends, or pinned as deliberate
and documented in its chapter as plain fact (the `new` keyword, access control, `include` as
an expression, `use_count` accounting, class-typed operator returns all resolved that way).
What remains below is different in kind: places where two good principles pull in opposite
directions and the language has, for now, picked a side. Each entry states the question, the
tension, our current thinking, and what would change our mind.

## Should string mutators return new strings instead of mutating in place?

```jaiscript
auto name = "jai";
auto upper = name.to_upper();
print("name={} upper={}", name, upper);
// output: name=JAI upper=JAI
```

**The tension.** `to_upper`, `trim`, `replace_all`, and the rest of the mutator camp modify
the receiver and return it — which makes chained pipelines (`s.trim().to_lower()`)
allocation-free and fast, but violates the expectation nearly every other scripting language
has trained into people: that string methods return new strings. The result above surprises
everyone exactly once.

**Current thinking.** Value semantics contains the blast radius: strings never alias at a
distance, so the mutation is strictly local to the variable you called the method on — the
surprise cannot travel. The docs shout about it everywhere strings appear, and the escape is
one copy away. So the perf-leaning choice stands.

**What would change our mind.** Real users tripped by it in numbers. The fix
(value-returning methods plus explicit `_in_place` variants) would be a breaking change to
every shipped script, so the bar is deliberately high — but "everyone gets bitten once"
compounding into "everyone gets bitten repeatedly" would clear it.

## Are bitwise compound assignments worth their weight?

**The tension.** `f |= mask;` does not parse — and that is deliberate, not a gap. C++ muscle
memory says the operator table should be complete; the counter-argument is that every
compound form is permanent surface across the parser, two backends, the static checker, and
the differential fuzzer, purchased to save four characters over `f = f | mask;`.

**Current thinking.** Spell it out. The strongest evidence so far: JaiDOOM — a codebase made
almost entirely of flag words, bit masks, and packed fields — was written without them and
didn't miss them.

**What would change our mind.** That same evidence pointing the other way: flag-heavy
scripts in the wild where the long spelling measurably hurts readability, or a steady stream
of users hitting the parse error and filing it as a bug. It's the cheapest entry in this
file to reverse; it stays out because nothing yet has argued it in.

## How far should type annotations change memory layout?

```jaiscript
array<int> pix = [12, 34, 56];   // packed int storage; the math on top is int64
pix[1] = pix[1] * 2;
print("{}", pix[1]);

class Vec { float x = 0.0; float y = 0.0; }   // dynamic object layout today
auto v = Vec();
v.x = 1.5;
print("{}", v.x);
// output: 68
//         1.500000
```

**The tension.** `array<int>` already gets packed storage — same semantics, same errors, a
fraction of the memory traffic. The obvious next step is a class whose fields are all typed
getting flat, fixed-offset storage, C-struct style. But object layout is precisely the thing
hot reload has to migrate, reflection has to walk, and `var` fields get to ignore — the
dynamic layout isn't laziness, it's what makes "redefine the class mid-session and live
instances survive" cheap to guarantee.

**Current thinking.** The typed-array playbook is the model: storage specialization must be
invisible to behavior — opt-in by annotation, identical semantics, identical error text,
both backends — and there will be no separate `struct` keyword splitting the language in
two. A provably all-typed class earning flat storage under those constraints is attractive;
a fast path that costs reload or parity is not.

**What would change our mind.** In either direction: a design that preserves instance
migration and two-backend parity under flat layout makes it a straight win; proof that it
can't be preserved keeps layout a container-only trick permanently.

## Where should the line sit for parallel admission?

**The tension.** `parallel_for` and `parallel_transform` run script bodies on worker threads
with a hard guarantee: deterministic results at *any* worker count, no data races, no locks
exposed to script. The guarantee is only as good as what the engine can prove about the body
— so some shapes (value closures, disjoint element writes) are admitted to workers, and
shapes it cannot prove safe run serially instead of running wrong. Every widening of the
proof buys expressiveness; every widening is also new surface where the guarantee has to
hold.

**Current thinking.** Prove-or-serial, permanently: the answer to "can I do X in a parallel
body?" evolves by strengthening proofs, never by weakening the guarantee. A body that falls
back to serial is a performance note; a body that races is a broken language. GLOOM's ray
and particle pipelines run under this regime today and stay hash-identical from one worker
to eight.

**What would change our mind.** Not the guarantee — that's settled. What's genuinely open is
whether authors should get an annotation to *extend* the proof ("this function is
parallel-safe, hold me to it") with the engine verifying the claim, or whether admission
must stay fully automatic. Automatic is safer; annotated is how the remaining serial
fallbacks likely die. Real scripts blocked on the automatic prover would push us toward the
annotation.

## Will there be a JIT?

**The tension.** The bytecode VM keeps getting faster — fused superinstructions, typed
storage, cheap call frames — but LuaJIT-class wall clock almost certainly requires emitting
machine code. A JIT buys speed and costs nearly everything else this project treats as
load-bearing: portability, platforms where writable-executable memory is forbidden,
debuggability, and above all the two-implementations-one-spec contract — a JIT is a third
implementation that must also agree byte-for-byte, including error text, or the executable
spec stops being one.

**Current thinking.** Exhaust the VM first. The GLOOM comparison exists precisely to keep
this honest: it says where the time goes, stage by stage, against four other runtimes on
identical work. As long as VM-level structure keeps converting those diagnoses into wins,
machine code stays unjustified complexity. If a JIT ever lands, it joins the parity contract
as a full citizen — same tests, same fuzzer, same byte-for-byte bar — not as a fast mode
with asterisks.

**What would change our mind.** A measured VM ceiling: a real game (not a microbenchmark)
that still needs headroom after the VM's structural ideas are spent. Until both halves of
that sentence are true, no JIT.

## Is a byte the right atom for strings?

```jaiscript
auto s = "\xC3\xA9";        // utf-8 for é
print("len={}", s.length());
print("b0={} b1={}", s[0] + 0, s[1] + 0);
// output: len=2
//         b0=195 b1=169
```

**The tension.** JaiScript strings are byte strings: `s[i]` is a byte, `char` is 0..255,
`length()` counts bytes. That choice is why a WAD parser, a binary writer, and a
terminal-escape renderer are all comfortable pure-script territory — JaiDOOM reads id
Software's binary format with nothing but `s[i] + 0`. But it means one é is two of whatever
`length()` counts, and code that slices user-facing text by index can split a code point.

**Current thinking.** Bytes, deliberately. Encoding-aware behavior hidden inside core string
semantics is where scripting languages go to accumulate surprises; utf-8 stays a convention
the program owns (the example games build their own utf-8 output, in script, without
ceremony). If text-aware operations earn their place, they arrive as explicit, named library
surface — code-point iteration you ask for — never as a reinterpretation of what `s[i]`
means.

**What would change our mind.** Nothing about the byte layer — it's load-bearing. The open
half is the library: an embedder shipping real user-facing text manipulation in scripts
(editors, chat, localization) is the forcing function that would prioritize a proper utf-8
helper surface, and would tell us what it needs to contain.
