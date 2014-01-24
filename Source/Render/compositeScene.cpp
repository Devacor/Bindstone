#include "compositeScene.h"
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Clipped);

namespace MV {
	namespace Scene {

		void Clipped::refreshTexture(bool a_forceRefresh) {
			if(a_forceRefresh || dirtyTexture){
				auto pointAABB = getPointAABB();
				auto textureSize = castSize<int>(pointAABB.getSize());
				clippedTexture = DynamicTextureDefinition::make("", textureSize);
				dirtyTexture = false;
				setTexture(clippedTexture->makeHandle(Point<int>(), textureSize));
				framebuffer = renderer->makeFramebuffer(castPoint<int>(pointAABB.minPoint), textureSize, clippedTexture->textureId()); //need to set texture id later anyway
				framebuffer->setTextureId(clippedTexture->textureId());
				{
					renderer->modelviewMatrix().push();
					SCOPE_EXIT{renderer->modelviewMatrix().pop();};
					renderer->modelviewMatrix().top().makeIdentity();
					
					pushMatrix();
					SCOPE_EXIT{popMatrix();};

					framebuffer->start();
					SCOPE_EXIT{framebuffer->stop();};

					if(drawSorted){
						sortedRender();
					} else{
						unsortedRender();
					}
				}
				alertParent(VisualChange::make(shared_from_this()));
			}
		}

		bool Clipped::preDraw() {
			refreshTexture();

			pushMatrix();
			SCOPE_EXIT{popMatrix();};
			defaultDraw(GL_TRIANGLE_FAN);

			return false; //returning false blocks the default rendering steps for this node.
		}

		std::shared_ptr<Clipped> Clipped::make(Draw2D* a_renderer) {
			return std::shared_ptr<Clipped>(new Clipped(a_renderer));
		}

		std::shared_ptr<Clipped> Clipped::make(Draw2D* a_renderer, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight) {
			auto clipped = std::shared_ptr<Clipped>(new Clipped(a_renderer));
			clipped->setTwoCorners(a_topLeft, a_bottomRight);
			return clipped;
		}

		std::shared_ptr<Clipped> Clipped::make(Draw2D* a_renderer, const Point<> &a_topLeft, const Point<> &a_bottomRight) {
			auto clipped = std::shared_ptr<Clipped>(new Clipped(a_renderer));
			clipped->setTwoCorners(a_topLeft, a_bottomRight);
			return clipped;
		}

		std::shared_ptr<Clipped> Clipped::make(Draw2D* a_renderer, const Point<> &a_point, Size<> &a_size, bool a_center) {
			auto clipped = std::shared_ptr<Clipped>(new Clipped(a_renderer));
			if(a_center){
				clipped->setSizeAndCenterPoint(a_point, a_size);
			} else{
				clipped->setSizeAndCornerPoint(a_point, a_size);
			}
			return clipped;
		}

		std::shared_ptr<Clipped> Clipped::make(Draw2D* a_renderer, const Size<> &a_size) {
			auto clipped = std::shared_ptr<Clipped>(new Clipped(a_renderer));
			clipped->setSize(a_size);
			return clipped;
		}

	}
}
