-- FINALE: the word JAISCRIPT ignites out of a wall of fire and burns in rolling
-- rainbow flame. It IS the fire scene (a subclass): the letters are a heat-source
-- mask stamped from the pixel font at double size.

Finale = class(Fire)

function Finale:init()
	Fire.init(self)
	self.cap_t = 13.0
end

function Finale:build()
	self.name = "FINALE"
	self.handle = "jaiscript forever . rainbow-fire salute"
	self:build_fire()
	local w, h = self.w, self.h
	self.mask = make_grid(w * (h + 2))
	local mask = self.mask
	local text = "JAISCRIPT"
	local s = w >= 98 and 2 or 1
	local pitch = s == 2 and 11 or 6
	local x0 = (w - (#text * pitch - 1)) // 2
	local y0 = h // 2 - 7 * s - 2
	for li = 0, #text - 1 do
		local bits = FONT5[text:sub(li + 1, li + 1)]
		for ry = 0, 7 * s - 1 do
			local b = bits[ry // s + 1]
			for rx = 0, 5 * s - 1 do
				if ((b >> (4 - rx // s)) & 1) == 1 then
					mask[(y0 + ry) * w + x0 + li * pitch + rx + 1] = 1
				end
			end
		end
	end
end

function Finale:update(t, dt)
	self.tt = t
	self.fr = self.fr + 1
	self.rainbow_on = t > 4.5 and 1 or 0             -- the word burns rainbow
	if self.rainbow_on == 1 then self.hue_t = self.hue_t + dt * 3.0 end
	-- the fire wall roars for the reveal, then dies down for the afterglow
	local amp = 42
	amp = t > 10.5 and itrunc(42.0 - (t - 10.5) * 19.0) or amp
	amp = amp < 0 and 0 or amp
	self:seed_rows(amp, amp // 2 + 2)
	self:convect(t > 2.2)
	self:move_sparks(dt, amp > 12)
end

-- the letters burn white-hot over their own flames; a dark one-pixel rim
-- separates them from the fire around them
function Finale:render(grid)
	local b = self:bank()
	local w = self.w
	local n = w * self.h
	local ht = self.heat
	local mk = self.mask
	local lit = self.tt > 2.2
	for i = 1, n do
		local v = ht[i]
		grid[i] = (v > 72 and 72 or v) + b
	end
	if lit then
		local ww = w
		local fr = self.fr
		for i2 = ww, n - ww - 1 do
			local j = i2 + 1
			if mk[j] == 1 then
				grid[j] = b + 64 + ((i2 * 7 + fr) & 7)
			elseif mk[j - 1] == 1 or mk[j + 1] == 1 or mk[j - ww] == 1 or mk[j + ww] == 1 then
				local v2 = ht[j] // 3
				grid[j] = b + v2
			end
		end
	end
	self:draw_sparks(grid, b)
end

function Finale:caption()
	if self.tt > self.cap_t + 1.8 then return "vm + interpreter . byte-identical frames . 16 scenes" end
	if self.tt > self.cap_t then return "every pixel of this reel is jaiscript" end
	return ""
end
