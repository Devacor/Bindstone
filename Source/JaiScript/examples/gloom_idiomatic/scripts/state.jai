// Idiomatic GLOOM — persistent globals that must exist before anything else
// loads. RNG is the host-bound seeded rng and the ONLY randomness allowed.
// (The world handle G lives in game.jai as a typed shared_ptr<Game>.)
var RNG = null;
