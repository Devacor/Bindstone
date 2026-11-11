#include "path.h"

#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/signals/signal.hpp>

namespace MV {
	namespace Scene {
		void PathAgent::jai_auto_bind(jai::dynamic_binder<PathAgent>& a_binder) {
			a_binder.property("onArrive", &PathAgent::onArrive);
			a_binder.property("onBlocked", &PathAgent::onBlocked);
			a_binder.property("onStop", &PathAgent::onStop);
			a_binder.property("onStart", &PathAgent::onStart);

			a_binder.method("gridPosition", static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::gridPosition));
			a_binder.method("gridPosition", static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<int>&)>(&PathAgent::gridPosition), jai::serial);
			a_binder.method("localPosition", static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::localPosition));
			a_binder.method("localPosition", static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<>&)>(&PathAgent::localPosition), jai::serial);

			a_binder.method("gridGoal", static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::gridGoal));
			a_binder.method("gridGoal", static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<int>&)>(&PathAgent::gridGoal), jai::serial);
			a_binder.method("gridGoal", [](PathAgent& a_self, const Point<int>& a_goal, double a_distance, int64_t a_mode) { return a_self.gridGoal(a_goal, static_cast<PointPrecision>(a_distance), static_cast<PathMode>(a_mode)); }, jai::serial);
			a_binder.method("gridGoal", [](PathAgent& a_self, const Point<>& a_goal, double a_distance, int64_t a_mode) { return a_self.gridGoal(a_goal, static_cast<PointPrecision>(a_distance), static_cast<PathMode>(a_mode)); }, jai::serial);
			a_binder.method("pathMode", [](PathAgent& a_self) -> int64_t { return static_cast<int64_t>(a_self.pathMode()); }, jai::serial);
			a_binder.method("pathMode", [](PathAgent& a_self, int64_t a_mode) { return a_self.pathMode(static_cast<PathMode>(a_mode)); }, jai::serial);
			a_binder.method("localGoal", static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::localGoal));
			a_binder.method("localGoal", static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<>&)>(&PathAgent::localGoal), jai::serial);

			a_binder.method("gridSpeed", static_cast<PointPrecision(PathAgent::*)() const>(&PathAgent::gridSpeed));
			a_binder.method("gridSpeed", static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(PointPrecision)>(&PathAgent::gridSpeed), jai::serial);
			a_binder.method("localSpeed", static_cast<PointPrecision(PathAgent::*)() const>(&PathAgent::localSpeed));
			a_binder.method("localSpeed", static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(PointPrecision)>(&PathAgent::localSpeed), jai::serial);

			a_binder.method("pathfinding", &PathAgent::pathfinding);
			a_binder.method("stop", &PathAgent::stop, jai::serial);
			a_binder.method("path", &PathAgent::path, jai::serial);

			a_binder.method("gridSize", &PathAgent::gridSize);
			a_binder.method("gridOverlaps", &PathAgent::gridOverlaps);
			a_binder.method("localOverlaps", &PathAgent::localOverlaps);
			a_binder.method("disableFootprint", &PathAgent::disableFootprint, jai::serial);
			a_binder.method("enableFootprint", &PathAgent::enableFootprint, jai::serial);
			a_binder.method("hasFootprint", &PathAgent::hasFootprint);
		}

		void PathMap::jai_auto_bind(jai::dynamic_binder<PathMap>& a_binder) {
			a_binder.method("resizeGrid", &PathMap::resizeGrid, jai::serial);
			a_binder.method("cellSize", static_cast<Size<>(PathMap::*)() const>(&PathMap::cellSize));
			a_binder.method("cellSize", static_cast<std::shared_ptr<PathMap>(PathMap::*)(const Size<>&)>(&PathMap::cellSize), jai::serial);
			a_binder.method("searchBudget", [](PathMap& a_self) -> int64_t { return a_self.searchBudget(); }, jai::serial);
			a_binder.method("searchBudget", [](PathMap& a_self, int64_t a_nodes) { return a_self.searchBudget(a_nodes); }, jai::serial);
			a_binder.method("searchLimit", [](PathMap& a_self) -> int64_t { return a_self.searchLimit(); }, jai::serial);
			a_binder.method("searchLimit", [](PathMap& a_self, int64_t a_nodes) { return a_self.searchLimit(a_nodes); }, jai::serial);
			a_binder.method("fieldsPerTick", [](PathMap& a_self) -> int64_t { return a_self.fieldsPerTick(); }, jai::serial);
			a_binder.method("fieldsPerTick", [](PathMap& a_self, int64_t a_fields) { return a_self.fieldsPerTick(static_cast<int>(a_fields)); }, jai::serial);
			a_binder.method("maxFlowFields", [](PathMap& a_self) -> int64_t { return a_self.maxFlowFields(); }, jai::serial);
			a_binder.method("maxFlowFields", [](PathMap& a_self, int64_t a_fields) { return a_self.maxFlowFields(a_fields); }, jai::serial);
			a_binder.method("gridSize", &PathMap::gridSize);

			a_binder.method("nodeFromGrid", static_cast<MapNode&(PathMap::*)(const Point<int>&)>(&PathMap::nodeFromGrid));
			a_binder.method("nodeFromLocal", &PathMap::nodeFromLocal);

			a_binder.method("gridFromLocal", static_cast<Point<>(PathMap::*)(const Point<>&)>(&PathMap::gridFromLocal));
			a_binder.method("localFromGrid", static_cast<Point<>(PathMap::*)(const Point<int>&)>(&PathMap::localFromGrid));

			a_binder.method("inBounds", &PathMap::inBounds);
			a_binder.method("blocked", &PathMap::blocked);
			a_binder.method("staticallyBlocked", &PathMap::staticallyBlocked);
			a_binder.method("traverseCorners", &PathMap::traverseCorners);
		}


		const std::vector<Color> PathMap::alternatingDebugTiles{ {0.1f, 0.7f, 0.1f, 0.5f}, { 0.1f, 0.1f, 0.7f, 0.5f } };
		const Color PathMap::staticBlockedDebugTile{ 0.7f, 0.1f, 0.1f, 0.5f };
		const Color PathMap::regularBlockedDebugTile{ 0.7f, 0.7f, 0.7f, 0.5f };

		PathMap::PathMap(const std::weak_ptr<Node> &a_owner, const Size<> &a_size, const Size<int> &a_gridSize, bool a_useCorners /*= true*/) :
			jai::property_owner<PathMap, Drawable>(a_owner) {
			map = Map::make(a_gridSize, a_useCorners);
			cellDimensions = a_size;
			shouldDraw = false;
		}

		Color PathMap::alternatingDebugTilesWithClearance(int a_x, int a_y, int a_clearance) {
			auto tileColor = alternatingDebugTiles[(a_x + a_y) % 2];
			tileColor.A = static_cast<float>(a_clearance) / static_cast<float>(MapNode::MAXIMUM_CLEARANCE);
			return tileColor;
		}

		void PathMap::updateDebugViewSignals() {
			if (visible()) {
				map->onStaticBlock.connect("_PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					int index = (a_position.x * map->size().height) + a_position.y;
					for (int i = 0; i < 4; ++i) {
						(*points)[(index * 4) + i] = staticBlockedDebugTile;
					}
				});

				map->onBlock.connect("_PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					int index = (a_position.x * map->size().height) + a_position.y;
					if (map->get(a_position).staticallyBlocked()) {
						for (int i = 0; i < 4; ++i) {
							(*points)[(index * 4) + i] = staticBlockedDebugTile;
						}
					}
					else {
						for (int i = 0; i < 4; ++i) {
							(*points)[(index * 4) + i] = regularBlockedDebugTile;
						}
					}
				});

				map->onUnblock.connect("_PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					int index = (a_position.x * map->size().height) + a_position.y;
					for (int i = 0; i < 4; ++i) {
						(*points)[(index * 4) + i] = alternatingDebugTilesWithClearance(a_position.x, a_position.y, map->get(a_position).clearance());
					}
				});

				map->onClearanceChange.connect("_PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					int index = (a_position.x * map->size().height) + a_position.y;
					if (map->get(a_position).staticallyBlocked()) {
						for (int i = 0; i < 4; ++i) {
							(*points)[(index * 4) + i] = staticBlockedDebugTile;
						}
					}
					else if (map->get(a_position).blocked()) {
						for (int i = 0; i < 4; ++i) {
							(*points)[(index * 4) + i] = regularBlockedDebugTile;
						}
					}
					else {
						for (int i = 0; i < 4; ++i) {
							(*points)[(index * 4) + i] = alternatingDebugTilesWithClearance(a_position.x, a_position.y, map->get(a_position).clearance());
						}
					}
				});
			}
			else {
				map->onStaticBlock.disconnect("_PARENT");
				map->onBlock.disconnect("_PARENT");
				map->onUnblock.disconnect("_PARENT");
				map->onClearanceChange.disconnect("_PARENT");
			}
		}

		void PathMap::repositionDebugDrawPoints() {
			std::vector<Point<>> cornerOffsets = { {0, 0}, {0, cellDimensions->height}, {cellDimensions->width, cellDimensions->height}, {cellDimensions->width, 0} };
			int squareIndex = 0;
			for (int x = 0; x < map->size().width; ++x) {
				for (int y = 0; y < map->size().height; ++y) {
					int index = (x * map->size().height) + y;
					for (int i = 0; i < 4; ++i) {
						(*points)[(index * 4) + i] = MV::point(x * cellDimensions->width, y * cellDimensions->height) + cornerOffsets[i] + topLeftOffset;
						if ((*map.get())[x][y].staticallyBlocked()) {
							(*points)[(index * 4) + i] = staticBlockedDebugTile;
						} else if ((*map.get())[x][y].blocked()) {
							(*points)[(index * 4) + i] = regularBlockedDebugTile;
						}
						else {
							(*points)[(index * 4) + i] = alternatingDebugTilesWithClearance(x, y, (*map.get())[x][y].clearance());
						}
					}
				}
			}
			refreshBounds();
		}

		void PathAgent::initialize() {
			applyAgentPositionToOwner();
			agentPassthroughSignals.push_back(agent->onArrive.connect([&](std::shared_ptr<NavigationAgent>) {
				onArriveSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
			}));
			agentPassthroughSignals.push_back(agent->onBlocked.connect([&](std::shared_ptr<NavigationAgent>) {
				auto self = std::static_pointer_cast<PathAgent>(shared_from_this());
				onBlockedSignal(self);
			}));
			agentPassthroughSignals.push_back(agent->onStop.connect([&](std::shared_ptr<NavigationAgent>) {
				onStopSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
			}));
			agentPassthroughSignals.push_back(agent->onStart.connect([&](std::shared_ptr<NavigationAgent>) {
				onStartSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
			}));
		}

	}
}
