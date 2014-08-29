#ifndef _MV_SCENE_CLIPPED_H_
#define _MV_SCENE_CLIPPED_H_

#include "Render/Scene/primitives.h"

namespace MV {
	namespace Scene {
		class Clipped :
			public Rectangle{

			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS(Clipped)
			RECTANGLE_OVERRIDES(Clipped)

			static std::shared_ptr<Clipped> make(Draw2D* a_renderer);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Size<> &a_size, const Point<>& a_centerPoint);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const BoxAABB<> &a_boxAABB);

			virtual ~Clipped(){}
			
			void refreshTexture(bool a_forceRefresh = false);

			//Useful for debugging
			void drawIgnoringClipping();
			void drawIgnoringClipping(const Point<> &a_positionOverride);
		protected:
			Clipped(Draw2D *a_renderer):
				Rectangle(a_renderer),
				dirtyTexture(true){
			}

			virtual BoxAABB<> worldAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB<int> screenAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB<> localAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;

		private:
			virtual bool preDraw();

			virtual bool handleBegin(std::shared_ptr<VisualChange>){
				dirtyTexture = true;
				return false;
			}
			virtual void handleEnd(std::shared_ptr<VisualChange>){
			}

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Clipped> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require<PointerException>(renderer != nullptr, "Error: Failed to load a renderer for Clipped node.");
				construct(renderer);
				archive(cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(construct.ptr())));
			}

			std::shared_ptr<DynamicTextureDefinition> clippedTexture;
			std::shared_ptr<Framebuffer> framebuffer;

			virtual void onChildAdded(std::shared_ptr<Node>){
				dirtyTexture = true;
			}

			bool dirtyTexture;
		};
	}
}

#endif
