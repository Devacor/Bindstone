#ifndef _MV_SCENE_MAP_H_
#define _MV_SCENE_MAP_H_

#include "node.h"
#include "ArtificialIntelligence/pathfinding.h"

namespace MV {
	namespace Scene {
		class PathMap : public Component {
			friend Node;
			friend cereal::access;

		public:
			ComponentDerivedAccessors(PathMap)

			virtual void update(double a_delta) override;

			std::shared_ptr<PathMap> cellSize(const Size<> &a_size) {
				cellDimensions = a_size;
			}

			Size<> cellSize() const {
				return cellDimensions;
			}

			std::shared_ptr<PathMap> size(const Size<int> &a_gridSize, bool a_useCorners = true) {
				map = Map::make(a_gridSize, a_useCorners);
			}
			Size<int> size() const {
				return map->size();
			}

		protected:
			PathMap(const std::weak_ptr<Node> &a_owner):
				Component(a_owner){
			}

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					cereal::make_nvp("Component", cereal::base_class<Component>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<PathMap> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual void initialize() override;

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<PathMap>().self());
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
