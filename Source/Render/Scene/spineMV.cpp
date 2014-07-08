#include "Render/Scene/spineMV.h"
#include "spine/spine.h"
#include "spine/extension.h"

#include "Render/Scene/primitives.h"

#include <fstream>
#include <string>

//these implementations are required for spine!
void _spAtlasPage_createTexture(spAtlasPage* self, const char* path){
	auto texture = MV::FileTextureDefinition::makeUnmanaged(path);
	texture->reload();
	self->width = texture->size().width;
	self->height = texture->size().height;
	self->rendererObject = texture.release();
}

void _spAtlasPage_disposeTexture(spAtlasPage* self){
	auto texture = static_cast<MV::FileTextureDefinition*>(self->rendererObject);
	texture->cleanup();
	delete texture;
}

char* _spUtil_readFile(const char* path, int* length){
	*length = 0;
	std::ifstream in(path);

	if(in){
		in.seekg(0, std::ios::end);
		*length = static_cast<int>(in.tellg());
		char* contents = new char[*length];

		in.seekg(0, std::ios::beg);
		in.read(&contents[0], *length);

		return contents;
	}

	return nullptr;
}

CEREAL_REGISTER_TYPE(MV::Scene::Spine);

namespace MV{
	namespace Scene{

		void spineAnimationCallback(spAnimationState* a_state, int a_trackIndex, spEventType a_type, spEvent* a_event, int a_loopCount) {
			if(a_state && a_state->rendererObject){
				static_cast<Spine*>(a_state->rendererObject)->onAnimationStateEvent(a_trackIndex, a_type, a_event, a_loopCount);
			}
		}

		void spineTrackEntryCallback(spAnimationState* a_state, int a_trackIndex, spEventType a_type, spEvent* a_event, int a_loopCount) {
			if(a_state && a_state->rendererObject){
				auto *spine = static_cast<Spine*>(a_state->rendererObject);
				if(spine){
					spine->track(a_trackIndex).onAnimationStateEvent(a_trackIndex, a_type, a_event, a_loopCount);
				}
			}
		}

		template <typename T>
		FileTextureDefinition* getSpineTexture(T* attachment){
			spAtlasRegion* atlasRegion = (attachment && attachment->rendererObject) ? static_cast<spAtlasRegion*>(attachment->rendererObject) : nullptr;
			return (atlasRegion && atlasRegion->page && atlasRegion->page->rendererObject) ? static_cast<FileTextureDefinition*>(atlasRegion->page->rendererObject) : nullptr;
		}

		std::shared_ptr<Spine> MV::Scene::Spine::make(Draw2D* a_renderer, const FileBundle &a_fileBundle){
			auto spine = std::shared_ptr<Spine>(new Spine(a_renderer, a_fileBundle));
			a_renderer->registerShader(spine);
			return spine;
		}

		Spine::Spine(Draw2D *a_renderer, const FileBundle &a_fileBundle):
			Node(a_renderer),
			fileBundle(a_fileBundle),
			autoUpdate(true),
			onStart(onStartSlot),
			onEnd(onEndSlot),
			onComplete(onCompleteSlot),
			onEvent(onEventSlot){

			atlas = spAtlas_createFromFile(fileBundle.atlasFile.c_str(), 0);
			require(atlas, ResourceException("Error reading atlas file:" + fileBundle.atlasFile));

			spSkeletonJson* json = spSkeletonJson_create(atlas);
			json->scale = fileBundle.loadScale;
			spSkeletonData* skeletonData = spSkeletonJson_readSkeletonDataFile(json, fileBundle.skeletonFile.c_str());

			require(skeletonData, ResourceException((json->error ? json->error : "Error reading skeleton data file:") + std::string(" ") + fileBundle.skeletonFile));

			spSkeletonJson_dispose(json);

			skeleton = spSkeleton_create(skeletonData);
			require(skeleton && skeleton->bones && skeleton->bones[0], ResourceException(fileBundle.skeletonFile + " has no bones!"));
			rootBone = skeleton->bones[0];

			animationState = spAnimationState_create(spAnimationStateData_create(skeleton->data));
			animationState->rendererObject = this;
			animationState->listener = spineAnimationCallback;

			spBone_setYDown(true);
			spineWorldVertices = new float[SPINE_MESH_VERTEX_COUNT_MAX]();
			update(0.0f);
		}

		Spine::~Spine() {
			if(atlas){
				spAtlas_dispose(atlas);
			}
			if(skeleton){
				if(skeleton->data){
					spSkeletonData_dispose(skeleton->data);
				}
				spSkeleton_dispose(skeleton);
			}
			if(animationState){
				spAnimationState_dispose(animationState);
			}
			if(spineWorldVertices){
				delete[] spineWorldVertices;
			}
		}

		void Spine::update(double a_delta) {
			if(skeleton){
				spSkeleton_update(skeleton, static_cast<float>(a_delta));
				if(animationState){
					spAnimationState_update(animationState, static_cast<float>(a_delta));
					spAnimationState_apply(animationState, skeleton);
				}
				spSkeleton_updateWorldTransform(skeleton);
			}
		}

		AnimationTrack& Spine::track(int a_index){
			defaultTrack = a_index;
			auto found = tracks.find(a_index);
			if(found != tracks.end()){
				return found->second;
			} else{
				return tracks.emplace(a_index, AnimationTrack(a_index, animationState, skeleton)).first->second;
			}
		}

		std::shared_ptr<Spine> Spine::animate(const std::string &a_animationName, bool a_loop){
			track(defaultTrack).animate(a_animationName, a_loop);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}
		std::shared_ptr<Spine> Spine::queueAnimation(const std::string &a_animationName, bool a_loop, double a_delay){
			track(defaultTrack).queueAnimation(a_animationName, a_loop, a_delay);
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		std::shared_ptr<Spine> Spine::crossfade(const std::string &a_fromAnimation, const std::string &a_toAnimation, double a_duration){
			if(animationState){
				spAnimationStateData_setMixByName(animationState->data, a_fromAnimation.c_str(), a_toAnimation.c_str(), a_duration);
			}
			return std::static_pointer_cast<Spine>(shared_from_this());
		}

		void Spine::drawImplementation(){
			conditionalUpdate();

			shaderProgram->use();
			if(bufferId == 0){
				glGenBuffers(1, &bufferId);
			}

			glBindBuffer(GL_ARRAY_BUFFER, bufferId);

			points.clear();
			vertexIndices.clear();
			FileTextureDefinition *previousTexture = nullptr;
			bool previousBlendingWasAdditive = false;
			size_t lastRenderedIndex = 0;
			for(int i = 0, n = skeleton->slotCount; i < n; i++) {
				spSlot* slot = skeleton->drawOrder[i];
				if(!slot->attachment) continue;
				FileTextureDefinition *texture = loadSpineSlotIntoPoints(slot);

				SCOPE_EXIT{renderer->defaultBlendFunction(); };
				if(slot->data->additiveBlending) {
					renderer->setBlendFunction(GL_SRC_ALPHA, GL_ONE);
				}

				if(skeletonRenderStateChangedSinceLastIteration(previousBlendingWasAdditive, slot->data->additiveBlending != 0, previousTexture, texture)){
					lastRenderedIndex = renderSkeletonBatch(lastRenderedIndex, (texture && texture->loaded()) ? texture->textureId() : 0);
				}
				previousTexture = texture;
				previousBlendingWasAdditive = slot->data->additiveBlending != 0;
			}

			if(lastRenderedIndex != vertexIndices.size()){
				renderSkeletonBatch(lastRenderedIndex, (previousTexture && previousTexture->loaded()) ? previousTexture->textureId() : 0);
			}

			glUseProgram(0);
		}

		size_t Spine::renderSkeletonBatch(size_t a_lastRenderedIndex, GLuint a_textureId) {
			auto structSize = static_cast<GLsizei>(sizeof(points[0]));
			auto bufferSizeToRender = (points.size()) * structSize;

			if(bufferSizeToRender > 0){
				glBufferData(GL_ARRAY_BUFFER, bufferSizeToRender, &(points[0]), GL_STATIC_DRAW);

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);

				auto positionOffset = static_cast<GLsizei>(offsetof(DrawPoint, x));
				auto textureOffset = static_cast<GLsizei>(offsetof(DrawPoint, textureX));
				auto colorOffset = static_cast<GLsizei>(offsetof(DrawPoint, R));
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, structSize, (void*)positionOffset); //Point
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, structSize, (void*)textureOffset); //UV
				glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, structSize, (void*)colorOffset); //Color

				TransformMatrix transformationMatrix(renderer->projectionMatrix().top() * renderer->modelviewMatrix().top());

				shaderProgram->set("texture", a_textureId);
				shaderProgram->set("transformation", transformationMatrix);

				glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(vertexIndices.size() - a_lastRenderedIndex), GL_UNSIGNED_INT, &vertexIndices[a_lastRenderedIndex]);

				glDisableVertexAttribArray(0);
				glDisableVertexAttribArray(1);
				glDisableVertexAttribArray(2);
				return vertexIndices.size();
			}
			return a_lastRenderedIndex;
		}

		bool Spine::skeletonRenderStateChangedSinceLastIteration(bool a_previousBlendingWasAdditive, bool a_currentAdditiveBlending, FileTextureDefinition * a_previousTexture, FileTextureDefinition * a_texture) {
			return !points.empty() &&
				(!a_previousBlendingWasAdditive && a_currentAdditiveBlending) || (a_previousBlendingWasAdditive && !a_currentAdditiveBlending) ||
				(a_previousTexture != nullptr && a_previousTexture != a_texture);
		}

		FileTextureDefinition * Spine::loadSpineSlotIntoPoints(spSlot* slot) {
			Color attachmentColor(skeleton->r * slot->r, skeleton->g * slot->g, skeleton->b * slot->b, skeleton->a * slot->a);
			if(slot->attachment->type == SP_ATTACHMENT_REGION){
				spRegionAttachment* attachment = reinterpret_cast<spRegionAttachment*>(slot->attachment);
				spRegionAttachment_computeWorldVertices(attachment, slot->skeleton->x, slot->skeleton->y, slot->bone, spineWorldVertices);

				std::vector<std::pair<spVertexIndex, spVertexIndex>> spineVertexIndices{
					{SP_VERTEX_X1, SP_VERTEX_Y1},
					{SP_VERTEX_X2, SP_VERTEX_Y2},
					{SP_VERTEX_X3, SP_VERTEX_Y3},
					{SP_VERTEX_X4, SP_VERTEX_Y4},
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
				require(attachment->verticesCount <= SPINE_MESH_VERTEX_COUNT_MAX, ResourceException("VerticesCount exceeded for spine mesh."));
				spMeshAttachment_computeWorldVertices(attachment, slot->skeleton->x, slot->skeleton->y, slot, spineWorldVertices);

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
			} else if(slot->attachment->type == SP_ATTACHMENT_SKINNED_MESH){
				spSkinnedMeshAttachment* attachment = reinterpret_cast<spSkinnedMeshAttachment*>(attachment);
				require(attachment->uvsCount <= SPINE_MESH_VERTEX_COUNT_MAX, ResourceException("UVS exceeded for spine skinned mesh."));
				spSkinnedMeshAttachment_computeWorldVertices(attachment, slot->skeleton->x, slot->skeleton->y, slot, spineWorldVertices);

				for(int i = 0; i < attachment->trianglesCount; ++i) {
					int index = attachment->triangles[i] << 1;
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

		void Spine::conditionalUpdate() {
			if(autoUpdate){
				while(timer.frame(TIME_BETWEEN_UPDATES)){
					update(TIME_BETWEEN_UPDATES);
				}
			}
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
		void Spine::onAnimationStateEvent(int a_trackIndex, spEventType type, spEvent* event, int loopCount) {
			if(type == SP_ANIMATION_START){
				onStartSlot(std::static_pointer_cast<Spine>(shared_from_this()), a_trackIndex);
			} else if(type == SP_ANIMATION_END){
				onEndSlot(std::static_pointer_cast<Spine>(shared_from_this()), a_trackIndex);
			} else if(type == SP_ANIMATION_COMPLETE){
				onCompleteSlot(std::static_pointer_cast<Spine>(shared_from_this()), a_trackIndex, loopCount);
			} else if(type == SP_ANIMATION_EVENT){
				onEventSlot(std::static_pointer_cast<Spine>(shared_from_this()), a_trackIndex, AnimationEventData((event->data && event->data->name) ? event->data->name : "Anon", (event->stringValue) ? event->stringValue : "", event->intValue, event->floatValue));
			}
		}

		void AnimationTrack::onAnimationStateEvent(int a_trackIndex, spEventType type, spEvent* event, int loopCount) {
			if(a_trackIndex == myTrackIndex){
				if(type == SP_ANIMATION_START){
					onStartSlot(*this);
				} else if(type == SP_ANIMATION_END){
					onEndSlot(*this);
				} else if(type == SP_ANIMATION_COMPLETE){
					onCompleteSlot(*this, loopCount);
				} else if(type == SP_ANIMATION_EVENT){
					onEventSlot(*this, AnimationEventData((event->data && event->data->name) ? event->data->name : "Anon", (event->stringValue)?event->stringValue:"", event->intValue, event->floatValue));
				}
			}
		}

		AnimationTrack& AnimationTrack::animate(const std::string &a_animationName, bool a_loop){
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

		AnimationTrack& AnimationTrack::queueAnimation(const std::string &a_animationName, bool a_loop, double a_delay){
			spAnimation* animation = spSkeletonData_findAnimation(skeleton->data, a_animationName.c_str());
			if(animation){
				auto* entry = spAnimationState_addAnimation(animationState, myTrackIndex, animation, a_loop, a_delay);
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
			spAnimationState_clearTrack(animationState, myTrackIndex);
			return *this;
		}

		AnimationTrack& AnimationTrack::time(double a_newTime){
			spAnimationState_getCurrent(animationState, myTrackIndex)->time = static_cast<float>(a_newTime);
			return *this;
		}
		double AnimationTrack::time() const{
			return static_cast<double>(spAnimationState_getCurrent(animationState, myTrackIndex)->time);
		}

		AnimationTrack& AnimationTrack::crossfade(double a_newTime){
			auto *trackState = spAnimationState_getCurrent(animationState, myTrackIndex);
			trackState->mixDuration = static_cast<float>(a_newTime);
			trackState->mix = equals(a_newTime, 0.0) ? 1.0f : 0.0f;

			return *this;
		}
		double AnimationTrack::crossfade() const{
			return static_cast<double>(spAnimationState_getCurrent(animationState, myTrackIndex)->mixDuration);
		}

		AnimationTrack& AnimationTrack::timeScale(double a_newTime){
			spAnimationState_getCurrent(animationState, myTrackIndex)->timeScale = static_cast<float>(a_newTime);
			return *this;
		}
		double AnimationTrack::timeScale() const{
			return static_cast<double>(spAnimationState_getCurrent(animationState, myTrackIndex)->timeScale);
		}

		std::string AnimationTrack::name() const {
			auto *currentTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			return (currentTrack && currentTrack->animation && currentTrack->animation->name) ? currentTrack->animation->name : "NULL";
		}

		double AnimationTrack::duration() const {
			auto *currentTrack = spAnimationState_getCurrent(animationState, myTrackIndex);
			return (currentTrack && currentTrack->animation && currentTrack->animation->duration) ? currentTrack->animation->duration : 0.0f;
		}

	}
}