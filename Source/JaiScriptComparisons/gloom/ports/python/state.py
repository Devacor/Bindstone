"""Persistent globals that must exist before anything else loads.

G (the one world object) is created in gloom.boot; RNG is the seeded
xorshift64* and the ONLY randomness allowed. Modules do `import state` and
read `state.G` at call time (never from-import: these rebind).
"""

G = None
RNG = None

# host globals (set by gloom.py before boot)
HOST_SEED = 666
HOST_SMOKE = False
HOST_TICKS = 2000
HOST_WORKERS = 0
HOST_GOD = False
HOST_BACKEND = "python"
HOST_PIX = 1        # 0 half, 1 quad, 2 sext
HOST_LOOK = "reference"   # "reference" | "showcase" (gloom.py resolves the default)

# the look gloom_boot picked: HOST_LOOK == "showcase"
SHOWCASE = False

# host services (gloom.py installs the real ones; key_down is smoke/bench-gated)
key_down = lambda name: False
host_log = print
