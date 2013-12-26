#ifndef _MV_COMPOSITESCENE_H_
#define _MV_COMPOSITESCENE_H_

#include "Render/scene.h"

namespace MV {
	namespace Scene {
		class Clipped : public Node{
			friend Node;
		public:
			static std::shared_ptr<Clipped> make(Draw2D* a_renderer, Size<> &a_size){
				auto clipped = std::shared_ptr<Clipped>(new Clipped(a_renderer));
				clipped->setSize(a_size);
				return clipped;
			}
			virtual ~Clipped(){}

			void setSize(const Size<> &a_size);

			void clearTextureCoordinates();
			void updateTextureCoordinates();
		protected:
			Clipped(Draw2D *a_renderer):
				Node(a_renderer){
				points.resize(4);
			}

		private:
			virtual bool preDraw();

			virtual bool getMessage(const std::string &a_message);

			std::shared_ptr<DynamicTextureDefinition> clippedTexture;
			std::shared_ptr<Framebuffer> framebuffer;
			bool dirtyTexture;
		};
	}
}

#endif