#ifndef _MV_SCENE_PATH_H_
#define _MV_SCENE_PATH_H_

#include "drawable.h"
#include "ArtificialIntelligence/pathfinding.h"

namespace MV {
	namespace Scene {
		class PathAgent;
		class PathMap : public Drawable {
			friend Node;
			friend PathAgent;
			friend cereal::access;

		public:
			DrawableDerivedAccessors(PathMap)

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
				return cellDimensions;
			}

			Size<int> gridSize() const {
				return map->size();
			}

			MapNode& nodeFromGrid(const Point<int> &a_location) {
				return map->get(a_location);
			}

			MapNode& nodeFromGrid(const Point<> &a_location) {
				return map->get(cast<int>(a_location));
			}

			MapNode& nodeFromLocal(const Point<> &a_location) {
				auto gridTile = cast<int>((a_location - topLeftOffset) / toPoint(cellDimensions));
				return map->get(gridTile);
			}

			Point<> gridFromLocal(const Point<> &a_location) {
				return (a_location - topLeftOffset) / toPoint(cellDimensions);
			}

			Point<> localFromGrid(const Point<int> &a_location) {
				return ((cast<PointPrecision>(a_location) + MV::point(0.5f, 0.5f)) * toPoint(cellDimensions)) + topLeftOffset;
			}

			Point<> localFromGrid(const Point<> &a_location) {
				return (a_location * toPoint(cellDimensions)) + topLeftOffset;
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

			std::shared_ptr<PathMap> bounds(const BoxAABB<> &a_bounds) {
				auto self = std::static_pointer_cast<PathMap>(shared_from_this());
				topLeftOffset = a_bounds.minPoint;
				auto mapSize = cast<PointPrecision>(map->size());
				cellDimensions = a_bounds.size() / mapSize;
				repositionDebugDrawPoints();
				return self;
			}
			BoxAABB<> bounds() {
				return boundsImplementation();
			}

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
				a_script.add(chaiscript::user_type<PathMap>(), "PathMap");
				a_script.add(chaiscript::base_class<Drawable, PathMap>());

				a_script.add(chaiscript::fun([](Node &a_self, const Size<int> &a_gridSize, bool a_useCorners) { return a_self.attach<PathMap>(a_gridSize, a_useCorners); }), "attachPathMap");
				a_script.add(chaiscript::fun([](Node &a_self, const Size<> &a_size, const Size<int> &a_gridSize, bool a_useCorners) { return a_self.attach<PathMap>(a_size, a_gridSize, a_useCorners); }), "attachPathMap");

				a_script.add(chaiscript::fun(&PathMap::inBounds), "inBounds");
				a_script.add(chaiscript::fun(&PathMap::traverseCorners), "traverseCorners");
				a_script.add(chaiscript::fun(&PathMap::resizeGrid), "resizeGrid");
				a_script.add(chaiscript::fun(&PathMap::gridSize), "gridSize");
				a_script.add(chaiscript::fun(&PathMap::blocked), "blocked");
				a_script.add(chaiscript::fun(&PathMap::staticallyBlocked), "staticallyBlocked");

				a_script.add(chaiscript::fun(static_cast<Size<>(PathMap::*)() const>(&PathMap::cellSize)), "cellSize");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathMap>(PathMap::*)(const Size<> &)>(&PathMap::cellSize)), "cellSize");

				a_script.add(chaiscript::fun(static_cast<MapNode&(PathMap::*)(const Point<int> &)>(&PathMap::nodeFromGrid)), "nodeFromGrid");
				a_script.add(chaiscript::fun(static_cast<MapNode&(PathMap::*)(const Point<> &)>(&PathMap::nodeFromGrid)), "nodeFromGrid");

				a_script.add(chaiscript::fun(static_cast<MapNode&(PathMap::*)(const Point<> &)>(&PathMap::nodeFromLocal)), "nodeFromLocal");

				a_script.add(chaiscript::fun(static_cast<Point<>(PathMap::*)(const Point<> &)>(&PathMap::gridFromLocal)), "gridFromLocal");

				a_script.add(chaiscript::fun(static_cast<Point<>(PathMap::*)(const Point<> &)>(&PathMap::localFromGrid)), "localFromGrid");
				a_script.add(chaiscript::fun(static_cast<Point<>(PathMap::*)(const Point<int> &)>(&PathMap::localFromGrid)), "localFromGrid");

				a_script.add(chaiscript::type_conversion<SafeComponent<PathMap>, std::shared_ptr<PathMap>>([](const SafeComponent<PathMap> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<PathMap>, std::shared_ptr<Drawable>>([](const SafeComponent<PathMap> &a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<PathMap>, std::shared_ptr<Component>>([](const SafeComponent<PathMap> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));

				return a_script;
			}
		protected:
			PathMap(const std::weak_ptr<Node> &a_owner, const Size<int> &a_gridSize, bool a_useCorners = true) :
				PathMap(a_owner, Size<>(1.0f, 1.0f), a_gridSize, a_useCorners) {
			}

			PathMap(const std::weak_ptr<Node> &a_owner, const Size<> &a_size, const Size<int> &a_gridSize, bool a_useCorners = true);

			void resizeNumberOfDebugDrawPoints() {
				auto totalMapNodes = (map->size().width * map->size().height);
				points.resize(4 * totalMapNodes);
				clearTexturePoints(points);

				vertexIndices.clear();
				for (int i = 0; i < totalMapNodes; ++i) {
					appendQuadVertexIndices(vertexIndices, i * 4);
				}

			}

			virtual void initialize() override {
				Drawable::initialize();
				map->onStaticBlock.connect("_PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					int index = (a_position.x * map->size().height) + a_position.y;
					for (int i = 0; i < 4; ++i) {
						points[(index * 4) + i] = staticBlockedDebugTile;
					}
				});

				map->onStaticUnblock.connect("_PARENT", [&](std::shared_ptr<Map> a_self, const Point<int> &a_position) {
					int index = (a_position.x * map->size().height) + a_position.y;
					for (int i = 0; i < 4; ++i) {
						points[(index * 4) + i] = alternatingDebugTiles[(a_position.x + a_position.y) % 2];
					}
				});
				resizeNumberOfDebugDrawPoints();
				repositionDebugDrawPoints();
			}

			void repositionDebugDrawPoints();

			template <class Archive>
			void serialize(Archive & a_archive) {
				std::vector<DrawPoint> tmpPoints{bounds().minPoint, bounds().maxPoint};
				std::vector<GLuint> tmpVertexIndices;

				std::swap(points, tmpPoints);
				std::swap(vertexIndices, tmpVertexIndices);

				SCOPE_EXIT{
					std::swap(points, tmpPoints);
					std::swap(vertexIndices, tmpVertexIndices);
				};

				a_archive(
					cereal::make_nvp("map", map),
					cereal::make_nvp("offset", topLeftOffset),
					cereal::make_nvp("cellDimensions", cellDimensions),
					cereal::make_nvp("Component", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & a_archive, cereal::construct<PathMap> &a_construct) {
				a_construct(std::shared_ptr<Node>(), Size<int>());
				a_archive(
					cereal::make_nvp("map", a_construct->map),
					cereal::make_nvp("offset", a_construct->topLeftOffset),
					cereal::make_nvp("cellDimensions", a_construct->cellDimensions),
					cereal::make_nvp("Component", cereal::base_class<Drawable>(a_construct.ptr()))
				);
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
			static const std::vector<Color> alternatingDebugTiles;
			static const Color staticBlockedDebugTile;

			std::shared_ptr<Map> map;
			MV::Point<> topLeftOffset;
			MV::Size<PointPrecision> cellDimensions;
		};


		class PathAgent : public Component {
			friend Node;
			friend cereal::access;
		public:
			typedef void CallbackSignature(std::shared_ptr<PathAgent>);
			typedef SignalRegister<CallbackSignature>::SharedRecieverType SharedRecieverType;
		private:
			Signal<CallbackSignature> onArriveSignal;
			Signal<CallbackSignature> onBlockedSignal;
			Signal<CallbackSignature> onStopSignal;
			Signal<CallbackSignature> onStartSignal;

		public:
			SignalRegister<CallbackSignature> onArrive;
			SignalRegister<CallbackSignature> onBlocked;
			SignalRegister<CallbackSignature> onStop;
			SignalRegister<CallbackSignature> onStart;

			ComponentDerivedAccessors(PathMap)

			Point<PointPrecision> gridPosition() const {
				return agent->position();
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

			Point<PointPrecision> gridGoal() const {
				return agent->goal();
			}

			PointPrecision gridSpeed() const {
				return agent->speed();
			}

			std::shared_ptr<PathAgent> gridSpeed(PointPrecision a_newSpeed) {
				agent->speed(a_newSpeed);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}
			bool pathfinding() const {
				return agent->pathfinding();
			}

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
				a_script.add(chaiscript::user_type<PathAgent>(), "PathAgent");
				a_script.add(chaiscript::base_class<Component, PathAgent>());

				a_script.add(chaiscript::fun([](Node &a_self, const std::shared_ptr<PathMap> &a_map, const Point<> &a_gridPosition) { return a_self.attach<PathAgent>(a_map, a_gridPosition); }), "attachPathAgent");
				a_script.add(chaiscript::fun([](Node &a_self, const std::shared_ptr<PathMap> &a_map, const Point<int> &a_gridPosition) { return a_self.attach<PathAgent>(a_map, a_gridPosition); }), "attachPathAgent");

				a_script.add(chaiscript::fun([](Node &a_self, const std::shared_ptr<PathMap> &a_map, const Point<> &a_gridPosition, int a_unitSize) { return a_self.attach<PathAgent>(a_map, a_gridPosition, a_unitSize); }), "attachPathAgent");
				a_script.add(chaiscript::fun([](Node &a_self, const std::shared_ptr<PathMap> &a_map, const Point<int> &a_gridPosition, int a_unitSize) { return a_self.attach<PathAgent>(a_map, a_gridPosition, a_unitSize); }), "attachPathAgent");

				a_script.add(chaiscript::fun(&PathAgent::pathfinding), "pathfinding");
				a_script.add(chaiscript::fun(&PathAgent::path), "path");

				a_script.add(chaiscript::fun(static_cast<PointPrecision (PathAgent::*)() const>(&PathAgent::gridSpeed)), "gridSpeed");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent> (PathAgent::*)(PointPrecision)>(&PathAgent::gridSpeed)), "gridSpeed");

				a_script.add(chaiscript::fun(static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::gridGoal)), "gridGoal");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<PointPrecision>&, PointPrecision)>(&PathAgent::gridGoal)), "gridGoal");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<int>&, PointPrecision)>(&PathAgent::gridGoal)), "gridGoal");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<PointPrecision>&)>(&PathAgent::gridGoal)), "gridGoal");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<int>&)>(&PathAgent::gridGoal)), "gridGoal");

				a_script.add(chaiscript::fun(static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::gridPosition)), "gridPosition");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<PointPrecision>&)>(&PathAgent::gridPosition)), "gridPosition");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<int>&)>(&PathAgent::gridPosition)), "gridPosition");

				MV::SignalRegister<CallbackSignature>::hook(a_script);
				a_script.add(chaiscript::fun(&PathAgent::onArrive), "onArrive");
				a_script.add(chaiscript::fun(&PathAgent::onBlocked), "onBlocked");
				a_script.add(chaiscript::fun(&PathAgent::onStop), "onStop");
				a_script.add(chaiscript::fun(&PathAgent::onStart), "onStart");

				a_script.add(chaiscript::type_conversion<SafeComponent<PathAgent>, std::shared_ptr<PathAgent>>([](const SafeComponent<PathAgent> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<PathAgent>, std::shared_ptr<Component>>([](const SafeComponent<PathAgent> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));

				return a_script;
			}

		protected:
			PathAgent(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<PathMap> &a_map, const Point<> &a_gridPosition, int a_unitSize = 1) :
				Component(a_owner),
				map(a_map),
				agent(NavigationAgent::make(a_map->map, a_gridPosition, a_unitSize)),
				onArrive(onArriveSignal),
				onBlocked(onBlockedSignal),
				onStop(onStopSignal),
				onStart(onStartSignal){
			}

			PathAgent(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<PathMap> &a_map, const Point<int> &a_gridPosition, int a_unitSize = 1) :
				Component(a_owner),
				map(a_map),
				agent(NavigationAgent::make(a_map->map, a_gridPosition, a_unitSize)),
				onArrive(onArriveSignal),
				onBlocked(onBlockedSignal),
				onStop(onStopSignal),
				onStart(onStartSignal) {
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

			template <class Archive>
			void serialize(Archive & a_archive) {
				a_archive(
					cereal::make_nvp("map", map),
					cereal::make_nvp("agent", agent),
					cereal::make_nvp("Component", cereal::base_class<Component>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & a_archive, cereal::construct<PathAgent> &a_construct) {
				a_construct(std::shared_ptr<Node>());
				a_archive(
					cereal::make_nvp("map", a_construct->map),
					cereal::make_nvp("agent", a_construct->agent),
					cereal::make_nvp("Component", cereal::base_class<Component>(a_construct.ptr()))
				);
				a_construct->initialize();
			}

			virtual void initialize() override {
				applyAgentPositionToOwner();
				agentPassthroughSignals.push_back(agent->onArrive.connect([&](std::shared_ptr<NavigationAgent>){
					onArriveSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
				}));
				agentPassthroughSignals.push_back(agent->onBlocked.connect([&](std::shared_ptr<NavigationAgent>) {
					onBlockedSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
				}));
				agentPassthroughSignals.push_back(agent->onStop.connect([&](std::shared_ptr<NavigationAgent>) {
					onStopSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
				}));
				agentPassthroughSignals.push_back(agent->onStart.connect([&](std::shared_ptr<NavigationAgent>) {
					onStartSignal(std::static_pointer_cast<PathAgent>(shared_from_this()));
				}));
			}

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
				Component(a_owner),
				map(nullptr),
				agent(nullptr),
				onArrive(onArriveSignal),
				onBlocked(onBlockedSignal),
				onStop(onStopSignal),
				onStart(onStartSignal) {
			}
			std::vector<NavigationAgent::SharedRecieverType> agentPassthroughSignals;
			std::shared_ptr<PathMap> map;
			std::shared_ptr<NavigationAgent> agent;
		};
	}
}

CEREAL_CLASS_VERSION(MV::Scene::PathAgent, 1);
CEREAL_CLASS_VERSION(MV::Scene::PathMap, 1);

#endif
