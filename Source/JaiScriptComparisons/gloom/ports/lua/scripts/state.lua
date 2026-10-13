-- Persistent globals that must exist before anything else loads.
-- G (the one world table) is created in gloom_boot; RNG is the host-bound
-- seeded rng and the ONLY randomness allowed.
RNG = nil
G = nil
