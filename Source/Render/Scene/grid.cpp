#include "grid.h"
#include <memory>
#include "cereal/archives/json.hpp"
CEREAL_REGISTER_TYPE(MV::Scene::Grid);

namespace MV {
	namespace Scene {

		void Grid::updateImplementation(double a_delta) {
			if (dirtyGrid) {
				layoutCells();
			}
		}

		std::shared_ptr<Grid> Grid::padding(const std::pair<Point<>, Point<>> &a_padding) {
			cellPadding = a_padding;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::shared_ptr<Grid> Grid::padding(const Point<> &a_topLeft, const Point<> &a_botRight) {
			return padding(std::pair<Point<>, Point<>>(a_topLeft, a_botRight));
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

		std::shared_ptr<Grid> Grid::margin(const Point<> &a_topLeft, const Point<> &a_botRight) {
			return margin(std::pair<Point<>, Point<>>(a_topLeft, a_botRight));
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

		std::shared_ptr<Node> Grid::nodeFromGrid(const Point<int> &a_coordinate, bool a_throwOnFail /*= true*/) {
			if (dirtyGrid) {
				layoutCells();
			}
			if (a_coordinate.y >= 0 && tiles.size() < a_coordinate.y) {
				if (a_coordinate.x >= 0 && tiles[a_coordinate.y].size() < a_coordinate.x) {
					auto result = tiles[a_coordinate.y][a_coordinate.x];
					if (!result.expired()) {
						return result.lock();
					}
				}
			}
			require<RangeException>(!a_throwOnFail, "Failed to load coordinate from grid at: ", a_coordinate, " It is either expired or out of range.");
			return nullptr;
		}

		std::shared_ptr<Node> Grid::nodeFromLocal(const Point<> &a_coordinate, bool a_throwOnFail /*= true*/) {
			if (dirtyGrid) {
				layoutCells();
			}
			auto tile = gridTileForYIndexAndPosition(gridYIndexForCoordinate(a_coordinate, a_throwOnFail), a_coordinate, a_throwOnFail);
			return tile;
		}

		void Grid::layoutCells() {
			allowDirty = false;
			SCOPE_EXIT{ allowDirty = true; };
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
			tiles.clear();
			tiles.push_back({});

			PointPrecision lineHeight = 0.0f;
			for (auto&& node : *owner()) {
				if (node->visible()) {
					cellPosition = positionChildNode(cellPosition, index, node, cellDimensions, calculatedBounds, lineHeight);
					++index;
				}
			}

			calculatedBounds = calculatedBounds.expandWith(calculatedBounds.maxPoint + margins.second);

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
				tiles.push_back({});
			} else if(a_lineHeight < a_cellSize.height) {
				a_lineHeight = a_cellSize.height;
			}

			a_node->position(a_cellPosition);
			a_calculatedBounds.expandWith(a_cellPosition + toPoint(a_cellSize));

			tiles[tiles.size() - 1].push_back(a_node);
			return nextPosition;
		}


		void Grid::layoutChildSize() {
			BoxAABB<> calculatedBounds(size(getContentWidth() - margins.second.x, cellDimensions.height + margins.first.y));
			Point<> cellPosition = margins.first;
			size_t index = 0;
			tiles.clear();
			tiles.push_back({});

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

			calculatedBounds = calculatedBounds.expandWith(calculatedBounds.maxPoint + margins.second);

			setPointsFromBounds(calculatedBounds);
			if (localBounds != calculatedBounds) {
				localBounds = calculatedBounds;
				notifyParentOfBoundsChange();
			}
		}

		void Grid::observeOwner(const std::shared_ptr<Node>& a_node) {
			basicSignals.push_back(a_node->onChildAdd.connect([&](const std::shared_ptr<Node> &a_this) {
				if (a_this->id() == "Background") {
					std::cout << "Background child add!" << std::endl;
				}
				dirtyGrid = true;
				observeChildNode(a_this);
			}));
			basicSignals.push_back(a_node->onBoundsRequest.connect([&](const std::shared_ptr<Node> &a_this){
				if (dirtyGrid) {
					layoutCells();
				}
			}));
			parentInteractionSignals.push_back(a_node->onChildRemove.connect([&](const std::shared_ptr<Node> &a_parent, const std::shared_ptr<Node> &a_child) {
				dirtyGrid = true;
			}));
			if (owner()->id() == "Background") {
				std::cout << "Background register!" << std::endl;
			}
			for (auto&& child : *a_node) {
				observeChildNode(child);
			}
		}

		void Grid::observeChildNode(const std::shared_ptr<Node>& a_node) {
			auto markDirty = [&](const std::shared_ptr<Node> &a_this) {
				if (allowDirty) {
					dirtyGrid = true;
				}
			};
			auto& childSignalBucket = childSignals[a_node];
			childSignalBucket.push_back(a_node->onOrderChange.connect(markDirty));
			childSignalBucket.push_back(a_node->onShow.connect(markDirty));
			childSignalBucket.push_back(a_node->onHide.connect(markDirty));
			childSignalBucket.push_back(a_node->onTransformChange.connect(markDirty));
			childSignalBucket.push_back(a_node->onLocalBoundsChange.connect(markDirty));
			childSignalBucket.push_back(a_node->onRemove.connect([&](const std::shared_ptr<Node> &a_this){
				auto found = childSignals.find(a_this);
				if (found != childSignals.end()) {
					childSignals.erase(found);
				}
			}));
		}

		std::shared_ptr<Node> Grid::gridTileForYIndexAndPosition(int yIndex, const Point<> &a_coordinate, bool a_throwOnFail) {
			if (yIndex >= 0) {
				bool foundLessX = false;
				for (int x = 0; x < tiles[yIndex].size(); ++x) {
					if (!tiles[yIndex][x].expired()) {
						auto tile = tiles[yIndex][x].lock();
						if (equals(tile->position().x, a_coordinate.x)) {
							return tile;
						} else if (tile->position().x < a_coordinate.x) {
							foundLessX = true;
						} else if (foundLessX && tile->position().x > a_coordinate.x) {
							return tile;
						} else if (tile->position().x > a_coordinate.x) {
							break;
						}
					}
				}
			}
			require<RangeException>(!a_throwOnFail, "Failed to load a grid position at: ", a_coordinate, " x is out of range, but y was okay");
			return nullptr;
		}

		int Grid::gridYIndexForCoordinate(const Point<> &a_coordinate, bool a_throwOnFail) {
			bool foundLessY = false;
			for (int y = 0; y < tiles.size(); ++y) {
				if (!tiles[y].empty() && !tiles[y][0].expired()) {
					auto tile = tiles[y][0].lock();
					if (equals(tile->position().y, a_coordinate.y)) {
						return y;
					} else if (tile->position().y < a_coordinate.y) {
						foundLessY = true;
					} else if (foundLessY && tile->position().y > a_coordinate.y) {
						return y;
					} else if (tile->position().y > a_coordinate.y) {
						break;
					}
				}
			}
			require<RangeException>(!a_throwOnFail, "Failed to load a grid position at: ", a_coordinate, " y is out of range");
			return -1;
		}

		BoxAABB<> Grid::boundsImplementation() {
			if (dirtyGrid) {
				layoutCells();
			}
			return Drawable::boundsImplementation();
		}

		void Grid::initialize() {
			Drawable::initialize();
			observeOwner(owner());
		}

		std::shared_ptr<Component> Grid::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Drawable::cloneHelper(a_clone);
			auto gridClone = std::static_pointer_cast<Grid>(a_clone);
			gridClone->maximumWidth = maximumWidth;
			gridClone->cellDimensions = cellDimensions;
			gridClone->margins = margins;
			gridClone->cellPadding = cellPadding;
			gridClone->cellColumns = cellColumns;
			gridClone->includeChildrenInChildSize = includeChildrenInChildSize;
			gridClone->dirtyGrid = true;
			return a_clone;
		}

	}
}