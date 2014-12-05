#ifndef _MV_SCENE_DRAWABLE_H_
#define _MV_SCENE_DRAWABLE_H_

#include "node.h"

#define DrawableDerivedAccessors(ComponentType) \
	std::shared_ptr<ComponentType> color(const Color &a_newColor) { \
		return std::static_pointer_cast<ComponentType>(Drawable::color(a_newColor)); \
	}\
	std::shared_ptr<ComponentType> shader(const std::string &a_shaderProgramId) { \
		return std::static_pointer_cast<ComponentType>(Drawable::shader(a_shaderProgramId)); \
	} \
	std::shared_ptr<ComponentType> texture(std::shared_ptr<TextureHandle> a_texture) { \
		return std::static_pointer_cast<ComponentType>(Drawable::texture(a_texture)); \
	} \
	std::shared_ptr<ComponentType> show() { \
		return std::static_pointer_cast<ComponentType>(Drawable::show()); \
	} \
	std::shared_ptr<ComponentType> hide() { \
		return std::static_pointer_cast<ComponentType>(Drawable::hide()); \
	} \
	Color color() const { \
		return Drawable::color(); \
	} \
	std::shared_ptr<TextureHandle> texture() const{ \
		return ourTexture; \
	}

namespace MV {
	namespace Scene {

		class Drawable : public Component {
			friend Node;
			friend cereal::access;

		public:

			virtual bool draw() {
				bool allowChildrenToDraw = true;
				lazyInitializeShader();
				if (preDraw()) {
					SCOPE_EXIT{ allowChildrenToDraw = postDraw(); };
					defaultDrawImplementation();
				}
				return allowChildrenToDraw;
			}

			bool visible() const {
				return shouldDraw;
			}

			Color color() const {
				std::vector<Color> colorsToAverage;
				for (auto&& point : points) {
					colorsToAverage.push_back(point);
				}
				return std::accumulate(colorsToAverage.begin(), colorsToAverage.end(), Color(0, 0, 0, 0)) / static_cast<PointPrecision>(colorsToAverage.size());
			}

			std::string shader() const {
				return shaderProgramId;
			}

			std::shared_ptr<Drawable> hide() {
				std::lock_guard<std::recursive_mutex> guard(lock);
				std::shared_ptr<Drawable> self = std::static_pointer_cast<Drawable>(shared_from_this());
				if (shouldDraw) {
					shouldDraw = false;
					notifyParentOfComponentChange();
				}
				return self;
			}

			std::shared_ptr<Drawable> show() {
				std::lock_guard<std::recursive_mutex> guard(lock);
				std::shared_ptr<Drawable> self = std::static_pointer_cast<Drawable>(shared_from_this());
				if (!shouldDraw) {
					shouldDraw = true;
					notifyParentOfComponentChange();
				}
				return self;
			}

			std::shared_ptr<Drawable> color(const Color &a_newColor) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				for (auto&& point : points) {
					point = a_newColor;
				}
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> colors(const std::vector<Color> &a_newColors) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				MV::require<RangeException>(a_newColors.size() == points.size(), "Point and Color vector size mismatch!");
				for (size_t i = 0; i < points.size();++i) {
					points[i] = a_newColors[i];
				}
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> shader(const std::string &a_shaderProgramId) {
				std::lock_guard<std::recursive_mutex> guard(lock);
				shaderProgramId = a_shaderProgramId;
				auto self = std::static_pointer_cast<Drawable>(shared_from_this());
				forceInitializeShader();
				return self;
			}

			std::shared_ptr<TextureHandle> texture() const {
				return ourTexture;
			}

			std::shared_ptr<Drawable> texture(std::shared_ptr<TextureHandle> a_texture) {
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

			std::shared_ptr<Drawable> clearTexture() {
				std::lock_guard<std::recursive_mutex> guard(lock);
				if (ourTexture && textureSizeSignal) {
					ourTexture->sizeObserver.disconnect(textureSizeSignal);
				}
				ourTexture = nullptr;
				updateTextureCoordinates();
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

		protected:
			Drawable(const std::weak_ptr<Node> &a_owner) :
				Component(a_owner) {
			}


			virtual BoxAABB<> boundsImplementation() {
				return localBounds;
			}

			//return false if you want this to not draw
			virtual bool preDraw() {
				return shouldDraw;
			}

			//return false if you want to block children from drawing
			virtual bool postDraw() {
				return true;
			}

			virtual void defaultDrawImplementation() {
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

			void refreshBounds() {
				std::lock_guard<std::recursive_mutex> guard(lock);
				auto originalBounds = localBounds;
				if (!points.empty()) {
					localBounds.initialize(points[0]);
					for (size_t i = 1; i < points.size(); ++i) {
						localBounds.expandWith(points[i]);
					}
				}
				else {
					localBounds = BoxAABB<>();
				}
				if (originalBounds != localBounds) {
					notifyParentOfBoundsChange();
				}
			}


			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					CEREAL_NVP(shouldDraw),
					CEREAL_NVP(ourTexture),
					CEREAL_NVP(shaderProgramId),
					CEREAL_NVP(vertexIndices),
					CEREAL_NVP(localBounds),
					CEREAL_NVP(drawType),
					CEREAL_NVP(points),
					cereal::make_nvp("Component", cereal::base_class<Component>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Drawable> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("shouldDraw", construct->shouldDraw),
					cereal::make_nvp("ourTexture", construct->ourTexture),
					cereal::make_nvp("shaderProgramId", construct->shaderProgramId),
					cereal::make_nvp("vertexIndices", construct->vertexIndices),
					cereal::make_nvp("localBounds", construct->localBounds),
					cereal::make_nvp("drawType", construct->drawType),
					cereal::make_nvp("points", construct->points),
					cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
				);
			}

			std::vector<DrawPoint> points;
			std::vector<GLuint> vertexIndices;

			BoxAABB<> localBounds;

			Shader* shaderProgram = nullptr;
			std::string shaderProgramId;
			GLuint bufferId = 0;

			std::shared_ptr<TextureHandle> ourTexture;
			TextureHandle::SignalType::SharedType textureSizeSignal;

			GLenum drawType = GL_TRIANGLES;

			bool shouldDraw = true;

		private:

			virtual void clearTextureCoordinates() {
			}

			virtual void updateTextureCoordinates() {
			}

			void lazyInitializeShader() {
				if (!shaderProgram) {
					forceInitializeShader();
				}
			}

			void forceInitializeShader() {
				if (owner()->renderer().hasShader(shaderProgramId)) {
					shaderProgram = owner()->renderer().getShader(shaderProgramId);
				}
				else {
					shaderProgram = owner()->renderer().defaultShader();
				}
			}
		};
	}
}

#endif
