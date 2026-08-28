#ifndef _MV_PATHFINDING_H_
#define _MV_PATHFINDING_H_

#include <iostream>
#include <string>
#include <regex>
#include <vector>
#include <array>
#include <list>
#include <deque>
#include <algorithm>
#include <queue>
#include <memory>
#include <chrono>
#include <cmath>

#include "MV/Utility/generalUtility.h"
#include "MV/Utility/scopeGuard.hpp"
#include <jaiscript/signals/signal_decl.hpp>
#include "MV/Render/points.h"
#include <jaiscript/serialization/construct.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {

	class Map;
	class TemporaryCost;
	class Path;
	class NavigationAgent;
	class FlowField;
	class MapNode {
		friend TemporaryCost;
		friend Map;  // For sparse serialization access to travelCost/staticBlockedSemaphore
		friend class jai::access;
		friend jai::access;
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

		// Forget every derived fact and connection; the next lazyInitialize rebuilds from raw state
		void resetObservation() {
			initialized = false;
			edges.fill(nullptr);
			clearanceReceivers.clear();
			clearanceAmount = 0;
			onBlockSignal.clear();
			onUnblockSignal.clear();
			onStaticBlockSignal.clear();
			onStaticUnblockSignal.clear();
			onCostChangeSignal.clear();
			onClearanceChangeSignal.clear();
		}

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

		// Per-search visit stamp: a cell belongs to the current search iff searchStamp matches
		// the map's generation (no open/closed list scans)
		friend Path;
		friend Map;
		mutable uint32_t searchStamp = 0;
		mutable int32_t searchIndex = -1;
		NavigationAgent* occupant = nullptr;
	};

	class Map : public std::enable_shared_from_this<Map> {
		friend class jai::access;
		friend jai::access;
	public:
		// load_and_construct must be public for trait detection
		template<typename Archive>
		static void load_and_construct(Archive& ar,
		                               jai::serialization::construct<Map>& c)
			requires jai::serialization::jai_archive<Archive> {
			c();  // Default construct via jai::access friend
			ar("usingCorners", c->usingCorners);
			ar("squares", c->squares);
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

		// Sized so a full-map path completes in one search on the game's 250x100 grid: at ~160 ns/node
		// even a fully spent budget is ~5 ms of server tick; typical ticks spend a fraction of it
		static constexpr int64_t FRAME_NODE_BUDGET = 128000;
		static constexpr double BUDGET_RESET_INTERVAL = 1.0 / 60.0;

		void ensureCellsInitialized();
		void reportStalls() const;

		void tickBudget() const {
			if (drivenByTicks) { return; }
			auto now = std::chrono::steady_clock::now();
			double elapsed = std::chrono::duration<double>(now - lastBudgetReset).count();
			if (elapsed >= BUDGET_RESET_INTERVAL) {
				nodesUsedThisFrame = 0;
				lastBudgetReset = now;
			}
		}

		uint32_t nextSearchGeneration() const {
			if (++searchGeneration == 0) { ++searchGeneration; }
			return searchGeneration;
		}

		// Explicit per-simulation-tick reset (deterministic; tickBudget() is the wall-clock fallback
		// for hosts that never call this). Queued searches are then served in request order,
		// urgent ones first, so an agent that has waited longest always gets budget before one
		// that merely updates earlier in the frame.
		void beginTick();

		void requestSearch(const std::shared_ptr<NavigationAgent>& a_agent, bool a_urgent);

		bool searchQueueEmpty() const {
			return searchQueue.empty();
		}

		size_t searchQueueLength() const {
			return searchQueue.size();
		}

		bool searchQueueContains(const NavigationAgent* a_agent) const {
			for (auto& weak : searchQueue) {
				if (auto agent = weak.lock()) { if (agent.get() == a_agent) { return true; } }
			}
			return false;
		}

		static constexpr int64_t MIN_SEARCH_NODES = 50;

		// The fresh field for a goal, or nullptr when it would have to be computed and this tick's
		// field allowance is spent (callers fall back to A* for that request)
		std::shared_ptr<FlowField> flowFieldFor(const Point<int>& a_goal, PointPrecision a_radius, int a_unitSize);
		static constexpr size_t MAX_FLOW_FIELDS = 16;
		static constexpr int FIELDS_PER_TICK = 1;

		// Fields depend on static blocks and base costs only; agents' temporary path costs never
		// invalidate them (they would, every tick, and every agent would fall back to searching)
		void invalidateFlowFields() {
			++flowGeneration;
		}

		uint64_t tick() const {
			return tickCount;
		}

		bool tickDriven() const {
			return drivenByTicks;
		}

		// Footprint fits ignoring other agents (static blocks only)
		bool clearedForSizeStatic(const Point<int>& a_topLeft, int a_size) const {
			for (int x = a_topLeft.x; x < a_topLeft.x + a_size; ++x) {
				for (int y = a_topLeft.y; y < a_topLeft.y + a_size; ++y) {
					if (!inBounds(Point<int>(x, y)) || squares[x][y].staticallyBlocked()) { return false; }
				}
			}
			return true;
		}

		// Cells of a footprint held by agents other than a_self
		int occupiedCellsExcept(const Point<int>& a_topLeft, int a_size, const NavigationAgent* a_self) const {
			int count = 0;
			for (int x = a_topLeft.x; x < a_topLeft.x + a_size; ++x) {
				for (int y = a_topLeft.y; y < a_topLeft.y + a_size; ++y) {
					auto* occupant = agentAt(Point<int>(x, y));
					if (occupant != nullptr && occupant != a_self) { ++count; }
				}
			}
			return count;
		}

		// Which agent's footprint covers a cell (nullptr when free or statically blocked)
		NavigationAgent* agentAt(const Point<int>& a_cell) const {
			return inBounds(a_cell) ? squares[a_cell.x][a_cell.y].occupant : nullptr;
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
		friend NavigationAgent;
		void occupy(const Point<int>& a_cell, NavigationAgent* a_agent) {
			if (inBounds(a_cell)) { squares[a_cell.x][a_cell.y].occupant = a_agent; }
		}
		void vacate(const Point<int>& a_cell) {
			if (inBounds(a_cell)) { squares[a_cell.x][a_cell.y].occupant = nullptr; }
		}

		mutable uint32_t searchGeneration = 0;
		std::deque<std::weak_ptr<NavigationAgent>> searchQueue;
		bool drivenByTicks = false;
		bool cellsInitialized = false;
		uint64_t tickCount = 0;
		uint64_t flowGeneration = 1;
		int fieldsComputedThisTick = 0;
		std::vector<std::shared_ptr<FlowField>> flowFields;
		uint32_t agentIds = 0;
		uint32_t registerAgent() {
			return ++agentIds;
		}

		Map();
		Map(const Size<int> &a_size, float a_defaultCost, bool a_useCorners);
		Map(const Map &) = delete;
		Map operator=(const Map&) = delete;

		void hookUpObservation();

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
			// map pointers for nodes are set in load_and_construct
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
			startPosition(clampToMap(a_start)),
			goalPosition(clampToMap(a_end)) {
		}

		// Runs the search to its node cap and returns the (possibly partial) path
		std::vector<PathNode> path() {
			step(maxSearchNodes);
			return result();
		}

		// Time-sliced search: expands up to a_nodes more nodes and returns true once the search is
		// complete (goal reached or frontier exhausted). Searches keep their frontier between slices.
		bool step(int64_t a_nodes);

		// The path from the current state: the found path, or the partial path toward the closest
		// expanded node when the search is incomplete or exhausted
		const std::vector<PathNode>& result();

		bool finished() const {
			return finishedFlag;
		}

		int64_t stepNodes() const {
			return stepSearched;
		}

		// Soft blocking: static blocks stay walls, cells under other agents cost extra instead of
		// being impassable — a route exists whenever one exists statically
		void softBlocking(bool a_soft, const NavigationAgent* a_owner) {
			softDynamic = a_soft;
			owner = a_owner;
		}

		bool soft() const {
			return softDynamic;
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

		int64_t nodesSearched() const {
			return searched;
		}
	private:
		struct SearchNode {
			MapNode* cell;
			int32_t parent;
			float costToArrive;
			float estimatedTotal;
			bool corner;
			bool closed;
		};

		struct OpenEntry {
			float estimatedTotal;
			uint32_t sequence;
			int32_t node;
		};

		Point<int> clampToMap(const Point<int>& a_point) const {
			auto mapSize = map->size();
			return Point<int>(std::clamp(a_point.x, 0, std::max(0, mapSize.width - 1)), std::clamp(a_point.y, 0, std::max(0, mapSize.height - 1)));
		}

		void begin();
		void resume();
		float heuristic(const Point<int>& a_position) const;
		void visit(MapNode& a_cell, int32_t a_parent, float a_costToArrive, bool a_corner, uint32_t a_generation);
		void pushOpen(int32_t a_node);
		int32_t popOpen();
		static bool openEntryAfter(const OpenEntry& a_lhs, const OpenEntry& a_rhs);

		static constexpr float SOFT_OCCUPANCY_COST = 8.0f;
		bool softDynamic = false;
		const NavigationAgent* owner = nullptr;
		bool found = false;
		bool started = false;
		bool finishedFlag = false;
		int64_t maxSearchNodes = -1;
		int64_t searched = 0;
		int64_t stepSearched = 0;
		int32_t current = -1;
		int32_t best = -1;
		PointPrecision bestDistance = -1;
		uint32_t generation = 0;
		PointPrecision minimumDistance = 0;
		int unitSize = 1;

		std::shared_ptr<Map> map;

		Point<int> startPosition;
		Point<int> goalPosition;
		Point<int> endPosition;

		std::vector<PathNode> pathNodes;
		std::vector<SearchNode> nodes;
		std::vector<OpenEntry> open;
		uint32_t sequence = 0;
	};

	// A shared distance field toward one goal region over static obstacles only: every agent
	// heading there reads its cell's gradient instead of searching. Dynamic blocking is left to
	// the movement layer (follow / swap / step aside), so a field stays valid while crowds move.
	class FlowField {
	public:
		FlowField(const std::shared_ptr<Map>& a_map, const Point<int>& a_goal, PointPrecision a_radius, int a_unitSize);

		std::vector<PathNode> descend(const Point<int>& a_from, size_t a_maxSteps) const;

		bool reachable(const Point<int>& a_cell) const {
			return inField(a_cell) && distances[index(a_cell)] != UNREACHABLE;
		}

		const Point<int>& goal() const { return goalCell; }
		PointPrecision radius() const { return goalRadius; }
		int unitSize() const { return footprint; }
		uint64_t generation = 0;
		uint64_t lastUsedTick = 0;

		static constexpr float UNREACHABLE = std::numeric_limits<float>::infinity();
	private:
		bool inField(const Point<int>& a_cell) const {
			return a_cell.x >= 0 && a_cell.y >= 0 && a_cell.x < width && a_cell.y < height;
		}
		size_t index(const Point<int>& a_cell) const {
			return static_cast<size_t>(a_cell.x) * static_cast<size_t>(height) + static_cast<size_t>(a_cell.y);
		}
		void compute(const std::shared_ptr<Map>& a_map);

		Point<int> goalCell;
		PointPrecision goalRadius;
		int footprint;
		int width;
		int height;
		bool corners;
		std::vector<float> distances;
	};

	class NavigationAgent : public std::enable_shared_from_this<NavigationAgent> {
		friend jai::access;
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

		bool queuedForSearch() const {
			return searchQueued;
		}

		// Every gate that can hold an agent still, in one line (diagnostics only)
		std::string debugState() const {
			std::string result = "id=" + std::to_string(agentId) + " dirty=" + std::to_string(dirtyPath) + " queued=" + std::to_string(searchQueued) +
				" inProgress=" + std::to_string(searchInProgress) + " retryIn=" + std::to_string(recalcRetryCountdown) + " backoff=" + std::to_string(recalcBackoff) +
				" waitPlace=" + std::to_string(waitingForPlacement) + " pathIdx=" + std::to_string(currentPathIndex) + "/" + std::to_string(calculatedPath.size()) +
				" blockedAt=" + std::to_string(blockedAtPathIndex) + " noProgress=" + std::to_string(ticksWithoutProgress) + " soft=" + std::to_string(softSearch) +
				" blockedTicks=" + std::to_string(blockedTicks) + " isBlocking=" + std::to_string(isBlocking) + " lastTick=" + std::to_string(lastUpdateTick) +
				" served=" + std::to_string(lastServedTick) + " since=" + std::to_string(queuedSinceTick) + " prio=" + std::to_string(searchQueued ? searchPriority() : -1) + " urgent=" + std::to_string(needsUrgentRecalculation()) + " queueLen=" + std::to_string(map->searchQueueLength()) + " cell=" + std::to_string(currentCell().x) + "," + std::to_string(currentCell().y);
			return result;
		}

		~NavigationAgent() {
			unblockMap();
		}

		std::shared_ptr<NavigationAgent> clone(const std::shared_ptr<Map> &a_map = nullptr) const {
			auto result = NavigationAgent::make(a_map == nullptr ? map : a_map, position(), unitSize, centerOffset.x != 0.0f);
			result->ourGoal = ourGoal;
			result->ourSpeed = ourSpeed;
			result->acceptableDistance = acceptableDistance;
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
				do { recalculate(); } while (searchInProgress);
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

		// THE cell convention: an agent is in the cell its centre lies in, so cells switch at the
		// midpoint between node centres and a node centre is never on a boundary. Footprint,
		// movement, search and negotiation all reason in these cells.
		Point<int> cellFor(const Point<PointPrecision>& a_center) const {
			return Point<int>(static_cast<int>(std::floor(a_center.x)), static_cast<int>(std::floor(a_center.y)));
		}

		Point<int> currentCell() const {
			return cellFor(ourPosition);
		}

		Point<int> goalCell() const {
			return cellFor(ourGoal);
		}

		bool overlaps(Point<int> a_position) const {
			auto topLeft = currentCell();
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
			auto goalDistance = static_cast<PointPrecision>(distance(currentCell(), goalCell()));
			return goalDistance > acceptableDistance;
		}
		NavigationAgent() :
			onArrive(onArriveSignal),
			onBlocked(onBlockedSignal),
			onStop(onStopSignal),
			onStart(onStartSignal){}
	private:
		friend Map;
		bool attemptToRecalculate();
		void performSearch();
		bool evaluateSearchResult();
		void runQueuedSearch();
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
			agentId = map->registerAgent();
			lastProgressCell = currentCell();
			blockMap();
		}
		NavigationAgent(const NavigationAgent &) = delete;
		NavigationAgent& operator=(const NavigationAgent&a_rhs) = delete;

		Point<PointPrecision> desiredPositionFromCalculatedPathIndex(size_t index) {
			if (cast<int>(calculatedPath[index].position()) == goalCell()) {
				return ourGoal;
			} else {
				return cast<PointPrecision>(calculatedPath[index].position()) + point(.5f, .5f);
			}
		}

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
			unblockMap();
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
			blockMap();
		}


		void markDirty() {
			costs.clear();
			receivers.clear();
			dirtyPath = true;
			recalcRetryCountdown = 0;
		}

		// A failed search backs off exponentially so a crowd of blocked agents cannot drain the
		// frame budget every frame; any cell freeing within a few squares retries at once.
		void deferRetryAfterFailedSearch() {
			recalcBackoff = std::min(std::max(recalcBackoff * 2, 1), MAX_RECALC_BACKOFF_FRAMES);
			recalcRetryCountdown = recalcBackoff;
			if (!nearbyUnblockReceiver) {
				nearbyUnblockReceiver = map->onUnblock.connect([this](std::shared_ptr<Map>, const Point<int> &a_position) {
					auto here = currentCell();
					if (!activeUpdate && std::max(std::abs(a_position.x - here.x), std::abs(a_position.y - here.y)) <= NEARBY_UNBLOCK_RADIUS) {
						recalcRetryCountdown = 0;
					}
				});
			}
		}

		void clearRetryBackoff() {
			recalcBackoff = 0;
			recalcRetryCountdown = 0;
			nearbyUnblockReceiver.reset();
		}

		// Spawn contention: after a short grace the agent takes the nearest free cell instead of
		// waiting on a square another agent may never leave.
		void placeNearby() {
			if (++placementWaitFrames < PLACEMENT_GRACE_FRAMES) {
				return;
			}
			auto origin = currentCell();
			bool goalWasHere = ourGoal == ourPosition;
			for (int radius = 1; radius <= PLACEMENT_SEARCH_RADIUS * std::max(1, unitSize); ++radius) {
				for (int dx = -radius; dx <= radius; ++dx) {
					for (int dy = -radius; dy <= radius; ++dy) {
						if (std::max(std::abs(dx), std::abs(dy)) != radius) { continue; }
						Point<int> candidate(origin.x + dx, origin.y + dy);
						if (map->clearedForSize(candidate, unitSize)) {
							ourPosition += point(static_cast<PointPrecision>(dx), static_cast<PointPrecision>(dy));
							if (goalWasHere) { ourGoal = ourPosition; }
							placementWaitFrames = 0;
							blockMap();
							markDirty();
							return;
						}
					}
				}
			}
		}

		void removeBlockedPathObservers(unsigned int a_pathId) {
			blockedNodeObservers.erase(std::remove_if(blockedNodeObservers.begin(), blockedNodeObservers.end(), [&](auto&& blockedObserver) {
				return blockedObserver.pathId == a_pathId;
			}), blockedNodeObservers.end());
		}

		void updateObservedNodes();

		// A shared field for the goal gives the next stretch of path by gradient descent: no search,
		// no budget, every agent every tick. Falls back to A* while no fresh field is available.
		bool followFlowField() {
			if (!useFlowFields || footprintDisabled) { return false; }
			auto field = map->flowFieldFor(goalCell(), acceptableDistance, unitSize);
			if (!field || !field->reachable(currentCell())) { return false; }
			auto nodes = field->descend(currentCell(), FLOW_STEPS);
			if (nodes.size() < 2) { return false; }
			receivers.clear();
			costs.clear();
			calculatedPath = std::move(nodes);
			ourPath.reset();
			searchInProgress = false;
			searchSlices = 0;
			followingField = true;
			currentPathIndex = 1;
			updateObservedNodes();
			dirtyPath = false;
			blockedAtPathIndex = -1;
			blockedSignalFired = false;
			return true;
		}

		// One slice of search per call; an unfinished search resumes next call (same start/goal)
		// instead of restarting, so a path that needs more than one budget grant still completes.
		void recalculate(int64_t searchLimit = DEFAULT_SEARCH_LIMIT) {
			if (followFlowField()) { return; }
			followingField = false;
			receivers.clear();
			costs.clear();

			unblockMap();
			auto start = currentCell();
			auto goal = goalCell();
			bool resumable = ourPath && searchInProgress && ourPath->start() == start && ourPath->goal() == goal && ourPath->soft() == softSearch;
			if (!resumable) {
				ourPath = std::make_shared<Path>(map, start, goal, acceptableDistance, unitSize, -1);
				ourPath->softBlocking(softSearch, this);
				searchSlices = 0;
			}
			bool complete = ourPath->step(searchLimit);
			++searchSlices;
			// Continue across ticks only while nobody else is waiting for budget; under contention a
			// partial path now beats a complete one several ticks later
			searchInProgress = !complete && searchSlices < MAX_SEARCH_SLICES && map->searchQueueEmpty();
			calculatedPath = searchInProgress ? std::vector<PathNode>() : ourPath->result();
			blockMap();

			currentPathIndex = !calculatedPath.empty() && currentCell() == calculatedPath[0].position() ? 1 : 0;
			updateObservedNodes();
			dirtyPath = searchInProgress;
			if (!searchInProgress) {
				blockedAtPathIndex = -1;
				blockedSignalFired = false;
			}
		}

		bool canPlaceOnMapAtCurrentPosition() {
			return map && map->clearedForSize(currentCell(), unitSize);
		}

		void blockMap(){
			if (isBlocking) {
				return;
			}
			if (footprintDisabled) {
				waitingForPlacement = false;
				return;
			}
			if (!canPlaceOnMapAtCurrentPosition()) {
				waitingForPlacement = true;
				return;
			}
			waitingForPlacement = false;
			isBlocking = true;
			blockedTopLeft = currentCell();
			blockedSize = unitSize;
			++activeUpdate;
			SCOPE_EXIT{ --activeUpdate; };
			forEachBlockedCell([this](MapNode &a_node) { a_node.block(); map->occupy(a_node.position(), this); });
		}

		void unblockMap() {
			if (!isBlocking) {
				return;
			}
			isBlocking = false;
			++activeUpdate;
			SCOPE_EXIT{ --activeUpdate; };
			forEachBlockedCell([this](MapNode &a_node) { a_node.unblock(); map->vacate(a_node.position()); });
		}

		template<typename Action>
		void forEachBlockedCell(Action a_action) {
			auto mapSize = map->size();
			auto right = std::min(blockedTopLeft.x + blockedSize, mapSize.width);
			auto bottom = std::min(blockedTopLeft.y + blockedSize, mapSize.height);
			for (int x = std::max(blockedTopLeft.x, 0); x < right; ++x) {
				for (int y = std::max(blockedTopLeft.y, 0); y < bottom; ++y) {
					a_action((*map)[x][y]);
				}
			}
		}

		struct RecentlyBlockedNodeObserver {
			RecentlyBlockedNodeObserver(NavigationAgent* a_self, const std::shared_ptr<Map> &a_map, const Point<int> &a_position, unsigned int a_pathId):
				pathId(a_pathId){
				// Captured by value: this observer lives in a vector, so its own address is not stable
				if (a_self->size() > 1) {
					receiver = a_map->get(a_position).onClearanceChange.connect([a_self, a_pathId](const std::shared_ptr<Map> &a_innerMap, const Point<int> &a_innerPosition) {
						if (!a_self->activeUpdate && !a_self->overlaps(a_innerPosition) && (a_self->size() >= a_innerMap->get(a_innerPosition).clearance())) {
							a_self->activeUpdate++;
							SCOPE_EXIT{ a_self->activeUpdate--; };
							a_self->markDirty();
							a_self->removeBlockedPathObservers(a_pathId);
						}
					});
				} else {
					receiver = a_map->get(a_position).onUnblock.connect([a_self, a_pathId](const std::shared_ptr<Map> &a_innerMap, const Point<int> &a_innerPosition) {
						if (!a_self->activeUpdate && !a_self->overlaps(a_innerPosition)) {
							a_self->activeUpdate++;
							SCOPE_EXIT{ a_self->activeUpdate--; };
							a_self->markDirty();
							a_self->removeBlockedPathObservers(a_pathId);
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

		static constexpr int RECALC_URGENCY_THRESHOLD = 3;
		static constexpr int64_t DEFAULT_SEARCH_LIMIT = 16000;
		static constexpr int MAX_SEARCH_SLICES = 8;
		static constexpr size_t MAX_LOOKAHEAD = 8;
		static constexpr size_t FLOW_STEPS = 12;
		bool followingField = false;
		bool useFlowFields = true;
		bool searchInProgress = false;
		int searchSlices = 0;

		// Every cell the segment from the agent's actual position to a node centre crosses is clear
		// for this unit (the agent walks exactly that segment, not centre to centre)
		bool lineOfSight(const Point<PointPrecision>& a_from, const Point<int>& a_toCell) const;

		// Local arbitration when the next cell holds another agent: two agents heading into each
		// other's cells swap places; an idle blocker is asked to step aside once we have waited.
		enum class BlockedResolution { none, swapped, freed };
		static constexpr int PUSH_ASIDE_TICKS = 20;
		BlockedResolution negotiateBlockedCell(const Point<int>& a_cell);
		bool negotiateAhead();
		bool negotiatePathAhead();
		bool stepAside(const Point<int>& a_awayFrom, int a_awayFromSize, int a_depth);
		Point<int> nextCellTowardGoal() const;
		int blockedTicks = 0;
		Point<int> blockedCell;

		// Progress guarantee: deterministic right of way (ticks without reaching a new cell, then
		// cells remaining, then a map-assigned id), convoys pulled front-first within a tick, and a
		// soft search once genuinely stuck so a route always exists where one exists statically
		static constexpr int SOFT_SEARCH_TICKS = 30;
		static constexpr int CROSSING_PUSH_TICKS = 3;
		static constexpr int PUSH_CASCADE_DEPTH = 3;
		static constexpr int FOLLOW_PATIENCE_TICKS = 15;
		static constexpr int FOLLOWER_PRIORITY_TICKS = 10;
		static constexpr int URGENT_PRIORITY_TICKS = 30;
		static constexpr int SWAP_COOLDOWN_TICKS = 6;
		bool urgentRequest = false;
		uint64_t queuedSinceTick = 0;
		uint64_t lastServedTick = 0;
		uint64_t lastSwapTick = 0;

		// Search-queue priority: ticks spent waiting IN THE QUEUE (reset when served, so being served
		// always costs an agent its place - a true round-robin); agents others are waiting behind,
		// and urgent blockages, jump the line by a fixed number of ticks
		int searchPriority() const {
			return static_cast<int>(map->tick() - queuedSinceTick) + FOLLOWER_PRIORITY_TICKS * followers() + (urgentRequest ? URGENT_PRIORITY_TICKS : 0);
		}
		uint32_t agentId = 0;
		uint64_t lastUpdateTick = UINT64_MAX;
		bool updating = false;
		double ourDt = 0.0;
		int ticksWithoutProgress = 0;
		Point<int> lastProgressCell;
		int followersThisTick = 0;
		uint64_t followersTick = UINT64_MAX;
		bool softSearch = false;

		float remainingDistance() const {
			return static_cast<float>(distance(ourPosition, ourGoal));
		}
		bool hasRightOfWayOver(const NavigationAgent& a_other) const;
		bool movingSameWay(const NavigationAgent& a_other) const;
		NavigationAgent* blockerAt(const Point<int>& a_topLeft) const;
		static bool footprintsOverlap(const Point<int>& a_topLeft, int a_size, const Point<int>& b_topLeft, int b_size) {
			return a_topLeft.x < b_topLeft.x + b_size && b_topLeft.x < a_topLeft.x + a_size &&
				a_topLeft.y < b_topLeft.y + b_size && b_topLeft.y < a_topLeft.y + a_size;
		}
		int followers() const {
			return followersTick == map->tick() ? followersThisTick : 0;
		}
		void noteFollower() {
			if (followersTick != map->tick()) { followersTick = map->tick(); followersThisTick = 0; }
			++followersThisTick;
		}

		// String-pulling on the fly: head for the farthest upcoming path node in line of sight,
		// so a grid path is walked as straight segments instead of centre-to-centre stair steps
		void advanceThroughVisibleNodes();
		static constexpr int64_t MIN_SEARCH_LIMIT = 50;
		static constexpr int MAX_RECALC_BACKOFF_FRAMES = 30;
		static constexpr int NEARBY_UNBLOCK_RADIUS = 4;
		static constexpr int PLACEMENT_GRACE_FRAMES = 2;
		static constexpr int PLACEMENT_SEARCH_RADIUS = 3;   // scaled by the footprint: a 3x3 unit searches 9 cells out

		int recalcBackoff = 0;
		int recalcRetryCountdown = 0;
		int placementWaitFrames = 0;
		bool searchQueued = false;
		Map::SharedReceiverType nearbyUnblockReceiver;

		int blockedAtPathIndex = -1;  // Earliest known blockage in path (-1 = none)
		bool blockedSignalFired = false;

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
		Point<int> blockedTopLeft;
		int blockedSize = 0;

		std::vector<TemporaryCost> costs;
		std::vector<MapNode::SharedReceiverType> receivers;
		std::vector<RecentlyBlockedNodeObserver> blockedNodeObservers;
		
		size_t currentPathIndex = 0;
		std::shared_ptr<Path> ourPath;
		std::vector<PathNode> calculatedPath;

		int ourDebugId = 0;
	};

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
