// Pathfinding benchmark: raw A* throughput plus agent-swarm scenarios driven with a fixed sim
// tick (Map::beginTick per tick, so results do not depend on wall-clock budget resets).
//
// Build: build_path.bat [engine lib dir] [out exe]     Run: bench_path.exe [scenario|all] [reps]
// Every scenario is seeded and deterministic; only the timings vary run to run (min of reps).

#include "MV/ArtificialIntelligence/pathfinding.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <functional>

using namespace MV;

namespace {
	struct Rng {
		uint64_t state;
		explicit Rng(uint64_t a_seed) : state(a_seed * 0x9E3779B97F4A7C15ull + 1) {}
		uint64_t next() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
		int below(int a_n) { return static_cast<int>(next() % static_cast<uint64_t>(a_n)); }
		float unit() { return static_cast<float>(next() % 1000000) / 1000000.0f; }
	};

	bool mapsUseCorners = false;   // the game's map has usingCorners = true

	using Clock = std::chrono::steady_clock;
	double msSince(Clock::time_point a_start) {
		return std::chrono::duration<double, std::milli>(Clock::now() - a_start).count();
	}

	std::shared_ptr<Map> makeMap(int a_width, int a_height, float a_blockedFraction, Rng& a_rng) {
		auto map = Map::make(Size<int>(a_width, a_height), mapsUseCorners);
		for (int x = 0; x < a_width; ++x) {
			for (int y = 0; y < a_height; ++y) {
				if (a_rng.unit() < a_blockedFraction) { map->get(Point<int>(x, y)).staticBlock(); }
			}
		}
		return map;
	}

	Point<int> freeCell(const std::shared_ptr<Map>& a_map, Rng& a_rng) {
		auto size = a_map->size();
		for (;;) {
			Point<int> candidate(a_rng.below(size.width), a_rng.below(size.height));
			if (a_map->clearedForSize(candidate, 1)) { return candidate; }
		}
	}

	struct SwarmResult {
		int agents = 0;
		int arrived = 0;
		int stuck = 0;
		int ticksToAllArrived = -1;
		int64_t nodesSearched = 0;
		double totalMs = 0.0;
		double maxTickMs = 0.0;
		double distanceRatio = 0.0;   // travelled / straight-line, arrived agents only
		double remaining = 0.0;       // mean distance still to cover, all agents (0 = everyone arrived)
		double mobility = 0.0;        // share of agent-ticks spent moving while pathfinding (1 = nobody ever waits)
	};

	struct AgentTrack {
		std::shared_ptr<NavigationAgent> agent;
		Point<PointPrecision> goal;
		Point<PointPrecision> last;
		double travelled = 0.0;
		double straight = 0.0;
		int lastMovedTick = 0;
		bool arrived = false;
		int arrivedTick = -1;
		bool leakReported = false;
		double acceptable = 0.0;
	};

	// Idle within two cells of a full goal region is waiting for room, not a stall
	constexpr double CAPACITY_SLACK = 4.0;

	constexpr int STUCK_TICKS = 180;

	SwarmResult runSwarm(const std::shared_ptr<Map>& a_map, std::vector<AgentTrack>& a_tracks, int a_ticks, double a_dt,
	                     const std::function<void(int)>& a_perTick = nullptr) {
		SwarmResult result;
		result.agents = static_cast<int>(a_tracks.size());
		int64_t movingTicks = 0, pathfindingTicks = 0;
		auto start = Clock::now();
		for (int tick = 0; tick < a_ticks; ++tick) {
			auto tickStart = Clock::now();
			a_map->beginTick();
			if (a_perTick) { a_perTick(tick); }
			for (auto& track : a_tracks) {
				if (!track.agent) { continue; }
				try {
					track.agent->update(a_dt);
				} catch (const std::exception& e) {
					auto pos = track.agent->position();
					std::printf("update threw at tick %d agent %d pos (%.2f, %.2f) goal (%.2f, %.2f): %s\n", tick,
						static_cast<int>(&track - a_tracks.data()), pos.x, pos.y, track.goal.x, track.goal.y, e.what());
					throw;
				}
				if (std::getenv("BENCH_PATH_DIAG") && track.agent->queuedForSearch() && !a_map->searchQueueContains(track.agent.get()) && !track.leakReported) {
					track.leakReported = true;
					std::printf("  QUEUE LEAK tick %d agent %d: flagged queued but not in the queue: %s\n", tick, static_cast<int>(&track - a_tracks.data()), track.agent->debugState().c_str());
				}
				auto now = track.agent->position();
				double moved = distance(now, track.last);
				if (track.agent->pathfinding()) { ++pathfindingTicks; if (moved > 0.0001) { ++movingTicks; } }
				if (moved > 0.0001) { track.travelled += moved; track.lastMovedTick = tick; track.last = now; }
				if (!track.arrived && !track.agent->pathfinding()) { track.arrived = true; track.arrivedTick = tick; }
			}
			result.nodesSearched += Map::FRAME_NODE_BUDGET - a_map->remainingBudget();
			result.maxTickMs = std::max(result.maxTickMs, msSince(tickStart));
			if (result.ticksToAllArrived < 0 && std::all_of(a_tracks.begin(), a_tracks.end(), [](const AgentTrack& t) { return !t.agent || t.arrived; })) {
				result.ticksToAllArrived = tick + 1;
			}
		}
		result.totalMs = msSince(start);
		result.mobility = pathfindingTicks > 0 ? static_cast<double>(movingTicks) / static_cast<double>(pathfindingTicks) : 1.0;
		int lastTick = a_ticks - 1;
		double ratioSum = 0.0; int ratioCount = 0;
		for (auto& track : a_tracks) {
			if (!track.agent) { continue; }
			if (track.arrived) {
				++result.arrived;
				if (track.straight > 0.5) { ratioSum += track.travelled / track.straight; ++ratioCount; }
			} else if (lastTick - track.lastMovedTick >= STUCK_TICKS && distance(track.agent->position(), track.goal) > track.acceptable + CAPACITY_SLACK) {
				++result.stuck;
			}
		}
		result.distanceRatio = ratioCount ? ratioSum / ratioCount : 0.0;
		if (std::getenv("BENCH_PATH_DIAG")) {
			for (auto& track : a_tracks) {
				if (!track.agent || track.arrived) { continue; }
				bool stuck = lastTick - track.lastMovedTick >= STUCK_TICKS && distance(track.agent->position(), track.goal) > track.acceptable + CAPACITY_SLACK;
				auto pos = track.agent->position();
				auto here = cast<int>(pos + point(0.5f, 0.5f));
				auto direction = track.goal - pos;
				Point<int> ahead(here.x + (direction.x > 0.5f ? 1 : direction.x < -0.5f ? -1 : 0), here.y + (direction.y > 0.5f ? 1 : direction.y < -0.5f ? -1 : 0));
				auto path = track.agent->path(false);
				auto goalCell = cast<int>(track.goal - point(0.5f, 0.5f));
				std::string nodes;
				for (size_t i = 0; i < path.size() && i < 12; ++i) { nodes += "(" + std::to_string(path[i].position().x) + "," + std::to_string(path[i].position().y) + ")"; }
				std::printf("  %s agent %d size %d at (%.2f, %.2f) goal (%.1f, %.1f) goalCell (%d,%d) clearedForSize=%d clearance=%d path %zu nodes %s, cell ahead (%d,%d) %s\n    %s\n",
					stuck ? "stuck" : "unfinished", static_cast<int>(&track - a_tracks.data()), track.agent->size(), pos.x, pos.y, track.goal.x, track.goal.y,
					goalCell.x, goalCell.y, a_map->clearedForSize(goalCell, track.agent->size()) ? 1 : 0, a_map->inBounds(goalCell) ? a_map->get(goalCell).clearance() : -1, path.size(), nodes.c_str(),
					ahead.x, ahead.y, a_map->agentAt(ahead) ? "held by an agent" : "free", track.agent->debugState().c_str());
			}
		}
		if (const char* grid = std::getenv("BENCH_PATH_GRID")) {
			int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if (std::sscanf(grid, "%d,%d,%d,%d", &x0, &y0, &x1, &y1) == 4) {
				std::printf("  clearance grid x %d..%d, y %d..%d ('#' static, letter = occupant id, digit = clearance)\n", x0, x1, y0, y1);
				for (int y = y0; y <= y1; ++y) {
					std::string row = "  " + std::to_string(y) + (y < 10 ? "  " : " ");
					for (int x = x0; x <= x1; ++x) {
						Point<int> cell(x, y);
						if (!a_map->inBounds(cell)) { row += " "; continue; }
						auto& node = a_map->get(cell);
						if (node.staticallyBlocked()) { row += "#"; }
						else if (auto* occupant = a_map->agentAt(cell)) { row += static_cast<char>('A' + (occupant->size() % 26)); }
						else { int c = node.clearance(); row += static_cast<char>(c >= 10 ? '+' : '0' + c); }
					}
					std::printf("%s\n", row.c_str());
				}
			}
		}
		double remainingSum = 0.0; int remainingCount = 0;
		for (auto& track : a_tracks) {
			if (!track.agent) { continue; }
			++remainingCount;
			if (!track.arrived) { remainingSum += static_cast<double>(distance(track.agent->position(), track.goal)); }
		}
		result.remaining = remainingCount ? remainingSum / remainingCount : 0.0;
		return result;
	}

	AgentTrack makeTrack(const std::shared_ptr<Map>& a_map, const Point<int>& a_start, const Point<int>& a_goal, float a_speed, float a_acceptable, PathMode a_mode = PathMode::search) {
		AgentTrack track;
		track.agent = NavigationAgent::make(a_map, a_start);
		track.agent->speed(a_speed);
		track.agent->goal(a_goal, a_acceptable, a_mode);
		track.goal = track.agent->goal();
		track.last = track.agent->position();
		track.straight = std::max(0.0, static_cast<double>(distance(track.last, track.goal)) - static_cast<double>(a_acceptable));
		track.acceptable = a_acceptable;
		return track;
	}

	void printSwarm(const char* a_name, const SwarmResult& r, int a_ticks) {
		std::string allIn = r.ticksToAllArrived < 0 ? (std::string(">") + std::to_string(a_ticks)) : std::to_string(r.ticksToAllArrived);
		std::printf("| %-18s | %5d | %6.1f%% | %5d | %8s | %10lld | %9.1f | %8.2f | %6.2f | %6.1f | %5.0f%% |\n",
			a_name, r.agents, 100.0 * r.arrived / std::max(1, r.agents), r.stuck, allIn.c_str(),
			static_cast<long long>(r.nodesSearched), r.totalMs, r.maxTickMs, r.distanceRatio, r.remaining, 100.0 * r.mobility);
	}

	// S1: raw search throughput, uncapped and capped, on a 128x128 map with 20% random rock.
	void benchSearch(int a_reps) {
		Rng rng(1);
		auto map = makeMap(128, 128, 0.20f, rng);
		std::vector<std::pair<Point<int>, Point<int>>> pairs;
		for (int i = 0; i < 400; ++i) { pairs.emplace_back(freeCell(map, rng), freeCell(map, rng)); }
		for (int64_t cap : { int64_t(-1), int64_t(200) }) {
			double bestMs = 1e300; int64_t nodes = 0; int complete = 0; double pathLen = 0.0;
			for (int rep = 0; rep < a_reps; ++rep) {
				nodes = 0; complete = 0; pathLen = 0.0;
				auto start = Clock::now();
				for (auto& [from, to] : pairs) {
					Path path(map, from, to, 0.0f, 1, cap);
					auto nodesOut = path.path();
					nodes += path.nodesSearched();
					complete += path.complete() ? 1 : 0;
					pathLen += static_cast<double>(nodesOut.size());
				}
				bestMs = std::min(bestMs, msSince(start));
			}
			std::printf("| search cap=%-5lld | %4zu searches | %10lld nodes | %8.2f ms | %7.1f ns/node | %5.1f%% complete | avg path %.1f |\n",
				static_cast<long long>(cap), pairs.size(), static_cast<long long>(nodes), bestMs, 1e6 * bestMs / std::max<int64_t>(1, nodes),
				100.0 * complete / pairs.size(), pathLen / pairs.size());
		}
	}

	// S2: 64 agents spawned in an 8x8 block march down a lane through a pillar line to one goal.
	SwarmResult benchCrowdLane(int a_ticks) {
		Rng rng(2);
		auto map = Map::make(Size<int>(64, 24), mapsUseCorners);
		for (int y = 0; y < 24; ++y) { if (y % 6 != 3 && y % 6 != 4) { map->get(Point<int>(32, y)).staticBlock(); } }
		std::vector<AgentTrack> tracks;
		for (int i = 0; i < 64; ++i) { tracks.push_back(makeTrack(map, Point<int>(2 + i % 8, 8 + i / 8), Point<int>(60, 12), 6.0f, 6.0f, PathMode::flowField)); }
		return runSwarm(map, tracks, a_ticks, 1.0 / 60.0);
	}

	// S2a: one 3x3 agent alone in the lane — the multi-cell movement sanity check.
	SwarmResult benchSoloBig(int a_ticks) {
		auto map = Map::make(Size<int>(64, 24), mapsUseCorners);
		std::vector<AgentTrack> tracks;
		AgentTrack track;
		track.agent = NavigationAgent::make(map, Point<int>(2, 8), 3);
		track.agent->speed(6.0f);
		track.agent->goal(Point<int>(58, 10), 1.0f, PathMode::flowField);
		track.acceptable = 1.0;
		track.goal = track.agent->goal();
		track.last = track.agent->position();
		track.straight = std::max(0.0, static_cast<double>(distance(track.last, track.goal)) - 1.0);
		tracks.push_back(track);
		return runSwarm(map, tracks, a_ticks, 1.0 / 60.0);
	}

	// S2b: Bindstone-sized units (3x3 footprints): 16 agents in a 4x4 block through a 5-wide gap.
	SwarmResult benchCrowdBig(int a_ticks) {
		auto map = Map::make(Size<int>(64, 24), mapsUseCorners);
		for (int y = 0; y < 24; ++y) { if (y < 9 || y > 13) { map->get(Point<int>(32, y)).staticBlock(); } }
		std::vector<AgentTrack> tracks;
		for (int i = 0; i < 16; ++i) {
			AgentTrack track;
			track.agent = NavigationAgent::make(map, Point<int>(2 + (i % 4) * 3, 5 + (i / 4) * 3), 3);
			track.agent->speed(6.0f);
			track.agent->goal(Point<int>(58, 10), 6.0f, PathMode::flowField);
			track.acceptable = 6.0;
			track.goal = track.agent->goal();
			track.last = track.agent->position();
			track.straight = std::max(0.0, static_cast<double>(distance(track.last, track.goal)) - 6.0);
			tracks.push_back(track);
		}
		return runSwarm(map, tracks, a_ticks, 1.0 / 60.0);
	}

	// S3: 32 agents spawn on the SAME cell one per tick (a building's spawn point under contention).
	SwarmResult benchSpawnContention(int a_ticks) {
		Rng rng(3);
		auto map = makeMap(64, 64, 0.10f, rng);
		Point<int> spawn = freeCell(map, rng);
		std::vector<AgentTrack> tracks(32);
		std::vector<Point<int>> goals;
		for (int i = 0; i < 32; ++i) { goals.push_back(freeCell(map, rng)); }
		return runSwarm(map, tracks, a_ticks, 1.0 / 60.0, [&](int tick) {
			if (tick < 32) { tracks[tick] = makeTrack(map, spawn, goals[tick], 6.0f, 1.0f); }
		});
	}

	// S4: 32 crossing agents on a map where 200 random cells toggle blocked/unblocked every tick.
	SwarmResult benchDynamicReplan(int a_ticks) {
		Rng rng(4);
		auto map = makeMap(64, 64, 0.10f, rng);
		std::vector<AgentTrack> tracks;
		for (int i = 0; i < 32; ++i) { tracks.push_back(makeTrack(map, freeCell(map, rng), freeCell(map, rng), 6.0f, 1.0f)); }
		std::vector<Point<int>> toggled;
		Rng toggleRng(44);
		return runSwarm(map, tracks, a_ticks, 1.0 / 60.0, [&](int) {
			for (auto& cell : toggled) { map->get(cell).unblock(); }
			toggled.clear();
			for (int i = 0; i < 200; ++i) {
				Point<int> cell(toggleRng.below(64), toggleRng.below(64));
				bool underAgent = std::any_of(tracks.begin(), tracks.end(), [&](const AgentTrack& t) { return t.agent && t.agent->overlaps(cell); });
				if (!map->blocked(cell) && !underAgent) { map->get(cell).block(); toggled.push_back(cell); }
			}
		});
	}

	// S6: the game's scale - 250x100 grid, building footprints, 3x3 units spawning in waves of 8
	// every 3 s from two spawn points, all heading for one enemy base region.
	std::shared_ptr<Map> laneMarchMap() {
		auto map = Map::make(Size<int>(250, 100), mapsUseCorners);
		auto building = [&](int a_x, int a_y, int a_w, int a_h) {
			for (int x = a_x; x < a_x + a_w; ++x) { for (int y = a_y; y < a_y + a_h; ++y) { map->get(Point<int>(x, y)).staticBlock(); } }
		};
		building(60, 30, 12, 12); building(60, 58, 12, 12); building(120, 44, 10, 12); building(180, 30, 12, 12); building(180, 58, 12, 12);
		for (int y = 0; y < 100; ++y) { if (y < 20 || y > 79) { map->get(Point<int>(125, y)).staticBlock(); } }
		return map;
	}

	// S2: what a shared field costs on the game's map - one build per (goal, radius, size) until the
	// static map changes, then a 12-step descent per agent per path extension
	void benchFlowField(int a_reps) {
		auto map = laneMarchMap();
		for (int size : { 1, 3 }) {
			double bestBuild = 1e9;
			size_t reachable = 0;
			for (int rep = 0; rep < a_reps; ++rep) {
				map->invalidateFlowFields();
				map->beginTick();
				auto start = Clock::now();
				auto field = map->flowFieldFor(Point<int>(236, 48), 8.0f, size);
				bestBuild = std::min(bestBuild, msSince(start));
				reachable = field->reachableCells();
			}
			auto field = map->flowFieldFor(Point<int>(236, 48), 8.0f, size);
			double bestDescend = 1e9;
			const int descents = 2000;
			size_t nodes = 0;
			for (int rep = 0; rep < a_reps; ++rep) {
				auto start = Clock::now();
				nodes = 0;
				for (int i = 0; i < descents; ++i) { nodes += field->descend(map, Point<int>(2 + (i % 40), 2 + (i % 90)), 12, nullptr).size(); }
				bestDescend = std::min(bestDescend, msSince(start) * 1000.0 / descents);
			}
			std::printf("| field size=%d    | 25000 cells | %6zu reachable | build %7.3f ms | %6.1f KB | descend(12) %6.2f us, %.1f nodes avg |\n", size, reachable, bestBuild, 25000 * sizeof(float) / 1024.0, bestDescend, static_cast<double>(nodes) / descents);
		}
	}

	SwarmResult benchLaneMarch(int a_ticks) {
		auto map = laneMarchMap();
		std::vector<AgentTrack> tracks(48);
		return runSwarm(map, tracks, a_ticks, 1.0 / 60.0, [&](int tick) {
			if (tick % 180 != 0 || tick / 180 >= 6) { return; }
			int wave = tick / 180;
			for (int i = 0; i < 8; ++i) {
				int spawnY = i < 4 ? 28 : 64;
				Point<int> spawn(6 + (i % 4) * 3, spawnY + ((i % 4) % 2) * 3);
				AgentTrack track;
				track.agent = NavigationAgent::make(map, spawn, 3);
				track.agent->speed(6.0f);
				track.agent->goal(Point<int>(236, 48), 8.0f, PathMode::flowField);
				track.acceptable = 8.0;
				track.goal = track.agent->goal();
				track.last = track.agent->position();
				track.straight = std::max(0.0, static_cast<double>(distance(track.last, track.goal)) - 8.0);
				track.lastMovedTick = tick;
				tracks[static_cast<size_t>(wave * 8 + i)] = track;
			}
		});
	}

	// S5: two groups of 16 pass each other in a 3-wide corridor.
	SwarmResult benchCorridorSwap(int a_ticks) {
		auto map = Map::make(Size<int>(48, 3), mapsUseCorners);
		std::vector<AgentTrack> tracks;
		for (int i = 0; i < 16; ++i) {
			tracks.push_back(makeTrack(map, Point<int>(i % 6, i / 6), Point<int>(46, 1), 6.0f, 6.0f, PathMode::flowField));
			tracks.push_back(makeTrack(map, Point<int>(47 - i % 6, 2 - i / 6), Point<int>(1, 1), 6.0f, 6.0f, PathMode::flowField));
		}
		return runSwarm(map, tracks, a_ticks, 1.0 / 60.0);
	}
}

int main(int argc, char** argv) {
	std::string which = argc > 1 ? argv[1] : "all";
	int reps = argc > 2 ? std::atoi(argv[2]) : 5;
	for (int i = 1; i < argc; ++i) { if (std::strcmp(argv[i], "corners") == 0) { mapsUseCorners = true; } }
	if (which == "corners") { which = "all"; }
	std::printf("\n# %s-connected maps\n", mapsUseCorners ? "8" : "4");
	auto wants = [&](const char* a_name) { return which == "all" || which == a_name; };

	if (wants("search")) {
		std::printf("\n## S1 raw search (min of %d reps)\n\n", reps);
		benchSearch(reps);
	}
	if (wants("field")) {
		std::printf("\n## S2 flow field on the 250x100 lane map (min of %d reps)\n\n", reps);
		benchFlowField(reps);
	}

	std::printf("\n## Swarm scenarios (fixed 1/60 tick, min total ms of %d reps; counts from the last rep)\n\n", reps);
	std::printf("| scenario           | agents | arrived | stuck | all-in @ | nodes      | total ms  | max tick | ratio  | remain | mobile |\n");
	std::printf("|--------------------|--------|---------|-------|----------|------------|-----------|----------|--------|--------|--------|\n");
	struct Scenario { const char* name; SwarmResult (*run)(int); int ticks; };
	Scenario scenarios[] = {
		{ "crowd_lane", benchCrowdLane, 1200 },
		{ "solo_big", benchSoloBig, 900 },
		{ "crowd_big", benchCrowdBig, 1200 },
		{ "spawn_contention", benchSpawnContention, 1200 },
		{ "dynamic_replan", benchDynamicReplan, 1200 },
		{ "corridor_swap", benchCorridorSwap, 1500 },
		{ "lane_march", benchLaneMarch, 3600 },
	};
	std::set_terminate([] { std::fprintf(stderr, "TERMINATE (exception escaped a noexcept context)\n"); std::abort(); });
	for (auto& scenario : scenarios) {
		if (!wants(scenario.name)) { continue; }
		SwarmResult best;
		for (int rep = 0; rep < reps; ++rep) {
			std::fprintf(stderr, "[%s] rep %d start\n", scenario.name, rep);
			auto result = scenario.run(scenario.ticks);
			std::fprintf(stderr, "[%s] rep %d ticks done, tearing down\n", scenario.name, rep);
			if (rep == 0 || result.totalMs < best.totalMs) { double keepMax = result.maxTickMs; best = result; best.maxTickMs = keepMax; }
		}
		printSwarm(scenario.name, best, scenario.ticks);
	}
	return 0;
}
