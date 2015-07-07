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
		typedef void CallbackSignature(const std::shared_ptr<Map> &, const Point<int> &);
	private:
		Signal<CallbackSignature> onBlockSignal;
		Signal<CallbackSignature> onUnblockSignal;
		Signal<CallbackSignature> onCostChangeSignal;

	public:
		SignalRegister<CallbackSignature> onBlock;
		SignalRegister<CallbackSignature> onUnblock;
		SignalRegister<CallbackSignature> onCostChange;

		MapNode(Map& a_grid, const Point<int> &a_location, float a_cost, bool a_useCorners);

		float baseCost() const;
		void baseCost(float a_newCost);

		float totalCost() const;

		void block();
		void unblock();
		bool blocked() const;

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
		void lazyInitialize() const;

		bool cornerDetected(const Point<int> a_location);

		Map& map;

		mutable bool initialized = false;
		mutable std::array<MapNode*, 8> edges;

		bool useCorners;
		Point<int> location;
		float travelCost = 1.0f;
		float temporaryCost = 0.0f;
		int blockedSemaphore = 0;
	};

	class NavigationAgent;

	class Map : public std::enable_shared_from_this<Map> {
		friend NavigationAgent;
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
			bool cellInBounds = inBounds(a_location);
			return (cellInBounds && squares[a_location.x][a_location.y].blocked()) || !cellInBounds;
		}

	private:
		Map(const Size<int> &a_size, float a_defaultCost, bool a_useCorners) :
			ourSize(a_size) {

			squares.reserve(a_size.width);
			for (int x = 0; x < a_size.width; ++x) {
				std::vector<MapNode> column;
				column.reserve(a_size.height);
				for (int y = 0; y < a_size.height; ++y) {
					column.push_back({ *this,{ x, y }, a_defaultCost, a_useCorners });
				}
				squares.push_back(column);
			}
		}

		void add(NavigationAgent* a_agent) {
			agents.push_back(a_agent);
		}

		void remove(NavigationAgent* a_agent) {
			agents.erase(std::find(agents.begin(), agents.end(), a_agent));
		}

		std::vector<std::vector<MapNode>> squares;
		Size<int> ourSize;

		std::vector<NavigationAgent*> agents;
	};

	class TemporaryCost {
	public:
		TemporaryCost(const std::shared_ptr<Map> &a_map, const Point<int> &a_position, float a_cost) :
			map(a_map),
			position(a_position),
			temporaryCost(a_cost) {

			map->get(position).addTemporaryCost(temporaryCost);
		}

		~TemporaryCost() {
			map->get(position).removeTemporaryCost(temporaryCost);
		}

	private:
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
		Path(std::shared_ptr<Map> a_map, const Point<int>& a_start, const Point<int>& a_end) :
			map(a_map),
			startPosition(std::min(a_start.x, map->size().width), std::min(a_start.y, map->size().height)),
			endPosition(std::min(a_end.x, map->size().width), std::min(a_end.y, map->size().height)) {
		}

		std::vector<PathNode> path() {
			if (isDirty) {
				calculate();
			}
			return pathNodes;
		}

		Point<int> start() const {
			return startPosition;
		}

		Point<int> end() const {
			return endPosition;
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

			PathCalculationNode* ourParent;

			Path* path;
			MapNode* mapCell;
		};

		void calculate() {
			open.clear();
			closed.clear();

			insertIntoOpen(startPosition, 0.0f);
			bool found = false;
			PathCalculationNode* currentNode = nullptr;
			while (!open.empty()) {
				closed.push_back(open.back());
				currentNode = &closed.back();
				open.pop_back();

				if (currentNode->position() == endPosition) {
					found = true;
					break; //success
				}
				if (!map->blocked(currentNode->position())) {
					auto& mapNode = currentNode->mapNode();
					for (size_t i = 0; i < mapNode.size(); ++i) {
						if (mapNode[i] != nullptr) {
							insertIntoOpen(mapNode[i]->position(), mapNode[i]->totalCost(), currentNode, i >= 4);
						}
					}
				}
			}

			pathNodes.clear();
			while (currentNode) {
				pathNodes.push_back({ currentNode->position(), currentNode->mapNode().baseCost() * (currentNode->corner() ? 1.4f : 1.0f) });
				currentNode = currentNode->parent();
			}
		}

		void insertIntoOpen(const Point<int> &a_position, float a_startCost, PathCalculationNode* a_parent = nullptr, bool a_isCorner = false) {
			if (!existsBetter(a_position, a_startCost)) {
				insertReverseSorted(open, Path::PathCalculationNode(endPosition, map->get(a_position), (a_parent ? a_parent->estimatedTotalCost() : 0.0f) + (a_startCost * (a_isCorner ? 1.4f : 1.0f)), a_parent));
			}
		}

		bool existsBetter(const Point<int> &a_position, float a_startCost) {
			if (std::find_if(closed.cbegin(), closed.cend(), [&](const PathCalculationNode &a_node) {return a_node == a_position && a_node.costToArrive() < a_startCost; }) != closed.cend()) {
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

		bool isDirty = true;

		std::shared_ptr<Map> map;

		Point<int> startPosition;
		Point<int> endPosition;

		std::vector<PathNode> pathNodes;

		std::vector<PathCalculationNode> open;
		std::list<PathCalculationNode> closed;
	};

	class NavigationAgent {
	public:
		NavigationAgent(std::shared_ptr<Map> a_map) :
			map(a_map) {
			map->add(this);
		}

		~NavigationAgent() {
			map->remove(this);
		}

		Point<int> position() const {
			return ourPosition;
		}

		void position(const Point<int> &a_newPosition) {
			ourPosition = a_newPosition;
		}

		void update(double a_dt) {
			ourPath = std::make_shared<Path>(map, ourPosition, ourGoal);
		}

		float speed() const {
			return ourSpeed;
		}

		void speed(float a_newSpeed) {
			ourSpeed = a_newSpeed;
		}
		bool pathfinding() {
			return ourPosition == ourGoal;
		}
	private:
		Point<int> ourPosition;
		Point<int> ourGoal;
		float ourSpeed = 1.0f;
		std::shared_ptr<Map> map;
		std::shared_ptr<Path> ourPath;
	};
}
