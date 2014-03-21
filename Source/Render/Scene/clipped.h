#ifndef _MV_SCENE_CLIPPED_H_
#define _MV_SCENE_CLIPPED_H_

#include "Render/Scene/primitives.h"

namespace MV {
	namespace Scene {
		class Clipped :
			public Rectangle,
			public MessageHandler<VisualChange>{

			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS

			static std::shared_ptr<Clipped> make(Draw2D* a_renderer);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Point<> &a_topLeft, const Point<> &a_bottomRight);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Point<> &a_point, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Size<> &a_size);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const BoxAABB &a_boxAABB);

			virtual ~Clipped(){}

			void refreshTexture(bool a_forceRefresh = false);

		protected:
			Clipped(Draw2D *a_renderer):
				Rectangle(a_renderer),
				dirtyTexture(true){
			}

		private:
			virtual bool preDraw();

			virtual void handleBegin(std::shared_ptr<VisualChange>){
				dirtyTexture = true;
			}
			virtual void handleEnd(std::shared_ptr<VisualChange>){
			}

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Clipped> &construct){
				construct(nullptr);
				archive(
					cereal::make_nvp("rectangle", cereal::base_class<Rectangle>(construct.ptr()))
					);
			}

			std::shared_ptr<DynamicTextureDefinition> clippedTexture;
			std::shared_ptr<Framebuffer> framebuffer;

			bool dirtyTexture;
		};
	}
}

#endif
