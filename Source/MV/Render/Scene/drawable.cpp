#include "drawable.h"

#include "MV/Utility/scopeGuard.hpp"
#include <jaiscript/serialization/archive.hpp>

#include "MV/Utility/log.h"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

// JaiScript binding for Drawable
static jai::registrar<MV::Scene::Drawable, MV::Services> _hookDrawable("Drawable",
	[](jai::dynamic_binder<MV::Scene::Drawable>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Component>();
	builder.auto_bind();

	// Visibility
	builder.method("visible", &MV::Scene::Drawable::visible);
	builder.method("show", &MV::Scene::Drawable::show);
	builder.method("hide", &MV::Scene::Drawable::hide);

	// Color
	builder.method("color", static_cast<MV::Color(MV::Scene::Drawable::*)() const>(&MV::Scene::Drawable::color));
	builder.method("color", static_cast<std::shared_ptr<MV::Scene::Drawable>(MV::Scene::Drawable::*)(const MV::Color&)>(&MV::Scene::Drawable::color));
	builder.method("colors", &MV::Scene::Drawable::colors);

	// Shader/Material
	builder.method("shader", static_cast<std::string(MV::Scene::Drawable::*)() const>(&MV::Scene::Drawable::shader));
	builder.method("shader", static_cast<std::shared_ptr<MV::Scene::Drawable>(MV::Scene::Drawable::*)(const std::string&)>(&MV::Scene::Drawable::shader));
	builder.method("material", static_cast<std::shared_ptr<MV::Material>(MV::Scene::Drawable::*)() const>(&MV::Scene::Drawable::material));
	builder.method("material", static_cast<std::shared_ptr<MV::Scene::Drawable>(MV::Scene::Drawable::*)(std::shared_ptr<MV::Material>)>(&MV::Scene::Drawable::material));

	// Texture
	builder.method("texture", static_cast<std::shared_ptr<MV::TextureHandle>(MV::Scene::Drawable::*)(size_t) const>(&MV::Scene::Drawable::texture));
	builder.method("texture", static_cast<std::shared_ptr<MV::Scene::Drawable>(MV::Scene::Drawable::*)(std::shared_ptr<MV::TextureHandle>, size_t)>(&MV::Scene::Drawable::texture));
	builder.method("clearTexture", &MV::Scene::Drawable::clearTexture);
	builder.method("clearTextures", &MV::Scene::Drawable::clearTextures);

	// Blend mode
	builder.method("blend", static_cast<MV::BlendMode(MV::Scene::Drawable::*)() const>(&MV::Scene::Drawable::blend));
	builder.method("blend", static_cast<std::shared_ptr<MV::Scene::Drawable>(MV::Scene::Drawable::*)(MV::BlendMode)>(&MV::Scene::Drawable::blend));

	// Points
	builder.method("pointSize", &MV::Scene::Drawable::pointSize);
	builder.method("getPoints", &MV::Scene::Drawable::getPoints);

	// Anchors
	builder.method("anchors", &MV::Scene::Drawable::anchors);
});

namespace MV {
	namespace Scene {

		Drawable::~Drawable() {
			// Free device buffers via the cached device, not owner() (gone during teardown, would throw).
			if (deviceForCleanup) {
				if (deviceVertexBuffer.valid()) { deviceForCleanup->destroyBuffer(deviceVertexBuffer); }
				if (deviceIndexBuffer.valid())  { deviceForCleanup->destroyBuffer(deviceIndexBuffer); }
			}
		}

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
			return std::accumulate(colorsToAverage.begin(), colorsToAverage.end(), Color(0.0f, 0.0f, 0.0f, 0.0f)) / static_cast<PointPrecision>(colorsToAverage.size());
		}

		std::shared_ptr<Drawable> Drawable::color(const Color &a_newColor) {
			for (auto&& point : points) {
				point = a_newColor;
			}
			dirtyVertexBuffer = true;
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> MV::Scene::Drawable::colors(const std::vector<Color>& a_newColors) {
			MV::require<RangeException>(a_newColors.size() == points->size(), "Point and Color vector size mismatch!");
			for (size_t i = 0; i < points->size(); ++i) {
				points[i] = a_newColors[i];
			}
			dirtyVertexBuffer = true;
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> Drawable::hide() {
			std::shared_ptr<Drawable> self = std::static_pointer_cast<Drawable>(shared_from_this());
			if (shouldDraw) {
				shouldDraw = false;
				notifyParentOfComponentChange();
			}
			return self;
		}

		std::shared_ptr<Drawable> MV::Scene::Drawable::show() {
			std::shared_ptr<Drawable> self = std::static_pointer_cast<Drawable>(shared_from_this());
			if (!shouldDraw) {
				shouldDraw = true;
				notifyParentOfComponentChange();
			}
			return self;
		}

		std::shared_ptr<Drawable> Drawable::shader(const std::string &a_shaderProgramId) {
			shaderProgramId = a_shaderProgramId;
			auto self = std::static_pointer_cast<Drawable>(shared_from_this());
			forceInitializeShader();
			return self;
		}

		std::shared_ptr<Drawable> Drawable::material(std::shared_ptr<Material> a_material) {
			materialInstance = a_material;
			if (a_material) {
				materialId = a_material->id();
			} else {
				materialId = "";
			}
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> Drawable::texture(std::shared_ptr<TextureHandle> a_texture, size_t a_textureId) {
			if (!a_texture) {
				clearTexture(a_textureId);
			} else {
				disconnectTexture(a_textureId);

				ourTextures[a_textureId] = a_texture;
				hookupTextureSizeWatcher(a_textureId);
				updateTextureCoordinates(a_textureId);
				rebuildTextureCache();
			}
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::map<size_t, std::shared_ptr<MV::TextureHandle>>::const_iterator Drawable::disconnectTexture(size_t a_textureId) {
			auto found = ourTextures->find(a_textureId);
			if (found != ourTextures.end()) {
				found->second->sizeChange.disconnect(textureSizeSignals[a_textureId]);
			}
			return found;
		}

		std::shared_ptr<Drawable> Drawable::clearTexture(size_t a_textureId) {
			disconnectTexture(a_textureId);
			ourTextures->erase(a_textureId);
			textureSizeSignals.erase(a_textureId);
			clearTextureCoordinates(a_textureId);
			rebuildTextureCache();
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> Drawable::clearTextures() {
			auto texturesToClear = ourTextures.get();
			ourTextures->clear();
			textureSizeSignals.clear();
			for(auto&& kv : texturesToClear){
				kv.second->sizeChange.disconnect(textureSizeSignals[kv.first]);
				clearTextureCoordinates(kv.first);
			}
			rebuildTextureCache();
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		void Drawable::boundsImplementation(const BoxAABB<> &a_bounds) {
			points[0] = a_bounds.minPoint;
			points[1].x = a_bounds.minPoint.x;	points[1].y = a_bounds.maxPoint.y;	points[1].z = (a_bounds.maxPoint.z + a_bounds.minPoint.z) / 2.0f;
			points[2] = a_bounds.maxPoint;
			points[3].x = a_bounds.maxPoint.x;	points[3].y = a_bounds.minPoint.y;	points[3].z = points[1].z;

			refreshBounds();
		}

		// The material/userMaterialSettings pass; shaderProgram->set records into the open uniform set (device) or issues GL (legacy).
		void Drawable::applyMaterialPass(Draw2D &a_renderer) {
			if (materialInstance) {
				try {
					MaterialContext context {
						a_renderer,
						static_cast<float>(accumulatedDelta),
						owner()->worldTransform(),
						owner()->worldAlpha(),
						owner()->cameraId()
					};
					materialInstance->apply(*shaderProgram, context);
				} catch (std::exception &e) {
					MV::error("Drawable::defaultDrawImplementation. Exception in material->apply: ", e.what());
				}
			} else {
				materialSettingsImplementation(*shaderProgram);
				if (userMaterialSettings) {
					try { userMaterialSettings(*shaderProgram); } catch (std::exception &e) { MV::error("Drawable::defaultDrawImplementation. Exception in userMaterialSettings: ", e.what()); }
				}
			}
		}

		void Drawable::syncDeviceBuffers(Render::Device* a_device, Render::BufferUpdateHint a_hint) {
			deviceForCleanup = a_device;   // cached so ~Drawable frees buffers without calling owner()
			const size_t structSize = sizeof(points[0]);

			const size_t vertexBytes = points->size() * structSize;
			if (!deviceVertexBuffer.valid() || vertexBytes != deviceVertexBufferBytes) {
				if (deviceVertexBuffer.valid()) { a_device->destroyBuffer(deviceVertexBuffer); }
				deviceVertexBuffer = a_device->createBuffer(Render::BufferUsage::Vertex, a_hint, points->data(), vertexBytes);
				deviceVertexBufferBytes = vertexBytes;
				dirtyVertexBuffer = false;
			} else if (dirtyVertexBuffer) {
				dirtyVertexBuffer = false;
				a_device->updateBuffer(deviceVertexBuffer, points->data(), vertexBytes, 0);
			}

			// Index buffer: a real GPU buffer (client-side arrays are illegal in Vk/Metal); same resize rule.
			const size_t indexBytes = vertexIndices->size() * sizeof(GLuint);
			if (!deviceIndexBuffer.valid() || indexBytes != deviceIndexBufferBytes) {
				if (deviceIndexBuffer.valid()) { a_device->destroyBuffer(deviceIndexBuffer); }
				deviceIndexBuffer = a_device->createBuffer(Render::BufferUsage::Index, a_hint, vertexIndices->data(), indexBytes);
				deviceIndexBufferBytes = indexBytes;
				dirtyIndexBuffer = false;
			} else if (dirtyIndexBuffer) {
				dirtyIndexBuffer = false;
				a_device->updateBuffer(deviceIndexBuffer, vertexIndices->data(), indexBytes, 0);
			}
		}

		void Drawable::defaultDrawImplementation() {
			auto& ourRenderer = owner()->renderer();
			if (vertexIndices->empty()) { return; }
			require<ResourceException>(shaderProgram, "No shader program for Drawable!");

			Render::Device* dev = ourRenderer.device();
			syncDeviceBuffers(dev, Render::BufferUpdateHint::Dynamic);

			// Resolve the shared pipeline (blend = material override or preset; topology = drawType).
			const BlendMode effectiveBlend = materialInstance ? materialInstance->blend() : blendModePreset;
			if (devicePipeline != cachedPipelineForKey || cachedBlendMode != effectiveBlend
			    || cachedTopologyDrawType != drawType || cachedColorWriteMask != devicePipelineColorWriteMask || !deviceUniformSet.valid()) {
				devicePipeline = ourRenderer.pipelineFor(shaderProgram, effectiveBlend, drawType, devicePipelineColorWriteMask);
				cachedPipelineForKey = devicePipeline;
				cachedBlendMode = effectiveBlend;
				cachedTopologyDrawType = drawType;
				cachedColorWriteMask = devicePipelineColorWriteMask;
				deviceUniformSet = dev->createUniformSet(devicePipeline); // a uniform set is bound to a pipeline.
			}

			// Re-record uniforms+textures (the set is a name-keyed bag). Save/restore around it: a nested
			// draw (mid-draw atlas FBO refill on this same shared Shader) must not clobber the outer set.
			Render::BoundUniformSet prevRecording = shaderProgram->beginRecording(deviceUniformSet);
			applyMaterialPass(ourRenderer);
			shaderProgram->endRecording(prevRecording);

			Render::DrawItem item;
			item.pipeline = devicePipeline;
			item.vertexBuffer = deviceVertexBuffer;
			item.indexBuffer = deviceIndexBuffer;
			item.indexType = Render::IndexType::Uint32;
			item.indexCount = static_cast<uint32_t>(vertexIndices->size());
			item.firstIndex = 0;
			item.baseVertex = 0;
			item.uniforms = deviceUniformSet;
			dev->draw(item);
		}

		void Drawable::refreshBounds() {
			dirtyVertexBuffer = true;
			dirtyIndexBuffer = true;   // refreshBounds runs after setPoints/appendPoints, which can change indices.
			auto originalBounds = *localBounds;
			if (!points->empty()) {
				localBounds->initialize(points[0]);
				for (size_t i = 1; i < points->size(); ++i) {
					localBounds->expandWith(points[i]);
				}
			} else {
				localBounds = BoxAABB<>();
			}
			if (originalBounds != *localBounds) {
				for (auto&& childAnchor : childAnchors) {
					childAnchor->apply();
				}
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
			jai::property_owner<Drawable, Component>(a_owner),
			ourAnchors(property_mgr, "anchors", {this}) {
			points->resize(4);
		}

		void Drawable::detachImplementation() {
			ourAnchors->removeFromParent();
		}

		void Drawable::materialSettingsImplementation(Shader& a_shaderProgram) {
			auto ourOwner = owner();
			a_shaderProgram.set("time", static_cast<PointPrecision>(accumulatedDelta), false); //optional but helpful default
			// Record alpha unconditionally: the device path reuses one uniform set, so a conditional set
			// would let a stale prior-frame alpha persist (alpha==1 is the shader identity anyway).
			a_shaderProgram.set("alpha", ourOwner->worldAlpha(), false);
			addTexturesToShader();

			a_shaderProgram.set("transformation", ourOwner->renderer().cameraProjectionMatrix(ourOwner->cameraId()) * ourOwner->worldTransform());
		}

		void Drawable::addTexturesToShader() {
			static const MV::BoxAABB<> unitBox{ MV::Point<>{}, MV::Point<>{1.0f, 1.0f} };
			if (cachedTextureList.empty()) {
				return;
			}
			auto& bounds = !ourTextures->empty() ? ourTextures.begin()->second->rawPercent() : unitBox;
			if (shaderProgram->setVec2("uvMin", bounds.minPoint, false)) {
				shaderProgram->setVec2("uvMax", bounds.maxPoint, false); //optional but helpful default
			}
			bool firstTexture = true;
			for (auto&& kv : cachedTextureList) {
				shaderProgram->set(kv.first, kv.second, firstTexture);
				firstTexture = false;
			}
		}

		void Drawable::rebuildTextureCache() {
			cachedTextureList.clear();
			std::set<std::shared_ptr<MV::TextureDefinition>> actuallyRegistered;
			for (auto&& kv : ourTextures) {
				if (actuallyRegistered.find(kv.second->texture()) == actuallyRegistered.end()) {
					cachedTextureList.emplace_back("texture" + std::to_string(actuallyRegistered.size()), kv.second->texture());
					actuallyRegistered.insert(kv.second->texture());
				}
			}
			if (actuallyRegistered.empty()) {
				cachedTextureList.emplace_back("texture0", nullptr);
			}
		}

		void Drawable::initialize() {
			for (auto&& kv : ourTextures) {
				texture(kv.second, kv.first);
			}
			if (ourTextures->empty()) {
				hookupTextureSizeWatchers();
				rebuildTextureCache();
			}
		}

		void Drawable::hookupTextureSizeWatchers() {
			for(auto&& kv : ourTextures) {
				size_t theTextureId = kv.first;
				auto signal = TextureHandle::SignalType::make([&, theTextureId](std::shared_ptr<MV::TextureHandle> a_handle) {
					updateTextureCoordinates(theTextureId);
				});
				kv.second->sizeChange.disconnect(textureSizeSignals[kv.first]);
				textureSizeSignals[kv.first] = signal;
				kv.second->sizeChange.connect(signal);
			}
		}

		void Drawable::hookupTextureSizeWatcher(size_t a_textureId) {
			auto signal = TextureHandle::SignalType::make([&, a_textureId](std::shared_ptr<MV::TextureHandle> a_handle) {
				updateTextureCoordinates(a_textureId);
			});
			disconnectTexture(a_textureId);
			textureSizeSignals[a_textureId] = signal;
			ourTextures[a_textureId]->sizeChange.connect(signal);
		}

		void Drawable::postLoadInitialize() {
			ourAnchors->postLoadInitialize();
		}

		std::shared_ptr<Component> Drawable::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Component::cloneHelper(a_clone);
			auto drawableClone = std::static_pointer_cast<Drawable>(a_clone);
			drawableClone->forceInitializeShader();
			drawableClone->notifyParentOfBoundsChange();
			drawableClone->hookupTextureSizeWatchers();
			drawableClone->rebuildTextureCache();
			return a_clone;
		}

		Anchors::Anchors(Drawable *a_self) :
			selfReference(a_self) {
		}

		Anchors::Anchors(const Anchors& a_rhs) :
			parentAnchors(a_rhs.parentAnchors),
			ourOffset(a_rhs.ourOffset),
			pivotPercent(a_rhs.pivotPercent),
			selfReference(a_rhs.selfReference) {

			if (!a_rhs.parentReference.expired() && a_rhs.parentReference.lock() == a_rhs.selfReference->owner()->componentInParents(a_rhs.parentReference.lock()->id(), false, true).self()) {
				parentIdLoaded = a_rhs.parentReference.lock()->id();
			} else {
				parentReference = a_rhs.parentReference;
			}
		}

		Anchors& Anchors::operator=(const Anchors& a_rhs){
			parentAnchors = a_rhs.parentAnchors;
			ourOffset = a_rhs.ourOffset;
			pivotPercent = a_rhs.pivotPercent;

			if (!a_rhs.parentReference.expired() && a_rhs.parentReference.lock() == a_rhs.selfReference->owner()->componentInParents(a_rhs.parentReference.lock()->id(), false, true).self()) {
				parentIdLoaded = a_rhs.parentReference.lock()->id();
			} else {
				parentReference = a_rhs.parentReference;
			}
			return *this;
		}

		Anchors::~Anchors() {
			removeFromParent();
		}

		Anchors& Anchors::parent(const std::weak_ptr<Drawable>& a_parent, bool a_calculateOffsetAndPivot) {
			removeFromParent();
			parentReference = a_parent;

			if (a_calculateOffsetAndPivot) {
				applyingPosition = true;
				calculateOffsetAndPivot();
			}

			registerWithParent();
			return *this;
		}

		std::shared_ptr<MV::Scene::Drawable> Anchors::parent() const {
			return parentReference.lock();
		}

		Anchors& Anchors::anchor(const BoxAABB<> &a_anchor) {
			parentAnchors = a_anchor;
			return *this;
		}

		Anchors& Anchors::anchor(const Point<> &a_anchor) {
			parentAnchors.minPoint = a_anchor;
			parentAnchors.maxPoint = a_anchor;
			return *this;
		}

		Anchors& Anchors::offset(const BoxAABB<> &a_offset) {
			ourOffset = a_offset;
			return *this;
		}

		Anchors& Anchors::pivot(const Point<> &a_pivot) {
			pivotPercent = a_pivot;
			return *this;
		}

		Anchors& Anchors::apply() {
			if (!applying) {
				if (auto lockedParentReference = parentReference.lock()) {
					applying = true;
					SCOPE_EXIT{ applying = false; };
					auto parentBounds = lockedParentReference->worldBounds();
					auto parentSize = parentBounds.size();

					BoxAABB<> childBounds{ (parentAnchors * toScale(parentSize)) + ourOffset + parentBounds.minPoint };
					if (applyingPosition) {
						selfReference->owner()->worldPosition(childBounds.minPoint + (toPoint(childBounds.size()) * pivotPercent));
						selfReference->worldBounds(childBounds);
					} else {
						selfReference->worldBounds(childBounds);
					}
				}
			}
			return *this;
		}

		MV::Scene::Anchors& Anchors::usePosition(bool a_newValue) {
			applyingPosition = a_newValue;
			apply();
			return *this;
		}

		Anchors& Anchors::calculateOffsetAndPivot() {
			if (!applying) {
				if (auto lockedParent = parentReference.lock()) {
					auto childBounds = selfReference->worldBounds();
					auto parentBounds = lockedParent->worldBounds();
					auto parentSize = parentBounds.size();

					auto scaledParentAnchors = parentAnchors * toScale(parentSize);

					auto ourPosition = selfReference->owner()->worldPosition();
					pivotPercent = (ourPosition - childBounds.minPoint) / toPoint(childBounds.size());

					ourOffset = childBounds - (scaledParentAnchors + parentBounds.minPoint);
				}
			}
			return *this;
		}

		Anchors& Anchors::removeFromParent() {
			if (auto parentShared = parentReference.lock()) {
				auto position = std::find(parentShared->childAnchors.begin(), parentShared->childAnchors.end(), this);
				if (position != parentShared->childAnchors.end()) {
					parentShared->childAnchors.erase(position);
				}
				parentReference.reset();
			}
			return *this;
		}

		void Anchors::postLoadInitialize() {
			if (!parentIdLoaded.empty()) {
				auto found = selfReference->owner()->componentInParents(parentIdLoaded, false, true);
				require<ResourceException>(found, "Failed to find Anchor Component Id: [", parentIdLoaded, "] when loading node [", selfReference->owner()->id(), "].[", selfReference->id(), "]");
				parentReference = found.cast<Drawable>().get();
				parentIdLoaded.clear();
			}
			registerWithParent();
		}

		void Anchors::registerWithParent() {
			if (auto lockedParent = parentReference.lock()) {
				lockedParent->childAnchors.push_back(this);
				apply();
			} else {
				ourOffset.clear();
			}
		}

		// JaiScript serialization for Anchors is now inline templated in drawable.h
	}
}
