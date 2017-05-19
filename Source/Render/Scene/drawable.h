#ifndef _MV_SCENE_DRAWABLE_H_
#define _MV_SCENE_DRAWABLE_H_

#include "node.h"

#define DrawableDerivedAccessorsShowHide(ComponentType) \
	std::shared_ptr<ComponentType> show() { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::show()); \
	} \
	std::shared_ptr<ComponentType> hide() { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::hide()); \
	}

#define DrawableDerivedAccessorsColor(ComponentType) \
	std::shared_ptr<ComponentType> color(const MV::Color &a_newColor) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::color(a_newColor)); \
	}\
	MV::Color color() const { \
		return MV::Scene::Drawable::color(); \
	}

#define DrawableDerivedAccessorsShader(ComponentType) \
	std::shared_ptr<ComponentType> shader(const std::string &a_shaderProgramId) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::shader(a_shaderProgramId)); \
	} \
	std::shared_ptr<ComponentType> materialSettings(std::function<void(Shader*)> a_materialSettingsCallback) { \
		userMaterialSettings = a_materialSettingsCallback; \
		return std::static_pointer_cast<ComponentType>(shared_from_this()); \
	} \
	std::shared_ptr<ComponentType> texture(std::shared_ptr<MV::TextureHandle> a_texture) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::texture(a_texture)); \
	} \
	std::shared_ptr<MV::TextureHandle> texture() const{ \
		return ourTexture; \
	} \
	std::string shader() const { \
		return shaderProgramId; \
	}

#define DrawableDerivedAccessorsNoColor(ComponentType) \
	ComponentDerivedAccessors(ComponentType) \
	DrawableDerivedAccessorsShowHide(ComponentType) \
	DrawableDerivedAccessorsShader(ComponentType)

#define DrawableDerivedAccessorsNoShowHide(ComponentType) \
	ComponentDerivedAccessors(ComponentType) \
	DrawableDerivedAccessorsColor(ComponentType) \
	DrawableDerivedAccessorsShader(ComponentType)

#define DrawableDerivedAccessors(ComponentType) \
	DrawableDerivedAccessorsNoColor(ComponentType) \
	DrawableDerivedAccessorsColor(ComponentType)

namespace MV {
	namespace Scene {

		class Drawable;

		class Anchors {
			friend cereal::access;
			friend Drawable;

		public:
			Anchors(Drawable *a_self);
			~Anchors();

			enum class BoundsToOffset { Ignore, Apply, Apply_Reposition };

			bool hasParent() const {
				return !parentReference.expired();
			}
			std::shared_ptr<Drawable> parent() const;

			Anchors& parent(const std::weak_ptr<Drawable> &a_parent, BoundsToOffset a_offsetFromBounds = BoundsToOffset::Ignore);
			Anchors& removeFromParent();

			Anchors& anchor(const BoxAABB<> &a_anchor);
			Anchors& anchor(const Point<> &a_anchor);

			BoxAABB<> anchor() const {
				return parentAnchors;
			}

			Anchors& offset(const BoxAABB<> &a_offset);

			BoxAABB<> offset() const {
				return ourOffset;
			}

			Anchors& pivot(const Point<> &a_pivot);

			Point<> pivot() const {
				return pivotPercent;
			}

			Anchors& apply();

			Anchors& usePosition(bool a_newValue);

			bool usePosition() const {
				return applyingPosition;
			}

			Anchors& applyBoundsToOffset(BoundsToOffset a_offsetFromBounds = BoundsToOffset::Apply);

		private:
			Anchors(const Anchors& a_rhs);
			Anchors& operator=(const Anchors& a_rhs);

			Drawable *selfReference = nullptr;
			std::weak_ptr<Drawable> parentReference;
			BoxAABB<> parentAnchors;
			BoxAABB<> ourOffset;
			Point<> pivotPercent;
			bool applying = false;
			bool applyingPosition = false;

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				auto selfShared = std::static_pointer_cast<Drawable>(selfReference->shared_from_this());
				bool parentCanUseId = !parentReference.expired() && parentReference.lock() == selfReference->owner()->componentInParents(parentReference.lock()->id(), false, true).self();
				archive(
					cereal::make_nvp("parent", parentCanUseId ? std::weak_ptr<Drawable>() : parentReference),
					cereal::make_nvp("parentId", parentCanUseId ? parentReference.lock()->id() : std::string()),
					cereal::make_nvp("anchors", parentAnchors),
					cereal::make_nvp("offset", ourOffset),
					cereal::make_nvp("pivot", pivotPercent),
					cereal::make_nvp("applyingPosition", applyingPosition)
				);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					cereal::make_nvp("parent", parentReference),
					cereal::make_nvp("parentId", parentIdLoaded),
					cereal::make_nvp("anchors", parentAnchors),
					cereal::make_nvp("offset", ourOffset),
					cereal::make_nvp("pivot", pivotPercent),
					cereal::make_nvp("applyingPosition", applyingPosition)
				);
			}

			std::string parentIdLoaded;
			
			void postLoadInitialize();
			void registerWithParent();
		};

		class Drawable : public Component {
			friend Anchors;
			friend Node;
			friend cereal::access;

		public:
			ComponentDerivedAccessors(Drawable)

			virtual bool draw();

			bool visible() const {
				return shouldDraw;
			}

			Color color() const;

			std::string shader() const {
				return shaderProgramId;
			}

			Anchors& anchors() {
				return ourAnchors;
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

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
				a_script.add(chaiscript::user_type<Drawable>(), "Drawable");
				a_script.add(chaiscript::base_class<Component, Drawable>());

				a_script.add(chaiscript::fun(&Drawable::visible), "visible");
				a_script.add(chaiscript::fun(&Drawable::hide), "hide");
				a_script.add(chaiscript::fun(&Drawable::show), "show");

				a_script.add(chaiscript::fun(&Drawable::materialSettings), "materialSettings");

				a_script.add(chaiscript::fun(static_cast<Color(Drawable::*)() const>(&Drawable::color)), "color");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(const Color &)>(&Drawable::color)), "color");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(const std::vector<Color> &)>(&Drawable::colors)), "colors");

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(Drawable::*)() const>(&Drawable::texture)), "texture");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(std::shared_ptr<TextureHandle>)>(&Drawable::texture)), "texture");
				a_script.add(chaiscript::fun(&Drawable::clearTexture), "clearTexture");

				a_script.add(chaiscript::fun(static_cast<std::string(Drawable::*)() const>(&Drawable::shader)), "shader");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(const std::string &)>(&Drawable::shader)), "shader");

				a_script.add(chaiscript::type_conversion<SafeComponent<Drawable>, std::shared_ptr<Drawable>>([](const SafeComponent<Drawable> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Drawable>, std::shared_ptr<Component>>([](const SafeComponent<Drawable> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));

				return a_script;
			}

			const DrawPoint point(size_t a_index) const {
				return points[a_index];
			}

			DrawPoint& point(size_t a_index) {
				return points[a_index];
			}

			size_t pointSize() const {
				return points.size();
			}

			std::shared_ptr<Drawable> materialSettings(std::function<void(Shader*)> a_materialSettingsCallback) {
				userMaterialSettings = a_materialSettingsCallback;
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> setPoints(const std::vector<DrawPoint> &a_points, const std::vector<GLuint> &a_vertexIndices) {
				points = a_points;
				vertexIndices = a_vertexIndices;
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

		protected:
			Drawable(const std::weak_ptr<Node> &a_owner);

			virtual void detachImplementation() override;

			virtual void materialSettingsImplementation(Shader* a_shaderProgram) {
				a_shaderProgram->set("time", static_cast<PointPrecision>(Stopwatch::systemTime()), false); //optional but helpful default
				a_shaderProgram->set("texture", ourTexture);
				a_shaderProgram->set("transformation", owner()->renderer().projectionMatrix().top() * owner()->worldTransform());
			}

			virtual void onRemoved() {
				notifyParentOfBoundsChange();
			}

			virtual BoxAABB<> boundsImplementation() override {
				return localBounds;
			}

			virtual void boundsImplementation(const BoxAABB<> &a_bounds) override;

			//return false if you want this to not draw
			virtual bool preDraw() {
				return shouldDraw;
			}

			//return false if you want to block children from drawing
			virtual bool postDraw() {
				return true;
			}

			virtual void defaultDrawImplementation();

			virtual void refreshBounds();

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const version) {
				if (version > 0) {
					archive(cereal::make_nvp("anchors", ourAnchors));
				}
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
			static void load_and_construct(Archive & archive, cereal::construct<Drawable> &construct, std::uint32_t const version) {
				construct(std::shared_ptr<Node>());
				if (version > 0) {
					archive(cereal::make_nvp("anchors", construct->ourAnchors));
				}
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
				auto test = construct->shared_from_this();
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Drawable>().self());
			}

			virtual void postLoadInitialize() override;

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

			Anchors ourAnchors;
			std::vector<Anchors*> childAnchors;

			std::function<void(Shader*)> userMaterialSettings;

			void hookupTextureSizeWatcher();

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
