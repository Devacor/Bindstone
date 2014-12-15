#include "grid.h"
#include <memory>
#include "cereal/archives/json.hpp"
CEREAL_REGISTER_TYPE(MV::Scene::Grid);

namespace MV {
	namespace Scene {


		void Grid::update(double a_delta) {
			if (dirtyGrid) {
				layoutCells();
			}
		}

		std::shared_ptr<Grid> Grid::padding(const std::pair<Point<>, Point<>> &a_padding) {
			cellPadding = a_padding;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::shared_ptr<Grid> Grid::padding(const Size<> &a_padding) {
			cellPadding.first = toPoint(a_padding);
			cellPadding.second = toPoint(a_padding);
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::pair<Point<>, Point<>> Grid::padding() const {
			return cellPadding;
		}

		std::shared_ptr<Grid> Grid::margin(const std::pair<Point<>, Point<>> &a_margin) {
			margins = a_margin;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::shared_ptr<Grid> Grid::margin(const Size<> &a_margin) {
			margins.first = toPoint(a_margin);
			margins.second = toPoint(a_margin);
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::shared_ptr<Grid> Grid::cellSize(const Size<> &a_size) {
			cellDimensions = a_size;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::shared_ptr<Grid> Grid::gridWidth(PointPrecision a_rowWidth) {
			maximumWidth = a_rowWidth;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::shared_ptr<Grid> Grid::columns(size_t a_columns, bool a_useChildrenForSize) {
			cellColumns = a_columns;
			includeChildrenInChildSize = a_useChildrenForSize;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		void Grid::layoutCells() {
			dirtyGrid = false;
			if (cellDimensions.width > 0.0f || cellDimensions.height > 0.0f) {
				layoutCellSize();
			} else {
				layoutChildSize();
			}
		}

		Grid::Grid(const std::weak_ptr<Node> &a_owner) :
			Drawable(a_owner),
			maximumWidth(0.0f),
			cellColumns(0),
			dirtyGrid(true) {
			points.resize(4);
			clearTexturePoints(points);
			appendQuadVertexIndices(vertexIndices, 0);
			observeNode(owner());
		}

		bool Grid::bumpToNextLine(PointPrecision a_currentPosition, size_t a_index) const {
			return a_index > 0 &&
				(maximumWidth > 0.0f && a_currentPosition > maximumWidth) ||
				(cellColumns > 0 && ((a_index + 1) >= cellColumns && (a_index % cellColumns == 0)));
		}

		float Grid::getContentWidth() const {
			if (maximumWidth > 0.0f) {
				return maximumWidth;
			} else {
				return (cellColumns * (cellDimensions.width + cellPadding.first.x + cellPadding.second.x) - cellPadding.first.x - cellPadding.second.x) + margins.first.x + margins.second.x;
			}
		}

		void Grid::setPointsFromBounds(const BoxAABB<> &a_bounds) {
			points[0] = a_bounds.minPoint;
			points[1].x = a_bounds.minPoint.x;	points[1].y = a_bounds.maxPoint.y;	points[1].z = (a_bounds.maxPoint.z + a_bounds.minPoint.z) / 2.0f;
			points[2] = a_bounds.maxPoint;
			points[3].x = a_bounds.maxPoint.x;	points[3].y = a_bounds.minPoint.y;	points[3].z = points[1].z;
		}

		void Grid::layoutCellSize() {
			BoxAABB<> calculatedBounds(size(getContentWidth() - margins.second.x, cellDimensions.height + margins.first.y));
			Point<> cellPosition = margins.first;
			size_t index = 0;

			PointPrecision lineHeight = 0.0f;
			for (auto&& node : *owner()) {
				if (node->visible()) {
					cellPosition = positionChildNode(cellPosition, index, node, cellDimensions, calculatedBounds, lineHeight);
					++index;
				}
			}

			calculatedBounds.expandWith(calculatedBounds.maxPoint + margins.second);

			setPointsFromBounds(calculatedBounds);
			if (localBounds != calculatedBounds) {
				localBounds = calculatedBounds;
				notifyParentOfBoundsChange();
			}
		}


		Point<> Grid::positionChildNode(Point<> a_cellPosition, size_t a_index, std::shared_ptr<Node> a_node, const Size<> &a_cellSize, BoxAABB<> &a_calculatedBounds, PointPrecision &a_lineHeight) {
			auto nextPosition = a_cellPosition;
			nextPosition.translate(a_cellSize.width + (cellPadding.first + cellPadding.second).x, 0.0f);
			if (bumpToNextLine(nextPosition.x - cellPadding.first.x - margins.first.x, a_index)) {
				a_cellPosition.locate(margins.first.x, a_cellPosition.y + a_lineHeight + cellPadding.first.x + cellPadding.second.y);
				nextPosition = a_cellPosition;
				nextPosition.translate(a_cellSize.width + (cellPadding.first + cellPadding.second).x, 0.0f);
				a_lineHeight = a_cellSize.height;
			} else if(a_lineHeight < a_cellSize.height) {
				a_lineHeight = a_cellSize.height;
			}

			a_node->position(a_cellPosition);
			a_calculatedBounds.expandWith(a_cellPosition + toPoint(a_cellSize));

			return nextPosition;
		}


		void Grid::layoutChildSize() {
			BoxAABB<> calculatedBounds(size(getContentWidth() - margins.second.x, cellDimensions.height + margins.first.y));
			Point<> cellPosition = margins.first;
			size_t index = 0;

			PointPrecision maxHeightInLine = 0.0f;

			PointPrecision lineHeight = 0.0f;
			for (auto&& node : *owner()) {
				if (node->visible()) {
					auto nodeBounds = node->bounds(includeChildrenInChildSize);
					auto shapeSize = nodeBounds.size();
					cellPosition = positionChildNode(cellPosition, index, node, shapeSize, calculatedBounds, lineHeight);
					node->translate(nodeBounds.minPoint * -1.0f);
					++index;
				}
			}

			calculatedBounds.expandWith(calculatedBounds.maxPoint + margins.second);

			setPointsFromBounds(calculatedBounds);
			if (localBounds != calculatedBounds) {
				localBounds = calculatedBounds;
				notifyParentOfBoundsChange();
			}
		}

		void Grid::observeNode(const std::shared_ptr<Node>& a_node) {
			auto markDirty = [&](const std::shared_ptr<Node> &a_this) {
				dirtyGrid = true;
			};
			basicSignals.push_back(a_node->onChildAdd.connect(markDirty));
			basicSignals.push_back(a_node->onDepthChange.connect(markDirty));
			basicSignals.push_back(a_node->onShow.connect(markDirty));
			basicSignals.push_back(a_node->onHide.connect(markDirty));
			basicSignals.push_back(a_node->onTransformChange.connect(markDirty));
			basicSignals.push_back(a_node->onChildBoundsChange.connect(markDirty));

			basicSignals.push_back(a_node->onBoundsRequest.connect([&](const std::shared_ptr<Node> &a_this){
				if (dirtyGrid) {
					layoutCells();
				}
			}));
		}

		BoxAABB<> Grid::boundsImplementation() {
			if (dirtyGrid) {
				layoutCells();
			}
			return Drawable::boundsImplementation();
		}

	}
}