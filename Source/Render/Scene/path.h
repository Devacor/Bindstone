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
				return ((cast<PointPrecision>(a_location) + point(0.5f, 0.5f)) * toPoint(cellDimensions)) + topLeftOffset;
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
			void serialize(Archive & archive) {
				std::vector<DrawPoint> tmpPoints{bounds().minPoint, bounds().maxPoint};
				std::vector<GLuint> tmpVertexIndices;

				std::swap(points, tmpPoints);
				std::swap(vertexIndices, tmpVertexIndices);

				SCOPE_EXIT{
					std::swap(points, tmpPoints);
					std::swap(vertexIndices, tmpVertexIndices);
				};

				archive(
					cereal::make_nvp("map", map),
					cereal::make_nvp("offset", topLeftOffset),
					cereal::make_nvp("cellDimensions", cellDimensions),
					cereal::make_nvp("Component", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<PathMap> &construct) {
				construct(std::shared_ptr<Node>(), Size<int>());
				archive(
					cereal::make_nvp("map", construct->map),
					cereal::make_nvp("offset", construct->topLeftOffset),
					cereal::make_nvp("cellDimensions", construct->cellDimensions),
					cereal::make_nvp("Component", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->initialize();
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

			std::shared_ptr<PathAgent> gridGoal(const Point<int> &a_newGoal, PointPrecision a_acceptableDistance = 0.0f) {
				agent->goal(a_newGoal, a_acceptableDistance);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> gridGoal(const Point<> &a_newGoal, PointPrecision a_acceptableDistance = 0.0f) {
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

		protected:
			PathAgent(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<PathMap> &a_map, const Point<> &a_gridPosition) :
				Component(a_owner),
				map(a_map),
				agent(NavigationAgent::make(a_map->map, a_gridPosition)) {
			}

			PathAgent(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<PathMap> &a_map, const Point<int> &a_gridPosition) :
				Component(a_owner),
				map(a_map),
				agent(NavigationAgent::make(a_map->map, a_gridPosition)) {
			}

			virtual void updateImplementation(double a_dt) override {
				agent->update(a_dt);
				auto ourOwner = owner();
				if (ourOwner->parent() == map->owner()) {
					ourOwner->position(map->localFromGrid(agent->position()));
				} else {
					ourOwner->worldPosition(map->owner()->worldFromLocal(map->localFromGrid(agent->position())));
				}
			}

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					cereal::make_nvp("map", map),
					cereal::make_nvp("agent", agent),
					cereal::make_nvp("Component", cereal::base_class<Component>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<PathAgent> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("map", construct->map),
					cereal::make_nvp("agent", construct->agent),
					cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
				);
				construct->initialize();
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
				agent(nullptr) {
			}
			std::shared_ptr<PathMap> map;
			std::shared_ptr<NavigationAgent> agent;
		};
	}
}

#endif
