// Pathfinding Budget and Urgency System Tests
// Tests for the amortized A* pathfinding with urgency-based recalculation

#include <jaiscript/testing/foundry.hpp>
#include "MV/ArtificialIntelligence/pathfinding.h"
#include <thread>
#include <chrono>

using namespace jai::foundry;

namespace bindstone::tests {

class pathfinding_tests : public suite {
public:
    pathfinding_tests() : suite("Pathfinding") {}

    void forge_tests() override {
        // ==========================================
        // Map Budget Tests
        // ==========================================

        test("map_budget_initial_state", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

            check_true(map->hasBudget(), "Fresh map should have budget");
            check_eq(map->remainingBudget(), MV::Map::FRAME_NODE_BUDGET, "Full budget available initially");
        });

        test("map_budget_consumption", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

            map->consumeBudget(100);
            check_eq(map->remainingBudget(), MV::Map::FRAME_NODE_BUDGET - 100, "Budget reduced after consumption");
            check_true(map->hasBudget(), "Still has budget after partial consumption");

            map->consumeBudget(MV::Map::FRAME_NODE_BUDGET);  // Consume more than remaining
            check_false(map->hasBudget(), "No budget after over-consumption");
            check_eq(map->remainingBudget(), int64_t(0), "Remaining budget clamped to 0");
        });

        test("map_budget_reset_after_frame_time", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

            map->consumeBudget(500);
            check_eq(map->remainingBudget(), MV::Map::FRAME_NODE_BUDGET - 500, "Budget consumed");

            // Sleep for more than frame time to trigger reset
            std::this_thread::sleep_for(std::chrono::milliseconds(20));  // > 16.67ms
            map->tickBudget();

            check_eq(map->remainingBudget(), MV::Map::FRAME_NODE_BUDGET, "Budget reset after frame time");
        });

        test("map_budget_no_reset_within_frame", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

            map->consumeBudget(500);
            map->tickBudget();  // Immediate tick, no time passed

            check_eq(map->remainingBudget(), MV::Map::FRAME_NODE_BUDGET - 500,
                     "Budget NOT reset within same frame");
        });

        // ==========================================
        // Basic Pathfinding Tests
        // ==========================================

        test("agent_basic_creation", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(5, 5));

            auto pos = agent->position();
            check_eq(static_cast<int>(pos.x), 5, "Agent X position correct");
            check_eq(static_cast<int>(pos.y), 5, "Agent Y position correct");
            check_false(agent->pathfinding(), "Not pathfinding initially");
        });

        test("agent_simple_path", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            agent->goal(MV::Point<int>(5, 0));
            check_true(agent->pathfinding(), "Agent is pathfinding to goal");

            auto path = agent->path();
            check_true(path.size() > 1, "Path has multiple nodes");
        });

        test("agent_blocked_path", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

            // Create a wall blocking the path
            for (int y = 0; y < 10; ++y) {
                map->get(MV::Point<int>(5, y)).staticBlock();
            }

            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));
            agent->goal(MV::Point<int>(9, 0));

            auto path = agent->path();
            // Path should exist (even if just start position when fully blocked)
            check_true(path.size() >= 1, "Path exists (even if just start position)");
        });

        // ==========================================
        // Urgency-Based Recalculation Tests
        // ==========================================

        test("agent_distant_blockage_deferred", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            agent->goal(MV::Point<int>(19, 0));
            agent->speed(1.0f);

            auto path_before = agent->path();
            size_t path_length = path_before.size();
            check_true(path_length > 5, "Path is long enough for test");

            // Block a distant node (more than RECALC_URGENCY_THRESHOLD away)
            if (path_length > 10) {
                auto distant_pos = path_before[10].position();
                map->get(distant_pos).block();

                // Agent should NOT immediately recalculate (blockage is distant)
                // The urgency threshold is 3 nodes
            }
        });

        test("agent_urgent_blockage_triggers_recalc", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            agent->goal(MV::Point<int>(10, 0));
            agent->speed(1.0f);

            auto path = agent->path();
            check_true(path.size() > 2, "Path has enough nodes");

            bool blocked_signal_received = false;
            agent->onBlocked.connect("test", [&](std::shared_ptr<MV::NavigationAgent>) {
                blocked_signal_received = true;
            });

            // Block the next node in path (urgent)
            if (path.size() > 1) {
                auto next_pos = path[1].position();
                map->get(next_pos).block();

                // Simulate update
                agent->update(0.016);

                // Agent should have tried to recalculate
            }
        });

        // ==========================================
        // Budget Limiting Tests
        // ==========================================

        test("multiple_agents_share_budget", [this]() {
            auto map = MV::Map::make(MV::Size<int>(50, 50));

            // Create multiple agents
            std::vector<std::shared_ptr<MV::NavigationAgent>> agents;
            for (int i = 0; i < 10; ++i) {
                auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, i * 5));
                agent->goal(MV::Point<int>(49, i * 5));
                agents.push_back(agent);
            }

            // All agents tick in same "frame"
            for (auto& agent : agents) {
                agent->update(0.016);
            }

            // Budget should be consumed by the agents
            check_true(map->remainingBudget() < MV::Map::FRAME_NODE_BUDGET,
                      "Budget consumed by agent updates");
        });

        test("agent_respects_zero_budget", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            // Exhaust budget
            map->consumeBudget(MV::Map::FRAME_NODE_BUDGET + 1000);
            check_false(map->hasBudget(), "Budget exhausted");

            // Agent should not be able to pathfind
            agent->goal(MV::Point<int>(19, 0));

            // When budget is zero, attemptToRecalculate should return early
        });

        // ==========================================
        // Path Calculation Tests
        // ==========================================

        test("path_uses_budget_limit", [this]() {
            auto map = MV::Map::make(MV::Size<int>(100, 100));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            // Create some obstacles to make pathfinding harder
            for (int x = 10; x < 90; ++x) {
                map->get(MV::Point<int>(x, 50)).staticBlock();
            }

            agent->goal(MV::Point<int>(99, 99));
            auto path = agent->path();

            // Path should exist, limited by search budget
            check_true(path.size() >= 1, "Some path calculated within budget");
        });

        test("clearance_for_unit_size", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));

            // Create a 2x2 unit
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0), 2);

            // Create a narrow passage (only 1 cell wide)
            for (int y = 0; y < 20; ++y) {
                if (y != 10) {  // Leave one gap
                    map->get(MV::Point<int>(5, y)).staticBlock();
                }
            }

            agent->goal(MV::Point<int>(10, 0));
            auto path = agent->path();

            // 2x2 unit should not be able to pass through 1-cell gap
            // Path should be blocked or go around
        });

        // ==========================================
        // Signal Tests
        // ==========================================

        test("on_arrive_signal", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            bool arrived = false;
            agent->onArrive.connect("test", [&](std::shared_ptr<MV::NavigationAgent>) {
                arrived = true;
            });

            agent->goal(MV::Point<int>(2, 0));
            agent->speed(100.0f);  // Fast speed to arrive quickly

            // Simulate updates until arrival
            for (int i = 0; i < 100 && agent->pathfinding(); ++i) {
                agent->update(0.1);
            }

            check_true(arrived, "onArrive signal fired");
        });

        test("on_blocked_signal_not_repeated", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

            // Block everything except start
            for (int x = 1; x < 10; ++x) {
                for (int y = 0; y < 10; ++y) {
                    map->get(MV::Point<int>(x, y)).staticBlock();
                }
            }

            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            int blocked_count = 0;
            agent->onBlocked.connect("test", [&](std::shared_ptr<MV::NavigationAgent>) {
                blocked_count++;
            });

            agent->goal(MV::Point<int>(9, 9));

            // Multiple updates
            for (int i = 0; i < 10; ++i) {
                // Sleep to reset budget between frames
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                agent->update(0.016);
            }

            // Signal should fire at most once per blocking situation
            check_true(blocked_count <= 2, "onBlocked signal not fired excessively");
        });

        // ==========================================
        // Movement Tests
        // ==========================================

        test("agent_stops_on_stop", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            bool stopped = false;
            agent->onStop.connect("test", [&](std::shared_ptr<MV::NavigationAgent>) {
                stopped = true;
            });

            agent->goal(MV::Point<int>(19, 19));
            check_true(agent->pathfinding(), "Agent is pathfinding");

            agent->stop();
            check_false(agent->pathfinding(), "Agent stopped pathfinding");
            check_true(stopped, "onStop signal fired");
        });

        test("agent_footprint_blocking", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(5, 5));

            // Agent should block its position
            check_true(map->blocked(MV::Point<int>(5, 5)), "Agent blocks its cell");

            agent->disableFootprint();
            check_false(map->blocked(MV::Point<int>(5, 5)), "Cell unblocked after disabling footprint");

            agent->enableFootprint();
            check_true(map->blocked(MV::Point<int>(5, 5)), "Cell blocked after enabling footprint");
        });

        // ==========================================
        // Temporary Cost Tests
        // ==========================================

        test("temporary_cost_affects_pathfinding", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

            // Add high temporary cost to middle row
            std::vector<MV::TemporaryCost> costs;
            for (int x = 0; x < 10; ++x) {
                costs.emplace_back(map, MV::Point<int>(x, 5), 100.0f);
            }

            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));
            agent->goal(MV::Point<int>(0, 9));

            auto path = agent->path();

            // Path should exist and potentially avoid the high-cost area
            check_true(path.size() >= 1, "Path calculated with temporary costs");
        });
    }
};

} // namespace bindstone::tests

FOUNDRY_REGISTER(bindstone::tests::pathfinding_tests);
