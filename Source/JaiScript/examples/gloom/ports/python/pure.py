"""The reference's PURE parallel bodies, as plain serial Python (PORTING.md:
ports always run the serial-equivalent loops; outputs are defined identical).
The DDA math is a line-for-line transliteration so the float stream is
bit-exact; the renderer inlines its own copy of the same walk."""

from math import floor

import state


def gloom_ray(posx, posy, rdx, rdy):
	"""One wall ray over the render-kind grid (the serial helper for hitscan
	and tracer garnish). Returns (pdist, side, kind, texcol 0..63)."""
	g = state.G
	mw = g.mw
	mk = g.mapkind
	mx = floor(posx)                 # coords stay positive in-map
	my = floor(posy)
	adx = -rdx if rdx < 0.0 else rdx
	ady = -rdy if rdy < 0.0 else rdy
	ddx = 100000000.0 if adx < 0.00000001 else 1.0 / adx
	ddy = 100000000.0 if ady < 0.00000001 else 1.0 / ady
	if rdx < 0.0:
		stepx = -1
		sdx = (posx - mx) * ddx
	else:
		stepx = 1
		sdx = (mx + 1.0 - posx) * ddx
	if rdy < 0.0:
		stepy = -1
		sdy = (posy - my) * ddy
	else:
		stepy = 1
		sdy = (my + 1.0 - posy) * ddy
	side = 0
	kind = 1
	guard = 0
	while guard < 128:
		if sdx < sdy:
			sdx += ddx
			mx += stepx
			side = 0
		else:
			sdy += ddy
			my += stepy
			side = 1
		c = mk[my * mw + mx]
		if c != 0:
			kind = c
			break
		guard += 1
	pdist = sdx - ddx if side == 0 else sdy - ddy
	if pdist < 0.02:
		pdist = 0.02
	wallx = posy + pdist * rdy if side == 0 else posx + pdist * rdx
	wallx -= floor(wallx)
	texcol = floor(wallx * 64.0)
	return pdist, side, kind, texcol


def gloom_particle(p):
	"""One particle step at the fixed timestep; mutates the slot list in place
	(same numbers as the reference's rebuilt value records, no allocation).
	Slot: [kind, x, y, z, vx, vy, vz, life]."""
	kind = p[0]
	if kind == 0:
		return
	life = p[7] - 1
	if life <= 0:
		p[0] = 0
		p[1] = 0.0
		p[2] = 0.0
		p[3] = 0.0
		p[4] = 0.0
		p[5] = 0.0
		p[6] = 0.0
		p[7] = 0
		return
	vx = p[4]
	vy = p[5]
	vz = p[6]
	if kind == 1:                      # spark: fast, hard gravity
		vz -= 0.30
		vx *= 0.90
		vy *= 0.90
	elif kind == 2:                    # blood: droops
		vz -= 0.22
		vx *= 0.94
		vy *= 0.94
	elif kind == 3:                    # gib: ballistic, bounces once-ish
		vz -= 0.16
	elif kind == 4:                    # smoke: rises, drifts
		vz = vz * 0.90 + 0.012
		vx *= 0.96
		vy *= 0.96
	elif kind == 5:                    # muzzle/impact flash: hangs still
		vx *= 0.5
		vy *= 0.5
	else:                              # hexmote: swirly float
		vz = vz * 0.98 + 0.004
		vx *= 0.97
		vy *= 0.97
	x = p[1] + vx * 0.033333333
	y = p[2] + vy * 0.033333333
	z = p[3] + vz * 0.033333333
	if z < 0.02:
		z = 0.02
		if kind == 3 and (vz < -0.5 or vz > 0.5):
			vz = 0.0 - vz * 0.45
		else:
			vz = 0.0
		if kind == 1 or kind == 2:
			if life >= 4:
				life = 4
	p[1] = x
	p[2] = y
	p[3] = z
	p[4] = vx
	p[5] = vy
	p[6] = vz
	p[7] = life
