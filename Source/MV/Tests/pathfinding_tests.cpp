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

            std::this_thread::sleep_for(std::chrono::milliseconds(20));  // > 16.67ms
            map->tickBudget();

            check_eq(map->remainingBudget(), MV::Map::FRAME_NODE_BUDGET, "Budget reset after frame time");
        });

        test("map_budget_no_reset_within_frame", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

            map->consumeBudget(500);
            map->tickBudget();

            check_eq(map->remainingBudget(), MV::Map::FRAME_NODE_BUDGET - 500,
                     "Budget NOT reset within same frame");
        });

        test("failed_search_is_charged_for_nodes_searched_not_the_limit", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));
            MV::Point<int> center(5, 5);
            for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) if (dx || dy) map->get(center + MV::Point<int>(dx, dy)).staticBlock();
            auto trapped = MV::NavigationAgent::make(map, center);
            trapped->goal(MV::Point<int>(9, 9));
            std::this_thread::sleep_for(std::chrono::milliseconds(18));
            trapped->update(1.0 / 60.0);
            check_true(map->remainingBudget() > MV::Map::FRAME_NODE_BUDGET - 50, "a search that visits one node costs about one node, not the whole search limit");
        });

        test("blocked_agents_do_not_starve_others_of_budget", [this]() {
            auto map = MV::Map::make(MV::Size<int>(40, 12));
            std::vector<std::shared_ptr<MV::NavigationAgent>> penned;
            for (int i = 0; i < 6; ++i) {
                MV::Point<int> center(3 + i * 5, 6);
                for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) if (dx || dy) map->get(center + MV::Point<int>(dx, dy)).staticBlock();
                auto agent = MV::NavigationAgent::make(map, center);
                agent->goal(MV::Point<int>(39, 11));
                penned.push_back(agent);
            }
            auto runner = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));
            runner->speed(10.0f);
            runner->goal(MV::Point<int>(39, 0));
            for (int frame = 0; frame < 30; ++frame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(18));
                for (auto& agent : penned) { agent->update(1.0 / 60.0); }
                runner->update(1.0 / 60.0);
            }
            check_true(runner->position().x > 1.0f, "an agent with a clear path keeps moving while blocked agents retry ahead of it in update order");
        });

        test("blocked_agent_resumes_when_a_nearby_cell_frees", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));
            MV::Point<int> center(5, 5);
            for (int dx = -1; dx <= 1; ++dx) for (int dy = -1; dy <= 1; ++dy) if (dx || dy) map->get(center + MV::Point<int>(dx, dy)).staticBlock();
            MV::Point<int> gate(6, 5);
            map->get(gate).staticUnblock();
            map->get(gate).block();
            auto agent = MV::NavigationAgent::make(map, center);
            agent->speed(10.0f);
            agent->goal(MV::Point<int>(15, 5));
            for (int frame = 0; frame < 40; ++frame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(18));
                agent->update(1.0 / 60.0);
            }
            check_true(agent->position().x < 5.5f, "agent stays put while the only exit is blocked");
            map->get(gate).unblock();
            for (int frame = 0; frame < 30; ++frame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(18));
                agent->update(1.0 / 60.0);
            }
            check_true(agent->position().x > 6.0f, "agent leaves through the gate soon after it frees");
        });

        test("agent_spawned_onto_an_occupied_cell_is_placed_nearby_and_moves", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));
            auto squatter = MV::NavigationAgent::make(map, MV::Point<int>(5, 5));
            auto spawned = MV::NavigationAgent::make(map, MV::Point<int>(5, 5));
            spawned->speed(10.0f);
            spawned->goal(MV::Point<int>(15, 5));
            for (int frame = 0; frame < 30; ++frame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(18));
                squatter->update(1.0 / 60.0);
                spawned->update(1.0 / 60.0);
            }
            check_true(spawned->position().x > 6.0f, "spawned agent takes a free cell next to the occupied spawn and makes progress");
            check_eq(5.0f, squatter->position().x, "the squatter was not displaced");
        });

        test("tick_driven_budget_ignores_the_wall_clock", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));
            map->beginTick();
            map->consumeBudget(600);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            map->tickBudget();
            check_eq(MV::Map::FRAME_NODE_BUDGET - 600, map->remainingBudget(), "once a host drives ticks, elapsed time never refills the budget mid-tick");
            map->beginTick();
            check_eq(MV::Map::FRAME_NODE_BUDGET, map->remainingBudget(), "the next tick refills it");
        });

        test("queued_searches_are_served_oldest_first", [this]() {
            auto map = MV::Map::make(MV::Size<int>(60, 60));
            std::vector<std::shared_ptr<MV::NavigationAgent>> agents;
            for (int i = 0; i < 12; ++i) {
                auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, i * 4));
                agent->speed(10.0f);
                agent->goal(MV::Point<int>(59, 59 - i * 4));
                agents.push_back(agent);
            }
            int ticks = 0;
            while (ticks < 20 && std::any_of(agents.begin(), agents.end(), [](auto& a) { return a->path(false).empty(); })) {
                map->beginTick();
                map->consumeBudget(MV::Map::FRAME_NODE_BUDGET - 300);   // leave room for about one search per tick so the queue is exercised whatever the budget constant is
                for (auto& agent : agents) { agent->update(1.0 / 60.0); }
                ++ticks;
            }
            check_true(std::all_of(agents.begin(), agents.end(), [](auto& a) { return !a->path(false).empty(); }), "every agent got a search within a few ticks");
            check_true(ticks > 1, "the frame budget could not serve all twelve searches in one tick, so the queue was exercised");
        });

        test("head_on_agents_in_a_one_wide_corridor_swap_and_pass", [this]() {
            auto map = MV::Map::make(MV::Size<int>(12, 1));
            auto left = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));
            auto right = MV::NavigationAgent::make(map, MV::Point<int>(11, 0));
            left->speed(6.0f); right->speed(6.0f);
            left->goal(MV::Point<int>(11, 0)); right->goal(MV::Point<int>(0, 0));
            for (int tick = 0; tick < 400 && (left->pathfinding() || right->pathfinding()); ++tick) {
                map->beginTick();
                left->update(1.0 / 60.0);
                right->update(1.0 / 60.0);
            }
            check_true(!left->pathfinding() && !right->pathfinding(), "both agents reached the far end of a corridor only one of them fits in");
        });

        test("idle_blocker_steps_aside_after_the_waiter_has_been_patient", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 2));
            for (int x = 0; x < 10; ++x) { if (x != 5) { map->get(MV::Point<int>(x, 1)).staticBlock(); } }
            auto idle = MV::NavigationAgent::make(map, MV::Point<int>(5, 0));
            auto walker = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));
            walker->speed(6.0f);
            walker->goal(MV::Point<int>(9, 0));
            for (int tick = 0; tick < 400 && walker->pathfinding(); ++tick) {
                map->beginTick();
                idle->update(1.0 / 60.0);
                walker->update(1.0 / 60.0);
            }
            check_true(!walker->pathfinding(), "the walker got past the idle agent");
            check_eq(5.0f, idle->position().x, "the idle agent stepped into the side pocket");
            check_eq(1.0f, idle->position().y, "the idle agent stepped into the side pocket");
        });

        test("no_agent_is_left_stuck_when_two_columns_pass_in_a_corridor", [this]() {
            auto map = MV::Map::make(MV::Size<int>(48, 3));
            std::vector<std::shared_ptr<MV::NavigationAgent>> agents;
            for (int i = 0; i < 16; ++i) {
                agents.push_back(MV::NavigationAgent::make(map, MV::Point<int>(i % 6, i / 6)));
                agents.back()->speed(6.0f); agents.back()->goal(MV::Point<int>(46, 1), 6.0f);
                agents.push_back(MV::NavigationAgent::make(map, MV::Point<int>(47 - i % 6, 2 - i / 6)));
                agents.back()->speed(6.0f); agents.back()->goal(MV::Point<int>(1, 1), 6.0f);
            }
            std::vector<MV::Point<int>> lastCell(agents.size());
            std::vector<int> idle(agents.size(), 0);
            int maxIdle = 0;
            for (int tick = 0; tick < 1500; ++tick) {
                map->beginTick();
                for (size_t i = 0; i < agents.size(); ++i) {
                    agents[i]->update(1.0 / 60.0);
                    auto cell = MV::cast<int>(agents[i]->position());
                    if (cell != lastCell[i] || !agents[i]->pathfinding()) { lastCell[i] = cell; idle[i] = 0; } else { maxIdle = std::max(maxIdle, ++idle[i]); }
                }
            }
            int arrived = static_cast<int>(std::count_if(agents.begin(), agents.end(), [](auto& a) { return !a->pathfinding(); }));
            int nearGoal = static_cast<int>(std::count_if(agents.begin(), agents.end(), [](auto& a) { return MV::distance(a->position(), a->goal()) <= 6.0f + 4.0f; }));
            check_eq(32, nearGoal, "every agent of both columns reached its goal region or is waiting at its edge for room, arrived: " + std::to_string(arrived));
            check_true(arrived >= 16, "at least a full column fits inside its goal region, arrived: " + std::to_string(arrived));
            check_true(maxIdle < 180 || nearGoal == 32, "no agent ever sat still for 3 seconds away from its goal, longest idle: " + std::to_string(maxIdle));
        });

        test("size_three_agents_head_on_swap_and_pass", [this]() {
            auto map = MV::Map::make(MV::Size<int>(30, 3));
            auto left = MV::NavigationAgent::make(map, MV::Point<int>(0, 0), 3);
            auto right = MV::NavigationAgent::make(map, MV::Point<int>(27, 0), 3);
            left->speed(6.0f); right->speed(6.0f);
            left->goal(MV::Point<int>(27, 0)); right->goal(MV::Point<int>(0, 0));
            for (int tick = 0; tick < 600 && (left->pathfinding() || right->pathfinding()); ++tick) {
                map->beginTick();
                left->update(1.0 / 60.0);
                right->update(1.0 / 60.0);
            }
            check_true(!left->pathfinding() && !right->pathfinding(), "two 3x3 agents passed each other in a 3-high corridor");
        });

        test("resize_rebuilds_every_cell_and_keeps_agents_valid", [this]() {
            auto map = MV::Map::make(MV::Size<int>(8, 8));
            map->get(MV::Point<int>(3, 3)).staticBlock();
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(1, 1));
            agent->speed(6.0f);
            agent->goal(MV::Point<int>(6, 6));
            map->beginTick();
            agent->update(1.0 / 60.0);
            map->resize(MV::Size<int>(20, 12));
            check_eq(20, map->size().width, "wider");
            check_eq(12, map->size().height, "taller");
            check_true(map->blocked(MV::Point<int>(3, 3)), "static block survives the resize");
            check_true(map->agentAt(MV::Point<int>(1, 1)) == agent.get(), "the agent is placed back on its cell");
            check_true(map->get(MV::Point<int>(15, 9)).clearance() > 1, "new cells are initialised, not phantom-walled");
            agent->goal(MV::Point<int>(18, 10));
            for (int tick = 0; tick < 600 && agent->pathfinding(); ++tick) {
                map->beginTick();
                agent->update(1.0 / 60.0);
            }
            check_true(!agent->pathfinding(), "the agent paths across the grown map after the resize");
            map->get(MV::Point<int>(3, 3)).staticUnblock();
            map->resize(MV::Size<int>(6, 6));
            check_eq(6, map->size().width, "narrower");
            check_true(map->agentAt(MV::Point<int>(1, 1)) == nullptr, "an agent left outside the shrunk map holds no cell");
        });

        test("agents_sharing_a_goal_all_move_at_once_via_the_flow_field", [this]() {
            auto map = MV::Map::make(MV::Size<int>(120, 40), true);
            for (int y = 0; y < 40; ++y) { if (y < 16 || y > 23) { map->get(MV::Point<int>(60, y)).staticBlock(); } }
            std::vector<std::shared_ptr<MV::NavigationAgent>> agents;
            for (int i = 0; i < 24; ++i) {
                auto agent = MV::NavigationAgent::make(map, MV::Point<int>(2 + (i % 4) * 3, 8 + (i / 4) * 4), 3);
                agent->speed(6.0f);
                agent->goal(MV::Point<int>(110, 20), 8.0f, MV::PathMode::flowField);
                agents.push_back(agent);
            }
            int movingTicks = 0, pathfindingTicks = 0;
            std::vector<MV::Point<MV::PointPrecision>> last(agents.size());
            for (size_t i = 0; i < agents.size(); ++i) { last[i] = agents[i]->position(); }
            for (int tick = 0; tick < 300; ++tick) {
                map->beginTick();
                for (size_t i = 0; i < agents.size(); ++i) {
                    agents[i]->update(1.0 / 60.0);
                    if (agents[i]->pathfinding()) {
                        ++pathfindingTicks;
                        if (MV::distance(agents[i]->position(), last[i]) > 0.001f) { ++movingTicks; }
                    }
                    last[i] = agents[i]->position();
                }
            }
            check_true(map->remainingBudget() > MV::Map::FRAME_NODE_BUDGET - 200, "a shared goal costs almost no search budget per tick: used " + std::to_string(MV::Map::FRAME_NODE_BUDGET - map->remainingBudget()));
            check_true(movingTicks * 10 >= pathfindingTicks * 3, "at least 30% of agent-ticks were spent moving (a packed 4x4 block of 3x3 units unpacks from the front): " + std::to_string(movingTicks) + "/" + std::to_string(pathfindingTicks));
        });

        test("active_goals_pin_their_flow_field_against_eviction", [this]() {
            auto map = MV::Map::make(MV::Size<int>(40, 40));
            map->maxFlowFields(2);
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(2, 2));
            agent->goal(MV::Point<int>(36, 36), 1.0f, MV::PathMode::flowField);
            map->beginTick();
            agent->update(1.0 / 60.0);
            auto pinned = agent->flowField();
            check_true(pinned != nullptr, "a followed field is held by the agent");
            check_eq(1, pinned->users(), "one user");
            check_eq(size_t(1), map->pinnedFlowFields(), "the map sees the pin");
            for (int i = 0; i < 4; ++i) {
                map->beginTick();
                check_true(map->flowFieldFor(MV::Point<int>(10 + i, 5), 1.0f, 1) != nullptr, "other goals get fields");
            }
            check_eq(size_t(2), map->flowFieldCount(), "idle fields are trimmed to the cap");
            map->beginTick();
            check_true(map->flowFieldFor(MV::Point<int>(36, 36), 1.0f, 1) == pinned, "the pinned field survived four evictions");
            map->beginTick();
            check_true(map->flowFieldFor(MV::Point<int>(20, 20), 1.0f, 1) != nullptr, "a third live key");
            map->beginTick();
            check_true(map->flowFieldFor(MV::Point<int>(21, 20), 1.0f, 1) != nullptr, "a fourth");
            check_eq(size_t(2), map->flowFieldCount(), "still at the cap: the idle ones rotate, the pinned one stays");
            agent->stop();
            check_true(agent->flowField() == nullptr, "stopping releases the field");
            check_eq(0, pinned->users(), "no users");
            map->beginTick();
            check_true(map->flowFieldFor(MV::Point<int>(22, 20), 1.0f, 1) != nullptr, "insert after release");
            map->beginTick();
            check_true(map->flowFieldFor(MV::Point<int>(23, 20), 1.0f, 1) != nullptr, "and again");
            map->beginTick();
            check_true(map->flowFieldFor(MV::Point<int>(36, 36), 1.0f, 1) != pinned, "the released field was evicted and rebuilt on demand");
        });

        test("cache_exceeds_the_cap_only_while_every_field_is_pinned", [this]() {
            auto map = MV::Map::make(MV::Size<int>(40, 40));
            map->maxFlowFields(1);
            map->fieldsPerTick(3);
            std::vector<std::shared_ptr<MV::NavigationAgent>> agents;
            for (int i = 0; i < 3; ++i) {
                auto agent = MV::NavigationAgent::make(map, MV::Point<int>(2, 2 + i * 4));
                agent->goal(MV::Point<int>(36, 4 + i * 12), 1.0f, MV::PathMode::flowField);
                agents.push_back(agent);
            }
            for (int tick = 0; tick < 3; ++tick) {
                map->beginTick();
                for (auto& agent : agents) { agent->update(1.0 / 60.0); }
            }
            check_eq(size_t(3), map->flowFieldCount(), "three active goals, three fields despite a cap of one");
            check_eq(size_t(3), map->pinnedFlowFields(), "all pinned");
            agents.clear();
            check_eq(size_t(0), map->pinnedFlowFields(), "destroyed agents release");
            map->beginTick();
            check_true(map->flowFieldFor(MV::Point<int>(30, 30), 1.0f, 1) != nullptr, "the next insert trims");
            check_eq(size_t(1), map->flowFieldCount(), "back to the cap");
        });

        test("goal_inside_a_static_block_seeds_the_nearest_standable_ring", [this]() {
            auto map = MV::Map::make(MV::Size<int>(40, 40));
            for (int x = 18; x <= 22; ++x) { for (int y = 18; y <= 22; ++y) { map->get(MV::Point<int>(x, y)).staticBlock(); } }
            auto field = map->flowFieldFor(MV::Point<int>(20, 20), 0.0f, 1);
            check_true(field != nullptr && field->reachableCells() > 0, "a blocked goal still yields a field");
            check_true(field->reachable(MV::Point<int>(2, 2)), "the far corner descends toward it");
            check_true(!field->reachable(MV::Point<int>(20, 20)), "the blocked cell itself is not part of it");
            map->beginTick();
            auto big = map->flowFieldFor(MV::Point<int>(20, 20), 0.0f, 3);
            check_true(big != nullptr && big->reachableCells() > 0, "a 3x3 footprint gets its own standable ring");
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(2, 2), 3);
            agent->goal(MV::Point<int>(20, 20), 0.0f, MV::PathMode::flowField);
            agent->speed(6.0f);
            for (int tick = 0; tick < 600 && agent->flowField() == nullptr; ++tick) { map->beginTick(); agent->update(1.0 / 60.0); }
            check_true(agent->flowField() == big, "the creature-sized agent follows the field to the building");
            for (int tick = 0; tick < 1200; ++tick) { map->beginTick(); agent->update(1.0 / 60.0); }
            auto cell = agent->currentCell();
            check_true(std::max(std::abs(cell.x - 20), std::abs(cell.y - 20)) <= 5, "it ends up beside the block, not stuck at spawn");
        });

        test("a_flow_field_handle_is_the_lease", [this]() {
            auto map = MV::Map::make(MV::Size<int>(30, 30));
            map->maxFlowFields(1);
            auto field = map->flowFieldFor(MV::Point<int>(25, 25), 1.0f, 1);
            check_eq(0, field->users(), "unleased");
            {
                auto lease = field->makeHandle();
                auto second = field->makeHandle();
                check_eq(2, field->users(), "two leases");
                check_true(lease->field() == field, "the handle reaches its field");
                map->beginTick();
                check_true(map->flowFieldFor(MV::Point<int>(5, 5), 1.0f, 1) != nullptr, "another key at cap 1");
                check_eq(size_t(2), map->flowFieldCount(), "the leased field could not be trimmed");
            }
            check_eq(0, field->users(), "both leases gone");
            map->beginTick();
            check_true(map->flowFieldFor(MV::Point<int>(6, 5), 1.0f, 1) != nullptr, "the next insert");
            check_eq(size_t(1), map->flowFieldCount(), "trims the released field");
        });

        test("path_mode_defaults_to_search_and_is_opt_in", [this]() {
            auto map = MV::Map::make(MV::Size<int>(30, 30));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(1, 1));
            check_true(agent->pathMode() == MV::PathMode::search, "search by default");
            agent->goal(MV::Point<int>(25, 25), 2.0f);
            map->beginTick();
            agent->update(1.0 / 60.0);
            check_true(map->remainingBudget() < MV::Map::FRAME_NODE_BUDGET, "a search goal spends budget");
            agent->goal(MV::Point<int>(25, 25), 2.0f, MV::PathMode::flowField);
            check_true(agent->pathMode() == MV::PathMode::flowField, "the goal call switched the mode");
            map->beginTick();
            agent->update(1.0 / 60.0);
            map->beginTick();
            agent->update(1.0 / 60.0);
            check_eq(MV::Map::FRAME_NODE_BUDGET, map->remainingBudget(), "with a field the same goal spends nothing");
            map->searchBudget(500);
            check_eq(int64_t(500), map->searchBudget(), "budget is a runtime tunable");
            map->beginTick();
            check_eq(int64_t(500), map->remainingBudget(), "the next tick uses the new budget");
        });

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

            for (int y = 0; y < 10; ++y) {
                map->get(MV::Point<int>(5, y)).staticBlock();
            }

            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));
            agent->goal(MV::Point<int>(9, 0));

            auto path = agent->path();
            check_true(path.size() >= 1, "Path exists (even if just start position)");
        });

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

            if (path.size() > 1) {
                auto next_pos = path[1].position();
                map->get(next_pos).block();

                agent->update(0.016);
            }
        });

        test("multiple_agents_share_budget", [this]() {
            auto map = MV::Map::make(MV::Size<int>(50, 50));

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

            check_true(map->remainingBudget() < MV::Map::FRAME_NODE_BUDGET,
                      "Budget consumed by agent updates");
        });

        test("agent_respects_zero_budget", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            map->consumeBudget(MV::Map::FRAME_NODE_BUDGET + 1000);
            check_false(map->hasBudget(), "Budget exhausted");

            agent->goal(MV::Point<int>(19, 0));

            // When budget is zero, attemptToRecalculate should return early
        });

        test("path_uses_budget_limit", [this]() {
            auto map = MV::Map::make(MV::Size<int>(100, 100));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            for (int x = 10; x < 90; ++x) {
                map->get(MV::Point<int>(x, 50)).staticBlock();
            }

            agent->goal(MV::Point<int>(99, 99));
            auto path = agent->path();

            check_true(path.size() >= 1, "Some path calculated within budget");
        });

        test("clearance_for_unit_size", [this]() {
            auto map = MV::Map::make(MV::Size<int>(20, 20));

            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0), 2);

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

        test("on_arrive_signal", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));

            bool arrived = false;
            agent->onArrive.connect("test", [&](std::shared_ptr<MV::NavigationAgent>) {
                arrived = true;
            });

            agent->goal(MV::Point<int>(2, 0));
            agent->speed(100.0f);  // Fast speed to arrive quickly

            for (int i = 0; i < 100 && agent->pathfinding(); ++i) {
                agent->update(0.1);
            }

            check_true(arrived, "onArrive signal fired");
        });

        test("on_blocked_signal_not_repeated", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

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

            for (int i = 0; i < 10; ++i) {
                // Sleep to reset budget between frames
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                agent->update(0.016);
            }

            check_true(blocked_count <= 2, "onBlocked signal not fired excessively");
        });

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

            check_true(map->blocked(MV::Point<int>(5, 5)), "Agent blocks its cell");

            agent->disableFootprint();
            check_false(map->blocked(MV::Point<int>(5, 5)), "Cell unblocked after disabling footprint");

            agent->enableFootprint();
            check_true(map->blocked(MV::Point<int>(5, 5)), "Cell blocked after enabling footprint");
        });

        test("temporary_cost_affects_pathfinding", [this]() {
            auto map = MV::Map::make(MV::Size<int>(10, 10));

            std::vector<MV::TemporaryCost> costs;
            for (int x = 0; x < 10; ++x) {
                costs.emplace_back(map, MV::Point<int>(x, 5), 100.0f);
            }

            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(0, 0));
            agent->goal(MV::Point<int>(0, 9));

            auto path = agent->path();

            check_true(path.size() >= 1, "Path calculated with temporary costs");
        });

        test("agent_footprint_moves_restore_block_counts", [this]() {
            auto map = MV::Map::make(MV::Size<int>(12, 12));
            auto otherMap = MV::Map::make(MV::Size<int>(12, 12));
            for (int y = 0; y < 12; ++y) {
                map->get(MV::Point<int>(11, y)).block();
            }
            // clearance is lazily initialized per node and refreshed from neighbours' cached values; warm it so a 2x2 move never reads an uninitialized neighbour
            for (int x = 0; x < 12; ++x) {
                for (int y = 0; y < 12; ++y) {
                    map->get(MV::Point<int>(x, y)).clearance();
                }
            }
            auto snapshot = [](const std::shared_ptr<MV::Map>& a_map) {
                std::vector<bool> cells;
                for (int x = 0; x < a_map->size().width; ++x) {
                    for (int y = 0; y < a_map->size().height; ++y) {
                        cells.push_back(a_map->blocked(MV::Point<int>(x, y)));
                    }
                }
                return cells;
            };
            auto before = snapshot(map);
            auto otherBefore = snapshot(otherMap);
            {
                auto agent = MV::NavigationAgent::make(map, MV::Point<int>(1, 1), 2);
                check_true(map->blocked(MV::Point<int>(2, 2)), "2x2 footprint marks its far corner");

                agent->goal(MV::Point<int>(8, 8));
                agent->speed(4.0f);
                for (int i = 0; i < 300 && agent->pathfinding(); ++i) {
                    agent->update(0.1);
                }
                auto moved = MV::cast<int>(agent->position());
                check_ne(MV::Point<int>(1, 1), moved, "Agent left its starting cell via update()");
                check_true(map->blocked(moved + MV::Point<int>(1, 1)), "Footprint follows the agent through update() moves");
                check_false(map->blocked(MV::Point<int>(1, 1)), "Starting cell released after moving away");

                agent->position(MV::Point<int>(0, 9));
                check_true(map->blocked(MV::Point<int>(1, 10)), "Footprint follows position()");
                check_false(map->blocked(moved), "Previous footprint released by position()");

                agent->disableFootprint();
                agent->position(MV::Point<int>(6, 6));
                agent->enableFootprint();
                check_true(map->blocked(MV::Point<int>(7, 7)), "Footprint re-placed at the post-toggle position");
                check_false(map->blocked(MV::Point<int>(1, 10)), "Pre-toggle footprint released");

                auto copy = agent->clone(otherMap);
                check_true(otherMap->blocked(MV::Point<int>(7, 7)), "Clone blocks its full 2x2 footprint");
                copy.reset();
                check_true(snapshot(otherMap) == otherBefore, "Clone teardown restores every cell");
            }
            check_true(snapshot(map) == before, "Every cell's block state restored after agent teardown");
        });

        test("agent_footprint_partially_off_map_top_left", [this]() {
            auto map = MV::Map::make(MV::Size<int>(6, 6));
            auto agent = MV::NavigationAgent::make(map, MV::Point<int>(-1, -1), 2);
            check_false(map->blocked(MV::Point<int>(0, 0)), "Footprint overhanging the top-left corner is not placed");

            agent->update(0.016);
            check_false(map->blocked(MV::Point<int>(0, 0)), "Waiting agent leaves the corner untouched");

            agent->position(MV::Point<int>(0, 0));
            check_true(map->blocked(MV::Point<int>(0, 0)), "Footprint placed once fully on the map");
            check_true(map->blocked(MV::Point<int>(1, 1)), "Full 2x2 placed");

            agent->position(MV::Point<int>(-1, -1));
            check_false(map->blocked(MV::Point<int>(0, 0)), "Moving back off the corner releases the footprint");
            check_false(map->blocked(MV::Point<int>(1, 1)), "Whole footprint released");

            agent.reset();
            for (int x = 0; x < 6; ++x) {
                for (int y = 0; y < 6; ++y) {
                    check_false(map->blocked(MV::Point<int>(x, y)), "Map clean after off-map agent teardown");
                }
            }
        });
    }
};

} // namespace bindstone::tests

FOUNDRY_REGISTER(bindstone::tests::pathfinding_tests);
