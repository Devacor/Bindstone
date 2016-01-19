#include "path.h"
#include "cereal/archives/json.hpp"
CEREAL_REGISTER_TYPE(MV::Scene::PathMap);
CEREAL_REGISTER_TYPE(MV::Scene::PathAgent);

namespace MV {
	namespace Scene {

		const std::vector<Color> PathMap::alternatingDebugTiles{ {0.1f, 0.7f, 0.1f, 0.5f}, { 0.1f, 0.1f, 0.7f, 0.5f } };
		const Color PathMap::staticBlockedDebugTile{ 0.7f, 0.1f, 0.1f, 0.5f };
		const Color PathMap::regularBlockedDebugTile{ 0.7f, 0.7f, 0.7f, 0.5f };

		PathMap::PathMap(const std::weak_ptr<Node> &a_owner, const Size<> &a_size, const Size<int> &a_gridSize, bool a_useCorners /*= true*/) :
			Drawable(a_owner),
			map(Map::make(a_gridSize, a_useCorners)),
			cellDimensions(a_size) {

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
						points[(index * 4) + i] = staticBlockedDebugTile;
					}
				});

				map->onBlock.connect("_PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					int index = (a_position.x * map->size().height) + a_position.y;
					if (map->get(a_position).staticallyBlocked()) {
						for (int i = 0; i < 4; ++i) {
							points[(index * 4) + i] = staticBlockedDebugTile;
						}
					}
					else {
						for (int i = 0; i < 4; ++i) {
							points[(index * 4) + i] = regularBlockedDebugTile;
						}
					}
				});

				map->onUnblock.connect("_PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					int index = (a_position.x * map->size().height) + a_position.y;
					for (int i = 0; i < 4; ++i) {
						points[(index * 4) + i] = alternatingDebugTilesWithClearance(a_position.x, a_position.y, map->get(a_position).clearance());
					}
				});

				map->onClearanceChange.connect("_PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					int index = (a_position.x * map->size().height) + a_position.y;
					if (map->get(a_position).staticallyBlocked()) {
						for (int i = 0; i < 4; ++i) {
							points[(index * 4) + i] = staticBlockedDebugTile;
						}
					}
					else if (map->get(a_position).blocked()) {
						for (int i = 0; i < 4; ++i) {
							points[(index * 4) + i] = regularBlockedDebugTile;
						}
					}
					else {
						for (int i = 0; i < 4; ++i) {
							points[(index * 4) + i] = alternatingDebugTilesWithClearance(a_position.x, a_position.y, map->get(a_position).clearance());
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
			std::vector<Point<>> cornerOffsets = { {0, 0}, {0, cellDimensions.height}, {cellDimensions.width, cellDimensions.height}, {cellDimensions.width, 0} };
			int squareIndex = 0;
			for (int x = 0; x < map->size().width; ++x) {
				for (int y = 0; y < map->size().height; ++y) {
					int index = (x * map->size().height) + y;
					for (int i = 0; i < 4; ++i) {
						points[(index * 4) + i] = MV::point(x * cellDimensions.width, y * cellDimensions.height) + cornerOffsets[i] + topLeftOffset;
						if ((*map)[x][y].staticallyBlocked()) {
							points[(index * 4) + i] = staticBlockedDebugTile;
						} else if ((*map)[x][y].blocked()) {
							points[(index * 4) + i] = regularBlockedDebugTile;
						}
						else {
							points[(index * 4) + i] = alternatingDebugTilesWithClearance(x, y, (*map)[x][y].clearance());
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