#ifndef _MV_COMPOSITESCENE_H_
#define _MV_COMPOSITESCENE_H_

#include "Render/scene.h"

namespace MV {
	namespace Scene {
		class Clipped : public Rectangle, public MessageHandler<VisualChange>{
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS

			static std::shared_ptr<Clipped> make(Draw2D* a_renderer);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Point<> &a_topLeft, const Point<> &a_bottomRight);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Point<> &a_point, Size<> &a_size, bool a_center);
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, const Size<> &a_size);

			virtual ~Clipped(){}

			void refreshTexture(bool a_forceRefresh = false);
		protected:
			Clipped(Draw2D *a_renderer):
				Rectangle(a_renderer){
			}

		private:
			virtual bool preDraw();

			virtual void handleBegin(std::shared_ptr<VisualChange>){
				dirtyTexture = true;
			}
			virtual void handleEnd(std::shared_ptr<VisualChange>){
			}

			std::shared_ptr<DynamicTextureDefinition> clippedTexture;
			std::shared_ptr<Framebuffer> framebuffer;

			bool dirtyTexture;
		};
	}
}

#endif