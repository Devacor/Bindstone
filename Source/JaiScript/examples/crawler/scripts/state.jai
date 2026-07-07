// Persistent globals. This file is NOT re-executed by dev hot reload ('R'),
// so the running game survives while every other file is redefined under it.
//
// Coroutine handles are reference-semantic now (copies share the live
// coroutine), so they live wherever they belong: the boss handle sits in a
// Game FIELD (G.boss_co); only truly-global coroutines stay here.
var G = null;         // the Game instance
var RNG = null;       // the seeded Rng (host-bound class); sole source of randomness
var PILOT_CO = null;  // deterministic autopilot key generator (--smoke)
