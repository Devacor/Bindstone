#ifndef _MV_SCENE_GRID_H_
#define _MV_SCENE_GRID_H_

#include "drawable.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <vector>

namespace MV {
	namespace Scene {
		class Grid : public jai::property_owner<Grid, Drawable> {
			friend Node;
			friend jai::access;

		public:
			enum class AutoLayoutPolicy {None, Self, Local, Child, Comprehensive};

			DrawableDerivedAccessors(Grid)

			std::shared_ptr<Grid> padding(const std::pair<Point<>, Point<>> &a_padding);
			std::shared_ptr<Grid> padding(const Point<> &a_topLeft, const Point<> &a_botRight);
			std::shared_ptr<Grid> padding(const Size<> &a_padding);
			std::pair<Point<>, Point<>> padding() const;

			std::shared_ptr<Grid> margin(const std::pair<Point<>, Point<>> &a_margin);
			std::shared_ptr<Grid> margin(const Point<> &a_topLeft, const Point<> &a_botRight);
			std::shared_ptr<Grid> margin(const Size<> &a_margin);
			std::pair<Point<>, Point<>> margin() const {
				return margins.get();
			}

			std::shared_ptr<Grid> cellSize(const Size<> &a_size);
			Size<> cellSize() const {
				return cellDimensions.get();
			}

			std::shared_ptr<Grid> gridWidth(PointPrecision a_rowWidth);
			PointPrecision gridWidth() const {
				return maximumWidth.get();
			}

			std::shared_ptr<Grid> gridOffset(const Point<> &a_topLeftOffset);
			Point<> gridOffset() const {
				return topLeftOffset.get();
			}

			std::shared_ptr<Grid> columns(size_t a_columns, bool a_useChildrenForSize = true);
			size_t columns() const {
				return cellColumns.get();
			}

			std::shared_ptr<Node> nodeFromGrid(const Point<int> &a_coordinate, bool a_throwOnFail = true);
			std::shared_ptr<Node> nodeFromLocal(const Point<> &a_coordinate, bool a_throwOnFail = true);

			//unlikely you'll need to call this directly unless set to manual reposition
			void layoutCells();

			AutoLayoutPolicy layoutPolicy() const {
				return policy.get();
			}
			std::shared_ptr<Grid> layoutPolicy(AutoLayoutPolicy a_policy) {
				policy = a_policy;
				return std::static_pointer_cast<Grid>(shared_from_this());
			}
		protected:
			Grid(const std::weak_ptr<Node> &a_owner);

			virtual void detachImplementation() override;
			virtual void reattachImplementation() override;

			virtual void updateImplementation(double a_delta) override;

			template<class Archive>
			void save(Archive & archive, std::uint32_t const a_version) const {
				if (dirtyGrid) {
					const_cast<Grid*>(this)->layoutCells();
				}
				property_mgr.save(archive);
				Drawable::save(archive, 0);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const a_version) {
				property_mgr.load(archive);
				Drawable::load(archive, 0);
				dirtyGrid = false;
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Grid>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			virtual void initialize() override;

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Grid>().self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			virtual void boundsImplementation(const BoxAABB<> &a_bounds) override;
			virtual BoxAABB<> boundsImplementation() override;

			bool bumpToNextLine(PointPrecision a_currentPosition, size_t a_index) const;

			float getContentWidth() const;

			void setPointsFromBounds(const BoxAABB<> &a_bounds);

			bool layoutCellSize();

			Point<> positionChildNode(Point<> a_cellPosition, size_t a_index, std::shared_ptr<Node> a_node, const Size<> &a_cellSize, BoxAABB<> &a_calculatedBounds, PointPrecision &a_lineHeight);

			bool layoutChildSize();

			void observeOwner(const std::shared_ptr<Node>& a_node);

			std::shared_ptr<Node> gridTileForYIndexAndPosition(int yIndex, const Point<> &a_coordinate, bool a_throwOnFail);

			int gridYIndexForCoordinate(const Point<> &a_coordinate, bool a_throwOnFail);

			std::list<Node::BasicReceiverType> basicSignals;
			std::list<Node::ParentInteractionReceiverType> parentInteractionSignals;

			JAI_PROPERTY((PointPrecision), maximumWidth, 0.0f);
			JAI_PROPERTY((Size<>), cellDimensions);
			JAI_PROPERTY((std::pair<Point<>, Point<>>), cellPadding);
			JAI_PROPERTY((std::pair<Point<>, Point<>>), margins);
			JAI_PROPERTY((Point<>), topLeftOffset);
			JAI_PROPERTY((size_t), cellColumns, 0);
			bool inLayoutCall = false;
			bool dirtyGrid = true;
			JAI_PROPERTY((bool), includeChildrenInChildSize, true);
			JAI_PROPERTY((AutoLayoutPolicy), policy, AutoLayoutPolicy::Comprehensive);
			JAI_DELETED_PROPERTY((bool), manualReposition);

			std::vector<std::vector<std::weak_ptr<MV::Scene::Node>>> tiles;
		};
	}
}

#endif
