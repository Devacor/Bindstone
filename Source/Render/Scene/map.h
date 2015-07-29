#ifndef _MV_SCENE_MAP_H_
#define _MV_SCENE_MAP_H_

#include "drawable.h"
#include "ArtificialIntelligence/pathfinding.h"

namespace MV {
	namespace Scene {
		class PathMap : public Drawable {
			friend Node;
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

		protected:
			PathMap(const std::weak_ptr<Node> &a_owner, const Size<int> &a_gridSize, bool a_useCorners = true):
				Drawable(a_owner),
				map(Map::make(a_gridSize, a_useCorners)),
				cellDimensions(MV::size(1.0f, 1.0f)) {
				shouldDraw = false;
			}

			PathMap(const std::weak_ptr<Node> &a_owner, const Size<> &a_size, const Size<int> &a_gridSize, bool a_useCorners = true) :
				Drawable(a_owner),
				map(Map::make(a_gridSize, a_useCorners)),
				cellDimensions(a_size) {
				shouldDraw = false;
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

			virtual void initialize() override {
			}

			virtual void updateImplementation(double a_delta) override {
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<PathMap>(cellDimensions, map->size(), map->corners()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<MV::Scene::Component> &a_clone) {
				Component::cloneHelper(a_clone);
				auto creatureClone = std::static_pointer_cast<Component>(a_clone);
				return a_clone;
			}

		private:
			std::shared_ptr<Map> map;
			MV::Size<float> cellDimensions;
		};
	}
}

#endif
