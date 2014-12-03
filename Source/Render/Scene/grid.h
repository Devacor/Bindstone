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

			std::shared_ptr<Grid> padding(const std::pair<Point<>, Point<>> &a_padding) {
				cellPadding = a_padding;
				return std::static_pointer_cast<Grid>(shared_from_this());
			}
			std::shared_ptr<Grid> padding(const Size<> &a_padding) {
				cellPadding.first = toPoint(a_padding);
				cellPadding.second = toPoint(a_padding);
				return std::static_pointer_cast<Grid>(shared_from_this());
			}
			std::pair<Point<>, Point<>> padding() const {
				return cellPadding;
			}

			std::shared_ptr<Grid> margin(const std::pair<Point<>, Point<>> &a_margin) {
				margins = a_margin;
				return std::static_pointer_cast<Grid>(shared_from_this());
			}
			std::shared_ptr<Grid> margin(const Size<> &a_margin) {
				margins.first = toPoint(a_margin);
				margins.second = toPoint(a_margin);
				return std::static_pointer_cast<Grid>(shared_from_this());
			}
			std::pair<Point<>, Point<>> margin() const {
				return margins;
			}

			std::shared_ptr<Grid> cellSize(const Size<> &a_size) {
				cellDimensions = a_size;
				return std::static_pointer_cast<Grid>(shared_from_this());
			}
			Size<> cellSize() const {
				return cellDimensions;
			}

			std::shared_ptr<Grid> rowWidth(PointPrecision a_rowWidth) {
				maximumWidth = a_rowWidth;
				return std::static_pointer_cast<Grid>(shared_from_this());
			}
			PointPrecision rowWidth() const {
				return maximumWidth;
			}

			std::shared_ptr<Grid> rows(size_t a_rows) {
				cellRows = a_rows;
				return std::static_pointer_cast<Grid>(shared_from_this());
			}
			size_t rows() const {
				return cellRows;
			}

		protected:
			Grid(const std::weak_ptr<Node> &a_owner) :
				Drawable(a_owner) {

				points.resize(4);
				clearTexturePoints(points);
				appendQuadVertexIndices(vertexIndices, 0);
			}

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					CEREAL_NVP(maximumWidth),
					CEREAL_NVP(cellDimensions),
					CEREAL_NVP(cellPadding),
					CEREAL_NVP(margins),
					CEREAL_NVP(cellRows),
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
					cereal::make_nvp("cellRows", construct->cellRows),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->dirtyGrid = true;
			}

		private:

			bool bumpToNextLine(PointPrecision a_currentPosition, size_t a_index) const {
				return (maximumWidth > 0.0f && a_currentPosition > maximumWidth) ||
					(cellRows > 0 && (a_index + 1) >= cellRows && ((a_index + 1) % cellRows == 0));
			}

			float getContentWidth() const {
				if (maximumWidth > 0.0f) {
					return maximumWidth;
				} else {
					return (cellRows * (cellDimensions.width + cellPadding.first.x + cellPadding.second.x));
				}
			}

			void setPointsFromBounds(const BoxAABB<> &a_bounds) {
				points[0] = a_bounds.minPoint;
				points[1].x = a_bounds.minPoint.x;	points[1].y = a_bounds.maxPoint.y;	points[1].z = (a_bounds.maxPoint.z + a_bounds.minPoint.z) / 2.0f;
				points[2] = a_bounds.maxPoint;
				points[3].x = a_bounds.maxPoint.x;	points[3].y = a_bounds.minPoint.y;	points[3].z = points[1].z;
			}

			void layoutCellSize() {
				BoxAABB<> calculatedBounds(size(getContentWidth(), cellDimensions.height));
				Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
				size_t index = 0;

				for (auto&& node : *owner()) {
					if (node->visible()) {
						auto nextPosition = cellPosition;
						nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
						if (bumpToNextLine(nextPosition.x - cellPadding.second.x, index++)) {
							cellPosition.locate(margins.first.x, cellPosition.y + cellDimensions.height);
							cellPosition += cellPadding.first + cellPadding.second;
							nextPosition = cellPosition;
							nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
						}

						node->position(cellPosition - cellPadding.second);
						calculatedBounds.expandWith(cellPosition + toPoint(cellDimensions) - cellPadding.second);

						cellPosition = nextPosition;
					}
				}

				calculatedBounds.expandWith(calculatedBounds.maxPoint + margins.second);
				setPointsFromBounds(calculatedBounds);
				if (localBounds != calculatedBounds) {
					localBounds = calculatedBounds;
					notifyParentOfBoundsChange();
				}
			}

			void layoutChildSize() {
				BoxAABB<> calculatedBounds(size(getContentWidth(), cellDimensions.height));
				Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
				size_t index = 0;

				PointPrecision maxHeightInLine = 0.0f;

				for (auto&& node : *owner()) {
					if (node->visible()) {
						auto shapeSize = node->bounds().size();
						auto nextPosition = cellPosition;
						nextPosition.translate(shapeSize.width + (cellPadding.first + cellPadding.second).x, 0.0f);
						if (bumpToNextLine(nextPosition.x - cellPadding.second.x, index++)) {
							cellPosition.locate(margins.first.x, cellPosition.y + maxHeightInLine);
							cellPosition += cellPadding.first + cellPadding.second;
							maxHeightInLine = 0.0f;
							nextPosition = cellPosition;
							nextPosition.translate(shapeSize.width + (cellPadding.first + cellPadding.second).x, 0.0f);
						}

						maxHeightInLine = std::max(shapeSize.height, maxHeightInLine);
						node->position(cellPosition - cellPadding.second);
						calculatedBounds.expandWith(cellPosition + toPoint(shapeSize) - cellPadding.second);

						cellPosition = nextPosition;
					}
				}

				calculatedBounds.expandWith(calculatedBounds.maxPoint + margins.second);
				setPointsFromBounds(calculatedBounds);
				if (localBounds != calculatedBounds) {
					localBounds = calculatedBounds;
					notifyParentOfBoundsChange();
				}
			}

			void layoutCells() {
				if (dirtyGrid) {
					if (cellDimensions.width > 0.0f || cellDimensions.height > 0.0f) {
						layoutCellSize();
					} else {
						layoutChildSize();
					}
					dirtyGrid = false;
				}
			}

			void observeNode(const std::shared_ptr<Node>& a_node) {
				auto markDirty = [&](const std::shared_ptr<Node> &a_this) {
					dirtyGrid = true;
				};
				basicSignals.push_back(a_node->onChildAdd.connect(markDirty));
				basicSignals.push_back(a_node->onDepthChange.connect(markDirty));
				basicSignals.push_back(a_node->onShow.connect(markDirty));
				basicSignals.push_back(a_node->onHide.connect(markDirty));
				basicSignals.push_back(a_node->onTransformChange.connect(markDirty));
				basicSignals.push_back(a_node->onLocalBoundsChange.connect(markDirty));

				basicSignals.push_back(nullptr);
			}

			std::list<Node::BasicSharedSignalType> basicSignals;

			PointPrecision maximumWidth;
			Size<> cellDimensions;
			std::pair<Point<>, Point<>> cellPadding;
			std::pair<Point<>, Point<>> margins;
			size_t cellRows;
			bool dirtyGrid;
		};
	}
}

#endif
