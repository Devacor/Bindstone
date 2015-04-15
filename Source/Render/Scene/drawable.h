#ifndef _MV_SCENE_DRAWABLE_H_
#define _MV_SCENE_DRAWABLE_H_

#include "node.h"

#define DrawableDerivedAccessors(ComponentType) \
	ComponentDerivedAccessors(ComponentType) \
	std::shared_ptr<ComponentType> color(const MV::Color &a_newColor) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::color(a_newColor)); \
	}\
	std::shared_ptr<ComponentType> shader(const std::string &a_shaderProgramId) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::shader(a_shaderProgramId)); \
	} \
	std::shared_ptr<ComponentType> texture(std::shared_ptr<MV::TextureHandle> a_texture) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::texture(a_texture)); \
	} \
	std::shared_ptr<ComponentType> show() { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::show()); \
	} \
	std::shared_ptr<ComponentType> hide() { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::hide()); \
	} \
	MV::Color color() const { \
		return MV::Scene::Drawable::color(); \
	} \
	std::shared_ptr<MV::TextureHandle> texture() const{ \
		return ourTexture; \
	} \
	std::string shader() const { \
		return shaderProgramId; \
	}

namespace MV {
	namespace Scene {

		class Drawable : public Component {
			friend Node;
			friend cereal::access;

		public:

			virtual bool draw();

			bool visible() const {
				return shouldDraw;
			}

			Color color() const;

			std::string shader() const {
				return shaderProgramId;
			}

			std::shared_ptr<Drawable> hide();
			std::shared_ptr<Drawable> show();

			std::shared_ptr<Drawable> color(const Color &a_newColor);
			std::shared_ptr<Drawable> colors(const std::vector<Color> &a_newColors);

			std::shared_ptr<Drawable> shader(const std::string &a_shaderProgramId);

			std::shared_ptr<TextureHandle> texture() const {
				return ourTexture;
			}

			std::shared_ptr<Drawable> texture(std::shared_ptr<TextureHandle> a_texture);
			std::shared_ptr<Drawable> clearTexture();

		protected:
			Drawable(const std::weak_ptr<Node> &a_owner);

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

			virtual void defaultDrawImplementation();

			void refreshBounds();


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
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Drawable>().self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			std::vector<DrawPoint> points;
			std::vector<GLuint> vertexIndices;

			BoxAABB<> localBounds;

			Shader* shaderProgram = nullptr;
			std::string shaderProgramId = PREMULTIPLY_ID;
			GLuint bufferId = 0;

			std::shared_ptr<TextureHandle> ourTexture;
			TextureHandle::SignalType::SharedType textureSizeSignal;

			GLenum drawType = GL_TRIANGLES;

			bool shouldDraw = true;

			virtual void initialize() override;

		private:

			virtual void clearTextureCoordinates() {
			}

			virtual void updateTextureCoordinates() {
			}

			void lazyInitializeShader();

			void forceInitializeShader();
		};
	}
}

#endif
