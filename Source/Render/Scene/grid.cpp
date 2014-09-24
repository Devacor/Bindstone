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

		void Grid::setPointsFromBounds(const BoxAABB<> &a_bounds){
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
			BoxAABB<> bounds(size(getContentWidth(), cellDimensions.height));
			Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
			size_t index = 0;

			for(auto&& shape : drawList){
				auto nextPosition = cellPosition;
				nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
				if(bumpToNextLine(nextPosition.x - cellPadding.second.x, index++)){
					cellPosition.locate(margins.first.x, cellPosition.y + cellDimensions.height);
					cellPosition += cellPadding.first + cellPadding.second;
					nextPosition = cellPosition;
					nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
				}

				shape->position(cellPosition - cellPadding.second);
				bounds.expandWith(cellPosition + toPoint(cellDimensions) - cellPadding.second);

				cellPosition = nextPosition;
			};

			bounds.expandWith(bounds.maxPoint + margins.second);
			setPointsFromBounds(bounds);
		}

		void Grid::layoutChildSize(){
			BoxAABB<> bounds(size(getContentWidth(), cellDimensions.height));
			Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
			size_t index = 0;
			
			PointPrecision maxHeightInLine = 0.0f;

			for(auto&& shape : drawList){
				auto shapeSize = shape->localAABB().size();
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
				shape->position(cellPosition - cellPadding.second);
				bounds.expandWith(cellPosition + toPoint(shapeSize) - cellPadding.second);

				cellPosition = nextPosition;
			};

			bounds.expandWith(bounds.maxPoint + margins.second);
			setPointsFromBounds(bounds);
		}

		void Grid::layoutCells(){
			if(dirtyGrid){
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
			cellPadding.first = toPoint(a_padding);
			cellPadding.second = toPoint(a_padding);
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
			margins.first = toPoint(a_margin);
			margins.second = toPoint(a_margin);
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

		MV::BoxAABB<> Grid::worldAABBImplementation(bool a_includeChildren, bool a_nestedCall) {
			layoutCells();
			return Node::worldAABBImplementation(a_includeChildren, a_nestedCall);
		}

		MV::BoxAABB<int> Grid::screenAABBImplementation(bool a_includeChildren, bool a_nestedCall) {
			layoutCells();
			return Node::screenAABBImplementation(a_includeChildren, a_nestedCall);
		}

		MV::BoxAABB<> Grid::localAABBImplementation(bool a_includeChildren, bool a_nestedCall) {
			layoutCells();
			return Node::localAABBImplementation(a_includeChildren, a_nestedCall);
		}

		void Grid::clearTextureCoordinates() {
			clearTexturePoints(points);
			alertParent(VisualChange::make(shared_from_this(), false));
		}

		void Grid::updateTextureCoordinates() {
			if(ourTexture != nullptr){
				ourTexture->apply(points);
				alertParent(VisualChange::make(shared_from_this(), false));
			} else {
				clearTextureCoordinates();
			}
		}

		void Grid::drawImplementation(){
			defaultDraw(GL_TRIANGLES);
		}

		BoxAABB<> Grid::calculateBasicAABBFromDimensions() const{
			BoxAABB<> bounds(size(getContentWidth(), cellDimensions.height));
			Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
			size_t index = 0;

			for(auto&& shape : drawList){
				auto nextPosition = cellPosition;
				nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
				if(bumpToNextLine(nextPosition.x - cellPadding.second.x, index++)){
					cellPosition.locate(margins.first.x, cellPosition.y + cellDimensions.height);
					cellPosition += cellPadding.first + cellPadding.second;
					nextPosition = cellPosition;
					nextPosition.translate(cellDimensions.width + (cellPadding.first + cellPadding.second).x, 0.0f);
				}

				bounds.expandWith(cellPosition + toPoint(cellDimensions) - cellPadding.second);

				cellPosition = nextPosition;
			}

			bounds.expandWith(bounds.maxPoint + margins.second);
			return bounds;
		}

		BoxAABB<> Grid::calculateBasicAABBFromCells() const{
			BoxAABB<> bounds(size(getContentWidth(), cellDimensions.height));
			Point<> cellPosition = cellPadding.first + cellPadding.second + margins.first;
			size_t index = 0;

			PointPrecision maxHeightInLine = 0.0f;

			for(auto&& shape : drawList){
				auto shapeSize = shape->localAABB().size();
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
				bounds.expandWith(cellPosition + toPoint(shapeSize) - cellPadding.second);

				cellPosition = nextPosition;
			}

			bounds.expandWith(bounds.maxPoint + margins.second);
			return bounds;
		}

		MV::BoxAABB<> Grid::basicAABBImplementation() const {
			if(dirtyGrid){
				if(cellDimensions.width > 0.0f || cellDimensions.height > 0.0f){
					return calculateBasicAABBFromDimensions();
				} else{
					return calculateBasicAABBFromCells();
				}
			} else{
				return BoxAABB<>(points[0], points[2]);
			}
		}

	}
}