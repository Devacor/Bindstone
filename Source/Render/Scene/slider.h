#ifndef _MV_SCENE_SLIDER_H_
#define _MV_SCENE_SLIDER_H_

#include "Render/Scene/button.h"

namespace MV {
	namespace Scene {
		class Slider : public Node{
			friend cereal::access;

			MV::Slot<void(std::shared_ptr<Slider>)> onPercentChangeSlot;

		public:
			MV::SlotRegister<void(std::shared_ptr<Slider>)> onPercentChange;

			SCENE_MAKE_FACTORY_METHODS(Slider)

				static std::shared_ptr<Slider> make(Draw2D* a_renderer, MouseState *a_mouse);
			static std::shared_ptr<Slider> make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Slider> make(Draw2D* a_renderer, MouseState *a_mouse, const Size<> &a_size, const Point<>& a_centerPoint);
			static std::shared_ptr<Slider> make(Draw2D* a_renderer, MouseState *a_mouse, const BoxAABB &a_boxAABB);

			std::shared_ptr<Clickable> area() const{
				return dragArea;
			}

			std::shared_ptr<Rectangle> handle() const{
				return dragHandle;
			}

			PointPrecision percent() const{
				return dragPercent;
			}

			std::shared_ptr<Slider> percent(PointPrecision a_newPercent);

			virtual void setRenderer(Draw2D* a_renderer, bool includeChildren = true, bool includeParents = true) override;
		protected:
			Slider(Draw2D *a_renderer):
				Node(a_renderer),
				dragPercent(0.0f),
				onPercentChange(onPercentChangeSlot),
				inUpdateHandlePosition(false),
				dragHandle(Rectangle::make(a_renderer)){
			}

			virtual void drawImplementation();

			virtual BoxAABB worldAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB screenAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB localAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB basicAABBImplementation() const override;
		private:
			void updateHandlePosition();
			void updateDragPercentForMousePosition(const Point<int> &a_screenPoint);

			void initializeDragArea();

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("percent", dragPercent), cereal::make_nvp("area", dragArea), 
					cereal::make_nvp("handle", dragHandle), cereal::make_nvp("node", cereal::base_class<Node>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Slider> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require<PointerException>(renderer != nullptr, "Error: Failed to load a renderer for Slider node.");
				construct(renderer);
				archive(cereal::make_nvp("percent", construct->dragPercent), cereal::make_nvp("area", construct->dragArea), 
					cereal::make_nvp("handle", construct->dragHandle), cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}

			std::shared_ptr<Clickable> dragArea;
			std::shared_ptr<Rectangle> dragHandle;
			PointPrecision dragPercent;
			bool inUpdateHandlePosition;
		};
	}
}

#endif
