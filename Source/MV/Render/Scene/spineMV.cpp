#include "spineMV.h"

#include "sprite.h"

#include <fstream>
#include <string>
#include <cstring>
#include <mutex>

#include <spine/spine.h>

#include "MV/Utility/scopeGuard.hpp"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/signals/signal_binding.hpp>
#include "MV/Utility/services.hpp"

static jai::registrar<MV::Scene::AnimationEventData, MV::Services> _hookAnimationEventData("AnimationEventData",
	[](jai::dynamic_binder<MV::Scene::AnimationEventData>& builder, const MV::Services&) {
	builder.property("name", &MV::Scene::AnimationEventData::name);
	builder.property("stringValue", &MV::Scene::AnimationEventData::stringValue);
	builder.property("intValue", &MV::Scene::AnimationEventData::intValue);
	builder.property("floatValue", &MV::Scene::AnimationEventData::floatValue);
});

static jai::registrar<MV::Scene::AnimationTrack, MV::Services> _hookAnimationTrack("AnimationTrack",
	[](jai::dynamic_binder<MV::Scene::AnimationTrack>& builder, const MV::Services&) {
	builder.method("name", &MV::Scene::AnimationTrack::name);
	builder.method("duration", &MV::Scene::AnimationTrack::duration);
	builder.method("looping", &MV::Scene::AnimationTrack::looping);
	builder.method("trackIndex", &MV::Scene::AnimationTrack::trackIndex);
});

static jai::registrar<MV::Scene::Spine, MV::Services> _hookSpine("Spine",
	[](jai::dynamic_binder<MV::Scene::Spine>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Drawable>();

	auto& eng = builder.bound_engine();
	jai::bind_signal_type<void(std::shared_ptr<MV::Scene::Spine>, int)>(eng, "SignalSpineTrack");
	jai::bind_signal_type<void(std::shared_ptr<MV::Scene::Spine>, int, const MV::Scene::AnimationEventData&)>(eng, "SignalSpineEvent");
	jai::bind_signal_type<void(std::shared_ptr<MV::Scene::Spine>, int, int)>(eng, "SignalSpineComplete");
	jai::bind_signal_type<void(MV::Scene::Spine*, int)>(eng, "SignalSpineDispose");
	builder.property("onStart", [](MV::Scene::Spine& a_self) -> jai::signal<void(std::shared_ptr<MV::Scene::Spine>, int)>& { return a_self.onStart; }, nullptr);
	builder.property("onEnd", [](MV::Scene::Spine& a_self) -> jai::signal<void(std::shared_ptr<MV::Scene::Spine>, int)>& { return a_self.onEnd; }, nullptr);
	builder.property("onEvent", [](MV::Scene::Spine& a_self) -> jai::signal<void(std::shared_ptr<MV::Scene::Spine>, int, const MV::Scene::AnimationEventData&)>& { return a_self.onEvent; }, nullptr);
	builder.property("onComplete", [](MV::Scene::Spine& a_self) -> jai::signal<void(std::shared_ptr<MV::Scene::Spine>, int, int)>& { return a_self.onComplete; }, nullptr);
	builder.property("onDispose", [](MV::Scene::Spine& a_self) -> jai::signal<void(MV::Scene::Spine*, int)>& { return a_self.onDispose; }, nullptr);

	builder.method("bindNode", &MV::Scene::Spine::bindNode);
	builder.method("unbindSlot", &MV::Scene::Spine::unbindSlot);
	builder.method("unbindNode", &MV::Scene::Spine::unbindNode);
	builder.method("unbindAll", &MV::Scene::Spine::unbindAll);
	builder.method("boundSlots", &MV::Scene::Spine::boundSlots);
	builder.method("slotPosition", &MV::Scene::Spine::slotPosition);

	builder.method("timeScale", static_cast<double(MV::Scene::Spine::*)() const>(&MV::Scene::Spine::timeScale));
	builder.method("timeScale", static_cast<std::shared_ptr<MV::Scene::Spine>(MV::Scene::Spine::*)(double)>(&MV::Scene::Spine::timeScale));

	builder.method("crossfade", &MV::Scene::Spine::crossfade);

	builder.method("track", static_cast<MV::Scene::AnimationTrack&(MV::Scene::Spine::*)(int)>(&MV::Scene::Spine::track));
	builder.method("track", static_cast<MV::Scene::AnimationTrack&(MV::Scene::Spine::*)()>(&MV::Scene::Spine::track));
	builder.method("currentTrack", &MV::Scene::Spine::currentTrack);

	builder.method("animate", &MV::Scene::Spine::animate);
	builder.method("queueAnimation", static_cast<std::shared_ptr<MV::Scene::Spine>(MV::Scene::Spine::*)(const std::string&, double, bool)>(&MV::Scene::Spine::queueAnimation));
	builder.method("queueAnimation", static_cast<std::shared_ptr<MV::Scene::Spine>(MV::Scene::Spine::*)(const std::string&, bool)>(&MV::Scene::Spine::queueAnimation));

	builder.method("load", [](MV::Scene::Spine& self, const MV::Scene::Spine::FileBundle& bundle) { return self.load(bundle); });
	builder.method("unload", &MV::Scene::Spine::unload);
	builder.method("loaded", &MV::Scene::Spine::loaded);
	builder.method("bundle", &MV::Scene::Spine::bundle);
});

namespace {
	// File reads go through the engine's search paths (user overrides, Assets/ prefix), not fopen
	class MVSpineExtension : public spine::DefaultSpineExtension {
	protected:
		char* _readFile(const spine::String& a_path, int* a_length) override {
			std::string contents = MV::fileContents(a_path.buffer());
			if (contents.empty()) {
				*a_length = 0;
				return nullptr;
			}
			*a_length = static_cast<int>(contents.size());
			char* data = spine::SpineExtension::alloc<char>(contents.size(), __FILE__, __LINE__);
			std::memcpy(data, contents.data(), contents.size());
			return data;
		}
	};

	// Atlas pages share one texture per path across every skeleton that references it
	class MVSpineTextureLoader : public spine::TextureLoader {
	public:
		void load(spine::AtlasPage& a_page, const spine::String& a_path) override {
			std::lock_guard<std::mutex> guard(mutex);
			auto& texturePair = textures[a_path.buffer()];
			if (texturePair.second == 0) {
				texturePair.first = MV::FileTextureDefinition::makeUnmanaged(a_path.buffer()).release();
				texturePair.first->load();
			}
			++texturePair.second;
			a_page.width = texturePair.first->size().width;
			a_page.height = texturePair.first->size().height;
			a_page.texture = texturePair.first;
		}

		void unload(void* a_texture) override {
			std::lock_guard<std::mutex> guard(mutex);
			auto* texture = static_cast<MV::FileTextureDefinition*>(a_texture);
			if (!texture) { return; }
			auto& texturePair = textures[texture->name()];
			if (--texturePair.second == 0) {
				SCOPE_EXIT{ delete texturePair.first; };
				texturePair.first->unload();
			}
		}

		static MVSpineTextureLoader& instance() {
			static MVSpineTextureLoader loader;
			return loader;
		}
	private:
		std::mutex mutex;
		std::map<std::string, std::pair<MV::FileTextureDefinition*, size_t>> textures;
	};
}

spine::SpineExtension* spine::getDefaultExtension() {
	return new MVSpineExtension();
}

namespace MV{
	void initializeSpineBindings() {
	}

	namespace Scene {

		static FileTextureDefinition* textureOf(spine::TextureRegion* a_region) {
			return a_region ? static_cast<FileTextureDefinition*>(a_region->getRendererObject()) : nullptr;
		}

		void Spine::initialize() {
			Drawable::initialize();
			loadImplementation(*fileBundle, false);
		}

		Spine::Spine(const std::weak_ptr<Node> &a_owner, const FileBundle &a_fileBundle) :
			jai::property_owner<Spine, Drawable>(a_owner),
			autoUpdate(true),
			fileBundle(property_mgr, "fileBundle", a_fileBundle),
			onStart(onStartSignal),
			onEnd(onEndSignal),
			onComplete(onCompleteSignal),
			onDispose(onDisposeSignal),
			onEvent(onEventSignal) {
		}

		Spine::Spine(const std::weak_ptr<Node> &a_owner) :
			jai::property_owner<Spine, Drawable>(a_owner),
			autoUpdate(true),
			onStart(onStartSignal),
			onEnd(onEndSignal),
			onComplete(onCompleteSignal),
			onDispose(onDisposeSignal),
			onEvent(onEventSignal) {
		}

		std::shared_ptr<Spine> Spine::load(const FileBundle &a_fileBundle) {
			loadImplementation(a_fileBundle);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		std::shared_ptr<Spine> Spine::unload() {
			unloadImplementation();
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		bool Spine::loaded() const {
			return skeleton != nullptr;
		}

		void Spine::loadImplementation(const FileBundle &a_fileBundle, bool a_refreshBounds) {
			unloadImplementation();
			if (*a_fileBundle.skeletonFile != "") {
				atlas = new spine::Atlas(spine::String(a_fileBundle.atlasFile->c_str()), &MVSpineTextureLoader::instance());
				if (atlas->getPages().size() == 0) {
					delete atlas;
					atlas = nullptr;
					require<ResourceException>(false, "Error reading atlas file:", *a_fileBundle.atlasFile);
				}

				spine::SkeletonJson json(*atlas);
				json.setScale(*a_fileBundle.loadScale);
				skeletonData = json.readSkeletonDataFile(spine::String(a_fileBundle.skeletonFile->c_str()));
				if (!skeletonData) {
					std::string error = json.getError().buffer() ? json.getError().buffer() : "";
					delete atlas;
					atlas = nullptr;
					require<ResourceException>(false, "Error reading skeleton data file: ", *a_fileBundle.skeletonFile, error.empty() ? "" : " - ", error);
				}

				skeleton = new spine::Skeleton(*skeletonData);
				require<ResourceException>(skeleton->getBones().size() > 0 && skeleton->getBones()[0], *a_fileBundle.skeletonFile, " has no bones!");
				rootBone = skeleton->getBones()[0];

				animationStateData = new spine::AnimationStateData(*skeletonData);
				animationState = new spine::AnimationState(*animationStateData);
				animationState->setListener(static_cast<spine::AnimationStateListenerObject*>(this));

				spine::Bone::setYDown(true);
				updateImplementation(0.0f);

				auto& drawOrder = skeleton->getDrawOrder().getAppliedPose();
				for (size_t i = 0, n = drawOrder.size(); i < n; ++i) {
					if (drawOrder[i]->getAppliedPose().getAttachment()) {
						loadSpineSlotIntoPoints(drawOrder[i]);
					}
				}

				fileBundle = a_fileBundle;
				if (a_refreshBounds) {
					refreshBounds();
				}
			}
		}

		Spine::~Spine() {
			destroying = true;
			for (auto&& animationTrack : tracks) {
				animationTrack.second->destroying = true;
			}
			unloadImplementation();
		}

		void Spine::updateImplementation(double a_delta) {
			if (loaded()) {
				auto liveUntilThisFunctionDies = shared_from_this();
				inUpdate = true;
				skeleton->update(static_cast<float>(a_delta));
				if (animationState) {
					animationState->update(static_cast<float>(a_delta));
					animationState->apply(*skeleton);
				}
				inUpdate = false;
				if (pendingDelete) {
					pendingDelete = false;
					unloadImplementation();
				} else {
					skeleton->updateWorldTransform(spine::Physics_Update);
				}
			}
		}

		void Spine::unloadImplementation() {
			if (loaded() && !inUpdate) {
				tracks.clear();
				slotsToNodes->clear();
				points->clear();
				vertexIndices->clear();
				fileBundle = FileBundle();
				spineWorldVertices.clear();
				delete animationState;
				delete animationStateData;
				delete skeleton;
				delete skeletonData;
				delete atlas;

				animationState = nullptr;
				animationStateData = nullptr;
				skeleton = nullptr;
				skeletonData = nullptr;
				atlas = nullptr;
				rootBone = nullptr;

				refreshBounds();
			} else if (loaded() && inUpdate) {
				pendingDelete = true;
			}
		}

		BoxAABB<> Spine::boundsImplementation() {
			if (loaded()) {
				if (points->empty()) {
					points->clear();
					vertexIndices->clear();

					auto& drawOrder = skeleton->getDrawOrder().getAppliedPose();
					for (size_t i = 0, n = drawOrder.size(); i < n; ++i) {
						if (drawOrder[i]->getAppliedPose().getAttachment()) {
							loadSpineSlotIntoPoints(drawOrder[i]);
						}
					}
				}
				refreshBounds();
				return localBounds;
			} else {
				return {};
			}
		}

		MV::Point<> Spine::slotPosition(const std::string &a_slotId) const {
			if (!owner()->renderer().headless() && loaded() && !a_slotId.empty()) {
				auto& slots = skeleton->getSlots();
				for (size_t i = 0, n = slots.size(); i < n; ++i) {
					if (a_slotId == slots[i]->getData().getName().buffer()) {
						auto& bone = slots[i]->getBone().getAppliedPose();
						return { bone.getWorldX(), bone.getWorldY(), 0.0 };
					}
				}
			}

			return {};
		}

		AnimationTrack& Spine::track(int a_index) {
			require<ResourceException>(loaded(), "Spine asset not loaded, cannot call track.");
			defaultTrack = a_index;
			return track();
		}

		AnimationTrack& Spine::track() {
			require<ResourceException>(loaded(), "Spine asset not loaded, cannot call track.");
			auto found = tracks.find(defaultTrack);
			if (found != tracks.end()) {
				return *found->second;
			} else {
				return *(tracks.emplace(defaultTrack, std::make_unique<AnimationTrack>(defaultTrack, animationState, skeleton)).first->second);
			}
		}

		std::shared_ptr<Spine> Spine::animate(const std::string &a_animationName, bool a_loop) {
			track(defaultTrack).animate(a_animationName, a_loop);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}
		std::shared_ptr<Spine> Spine::queueAnimation(const std::string &a_animationName, double a_delay, bool a_loop) {
			track(defaultTrack).queueAnimation(a_animationName, a_delay, a_loop);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		std::shared_ptr<Spine> Spine::queueAnimation(const std::string &a_animationName, bool a_loop) {
			track(defaultTrack).queueAnimation(a_animationName, a_loop);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		std::shared_ptr<Spine> Spine::crossfade(const std::string &a_fromAnimation, const std::string &a_toAnimation, double a_duration) {
			if (animationStateData) {
				animationStateData->setMix(spine::String(a_fromAnimation.c_str()), spine::String(a_toAnimation.c_str()), static_cast<float>(a_duration));
			}
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		std::shared_ptr<Spine> Spine::bindNode(const std::string &a_slotId, const std::string &a_nodeId) {
			slotsToNodes[a_slotId].insert(a_nodeId);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}
		std::shared_ptr<Spine> Spine::unbindSlot(const std::string &a_slotId) {
			slotsToNodes->erase(a_slotId);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}
		std::shared_ptr<Spine> Spine::unbindNode(const std::string &a_slotId, const std::string &a_nodeId) {
			auto& slotToNodeBinding = slotsToNodes[a_slotId];
			if (slotToNodeBinding.size() == 1 || slotToNodeBinding.empty()) {
				slotsToNodes->erase(a_slotId);
			} else {
				slotToNodeBinding.erase(a_nodeId);
			}
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		std::shared_ptr<Spine> Spine::unbindAll() {
			slotsToNodes->clear();
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		void Spine::defaultDrawImplementation(){
			if (loaded()) {
				deviceBatchCursor = 0;   // batches this frame draw from a fresh slice of the set pool.

				points->clear();
				vertexIndices->clear();

				FileTextureDefinition *previousTexture = nullptr;
				spine::BlendMode previousBlending = spine::BlendMode_Normal;
				size_t lastRenderedIndex = 0;
				std::vector<std::string> renderedChildren;
				bool firstSlotTexture = true;
				auto& drawOrder = skeleton->getDrawOrder().getAppliedPose();
				for (size_t i = 0, n = drawOrder.size(); i < n; ++i) {
					spine::Slot* slot = drawOrder[i];
					if (slot->getAppliedPose().getAttachment()) {
						FileTextureDefinition *texture = getSpineTextureFromSlot(slot);
						spine::BlendMode blending = slot->getData().getBlendMode();
						if (firstSlotTexture) {
							firstSlotTexture = false;
							previousTexture = texture;
							previousBlending = blending;
						}

						if (skeletonRenderStateChangedSinceLastIteration(previousBlending, blending, previousTexture, texture)) {
							lastRenderedIndex = renderSkeletonBatch(lastRenderedIndex, (previousTexture && previousTexture->loaded()) ? previousTexture : nullptr, previousBlending);
						}

						loadSpineSlotIntoPoints(slot);
						previousTexture = texture;
						previousBlending = blending;
					}
					auto nodesToRender = slotsToNodes->find(slot->getData().getName().buffer());
					if (nodesToRender != slotsToNodes->end()) {
						bool interrupted = false;
						for (auto&& nodeToRender : nodesToRender->second) {
							auto node = owner()->get(nodeToRender, false);
							if (node) {
								if (!interrupted) {
									interrupted = true;
									lastRenderedIndex = renderSkeletonBatch(lastRenderedIndex, (previousTexture && previousTexture->loaded()) ? previousTexture : nullptr, previousBlending);
								}
								auto& bone = slot->getBone().getAppliedPose();
								node->silence()->
									position({ bone.getWorldX(), bone.getWorldY() })->
									rotation({ 0.0f, bone.getWorldRotationY(), bone.getWorldRotationX() })->
									scale({ bone.getWorldScaleX(), bone.getWorldScaleY() });
								node->draw();
								renderedChildren.push_back(node->id());
							}
						}
					}
				}

				if (lastRenderedIndex != vertexIndices->size()) {
					renderSkeletonBatch(lastRenderedIndex, (previousTexture && previousTexture->loaded()) ? previousTexture : nullptr, previousBlending);
				}

				for (auto&& node : *owner()) {
					if (std::find(renderedChildren.begin(), renderedChildren.end(), node->id()) == renderedChildren.end()) {
						node->draw();
					}
				}

				refreshBounds();
			}
		}

		// Spine uses the premultiply shader, so additive maps to premultiplied One/One (BLEND_ADD), not the
		// legacy non-premultiplied SrcAlpha/One; normal/multiply/screen map straight across.
		static BlendMode spineBlendToBlendMode(spine::BlendMode a_mode) {
			switch (a_mode) {
				case spine::BlendMode_Additive: return BLEND_ADD;
				case spine::BlendMode_Multiply: return BLEND_MULTIPLY;
				case spine::BlendMode_Screen:   return BLEND_SCREEN;
				default:                        return BLEND_DEFAULT;
			}
		}

		size_t Spine::renderSkeletonBatch(size_t a_lastRenderedIndex, FileTextureDefinition* a_texture, spine::BlendMode a_blendMode) {
			auto structSize = static_cast<GLsizei>(sizeof(points[0]));
			auto bufferSizeToRender = (points->size()) * structSize;
			if (bufferSizeToRender <= 0) { return a_lastRenderedIndex; }

			auto ourOwner = owner();
			Render::Device* dev = ourOwner->renderer().device();
			deviceForCleanup = dev;
			// Grow-only buffer reuse: spine regenerates geometry every frame and uploads the (growing)
			// full vertex/index arrays per batch, drawing only [a_lastRenderedIndex, end). updateBuffer
			// can't grow, so recreate when the data exceeds the allocation; otherwise upload in place.
			const size_t vertexBytes = points->size() * static_cast<size_t>(structSize);
			if (!deviceVertexBuffer.valid() || vertexBytes > deviceVertexBufferBytes) {
				if (deviceVertexBuffer.valid()) { dev->destroyBuffer(deviceVertexBuffer); }
				deviceVertexBuffer = dev->createBuffer(Render::BufferUsage::Vertex, Render::BufferUpdateHint::Stream, points->data(), vertexBytes);
				deviceVertexBufferBytes = vertexBytes;
			} else {
				dev->updateBuffer(deviceVertexBuffer, points->data(), vertexBytes, 0);
			}
			const size_t indexBytes = vertexIndices->size() * sizeof(GLuint);
			if (!deviceIndexBuffer.valid() || indexBytes > deviceIndexBufferBytes) {
				if (deviceIndexBuffer.valid()) { dev->destroyBuffer(deviceIndexBuffer); }
				deviceIndexBuffer = dev->createBuffer(Render::BufferUsage::Index, Render::BufferUpdateHint::Stream, vertexIndices->data(), indexBytes);
				deviceIndexBufferBytes = indexBytes;
			} else {
				dev->updateBuffer(deviceIndexBuffer, vertexIndices->data(), indexBytes, 0);
			}

			Render::BoundPipeline pipeline = ourOwner->renderer().pipelineFor(shaderProgram, spineBlendToBlendMode(a_blendMode), GL_TRIANGLES);
			if (deviceBatchCursor >= deviceBatchSets.size()) {
				deviceBatchSets.push_back(dev->createUniformSet(pipeline));
			}
			Render::BoundUniformSet set = deviceBatchSets[deviceBatchCursor++];

			Render::BoundUniformSet prevRecording = shaderProgram->beginRecording(set);
			shaderProgram->set("texture0", a_texture, 0);
			shaderProgram->set("transformation", TransformMatrix(ourOwner->renderer().cameraProjectionMatrix(ourOwner->cameraId()) * ourOwner->worldTransform()));
			shaderProgram->endRecording(prevRecording);

			Render::DrawItem item;
			item.pipeline = pipeline;
			item.vertexBuffer = deviceVertexBuffer;
			item.indexBuffer = deviceIndexBuffer;
			item.indexType = Render::IndexType::Uint32;
			item.firstIndex = static_cast<uint32_t>(a_lastRenderedIndex);
			item.indexCount = static_cast<uint32_t>(vertexIndices->size() - a_lastRenderedIndex);
			item.uniforms = set;
			dev->draw(item);
			return vertexIndices->size();
		}

		std::shared_ptr<Component> Spine::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Drawable::cloneHelper(a_clone);
			auto spineClone = std::static_pointer_cast<Spine>(a_clone);
			spineClone->slotsToNodes = slotsToNodes;
			return a_clone;
		}

		bool Spine::skeletonRenderStateChangedSinceLastIteration(spine::BlendMode a_previousBlending, spine::BlendMode a_currentBlending, FileTextureDefinition * a_previousTexture, FileTextureDefinition * a_texture) {
			return !points->empty() &&
				(a_previousBlending != a_currentBlending) ||
				(a_previousTexture != a_texture);
		}

		FileTextureDefinition * Spine::getSpineTextureFromSlot(spine::Slot* slot) const {
			auto& pose = slot->getAppliedPose();
			spine::Attachment* attachment = pose.getAttachment();
			if (!attachment) { return nullptr; }
			if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
				auto* region = static_cast<spine::RegionAttachment*>(attachment);
				auto& sequence = region->getSequence();
				return textureOf(sequence.getRegion(sequence.resolveIndex(pose)));
			} else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
				auto* mesh = static_cast<spine::MeshAttachment*>(attachment);
				auto& sequence = mesh->getSequence();
				return textureOf(sequence.getRegion(sequence.resolveIndex(pose)));
			}
			return nullptr;
		}

		FileTextureDefinition * Spine::loadSpineSlotIntoPoints(spine::Slot* slot) {
			auto& pose = slot->getAppliedPose();
			spine::Attachment* attachment = pose.getAttachment();
			if (!attachment) { return nullptr; }
			auto tint = [&](const spine::Color& a_attachmentColor) {
				const auto& skeletonColor = skeleton->getColor();
				const auto& slotColor = pose.getColor();
				return Color(skeletonColor.r * slotColor.r * a_attachmentColor.r, skeletonColor.g * slotColor.g * a_attachmentColor.g, skeletonColor.b * slotColor.b * a_attachmentColor.b, skeletonColor.a * slotColor.a * a_attachmentColor.a);
			};
			if (attachment->getRTTI().isExactly(spine::RegionAttachment::rtti)) {
				auto* region = static_cast<spine::RegionAttachment*>(attachment);
				auto& sequence = region->getSequence();
				int sequenceIndex = sequence.resolveIndex(pose);
				auto& uvs = sequence.getUVs(sequenceIndex);
				Color attachmentColor = tint(region->getColor());
				if (spineWorldVertices.size() < 8) { spineWorldVertices.resize(8); }
				region->computeWorldVertices(*slot, region->getOffsets(pose).buffer(), spineWorldVertices.data(), 0, 2);

				MV::Scene::appendQuadVertexIndices(*vertexIndices, static_cast<GLuint>(points->size()));
				for (size_t v = 0; v < 8; v += 2) {
					points->push_back(DrawPoint(
						Point<>(spineWorldVertices[v], spineWorldVertices[v + 1]),
						attachmentColor,
						TexturePoint(uvs[v], uvs[v + 1])
						));
				}

				return textureOf(sequence.getRegion(sequenceIndex));
			} else if (attachment->getRTTI().isExactly(spine::MeshAttachment::rtti)) {
				auto* mesh = static_cast<spine::MeshAttachment*>(attachment);
				auto& sequence = mesh->getSequence();
				int sequenceIndex = sequence.resolveIndex(pose);
				auto& uvs = sequence.getUVs(sequenceIndex);
				Color attachmentColor = tint(mesh->getColor());
				size_t worldVerticesLength = mesh->getWorldVerticesLength();
				if (spineWorldVertices.size() < worldVerticesLength) { spineWorldVertices.resize(worldVerticesLength); }
				mesh->computeWorldVertices(*skeleton, *slot, 0, worldVerticesLength, spineWorldVertices.data(), 0, 2);

				auto& triangles = mesh->getTriangles();
				for (size_t i = 0, n = triangles.size(); i < n; ++i) {
					auto index = static_cast<size_t>(triangles[i]) << 1;
					points->push_back(DrawPoint(
						Point<>(spineWorldVertices[index], spineWorldVertices[index + 1]),
						attachmentColor,
						TexturePoint(uvs[index], uvs[index + 1])
						));
					vertexIndices->push_back(static_cast<GLuint>(points->size() - 1));
				}

				return textureOf(sequence.getRegion(sequenceIndex));
			}
			return nullptr;
		}

		std::shared_ptr<Spine> Spine::timeScale(double a_timeScale){
			animationState->setTimeScale(static_cast<float>(a_timeScale));
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		double Spine::timeScale() const {
			return static_cast<double>(animationState->getTimeScale());
		}

		const double Spine::TIME_BETWEEN_UPDATES = 1.0 / 60.0;

		Spine::FileBundle::FileBundle() {
		}

		Spine::FileBundle::FileBundle(const std::string &a_skeletonFile, const std::string &a_atlasFile, float a_loadScale /*= 1.0f*/) {
			*skeletonFile = a_skeletonFile;
			*atlasFile = a_atlasFile;
			*loadScale = a_loadScale;
		}

		static AnimationEventData eventData(spine::Event* a_event) {
			const char* name = a_event ? a_event->getData().getName().buffer() : nullptr;
			const char* text = a_event ? a_event->getString().buffer() : nullptr;
			return AnimationEventData(name ? name : "NULL", text ? text : "", a_event ? a_event->getInt() : 0, a_event ? a_event->getFloat() : 0.0f);
		}

		// Whole loops completed so far; the pre-4.x integration reported a rotation-timeline count here
		static int completedLoops(spine::TrackEntry* a_entry) {
			auto duration = a_entry->getAnimation().getDuration();
			return duration > 0.0f ? static_cast<int>(a_entry->getTrackTime() / duration) : 0;
		}

		void Spine::callback(spine::AnimationState* /*a_state*/, spine::EventType a_type, spine::TrackEntry* a_entry, spine::Event* a_event) {
			int trackIndex = a_entry->getTrackIndex();
			track(trackIndex).recentTrack = a_entry;
			SCOPE_EXIT{
				track(trackIndex).recentTrack = nullptr;
			};
			if (a_type == spine::EventType_Dispose) {
				onDisposeSignal(this, trackIndex);
			} else {
				if (destroying) {
					return;
				}
				auto self = std::static_pointer_cast<Spine>(shared_from_this());
				if (a_type == spine::EventType_Start) {
					onStartSignal(self, trackIndex);
				} else if (a_type == spine::EventType_End) {
					onEndSignal(self, trackIndex);
				} else if (a_type == spine::EventType_Complete) {
					onCompleteSignal(self, trackIndex, completedLoops(a_entry));
					if (!a_entry->getLoop()) {
						track(trackIndex).stop();
					}
				} else if (a_type == spine::EventType_Event) {
					onEventSignal(self, trackIndex, eventData(a_event));
				}
			}
		}

		bool Spine::preDraw() {
			return shouldDraw;
		}

		bool Spine::postDraw() {
			return !shouldDraw;
		}

		void AnimationTrack::callback(spine::AnimationState* /*a_state*/, spine::EventType a_type, spine::TrackEntry* a_entry, spine::Event* a_event) {
			if(a_entry->getTrackIndex() == myTrackIndex){
				recentTrack = a_entry;
				SCOPE_EXIT{
					recentTrack = nullptr;
				};
				if (a_type == spine::EventType_Dispose) {
					onDisposeSignal(*this);
				} else {
					if (destroying) {
						return;
					}
					if (a_type == spine::EventType_Start) {
						onStartSignal(*this);
					} else if (a_type == spine::EventType_End) {
						onEndSignal(*this);
					} else if (a_type == spine::EventType_Complete) {
						onCompleteSignal(*this, completedLoops(a_entry));
						if (!a_entry->getLoop()) {
							stop();
						}
					} else if (a_type == spine::EventType_Event) {
						onEventSignal(*this, eventData(a_event));
					}
				}
			}
		}

		AnimationTrack& AnimationTrack::animate(const std::string &a_animationName, bool a_loop){
			require<ResourceException>(skeleton, "Spine asset not loaded, cannot call animate.");
			spine::Animation* animation = skeleton->getData().findAnimation(spine::String(a_animationName.c_str()));
			if(animation){
				animationState->setAnimation(static_cast<size_t>(myTrackIndex), *animation, a_loop).setListener(static_cast<spine::AnimationStateListenerObject*>(this));
			} else{
				std::cerr << "Failed to find animation: " << a_animationName << std::endl;
			}
			return *this;
		}

		AnimationTrack& AnimationTrack::queueAnimation(const std::string &a_animationName, bool a_loop) {
			return queueAnimation(a_animationName, 0.0f, a_loop);
		}

		AnimationTrack& AnimationTrack::queueAnimation(const std::string &a_animationName, double a_delay, bool a_loop){
			require<ResourceException>(skeleton, "Spine asset not loaded, cannot call queueAnimation.");
			spine::Animation* animation = skeleton->getData().findAnimation(spine::String(a_animationName.c_str()));
			if(animation){
				animationState->addAnimation(static_cast<size_t>(myTrackIndex), *animation, a_loop, static_cast<float>(a_delay)).setListener(static_cast<spine::AnimationStateListenerObject*>(this));
			} else{
				std::cerr << "Failed to find animation: " << a_animationName << std::endl;
			}
			return *this;
		}

		AnimationTrack& AnimationTrack::stop(){
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call stop.");
			animationState->clearTrack(static_cast<size_t>(myTrackIndex));
			return *this;
		}

		AnimationTrack& AnimationTrack::time(double a_newTime){
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call time.");
			auto *foundTrack = animationState->getTrack(static_cast<size_t>(myTrackIndex));
			if (foundTrack) {
				foundTrack->setTrackTime(static_cast<float>(a_newTime));
			}
			return *this;
		}
		double AnimationTrack::time() const{
			auto *foundTrack = animationState->getTrack(static_cast<size_t>(myTrackIndex));
			return foundTrack ? static_cast<double>(foundTrack->getTrackTime()) : 0.0;
		}

		AnimationTrack& AnimationTrack::crossfade(double a_newTime){
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call crossfade.");
			auto *trackState = animationState->getTrack(static_cast<size_t>(myTrackIndex));
			if (trackState) {
				trackState->setMixDuration(static_cast<float>(a_newTime));
			}
			return *this;
		}
		double AnimationTrack::crossfade() const{
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call crossfade.");
			auto *foundTrack = animationState->getTrack(static_cast<size_t>(myTrackIndex));
			return foundTrack ? static_cast<double>(foundTrack->getMixDuration()) : 0.0;
		}

		AnimationTrack& AnimationTrack::timeScale(double a_newTime){
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call timescale.");
			auto *foundTrack = animationState->getTrack(static_cast<size_t>(myTrackIndex));
			if (foundTrack) {
				foundTrack->setTimeScale(static_cast<float>(a_newTime));
			}
			return *this;
		}
		double AnimationTrack::timeScale() const{
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call timescale.");
			auto *foundTrack = animationState->getTrack(static_cast<size_t>(myTrackIndex));
			return foundTrack ? static_cast<double>(foundTrack->getTimeScale()) : 1.0;
		}

		std::string AnimationTrack::name() const {
			if (recentTrack) {
				return std::string(recentTrack->getAnimation().getName().buffer());
			}
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call name.");
			auto *currentTrack = animationState->getTrack(static_cast<size_t>(myTrackIndex));
			return (currentTrack && currentTrack->getAnimation().getName().buffer()) ? currentTrack->getAnimation().getName().buffer() : "NULL";
		}

		double AnimationTrack::duration() const {
			if (recentTrack) {
				return recentTrack->getAnimation().getDuration();
			}
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call duration.");
			auto *currentTrack = animationState->getTrack(static_cast<size_t>(myTrackIndex));
			return currentTrack ? currentTrack->getAnimation().getDuration() : 0.0f;
		}

		bool AnimationTrack::looping() const {
			if (recentTrack) {
				return recentTrack->getLoop();
			}
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call name.");
			auto *currentTrack = animationState->getTrack(static_cast<size_t>(myTrackIndex));
			return currentTrack && currentTrack->getLoop();
		}

	}
}
