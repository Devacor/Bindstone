-- demoreel entry points (Lua 5.4 port of scenes/main.jai). The adapter binds Rng, host_log
-- and ESC, points package.path at this folder and runs this file once; a hot reload re-runs
-- every other file, so the globals owned here (RESET, gfx, the reel) persist.
-- reel.lua holds the scene-file contract.

require("util")
require("music")
require("reel")

RESET = ESC .. "[0m"
gfx = Gfx.new()
reel = nil
-- the cell shape: "half" blocks, "sext"ants, or "" for sextants while the grid stays within PIX_BUDGET pixels
PIX_MODE = ""
PIX_BUDGET = 48000

require("julia")
require("globhunter")
require("skull")
require("fluid")

function demo_boot(w, h, seed, backend, smoke, hud_on)
	reel = Reel.new(w, h, seed, backend, smoke, hud_on)
end

function demo_frame(t, dt, key, fps, ms_sim, ms_draw, ms_other)
	return reel:frame(t, dt, key, fps, ms_sim, ms_draw, ms_other)
end

function demo_pixels(mode) PIX_MODE = mode == "auto" and "" or mode end
function demo_scene_index() return reel.cur_idx end
function demo_scene_time() return reel.scene_t end
function demo_jump(idx, at_t) reel:jump(idx, at_t) end
function demo_reload()
	if reel.cur then reel:reload_current() end
end
function demo_scene_count() return reel.nscenes end
function demo_scene_name() return reel.cur.name end
