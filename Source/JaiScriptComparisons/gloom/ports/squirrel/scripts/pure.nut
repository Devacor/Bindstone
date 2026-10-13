// The reference's "pure parallel bodies", in their serial-equivalent form
// (ports run serial loops always; outputs are defined identical). The DDA
// reads the tile array directly through TILE_TO_KIND — the reference's
// bit-packed snapshot exists only for its parallel value-isolation contract.

// One wall ray: standard DDA over the tile grid. Returns
// [perp_dist, side, kind, texcol 0..63].
function gloom_ray(posx, posy, rdx, rdy) {
	local g = ::G;
	local mw = g.mw;
	local tiles = g.tiles;
	local t2k = ::TILE_TO_KIND;
	local mx = posx.tointeger();      // truncation; coords stay positive
	local my = posy.tointeger();
	local adx = rdx < 0.0 ? -rdx : rdx;
	local ady = rdy < 0.0 ? -rdy : rdy;
	local ddx = adx < 0.00000001 ? 100000000.0 : 1.0 / adx;
	local ddy = ady < 0.00000001 ? 100000000.0 : 1.0 / ady;
	local stepx = rdx < 0.0 ? -1 : 1;
	local stepy = rdy < 0.0 ? -1 : 1;
	local sdx = rdx < 0.0 ? (posx - mx) * ddx : (mx + 1.0 - posx) * ddx;
	local sdy = rdy < 0.0 ? (posy - my) * ddy : (my + 1.0 - posy) * ddy;
	local side = 0;
	local kind = 1;
	local guard = 0;
	local hit = false;
	while (!hit && guard < 128) {
		if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
		else { sdy += ddy; my += stepy; side = 1; }
		local c = t2k[tiles[my * mw + mx]];
		if (c != 0) {
			hit = true;
			kind = c;
		}
		guard++;
	}
	local pdist = side == 0 ? sdx - ddx : sdy - ddy;
	pdist = pdist < 0.02 ? 0.02 : pdist;
	local wallx = side == 0 ? posy + pdist * rdy : posx + pdist * rdx;
	wallx = wallx - ::floor(wallx);
	local texcol = (wallx * 64.0).tointeger();
	return [pdist, side, kind, texcol];
}

// One particle step at the fixed timestep (TICK baked in as a literal, like
// the reference). Element: [kind, x, y, z, vx, vy, vz, life]
//   kind 0 dead | 1 spark | 2 blood | 3 gib | 4 smoke | 5 flash | 6 hexmote
function gloom_particle(p) {
	local kind = p[0];
	if (kind == 0) { return p; }
	local x = p[1];
	local y = p[2];
	local z = p[3];
	local vx = p[4];
	local vy = p[5];
	local vz = p[6];
	local life = p[7];
	life = life - 1;
	if (life <= 0) { return [0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0]; }
	if (kind == 1) {                       // spark: fast, hard gravity
		vz = vz - 0.30;
		vx = vx * 0.90;
		vy = vy * 0.90;
	} else if (kind == 2) {                // blood: droops
		vz = vz - 0.22;
		vx = vx * 0.94;
		vy = vy * 0.94;
	} else if (kind == 3) {                // gib: ballistic, bounces once-ish
		vz = vz - 0.16;
	} else if (kind == 4) {                // smoke: rises, drifts
		vz = vz * 0.90 + 0.012;
		vx = vx * 0.96;
		vy = vy * 0.96;
	} else if (kind == 5) {                // muzzle/impact flash: hangs still
		vx = vx * 0.5;
		vy = vy * 0.5;
	} else {                               // hexmote: swirly float
		vz = vz * 0.98 + 0.004;
		vx = vx * 0.97;
		vy = vy * 0.97;
	}
	x = x + vx * 0.033333333;
	y = y + vy * 0.033333333;
	z = z + vz * 0.033333333;
	if (z < 0.02) {
		z = 0.02;
		if (kind == 3 && (vz < -0.5 || vz > 0.5)) { vz = 0.0 - vz * 0.45; }
		else { vz = 0.0; }
		if (kind == 1 || kind == 2) { life = life < 4 ? life : 4; }
	}
	return [kind, x, y, z, vx, vy, vz, life];
}
