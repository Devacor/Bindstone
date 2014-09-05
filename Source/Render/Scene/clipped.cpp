#include "clipped.h"
#include <memory>
#include "cereal/archives/json.hpp"
CEREAL_REGISTER_TYPE(MV::Scene::Clipped);

namespace MV {
	namespace Scene {

		/*************************\
		| --------Clipped-------- |
		\*************************/

		void Clipped::refreshTexture(bool a_forceRefresh) {
			if(a_forceRefresh || dirtyTexture){
				auto pointAABB = basicAABB();
				auto textureSize = cast<int>(pointAABB.size());
				if(!clippedTexture || clippedTexture->size() != textureSize){
					clippedTexture = DynamicTextureDefinition::make("", textureSize, {0.0f, 0.0f, 0.0f, 0.0f});
				}
				dirtyTexture = false;
				texture(clippedTexture->makeHandle(textureSize));
				{
					auto framebuffer = renderer->makeFramebuffer(cast<int>(pointAABB.minPoint), textureSize, clippedTexture->textureId())->start();

					renderer->setBlendFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
					SCOPE_EXIT{renderer->defaultBlendFunction(); };

					renderer->modelviewMatrix().push();
					SCOPE_EXIT{renderer->modelviewMatrix().pop(); };
					renderer->modelviewMatrix().top().makeIdentity();

					if(drawSorted){
						sortedRender();
					} else{
						unsortedRender();
					}
				}

				alertParent(VisualChange::make(shared_from_this(), false));
			}
		}

		bool Clipped::preDraw() {
			refreshTexture();

			pushMatrix();
			SCOPE_EXIT{popMatrix(); };

			renderer->setBlendFunction(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			SCOPE_EXIT{renderer->defaultBlendFunction(); };
			defaultDraw(GL_TRIANGLE_FAN);

			return false; //returning false blocks the default rendering steps for this node.
		}

		std::shared_ptr<Clipped> Clipped::make(Draw2D* a_renderer) {
			auto clipped = std::shared_ptr<Clipped>(new Clipped(a_renderer));
			clipped->registerShader();
			return clipped;
		}

		std::shared_ptr<Clipped> Clipped::make(Draw2D* a_renderer, const Size<> &a_size, bool a_center) {
			auto clipped = std::shared_ptr<Clipped>(new Clipped(a_renderer));
			clipped->registerShader();
			return clipped->size(a_size, a_center);
		}

		std::shared_ptr<Clipped> Clipped::make(Draw2D* a_renderer, const Size<> &a_size, const Point<> &a_centerPoint) {
			auto clipped = std::shared_ptr<Clipped>(new Clipped(a_renderer));
			clipped->registerShader();
			return clipped->size(a_size, a_centerPoint);
		}

		std::shared_ptr<Clipped> Clipped::make(Draw2D* a_renderer, const BoxAABB<> &a_boxAABB) {
			auto clipped = std::shared_ptr<Clipped>(new Clipped(a_renderer));
			clipped->registerShader();
			return clipped->bounds(a_boxAABB);
		}

		void Clipped::drawIgnoringClipping(){
			if(isVisible){
				pushMatrix();
				SCOPE_EXIT{popMatrix(); };

				if(drawSorted){
					sortedRender();
				} else{
					unsortedRender();
				}
			}
		}

		void Clipped::drawIgnoringClipping(const Point<> &a_positionOverride) {
			if(isVisible){
				auto oldPosition = position();
				position(a_positionOverride);
				SCOPE_EXIT{position(oldPosition);};

				pushMatrix();
				SCOPE_EXIT{popMatrix(); };

				if(drawSorted){
					sortedRender();
				} else{
					unsortedRender();
				}
			}
		}

		BoxAABB<> Clipped::worldAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return Node::worldAABBImplementation(false, a_nestedCall);
		}
		BoxAABB<int> Clipped::screenAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return Node::screenAABBImplementation(false, a_nestedCall);
		}
		BoxAABB<> Clipped::localAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			return Node::localAABBImplementation(false, a_nestedCall);
		}

	}
}
