#include "grid.h"
#include <memory>
#include "cereal/archives/json.hpp"
CEREAL_REGISTER_TYPE(MV::Scene::Grid);

namespace MV {
	namespace Scene {

		/*************************\
		| ----------Grid--------- |
		\*************************/
		std::shared_ptr<Grid> Grid::make(Draw2D* a_renderer) {
			auto grid = std::shared_ptr<Grid>(new Grid(a_renderer));
			grid->registerShader();
			return grid;
		}

		std::shared_ptr<Grid> Grid::make(Draw2D* a_renderer, const Size<> &a_size) {
			auto grid = std::shared_ptr<Grid>(new Grid(a_renderer, a_size));
			grid->registerShader();
			return grid;
		}

		Grid::Grid(Draw2D *a_renderer, const Size<> &a_cellSize):
			Node(a_renderer),
			dirtyGrid(true),
			cellDimensions(a_cellSize),
			maximumWidth(0.0f),
			cellRows(0) {

			initializePoints();
		}

		Grid::Grid(Draw2D *a_renderer):
			Node(a_renderer),
			dirtyGrid(true),
			maximumWidth(0.0f),
			cellRows(0) {

			initializePoints();

		}

		void Grid::setPointsFromBounds(const BoxAABB &a_bounds){
			points[0] = a_bounds.minPoint;
			points[1].x = a_bounds.minPoint.x;	points[1].y = a_bounds.maxPoint.y;	points[1].z = (equals(a_bounds.maxPoint.z, 0.0f) && equals(a_bounds.minPoint.z, 0.0f)) ? 0.0f : (a_bounds.maxPoint.z + a_bounds.minPoint.z) / 2.0f;
			points[2] = a_bounds.maxPoint;
			points[3].x = a_bounds.maxPoint.x;	points[3].y = a_bounds.minPoint.y;	points[3].z = points[1].z;
		}

		bool Grid::bumpToNextLine(PointPrecision a_currentPosition, size_t a_index) const{
			return (maximumWidth > 0.0f && a_currentPosition > maximumWidth) ||
				(cellRows > 0 && (a_index+1) >= cellRows && ((a_index+1) % cellRows == 0));
		}

		float Grid::getContentWidth() const{
			if(maximumWidth > 0.0f){
				return maximumWidth;
			} else{
				return cellRows * cellDimensions.width;
			}
		}

		void Grid::layoutCellSize(){
			BoxAABB bounds(size(getContentWidth(), cellDimensions.height));
			Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
			size_t index = 0;

			drawListVector.erase(remove_if(drawListVector.begin(), drawListVector.end(), [&](DrawListVectorType::value_type &shape){
				if(!shape.expired()){
					auto nextPosition = cellPosition;
					nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
					if(bumpToNextLine(nextPosition.x - cellPadding.second.x, index++)){
						cellPosition.locate(margins.first.x, cellPosition.y + cellDimensions.height);
						cellPosition += cellPadding.first + cellPadding.second;
						nextPosition = cellPosition;
						nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
					}

					shape.lock()->position(cellPosition - cellPadding.second);
					bounds.expandWith(cellPosition + sizeToPoint(cellDimensions) - cellPadding.second);

					cellPosition = nextPosition;
					return false;
				} else{
					return true;
				}
			}), drawListVector.end());

			bounds.expandWith(bounds.maxPoint + margins.second);
			setPointsFromBounds(bounds);
		}

		void Grid::layoutChildSize(){
			BoxAABB bounds(size(getContentWidth(), cellDimensions.height));
			Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
			size_t index = 0;
			
			PointPrecision maxHeightInLine = 0.0f;

			drawListVector.erase(remove_if(drawListVector.begin(), drawListVector.end(), [&](DrawListVectorType::value_type &shape){
				if(!shape.expired()){
					auto shapeSize = shape.lock()->localAABB().size();
					auto nextPosition = cellPosition;
					nextPosition.translate(shapeSize.width + (cellPadding.first + cellPadding.second).x, 0.0f);
					if(bumpToNextLine(nextPosition.x - cellPadding.second.x, index++)){
						cellPosition.locate(margins.first.x, cellPosition.y + maxHeightInLine);
						cellPosition += cellPadding.first + cellPadding.second;
						maxHeightInLine = 0.0f;
						nextPosition = cellPosition;
						nextPosition.translate(shapeSize.width + (cellPadding.first + cellPadding.second).x, 0.0f);
					}

					maxHeightInLine = std::max(shapeSize.height, maxHeightInLine);
					shape.lock()->position(cellPosition - cellPadding.second);
					bounds.expandWith(cellPosition + sizeToPoint(shapeSize) - cellPadding.second);

					cellPosition = nextPosition;
					return false;
				} else{
					return true;
				}
			}), drawListVector.end());

			bounds.expandWith(bounds.maxPoint + margins.second);
			setPointsFromBounds(bounds);
		}

		void Grid::layoutCells(){
			if(dirtyGrid){
				sortDrawListVector();
				if(cellDimensions.width > 0.0f || cellDimensions.height > 0.0f){
					layoutCellSize();
				} else{
					layoutChildSize();
				}
				dirtyGrid = false;
			}
		}

		bool Grid::preDraw(){
			layoutCells();
			return true;
		}

		std::shared_ptr<Grid> Grid::update() {
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::shared_ptr<Grid> Grid::padding(const std::pair<Point<>, Point<>> &a_padding) {
			cellPadding = a_padding;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::shared_ptr<Grid> Grid::padding(const Size<> &a_padding) {
			cellPadding.first = sizeToPoint(a_padding);
			cellPadding.second = sizeToPoint(a_padding);
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::pair<Point<>, Point<>> Grid::padding() const {
			return cellPadding;
		}

		std::shared_ptr<Grid> Grid::margin(const std::pair<Point<>, Point<>> &a_margin){
			margins = a_margin;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::shared_ptr<Grid> Grid::margin(const Size<> &a_margin){
			margins.first = sizeToPoint(a_margin);
			margins.second = sizeToPoint(a_margin);
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		std::pair<Point<>, Point<>> Grid::margin() const{
			return margins;
		}

		std::shared_ptr<Grid> Grid::rowWidth(PointPrecision a_rowWidth) {
			maximumWidth = a_rowWidth;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		MV::PointPrecision Grid::rowWidth() const {
			return maximumWidth;
		}

		std::shared_ptr<Grid> Grid::rows(size_t a_rows) {
			cellRows = a_rows;
			return std::static_pointer_cast<Grid>(shared_from_this());
		}

		size_t Grid::rows() const {
			return cellRows;
		}

		MV::BoxAABB Grid::worldAABBImplementation(bool a_includeChildren, bool a_nestedCall) {
			layoutCells();
			return Node::worldAABBImplementation(a_includeChildren, a_nestedCall);
		}

		MV::BoxAABB Grid::screenAABBImplementation(bool a_includeChildren, bool a_nestedCall) {
			layoutCells();
			return Node::screenAABBImplementation(a_includeChildren, a_nestedCall);
		}

		MV::BoxAABB Grid::localAABBImplementation(bool a_includeChildren, bool a_nestedCall) {
			layoutCells();
			return Node::localAABBImplementation(a_includeChildren, a_nestedCall);
		}

		void Grid::clearTextureCoordinates() {
			points[0].textureX = 0.0f; points[0].textureY = 0.0f;
			points[1].textureX = 0.0f; points[1].textureY = 1.0f;
			points[2].textureX = 1.0f; points[2].textureY = 1.0f;
			points[3].textureX = 1.0f; points[3].textureY = 0.0f;
			alertParent(VisualChange::make(shared_from_this(), false));
		}

		void Grid::updateTextureCoordinates() {
			if(ourTexture != nullptr){
				points[0].textureX = static_cast<PointPrecision>(ourTexture->percentLeft()); points[0].textureY = static_cast<PointPrecision>(ourTexture->percentTop());
				points[1].textureX = static_cast<PointPrecision>(ourTexture->percentLeft()); points[1].textureY = static_cast<PointPrecision>(ourTexture->percentBottom());
				points[2].textureX = static_cast<PointPrecision>(ourTexture->percentRight()); points[2].textureY = static_cast<PointPrecision>(ourTexture->percentBottom());
				points[3].textureX = static_cast<PointPrecision>(ourTexture->percentRight()); points[3].textureY = static_cast<PointPrecision>(ourTexture->percentTop());
				alertParent(VisualChange::make(shared_from_this(), false));
			} else {
				clearTextureCoordinates();
			}
		}

		void Grid::drawImplementation(){
			defaultDraw(GL_TRIANGLES);
		}

		BoxAABB Grid::calculateBasicAABBFromDimensions(DrawListVectorType &a_drawListVector) const{
			BoxAABB bounds(size(getContentWidth(), cellDimensions.height));
			Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
			size_t index = 0;

			std::for_each(a_drawListVector.begin(), a_drawListVector.end(), [&](DrawListVectorType::value_type &shape){
				if(!shape.expired()){
					auto nextPosition = cellPosition;
					nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
					if(bumpToNextLine(nextPosition.x - cellPadding.second.x, index++)){
						cellPosition.locate(margins.first.x, cellPosition.y + cellDimensions.height);
						cellPosition += cellPadding.first + cellPadding.second;
						nextPosition = cellPosition;
						nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
					}

					bounds.expandWith(cellPosition + sizeToPoint(cellDimensions) - cellPadding.second);

					cellPosition = nextPosition;
					return false;
				} else{
					return true;
				}
			});

			bounds.expandWith(bounds.maxPoint + margins.second);
			return bounds;
		}

		BoxAABB Grid::calculateBasicAABBFromCells(DrawListVectorType &a_drawListVector) const{
			BoxAABB bounds(size(getContentWidth(), cellDimensions.height));
			Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
			size_t index = 0;

			PointPrecision maxHeightInLine = 0.0f;

			std::for_each(a_drawListVector.begin(), a_drawListVector.end(), [&](DrawListVectorType::value_type &shape){
				if(!shape.expired()){
					auto shapeSize = shape.lock()->localAABB().size();
					auto nextPosition = cellPosition;
					nextPosition.translate(shapeSize.width + (cellPadding.first + cellPadding.second).x, 0.0f);
					if(bumpToNextLine(nextPosition.x - cellPadding.second.x, index++)){
						cellPosition.locate(margins.first.x, cellPosition.y + maxHeightInLine);
						cellPosition += cellPadding.first + cellPadding.second;
						maxHeightInLine = 0.0f;
						nextPosition = cellPosition;
						nextPosition.translate(shapeSize.width + (cellPadding.first + cellPadding.second).x, 0.0f);
					}

					maxHeightInLine = std::max(shapeSize.height, maxHeightInLine);
					bounds.expandWith(cellPosition + sizeToPoint(shapeSize) - cellPadding.second);

					cellPosition = nextPosition;
					return false;
				} else{
					return true;
				}
			});

			bounds.expandWith(bounds.maxPoint + margins.second);
			return bounds;
		}

		MV::BoxAABB Grid::basicAABBImplementation() const {
			if(dirtyGrid){
				DrawListVectorType tmpDrawListVector;
				if(!isSorted){
					std::transform(drawList.begin(), drawList.end(), std::back_inserter(tmpDrawListVector), [](DrawListType::value_type shape){
						return shape.second;
					});
					std::sort(tmpDrawListVector.begin(), tmpDrawListVector.end(), sortFunction);
				} else{
					tmpDrawListVector = drawListVector;
				}

				if(cellDimensions.width > 0.0f || cellDimensions.height > 0.0f){
					return calculateBasicAABBFromDimensions(tmpDrawListVector);
				} else{
					return calculateBasicAABBFromCells(tmpDrawListVector);
				}
			} else{
				return BoxAABB(points[0], points[2]);
			}
		}

	}
}