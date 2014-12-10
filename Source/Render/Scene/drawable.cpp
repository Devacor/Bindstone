#include "drawable.h"
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Drawable);

namespace MV {
	namespace Scene {


		bool Drawable::draw() {
			bool allowChildrenToDraw = true;
			lazyInitializeShader();
			if (preDraw()) {
				SCOPE_EXIT{ allowChildrenToDraw = postDraw(); };
				defaultDrawImplementation();
			}
			return allowChildrenToDraw;
		}

		Color Drawable::color() const {
			std::vector<Color> colorsToAverage;
			for (auto&& point : points) {
				colorsToAverage.push_back(point);
			}
			return std::accumulate(colorsToAverage.begin(), colorsToAverage.end(), Color(0, 0, 0, 0)) / static_cast<PointPrecision>(colorsToAverage.size());
		}

		std::shared_ptr<Drawable> Drawable::color(const Color &a_newColor) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			for (auto&& point : points) {
				point = a_newColor;
			}
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> MV::Scene::Drawable::colors(const std::vector<Color>& a_newColors) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			MV::require<RangeException>(a_newColors.size() == points.size(), "Point and Color vector size mismatch!");
			for (size_t i = 0; i < points.size(); ++i) {
				points[i] = a_newColors[i];
			}
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> Drawable::hide() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			std::shared_ptr<Drawable> self = std::static_pointer_cast<Drawable>(shared_from_this());
			if (shouldDraw) {
				shouldDraw = false;
				notifyParentOfComponentChange();
			}
			return self;
		}

		std::shared_ptr<Drawable> MV::Scene::Drawable::show() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			std::shared_ptr<Drawable> self = std::static_pointer_cast<Drawable>(shared_from_this());
			if (!shouldDraw) {
				shouldDraw = true;
				notifyParentOfComponentChange();
			}
			return self;
		}

		std::shared_ptr<Drawable> Drawable::shader(const std::string &a_shaderProgramId) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			shaderProgramId = a_shaderProgramId;
			auto self = std::static_pointer_cast<Drawable>(shared_from_this());
			forceInitializeShader();
			return self;
		}

		std::shared_ptr<Drawable> Drawable::texture(std::shared_ptr<TextureHandle> a_texture) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			if (ourTexture && textureSizeSignal) {
				ourTexture->sizeObserver.disconnect(textureSizeSignal);
			}
			textureSizeSignal.reset();
			ourTexture = a_texture;
			if (ourTexture) {
				textureSizeSignal = TextureHandle::SignalType::make([&](std::shared_ptr<MV::TextureHandle> a_handle) {
					updateTextureCoordinates();
				});
				ourTexture->sizeObserver.connect(textureSizeSignal);
			}
			updateTextureCoordinates();
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> Drawable::clearTexture() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			if (ourTexture && textureSizeSignal) {
				ourTexture->sizeObserver.disconnect(textureSizeSignal);
			}
			ourTexture = nullptr;
			updateTextureCoordinates();
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		void Drawable::defaultDrawImplementation() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			if (!vertexIndices.empty()) {
				require<ResourceException>(shaderProgram, "No shader program for Drawable!");
				shaderProgram->use();

				if (bufferId == 0) {
					glGenBuffers(1, &bufferId);
				}

				glBindBuffer(GL_ARRAY_BUFFER, bufferId);
				auto structSize = static_cast<GLsizei>(sizeof(points[0]));
				glBufferData(GL_ARRAY_BUFFER, points.size() * structSize, &(points[0]), GL_STATIC_DRAW);

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);

				auto positionOffset = static_cast<GLsizei>(offsetof(DrawPoint, x));
				auto textureOffset = static_cast<GLsizei>(offsetof(DrawPoint, textureX));
				auto colorOffset = static_cast<GLsizei>(offsetof(DrawPoint, R));
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, structSize, (void*)positionOffset); //Point
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, structSize, (void*)textureOffset); //UV
				glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, structSize, (void*)colorOffset); //Color

				shaderProgram->set("texture", ourTexture);
				shaderProgram->set("transformation", owner()->renderer().projectionMatrix().top() * owner()->worldTransform());

				glDrawElements(drawType, static_cast<GLsizei>(vertexIndices.size()), GL_UNSIGNED_INT, &vertexIndices[0]);

				glDisableVertexAttribArray(0);
				glDisableVertexAttribArray(1);
				glDisableVertexAttribArray(2);
				glUseProgram(0);
			}
		}

		void Drawable::refreshBounds() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto originalBounds = localBounds;
			if (!points.empty()) {
				localBounds.initialize(points[0]);
				for (size_t i = 1; i < points.size(); ++i) {
					localBounds.expandWith(points[i]);
				}
			} else {
				localBounds = BoxAABB<>();
			}
			if (originalBounds != localBounds) {
				notifyParentOfBoundsChange();
			}
		}

		void Drawable::lazyInitializeShader() {
			if (!shaderProgram) {
				forceInitializeShader();
			}
		}

		void Drawable::forceInitializeShader() {
			if (owner()->renderer().hasShader(shaderProgramId)) {
				shaderProgram = owner()->renderer().getShader(shaderProgramId);
			} else {
				shaderProgram = owner()->renderer().defaultShader();
			}
		}

		Drawable::Drawable(const std::weak_ptr<Node> &a_owner) :
			Component(a_owner) {
		}

	}
}
