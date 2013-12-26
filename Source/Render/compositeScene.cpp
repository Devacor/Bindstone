#include "compositeScene.h"

namespace MV {
	namespace Scene {
		void Clipped::updateTextureCoordinates() {
			if(texture != nullptr){
				points[0].textureX = texture->percentLeft(); points[0].textureY = texture->percentTop();
				points[1].textureX = texture->percentLeft(); points[1].textureY = texture->percentBottom();
				points[2].textureX = texture->percentRight(); points[2].textureY = texture->percentBottom();
				points[3].textureX = texture->percentRight(); points[3].textureY = texture->percentTop();
			} else{
				clearTextureCoordinates();
			}
			alertParent("Change");
		}

		void Clipped::clearTextureCoordinates() {
			points[0].textureX = 0.0; points[0].textureY = 0.0;
			points[1].textureX = 0.0; points[1].textureY = 1.0;
			points[2].textureX = 1.0; points[2].textureY = 1.0;
			points[3].textureX = 1.0; points[3].textureY = 0.0;
			alertParent("Change");
		}

		void Clipped::setSize(const Size<> &a_size) {
			points[2] = pointFromSize(a_size);
			points[1].y = points[2].y;
			points[3].x = points[2].x;
			clippedTexture = DynamicTextureDefinition::make("", castSize<int>(a_size));
			framebuffer = renderer->makeFramebuffer(castSize<int>(a_size), 0); //need to set texture id later anyway
			clearTexture();
			dirtyTexture = true;
			alertParent("Changed");
		}

		void Clipped::refreshTexture(bool a_forceRefresh) {
			if(a_forceRefresh || dirtyTexture){
				dirtyTexture = false;
				auto textureSize = castSize<int>(sizeFromPoint(points[2]));
				setTexture(clippedTexture->makeHandle(Point<int>(), textureSize));
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
			}
		}

		bool Clipped::preDraw() {
			refreshTexture();

			pushMatrix();
			SCOPE_EXIT{popMatrix();};
			defaultDraw(GL_TRIANGLE_FAN);

			return false; //returning false blocks the default rendering steps for this node.
		}

		bool Clipped::getMessage(const std::string &a_message) {
			bool handled = Node::getMessage(a_message);
			if(a_message == "Change"){
				dirtyTexture = true;
				handled = true;
			}
			return handled;
		}

	}
}