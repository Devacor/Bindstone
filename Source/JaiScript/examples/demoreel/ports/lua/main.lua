-- demoreel (Lua 5.4 port) entry points. The adapter binds Rng, host_log and ESC,
-- sets package.path to this folder and runs this file; the host drives demo_*.

require("util")
require("reel")
require("plasma")
require("starfield")
require("tunnel")
require("donut")
require("julia")
require("lava")
require("pipes")
require("fire")
require("sand")
require("boids")
require("kinstein")
require("voxelspace")
require("marcher")
require("cloth")
require("chrome")
require("skull")
require("finale")

RESET = ESC .. "[0m"
gfx = Gfx.new()
reel = nil

function demo_boot(w, h, seed, backend, smoke, hud_on)
	reel = Reel.new(w, h, seed, backend, smoke, hud_on)
end

function demo_frame(t, dt, key, fps, ms_sim, ms_draw, ms_other)
	return reel:frame(t, dt, key, fps, ms_sim, ms_draw, ms_other)
end

function demo_scene_index() return reel.cur_idx end
function demo_scene_time() return reel.scene_t end
function demo_scene_count() return reel.nscenes end
function demo_scene_name() return reel.cur.name end
function demo_jump(idx, at_t) reel:jump(idx, at_t) end
function demo_reload() reel:reload_current() end
