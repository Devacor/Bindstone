#include "pathfinding.h"
#include "cereal/archives/json.hpp"

namespace MV {

	MapNode::MapNode(Map& a_grid, const Point<int> &a_location, float a_cost, bool a_useCorners) :
		initialized(false),
		travelCost(a_cost),
		location(a_location),
		map(&a_grid),
		useCorners(a_useCorners),
		onBlock(onBlockSignal),
		onUnblock(onUnblockSignal),
		onStaticBlock(onStaticBlockSignal),
		onStaticUnblock(onStaticUnblockSignal),
		onCostChange(onCostChangeSignal){
	}

	MapNode::MapNode(const MapNode &a_rhs) :
		initialized(false),
		travelCost(a_rhs.travelCost),
		location(a_rhs.location),
		map(a_rhs.map),
		staticBlockedSemaphore(a_rhs.staticBlockedSemaphore),
		useCorners(a_rhs.useCorners),
		onBlock(onBlockSignal),
		onUnblock(onUnblockSignal),
		onStaticBlock(onStaticBlockSignal),
		onStaticUnblock(onStaticUnblockSignal),
		onCostChange(onCostChangeSignal) {
	}

	MapNode::MapNode() :
		initialized(false),
		map(nullptr),
		useCorners(false),
		onBlock(onBlockSignal),
		onUnblock(onUnblockSignal),
		onStaticBlock(onStaticBlockSignal),
		onStaticUnblock(onStaticUnblockSignal),
		onCostChange(onCostChangeSignal) {
	}

	MapNode& MapNode::operator=(const MapNode &a_rhs) {
		initialized = false;
		travelCost = a_rhs.travelCost;
		location = a_rhs.location;
		map = a_rhs.map;
		staticBlockedSemaphore = a_rhs.staticBlockedSemaphore;
		useCorners = a_rhs.useCorners;
		for (auto&& edge : edges) {
			edge = nullptr;
		}
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
			onCostChangeSignal(map->shared_from_this(), location);
		}
	}

	float MapNode::totalCost() const {
		return travelCost + temporaryCost;
	}

	void MapNode::block() {
		bool wasBlocked = blocked();
		blockedSemaphore++;
		if (!wasBlocked) {
			onBlockSignal(map->shared_from_this(), location);
		}
	}

	void MapNode::unblock() {
		require<ResourceException>(blockedSemaphore > 0, "Error: Block Semaphore overextended in MapNode, something is unblocking excessively.");
		blockedSemaphore--;
		if (!blocked()) {
			onUnblockSignal(map->shared_from_this(), location);
		}
	}

	bool MapNode::blocked() const {
		return staticBlockedSemaphore != 0 || blockedSemaphore != 0;
	}

	void MapNode::staticBlock() {
		bool wasBlocked = blocked();
		++staticBlockedSemaphore;
		auto mapShared = map->shared_from_this();
		if (!wasBlocked) {
			onBlockSignal(mapShared, location);
		}
		if (staticBlockedSemaphore == 1) {
			onStaticBlockSignal(mapShared, location);
		}
	}

	void MapNode::staticUnblock() {
		require<ResourceException>(staticBlockedSemaphore > 0, "Error: Static Block Semaphore overextended in MapNode, something is unblocking excessively.");
		--staticBlockedSemaphore;
		auto mapShared = map->shared_from_this();
		if (!blocked()) {
			onUnblockSignal(mapShared, location);
		}
		if (staticBlockedSemaphore == 0) {
			onStaticUnblockSignal(mapShared, location);
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
			onCostChangeSignal(map->shared_from_this() , location);
		}
	}

	void MapNode::removeTemporaryCost(float a_newTemporaryCost) {
		temporaryCost -= a_newTemporaryCost;
		if (a_newTemporaryCost != 0.0f) {
			onCostChangeSignal(map->shared_from_this(), location);
		}
	}

	void MapNode::initializeEdge(size_t a_index, const Point<int> &a_offset) const {
		auto& mapRef = *map;
		if (mapRef.inBounds(location + a_offset)) {
			auto& cell = mapRef[location.x + a_offset.x][location.y + a_offset.y];

			edges[a_index] = edgeBlocked(a_offset) ? &cell : nullptr;

			cell.onBlock.connect(guid("Block"), [&, a_index, a_offset](std::shared_ptr<Map>, const Point<int> &) {
				edges[a_index] = edgeBlocked(a_offset) ? &mapRef.get(location + a_offset) : nullptr;
			});
			cell.onUnblock.connect(guid("Unblock"), [&, a_index, a_offset](std::shared_ptr<Map>, const Point<int> &) {
				edges[a_index] = edgeBlocked(a_offset) ? &mapRef.get(location + a_offset) : nullptr;
			});
		} else {
			edges[a_index] = nullptr;
		}
	}

	bool MapNode::edgeBlocked(const Point<int> &a_offset) const {
		auto& mapRef = *map;
		return !mapRef.blocked({ location.x + a_offset.y, location.y }) && !mapRef.blocked({ location.x, location.y + a_offset.y }) && !mapRef.blocked({ location.x + a_offset.x, location.y + a_offset.y });
	}

	void MapNode::lazyInitialize() const {
		if (!initialized) {
			auto& mapRef = *map;
			initialized = true;

			std::vector<Point<int>> edgePoints = {
				{ location.x - 1, location.y },
				{ location.x, location.y - 1 },
				{ location.x + 1, location.y },
				{ location.x, location.y + 1 }
			};

			for (size_t i = 0; i < 4;++i) {
				if (mapRef.inBounds(edgePoints[i])) {
					auto edgePoint = edgePoints[i];
					auto& cell = mapRef.get(edgePoints[i]);
					edges[i] = !mapRef.blocked(edgePoint) ? &cell : nullptr;
					cell.onBlock.connect(guid("Block"), [&, i](std::shared_ptr<Map>, const Point<int> &){
						edges[i] = nullptr;
					});
					cell.onUnblock.connect(guid("Unblock"), [&, i, edgePoint](std::shared_ptr<Map>, const Point<int> &) {
						edges[i] = &mapRef.get(edgePoint);
					});
				}
			}

			if (useCorners) {
				initializeEdge(4, { -1, -1 });
				initializeEdge(5, { 1, 1 });
				initializeEdge(6, { -1, 1 });
				initializeEdge(7, { 1, -1 });
			} else {
				for (int i = 4; i < 8; ++i) {
					edges[i] = nullptr;
				}
			}
		}
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
		onCostChange(onCostChangeSignal) {
	}

	Map::Map(const Size<int> &a_size, float a_defaultCost, bool a_useCorners) :
		onBlock(onBlockSignal),
		onUnblock(onUnblockSignal),
		onStaticBlock(onStaticBlockSignal),
		onStaticUnblock(onStaticUnblockSignal),
		onCostChange(onCostChangeSignal),
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
				square.onBlock.connect("__PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					onBlockSignal(a_self, a_position);
				});
				square.onUnblock.connect("__PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					onUnblockSignal(a_self, a_position);
				});
				square.onStaticBlock.connect("__PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					onStaticBlockSignal(a_self, a_position);
				});
				square.onStaticUnblock.connect("__PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					onStaticUnblockSignal(a_self, a_position);
				});
				square.onCostChange.connect("__PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					onCostChangeSignal(a_self, a_position);
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
				if (mapNode[i] != nullptr && !mapNode[i]->blocked()) {
					insertIntoOpen(mapNode[i]->position(), mapNode[i]->totalCost(), currentNode, i >= 4);
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

}