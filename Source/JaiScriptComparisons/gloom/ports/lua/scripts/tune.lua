-- The showcase look's tuning: light, fog, camera feel. Every value here is
-- read when the look builds its tables, or every frame. Nothing here touches
-- the simulation: STATE_HASH never moves.

-- how much of a floor's mood light reaches a surface with no lamp over it
AMBIENT = 0.34

-- lamps hang over every few tiles of open floor and light a pool beneath them
LAMP_RADIUS = 3.2
LAMP_POWER = 1.0
LAMP_SPACING = 5            -- no two lamps closer than this many tiles

-- the muzzle flash, projectiles and blasts carry their own light
MUZZLE_RADIUS = 4.6
SHOT_RADIUS = 2.6
BLAST_RADIUS = 4.2
LIGHT_GAIN = 1.35           -- how bright a fully lit texel gets

-- light channels: lamp (the floor's own mood light), fire, hex, acid, red lock, blue lock; [channel + 1]
LIGHT_COLORS = {
	color(1.0, 0.86, 0.62),
	color(1.0, 0.56, 0.18),
	color(0.36, 0.86, 1.0),
	color(0.46, 1.0, 0.36),
	color(1.0, 0.18, 0.12),
	color(0.22, 0.42, 1.0)
}

-- camera feel
BOB_PIXELS = 1.6            -- view bob while walking, in pixel rows
SHAKE = 1.0                 -- screen shake strength: hits, blasts, the scattergun
VIGNETTE = 0.55             -- how dark the view's corners get
