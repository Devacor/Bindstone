-- GLOOM entry point (Lua 5.4 / sol2 port). The host adapter runs this file
-- (package.path already points at the scripts dir), then calls gloom_boot(w,
-- h) once and gloom_frame(dt, key, fps, ms_sim, ms_draw) per frame; --smoke
-- additionally calls gloom_state_hash() / gloom_summary().
-- NOTE: the title-screen tagline is kept byte-identical to the reference so
-- the frame-stream hash stays comparable across ports.

require("state")
require("util")
require("data")
require("defs")
require("maps")
require("pure")
require("particles")
require("enemies")
require("combat")
require("sim")
require("render")
require("hud")
require("game")

FORCE_PILOT = false

function gloom_force_autopilot() FORCE_PILOT = true end

function gloom_boot(w, h)
	RNG = Rng.new(HOST_SEED)
	if HOST_PIX == 0 then PIXW = 1; PIXH = 2
	elseif HOST_PIX == 2 then PIXW = 2; PIXH = 3
	else PIXW = 2; PIXH = 2 end
	parse_arts()
	validate_all_maps()
	G = Game.new(w, h)
	init_render()
	G.autopilot = HOST_SMOKE or FORCE_PILOT
end

-- dt accumulates into fixed TICKs (smoke feeds exactly TICK: one tick/frame).
-- The edge key is consumed by the first tick of the frame.
function gloom_frame(dt, key, fps, ms_sim, ms_draw)
	local g = G
	g.frame_key = key
	g.accum = g.accum + dt
	local steps = 0
	while g.accum >= TICK - 0.0000001 and steps < 3 do
		g.accum = g.accum - TICK
		g:run_tick()
		g.frame_key = ""
		steps = steps + 1
	end
	if steps == 3 then g.accum = 0.0 end   -- stall recovery: never spiral
	return render_frame(fps, ms_sim, ms_draw)
end

function gloom_state_hash() return G.hash end

function gloom_wants_quit() return G.quit end

function gloom_summary()
	local g = G
	host_log("")
	host_log("=== GLOOM RESULT ===")
	host_log("backend: " .. HOST_BACKEND .. "  seed: " .. HOST_SEED ..
		"  workers: " .. HOST_WORKERS .. "  ticks: " .. g.tick)
	host_log("mode: " .. g.mode .. "  map: " .. g.map_name .. "  deaths: " .. g.deaths)
	host_log("hp: " .. g.hp .. "  armor: " .. g.armor ..
		"  ammo: " .. g.ammo[1] .. "/" .. g.ammo[2] .. "/" .. g.ammo[3])
	host_log("map kills: " .. g.kills .. "/" .. g.kill_total ..
		"  secrets: " .. g.secrets .. "/" .. g.secret_total)
	host_log("episode kills: " .. g.ep_kills .. "/" .. g.ep_kill_total ..
		"  secrets: " .. g.ep_secrets .. "/" .. g.ep_secret_total)
	host_log("warden down: " .. tostring(g.warden_down))
	host_log("STATE_HASH: " .. g.hash)
end
