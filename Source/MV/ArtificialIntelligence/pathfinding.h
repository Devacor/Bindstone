#ifndef _MV_PATHFINDING_H_
#define _MV_PATHFINDING_H_

#include <iostream>
#include <string>
#include <regex>
#include <vector>
#include <array>
#include <list>
#include <queue>
#include <memory>
#include <chrono>

#include "MV/Utility/generalUtility.h"
#include "MV/Utility/scopeGuard.hpp"
#include <jaiscript/signals/signal_decl.hpp>
#include "MV/Render/points.h"
#include <jaiscript/serialization/construct.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {

	class Map;
	class TemporaryCost;
	class MapNode {
		friend TemporaryCost;
		friend Map;  // For sparse serialization access to travelCost/staticBlockedSemaphore
		friend class jai::access;  // JaiScript load_and_construct support
		friend class jai::serialization::access;  // JaiScript save/load support
	public:
		typedef void CallbackSignature(const std::shared_ptr<Map> &, const Point<int> &);
		typedef jai::signal<CallbackSignature>::shared_receiver_type SharedReceiverType;
	private:
		jai::signal_emitter<CallbackSignature> onBlockSignal;
		jai::signal_emitter<CallbackSignature> onUnblockSignal;
		jai::signal_emitter<CallbackSignature> onStaticBlockSignal;
		jai::signal_emitter<CallbackSignature> onStaticUnblockSignal;
		jai::signal_emitter<CallbackSignature> onCostChangeSignal;
		mutable jai::signal_emitter<CallbackSignature> onClearanceChangeSignal;

	public:
		jai::signal<CallbackSignature> onBlock;
		jai::signal<CallbackSignature> onUnblock;
		jai::signal<CallbackSignature> onStaticBlock;
		jai::signal<CallbackSignature> onStaticUnblock;
		jai::signal<CallbackSignature> onCostChange;
		jai::signal<CallbackSignature> onClearanceChange;

		MapNode(Map& a_grid, const Point<int> &a_location, float a_cost, bool a_useCorners);
		MapNode(const MapNode &a_rhs);
		MapNode& operator=(const MapNode &a_rhs);

		MapNode();
		float baseCost() const;
		void baseCost(float a_newCost);

		inline float totalCost() const {
			return travelCost + temporaryCost;
		}

		inline void block();
		inline void unblock();
		inline bool blocked() const {
			return staticBlockedSemaphore != 0 || blockedSemaphore != 0;
		}

		inline bool clearedForSize(int a_unitSize) const {
			return !blocked() && (a_unitSize <= 1 || clearance() >= a_unitSize);
		}

		void staticBlock();
		void staticUnblock();
		bool staticallyBlocked() const;

		inline int clearance() const {
			lazyInitialize();
			return clearanceAmount;
		}

		Map& parent();
		Point<int> position() const;

		size_t size() const;

		std::array<MapNode*, 8>::iterator begin();
		std::array<MapNode*, 8>::const_iterator cbegin() const;

		std::array<MapNode*, 8>::iterator end();
		std::array<MapNode*, 8>::const_iterator cend() const;

		MapNode* operator[](size_t a_size);
		bool operator==(const MapNode &a_rhs) const;

		static const int MAXIMUM_CLEARANCE = 8;
	private:

		// Versioned save/load for JaiScript only
		// Defined out-of-line after Map class because it needs Map to be complete
		template<class Archive>
			requires jai::serialization::jai_archive<Archive>
		void save(Archive& archive, std::uint32_t const version) const;
		template<class Archive>
			requires jai::serialization::jai_archive<Archive>
		void load(Archive& archive, std::uint32_t const version);

		void calculateClearance() const;

		void registerCalculateClearanceCallbacks() const;

		void performClearanceIncrement() const;

		bool offsetBlocked(int x, int y) const;

		void addTemporaryCost(float a_newTemporaryCost);
		void removeTemporaryCost(float a_newTemporaryCost);

		void initializeEdge(size_t a_index, const Point<int> &a_offset) const;

		bool edgeBlocked(const Point<int> &a_offset) const;

		void lazyInitialize() const;

		Map* map;

		mutable bool initialized = false;
		mutable std::array<MapNode*, 8> edges;
		mutable int clearanceAmount = 0;

		mutable std::vector<MapNode::SharedReceiverType> clearanceReceivers;

		bool useCorners;
		Point<int> location;
		float travelCost = 1.0f;
		float temporaryCost = 0.0f;
		int staticBlockedSemaphore = 0;
		int blockedSemaphore = 0;
	};

	class Map : public std::enable_shared_from_this<Map> {
		friend class jai::access;  // JaiScript load_and_construct support
		friend class jai::serialization::access;  // JaiScript save/load support
	public:
		// JaiScript load_and_construct - must be public for trait detection
		template<typename Archive>
		static void load_and_construct(Archive& ar,
		                               jai::serialization::construct<Map>& c)
			requires jai::serialization::jai_archive<Archive> {
			c();  // Default construct via jai::access friend
			ar("usingCorners", c->usingCorners);
			ar("squares", c->squares);  // Generic vector<vector<MapNode>> deserialization
			// Set map pointer for all nodes (not serialized)
			for (auto& column : c->squares) {
				for (auto& node : column) {
					node.map = c.operator->();
				}
			}
			c->hookUpObservation();
		}

		typedef void CallbackSignature(std::shared_ptr<Map>, const Point<int> &);
		typedef jai::signal<CallbackSignature>::shared_receiver_type SharedReceiverType;
	private:
		jai::signal_emitter<CallbackSignature> onBlockSignal;
		jai::signal_emitter<CallbackSignature> onUnblockSignal;
		jai::signal_emitter<CallbackSignature> onStaticBlockSignal;
		jai::signal_emitter<CallbackSignature> onStaticUnblockSignal;
		jai::signal_emitter<CallbackSignature> onCostChangeSignal;
		jai::signal_emitter<CallbackSignature> onClearanceChangeSignal;
	public:
		jai::signal<CallbackSignature> onBlock;
		jai::signal<CallbackSignature> onUnblock;
		jai::signal<CallbackSignature> onStaticBlock;
		jai::signal<CallbackSignature> onStaticUnblock;
		jai::signal<CallbackSignature> onCostChange;
		jai::signal<CallbackSignature> onClearanceChange;

		static std::shared_ptr<Map> make(const Size<int> &a_size, bool a_useCorners = false) {
			return std::shared_ptr<Map>(new Map(a_size, 1.0f, a_useCorners));
		}

		static std::shared_ptr<Map> make(const Size<int> &a_size, float a_defaultCost, bool a_useCorners = false) {
			return std::shared_ptr<Map>(new Map(a_size, a_defaultCost, a_useCorners));
		}

		void resize(const Size<int> &a_size, float a_defaultCost = 1.0f);

		std::shared_ptr<Map> clone() const;

		inline std::vector<MapNode>& operator[](int a_x) {
			return squares[a_x];
		}

		inline MapNode& get(const Point<int> &a_location) {
			require<ResourceException>(inBounds(a_location), "Failed to retrieve grid location from map: ", a_location);
			return squares[a_location.x][a_location.y];
		}

		inline bool inBounds(Point<int> a_location) const {
			auto ourSize = size();
			return !squares.empty() && (a_location.x >= 0 && a_location.y >= 0) && (a_location.x < ourSize.width && a_location.y < ourSize.height);
		}

		inline Size<int> size() const {
			return Size<int>(static_cast<int>(squares.size()), static_cast<int>((squares.empty() ? 0 : squares[0].size())));
		}

		inline bool blocked(Point<int> a_location) const {
			return !inBounds(a_location) || squares[a_location.x][a_location.y].blocked();
		}

		inline bool staticallyBlocked(Point<int> a_location) const {
			return !inBounds(a_location) || squares[a_location.x][a_location.y].staticallyBlocked();
		}

		inline bool clearedForSize(Point<int> a_location, int a_unitSize) {
			return inBounds(a_location) && squares[a_location.x][a_location.y].clearedForSize(a_unitSize);
		}

		inline bool corners() const {
			return usingCorners;
		}

		// Pathfinding budget management - amortizes pathfinding costs across frames
		static constexpr int64_t FRAME_NODE_BUDGET = 1000;  // Max A* nodes to search per frame
		static constexpr double BUDGET_RESET_INTERVAL = 1.0 / 60.0;  // ~16.67ms

		void tickBudget() const {
			auto now = std::chrono::steady_clock::now();
			double elapsed = std::chrono::duration<double>(now - lastBudgetReset).count();
			if (elapsed >= BUDGET_RESET_INTERVAL) {
				nodesUsedThisFrame = 0;
				lastBudgetReset = now;
			}
		}

		bool hasBudget() const {
			return nodesUsedThisFrame < FRAME_NODE_BUDGET;
		}

		int64_t remainingBudget() const {
			return std::max(int64_t(0), FRAME_NODE_BUDGET - nodesUsedThisFrame);
		}

		void consumeBudget(int64_t nodes) const {
			nodesUsedThisFrame += nodes;
		}

	private:
		Map();
		Map(const Size<int> &a_size, float a_defaultCost, bool a_useCorners);
		Map(const Map &) = delete;
		Map operator=(const Map&) = delete;

		void hookUpObservation();

		// Versioned save/load for JaiScript only
		template<class Archive>
			requires jai::serialization::jai_archive<Archive>
		void save(Archive & archive, std::uint32_t const /*version*/) const {
			archive("usingCorners", usingCorners);
			archive("squares", squares);
		}

		template<class Archive>
			requires jai::serialization::jai_archive<Archive>
		void load(Archive & archive, std::uint32_t const /*version*/) {
			archive("usingCorners", usingCorners);
			archive("squares", squares);
			// Note: map pointers for nodes are set in load_and_construct
			hookUpObservation();
		}

		bool usingCorners;

		std::vector<std::vector<MapNode>> squares;

		// Mutable for budget tracking (allows const methods to update frame state)
		mutable std::chrono::steady_clock::time_point lastBudgetReset = std::chrono::steady_clock::now();
		mutable int64_t nodesUsedThisFrame = 0;
	};

	class TemporaryCost {
	public:
		TemporaryCost(const std::shared_ptr<Map> &a_map, const Point<int> &a_position, float a_cost) :
			map(a_map),
			position(a_position),
			temporaryCost(a_cost) {
			if (temporaryCost > 0) {
				a_map->get(position).addTemporaryCost(temporaryCost);
			}
		}

		TemporaryCost(TemporaryCost &&a_rhs) noexcept:
			map(std::move(a_rhs.map)),
			position(std::move(a_rhs.position)),
			temporaryCost(std::move(a_rhs.temporaryCost)){

			a_rhs.map.reset();
			a_rhs.temporaryCost = 0;
		}

		~TemporaryCost() {
			if (temporaryCost > 0) {
				if (auto a_map = map.lock()) {
					a_map->get(position).removeTemporaryCost(temporaryCost);
				}
			}
		}

	private:
		TemporaryCost(const TemporaryCost &) = delete;
		TemporaryCost operator=(const TemporaryCost &) = delete;
		TemporaryCost operator=(TemporaryCost &&) = delete;

		std::weak_ptr<Map> map;
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
		Path(std::shared_ptr<Map> a_map, const Point<int>& a_start, const Point<int>& a_end, PointPrecision a_distance = 0.0f, int a_unitSize = 1, int64_t a_maxSearchNodes = -1) :
			maxSearchNodes(a_maxSearchNodes),
			minimumDistance(a_distance),
			unitSize(a_unitSize),
			map(a_map),
			startPosition(std::min(a_start.x, map->size().width), std::min(a_start.y, map->size().height)),
			goalPosition(std::min(a_end.x, map->size().width), std::min(a_end.y, map->size().height)) {
		}

		std::vector<PathNode> path() {
			calculate();
			return pathNodes;
		}

		const Point<int>& start() const {
			return startPosition;
		}

		//Desired Target
		const Point<int>& goal() const {
			return goalPosition;
		}

		//End of Path
		const Point<int>& end() const {
			return endPosition;
		}

		bool complete() const {
			return found;
		}
	private:
		class PathCalculationNode {
		public:
			PathCalculationNode(const Point<int> &a_pathGoal, MapNode& a_mapCell, float a_startCost, Path::PathCalculationNode* a_parent = nullptr, bool a_isCorner = false) :
				initialCost(a_startCost),
				isCorner(a_isCorner),
				ourParent(a_parent),
				mapCell(&a_mapCell) {

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

		void calculate();

		void insertIntoOpen(const Point<int> &a_position, float a_startCost, PathCalculationNode* a_parent = nullptr, bool a_isCorner = false);

		bool existsBetter(const Point<int> &a_position, float a_startCost);

		bool overlaps(Point<int> a_topLeft, int a_size, Point<int> a_position) const {
			return (a_position.x >= a_topLeft.x) && (a_position.x < (a_topLeft.x + a_size)) &&
				(a_position.y >= a_topLeft.y) && (a_position.y < (a_topLeft.y + a_size));
		}

		bool found = false;

		int64_t maxSearchNodes = -1;

		PointPrecision minimumDistance = 0;

		int unitSize = 1;

		std::shared_ptr<Map> map;

		Point<int> startPosition;
		Point<int> goalPosition;
		Point<int> endPosition;

		std::vector<PathNode> pathNodes;

		std::vector<PathCalculationNode> open;
		std::list<PathCalculationNode> closed;
	};

	class NavigationAgent : public std::enable_shared_from_this<NavigationAgent> {
		friend class jai::serialization::access;
	public:
		typedef void CallbackSignature(std::shared_ptr<NavigationAgent>);
		typedef jai::signal<CallbackSignature>::shared_receiver_type SharedReceiverType;
	private:
		jai::signal_emitter<CallbackSignature> onArriveSignal;
		jai::signal_emitter<CallbackSignature> onBlockedSignal;
		jai::signal_emitter<CallbackSignature> onStopSignal;
		jai::signal_emitter<CallbackSignature> onStartSignal;

	public:
		jai::signal<CallbackSignature> onArrive;
		jai::signal<CallbackSignature> onBlocked;
		jai::signal<CallbackSignature> onStop;
		jai::signal<CallbackSignature> onStart;

		static std::shared_ptr<NavigationAgent> make(const std::shared_ptr<Map> &a_map, const Point<int> &a_newPosition = Point<int>(), int a_unitSize = 1, bool a_offsetCenterByHalf = true){
			return std::shared_ptr<NavigationAgent>(new NavigationAgent(a_map, a_newPosition, a_unitSize, a_offsetCenterByHalf));
		}

		static std::shared_ptr<NavigationAgent> make(const std::shared_ptr<Map> &a_map, const Point<> &a_newPosition, int a_unitSize = 1, bool a_offsetCenterByHalf = true) {
			return std::shared_ptr<NavigationAgent>(new NavigationAgent(a_map, a_newPosition, a_unitSize, a_offsetCenterByHalf));
		}

		int debugId() { return ourDebugId; }
		void debugId(int a_id) { ourDebugId = a_id; }

		~NavigationAgent() {
			unblockMap();
		}

		std::shared_ptr<NavigationAgent> clone(const std::shared_ptr<Map> &a_map = nullptr) const {
			auto result = NavigationAgent::make(a_map == nullptr ? map : a_map, ourPosition);
			result->ourGoal = ourGoal;
			result->ourSpeed = ourSpeed;
			result->acceptableDistance = acceptableDistance;
			result->unitSize = unitSize;
			return result;
		}

		std::shared_ptr<NavigationAgent> disableFootprint() {
			if (!footprintDisabled) {
				unblockMap();
				footprintDisabled = true;
			}
			return shared_from_this();
		}

		std::shared_ptr<NavigationAgent> enableFootprint() {
			if (footprintDisabled) {
				footprintDisabled = false;
				blockMap();
				markDirty();
			}
			return shared_from_this();
		}

		bool hasFootprint() const {
			return !footprintDisabled;
		}

		Point<PointPrecision> position() const {
			return ourPosition - centerOffset;
		}

		std::vector<PathNode> path(bool a_allowRecalculate = true) {
			if (dirtyPath && a_allowRecalculate) {
				recalculate();
			}
			return calculatedPath;
		}

		std::shared_ptr<NavigationAgent> stop() {
			auto self = shared_from_this();
			bool wasMoving = pathfinding();
			ourGoal = ourPosition;
			if (wasMoving) {
				onStopSignal(shared_from_this());
			}
			return self;
		}

		std::shared_ptr<NavigationAgent> position(const Point<int> &a_newPosition) {
			return position(cast<PointPrecision>(a_newPosition));
		}

		std::shared_ptr<NavigationAgent> position(const Point<> &a_newPosition) {
			auto self = shared_from_this();
			bool wasMoving = pathfinding();
			unblockMap();
			ourPosition = a_newPosition + centerOffset;
			ourGoal = a_newPosition + centerOffset;
			blockMap();
			markDirty();
			if (wasMoving) {
				onArriveSignal(shared_from_this());
			}
			return self;
		}

		std::shared_ptr<NavigationAgent> goal(const Point<int> &a_newGoal) {
			return goal(a_newGoal, 0.0f);
		}

		std::shared_ptr<NavigationAgent> goal(const Point<int> &a_newGoal, PointPrecision a_acceptableDistance) {
			return goal(cast<PointPrecision>(a_newGoal), a_acceptableDistance);
		}

		std::shared_ptr<NavigationAgent> goal(const Point<> &a_newGoal) {
			return goal(a_newGoal, 0.0f);
		}

		std::shared_ptr<NavigationAgent> goal(const Point<> &a_newGoal, PointPrecision a_acceptableDistance) {
			auto self = shared_from_this();
			auto potentialNewGoal = a_newGoal + centerOffset;
			bool wasMoving = pathfinding();
			if (!wasMoving || ourGoal != potentialNewGoal) {
				ourGoal = potentialNewGoal;
				acceptableDistance = std::max(a_acceptableDistance, 0.0f);
				markDirty();
				if (!pathfinding()) {
					onArriveSignal(shared_from_this());
				} else if (!wasMoving && pathfinding()) {
					onStartSignal(shared_from_this());
				}
			}
			return self;
		}

		Point<PointPrecision> goal() const {
			return ourGoal;
		}

		void update(double a_dt);

		int size() const {
			return unitSize;
		}

		bool overlaps(Point<int> a_position) const {
			auto topLeft = cast<int>(position());
			return (a_position.x >= topLeft.x) && (a_position.x < (topLeft.x + size())) &&
				(a_position.y >= topLeft.y) && (a_position.y < (topLeft.y + size()));
		}

		PointPrecision speed() const {
			return ourSpeed;
		}

		std::shared_ptr<NavigationAgent> speed(PointPrecision a_newSpeed) {
			auto self = shared_from_this();
			ourSpeed = a_newSpeed;
			updateObservedNodes();
			return self;
		}
		bool pathfinding() const {
			auto goalDistance = static_cast<PointPrecision>(distance(cast<int>(ourPosition), cast<int>(ourGoal)));
			return goalDistance > acceptableDistance;
		}
		NavigationAgent() :
			onArrive(onArriveSignal),
			onBlocked(onBlockedSignal),
			onStop(onStopSignal),
			onStart(onStartSignal){}
	private:
		bool attemptToRecalculate();
		void incrementPathIndex();

		NavigationAgent(std::shared_ptr<Map> a_map, const Point<int> &a_newPosition, int a_unitSize, bool a_offsetCenterByHalf) :
			NavigationAgent(a_map, cast<PointPrecision>(a_newPosition), a_unitSize, a_offsetCenterByHalf){
		}
		NavigationAgent(std::shared_ptr<Map> a_map, const Point<> &a_newPosition, int a_unitSize, bool a_offsetCenterByHalf) :
			onArrive(onArriveSignal),
			onBlocked(onBlockedSignal),
			onStop(onStopSignal),
			onStart(onStartSignal),
			map(a_map),
			centerOffset(a_offsetCenterByHalf ? point(.5f, .5f) : point(.0f, .0f)),
			ourPosition(a_newPosition + centerOffset),
			ourGoal(a_newPosition + centerOffset),
			unitSize(a_unitSize) {
			if (canPlaceOnMapAtCurrentPosition()) {
				blockMap();
				waitingForPlacement = false;
			} else {
				waitingForPlacement = true;
			}
		}
		NavigationAgent(const NavigationAgent &) = delete;
		NavigationAgent& operator=(const NavigationAgent&a_rhs) = delete;

		Point<PointPrecision> desiredPositionFromCalculatedPathIndex(size_t index) {
			if (cast<int>(calculatedPath[index].position()) == cast<int>(ourGoal)) {
				return ourGoal;
			} else {
				return cast<PointPrecision>(calculatedPath[index].position()) + point(.5f, .5f);
			}
		}

		// Versioned save/load for JaiScript only
		template<class Archive>
			requires jai::serialization::jai_archive<Archive>
		void save(Archive & archive, std::uint32_t const /*version*/) const {
			archive(
				JAI_NVP(ourPosition),
				JAI_NVP(ourGoal),
				JAI_NVP(ourSpeed),
				JAI_NVP(acceptableDistance),
				JAI_NVP(unitSize),
				JAI_NVP(map),
				JAI_NVP(footprintDisabled),
				JAI_NVP(waitingForPlacement)
			);
		}

		template<class Archive>
			requires jai::serialization::jai_archive<Archive>
		void load(Archive & archive, std::uint32_t const /*version*/) {
			blockMap();
			archive(
				JAI_NVP(ourPosition),
				JAI_NVP(ourGoal),
				JAI_NVP(ourSpeed),
				JAI_NVP(acceptableDistance),
				JAI_NVP(unitSize),
				JAI_NVP(map),
				JAI_NVP(footprintDisabled),
				JAI_NVP(waitingForPlacement)
			);
			unblockMap();
		}


		void markDirty() {
			costs.clear();
			receivers.clear();
			dirtyPath = true;
		}

		void removeBlockedPathObservers(unsigned int a_pathId) {
			blockedNodeObservers.erase(std::remove_if(blockedNodeObservers.begin(), blockedNodeObservers.end(), [&](auto&& blockedObserver) {
				return blockedObserver.pathId == a_pathId;
			}), blockedNodeObservers.end());
		}

		void updateObservedNodes();

		void recalculate(int64_t searchLimit = DEFAULT_SEARCH_LIMIT) {
			receivers.clear();
			costs.clear();

			unblockMap();
			ourPath = std::make_shared<Path>(map, cast<int>(ourPosition), cast<int>(ourGoal), acceptableDistance, unitSize, searchLimit);
			calculatedPath = ourPath->path();
			blockMap();

			currentPathIndex = !calculatedPath.empty() && cast<int>(ourPosition) == calculatedPath[0].position() ? 1 : 0;
			updateObservedNodes();
			dirtyPath = false;
			blockedAtPathIndex = -1;  // Clear blockage tracking after recalculation
			blockedSignalFired = false;
		}

		bool canPlaceOnMapAtCurrentPosition() {
			auto topLeft = cast<int>(position());
			return map->clearedForSize(topLeft, unitSize);
		}

		void blockMap(){
			if (!footprintDisabled) {
				if (isBlocking) {
					return;
				}
				if (!canPlaceOnMapAtCurrentPosition()) {
					waitingForPlacement = true;
					return;
				}
				isBlocking = true;
				++activeUpdate;
				SCOPE_EXIT{ --activeUpdate; };
				auto topLeft = cast<int>(position());
				for (int x = topLeft.x; x < topLeft.x + size() && x < map->size().width; ++x) {
					for (int y = topLeft.y; y < topLeft.y + size() && y < map->size().height; ++y) {
						(*map)[x][y].block();
					}
				}
			}
		}

		void unblockMap(bool a_notify = true) {
			if (!footprintDisabled) {
				if (!isBlocking) {
					return;
				}
				isBlocking = false;
				++activeUpdate;
				SCOPE_EXIT{ --activeUpdate; };
				auto topLeft = cast<int>(position());
				for (int x = topLeft.x; x < topLeft.x + size() && x < map->size().width; ++x) {
					for (int y = topLeft.y; y < topLeft.y + size() && y < map->size().height; ++y) {
						(*map)[x][y].unblock();
					}
				}
			}
		}

		struct RecentlyBlockedNodeObserver {
			RecentlyBlockedNodeObserver(NavigationAgent* a_self, const std::shared_ptr<Map> &a_map, const Point<int> &a_position, unsigned int a_pathId):
				pathId(a_pathId){
				if (a_self->size() > 1) {
					receiver = a_map->get(a_position).onClearanceChange.connect([this, a_self](const std::shared_ptr<Map> &a_innerMap, const Point<int> &a_innerPosition) {
						if (!a_self->activeUpdate && !a_self->overlaps(a_innerPosition) && (a_self->size() >= a_innerMap->get(a_innerPosition).clearance())) {
							a_self->activeUpdate++;
							SCOPE_EXIT{ a_self->activeUpdate--; };
							a_self->markDirty();
							a_self->removeBlockedPathObservers(pathId);
						}
					});
				} else {
					receiver = a_map->get(a_position).onUnblock.connect([this, a_self](const std::shared_ptr<Map> &a_innerMap, const Point<int> &a_innerPosition) {
						if (!a_self->activeUpdate && !a_self->overlaps(a_innerPosition)) {
							a_self->activeUpdate++;
							SCOPE_EXIT{ a_self->activeUpdate--; };
							a_self->markDirty();
							a_self->removeBlockedPathObservers(pathId);
						}
					});
				}
			}

			MapNode::SharedReceiverType receiver;
			int gridMovesLeftUntilExpires = 5;
			unsigned int pathId;
		};

		std::shared_ptr<Map> map;
		Point<PointPrecision> centerOffset;
		Point<PointPrecision> ourPosition;
		Point<PointPrecision> ourGoal;
		PointPrecision ourSpeed = 1.0f;
		PointPrecision acceptableDistance = 0.0f;

		bool dirtyPath = true;

		// Deferred recalculation with urgency-based prioritization
		static constexpr int RECALC_URGENCY_THRESHOLD = 3;  // Recalc when blockage within N nodes
		static constexpr int64_t DEFAULT_SEARCH_LIMIT = 200;

		int blockedAtPathIndex = -1;  // Earliest known blockage in path (-1 = none)
		bool blockedSignalFired = false;  // Prevent repeated blocked signals

		void markPathBlockedAt(int pathIndex) {
			if (blockedAtPathIndex < 0 || pathIndex < blockedAtPathIndex) {
				blockedAtPathIndex = pathIndex;
			}
		}

		int urgency() const {
			if (blockedAtPathIndex < 0) return INT_MAX;
			return blockedAtPathIndex - static_cast<int>(currentPathIndex);
		}

		bool needsUrgentRecalculation() const {
			return blockedAtPathIndex >= 0 && urgency() <= RECALC_URGENCY_THRESHOLD;
		}

		int activeUpdate = 0;

		bool footprintDisabled = false;
		bool waitingForPlacement = true;

		int unitSize = 1;

		bool isBlocking = false;

		std::vector<TemporaryCost> costs;
		std::vector<MapNode::SharedReceiverType> receivers;
		std::vector<RecentlyBlockedNodeObserver> blockedNodeObservers;
		
		size_t currentPathIndex = 0;
		std::shared_ptr<Path> ourPath;
		std::vector<PathNode> calculatedPath;

		int ourDebugId = 0;
	};

	// Out-of-line MapNode save/load for JaiScript only
	template<class Archive>
		requires jai::serialization::jai_archive<Archive>
	void MapNode::save(Archive& archive, std::uint32_t const /*version*/) const {
		archive("location", location);
		archive("useCorners", useCorners);
		archive("travelCost", travelCost);
		archive("staticBlockedSemaphore", staticBlockedSemaphore);
	}

	template<class Archive>
		requires jai::serialization::jai_archive<Archive>
	void MapNode::load(Archive& archive, std::uint32_t const /*version*/) {
		archive("location", location);
		archive("useCorners", useCorners);
		archive("travelCost", travelCost);
		archive("staticBlockedSemaphore", staticBlockedSemaphore);
		// map pointer set by Map::load_and_construct after deserializing squares
	}

	void MapNode::block() {
		bool wasBlocked = blocked();
		blockedSemaphore++;
		if (!wasBlocked) {
			auto sharedMap = map->shared_from_this();
			onBlockSignal(sharedMap, location);
			if(blocked()){clearanceAmount = 0; onClearanceChangeSignal(sharedMap, location);}
			else { calculateClearance(); }
		}
	}

	void MapNode::unblock() {
		require<ResourceException>(blockedSemaphore > 0, "Error: Block Semaphore overextended in MapNode, something is unblocking excessively.");
		blockedSemaphore--;
		if (!blocked()) {
			auto sharedMap = map->shared_from_this();
			onUnblockSignal(sharedMap, location);
			calculateClearance();
		}
	}

}

#endif
