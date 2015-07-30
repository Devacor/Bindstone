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
		if (!wasBlocked) {
			onBlockSignal(map->shared_from_this(), location);
		}
	}

	void MapNode::staticUnblock() {
		require<ResourceException>(staticBlockedSemaphore > 0, "Error: Static Block Semaphore overextended in MapNode, something is unblocking excessively.");
		--staticBlockedSemaphore;
		if (!blocked()) {
			onUnblockSignal(map->shared_from_this(), location);
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

}