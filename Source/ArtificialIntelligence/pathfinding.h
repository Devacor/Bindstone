#include <iostream>
#include <string>
#include <regex>
#include <vector>
#include <array>
#include <list>
#include <queue>
#include <memory>

#include "Utility/generalUtility.h"
#include "Utility/scopeGuard.hpp"
#include "Utility/signal.hpp"
#include "Render/points.h"

namespace MV {

	class Map;
	class TemporaryCost;
	class MapNode {
		friend TemporaryCost;
	public:
		typedef void CallbackSignature(std::shared_ptr<Map>, const Point<int> &);
		typedef SignalRegister<CallbackSignature>::SharedRecieverType SharedRecieverType;
	private:
		Signal<CallbackSignature> onBlockSignal;
		Signal<CallbackSignature> onUnblockSignal;
		Signal<CallbackSignature> onStaticBlockSignal;
		Signal<CallbackSignature> onStaticUnblockSignal;
		Signal<CallbackSignature> onCostChangeSignal;

	public:
		SignalRegister<CallbackSignature> onBlock;
		SignalRegister<CallbackSignature> onUnblock;
		SignalRegister<CallbackSignature> onStaticBlock;
		SignalRegister<CallbackSignature> onStaticUnblock;
		SignalRegister<CallbackSignature> onCostChange;

		MapNode(Map& a_grid, const Point<int> &a_location, float a_cost, bool a_useCorners);
		MapNode(const MapNode &a_rhs);
		MapNode& operator=(const MapNode &a_rhs) = delete;
		float baseCost() const;
		void baseCost(float a_newCost);

		float totalCost() const;

		void block();
		void unblock();
		bool blocked() const;

		void staticBlock();
		void staticUnblock();
		bool staticallyBlocked() const;

		Map& parent();
		Point<int> position() const;

		size_t size() const;

		std::array<MapNode*, 8>::iterator begin();
		std::array<MapNode*, 8>::const_iterator cbegin() const;

		std::array<MapNode*, 8>::iterator end();
		std::array<MapNode*, 8>::const_iterator cend() const;

		MapNode* operator[](size_t a_size);
		bool operator==(const MapNode &a_rhs) const;

	private:
		void addTemporaryCost(float a_newTemporaryCost);
		void removeTemporaryCost(float a_newTemporaryCost);

		void initializeEdge(size_t a_index, const Point<int> &a_offset) const;

		bool edgeBlocked(const Point<int> &a_offset) const;

		void lazyInitialize() const;

		Map& map;

		mutable bool initialized = false;
		mutable std::array<MapNode*, 8> edges;

		bool useCorners;
		Point<int> location;
		float travelCost = 1.0f;
		float temporaryCost = 0.0f;
		bool staticBlocked = false;
		int blockedSemaphore = 0;
	};

	class Map : public std::enable_shared_from_this<Map> {
	public:
		static std::shared_ptr<Map> make(const Size<int> &a_size, bool a_useCorners = false) {
			return std::shared_ptr<Map>(new Map(a_size, 1.0f, a_useCorners));
		}

		static std::shared_ptr<Map> make(const Size<int> &a_size, float a_defaultCost, bool a_useCorners = false) {
			return std::shared_ptr<Map>(new Map(a_size, a_defaultCost, a_useCorners));
		}

		std::vector<MapNode>& operator[](int a_x) {
			return squares[a_x];
		}

		MapNode& get(const Point<int> &a_location) {
			return squares[a_location.x][a_location.y];
		}

		bool inBounds(Point<int> a_location) const {
			return !squares.empty() && (a_location.x >= 0 && a_location.y >= 0) && (a_location.x < ourSize.width && a_location.y < ourSize.height);
		}

		Size<int> size() const {
			return ourSize;
		}

		bool blocked(Point<int> a_location) const {
			return !inBounds(a_location) || squares[a_location.x][a_location.y].blocked();
		}

		bool staticallyBlocked(Point<int> a_location) const {
			return !inBounds(a_location) || squares[a_location.x][a_location.y].staticallyBlocked();
		}

		bool corners() const {
			return usingCorners;
		}

	private:
		Map(const Size<int> &a_size, float a_defaultCost, bool a_useCorners) :
			usingCorners(a_useCorners),
			ourSize(a_size) {

			squares.reserve(a_size.width);
			for (int x = 0; x < a_size.width; ++x) {
				std::vector<MapNode> column;
				column.reserve(a_size.height);
				for (int y = 0; y < a_size.height; ++y) {
					column.emplace_back( *this, Point<int>( x, y ), a_defaultCost, a_useCorners );
				}
				squares.push_back(column);
			}
		}

		bool usingCorners;

		std::vector<std::vector<MapNode>> squares;
		Size<int> ourSize;
	};

	class TemporaryCost {
	public:
		TemporaryCost(const std::shared_ptr<Map> &a_map, const Point<int> &a_position, float a_cost) :
			map(a_map),
			position(a_position),
			temporaryCost(a_cost) {

			map->get(position).addTemporaryCost(temporaryCost);
		}

		TemporaryCost(TemporaryCost &&a_rhs):
			temporaryCost(std::move(a_rhs.temporaryCost)),
			position(std::move(a_rhs.position)),
			map(std::move(a_rhs.map)){
			
			a_rhs.temporaryCost = 0;
		}

		~TemporaryCost() {
			if (temporaryCost != 0) {
				map->get(position).removeTemporaryCost(temporaryCost);
			}
		}

	private:
		TemporaryCost(const TemporaryCost &) = delete;
		std::shared_ptr<Map> map;
		Point<int> position;
		float temporaryCost;
	};

	class PathNode {
	public:
		PathNode(const Point<int> &a_location, float a_cost) :
			ourPosition(a_location),
			ourCost(a_cost) {
		}

		Point<int> position() const {
			return ourPosition;
		}

		float cost() const {
			return ourCost;
		}
	private:
		Point<int> ourPosition;
		float ourCost;
	};

	class Path {
	public:
		Path(std::shared_ptr<Map> a_map, const Point<int>& a_start, const Point<int>& a_end, PointPrecision a_distance = 0.0f, int64_t a_maxSearchNodes = -1) :
			map(a_map),
			startPosition(std::min(a_start.x, map->size().width), std::min(a_start.y, map->size().height)),
			goalPosition(std::min(a_end.x, map->size().width), std::min(a_end.y, map->size().height)),
			minimumDistance(a_distance),
			maxSearchNodes(a_maxSearchNodes) {
		}

		std::vector<PathNode> path() {
			calculate();
			return pathNodes;
		}

		Point<int> start() const {
			return startPosition;
		}

		//Desired Target
		Point<int> goal() const {
			return goalPosition;
		}

		//End of Path
		Point<int> end() const {
			return endPosition;
		}

		bool complete() const {
			return found;
		}
	private:
		class PathCalculationNode {
		public:
			PathCalculationNode(const Point<int> &a_pathGoal, MapNode& a_mapCell, float a_startCost, Path::PathCalculationNode* a_parent = nullptr, bool a_isCorner = false) :
				isCorner(a_isCorner),
				initialCost(a_startCost),
				mapCell(&a_mapCell),
				ourParent(a_parent) {

				goalCost = static_cast<float>(std::abs(mapCell->position().x - a_pathGoal.x) + std::abs(mapCell->position().y - a_pathGoal.y));
			}

			PathCalculationNode* parent() const {
				return ourParent;
			}

			void parent(PathCalculationNode* a_parent) {
				ourParent = a_parent;
			}

			float estimatedTotalCost() const {
				return goalCost + initialCost;
			}

			float costToArrive() const {
				return initialCost;
			}

			Point<int> position() const {
				return mapCell->position();
			}

			bool operator==(const Point<int> &a_position) const {
				return mapCell->position() == a_position;
			}

			bool operator==(const PathCalculationNode &a_rhs) const {
				return mapCell->position() == a_rhs.mapCell->position();
			}

			bool operator<(const PathCalculationNode &a_rhs) const {
				return estimatedTotalCost() < a_rhs.estimatedTotalCost();
			}

			MapNode& mapNode() {
				require<PointerException>(mapCell, "PathCalculationNode::mapNode has an empty pointer.");
				return *mapCell;
			}

			bool corner() const {
				return isCorner;
			}

		private:
			float goalCost;
			float initialCost;
			bool isCorner;

			PathCalculationNode* ourParent = nullptr;

			Path* path = nullptr;
			MapNode* mapCell = nullptr;
		};

		void calculate() {
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

		void insertIntoOpen(const Point<int> &a_position, float a_startCost, PathCalculationNode* a_parent = nullptr, bool a_isCorner = false) {
			if (!existsBetter(a_position, a_startCost)) {
				insertReverseSorted(open, Path::PathCalculationNode(goalPosition, map->get(a_position), (a_parent ? a_parent->estimatedTotalCost() : 0.0f) + (a_startCost * (a_isCorner ? 1.4f : 1.0f)), a_parent));
			}
		}

		bool existsBetter(const Point<int> &a_position, float a_startCost) {
			if (std::find_if(closed.cbegin(), closed.cend(), [&](const PathCalculationNode &a_node) {return a_node == a_position; }) != closed.cend()) {
				return true;
			}
			auto foundOpen = std::find_if(open.cbegin(), open.cend(), [&](const PathCalculationNode &a_node) {return a_node == a_position; });
			if (foundOpen != open.cend()) {
				if (foundOpen->costToArrive() > a_startCost) {
					open.erase(foundOpen);
					return false;
				} else {
					return true;
				}
			}
			return false;
		}

		bool found = false;

		int64_t maxSearchNodes = -1;

		PointPrecision minimumDistance = 0;

		std::shared_ptr<Map> map;

		Point<int> startPosition;
		Point<int> goalPosition;
		Point<int> endPosition;

		std::vector<PathNode> pathNodes;

		std::vector<PathCalculationNode> open;
		std::list<PathCalculationNode> closed;
	};

	class NavigationAgent {
	public:
		static std::shared_ptr<NavigationAgent> make(std::shared_ptr<Map> a_map, const Point<int> &a_newPosition = Point<int>()){
			return std::shared_ptr<NavigationAgent>(new NavigationAgent(a_map, a_newPosition));
		}

		~NavigationAgent() {
			map->get(cast<int>(ourPosition)).unblock();
		}

		Point<PointPrecision> position() const {
			return ourPosition;
		}

		std::vector<PathNode> path() {
			if (dirtyPath) {
				recalculate();
			}
			return calculatedPath;
		}

		void position(const Point<int> &a_newPosition) {
			map->get(cast<int>(ourPosition)).unblock();
			ourPosition = cast<PointPrecision>(a_newPosition) + point(.5f, .5f);
			map->get(cast<int>(ourPosition)).block();
			markDirty();
		}

		void goal(const Point<int> &a_newGoal, PointPrecision a_acceptableDistance = 0.0f) {
			ourGoal = a_newGoal;
			acceptableDistance = std::max(a_acceptableDistance, 0.0f);
			markDirty();
		}

		Point<int> goal() const {
			return ourGoal;
		}

		void update(double a_dt) {
			if (pathfinding())
			{
				if (dirtyPath || calculatedPath.empty()) {
					recalculate();
				}
				if (!calculatedPath.empty() && currentPathIndex < calculatedPath.size()){
					auto direction = (ourPosition - cast<PointPrecision>(calculatedPath[currentPathIndex].position())).normalized();
					
					activeUpdate = true;
					SCOPE_EXIT{ activeUpdate = false; };

					PointPrecision totalDistanceToTravel = static_cast<PointPrecision>(a_dt) * ourSpeed;
					bool movedThisFrame = false;
					while (pathfinding() && totalDistanceToTravel > 0.0f && currentPathIndex < calculatedPath.size()) {
						auto previousGridSquare = cast<int>(ourPosition);
						auto desiredPosition = cast<PointPrecision>(calculatedPath[currentPathIndex].position()) + point(.5f, .5f);
						PointPrecision distanceToNextNode = static_cast<PointPrecision>(distance(ourPosition, desiredPosition));
						PointPrecision maxDistance = std::min(totalDistanceToTravel, distanceToNextNode);
						map->get(cast<int>(ourPosition)).unblock();
						ourPosition += (desiredPosition - ourPosition).normalized() * maxDistance;
						ourPosition.z = 0;
						map->get(cast<int>(ourPosition)).block();
						if (cast<int>(ourPosition) != previousGridSquare || equals(maxDistance, 0.0f)) {
							++currentPathIndex;
							movedThisFrame = true;
						}
						totalDistanceToTravel -= maxDistance;
					}
					if (movedThisFrame) {
						updateObservedNodes();
					}
				}
			}
		}

		PointPrecision speed() const {
			return ourSpeed;
		}

		void speed(PointPrecision a_newSpeed) {
			ourSpeed = a_newSpeed;
			updateObservedNodes();
		}
		bool pathfinding() {
			auto goalDistance = static_cast<PointPrecision>(distance(ourPosition, cast<PointPrecision>(ourGoal) + point(.5f, .5f)));
			return goalDistance > acceptableDistance && !equals(goalDistance, acceptableDistance);
		}
	private:
		NavigationAgent(std::shared_ptr<Map> a_map, const Point<int> &a_newPosition = Point<int>()) :
			map(a_map),
			ourPosition(cast<PointPrecision>(a_newPosition)),
			ourGoal(a_newPosition) {
			map->get(cast<int>(ourPosition)).block();
		}
		NavigationAgent(const NavigationAgent &) = delete;
		NavigationAgent& operator=(const NavigationAgent&a_rhs) = delete;

		void markDirty() {
			costs.clear();
			recievers.clear();
			dirtyPath = true;
		}

		void updateObservedNodes() {
			recievers.clear();
			costs.clear();
			for (int i = 0; i < calculatedPath.size() && i < (ourSpeed*4.0f); ++i) {
				costs.push_back(TemporaryCost(map, calculatedPath[i].position(), (ourSpeed*4.0f) - static_cast<PointPrecision>(i)));
			}
			for (auto&& node : calculatedPath) {
				auto& mapNode = map->get(node.position());
				auto reciever = mapNode.onBlock.connect([&](const std::shared_ptr<Map> &, const Point<int> &a_position) {
					if (a_position != cast<int>(ourPosition) && !activeUpdate) {
						dirtyPath = true;
					}
				});
				recievers.push_back(reciever);
			}
		}

		void recalculate() {
			recievers.clear();
			costs.clear();
			currentPathIndex = 0;
			ourPath = std::make_shared<Path>(map, cast<int>(ourPosition), ourGoal, acceptableDistance, maxNodesToSearch);
			calculatedPath = ourPath->path();
			updateObservedNodes();
		}

		bool dirtyPath = true;
		Point<PointPrecision> ourPosition;
		Point<int> ourGoal;
		const int64_t maxNodesToSearch = 100;
		PointPrecision ourSpeed = 1.0f;
		PointPrecision remainder = 0.0f;
		size_t currentPathIndex = 0;
		std::shared_ptr<Map> map;
		std::shared_ptr<Path> ourPath;
		std::vector<PathNode> calculatedPath;
		PointPrecision acceptableDistance = 0.0f;
		std::vector<TemporaryCost> costs;
		std::vector<MapNode::SharedRecieverType> recievers;
		bool activeUpdate = false;
	};
}
