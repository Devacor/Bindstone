// Persistent globals that must exist before anything else loads.
// The world handle G lives in game.as; RNG is the host-bound seeded rng and
// the ONLY randomness allowed (gloom_boot reseeds it).
Rng RNG(HOST_SEED);
