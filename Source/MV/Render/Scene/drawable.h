#ifndef _MV_SCENE_DRAWABLE_H_
#define _MV_SCENE_DRAWABLE_H_

#include "node.h"
#include "MV/Render/mv_jai_serialization_fwd.hpp"  // Forward decls for Anchors friend declarations
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

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
	} \
	MV::Color color() const { \
		return MV::Scene::Drawable::color(); \
	}

#define DrawableDerivedAccessorsShader(ComponentType) \
	std::shared_ptr<ComponentType> shader(const std::string &a_shaderProgramId) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::shader(a_shaderProgramId)); \
	} \
	std::shared_ptr<ComponentType> material(std::shared_ptr<Material> a_material) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::material(a_material)); \
	} \
	std::shared_ptr<Material> material() const { \
		return MV::Scene::Drawable::material(); \
	} \
	std::shared_ptr<ComponentType> materialSettings(std::function<void(Shader&)> a_materialSettingsCallback) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::materialSettings(a_materialSettingsCallback)); \
	} \
	std::shared_ptr<ComponentType> texture(std::shared_ptr<MV::TextureHandle> a_texture, size_t a_textureId = 0) { \
		return std::static_pointer_cast<ComponentType>(MV::Scene::Drawable::texture(a_texture, a_textureId)); \
	} \
	std::shared_ptr<MV::TextureHandle> texture(size_t a_textureId = 0) const{ \
		return MV::Scene::Drawable::texture(a_textureId); \
	} \
	std::string shader() const { \
		return MV::Scene::Drawable::shader(); \
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
			friend jai::access;
			friend Drawable;
			friend jai::property<Anchors>;
			template<typename Archive>
			friend void save(Archive& ar, const Anchors& anchors) requires jai::serialization::jai_archive<Archive>;
			template<typename Archive>
			friend void load(Archive& ar, Anchors& anchors) requires jai::serialization::jai_archive<Archive>;

		public:
			Anchors(Drawable* a_self);
			Anchors(Anchors&&) noexcept = default;
			~Anchors();

			bool hasParent() const {
				return !parentReference.expired();
			}
			std::shared_ptr<Drawable> parent() const;

			Anchors& parent(const std::weak_ptr<Drawable>& a_parent, bool a_calculateOffsetAndPivot = false);
			Anchors& removeFromParent();

			Anchors& anchor(const BoxAABB<>& a_anchor);
			Anchors& anchor(const Point<>& a_anchor);

			BoxAABB<> anchor() const {
				return parentAnchors;
			}

			Anchors& offset(const BoxAABB<>& a_offset);

			BoxAABB<> offset() const {
				return ourOffset;
			}

			Anchors& pivot(const Point<>& a_pivot);
			Point<> pivot() const {
				return pivotPercent;
			}

			Anchors& apply();

			Anchors& usePosition(bool a_newValue);

			bool usePosition() const {
				return applyingPosition;
			}

			Anchors& calculateOffsetAndPivot();

		private:
			Anchors(const Anchors& a_rhs);
			Anchors& operator=(const Anchors& a_rhs);

			Drawable* selfReference = nullptr;
			std::weak_ptr<Drawable> parentReference;
			BoxAABB<> parentAnchors;
			BoxAABB<> ourOffset;
			Point<> pivotPercent;
			bool applying = false;
			bool applyingPosition = false;

			std::string parentIdLoaded;

			void postLoadInitialize();
			void registerWithParent();
		};

		template<typename Archive>
		void save(Archive& ar, const Anchors& anchors) requires jai::serialization::jai_archive<Archive>;
		template<typename Archive>
		void load(Archive& ar, Anchors& anchors) requires jai::serialization::jai_archive<Archive>;

		class Drawable : public jai::property_owner<Drawable, Component> {
			friend Anchors;
			friend Node;
			friend jai::access;

		public:

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
				return ourAnchors.get();
			}

			std::shared_ptr<Drawable> hide();
			std::shared_ptr<Drawable> show();

			std::shared_ptr<Drawable> color(const Color& a_newColor);
			std::shared_ptr<Drawable> colors(const std::vector<Color>& a_newColors);

			std::shared_ptr<Drawable> shader(const std::string& a_shaderProgramId);
			
			std::shared_ptr<Drawable> material(std::shared_ptr<Material> a_material);
			std::shared_ptr<Material> material() const { return materialInstance; }

			std::shared_ptr<TextureHandle> texture(size_t a_index = 0) const {
				return ourTextures->at(a_index);
			}

			std::shared_ptr<Drawable> texture(std::shared_ptr<TextureHandle> a_texture, size_t a_textureId = 0);

			std::shared_ptr<Drawable> clearTextures();
			std::shared_ptr<Drawable> clearTexture(size_t a_textureId = 0);

			//encapsulated to enforce bounds refreshing.
			const DrawPoint& point(size_t a_index) const {
				return (*points)[a_index];
			}

			size_t pointSize() const {
				return points->size();
			}

			std::shared_ptr<Drawable> setPoint(size_t a_index, const DrawPoint& a_value) {
				bool needRefresh = (*points)[a_index].point() != a_value.point();
				(*points)[a_index] = a_value;
				if (needRefresh) {
					refreshBounds();
				}
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> setPoint(size_t a_index, const Color& a_value) {
				if ((*points)[a_index].color() != a_value) {
					(*points)[a_index] = a_value;
					dirtyVertexBuffer = true;
				}
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			//returns a copy of the points list, intentionally not a reference to enforce bounds refreshing.
			std::vector<DrawPoint> getPoints() const {
				return (*points);
			}

			//fine to just pass back a reference, changing the index order doesn't impact the bounding box logic.
			std::vector<GLuint>& pointIndices() {
				return vertexIndices.get();
			}

			std::shared_ptr<Drawable> setPoints(const std::vector<DrawPoint>& a_points, const std::vector<GLuint>& a_vertexIndices) {
				points = a_points;
				vertexIndices = a_vertexIndices;
				refreshBounds();
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> appendPoints(const std::vector<DrawPoint>& a_points, std::vector<GLuint> a_vertexIndices) {
				auto offsetIndex = static_cast<GLuint>(points->size());
				std::transform(a_vertexIndices.begin(), a_vertexIndices.end(), a_vertexIndices.begin(), [&](GLuint index) {return index + offsetIndex; });
				points->insert(points->end(), a_points.begin(), a_points.end());
				vertexIndices->insert(vertexIndices->end(), a_vertexIndices.begin(), a_vertexIndices.end());
				refreshBounds();
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> materialSettings(std::function<void(Shader&)> a_materialSettingsCallback) {
				userMaterialSettings = a_materialSettingsCallback;
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			std::shared_ptr<Drawable> blend(BlendMode a_newPreset) {
				blendModePreset = a_newPreset;
				return std::static_pointer_cast<Drawable>(shared_from_this());
			}

			BlendMode blend() const {
				return blendModePreset;
			}

		protected:
			Drawable(const std::weak_ptr<Node>& a_owner);

			std::map<size_t, std::shared_ptr<TextureHandle>>::const_iterator disconnectTexture(size_t a_textureId);

			virtual bool serializePoints() const { return true; }

			virtual void detachImplementation() override;

			virtual void materialSettingsImplementation(Shader& a_shaderProgram);

			// Runs material/userMaterialSettings; records into the shader's uniform set on the device
			// path, or issues GL live on the legacy path. Shared by both branches of the draw impl.
			void applyMaterialPass(Draw2D& a_renderer);

			// Create or resize+upload the device vertex+index buffers from points/vertexIndices
			// (recreate on byte-size change since updateBuffer can't grow; else honor the dirty flags).
			void syncDeviceBuffers(Render::Device* a_device, Render::BufferUpdateHint a_hint);

			void addTexturesToShader();

			virtual void onRemoved() {
				notifyParentOfBoundsChange();
			}

			virtual BoxAABB<> boundsImplementation() override {
				return localBounds;
			}
			virtual void boundsImplementation(const BoxAABB<>& a_bounds) override;

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

			template<class Archive>
			void save(Archive& archive, std::uint32_t const /*version*/) const {
				// Honour serializePoints(): Emitter, Path, Spine etc. override serializePoints()=false because their
				// geometry is procedural/computed and must not be saved (they have 10k+ points).
				// Temporarily suppress serialization so property_mgr.save() skips them.
				const auto pointsMode = points.get_serialize_mode();
				const auto indicesMode = vertexIndices.get_serialize_mode();
				if (!serializePoints()) {
					points.set_serialize_mode(jai::serialize_mode::transient);
					vertexIndices.set_serialize_mode(jai::serialize_mode::transient);
				}
				property_mgr.save(archive);
				points.set_serialize_mode(pointsMode);
				vertexIndices.set_serialize_mode(indicesMode);
				Component::save(archive, 0);
			}

			template<class Archive>
			void load(Archive& archive, std::uint32_t const version) {
				property_mgr.load(archive);
				Component::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Drawable>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node>& a_parent) {
				return cloneHelper(a_parent->attach<Drawable>().self());
			}

			virtual void postLoadInitialize() override;

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component>& a_clone);

			JAI_NAMED_PROPERTY((std::map<size_t, std::shared_ptr<TextureHandle>>), "textures", ourTextures, [](auto& source, auto& destination) {
				destination->clear();
				for (auto&& kv : source) {
					(*destination)[kv.first] = kv.second->clone();
				}
			});

			JAI_PROPERTY((std::vector<DrawPoint>), points, {});
			JAI_PROPERTY((std::vector<GLuint>), vertexIndices, {});

			JAI_TRANSIENT_PROPERTY((BoxAABB<>), localBounds);  // derived: recomputed from points

			std::shared_ptr<Shader> shaderProgram = nullptr;
			JAI_PROPERTY((std::string), shaderProgramId, PREMULTIPLY_ID);

			std::shared_ptr<Material> materialInstance = nullptr;
			JAI_PROPERTY((std::string), materialId, "");
			
			bool dirtyVertexBuffer = true;

			// --- RHI device-path caches (parallel to dirtyVertexBuffer). ---
			// Per-drawable buffers + a uniform set reused across frames (re-recorded each draw) so the
			// backend's set pool doesn't grow per-frame; pipeline is borrowed from Draw2D's cache.
			Render::BoundBuffer     deviceVertexBuffer;
			Render::BoundBuffer     deviceIndexBuffer;
			// Allocated size of each buffer: updateBuffer can't grow one, so recreate when the count changes; equal size => in-place.
			size_t                  deviceVertexBufferBytes = 0;
			size_t                  deviceIndexBufferBytes = 0;
			Render::BoundUniformSet deviceUniformSet;
			Render::BoundPipeline   devicePipeline;        // borrowed from Draw2D::pipelineFor (do not destroy).
			Render::BoundPipeline   cachedPipelineForKey;  // == devicePipeline when the key below is current.
			BlendMode               cachedBlendMode = BLEND_DEFAULT;
			GLenum                  cachedTopologyDrawType = 0; // 0 == no pipeline resolved yet.
			uint8_t                 devicePipelineColorWriteMask = 0xF; // Stencil sets 0 to draw its mask without color.
			uint8_t                 cachedColorWriteMask = 0xF;
			bool                    dirtyIndexBuffer = true;
			// Lets ~Drawable free its device buffers without calling owner() (which throws during teardown).
			Render::Device*         deviceForCleanup = nullptr;

			JAI_DELETED_PROPERTY((std::shared_ptr<TextureHandle>), ourTexture);

			std::map<size_t, TextureHandle::SignalType::shared_type> textureSizeSignals;

			JAI_PROPERTY((GLenum), drawType, GL_TRIANGLES);

			JAI_PROPERTY((bool), shouldDraw, true);

			virtual void initialize() override;

			jai::property<Anchors> ourAnchors;
			std::vector<Anchors*> childAnchors;

			std::function<void(Shader&)> userMaterialSettings;

			void hookupTextureSizeWatchers();
			void hookupTextureSizeWatcher(size_t a_textureId);

			JAI_NAMED_PROPERTY((BlendMode), "blendMode", blendModePreset, BLEND_DEFAULT);

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

		template<typename Archive>
		void save(Archive& ar, const Anchors& anchors) requires jai::serialization::jai_archive<Archive> {
			// Anchor parents resolve by component id at postLoadInitialize; a parent outside the saved subtree must carry an id to survive.
			std::string parentId;
			if (auto lockedParent = anchors.parentReference.lock()) {
				parentId = lockedParent->id();
			}
			ar(jai::serialization::make_nvp("parentId", parentId));
			ar(jai::serialization::make_nvp("anchors", anchors.parentAnchors));
			ar(jai::serialization::make_nvp("offset", anchors.ourOffset));
			ar(jai::serialization::make_nvp("pivot", anchors.pivotPercent));
			ar(jai::serialization::make_nvp("applyingPosition", anchors.applyingPosition));
		}

		template<typename Archive>
		void load(Archive& ar, Anchors& anchors) requires jai::serialization::jai_archive<Archive> {
			if (ar.has_property("parentId")) { // older jai scenes predate anchor-parent persistence
				ar(jai::serialization::make_nvp("parentId", anchors.parentIdLoaded));
			}
			ar(jai::serialization::make_nvp("anchors", anchors.parentAnchors));
			ar(jai::serialization::make_nvp("offset", anchors.ourOffset));
			ar(jai::serialization::make_nvp("pivot", anchors.pivotPercent));
			ar(jai::serialization::make_nvp("applyingPosition", anchors.applyingPosition));
		}

	}
}

#endif
