// Persistent globals. This file is NOT re-executed by dev hot reload ('R'),
// so the running game survives while every other file is redefined under it.
//
// Coroutine handles can ONLY live in plain variables (JaiScript's value
// semantics deep-copy fields/containers, and coroutine handles refuse to
// copy) — so every long-lived coroutine gets a named global slot here.
var G = null;         // the Game instance
var RNG = null;       // the seeded Rng (host-bound class); sole source of randomness
var BOSS_CO = null;   // Vexadrach's multi-phase fight script (floor 10)
var PILOT_CO = null;  // deterministic autopilot key generator (--smoke)
