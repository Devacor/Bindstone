# LuaJIT 2.1 JIT port

These overrides preserve the Lua 5.4 port's algorithm and evaluation order.
Other modules are loaded from the common Lua port.

- Floor division uses math.floor(a / b) on the game's bounded numeric values.
- Texture bit operations use LuaJIT JIT's bit module.
- Two-argument angle calculation uses math.atan2.
- Rng.state() returns exact uint64 cdata assembled from two 32-bit halves of
  the shared C++ RNG state. It never passes the full state through a double.
- FNV-1a splits its multiply into exact double-sized terms before reduction
  modulo 2^32. Both halves of the 64-bit RNG state participate.
- The JIT stays enabled with its default optimization settings. No game kernel
  is moved into a native helper.

The JIT is a technique ceiling for comparison, not a shipping target for
platforms that forbid runtime code generation (iOS). The comparison runner and
its placement controls are described in [../README.md](../README.md).
