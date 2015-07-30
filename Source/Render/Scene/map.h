#ifndef _MV_SCENE_MAP_H_
#define _MV_SCENE_MAP_H_

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

				std::shared_ptr<PathMap> cellSize(const Size<> &a_size) {
				cellDimensions = a_size;
				return std::static_pointer_cast<PathMap>(shared_from_this());
			}

			Size<> cellSize() const {
				return cellDimensions;
			}

			Size<int> size() const {
				return map->size();
			}

			MapNode& get(const Point<int> &a_location) {
				return map->get(a_location);
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

			bool corners() const {
				return map->corners();
			}
		protected:
			PathMap(const std::weak_ptr<Node> &a_owner, const Size<int> &a_gridSize, bool a_useCorners = true) :
				PathMap(a_owner, Size<>(1.0f, 1.0f), a_gridSize, a_useCorners) {
			}

			PathMap(const std::weak_ptr<Node> &a_owner, const Size<> &a_size, const Size<int> &a_gridSize, bool a_useCorners = true) :
				Drawable(a_owner),
				map(Map::make(a_gridSize, a_useCorners)),
				cellDimensions(a_size) {
				shouldDraw = false;

				points.resize(4 * (a_gridSize.width * a_gridSize.height));
				clearTexturePoints(points);

				for (int x = 0; x < a_gridSize.width; ++x) {
					for (int y = 0; y < a_gridSize.height; ++y) {
						appendQuadVertexIndices(vertexIndices, x * y * 4);
					}
				}
			}

			virtual void initialize() override {
				Drawable::initialize();
				repositionDebugDrawPoints();
			}

			void repositionDebugDrawPoints() {
				std::vector<Color> squareColors = { Color(0.0f, 1.0f, 0.0f, .75f), Color(0.0f, 0.0f, 1.0f, .75f) };
				std::vector<Point<>> cornerOffsets = { {0, 0}, {0, cellDimensions.height}, {cellDimensions.width, cellDimensions.height}, {cellDimensions.width, 0} };
				int squareIndex = 0;
				for (int x = 0; x < map->size().width; ++x) {
					for (int y = 0; y < map->size().height; ++y) {
						int index = x * y;
						for (int i = 0; i < 4; ++i) {
							points[index + i] = point(index * cellDimensions.width, index * cellDimensions.height) + cornerOffsets[i] + topLeftOffset;
							points[index + i] = squareColors[squareIndex];
						}
						squareIndex = !squareIndex;
					}
				}
				refreshBounds();
			}

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					cereal::make_nvp("map", map),
					cereal::make_nvp("cellDimensions", cellDimensions),
					cereal::make_nvp("Component", cereal::base_class<Drawable>(this))
					);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<PathMap> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("map", map),
					cereal::make_nvp("cellDimensions", cellDimensions),
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
			std::shared_ptr<Map> map;
			MV::Point<> topLeftOffset;
			MV::Size<float> cellDimensions;
		};


		class PathAgent : public Component {
			friend Node;
			friend cereal::access;
		public:
			Point<PointPrecision> gridPosition() const {
				return agent->position();
			}

			std::vector<PathNode> path() {
				return agent->path();
			}

			std::shared_ptr<PathAgent> position(const Point<int> &a_newPosition) {
				agent->position(a_newPosition);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> position(const Point<> &a_newPosition) {
				agent->position(a_newPosition);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> goal(const Point<int> &a_newGoal, PointPrecision a_acceptableDistance = 0.0f) {
				agent->goal(a_newGoal, a_acceptableDistance);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			std::shared_ptr<PathAgent> goal(const Point<> &a_newGoal, PointPrecision a_acceptableDistance = 0.0f) {
				agent->goal(a_newGoal, a_acceptableDistance);
				return std::static_pointer_cast<PathAgent>(shared_from_this());
			}

			Point<PointPrecision> goal() const {
				return agent->goal();
			}

			PointPrecision speed() const {
				return agent->speed();
			}

			std::shared_ptr<PathAgent> speed(PointPrecision a_newSpeed) {
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
				owner()->position();
			}

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					cereal::make_nvp("map", map),
					cereal::make_nvp("agent", agent)
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<PathMap> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("map", map),
					cereal::make_nvp("agent", agent)
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
			std::shared_ptr<PathMap> map;
			std::shared_ptr<NavigationAgent> agent;
		};
	}
}

#endif
