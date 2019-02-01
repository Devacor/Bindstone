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
	std::shared_ptr<ComponentType> texture(std::shared_ptr<MV::TextureHandle> a_texture, size_t a_textureId = 0) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::texture(a_texture, a_textureId)); \
	} \
	std::shared_ptr<MV::TextureHandle> texture(size_t a_textureId = 0) const{ \
		return ourTextures.at(a_textureId); \
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
			//NOTE: not currently implemented
			Anchors& pivot(const Point<> &a_pivot);
			//NOTE: not currently implemented
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
            void save(Archive & archive, std::uint32_t const /*version*/) const;

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
			enum BlendModePreset { DEFAULT, MULTIPLY, ADD, SCREEN };

			ComponentDerivedAccessors(Drawable)

			~Drawable();

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

			std::shared_ptr<TextureHandle> texture(size_t a_index = 0) const {
				return ourTextures.at(a_index);
			}

			std::shared_ptr<Drawable> texture(std::shared_ptr<TextureHandle> a_texture, size_t a_textureId = 0);

			std::shared_ptr<Drawable> clearTextures();
			std::shared_ptr<Drawable> clearTexture(size_t a_textureId = 0);

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
				a_script.add(chaiscript::user_type<Drawable>(), "Drawable");
				a_script.add(chaiscript::base_class<Component, Drawable>());

				a_script.add(chaiscript::fun(&Drawable::visible), "visible");
				a_script.add(chaiscript::fun(&Drawable::hide), "hide");
				a_script.add(chaiscript::fun(&Drawable::show), "show");

				a_script.add(chaiscript::fun(&Drawable::point), "point");
				a_script.add(chaiscript::fun(&Drawable::pointSize), "pointSize");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(size_t a_index, const DrawPoint& a_value)>(&Drawable::setPoint)), "setPoint");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(size_t a_index, const Color& a_value)>(&Drawable::setPoint)), "setPoint");
				a_script.add(chaiscript::fun(&Drawable::getPoints), "getPoints");

				a_script.add(chaiscript::fun(&Drawable::setPoints), "setPoints");

				a_script.add(chaiscript::fun(&Drawable::pointIndices), "pointIndices");

				a_script.add(chaiscript::fun(&Drawable::materialSettings), "materialSettings");

				a_script.add(chaiscript::fun(static_cast<Color(Drawable::*)() const>(&Drawable::color)), "color");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(const Color &)>(&Drawable::color)), "color");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(const std::vector<Color> &)>(&Drawable::colors)), "colors");

				a_script.add(chaiscript::fun([](Drawable &a_self, std::shared_ptr<TextureHandle> a_texture, size_t a_textureId) {return a_self.texture(a_texture, a_textureId); }), "texture");
				a_script.add(chaiscript::fun([](Drawable &a_self, std::shared_ptr<TextureHandle> a_texture) {return a_self.texture(a_texture); }), "texture");
				a_script.add(chaiscript::fun([](Drawable &a_self, size_t a_textureId) {return a_self.texture(a_textureId); }), "texture");
				a_script.add(chaiscript::fun([](Drawable &a_self) {return a_self.texture(); }), "texture");
				a_script.add(chaiscript::fun([](Drawable &a_self, size_t a_textureId) {return a_self.clearTexture(a_textureId); }), "clearTexture");
				a_script.add(chaiscript::fun([](Drawable &a_self) {return a_self.clearTexture(); }), "clearTexture");
				a_script.add(chaiscript::fun([](Drawable &a_self) {return a_self.clearTextures(); }), "clearTextures");

				a_script.add(chaiscript::fun(static_cast<std::string(Drawable::*)() const>(&Drawable::shader)), "shader");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(const std::string &)>(&Drawable::shader)), "shader");

				a_script.add(chaiscript::type_conversion<SafeComponent<Drawable>, std::shared_ptr<Drawable>>([](const SafeComponent<Drawable> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Drawable>, std::shared_ptr<Component>>([](const SafeComponent<Drawable> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));

				return a_script;
			}

			//encapsulated to enforce bounds refreshing.
			const DrawPoint &point(size_t a_index) const {
				return points[a_index];
			}

			size_t pointSize() const {
				return points.size();
			}

			std::shared_ptr<Drawable> setPoint(size_t a_index, const DrawPoint& a_value) {
				bool needRefresh = points[a_index].point() != a_value.point();
				points[a_index] = a_value;
				if (needRefresh) {
					refreshBounds();
				}
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> setPoint(size_t a_index, const Color& a_value) {
				if (points[a_index].color() != a_value) {
					points[a_index] = a_value;
					dirtyVertexBuffer = true;
				}
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			//returns a copy of the points list, intentionally not a reference to enforce bounds refreshing.
			std::vector<DrawPoint> getPoints() const {
				return points;
			}

			//fine to just pass back a reference, changing the index order doesn't impact the bounding box logic.
			std::vector<GLuint>& pointIndices() {
				return vertexIndices;
			}

			std::shared_ptr<Drawable> setPoints(const std::vector<DrawPoint> &a_points, const std::vector<GLuint> &a_vertexIndices) {
				points = a_points;
				vertexIndices = a_vertexIndices;
				refreshBounds();
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> appendPoints(const std::vector<DrawPoint> &a_points, std::vector<GLuint> a_vertexIndices) {
				auto offsetIndex = static_cast<GLuint>(points.size());
				std::transform(a_vertexIndices.begin(), a_vertexIndices.end(), a_vertexIndices.begin(), [&](GLuint index) {return index + offsetIndex; });
				points.insert(points.end(), a_points.begin(), a_points.end());
				vertexIndices.insert(vertexIndices.end(), a_vertexIndices.begin(), a_vertexIndices.end());
				refreshBounds();
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> materialSettings(std::function<void(Shader*)> a_materialSettingsCallback) {
				userMaterialSettings = a_materialSettingsCallback;
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> blend(BlendModePreset a_newPreset) {
				blendModePreset = a_newPreset;
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			BlendModePreset blend() const {
				return blendModePreset;
			}

		protected:
			Drawable(const std::weak_ptr<Node> &a_owner);

			std::map<size_t, std::shared_ptr<TextureHandle>>::const_iterator disconnectTexture(size_t a_textureId);

			virtual bool serializePoints() const { return true; }

			virtual void detachImplementation() override;

			virtual void materialSettingsImplementation(Shader* a_shaderProgram);

			void addTexturesToShader();

			virtual void onRemoved() {
				notifyParentOfBoundsChange();
			}

			virtual BoxAABB<> boundsImplementation() override {
				return localBounds;
			}

			void applyPresetBlendMode(Draw2D &ourRenderer) const;

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
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				archive(cereal::make_nvp("anchors", ourAnchors));
				archive(CEREAL_NVP(shouldDraw));
				archive(cereal::make_nvp("textures", ourTextures));
				archive(
					CEREAL_NVP(shaderProgramId),
					CEREAL_NVP(localBounds),
					CEREAL_NVP(drawType)
				);

				if (serializePoints()) {
					archive(
						CEREAL_NVP(vertexIndices),
						CEREAL_NVP(points)
					);
				}
				archive(cereal::make_nvp("blendMode", blendModePreset));
				archive(cereal::make_nvp("Component", cereal::base_class<Component>(this)));
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if (version > 0) {
					archive(cereal::make_nvp("anchors", ourAnchors));
				}
				archive(CEREAL_NVP(shouldDraw));
				if (version < 3) {
					std::shared_ptr<TextureHandle> ourTexture;
					archive(cereal::make_nvp("ourTexture", ourTexture));
					ourTextures.clear();
					if (ourTexture) {
						ourTextures[0] = ourTexture;
					}
				}
				else {
					archive(cereal::make_nvp("textures", ourTextures));
				}
				if (version < 4)
				{
					archive(
						CEREAL_NVP(shaderProgramId),
						CEREAL_NVP(vertexIndices),
						CEREAL_NVP(localBounds),
						CEREAL_NVP(drawType),
						CEREAL_NVP(points)
					);
				}
				else
				{
					archive(
						CEREAL_NVP(shaderProgramId),
						CEREAL_NVP(localBounds),
						CEREAL_NVP(drawType)
					);

					if (serializePoints()) {
						archive(
							CEREAL_NVP(vertexIndices),
							CEREAL_NVP(points)
						);
					}
				}
				if (version > 1) {
					archive(cereal::make_nvp("blendMode", blendModePreset));
				}
				archive(cereal::make_nvp("Component", cereal::base_class<Component>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Drawable> &construct, std::uint32_t const version) {
				construct(std::shared_ptr<Node>());
				construct->load(archive, version);
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

			bool dirtyVertexBuffer = true;

			std::map<size_t, std::shared_ptr<TextureHandle>> ourTextures;
			std::map<size_t, TextureHandle::SignalType::SharedType> textureSizeSignals;

			GLenum drawType = GL_TRIANGLES;

			bool shouldDraw = true;

			virtual void initialize() override;

			Anchors ourAnchors;
			std::vector<Anchors*> childAnchors;

			std::function<void(Shader*)> userMaterialSettings;

			void hookupTextureSizeWatchers();
			void hookupTextureSizeWatcher(size_t a_textureId);

			BlendModePreset blendModePreset = DEFAULT;

			void rebuildTextureCache();

			std::vector<std::pair<std::string, std::shared_ptr<TextureDefinition>>> cachedTextureList;

		private:
			virtual void clearTextureCoordinates(size_t a_textureId) {
			}

			virtual void updateTextureCoordinates(size_t a_textureId) {
			}

			void lazyInitializeShader();

			void forceInitializeShader();
		};
        
        template <class Archive>
        void Anchors::save(Archive & archive, std::uint32_t const /*version*/) const {
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
	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_scenedrawable);

#endif
