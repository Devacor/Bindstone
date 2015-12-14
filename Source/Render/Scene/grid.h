#ifndef _MV_SCENE_GRID_H_
#define _MV_SCENE_GRID_H_

#include "drawable.h"
#include "cereal/types/utility.hpp"
#include <vector>

namespace MV {
	namespace Scene {
		class Grid : public Drawable {
			friend Node;
			friend cereal::access;

		public:
			DrawableDerivedAccessors(Grid)

			std::shared_ptr<Grid> padding(const std::pair<Point<>, Point<>> &a_padding);
			std::shared_ptr<Grid> padding(const Point<> &a_topLeft, const Point<> &a_botRight);
			std::shared_ptr<Grid> padding(const Size<> &a_padding);
			std::pair<Point<>, Point<>> padding() const;

			std::shared_ptr<Grid> margin(const std::pair<Point<>, Point<>> &a_margin);
			std::shared_ptr<Grid> margin(const Point<> &a_topLeft, const Point<> &a_botRight);
			std::shared_ptr<Grid> margin(const Size<> &a_margin);
			std::pair<Point<>, Point<>> margin() const {
				return margins;
			}

			std::shared_ptr<Grid> cellSize(const Size<> &a_size);
			Size<> cellSize() const {
				return cellDimensions;
			}

			std::shared_ptr<Grid> gridWidth(PointPrecision a_rowWidth);
			PointPrecision gridWidth() const {
				return maximumWidth;
			}

			std::shared_ptr<Grid> columns(size_t a_columns, bool a_useChildrenForSize = true);
			size_t columns() const {
				return cellColumns;
			}

			std::shared_ptr<Node> nodeFromGrid(const Point<int> &a_coordinate, bool a_throwOnFail = true);
			std::shared_ptr<Node> nodeFromLocal(const Point<> &a_coordinate, bool a_throwOnFail = true);

			//unlikely you'll need to call this directly unless set to manual reposition
			void layoutCells();

			void repositionManual() {
				manualReposition = true;
			}
			void repositionAutomatic() {
				manualReposition = false;
			}

		protected:
			Grid(const std::weak_ptr<Node> &a_owner);

			virtual void updateImplementation(double a_delta) override;

			template <class Archive>
			void serialize(Archive & archive) {
				if (dirtyGrid) {
					layoutCells();
				}
				archive(
					CEREAL_NVP(maximumWidth),
					CEREAL_NVP(cellDimensions),
					CEREAL_NVP(cellPadding),
					CEREAL_NVP(margins),
					CEREAL_NVP(cellColumns),
					CEREAL_NVP(includeChildrenInChildSize),
					CEREAL_NVP(manualReposition),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Grid> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("maximumWidth", construct->maximumWidth),
					cereal::make_nvp("cellDimensions", construct->cellDimensions),
					cereal::make_nvp("cellPadding", construct->cellPadding),
					cereal::make_nvp("margins", construct->margins),
					cereal::make_nvp("cellColumns", construct->cellColumns),
					cereal::make_nvp("includeChildrenInChildSize", construct->includeChildrenInChildSize),
					cereal::make_nvp("manualReposition", construct->manualReposition),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->dirtyGrid = false;
				construct->initialize();
			}

			virtual void initialize() override;

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Grid>().self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			virtual BoxAABB<> boundsImplementation() override;

			bool bumpToNextLine(PointPrecision a_currentPosition, size_t a_index) const;

			float getContentWidth() const;

			void setPointsFromBounds(const BoxAABB<> &a_bounds);

			void layoutCellSize();

			Point<> positionChildNode(Point<> a_cellPosition, size_t a_index, std::shared_ptr<Node> a_node, const Size<> &a_cellSize, BoxAABB<> &a_calculatedBounds, PointPrecision &a_lineHeight);

			void layoutChildSize();

			void observeOwner(const std::shared_ptr<Node>& a_node);
			void observeChildNode(const std::shared_ptr<Node>& a_node);

			std::shared_ptr<Node> gridTileForYIndexAndPosition(int yIndex, const Point<> &a_coordinate, bool a_throwOnFail);

			int gridYIndexForCoordinate(const Point<> &a_coordinate, bool a_throwOnFail);

			std::list<Node::BasicSharedSignalType> basicSignals;
			std::list<Node::ParentInteractionSharedSignalType> parentInteractionSignals;

			std::map<std::shared_ptr<Node>, std::list<Node::BasicSharedSignalType>> childSignals;

			PointPrecision maximumWidth;
			Size<> cellDimensions;
			std::pair<Point<>, Point<>> cellPadding;
			std::pair<Point<>, Point<>> margins;
			size_t cellColumns;
			bool allowDirty = true;
			bool dirtyGrid;
			bool includeChildrenInChildSize = true;
			bool manualReposition = false;

			std::vector<std::vector<std::weak_ptr<MV::Scene::Node>>> tiles;
		};
	}
}

CEREAL_CLASS_VERSION(MV::Scene::Grid, 1)

#endif
