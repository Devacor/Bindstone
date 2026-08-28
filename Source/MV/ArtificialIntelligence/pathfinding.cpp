#include "pathfinding.h"

#include <limits>
#include <cstdlib>
#include <iostream>

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

static jai::registrar<MV::PathNode, MV::Services> _hookPathNode("PathNode",
	[](jai::dynamic_binder<MV::PathNode>& builder, const MV::Services&) {
	builder.auto_bind();
	builder.constructor<const MV::Point<int>&, float>();
	builder.method("position", &MV::PathNode::position);
	builder.method("cost", &MV::PathNode::cost);
});

static jai::registrar<MV::NavigationAgent, MV::Services> _hookNavigationAgent("NavigationAgent",
	[](jai::dynamic_binder<MV::NavigationAgent>& builder, const MV::Services&) {
	builder.auto_bind();

	builder.method("pathfinding", &MV::NavigationAgent::pathfinding);
	builder.method("stop", &MV::NavigationAgent::stop);
	builder.method("path", &MV::NavigationAgent::path);
	builder.method("hasFootprint", &MV::NavigationAgent::hasFootprint);
	builder.method("disableFootprint", &MV::NavigationAgent::disableFootprint);
	builder.method("enableFootprint", &MV::NavigationAgent::enableFootprint);

	builder.method("speed", static_cast<MV::PointPrecision(MV::NavigationAgent::*)() const>(&MV::NavigationAgent::speed));
	builder.method("speed", static_cast<std::shared_ptr<MV::NavigationAgent>(MV::NavigationAgent::*)(MV::PointPrecision)>(&MV::NavigationAgent::speed));

	builder.method("goal", static_cast<MV::Point<MV::PointPrecision>(MV::NavigationAgent::*)() const>(&MV::NavigationAgent::goal));
	builder.method("goal", static_cast<std::shared_ptr<MV::NavigationAgent>(MV::NavigationAgent::*)(const MV::Point<>&)>(&MV::NavigationAgent::goal));
	builder.method("goal", static_cast<std::shared_ptr<MV::NavigationAgent>(MV::NavigationAgent::*)(const MV::Point<int>&)>(&MV::NavigationAgent::goal));
	builder.method("goal", static_cast<std::shared_ptr<MV::NavigationAgent>(MV::NavigationAgent::*)(const MV::Point<>&, MV::PointPrecision)>(&MV::NavigationAgent::goal));
	builder.method("goal", static_cast<std::shared_ptr<MV::NavigationAgent>(MV::NavigationAgent::*)(const MV::Point<int>&, MV::PointPrecision)>(&MV::NavigationAgent::goal));

	builder.method("position", static_cast<MV::Point<MV::PointPrecision>(MV::NavigationAgent::*)() const>(&MV::NavigationAgent::position));
	builder.method("position", static_cast<std::shared_ptr<MV::NavigationAgent>(MV::NavigationAgent::*)(const MV::Point<>&)>(&MV::NavigationAgent::position));
	builder.method("position", static_cast<std::shared_ptr<MV::NavigationAgent>(MV::NavigationAgent::*)(const MV::Point<int>&)>(&MV::NavigationAgent::position));
});

namespace MV {

	MapNode::MapNode(Map& a_grid, const Point<int> &a_location, float a_cost, bool a_useCorners) :
		onBlock(onBlockSignal),
		onUnblock(onUnblockSignal),
		onStaticBlock(onStaticBlockSignal),
		onStaticUnblock(onStaticUnblockSignal),
		onCostChange(onCostChangeSignal),
		onClearanceChange(onClearanceChangeSignal),
	map(&a_grid),
	initialized(false),
	useCorners(a_useCorners),
	location(a_location),
	travelCost(a_cost){
	
		edges.fill(nullptr);
	}

	MapNode::MapNode(const MapNode &a_rhs) :
	onBlock(onBlockSignal),
	onUnblock(onUnblockSignal),
	onStaticBlock(onStaticBlockSignal),
	onStaticUnblock(onStaticUnblockSignal),
	onCostChange(onCostChangeSignal),
	onClearanceChange(onClearanceChangeSignal),
		map(a_rhs.map),
		initialized(false),
		useCorners(a_rhs.useCorners),
		location(a_rhs.location),
	travelCost(a_rhs.travelCost),
		staticBlockedSemaphore(a_rhs.staticBlockedSemaphore){
	
		edges.fill(nullptr);
	}

	MapNode::MapNode() :
	onBlock(onBlockSignal),
	onUnblock(onUnblockSignal),
	onStaticBlock(onStaticBlockSignal),
	onStaticUnblock(onStaticUnblockSignal),
	onCostChange(onCostChangeSignal),
	onClearanceChange(onClearanceChangeSignal),
		map(nullptr),
		initialized(false),
		useCorners(false) {
	
		edges.fill(nullptr);
	}

	MapNode& MapNode::operator=(const MapNode &a_rhs) {
		initialized = false;
		travelCost = a_rhs.travelCost;
		location = a_rhs.location;
		map = a_rhs.map;
		staticBlockedSemaphore = a_rhs.staticBlockedSemaphore;
		useCorners = a_rhs.useCorners;
		edges.fill(nullptr);
		blockedSemaphore = 0;
		temporaryCost = 0;
		occupant = nullptr;
		return *this;
	}

	float MapNode::baseCost() const {
		return travelCost;
	}

	void MapNode::baseCost(float a_newCost) {
		auto oldCost = travelCost;
		travelCost = a_newCost;
		if (oldCost != travelCost) {
			map->invalidateFlowFields();
			auto mapShared = map->shared_from_this();
			onCostChangeSignal(mapShared, location);
		}
	}

	void MapNode::staticBlock() {
		bool wasBlocked = blocked();
		++staticBlockedSemaphore;
		auto mapShared = map->shared_from_this();
		if (staticBlockedSemaphore == 1) {
			onStaticBlockSignal(mapShared, location);
		}
		if (!wasBlocked) {
			onBlockSignal(mapShared, location);
			if (blocked()) { clearanceAmount = 0; onClearanceChangeSignal(mapShared, location); }
			else { calculateClearance(); }
		}
	}

	void MapNode::staticUnblock() {
		require<ResourceException>(staticBlockedSemaphore > 0, "Error: Static Block Semaphore overextended in MapNode, something is unblocking excessively.");
		--staticBlockedSemaphore;
		auto mapShared = map->shared_from_this();
		if (staticBlockedSemaphore == 0) {
			onStaticUnblockSignal(mapShared, location);
		}
		if (!blocked()) {
			onUnblockSignal(mapShared, location);
			calculateClearance();
		}
	}

	bool MapNode::staticallyBlocked() const {
		return staticBlockedSemaphore != 0;
	}

	Map& MapNode::parent() {
		return *map;
	}

	Point<int> MapNode::position() const {
		return location;
	}

	size_t MapNode::size() const {
		return useCorners ? 8 : 4;
	}

	std::array<MapNode*, 8>::iterator MapNode::begin() {
		lazyInitialize();
		return edges.begin();
	}

	std::array<MapNode*, 8>::const_iterator MapNode::cbegin() const {
		lazyInitialize();
		return edges.cbegin();
	}

	std::array<MapNode*, 8>::iterator MapNode::end() {
		lazyInitialize();
		return edges.end() - (useCorners ? 4 : 0);
	}

	std::array<MapNode*, 8>::const_iterator MapNode::cend() const {
		lazyInitialize();
		return edges.cend() - (useCorners ? 4 : 0);
	}

	MapNode* MapNode::operator[](size_t a_size) {
		lazyInitialize();
		return edges[a_size];
	}

	bool MapNode::operator==(const MapNode &a_rhs) const {
		return location == a_rhs.location;
	}

	void MapNode::addTemporaryCost(float a_newTemporaryCost) {
		temporaryCost += a_newTemporaryCost;
		if (a_newTemporaryCost != 0.0f) {
			auto sharedMap = map->shared_from_this();
			onCostChangeSignal(sharedMap, location);
		}
	}

	void MapNode::removeTemporaryCost(float a_newTemporaryCost) {
		temporaryCost -= a_newTemporaryCost;
		if (a_newTemporaryCost != 0.0f) {
			auto sharedMap = map->shared_from_this();
			onCostChangeSignal(sharedMap, location);
		}
	}

	void MapNode::initializeEdge(size_t a_index, const Point<int> &a_location) const {
		auto& mapRef = *map;
		if (mapRef.inBounds(a_location)) {
			auto& cell = mapRef.get(a_location);
			edges[a_index] = !mapRef.blocked(a_location) ? &cell : nullptr;
			cell.onBlock.connect(guid("Block"), [&, a_index](std::shared_ptr<Map>, const Point<int> &) {
				edges[a_index] = nullptr;
			});
			cell.onUnblock.connect(guid("Unblock"), [&, a_index, a_location](std::shared_ptr<Map>, const Point<int> &) {
				edges[a_index] = &(map->get(a_location));
			});
		} else {
			edges[a_index] = nullptr;
		}
	}

	bool MapNode::edgeBlocked(const Point<int> &a_offset) const {
		auto& mapRef = *map;
		return !mapRef.blocked({ location.x + a_offset.y, location.y }) && !mapRef.blocked({ location.x, location.y + a_offset.y }) && !mapRef.blocked({ location.x + a_offset.x, location.y + a_offset.y });
	}
    	const int MapNode::MAXIMUM_CLEARANCE;

	void MapNode::lazyInitialize() const {
		if (!initialized) {
			auto& mapRef = *map;
			initialized = true;

			std::vector<Point<int>> edgePoints = {
				{ location.x - 1, location.y },
				{ location.x, location.y - 1 },
				{ location.x + 1, location.y },
				{ location.x, location.y + 1 },
				{ location.x - 1, location.y - 1 },
				{ location.x + 1, location.y + 1 },
				{ location.x - 1, location.y + 1 },
				{ location.x + 1, location.y - 1 }
			};

			for (size_t i = 0; i < (useCorners ? 8 : 4);++i) {
				initializeEdge(i, edgePoints[i]);
			}

			calculateClearance();
			registerCalculateClearanceCallbacks();
		}
	}

	void MapNode::calculateClearance() const{
		int oldClearance = clearanceAmount;
		performClearanceIncrement();
		if (oldClearance != clearanceAmount) { auto sharedMap = map->shared_from_this(); onClearanceChangeSignal(sharedMap, location); }
	}

	void MapNode::performClearanceIncrement() const {
		if (blocked()) { clearanceAmount = 0; return; }
		clearanceAmount = 1;
		while (true){
			for (int y = clearanceAmount; y < clearanceAmount + 1; ++y) {
				for (int x = 0; x < clearanceAmount + 1; ++x) {
					if (clearanceAmount >= MAXIMUM_CLEARANCE || offsetBlocked(x, y) || offsetBlocked(y, x)) {
						return;
					}
				}
			}

			++clearanceAmount;
		}
	}

	void MapNode::registerCalculateClearanceCallbacks() const {
		clearanceReceivers.clear();
		std::vector<Point<int>> locations { location + Point<int>(0, 1), location + Point<int>(1, 1), location + Point<int>(1, 0) };
		locations.erase(std::remove_if(locations.begin(), locations.end(), [&](const Point<int> &a_item){
			return !map->inBounds(a_item);
		}), locations.end());

		for(auto&& offsetLocation : locations){
			if(map->inBounds(offsetLocation)){
				auto receiver = map->get(offsetLocation).onClearanceChange.connect([&, locations](std::shared_ptr<Map> a_map, const Point<int> &a_location) {
					int oldClearance = clearanceAmount;
					if(!blocked()){
						if (locations.size() < 3) {
							clearanceAmount = 1;
						} else {
							// clearance() (not the raw cache): an uninitialised neighbour reads as 0 and
							// paints a phantom wall of 1-2-3 clearance across the map
							int smallestClearance = std::min(map->get(locations[0]).clearance() + 1, MAXIMUM_CLEARANCE);
							for (auto i = 1; i < locations.size(); ++i) {
								auto clearanceCheck = std::min(map->get(locations[i]).clearance() + 1, MAXIMUM_CLEARANCE);
								smallestClearance = std::min(smallestClearance, clearanceCheck);
							}
							clearanceAmount = smallestClearance;
						}
					} else {
						clearanceAmount = 0;
					}
					if (oldClearance != clearanceAmount) { onClearanceChangeSignal(a_map, location); }
				});
				clearanceReceivers.push_back(receiver);
			}
		}
	}

	bool MapNode::offsetBlocked(int x, int y) const {
		return !map->inBounds(Point<int>(location.x + x, location.y + y)) || (*map)[location.x + x][location.y + y].blocked();
	}

	// Resize invalidates everything derived: agents are lifted off, every cell forgets its edges,
	// clearance and connections (node addresses move with the vectors), the grid is rebuilt, then
	// agents are placed back and re-plan. Nothing captured before the resize survives it.
	void Map::resize(const Size<int> &a_size, float a_defaultCost /*= 1.0f*/) {
		std::vector<NavigationAgent*> agents;
		for (auto&& column : squares) {
			for (auto&& square : column) {
				if (square.occupant != nullptr && std::find(agents.begin(), agents.end(), square.occupant) == agents.end()) {
					agents.push_back(square.occupant);
				}
			}
		}
		for (auto* agent : agents) {
			agent->costs.clear();
			agent->receivers.clear();
			agent->blockedNodeObservers.clear();
			agent->unblockMap();
		}
		for (auto&& column : squares) {
			for (auto&& square : column) { square.resetObservation(); }
		}

		squares.resize(static_cast<size_t>(std::max(0, a_size.width)));
		for (int x = 0; x < a_size.width; ++x) {
			auto& column = squares[static_cast<size_t>(x)];
			if (static_cast<int>(column.size()) > a_size.height) { column.resize(static_cast<size_t>(std::max(0, a_size.height))); }
			column.reserve(static_cast<size_t>(std::max(0, a_size.height)));
			for (int y = static_cast<int>(column.size()); y < a_size.height; ++y) {
				column.emplace_back(*this, Point<int>(x, y), a_defaultCost, corners());
			}
			for (auto&& square : column) { square.map = this; square.resetObservation(); }
		}

		hookUpObservation();
		cellsInitialized = false;
		ensureCellsInitialized();
		for (auto* agent : agents) {
			agent->blockMap();
			agent->markDirty();
		}
	}

	std::shared_ptr<Map> Map::clone() const {
		auto result = std::shared_ptr<Map>(new Map());
		result->squares = squares;
		result->usingCorners = usingCorners;
		return result;
	}

	Map::Map() :
		onBlock(onBlockSignal),
		onUnblock(onUnblockSignal),
		onStaticBlock(onStaticBlockSignal),
		onStaticUnblock(onStaticUnblockSignal),
		onCostChange(onCostChangeSignal),
		onClearanceChange(onClearanceChangeSignal),
		usingCorners(true) {
	}

	Map::Map(const Size<int> &a_size, float a_defaultCost, bool a_useCorners) :
		onBlock(onBlockSignal),
		onUnblock(onUnblockSignal),
		onStaticBlock(onStaticBlockSignal),
		onStaticUnblock(onStaticUnblockSignal),
		onCostChange(onCostChangeSignal),
		onClearanceChange(onClearanceChangeSignal),
		usingCorners(a_useCorners) {

		squares.reserve(a_size.width);
		for (int x = 0; x < a_size.width; ++x) {
			std::vector<MapNode> column;
			column.reserve(a_size.height);
			for (int y = 0; y < a_size.height; ++y) {
				column.emplace_back(*this, Point<int>(x, y), a_defaultCost, a_useCorners);
			}
			squares.push_back(std::move(column));
		}

		hookUpObservation();
	}

	void Map::hookUpObservation() {
		auto ourSize = size();
		onStaticBlock.connect("__FLOW", [this](const std::shared_ptr<Map>&, const Point<int>&) { invalidateFlowFields(); });
		onStaticUnblock.connect("__FLOW", [this](const std::shared_ptr<Map>&, const Point<int>&) { invalidateFlowFields(); });
		for (auto&& column : squares) {
			for (auto&& square : column) {
				square.onBlock.connect("__PARENT", [&](const std::shared_ptr<Map> &a_self, const Point<int> &a_position) {
					onBlockSignal(a_self, a_position);
				});
				square.onUnblock.connect("__PARENT", [&](const std::shared_ptr<Map> &a_self, const Point<int> &a_position) {
					onUnblockSignal(a_self, a_position);
				});
				square.onStaticBlock.connect("__PARENT", [&](const std::shared_ptr<Map> &a_self, const Point<int> &a_position) {
					onStaticBlockSignal(a_self, a_position);
				});
				square.onStaticUnblock.connect("__PARENT", [&](const std::shared_ptr<Map> &a_self, const Point<int> &a_position) {
					onStaticUnblockSignal(a_self, a_position);
				});
				square.onCostChange.connect("__PARENT", [&](const std::shared_ptr<Map> &a_self, const Point<int> &a_position) {
					onCostChangeSignal(a_self, a_position);
				});
				square.onClearanceChange.connect("__PARENT", [&](const std::shared_ptr<Map> &a_self, const Point<int> &a_position) {
					onClearanceChangeSignal(a_self, a_position);
				});
			}
		}
	}

	float Path::heuristic(const Point<int>& a_position) const {
		float dx = static_cast<float>(std::abs(a_position.x - goalPosition.x));
		float dy = static_cast<float>(std::abs(a_position.y - goalPosition.y));
		return map->corners() ? std::max(dx, dy) + 0.4f * std::min(dx, dy) : dx + dy;
	}

	// Min-heap on estimated total; among ties the newest entry wins (the old sorted list's order)
	bool Path::openEntryAfter(const OpenEntry& a_lhs, const OpenEntry& a_rhs) {
		return a_lhs.estimatedTotal > a_rhs.estimatedTotal || (a_lhs.estimatedTotal == a_rhs.estimatedTotal && a_lhs.sequence < a_rhs.sequence);
	}

	void Path::pushOpen(int32_t a_node) {
		open.push_back({ nodes[a_node].estimatedTotal, sequence++, a_node });
		std::push_heap(open.begin(), open.end(), openEntryAfter);
	}

	int32_t Path::popOpen() {
		std::pop_heap(open.begin(), open.end(), openEntryAfter);
		auto entry = open.back();
		open.pop_back();
		auto& node = nodes[entry.node];
		return (node.closed || entry.estimatedTotal > node.estimatedTotal) ? -1 : entry.node;
	}

	void Path::visit(MapNode& a_cell, int32_t a_parent, float a_costToArrive, bool a_corner, uint32_t a_generation) {
		if (a_cell.searchStamp == a_generation) {
			auto& node = nodes[a_cell.searchIndex];
			if (node.closed || node.costToArrive <= a_costToArrive) { return; }
			node.parent = a_parent;
			node.costToArrive = a_costToArrive;
			node.estimatedTotal = a_costToArrive + heuristic(a_cell.position());
			node.corner = a_corner;
			pushOpen(a_cell.searchIndex);
			return;
		}
		a_cell.searchStamp = a_generation;
		a_cell.searchIndex = static_cast<int32_t>(nodes.size());
		nodes.push_back({ &a_cell, a_parent, a_costToArrive, a_costToArrive + heuristic(a_cell.position()), a_corner, false });
		pushOpen(a_cell.searchIndex);
	}

	void Path::begin() {
		map->ensureCellsInitialized();
		nodes.clear();
		open.clear();
		pathNodes.clear();
		sequence = 0;
		found = false;
		searched = 0;
		current = -1;
		best = -1;
		bestDistance = -1;
		nodes.reserve(maxSearchNodes > 0 ? static_cast<size_t>(maxSearchNodes) * 2 : 256);
		generation = map->nextSearchGeneration();
		visit(map->get(startPosition), -1, 0.0f, false, generation);
		started = true;
		finishedFlag = false;
	}

	// Other searches reuse the cells' stamps between slices; re-stamp our arena before continuing
	void Path::resume() {
		generation = map->nextSearchGeneration();
		for (size_t i = 0; i < nodes.size(); ++i) {
			nodes[i].cell->searchStamp = generation;
			nodes[i].cell->searchIndex = static_cast<int32_t>(i);
		}
	}

	bool Path::step(int64_t a_nodes) {
		if (finishedFlag) { return true; }
		if (!started) { begin(); } else { resume(); }
		stepSearched = 0;
		while (!open.empty()) {
			if (a_nodes > 0 && stepSearched >= a_nodes) { return false; }
			int32_t index = popOpen();
			if (index < 0) { continue; }
			nodes[index].closed = true;
			++searched;
			++stepSearched;
			MapNode& cell = *nodes[index].cell;
			const float costToArrive = nodes[index].costToArrive;

			auto nodeDistance = static_cast<PointPrecision>(distance(cell.position(), goalPosition));
			if (best < 0 || nodeDistance < bestDistance) {
				bestDistance = nodeDistance;
				best = index;
			}
			if (cell.position() == goalPosition || nodeDistance < minimumDistance || equals(nodeDistance, minimumDistance)) {
				found = true;
				current = index;
				finishedFlag = true;
				return true;
			}

			// A diagonal step sweeps the footprint through both orthogonal neighbours; movement walks
			// the straight line and enters one of them first, so a corner is only cut when both are clear
			static const Point<int> offsets[8] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 } };
			auto cornerClear = [&](size_t a_edge, bool a_static) {
				if (a_edge < 4) { return true; }
				Point<int> across(cell.position().x + offsets[a_edge].x, cell.position().y);
				Point<int> down(cell.position().x, cell.position().y + offsets[a_edge].y);
				return a_static ? (map->clearedForSizeStatic(across, unitSize) && map->clearedForSizeStatic(down, unitSize))
				                : (map->clearedForSize(across, unitSize) && map->clearedForSize(down, unitSize));
			};
			if (!softDynamic) {
				for (size_t i = 0; i < cell.size(); ++i) {
					MapNode* next = cell[i];
					if (next == nullptr || !next->clearedForSize(unitSize) || !cornerClear(i, false)) { continue; }
					bool corner = i >= 4;
					visit(*next, index, costToArrive + next->totalCost() * (corner ? 1.4f : 1.0f), corner, generation);
				}
			} else {
				size_t count = map->corners() ? 8 : 4;
				for (size_t i = 0; i < count; ++i) {
					Point<int> nextPosition(cell.position().x + offsets[i].x, cell.position().y + offsets[i].y);
					if (!map->inBounds(nextPosition) || !map->clearedForSizeStatic(nextPosition, unitSize) || !cornerClear(i, true)) { continue; }
					MapNode& next = map->get(nextPosition);
					bool corner = i >= 4;
					float penalty = SOFT_OCCUPANCY_COST * static_cast<float>(map->occupiedCellsExcept(nextPosition, unitSize, owner));
					visit(next, index, costToArrive + (next.totalCost() + penalty) * (corner ? 1.4f : 1.0f), corner, generation);
				}
			}
		}
		finishedFlag = true;
		return true;
	}

	const std::vector<PathNode>& Path::result() {
		if (!started) { begin(); }
		int32_t target = current;
		if (!found) {
			target = (best < 0 || (distance(goalPosition, startPosition) < distance(goalPosition, nodes[best].cell->position()))) ? -1 : best;
		}

		endPosition = target >= 0 ? nodes[target].cell->position() : startPosition;

		pathNodes.clear();
		for (int32_t index = target; index >= 0; index = nodes[index].parent) {
			pathNodes.push_back({ nodes[index].cell->position(), nodes[index].cell->baseCost() * (nodes[index].corner ? 1.4f : 1.0f) });
		}
		std::reverse(pathNodes.begin(), pathNodes.end());
		return pathNodes;
	}

	void NavigationAgent::update(double a_dt) {
		if (updating) { return; }
		if (map->tickDriven()) {
			if (lastUpdateTick == map->tick()) { return; }
			lastUpdateTick = map->tick();
		}
		updating = true;
		SCOPE_EXIT{ updating = false; };
		ourDt = a_dt;
		map->tickBudget();

		auto cellNow = currentCell();
		if (cellNow != lastProgressCell) {
			lastProgressCell = cellNow;
			ticksWithoutProgress = 0;
			blockedSignalFired = false;
		} else if (pathfinding()) {
			++ticksWithoutProgress;
		}

		if (waitingForPlacement) {
			blockMap();
			if (waitingForPlacement) {
				placeNearby();
			}
			if (waitingForPlacement) {
				return;
			}
		}
		if (pathfinding() && attemptToRecalculate() && !calculatedPath.empty() && currentPathIndex < calculatedPath.size()) {
			auto direction = (ourPosition - desiredPositionFromCalculatedPathIndex(currentPathIndex)).normalized();

			auto originalPathIndex = currentPathIndex;
			PointPrecision totalDistanceToTravel = static_cast<PointPrecision>(a_dt) * ourSpeed;

			if (currentPathIndex < calculatedPath.size() - 1) {
				if (currentCell() == calculatedPath[currentPathIndex].position()) {
					++currentPathIndex;
				}
			}
			advanceThroughVisibleNodes();
			while (pathfinding() && totalDistanceToTravel > 0.0f) {
				if (currentPathIndex >= calculatedPath.size()) { break; }
				auto previousGridSquare = currentCell();
				auto desiredPosition = desiredPositionFromCalculatedPathIndex(currentPathIndex);
				PointPrecision distanceToNextNode = static_cast<PointPrecision>(distance(ourPosition, desiredPosition));
				PointPrecision maxDistance = std::min(totalDistanceToTravel, distanceToNextNode);

				auto movement = (desiredPosition - ourPosition).normalized() * maxDistance;
				auto newPosition = ourPosition + movement;
				auto newGridSquare = cellFor(newPosition);

				if (newGridSquare != previousGridSquare) {
					unblockMap();
					if (!map->clearedForSize(newGridSquare, unitSize)) {
						blockMap();
						if (newGridSquare == blockedCell) { ++blockedTicks; } else { blockedCell = newGridSquare; blockedTicks = 1; }
						auto resolution = negotiateBlockedCell(newGridSquare);
						if (resolution == BlockedResolution::freed && map->clearedForSize(newGridSquare, unitSize)) {
							unblockMap();
							ourPosition = newPosition;
							ourPosition.z = 0;
							blockMap();
							blockedTicks = 0;
						} else if (resolution == BlockedResolution::none) {
							markDirty();
							if (!blockedSignalFired) {
								blockedSignalFired = true;
								auto self = shared_from_this();
								onBlockedSignal(self);
							}
						}
					}else{
						ourPosition = newPosition;
						ourPosition.z = 0;
						blockMap();
						blockedTicks = 0;
					}
				} else {
					ourPosition = newPosition;
					ourPosition.z = 0;
				}

				totalDistanceToTravel -= maxDistance;
				if (totalDistanceToTravel > 0.0f && currentPathIndex < calculatedPath.size()) {
					++currentPathIndex;
					advanceThroughVisibleNodes();
				}
				if (followingField && currentPathIndex + FLOW_EXTEND_AT >= calculatedPath.size()) {
					followFlowField();
				}
				if (!attemptToRecalculate()) { break; }
			}
			if (originalPathIndex != currentPathIndex) {
				updateObservedNodes();
			}

			if (!pathfinding()) {
				receivers.clear();
				auto self = shared_from_this();
				onArriveSignal(self);
			}
		}
	}

	Point<int> NavigationAgent::nextCellTowardGoal() const {
		auto here = currentCell();
		auto direction = ourGoal - ourPosition;
		if (direction.x == 0.0f && direction.y == 0.0f) { return here; }
		int stepX = std::abs(direction.x) > 0.3f * std::abs(direction.y) ? (direction.x > 0.0f ? 1 : -1) : 0;
		int stepY = std::abs(direction.y) > 0.3f * std::abs(direction.x) ? (direction.y > 0.0f ? 1 : -1) : 0;
		if (!map->corners() && stepX != 0 && stepY != 0) {
			if (std::abs(direction.x) >= std::abs(direction.y)) { stepY = 0; } else { stepX = 0; }
		}
		return Point<int>(here.x + stepX, here.y + stepY);
	}

	// The path's known blockage first (an urgent agent waiting for budget still negotiates with
	// whoever holds its next cell), then the cell toward the goal
	bool NavigationAgent::negotiatePathAhead() {
		if (blockedAtPathIndex >= 0 && static_cast<size_t>(blockedAtPathIndex) < calculatedPath.size()) {
			auto cell = calculatedPath[static_cast<size_t>(blockedAtPathIndex)].position();
			if (map->agentAt(cell) != nullptr) {
				if (cell == blockedCell) { ++blockedTicks; } else { blockedCell = cell; blockedTicks = 1; }
				if (negotiateBlockedCell(cell) != BlockedResolution::none) { return true; }
			}
		}
		return negotiateAhead();
	}

	// A blocked or waiting agent looks one cell toward its goal: an agent there is negotiated with
	bool NavigationAgent::negotiateAhead() {
		auto ahead = nextCellTowardGoal();
		if (ahead == currentCell() || map->agentAt(ahead) == nullptr) { return false; }
		if (ahead == blockedCell) { ++blockedTicks; } else { blockedCell = ahead; blockedTicks = 1; }
		return negotiateBlockedCell(ahead) != BlockedResolution::none;
	}

	bool NavigationAgent::hasRightOfWayOver(const NavigationAgent& a_other) const {
		if (ticksWithoutProgress != a_other.ticksWithoutProgress) { return ticksWithoutProgress > a_other.ticksWithoutProgress; }
		float mine = remainingDistance();
		float theirs = a_other.remainingDistance();
		if (mine != theirs) { return mine < theirs; }
		return agentId < a_other.agentId;
	}

	bool NavigationAgent::movingSameWay(const NavigationAgent& a_other) const {
		auto mine = ourGoal - ourPosition;
		auto theirs = a_other.ourGoal - a_other.ourPosition;
		if ((mine.x == 0.0f && mine.y == 0.0f) || (theirs.x == 0.0f && theirs.y == 0.0f)) { return false; }
		mine = mine.normalized();
		theirs = theirs.normalized();
		return mine.x * theirs.x + mine.y * theirs.y > 0.5f;
	}

	NavigationAgent* NavigationAgent::blockerAt(const Point<int>& a_topLeft) const {
		for (int x = a_topLeft.x; x < a_topLeft.x + unitSize; ++x) {
			for (int y = a_topLeft.y; y < a_topLeft.y + unitSize; ++y) {
				auto* occupant = map->agentAt(Point<int>(x, y));
				if (occupant != nullptr && occupant != this) { return occupant; }
			}
		}
		return nullptr;
	}

	NavigationAgent::BlockedResolution NavigationAgent::negotiateBlockedCell(const Point<int>& a_cell) {
		if (footprintDisabled) { return BlockedResolution::none; }
		NavigationAgent* blocker = blockerAt(a_cell);
		if (blocker == nullptr || blocker->footprintDisabled) { return BlockedResolution::none; }
		blocker->noteFollower();

		// Pull ordering: whoever is in front takes its turn first; it may simply move on
		if (map->tickDriven() && blocker->lastUpdateTick != map->tick() && !blocker->updating) {
			blocker->update(ourDt);
			if (currentCell() == a_cell) { return BlockedResolution::swapped; }
			if (map->clearedForSize(a_cell, unitSize)) { return BlockedResolution::freed; }
			blocker = blockerAt(a_cell);
			if (blocker == nullptr || blocker->footprintDisabled) { return BlockedResolution::none; }
		}

		auto ourCell = currentCell();
		bool blockerMoving = blocker->pathfinding();
		bool swapCooled = map->tick() - lastSwapTick >= SWAP_COOLDOWN_TICKS && map->tick() - blocker->lastSwapTick >= SWAP_COOLDOWN_TICKS;
		if (blockerMoving && swapCooled && blocker->unitSize == unitSize && footprintsOverlap(blocker->nextCellTowardGoal(), unitSize, ourCell, unitSize)) {
			unblockMap();
			blocker->unblockMap();
			std::swap(ourPosition, blocker->ourPosition);
			blocker->blockMap();
			blockMap();
			blockedTicks = 0;
			lastSwapTick = map->tick();
			blocker->lastSwapTick = map->tick();
			return BlockedResolution::swapped;
		}
		// Follow a leader that is going our way and still making progress; a stuck leader is
		// negotiated with like anyone else
		if (blockerMoving && movingSameWay(*blocker) && blocker->ticksWithoutProgress < FOLLOW_PATIENCE_TICKS) {
			return BlockedResolution::none;
		}
		bool mayPush = blockerMoving ? (blockedTicks >= CROSSING_PUSH_TICKS && hasRightOfWayOver(*blocker)) : blockedTicks >= PUSH_ASIDE_TICKS;
		if (mayPush && blocker->stepAside(ourCell, unitSize, PUSH_CASCADE_DEPTH)) {
			return BlockedResolution::freed;
		}
		return BlockedResolution::none;
	}

	bool NavigationAgent::stepAside(const Point<int>& a_awayFrom, int a_awayFromSize, int a_depth) {
		if (footprintDisabled || updating) { return false; }
		auto here = currentCell();
		Point<int> away(here.x - a_awayFrom.x, here.y - a_awayFrom.y);
		Point<int> direction(std::clamp(away.x, -1, 1), std::clamp(away.y, -1, 1));
		if (direction.x == 0 && direction.y == 0) { direction = Point<int>(1, 0); }
		Point<int> steps[8] = { { -direction.y, direction.x }, { direction.y, -direction.x }, { direction.x, direction.y },
			{ direction.x - direction.y, direction.y + direction.x }, { direction.x + direction.y, direction.y - direction.x },
			{ -direction.x - direction.y, direction.x - direction.y }, { direction.y - direction.x, -direction.x - direction.y }, { -direction.x, -direction.y } };
		bool idle = ourGoal == ourPosition;
		unblockMap();
		for (int reach = 1; reach <= unitSize; ++reach) {
			for (auto& step : steps) {
				Point<int> unit(std::clamp(step.x, -1, 1), std::clamp(step.y, -1, 1));
				if ((unit.x == 0 && unit.y == 0) || (!map->corners() && unit.x != 0 && unit.y != 0)) { continue; }
				Point<int> candidate(here.x + unit.x * reach, here.y + unit.y * reach);
				if (footprintsOverlap(candidate, unitSize, a_awayFrom, a_awayFromSize) || !map->clearedForSizeStatic(candidate, unitSize)) { continue; }
				if (!map->clearedForSize(candidate, unitSize)) {
					if (a_depth <= 0) { continue; }
					NavigationAgent* other = blockerAt(candidate);
					if (other == nullptr || other->pathfinding() || !other->stepAside(here, unitSize, a_depth - 1) || !map->clearedForSize(candidate, unitSize)) { continue; }
				}
				ourPosition = cast<PointPrecision>(candidate) + centerOffset;
				if (idle) { ourGoal = ourPosition; }
				blockMap();
				markDirty();
				return true;
			}
		}
		blockMap();
		return false;
	}

	bool NavigationAgent::lineOfSight(const Point<PointPrecision>& a_from, const Point<int>& a_toCell) const {
		const float toX = static_cast<float>(a_toCell.x) + 0.5f;
		const float toY = static_cast<float>(a_toCell.y) + 0.5f;
		const float dx = toX - a_from.x;
		const float dy = toY - a_from.y;
		int x = static_cast<int>(std::floor(a_from.x));
		int y = static_cast<int>(std::floor(a_from.y));
		const int stepX = dx > 0.0f ? 1 : -1;
		const int stepY = dy > 0.0f ? 1 : -1;
		const float infinity = std::numeric_limits<float>::infinity();
		const float deltaX = dx != 0.0f ? 1.0f / std::abs(dx) : infinity;
		const float deltaY = dy != 0.0f ? 1.0f / std::abs(dy) : infinity;
		float nextX = dx != 0.0f ? ((stepX > 0 ? static_cast<float>(x + 1) - a_from.x : a_from.x - static_cast<float>(x)) * deltaX) : infinity;
		float nextY = dy != 0.0f ? ((stepY > 0 ? static_cast<float>(y + 1) - a_from.y : a_from.y - static_cast<float>(y)) * deltaY) : infinity;
		int remaining = std::abs(a_toCell.x - x) + std::abs(a_toCell.y - y) + 2;
		while ((x != a_toCell.x || y != a_toCell.y) && remaining-- > 0) {
			if (nextX <= nextY) { x += stepX; nextX += deltaX; } else { y += stepY; nextY += deltaY; }
			if (!map->clearedForSize(Point<int>(x, y), unitSize)) { return false; }
		}
		return x == a_toCell.x && y == a_toCell.y;
	}

	void NavigationAgent::advanceThroughVisibleNodes() {
		size_t farthest = currentPathIndex;
		size_t limit = std::min(calculatedPath.size(), currentPathIndex + MAX_LOOKAHEAD + 1);
		for (size_t i = currentPathIndex + 1; i < limit; ++i) {
			if (!lineOfSight(ourPosition, calculatedPath[i].position())) { break; }
			farthest = i;
		}
		currentPathIndex = farthest;
	}

	void NavigationAgent::incrementPathIndex() {
		++currentPathIndex;
		blockedNodeObservers.erase(std::remove_if(blockedNodeObservers.begin(), blockedNodeObservers.end(), [](auto &blockedNode) {
			return --blockedNode.gridMovesLeftUntilExpires <= 0;
		}), blockedNodeObservers.end());
	}

	// Queue order each tick: agents others are waiting behind, then longest without progress, then
	// closest to its goal, then id — the front of a column re-plans before the back of it
	// Every cell's clearance computed once from the static/dynamic blocks (never from a neighbour's
	// cache) as soon as the map is owned by a shared_ptr; the incremental rule then only ever reads
	// initialised neighbours
	void Map::ensureCellsInitialized() {
		if (cellsInitialized) { return; }
		cellsInitialized = true;
		for (auto&& column : squares) {
			for (auto&& square : column) {
				square.lazyInitialize();
			}
		}
	}

	// MV_PATH_DIAG=1: every 5 s report how many agents stand still while pathfinding (and why)
	void Map::reportStalls() const {
		static const bool enabled = std::getenv("MV_PATH_DIAG") != nullptr;
		if (!enabled || tickCount % 300 != 0) { return; }
		std::vector<const NavigationAgent*> agents;
		for (auto&& column : squares) {
			for (auto&& square : column) {
				if (square.occupant != nullptr && std::find(agents.begin(), agents.end(), square.occupant) == agents.end()) { agents.push_back(square.occupant); }
			}
		}
		int moving = 0, stalled = 0, idle = 0, shown = 0;
		std::string detail;
		for (auto* agent : agents) {
			if (!agent->pathfinding()) { ++idle; continue; }
			if (agent->ticksWithoutProgress < 60) { ++moving; continue; }
			++stalled;
			if (shown++ < 3) { detail += "\n    " + agent->debugState(); }
		}
		std::cerr << "[path] tick " << tickCount << " agents " << agents.size() << " moving " << moving << " stalled " << stalled << " idle " << idle
			<< " queue " << searchQueue.size() << " budget used " << nodesUsedThisFrame << "/" << FRAME_NODE_BUDGET << detail << std::endl;
	}

	std::shared_ptr<FlowField> Map::flowFieldFor(const Point<int>& a_goal, PointPrecision a_radius, int a_unitSize) {
		for (auto& field : flowFields) {
			if (field->goal() == a_goal && field->unitSize() == a_unitSize && equals(field->radius(), a_radius)) {
				if (field->generation == flowGeneration) { field->lastUsedTick = tickCount; return field; }
				if (fieldsComputedThisTick >= FIELDS_PER_TICK) { return nullptr; }
				++fieldsComputedThisTick;
				field = std::make_shared<FlowField>(shared_from_this(), a_goal, a_radius, a_unitSize);
				field->generation = flowGeneration;
				field->lastUsedTick = tickCount;
				return field;
			}
		}
		if (fieldsComputedThisTick >= FIELDS_PER_TICK) { return nullptr; }
		++fieldsComputedThisTick;
		auto field = std::make_shared<FlowField>(shared_from_this(), a_goal, a_radius, a_unitSize);
		field->generation = flowGeneration;
		field->lastUsedTick = tickCount;
		if (flowFields.size() >= MAX_FLOW_FIELDS) {
			auto oldest = std::min_element(flowFields.begin(), flowFields.end(), [](const std::shared_ptr<FlowField>& a_lhs, const std::shared_ptr<FlowField>& a_rhs) { return a_lhs->lastUsedTick < a_rhs->lastUsedTick; });
			*oldest = field;
		} else {
			flowFields.push_back(field);
		}
		return field;
	}

	FlowField::FlowField(const std::shared_ptr<Map>& a_map, const Point<int>& a_goal, PointPrecision a_radius, int a_unitSize) :
		goalCell(a_goal),
		goalRadius(a_radius),
		footprint(a_unitSize),
		width(a_map->size().width),
		height(a_map->size().height),
		corners(a_map->corners()),
		distances(static_cast<size_t>(std::max(0, a_map->size().width)) * static_cast<size_t>(std::max(0, a_map->size().height)), UNREACHABLE) {
		compute(a_map);
	}

	// Multi-source Dijkstra outward from every cell inside the goal region over static passability
	void FlowField::compute(const std::shared_ptr<Map>& a_map) {
		struct Entry { float cost; int x; int y; };
		auto later = [](const Entry& a_lhs, const Entry& a_rhs) { return a_lhs.cost > a_rhs.cost; };
		std::vector<Entry> open;
		int reach = static_cast<int>(std::ceil(goalRadius));
		for (int x = goalCell.x - reach; x <= goalCell.x + reach; ++x) {
			for (int y = goalCell.y - reach; y <= goalCell.y + reach; ++y) {
				Point<int> cell(x, y);
				auto cellDistance = static_cast<PointPrecision>(distance(cell, goalCell));
				if (!(cellDistance < goalRadius || equals(cellDistance, goalRadius))) { continue; }
				if (!a_map->clearedForSizeStatic(cell, footprint)) { continue; }
				distances[index(cell)] = 0.0f;
				open.push_back({ 0.0f, x, y });
				std::push_heap(open.begin(), open.end(), later);
			}
		}
		static const Point<int> offsets[8] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 } };
		size_t count = corners ? 8 : 4;
		while (!open.empty()) {
			std::pop_heap(open.begin(), open.end(), later);
			Entry current = open.back();
			open.pop_back();
			Point<int> here(current.x, current.y);
			if (current.cost > distances[index(here)]) { continue; }
			for (size_t i = 0; i < count; ++i) {
				Point<int> next(current.x + offsets[i].x, current.y + offsets[i].y);
				if (!inField(next) || !a_map->clearedForSizeStatic(next, footprint)) { continue; }
				if (i >= 4) {
					Point<int> across(current.x + offsets[i].x, current.y);
					Point<int> down(current.x, current.y + offsets[i].y);
					if (!a_map->clearedForSizeStatic(across, footprint) || !a_map->clearedForSizeStatic(down, footprint)) { continue; }
				}
				float stepCost = a_map->get(next).baseCost() * (i >= 4 ? 1.4f : 1.0f);
				float total = current.cost + stepCost;
				if (total < distances[index(next)]) {
					distances[index(next)] = total;
					open.push_back({ total, next.x, next.y });
					std::push_heap(open.begin(), open.end(), later);
				}
			}
		}
	}

	// Reads the field with the same no-corner-cutting rule the field was built with: a diagonal
	// step is only taken when both orthogonal footprints are clear, exactly as movement walks it
	std::vector<PathNode> FlowField::descend(const std::shared_ptr<Map>& a_map, const Point<int>& a_from, size_t a_maxSteps, const NavigationAgent* a_self) const {
		std::vector<PathNode> nodes;
		if (!reachable(a_from)) { return nodes; }
		static const Point<int> offsets[8] = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 }, { 1, -1 } };
		size_t count = corners ? 8 : 4;
		Point<int> here = a_from;
		nodes.push_back({ here, 1.0f });
		for (size_t step = 0; step < a_maxSteps; ++step) {
			float current = distances[index(here)];
			if (current <= 0.0f) { break; }
			// The immediate step prefers a footprint free of other agents over a lower distance;
			// later steps are re-derived before they are walked, so statics alone rank them
			const bool immediate = step == 0;
			float bestFree = current, bestAny = current;
			Point<int> freeCell = here, anyCell = here;
			bool freeCorner = false, anyCorner = false;
			for (size_t i = 0; i < count; ++i) {
				Point<int> next(here.x + offsets[i].x, here.y + offsets[i].y);
				if (!inField(next)) { continue; }
				Point<int> across(here.x + offsets[i].x, here.y);
				Point<int> down(here.x, here.y + offsets[i].y);
				if (i >= 4 && (!a_map->clearedForSizeStatic(across, footprint) || !a_map->clearedForSizeStatic(down, footprint))) { continue; }
				float candidate = distances[index(next)];
				if (candidate >= bestAny && candidate >= bestFree) { continue; }
				if (candidate < bestAny) { bestAny = candidate; anyCell = next; anyCorner = i >= 4; }
				if (immediate && candidate < bestFree && a_map->occupiedCellsExcept(next, footprint, a_self) == 0 &&
					(i < 4 || (a_map->occupiedCellsExcept(across, footprint, a_self) == 0 && a_map->occupiedCellsExcept(down, footprint, a_self) == 0))) {
					bestFree = candidate; freeCell = next; freeCorner = i >= 4;
				}
			}
			Point<int> bestCell = (immediate && freeCell != here) ? freeCell : anyCell;
			bool bestCorner = (immediate && freeCell != here) ? freeCorner : anyCorner;
			if (bestCell == here) { break; }
			here = bestCell;
			nodes.push_back({ here, bestCorner ? 1.4f : 1.0f });
		}
		return nodes;
	}

	void Map::beginTick() {
		ensureCellsInitialized();
		reportStalls();
		fieldsComputedThisTick = 0;
		nodesUsedThisFrame = 0;
		lastBudgetReset = std::chrono::steady_clock::now();
		drivenByTicks = true;
		++tickCount;
		std::vector<std::shared_ptr<NavigationAgent>> queued;
		for (auto& weak : searchQueue) {
			if (auto agent = weak.lock()) { queued.push_back(agent); }
		}
		searchQueue.clear();
		std::stable_sort(queued.begin(), queued.end(), [](const std::shared_ptr<NavigationAgent>& a_lhs, const std::shared_ptr<NavigationAgent>& a_rhs) {
			int lhsPriority = a_lhs->searchPriority();
			int rhsPriority = a_rhs->searchPriority();
			if (lhsPriority != rhsPriority) { return lhsPriority > rhsPriority; }
			float lhsRemaining = a_lhs->remainingDistance();
			float rhsRemaining = a_rhs->remainingDistance();
			if (lhsRemaining != rhsRemaining) { return lhsRemaining < rhsRemaining; }
			return a_lhs->agentId < a_rhs->agentId;
		});
		size_t served = 0;
		for (; served < queued.size() && remainingBudget() >= MIN_SEARCH_NODES; ++served) {
			queued[served]->runQueuedSearch();
		}
		for (; served < queued.size(); ++served) {
			searchQueue.push_back(queued[served]);
		}
	}

	void Map::requestSearch(const std::shared_ptr<NavigationAgent>& a_agent, bool a_urgent) {
		if (a_agent->searchQueued) { a_agent->urgentRequest = a_agent->urgentRequest || a_urgent; return; }
		a_agent->searchQueued = true;
		a_agent->urgentRequest = a_urgent;
		a_agent->queuedSinceTick = tickCount;
		searchQueue.push_back(a_agent);
	}

	void NavigationAgent::performSearch() {
		softSearch = ticksWithoutProgress >= SOFT_SEARCH_TICKS;
		int64_t searchLimit = std::min(map->remainingBudget(), DEFAULT_SEARCH_LIMIT);
		recalculate(searchLimit);
		if (ourPath) { map->consumeBudget(std::max<int64_t>(1, std::min(searchLimit, ourPath->stepNodes()))); }
	}

	// Served from Map::beginTick, outside update(): guard it the same way so a pulled neighbour
	// cannot re-enter this agent while its search is mid-flight
	void NavigationAgent::runQueuedSearch() {
		if (updating) { return; }
		updating = true;
		SCOPE_EXIT{ updating = false; };
		searchQueued = false;
		urgentRequest = false;
		lastServedTick = map->tick();
		bool stillNeeded = dirtyPath || calculatedPath.empty() ||
		                   (calculatedPath.size() > 1 && currentPathIndex >= calculatedPath.size()) || needsUrgentRecalculation();
		if (!stillNeeded || footprintDisabled && waitingForPlacement) { return; }
		performSearch();
		evaluateSearchResult();
	}

	bool NavigationAgent::attemptToRecalculate() {
		bool mustRecalc = dirtyPath || calculatedPath.empty() ||
		                  (calculatedPath.size() > 1 && currentPathIndex >= calculatedPath.size());
		bool urgentRecalc = needsUrgentRecalculation();

		if (recalcRetryCountdown > 0 && !urgentRecalc) {
			--recalcRetryCountdown;
			if (negotiatePathAhead()) {
				dirtyPath = true;
				clearRetryBackoff();
			}
			return false;
		}

		if (mustRecalc || urgentRecalc) {
			if (followFlowField()) { return evaluateSearchResult(); }
			// Contended (someone is queued) or too little budget for a meaningful search: take a
			// place in the map's queue instead of racing agents that update earlier in the tick
			if (searchQueued || !map->searchQueueEmpty() || map->remainingBudget() < Map::MIN_SEARCH_NODES) {
				if (!searchQueued) { map->requestSearch(shared_from_this(), urgentRecalc); }
				if (urgentRecalc && negotiatePathAhead()) {
					blockedAtPathIndex = -1;
					dirtyPath = true;
				}
				return !urgentRecalc && !searchInProgress && !calculatedPath.empty() && currentPathIndex < calculatedPath.size();
			}
			performSearch();
		}
		return evaluateSearchResult();
	}

	bool NavigationAgent::evaluateSearchResult() {
		if (searchInProgress) {
			return false;
		}
		if (calculatedPath.size() <= 1) {
			if (calculatedPath.empty() || calculatedPath[0].position() != goalCell()) {
				if (negotiatePathAhead()) {
					dirtyPath = true;
					clearRetryBackoff();
					return false;
				}
				if (!blockedSignalFired) {
					blockedSignalFired = true;
					onBlockedSignal(shared_from_this());
				}
				dirtyPath = true;
				deferRetryAfterFailedSearch();
				return false;
			}
		}
		clearRetryBackoff();

		if (calculatedPath.size() > currentPathIndex + 1) {
			auto nextPos = calculatedPath[currentPathIndex + 1].position();
			unblockMap();
			bool nextBlocked = !map->clearedForSize(nextPos, unitSize);
			blockMap();

			if (nextBlocked) {
				markPathBlockedAt(static_cast<int>(currentPathIndex + 1));
				if (map->agentAt(nextPos) != nullptr && negotiatePathAhead()) {
					blockedAtPathIndex = -1;
					// Freed: keep walking this tick. Swapped: we stand elsewhere now, so re-plan first.
					unblockMap();
					bool clearedNow = map->clearedForSize(nextPos, unitSize);
					blockMap();
					if (clearedNow && currentCell() != nextPos) { return true; }
					dirtyPath = true;
					return false;
				}
				if (needsUrgentRecalculation() && map->remainingBudget() < Map::MIN_SEARCH_NODES) {
					onBlockedSignal(shared_from_this());
					return false;
				}
			}
		}

		return true;
	}

	void NavigationAgent::updateObservedNodes() {
		static unsigned int pathId = 0;
		receivers.clear();
		costs.clear();
		pathId++;
		for (auto i = currentPathIndex; i < calculatedPath.size(); ++i) {
			auto temporaryCostAmount = (ourSpeed*4.0f) - (i - currentPathIndex);
			if (temporaryCostAmount <= 0) {
				break;
			}

			costs.push_back(TemporaryCost(map, calculatedPath[i].position(), temporaryCostAmount));

			auto& mapNode = map->get(calculatedPath[i].position());
			int pathIndex = static_cast<int>(i);

			if(unitSize <= 1) {
				auto receiver = mapNode.onBlock.connect([=](const std::shared_ptr<Map> &a_map, const Point<int> &a_position) {
					if (!activeUpdate && !overlaps(a_position)) {
						blockedNodeObservers.emplace_back(this, a_map, a_position, pathId);
						markPathBlockedAt(pathIndex);  // Track blockage location instead of immediate dirty
					}
				});
				receivers.push_back(receiver);
			} else {
				auto receiver = mapNode.onClearanceChange.connect([=](const std::shared_ptr<Map> &a_map, const Point<int> &a_position) {
					if (!activeUpdate && size() <= a_map->get(a_position).clearance() && !overlaps(a_position)) {
						blockedNodeObservers.emplace_back(this, a_map, a_position, pathId);
						markPathBlockedAt(pathIndex);  // Track blockage location instead of immediate dirty
					}
				});
				receivers.push_back(receiver);
			}
		}
	}

}
