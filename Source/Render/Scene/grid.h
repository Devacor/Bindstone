#ifndef _MV_SCENE_GRID_H_
#define _MV_SCENE_GRID_H_

#include "drawable.h"
#include "cereal/types/utility.hpp"

namespace MV {
	namespace Scene {
		class Grid : public Drawable {
			friend Node;
			friend cereal::access;

		public:
			DrawableDerivedAccessors(Grid)

			virtual void update(double a_delta) override;

			std::shared_ptr<Grid> padding(const std::pair<Point<>, Point<>> &a_padding);
			std::shared_ptr<Grid> padding(const Size<> &a_padding);
			std::pair<Point<>, Point<>> padding() const;

			std::shared_ptr<Grid> margin(const std::pair<Point<>, Point<>> &a_margin);
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

			std::shared_ptr<Grid> columns(size_t a_columns);
			size_t columns() const {
				return cellColumns;
			}

			//unlikely you'll need to call this directly
			void layoutCells();

		protected:
			Grid(const std::weak_ptr<Node> &a_owner);

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					CEREAL_NVP(maximumWidth),
					CEREAL_NVP(cellDimensions),
					CEREAL_NVP(cellPadding),
					CEREAL_NVP(margins),
					CEREAL_NVP(cellColumns),
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
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->dirtyGrid = true;
			}

		private:

			bool bumpToNextLine(PointPrecision a_currentPosition, size_t a_index) const;

			float getContentWidth() const;

			void setPointsFromBounds(const BoxAABB<> &a_bounds);

			void layoutCellSize();

			Point<> positionChildNode(Point<> a_cellPosition, size_t a_index, std::shared_ptr<Node> a_node, const Size<> &a_cellSize, BoxAABB<> &a_calculatedBounds);

			void layoutChildSize();

			void observeNode(const std::shared_ptr<Node>& a_node);

			std::list<Node::BasicSharedSignalType> basicSignals;

			PointPrecision maximumWidth;
			Size<> cellDimensions;
			std::pair<Point<>, Point<>> cellPadding;
			std::pair<Point<>, Point<>> margins;
			size_t cellColumns;
			bool dirtyGrid;
		};
	}
}

#endif
