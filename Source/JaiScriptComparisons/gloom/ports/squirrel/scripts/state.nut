// Persistent globals that must exist before anything else loads.
// The world handle G lives in game.nut; RNG is the host-bound seeded rng and
// the ONLY randomness allowed.
::RNG <- null;
