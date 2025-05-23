#include "spineMV.h"
#include "MVAdapters/spineBinder.h" //externally bind spine customization points

#include "sprite.h"

#include <fstream>
#include <string>

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Spine);
CEREAL_REGISTER_DYNAMIC_INIT(mv_scenespine);


namespace MV{
	void initializeSpineBindings() {
		static std::map<std::string, std::pair<MV::FileTextureDefinition*, size_t>> SHARED_SPINE_TEXTURES;
		static std::mutex SHARED_SPINE_TEXTURES_MUTEX;

		//these implementations are required for spine!
		Spine_CreateTextureHandler = [&](spAtlasPage* self, const char* path) {
			std::lock_guard<std::mutex> guard(SHARED_SPINE_TEXTURES_MUTEX);
			std::pair<MV::FileTextureDefinition*, size_t>& texturePair = SHARED_SPINE_TEXTURES[path];
			if (texturePair.second == 0) {
				texturePair.first = MV::FileTextureDefinition::makeUnmanaged(path).release();
				texturePair.first->load();
			}
			++texturePair.second;
			self->width = texturePair.first->size().width;
			self->height = texturePair.first->size().height;
			self->rendererObject = texturePair.first;
		};

		Spine_DisposeTextureHandler = [&](spAtlasPage* self) {
			std::lock_guard<std::mutex> guard(SHARED_SPINE_TEXTURES_MUTEX);
			auto* texture = static_cast<MV::FileTextureDefinition*>(self->rendererObject);
			std::pair<MV::FileTextureDefinition*, size_t>& texturePair = SHARED_SPINE_TEXTURES[texture->name()];
			if (--texturePair.second == 0) {
				SCOPE_EXIT{ delete texturePair.first; };
				texturePair.first->unload();
			}
		};

		Spine_ReadFileHandler = [&](const char* path, int* length) -> char* {
			std::string result = MV::fileContents(path);
			if (result.empty()) {
				*length = 0;
				return nullptr;
			}
			*length = static_cast<int>(result.size() + 1);
			char* ptr = new char[result.size() + 1];
			strcpy(ptr, result.c_str());

			return ptr;
		};
	}

	namespace Scene {

		void spineAnimationCallback(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event) {
			if (a_state && a_state->rendererObject) {
				static_cast<Spine*>(a_state->rendererObject)->onAnimationStateEvent(a_state, a_type, a_entry, a_event);
			}
		}

		void spineTrackEntryCallback(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event) {
			if (a_state && a_state->rendererObject) {
				auto *spine = static_cast<Spine*>(a_state->rendererObject);
				if (spine) {
					spine->track(a_entry->trackIndex).onAnimationStateEvent(a_state, a_type, a_entry, a_event);
				}
			}
		}

		template <typename T>
		FileTextureDefinition* getSpineTexture(T* attachment) {
			spAtlasRegion* atlasRegion = (attachment && attachment->rendererObject) ? static_cast<spAtlasRegion*>(attachment->rendererObject) : nullptr;
			return (atlasRegion && atlasRegion->page && atlasRegion->page->rendererObject) ? static_cast<FileTextureDefinition*>(atlasRegion->page->rendererObject) : nullptr;
		}

		void Spine::initialize() {
			Drawable::initialize();
			loadImplementation(fileBundle, false);
		}

		Spine::Spine(const std::weak_ptr<Node> &a_owner, const FileBundle &a_fileBundle) :
			Drawable(a_owner),
			autoUpdate(true),
			fileBundle(a_fileBundle),
			onStart(onStartSignal),
			onEnd(onEndSignal),
			onComplete(onCompleteSignal),
			onDispose(onDisposeSignal),
			onEvent(onEventSignal) {
		}

		Spine::Spine(const std::weak_ptr<Node> &a_owner) :
			Drawable(a_owner),
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
			if (a_fileBundle.skeletonFile != "") {
				atlas = spAtlas_createFromFile(a_fileBundle.atlasFile.c_str(), 0);
				require<ResourceException>(atlas, "Error reading atlas file:", a_fileBundle.atlasFile);

				spSkeletonJson* json = spSkeletonJson_create(atlas);
				json->scale = a_fileBundle.loadScale;
				spSkeletonData* skeletonData = spSkeletonJson_readSkeletonDataFile(json, a_fileBundle.skeletonFile.c_str());
				if (!skeletonData && json->error) {
					require<ResourceException>(false, json->error);
				}
				else if (!skeletonData) {
					require<ResourceException>(false, "Error reading skeleton data file: ", a_fileBundle.skeletonFile);
				}

				//TODO <low>: maybe cache instead of disposing to avoid file reads.
				spSkeletonJson_dispose(json);

				skeleton = spSkeleton_create(skeletonData);

				require<ResourceException>(skeleton && skeleton->bones && skeleton->bones[0], a_fileBundle.skeletonFile, " has no bones!");
				rootBone = skeleton->bones[0];

				animationState = spAnimationState_create(spAnimationStateData_create(skeleton->data));
				animationState->rendererObject = this;
				animationState->listener = spineAnimationCallback;

				spBone_setYDown(true);
				spineWorldVertices = new float[SPINE_MESH_VERTEX_COUNT_MAX]();
				updateImplementation(0.0f);

				for (int i = 0, n = skeleton->slotsCount; i < n; i++) {
					spSlot* slot = skeleton->drawOrder[i];
					if (!slot->attachment) continue;
					FileTextureDefinition *texture = loadSpineSlotIntoPoints(slot);
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
				spSkeleton_update(skeleton, static_cast<float>(a_delta));
				if (animationState) {
					spAnimationState_update(animationState, static_cast<float>(a_delta));
					spAnimationState_apply(animationState, skeleton);
				}
				inUpdate = false;
				if (pendingDelete) {
					pendingDelete = false;
					unloadImplementation();
				} else {
					spSkeleton_updateWorldTransform(skeleton);
				}
			}
		}

		void Spine::unloadImplementation() {
			if (loaded() && !inUpdate) {
				tracks.clear();
				slotsToNodes.clear();
				points.clear();
				vertexIndices.clear();
				fileBundle = FileBundle();
				if (spineWorldVertices) {
					FREE(spineWorldVertices);
				}
				if (atlas) {
					spAtlas_dispose(atlas);
				}
				if (animationState) {
					if (animationState->data) {
						spAnimationStateData_dispose(animationState->data);
					}
					spAnimationState_dispose(animationState);
				}
				if (skeleton) {
					if (skeleton->data) {
						spSkeletonData_dispose(skeleton->data);
					}
					spSkeleton_dispose(skeleton);
				}

				animationState = nullptr;
				skeleton = nullptr;
				atlas = nullptr;

				refreshBounds();
			} else if (loaded() && inUpdate) {
				pendingDelete = true;
			}
		}

		BoxAABB<> Spine::boundsImplementation() {
			if (loaded()) {
				if (points.empty()) {
					points.clear();
					vertexIndices.clear();

					for (int i = 0, n = skeleton->slotsCount; i < n; i++) {
						spSlot* slot = skeleton->drawOrder[i];
						if (slot->attachment) {
							loadSpineSlotIntoPoints(slot);
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
				for (int i = 0, n = skeleton->slotsCount; i < n; i++) {
					spSlot* slot = skeleton->drawOrder[i];
					if (slot->data->name == a_slotId) {
						return { slot->bone->worldX, slot->bone->worldY, 0.0 };
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
			if (animationState) {
				spAnimationStateData_setMixByName(animationState->data, a_fromAnimation.c_str(), a_toAnimation.c_str(), static_cast<float>(a_duration));
			}
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		std::shared_ptr<Spine> Spine::bindNode(const std::string &a_slotId, const std::string &a_nodeId) {
			slotsToNodes[a_slotId].insert(a_nodeId);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}
		std::shared_ptr<Spine> Spine::unbindSlot(const std::string &a_slotId) {
			slotsToNodes.erase(a_slotId);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}
		std::shared_ptr<Spine> Spine::unbindNode(const std::string &a_slotId, const std::string &a_nodeId) {
			auto& slotToNodeBinding = slotsToNodes[a_slotId];
			if (slotToNodeBinding.size() == 1 || slotToNodeBinding.empty()) {
				slotsToNodes.erase(a_slotId);
			} else {
				slotToNodeBinding.erase(a_nodeId);
			}
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		std::shared_ptr<Spine> Spine::unbindAll() {
			slotsToNodes.clear();
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		void Spine::defaultDrawImplementation(){
			if (owner()->renderer().headless()) { return; }

			if (loaded()) {
				if (bufferId == 0) {
					glGenBuffers(1, &bufferId);
				}

				glBindBuffer(GL_ARRAY_BUFFER, bufferId);

				points.clear();
				vertexIndices.clear();
				
				FileTextureDefinition *previousTexture = nullptr;
				spBlendMode previousBlending = SP_BLEND_MODE_NORMAL;
				size_t lastRenderedIndex = 0;
				std::vector<std::string> renderedChildren;
				bool firstSlotTexture = true;
				for (int i = 0, n = skeleton->slotsCount; i < n; i++) {
					spSlot* slot = skeleton->drawOrder[i];
					if (slot->attachment) {
						FileTextureDefinition *texture = getSpineTextureFromSlot(slot);
						if (firstSlotTexture) {
							firstSlotTexture = false;
							previousTexture = texture;
							previousBlending = slot->data->blendMode;
						}

						if (skeletonRenderStateChangedSinceLastIteration(previousBlending, slot->data->blendMode, previousTexture, texture)) {
							lastRenderedIndex = renderSkeletonBatch(lastRenderedIndex, (previousTexture && previousTexture->loaded()) ? previousTexture->textureId() : 0, previousBlending);
						}

						loadSpineSlotIntoPoints(slot);
						previousTexture = texture;
						previousBlending = slot->data->blendMode;
					}
					auto nodesToRender = slotsToNodes.find(slot->data->name);
					if (nodesToRender != slotsToNodes.end()) {
						bool interrupted = false;
						for (auto&& nodeToRender : nodesToRender->second) {
							auto node = owner()->get(nodeToRender, false);
							if (node) {
								if (!interrupted) {
									interrupted = true;
									lastRenderedIndex = renderSkeletonBatch(lastRenderedIndex, (previousTexture && previousTexture->loaded()) ? previousTexture->textureId() : 0, previousBlending);
								}
								node->silence()->
									position({ slot->bone->worldX, slot->bone->worldY })->
									rotation({ 0.0f, spBone_getWorldRotationY(slot->bone), spBone_getWorldRotationX(slot->bone) })->
									scale({ spBone_getWorldScaleX(slot->bone), spBone_getWorldScaleY(slot->bone) });
								node->draw();
								renderedChildren.push_back(node->id());
							}
						}
					}
				}

				if (lastRenderedIndex != vertexIndices.size()) {
					renderSkeletonBatch(lastRenderedIndex, (previousTexture && previousTexture->loaded()) ? previousTexture->textureId() : 0, previousBlending);
				}

				for (auto&& node : *owner()) {
					if (std::find(renderedChildren.begin(), renderedChildren.end(), node->id()) == renderedChildren.end()) {
						node->draw();
					}
				}

				refreshBounds();
			}
		}

		void Spine::applySpineBlendMode(spBlendMode a_spineBlendMode) {
			if (a_spineBlendMode == SP_BLEND_MODE_ADDITIVE) {
				owner()->renderer().setBlendFunction(GL_SRC_ALPHA, GL_ONE);
			}
			else if (a_spineBlendMode == SP_BLEND_MODE_MULTIPLY) {
				owner()->renderer().setBlendFunction(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
			}
			else if (a_spineBlendMode == SP_BLEND_MODE_SCREEN) {
				owner()->renderer().setBlendFunction(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
			}
		}

		size_t Spine::renderSkeletonBatch(size_t a_lastRenderedIndex, GLuint a_textureId, spBlendMode a_blendMode) {
			auto structSize = static_cast<GLsizei>(sizeof(points[0]));
			auto bufferSizeToRender = (points.size()) * structSize;

			if(bufferSizeToRender > 0){
				auto ourOwner = owner();
				shaderProgram->use();
				SCOPE_EXIT{ glUseProgram(0); };
				SCOPE_EXIT{ ourOwner->renderer().defaultBlendFunction(); };
				applySpineBlendMode(a_blendMode);
				glBufferData(GL_ARRAY_BUFFER, bufferSizeToRender, &(points[0]), GL_STATIC_DRAW);

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);

				auto positionOffset = static_cast<size_t>(offsetof(DrawPoint, x));
				auto textureOffset = static_cast<size_t>(offsetof(DrawPoint, textureX));
				auto colorOffset = static_cast<size_t>(offsetof(DrawPoint, R));
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)positionOffset); //Point
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)textureOffset); //UV
				glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)colorOffset); //Color

				TransformMatrix transformationMatrix(ourOwner->renderer().cameraProjectionMatrix(ourOwner->cameraId()) * ourOwner->worldTransform());

				shaderProgram->set("texture0", a_textureId);
				shaderProgram->set("transformation", transformationMatrix);

				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(vertexIndices.size() - a_lastRenderedIndex), GL_UNSIGNED_INT, &vertexIndices[a_lastRenderedIndex]);

				glDisableVertexAttribArray(0);
				glDisableVertexAttribArray(1);
				glDisableVertexAttribArray(2);
				return vertexIndices.size();
			}
			return a_lastRenderedIndex;
		}

		std::shared_ptr<Component> Spine::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Drawable::cloneHelper(a_clone);
			auto spineClone = std::static_pointer_cast<Spine>(a_clone);
			spineClone->slotsToNodes = slotsToNodes;
			return a_clone;
		}

		bool Spine::skeletonRenderStateChangedSinceLastIteration(spBlendMode a_previousBlending, spBlendMode a_currentBlending, FileTextureDefinition * a_previousTexture, FileTextureDefinition * a_texture) {
			return !points.empty() &&
				(a_previousBlending != a_currentBlending) ||
				(a_previousTexture != a_texture);
		}

		FileTextureDefinition * Spine::getSpineTextureFromSlot(spSlot* slot) const {
			if (slot->attachment->type == SP_ATTACHMENT_REGION) {
				spRegionAttachment* attachment = reinterpret_cast<spRegionAttachment*>(slot->attachment);
				return getSpineTexture(attachment);
			} else if (slot->attachment->type == SP_ATTACHMENT_MESH) {
				spMeshAttachment* attachment = reinterpret_cast<spMeshAttachment*>(slot->attachment);
				return getSpineTexture(attachment);
			}
			return nullptr;
		}
		
		FileTextureDefinition * Spine::loadSpineSlotIntoPoints(spSlot* slot) {
			if(slot->attachment->type == SP_ATTACHMENT_REGION){
				spRegionAttachment* attachment = reinterpret_cast<spRegionAttachment*>(slot->attachment);
				Color attachmentColor(skeleton->color.r * slot->color.r * attachment->color.r, skeleton->color.g * slot->color.g * attachment->color.g, skeleton->color.b * slot->color.b * attachment->color.b, skeleton->color.a * slot->color.a * attachment->color.a);
				spRegionAttachment_computeWorldVertices(attachment, slot->bone, spineWorldVertices, 0, 2);

				std::vector<std::pair<GLint, GLint>> spineVertexIndices{
					{0, 1},
					{2, 3},
					{4, 5},
					{6, 7},
				};

				MV::Scene::appendQuadVertexIndices(vertexIndices, static_cast<GLuint>(points.size()));
				for(auto &indexPair : spineVertexIndices){
					Point<> testPosition(spineWorldVertices[indexPair.first], spineWorldVertices[indexPair.second]);
					points.push_back(DrawPoint(
						testPosition,
						attachmentColor,
						TexturePoint(attachment->uvs[indexPair.first], attachment->uvs[indexPair.second])
						));
				}

				return getSpineTexture(attachment);
			} else if(slot->attachment->type == SP_ATTACHMENT_MESH){
				spMeshAttachment* attachment = reinterpret_cast<spMeshAttachment*>(slot->attachment);
				Color attachmentColor(skeleton->color.r * slot->color.r * attachment->color.r, skeleton->color.g * slot->color.g * attachment->color.g, skeleton->color.b * slot->color.b * attachment->color.b, skeleton->color.a * slot->color.a * attachment->color.a);
				require<ResourceException>(attachment->super.verticesCount <= SPINE_MESH_VERTEX_COUNT_MAX, "VerticesCount exceeded for spine mesh.");
				spVertexAttachment_computeWorldVertices(SUPER(attachment), slot, 0, attachment->super.worldVerticesLength, spineWorldVertices, 0, 2);

				for(int i = 0; i < attachment->trianglesCount; ++i) {
					auto index = attachment->triangles[i] << 1;
					points.push_back(DrawPoint(
						Point<>(spineWorldVertices[index], spineWorldVertices[index + 1]),
						attachmentColor,
						TexturePoint(attachment->uvs[index], attachment->uvs[index + 1])
						));
					vertexIndices.push_back(static_cast<GLuint>(points.size() - 1));
				}

				return getSpineTexture(attachment);
			}
			return nullptr;
		}

		std::shared_ptr<Spine> Spine::timeScale(double a_timeScale){
			animationState->timeScale = static_cast<float>(a_timeScale);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		double Spine::timeScale() const {
			return static_cast<double>(animationState->timeScale);
		}

		const double Spine::TIME_BETWEEN_UPDATES = 1.0 / 60.0;


		Spine::FileBundle::FileBundle(const std::string &a_skeletonFile, const std::string &a_atlasFile, float a_loadScale /*= 1.0f*/):
			skeletonFile(a_skeletonFile),
			atlasFile(a_atlasFile),
			loadScale(a_loadScale) {
		}

		Spine::FileBundle::FileBundle():
			loadScale(1.0f) {
		}

		//log("%d event: %s, %s: %d, %f, %s", trackIndex, animationName, event->data->name, event->intValue, event->floatValue, event->stringValue);
		void Spine::onAnimationStateEvent(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event) {
			track(a_entry->trackIndex).recentTrack = a_entry;
			SCOPE_EXIT{ 
				track(a_entry->trackIndex).recentTrack = nullptr; 
			};
			if (a_type == SP_ANIMATION_DISPOSE) {
				onDisposeSignal(this, a_entry->trackIndex);
			} else {
				if (destroying) {
					return;
				}
				auto self = std::static_pointer_cast<Spine>(shared_from_this());
				if (a_type == SP_ANIMATION_START) {
					onStartSignal(self, a_entry->trackIndex);
				} else if (a_type == SP_ANIMATION_END) {
					onEndSignal(self, a_entry->trackIndex);
				} else if (a_type == SP_ANIMATION_COMPLETE) {
					onCompleteSignal(self, a_entry->trackIndex, a_entry->timelinesRotationCount);
					if (!a_entry->loop) {
						track(a_entry->trackIndex).stop();
					}
				} else if (a_type == SP_ANIMATION_INTERRUPT) {
					//onCompleteSignal(this, a_entry->trackIndex, -1);
				} else if (a_type == SP_ANIMATION_EVENT) {
					onEventSignal(self, a_entry->trackIndex, AnimationEventData((a_event->data && a_event->data->name) ? a_event->data->name : "NULL", (a_event->stringValue) ? a_event->stringValue : "", a_event->intValue, a_event->floatValue));
				}
			}
		}

		bool Spine::preDraw() {
			return shouldDraw;
		}

		bool Spine::postDraw() {
			return !shouldDraw;
		}

		void AnimationTrack::onAnimationStateEvent(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event) {
			if(a_entry->trackIndex == myTrackIndex){
				recentTrack = a_entry;
				SCOPE_EXIT{ 
					recentTrack = nullptr; 
				};
				if (a_type == SP_ANIMATION_DISPOSE) {
					onDisposeSignal(*this);
				} else {
					if (destroying) {
						return;
					}
					if (a_type == SP_ANIMATION_START) {
						onStartSignal(*this);
					} else if (a_type == SP_ANIMATION_END) {
						onEndSignal(*this);
					} else if (a_type == SP_ANIMATION_COMPLETE) {
						onCompleteSignal(*this, a_entry->timelinesRotationCount);
						if (!a_entry->loop) {
							stop();
						}
					} else if (a_type == SP_ANIMATION_INTERRUPT) {
						//onCompleteSignal(*this, -1);
					} else if (a_type == SP_ANIMATION_EVENT) {
						std::cout << "EVENT: " << a_entry->animation->name << ":" << ((a_event->data && a_event->data->name) ? a_event->data->name : "NULL") << "\n";
						onEventSignal(*this, AnimationEventData((a_event->data && a_event->data->name) ? a_event->data->name : "NULL", (a_event->stringValue) ? a_event->stringValue : "", a_event->intValue, a_event->floatValue));
					}
				}
			}
		}

		AnimationTrack& AnimationTrack::animate(const std::string &a_animationName, bool a_loop){
			require<ResourceException>(skeleton, "Spine asset not loaded, cannot call animate.");
			spAnimation* animation = spSkeletonData_findAnimation(skeleton->data, a_animationName.c_str());
			if(animation){
				auto* entry = spAnimationState_setAnimation(animationState, myTrackIndex, animation, a_loop);
				if(entry && !entry->rendererObject){
					entry->rendererObject = this;
					entry->listener = spineTrackEntryCallback;
				}
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
			spAnimation* animation = spSkeletonData_findAnimation(skeleton->data, a_animationName.c_str());
			if(animation){
				auto* entry = spAnimationState_addAnimation(animationState, myTrackIndex, animation, a_loop, static_cast<float>(a_delay));
				if(entry && !entry->rendererObject){
					entry->rendererObject = this;
					entry->listener = spineTrackEntryCallback;
				}
			} else{
				std::cerr << "Failed to find animation: " << a_animationName << std::endl;
			}
			return *this;
		}

		AnimationTrack& AnimationTrack::stop(){
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call stop.");
			spAnimationState_clearTrack(animationState, myTrackIndex);
			return *this;
		}

		AnimationTrack& AnimationTrack::time(double a_newTime){
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call time.");
			auto *foundTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			if (foundTrack) {
				foundTrack->trackTime = static_cast<float>(a_newTime);
			}
			return *this;
		}
		double AnimationTrack::time() const{
			auto *foundTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			return foundTrack ? static_cast<double>(foundTrack->trackTime) : 0.0;
		}

		AnimationTrack& AnimationTrack::crossfade(double a_newTime){
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call crossfade.");
			auto *trackState = spAnimationState_getCurrent(animationState, myTrackIndex);
			if (trackState) {
				trackState->mixDuration = static_cast<float>(a_newTime);
			}
			return *this;
		}
		double AnimationTrack::crossfade() const{
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call crossfade.");
			auto *foundTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			return foundTrack ? static_cast<double>(foundTrack->mixDuration) : 0.0;
		}

		AnimationTrack& AnimationTrack::timeScale(double a_newTime){
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call timescale.");
			auto *foundTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			if (foundTrack) {
				foundTrack->timeScale = static_cast<float>(a_newTime);
			}
			return *this;
		}
		double AnimationTrack::timeScale() const{
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call timescale.");
			auto *foundTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			return foundTrack ? static_cast<double>(foundTrack->timeScale) : 1.0;
		}

		std::string AnimationTrack::name() const {
			if (recentTrack) {
				return std::string(recentTrack->animation->name);
			}
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call name.");
			auto *currentTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			return (currentTrack && currentTrack->animation && currentTrack->animation->name) ? currentTrack->animation->name : "NULL";
		}

		double AnimationTrack::duration() const {
			if (recentTrack) {
				return recentTrack->animation->duration;
			}
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call duration.");
			auto *currentTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			return (currentTrack && currentTrack->animation) ? currentTrack->animation->duration : 0.0f;
		}

		bool AnimationTrack::looping() const {
			if (recentTrack) {
				return recentTrack->loop;
			}
			require<ResourceException>(animationState, "Spine asset not loaded, cannot call name.");
			auto *currentTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			return currentTrack && currentTrack->loop;
		}

	}
}
