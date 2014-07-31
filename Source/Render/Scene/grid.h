#ifndef _MV_SCENE_GRID_H_
#define _MV_SCENE_GRID_H_

#include "Render/Scene/node.h"

namespace MV {
	namespace Scene {
		class Grid :
			public Node{

			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS(Grid)

			static std::shared_ptr<Grid> make(Draw2D* a_renderer);
			static std::shared_ptr<Grid> make(Draw2D* a_renderer, const Size<> &a_cellSize);
			
			virtual ~Grid(){}

			std::shared_ptr<Grid> update();

			std::shared_ptr<Grid> padding(const std::pair<Point<>, Point<>> &a_padding);
			std::shared_ptr<Grid> padding(const Size<> &a_padding);
			std::pair<Point<>, Point<>> padding() const;

			std::shared_ptr<Grid> cellSize(const Size<> &a_size);
			Size<> cellSize() const;

			std::shared_ptr<Grid> rowWidth(PointPrecision a_rowWidth);
			PointPrecision rowWidth() const;

			std::shared_ptr<Grid> rows(size_t a_rows);
			size_t rows() const;

			std::shared_ptr<Grid> margin(const std::pair<Point<>, Point<>> &a_margin);
			std::shared_ptr<Grid> margin(const Size<> &a_margin);
			std::pair<Point<>, Point<>> margin() const;

			virtual void clearTextureCoordinates();
			virtual void updateTextureCoordinates();
		protected:
			Grid(Draw2D *a_renderer, const Size<> &a_cellSize);

			Grid(Draw2D *a_renderer);

			void initializePoints() {
				points.emplace_back(Point<>(), Color(0.0f, 0.0f, 0.0f, 0.0f));
				points.emplace_back(Point<>(), Color(0.0f, 0.0f, 0.0f, 0.0f));
				points.emplace_back(Point<>(), Color(0.0f, 0.0f, 0.0f, 0.0f));
				points.emplace_back(Point<>(), Color(0.0f, 0.0f, 0.0f, 0.0f));

				points[0].textureX = 0.0; points[0].textureY = 0.0;
				points[1].textureX = 0.0; points[1].textureY = 1.0;
				points[2].textureX = 1.0; points[2].textureY = 1.0;
				points[3].textureX = 1.0; points[3].textureY = 0.0;

				appendQuadVertexIndices(vertexIndices, 0);
			}

			virtual BoxAABB worldAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB screenAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB localAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB basicAABBImplementation() const override;

			virtual void drawImplementation();

		private:
			virtual bool preDraw();

			virtual bool handleBegin(std::shared_ptr<VisualChange> a_change){
				dirtyGrid = dirtyGrid || a_change->changeShape;
				return true;
			}
			virtual void handleEnd(std::shared_ptr<VisualChange>){
			}

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("node", cereal::base_class<Node>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Grid> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require(renderer != nullptr, MV::PointerException("Error: Failed to load a renderer for Grid node."));
				construct(renderer);
				archive(cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}

			std::shared_ptr<DynamicTextureDefinition> clippedTexture;
			std::shared_ptr<Framebuffer> framebuffer;

			virtual void onChildAdded(std::shared_ptr<Node>){
				dirtyGrid = true;
			}
			void layoutCellSize();
			void layoutChildSize();
			bool bumpToNextLine(PointPrecision a_currentPosition, size_t a_index) const;
			void layoutCells();
			float getContentWidth() const;
			void setPointsFromBounds(const BoxAABB &a_bounds);
			BoxAABB calculateBasicAABBFromDimensions(DrawListVectorType &a_drawListVector) const;
			BoxAABB calculateBasicAABBFromCells(DrawListVectorType &a_drawListVector) const;
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
