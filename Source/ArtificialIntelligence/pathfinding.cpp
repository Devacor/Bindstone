#include "pathfinding.h"

namespace MV {

	MapNode::MapNode(Map& a_grid, const Point<int> &a_location, float a_cost, bool a_useCorners) :
		travelCost(a_cost),
		location(a_location),
		map(a_grid),
		useCorners(a_useCorners),
		onBlock(onBlockSlot),
		onUnblock(onUnblockSlot),
		onCostChange(onCostChangeSlot){
	}

	float MapNode::baseCost() const {
		return travelCost;
	}

	void MapNode::baseCost(float a_newCost) {
		auto oldCost = travelCost;
		travelCost = a_newCost;
		if (oldCost != travelCost) {
			onCostChangeSlot(map.shared_from_this(), location);
		}
	}

	float MapNode::totalCost() const {
		return travelCost + temporaryCost;
	}

	void MapNode::block() {
		blockedSemaphore++;
		if (blockedSemaphore == 1) {
			onBlockSlot(map.shared_from_this(), location);
		}
	}

	void MapNode::unblock() {
		require<ResourceException>(blockedSemaphore > 0, "Error: Semaphore overextended in MapNode, something is unblocking excessively.");
		blockedSemaphore--;
		if (blockedSemaphore == 0) {
			onBlockSlot(map.shared_from_this(), location);
		}
	}

	bool MapNode::blocked() const {
		return blockedSemaphore != 0;
	}

	Map& MapNode::parent() {
		return map;
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
			onCostChangeSlot(map.shared_from_this(), location);
		}
	}

	void MapNode::removeTemporaryCost(float a_newTemporaryCost) {
		temporaryCost -= a_newTemporaryCost;
		if (a_newTemporaryCost != 0.0f) {
			onCostChangeSlot(map.shared_from_this(), location);
		}
	}

	void MapNode::initializeEdge(size_t a_index, const Point<int> &a_offset) const {
		if (!map.blocked({ location.x + a_offset.y, location.y }) && !map.blocked({ location.x, location.y + a_offset.y }) && !map.blocked({ location.x + a_offset.x, location.y + a_offset.y })) {
			edges[a_index] = &map[location.x + a_offset.x][location.y + a_offset.y];
		} else {
			edges[a_index] = nullptr;
		}
	}

	bool MapNode::cornerDetected(const Point<int> a_location) {
		bool cellInBounds = map.inBounds(a_location);
		return (cellInBounds && map[a_location.x - 1][a_location.y].blocked()) || !cellInBounds;
	}

	void MapNode::lazyInitialize() const {
		if (!initialized) {
			initialized = true;

			edges[0] = !map.blocked({ location.x - 1, location.y }) ? &map[location.x - 1][location.y] : nullptr;
			edges[1] = !map.blocked({ location.x, location.y - 1 }) ? &map[location.x][location.y - 1] : nullptr;
			edges[2] = !map.blocked({ location.x + 1, location.y }) ? &map[location.x + 1][location.y] : nullptr;
			edges[3] = !map.blocked({ location.x, location.y + 1 }) ? &map[location.x][location.y + 1] : nullptr;

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