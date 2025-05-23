#include "pathfinding.h"
#include "cereal/archives/json.hpp"

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
		return *this;
	}

	float MapNode::baseCost() const {
		return travelCost;
	}

	void MapNode::baseCost(float a_newCost) {
		auto oldCost = travelCost;
		travelCost = a_newCost;
		if (oldCost != travelCost) {
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
							int smallestClearance = std::min(map->get(locations[0]).clearanceAmount + 1, MAXIMUM_CLEARANCE);
							for (auto i = 1; i < locations.size(); ++i) {
								auto clearanceCheck = std::min(map->get(locations[i]).clearanceAmount + 1, MAXIMUM_CLEARANCE);
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

	void Map::resize(const Size<int> &a_size, float a_defaultCost /*= 1.0f*/) {
		auto ourSize = size();
		if (a_size.width < ourSize.width) {
			squares.resize(a_size.width);
		}
		if (a_size.height < ourSize.height) {
			for (auto&& column : squares) {
				column.resize(a_size.height);
			}
		}

		for (int x = ourSize.width; x < a_size.width; ++x) {
			std::vector<MapNode> column;
			column.reserve(a_size.height);
			for (int y = ourSize.height; y < a_size.height; ++y) {
				column.emplace_back(*this, Point<int>(x, y), a_defaultCost, corners());
			}
			squares.push_back(std::move(column));
		}

		hookUpObservation();
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

	void Path::calculate() {
		open.clear();
		closed.clear();

		insertIntoOpen(startPosition, 0.0f);
		found = false;
		PathCalculationNode* currentNode = nullptr;
		PathCalculationNode* bestNode = nullptr;
		PointPrecision bestDistance = -1;
		int64_t totalSearched = 0;

		while (!open.empty() && ((maxSearchNodes > 0 && totalSearched++ < maxSearchNodes) || maxSearchNodes <= 0)) {
			closed.push_back(open.back());
			currentNode = &closed.back();
			open.pop_back();

			auto nodeDistance = static_cast<PointPrecision>(distance(currentNode->position(), goalPosition));
			if (bestNode == nullptr || nodeDistance < bestDistance) {
				bestDistance = nodeDistance;
				bestNode = currentNode;
			}
			if (currentNode->position() == goalPosition || nodeDistance < minimumDistance || equals(nodeDistance, minimumDistance)) {
				found = true;
				break; //success
			}

			auto& mapNode = currentNode->mapNode();
			for (size_t i = 0; i < mapNode.size(); ++i) {
				if (mapNode[i] != nullptr) {
					if (mapNode[i]->clearedForSize(unitSize)) {
						insertIntoOpen(mapNode[i]->position(), mapNode[i]->totalCost(), currentNode, i >= 4);
					}
				}
			}

		}

		if (!found) {
			currentNode = (bestNode == nullptr || (distance(goalPosition, startPosition) < distance(goalPosition, bestNode->position()))) ? nullptr : bestNode;
		}

		endPosition = currentNode != nullptr ? currentNode->position() : startPosition;

		pathNodes.clear();
		while (currentNode) {
			pathNodes.push_back({ currentNode->position(), currentNode->mapNode().baseCost() * (currentNode->corner() ? 1.4f : 1.0f) });
			currentNode = currentNode->parent();
		}
		std::reverse(pathNodes.begin(), pathNodes.end());
	}

	void Path::insertIntoOpen(const Point<int> &a_position, float a_startCost, PathCalculationNode* a_parent /*= nullptr*/, bool a_isCorner /*= false*/) {
		if (!existsBetter(a_position, a_startCost)) {
			insertReverseSorted(open, Path::PathCalculationNode(goalPosition, map->get(a_position), (a_parent ? a_parent->estimatedTotalCost() : 0.0f) + (a_startCost * (a_isCorner ? 1.4f : 1.0f)), a_parent));
		}
	}

	bool Path::existsBetter(const Point<int> &a_position, float a_startCost) {
		if (std::find_if(closed.cbegin(), closed.cend(), [&](const PathCalculationNode &a_node) {return a_node == a_position; }) != closed.cend()) {
			return true;
		}
		auto foundOpen = std::find_if(open.cbegin(), open.cend(), [&](const PathCalculationNode &a_node) {return a_node == a_position; });
		if (foundOpen != open.cend()) {
			if (foundOpen->costToArrive() > a_startCost) {
				open.erase(foundOpen);
				return false;
			}
			else {
				return true;
			}
		}
		return false;
	}

	void NavigationAgent::update(double a_dt) {
		if (waitingForPlacement) {
			if (canPlaceOnMapAtCurrentPosition()) {
				waitingForPlacement = false;
				blockMap();
			} else {
				return;
			}
		}
		if (pathfinding() && (attemptToRecalculate() && !calculatedPath.empty() && currentPathIndex < calculatedPath.size())) {
			auto direction = (ourPosition - desiredPositionFromCalculatedPathIndex(currentPathIndex)).normalized();

			auto originalPathIndex = currentPathIndex;
			PointPrecision totalDistanceToTravel = static_cast<PointPrecision>(a_dt) * ourSpeed;

			if (currentPathIndex < calculatedPath.size() - 1) {
				if (cast<int>(ourPosition) == calculatedPath[currentPathIndex].position()) {
					++currentPathIndex;
				}
			}
			while (pathfinding() && totalDistanceToTravel > 0.0f) {
				auto previousGridSquare = cast<int>(ourPosition);
				auto desiredPosition = desiredPositionFromCalculatedPathIndex(currentPathIndex);
				PointPrecision distanceToNextNode = static_cast<PointPrecision>(distance(ourPosition, desiredPosition));
				PointPrecision maxDistance = std::min(totalDistanceToTravel, distanceToNextNode);

				auto movement = (desiredPosition - ourPosition).normalized() * maxDistance;
				auto newPosition = ourPosition + movement;
				auto newGridSquare = cast<int>(newPosition);

				if (newGridSquare != previousGridSquare) {
					unblockMap();
					if (!map->clearedForSize(newGridSquare, unitSize)) {
						blockMap();
						markDirty();
						auto self = shared_from_this();
						onBlockedSignal(self);
					}else{
						ourPosition = newPosition;
						ourPosition.z = 0;
						blockMap();
					}
				} else {
					ourPosition = newPosition;
					ourPosition.z = 0;
				}

				totalDistanceToTravel -= maxDistance;
				if (totalDistanceToTravel > 0.0f && currentPathIndex < calculatedPath.size()) {
					++currentPathIndex;
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

	void NavigationAgent::incrementPathIndex() {
		++currentPathIndex;
		blockedNodeObservers.erase(std::remove_if(blockedNodeObservers.begin(), blockedNodeObservers.end(), [](auto &blockedNode) {
			return --blockedNode.gridMovesLeftUntilExpires <= 0;
		}), blockedNodeObservers.end());
	}

	bool NavigationAgent::attemptToRecalculate() {
		if (dirtyPath || calculatedPath.empty() || (calculatedPath.size() > 1 && currentPathIndex == calculatedPath.size())) {
			recalculate();
		}
		if (calculatedPath.size() == 1 && calculatedPath[0].position() != cast<int>(ourGoal)) {
			markDirty();
			auto self = shared_from_this();
			onBlockedSignal(self);
		}
		if (!dirtyPath && calculatedPath.size() > (currentPathIndex + 1)) {
			unblockMap();
			if (!map->clearedForSize(calculatedPath[currentPathIndex + 1].position(), unitSize)) {
				blockMap();
				markDirty();
				auto self = shared_from_this();
				onBlockedSignal(self);
			} else {
				blockMap();
			}
		}
		return !dirtyPath;
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

			if(unitSize <= 1) {
				auto receiver = mapNode.onBlock.connect([=](const std::shared_ptr<Map> &a_map, const Point<int> &a_position) {
					if (!activeUpdate && !overlaps(a_position)) {
						blockedNodeObservers.emplace_back(this, a_map, a_position, pathId);
						markDirty();
					}
				});
				receivers.push_back(receiver);
			} else {
				auto receiver = mapNode.onClearanceChange.connect([=](const std::shared_ptr<Map> &a_map, const Point<int> &a_position) {
					if (!activeUpdate && size() <= a_map->get(a_position).clearance() && !overlaps(a_position)) {
						blockedNodeObservers.emplace_back(this, a_map, a_position, pathId);
						markDirty();
					}
				});
				receivers.push_back(receiver);
			}
		}
	}

}
