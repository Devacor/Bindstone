#ifndef _MV_SCENE_PATH_H_
#define _MV_SCENE_PATH_H_

#include "drawable.h"
#include "MV/ArtificialIntelligence/pathfinding.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {
		class PathAgent;
		class PathMap : public jai::property_owner<PathMap, Drawable> {
			friend Node;
			friend PathAgent;
			friend cereal::access;
			friend class jai::access;

		public:
			// JaiScript serialization - must be public for trait detection (templated for CRTP archives)
			// Using requires clause to hide from Cereal's trait detection
			template<typename Archive>
			static void load_and_construct(Archive& ar,
			                               jai::serialization::construct<PathMap>& c)
				requires jai::serialization::jai_archive<Archive> {
				c(std::shared_ptr<Node>(), Size<int>());
				c->property_mgr.load(ar);
				c->initialize();
			}

			DrawableDerivedAccessorsNoShowHide(PathMap)


			std::shared_ptr<PathMap> show() {
				auto self = std::static_pointer_cast<PathMap>(MV::Scene::Drawable::show());
				updateDebugViewSignals();
				return self;
			}
			std::shared_ptr<PathMap> hide() {
				auto self = std::static_pointer_cast<PathMap>(MV::Scene::Drawable::hide());
				updateDebugViewSignals();
				return self;
			}

			std::shared_ptr<PathMap> resizeGrid(const Size<int> &a_size) {
				map->resize(a_size);
				resizeNumberOfDebugDrawPoints();
				repositionDebugDrawPoints();
				return std::static_pointer_cast<PathMap>(shared_from_this());
			}

			std::shared_ptr<PathMap> cellSize(const Size<> &a_size) {
				cellDimensions = a_size;
				repositionDebugDrawPoints();
				return std::static_pointer_cast<PathMap>(shared_from_this());
			}

			Size<> cellSize() const {
				return cellDimensions.get();
			}

			Size<int> gridSize() const {
				return map->size();
			}

			MapNode& nodeFromGrid(const Point<int> &a_location) {
				return map->get(a_location);
			}

			MapNode& nodeFromGrid(const Point<> &a_location) {
				return map->get(MV::cast<int>(a_location));
			}

			MapNode& nodeFromLocal(const Point<> &a_location) {
				auto gridTile = MV::cast<int>((a_location - topLeftOffset) / toPoint(cellDimensions.get()));
				return map->get(gridTile);
			}

			Point<> gridFromLocal(const Point<> &a_location) {
				return (a_location - topLeftOffset) / toPoint(cellDimensions.get());
			}

			Point<> localFromGrid(const Point<int> &a_location) {
				return ((MV::cast<PointPrecision>(a_location) + MV::point(0.5f, 0.5f)) * toPoint(cellDimensions.get())) + topLeftOffset;
			}

			Point<> localFromGrid(const Point<> &a_location) {
				return (a_location * toPoint(cellDimensions.get())) + topLeftOffset;
			}

			PointPrecision localFromGrid(PointPrecision a_gridSize) {
				return ((cellDimensions->width + cellDimensions->height) / 2.0f) * a_gridSize;
			}

			PointPrecision gridFromLocal(PointPrecision a_localSize) {
				return a_localSize / ((cellDimensions->width + cellDimensions->height) / 2.0f);
			}

			bool inBounds(Point<int> a_location) const {
				return map->inBounds(a_location);
			}

			bool blocked(Point<int> a_location) const {
				return map->blocked(a_location);
			}

			bool staticallyBlocked(Point<int> a_location) const {
				return map->staticallyBlocked(a_location);
			}

			bool traverseCorners() const {
				return map->corners();
			}

		protected:
			PathMap(const std::weak_ptr<Node> &a_owner, const Size<int> &a_gridSize, bool a_useCorners = true) :
				PathMap(a_owner, Size<>(1.0f, 1.0f), a_gridSize, a_useCorners) {
			}

			PathMap(const std::weak_ptr<Node> &a_owner, const Size<> &a_size, const Size<int> &a_gridSize, bool a_useCorners = true);

			virtual bool serializePoints() const override { return false; }

			virtual BoxAABB<> boundsImplementation() override { return Drawable::boundsImplementation(); }

			virtual void boundsImplementation(const BoxAABB<> &a_bounds) override {
				topLeftOffset = a_bounds.minPoint;
				auto mapSize = cast<PointPrecision>(map->size());
				cellDimensions = a_bounds.size() / mapSize;
				repositionDebugDrawPoints();
			}

			void resizeNumberOfDebugDrawPoints() {
				size_t totalMapNodes = static_cast<size_t>(map->size().width * map->size().height);
				points->resize(static_cast<size_t>(4) * totalMapNodes);
				clearTexturePoints(*points);

				vertexIndices->clear();
				for (int i = 0; i < totalMapNodes; ++i) {
					appendQuadVertexIndices(*vertexIndices, i * 4);
				}

			}

			virtual void initialize() override {
				Drawable::initialize();

				updateDebugViewSignals();

				resizeNumberOfDebugDrawPoints();
				repositionDebugDrawPoints();
			}

			void updateDebugViewSignals();

			void repositionDebugDrawPoints();

			template<class Archive>
			void save(Archive & a_archive, std::uint32_t const /*version*/) const {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.save(a_archive);
					Drawable::save(a_archive, 0);
				} else {
					a_archive(
						cereal::make_nvp("map", map.get()),
						cereal::make_nvp("offset", topLeftOffset.get()),
						cereal::make_nvp("cellDimensions", cellDimensions.get()),
						cereal::make_nvp("Component", cereal::base_class<Drawable>(this))
					);
				}
			}

			template<class Archive>
			void load(Archive & a_archive, std::uint32_t const version) {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.load(a_archive);
					Drawable::load(a_archive, 0);
				} else {
					a_archive(
						cereal::make_nvp("map", map.get()),
						cereal::make_nvp("offset", topLeftOffset.get()),
						cereal::make_nvp("cellDimensions", cellDimensions.get()),
						cereal::make_nvp("Component", cereal::base_class<Drawable>(this))
					);
				}
			}

			template<class Archive>
			static void load_and_construct(Archive & a_archive, cereal::construct<PathMap> &a_construct, std::uint32_t const version) {
				a_construct(std::shared_ptr<Node>(), Size<int>());
				a_construct->load(a_archive, version);
				a_construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<PathMap>(cellDimensions, map->size(), map->corners()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
				Drawable::cloneHelper(a_clone);
				auto mapClone = std::static_pointer_cast<PathMap>(a_clone);
				mapClone->map = map->clone();
				mapClone->cellDimensions = cellDimensions;
				return a_clone;
			}

		private:
			static Color alternatingDebugTilesWithClearance(int a_x, int a_y, int a_clearance);

			static const std::vector<Color> alternatingDebugTiles;
			static const Color staticBlockedDebugTile;
			static const Color regularBlockedDebugTile;

			// Clone is handled manually in cloneHelper
			JAI_PROPERTY((std::shared_ptr<Map>), map, nullptr, [](auto&, auto&) {});
			JAI_PROPERTY((MV::Point<>), topLeftOffset, {}, [](auto&, auto&) {});
			JAI_PROPERTY((MV::Size<PointPrecision>), cellDimensions, {}, [](auto&, auto&) {});
		};


		class PathAgent : public jai::property_owner<PathAgent, Component> {
			friend Node;
			friend cereal::access;
		public:
			typedef void CallbackSignature(std::shared_ptr<PathAgent>);
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

			ComponentDerivedAccessors(PathMap)

			Point<PointPrecision> gridPosition() const {
				return agent->position();
			}

			Point<PointPrecision> localPosition() const {
				auto ourOwner = owner();
				if (ourOwner->parent() == map->owner()) {
					return map->localFromGrid(agent->position());
				}
				else {
					return ourOwner->localFromWorld(map->owner()->worldFromLocal(map->localFromGrid(agent->position())));
				}
			}

			std::vector<PathNode> path() {
				return agent->path();
			}

			std::shared_ptr<PathAgent> gridPosition(const Point<int> &a_newPosition) {
				agent->position(a_newPosition);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> gridPosition(const Point<> &a_newPosition) {
				agent->position(a_newPosition);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> localPosition(const Point<> &a_newPosition) {
				agent->position(map->gridFromLocal(a_newPosition));
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> gridGoal(const Point<int> &a_newGoal) {
				return gridGoal(a_newGoal, 0.0f);
			}

			std::shared_ptr<PathAgent> gridGoal(const Point<int> &a_newGoal, PointPrecision a_acceptableDistance) {
				agent->goal(a_newGoal, a_acceptableDistance);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> gridGoal(const Point<> &a_newGoal) {
				return gridGoal(a_newGoal, 0.0f);
			}

			std::shared_ptr<PathAgent> gridGoal(const Point<> &a_newGoal, PointPrecision a_acceptableDistance) {
				agent->goal(a_newGoal, a_acceptableDistance);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> localGoal(const Point<> &a_newGoal) {
				return gridGoal(map->gridFromLocal(a_newGoal));
			}

			std::shared_ptr<PathAgent> localGoal(const Point<> &a_newGoal, PointPrecision a_acceptableDistance) {
				return gridGoal(map->gridFromLocal(a_newGoal), map->gridFromLocal(a_acceptableDistance));
			}

			Point<PointPrecision> gridGoal() const {
				return agent->goal();
			}

			Point<PointPrecision> localGoal() const {
				return map->localFromGrid(gridGoal());
			}

			PointPrecision gridSpeed() const {
				return agent->speed();
			}

			PointPrecision localSpeed() const {
				return map->localFromGrid(gridSpeed());
			}

			std::shared_ptr<PathAgent> gridSpeed(PointPrecision a_newSpeed) {
				agent->speed(a_newSpeed);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}
			std::shared_ptr<PathAgent> localSpeed(PointPrecision a_newSpeed) {
				return gridSpeed(map->localFromGrid(a_newSpeed));
			}

			bool pathfinding() const {
				return agent->pathfinding();
			}

			std::shared_ptr<PathAgent> stop() {
				agent->stop();
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			int gridSize() const {
				return agent->size();
			}

			bool gridOverlaps(Point<int> a_position) const {
				return agent->overlaps(a_position);
			}

			bool localOverlaps(Point<> a_position) const {
				return agent->overlaps(cast<int>(map->gridFromLocal(a_position)));
			}

			std::shared_ptr<PathAgent> disableFootprint() {
				agent->disableFootprint();
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> enableFootprint() {
				agent->enableFootprint();
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			bool hasFootprint() const {
				return !agent->hasFootprint();
			}

		protected:
			PathAgent(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<PathMap> &a_map, const Point<> &a_gridPosition, int a_unitSize = 1) :
				jai::property_owner<PathAgent, Component>(a_owner),
				onArrive(onArriveSignal),
				onBlocked(onBlockedSignal),
				onStop(onStopSignal),
				onStart(onStartSignal){
				map = a_map;
				agent = NavigationAgent::make(a_map->map.get(), a_gridPosition, a_unitSize);
			}

			PathAgent(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<PathMap> &a_map, const Point<int> &a_gridPosition, int a_unitSize = 1) :
				jai::property_owner<PathAgent, Component>(a_owner),
				onArrive(onArriveSignal),
				onBlocked(onBlockedSignal),
				onStop(onStopSignal),
				onStart(onStartSignal) {
				map = a_map;
				agent = NavigationAgent::make(a_map->map.get(), a_gridPosition, a_unitSize);
			}
			
			virtual void updateImplementation(double a_dt) override {
				agent->update(a_dt);
				applyAgentPositionToOwner();
			}

			void applyAgentPositionToOwner() {
				auto ourOwner = owner();
				if (ourOwner->parent() == map->owner()) {
					ourOwner->position(map->localFromGrid(agent->position()));
				} else {
					ourOwner->worldPosition(map->owner()->worldFromLocal(map->localFromGrid(agent->position())));
				}
			}

			template<class Archive>
			void save(Archive & a_archive, std::uint32_t const /*version*/) const {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.save(a_archive);
					Component::save(a_archive, 0);
				} else {
					a_archive(
						cereal::make_nvp("map", map.get()),
						cereal::make_nvp("agent", agent.get()),
						cereal::make_nvp("Component", cereal::base_class<Component>(this))
					);
				}
			}

			template<class Archive>
			void load(Archive & a_archive, std::uint32_t const /*version*/) {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.load(a_archive);
					Component::load(a_archive, 0);
				} else {
					a_archive(
						cereal::make_nvp("map", map.get()),
						cereal::make_nvp("agent", agent.get()),
						cereal::make_nvp("Component", cereal::base_class<Component>(this))
					);
				}
			}

			template<class Archive>
			static void load_and_construct(Archive & a_archive, cereal::construct<PathAgent> &a_construct, std::uint32_t const version) {
				a_construct(std::shared_ptr<Node>());
				a_construct->load(a_archive, version);
				a_construct->initialize();
			}

			virtual void initialize() override;

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<PathAgent>(map, agent->position()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
				Component::cloneHelper(a_clone);
				auto agentClone = std::static_pointer_cast<PathAgent>(a_clone);
				agentClone->map = map;
				agentClone->agent = agent->clone();
				return a_clone;
			}
		private:
			//only for loading
			PathAgent(const std::weak_ptr<Node> &a_owner) :
				jai::property_owner<PathAgent, Component>(a_owner),
				onArrive(onArriveSignal),
				onBlocked(onBlockedSignal),
				onStop(onStopSignal),
				onStart(onStartSignal) {
			}
			std::vector<NavigationAgent::SharedReceiverType> agentPassthroughSignals;
			// Clone is handled manually in cloneHelper
			JAI_PROPERTY((std::shared_ptr<PathMap>), map, nullptr, [](auto&, auto&) {});
			JAI_PROPERTY((std::shared_ptr<NavigationAgent>), agent, nullptr, [](auto&, auto&) {});
		};
	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_scenepath);

#endif
