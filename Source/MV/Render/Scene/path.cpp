#include "path.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"
#include "MV/Serialization/property_cereal.hpp"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

// JaiScript binding for PathMap
static jai::registrar<MV::Scene::PathMap, MV::Services> _hookPathMap("PathMap",
	[](jai::dynamic_binder<MV::Scene::PathMap>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Drawable>();
	builder.auto_bind();

	// Grid operations
	builder.method("resizeGrid", &MV::Scene::PathMap::resizeGrid);
	builder.method("cellSize", static_cast<MV::Size<>(MV::Scene::PathMap::*)() const>(&MV::Scene::PathMap::cellSize));
	builder.method("cellSize", static_cast<std::shared_ptr<MV::Scene::PathMap>(MV::Scene::PathMap::*)(const MV::Size<>&)>(&MV::Scene::PathMap::cellSize));
	builder.method("gridSize", &MV::Scene::PathMap::gridSize);

	// Node lookups
	builder.method("nodeFromGrid", static_cast<MV::MapNode&(MV::Scene::PathMap::*)(const MV::Point<int>&)>(&MV::Scene::PathMap::nodeFromGrid));
	builder.method("nodeFromLocal", &MV::Scene::PathMap::nodeFromLocal);

	// Coordinate conversions
	builder.method("gridFromLocal", static_cast<MV::Point<>(MV::Scene::PathMap::*)(const MV::Point<>&)>(&MV::Scene::PathMap::gridFromLocal));
	builder.method("localFromGrid", static_cast<MV::Point<>(MV::Scene::PathMap::*)(const MV::Point<int>&)>(&MV::Scene::PathMap::localFromGrid));

	// Queries
	builder.method("inBounds", &MV::Scene::PathMap::inBounds);
	builder.method("blocked", &MV::Scene::PathMap::blocked);
	builder.method("staticallyBlocked", &MV::Scene::PathMap::staticallyBlocked);
	builder.method("traverseCorners", &MV::Scene::PathMap::traverseCorners);
});

// JaiScript binding for PathAgent
static jai::registrar<MV::Scene::PathAgent, MV::Services> _hookPathAgent("PathAgent",
	[](jai::dynamic_binder<MV::Scene::PathAgent>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Component>();
	builder.auto_bind();

	// Position
	builder.method("gridPosition", static_cast<MV::Point<MV::PointPrecision>(MV::Scene::PathAgent::*)() const>(&MV::Scene::PathAgent::gridPosition));
	builder.method("gridPosition", static_cast<std::shared_ptr<MV::Scene::PathAgent>(MV::Scene::PathAgent::*)(const MV::Point<int>&)>(&MV::Scene::PathAgent::gridPosition));
	builder.method("localPosition", static_cast<MV::Point<MV::PointPrecision>(MV::Scene::PathAgent::*)() const>(&MV::Scene::PathAgent::localPosition));
	builder.method("localPosition", static_cast<std::shared_ptr<MV::Scene::PathAgent>(MV::Scene::PathAgent::*)(const MV::Point<>&)>(&MV::Scene::PathAgent::localPosition));

	// Goal
	builder.method("gridGoal", static_cast<MV::Point<MV::PointPrecision>(MV::Scene::PathAgent::*)() const>(&MV::Scene::PathAgent::gridGoal));
	builder.method("gridGoal", static_cast<std::shared_ptr<MV::Scene::PathAgent>(MV::Scene::PathAgent::*)(const MV::Point<int>&)>(&MV::Scene::PathAgent::gridGoal));
	builder.method("localGoal", static_cast<MV::Point<MV::PointPrecision>(MV::Scene::PathAgent::*)() const>(&MV::Scene::PathAgent::localGoal));
	builder.method("localGoal", static_cast<std::shared_ptr<MV::Scene::PathAgent>(MV::Scene::PathAgent::*)(const MV::Point<>&)>(&MV::Scene::PathAgent::localGoal));

	// Speed
	builder.method("gridSpeed", static_cast<MV::PointPrecision(MV::Scene::PathAgent::*)() const>(&MV::Scene::PathAgent::gridSpeed));
	builder.method("gridSpeed", static_cast<std::shared_ptr<MV::Scene::PathAgent>(MV::Scene::PathAgent::*)(MV::PointPrecision)>(&MV::Scene::PathAgent::gridSpeed));
	builder.method("localSpeed", static_cast<MV::PointPrecision(MV::Scene::PathAgent::*)() const>(&MV::Scene::PathAgent::localSpeed));
	builder.method("localSpeed", static_cast<std::shared_ptr<MV::Scene::PathAgent>(MV::Scene::PathAgent::*)(MV::PointPrecision)>(&MV::Scene::PathAgent::localSpeed));

	// Control
	builder.method("pathfinding", &MV::Scene::PathAgent::pathfinding);
	builder.method("stop", &MV::Scene::PathAgent::stop);
	builder.method("path", &MV::Scene::PathAgent::path);

	// Size and footprint
	builder.method("gridSize", &MV::Scene::PathAgent::gridSize);
	builder.method("gridOverlaps", &MV::Scene::PathAgent::gridOverlaps);
	builder.method("localOverlaps", &MV::Scene::PathAgent::localOverlaps);
	builder.method("disableFootprint", &MV::Scene::PathAgent::disableFootprint);
	builder.method("enableFootprint", &MV::Scene::PathAgent::enableFootprint);
	builder.method("hasFootprint", &MV::Scene::PathAgent::hasFootprint);
});

CEREAL_REGISTER_TYPE(MV::Scene::PathMap);
CEREAL_REGISTER_TYPE(MV::Scene::PathAgent);
CEREAL_CLASS_VERSION(MV::Scene::PathAgent, 1);
CEREAL_CLASS_VERSION(MV::Scene::PathMap, 1);
CEREAL_REGISTER_DYNAMIC_INIT(mv_scenepath);

namespace MV {
	namespace Scene {

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