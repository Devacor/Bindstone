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
		template <typename T>
		FileTextureDefinition* getSpineTexture(T* attachment){
			spAtlasRegion* atlasRegion = (attachment && attachment->rendererObject) ? reinterpret_cast<spAtlasRegion*>(attachment->rendererObject) : nullptr;
			return (atlasRegion && atlasRegion->page && atlasRegion->page->rendererObject) ? reinterpret_cast<FileTextureDefinition*>(atlasRegion->page->rendererObject) : nullptr;
		}

		std::shared_ptr<Spine> MV::Scene::Spine::make(Draw2D* a_renderer, const std::string &a_skeletonFile, const std::string &a_atlasFile){
			return Spine::make(a_renderer, a_skeletonFile, a_atlasFile, 1.0f);
		}

		std::shared_ptr<Spine> MV::Scene::Spine::make(Draw2D* a_renderer, const std::string &a_skeletonFile, const std::string &a_atlasFile, float a_loadScale){
			auto spine = std::shared_ptr<Spine>(new Spine(a_renderer, a_skeletonFile, a_atlasFile, a_loadScale));
			a_renderer->registerShader(spine);
			return spine;
		}

		Spine::Spine(Draw2D *a_renderer, const std::string &a_skeletonFile, const std::string &a_atlasFile, float a_loadScale):
			Node(a_renderer),
			skeletonFile(a_skeletonFile),
			atlasFile(a_atlasFile),
			loadScale(a_loadScale){

			atlas = spAtlas_createFromFile(a_atlasFile.c_str(), 0);
			require(atlas, ResourceException("Error reading atlas file:" + a_atlasFile));

			spSkeletonJson* json = spSkeletonJson_create(atlas);
			json->scale = a_loadScale;
			spSkeletonData* skeletonData = spSkeletonJson_readSkeletonDataFile(json, a_skeletonFile.c_str());

			require(skeletonData, ResourceException((json->error ? json->error : "Error reading skeleton data file:") + std::string(" ") + a_skeletonFile));

			spSkeletonJson_dispose(json);

			skeleton = spSkeleton_create(skeletonData);
			require(skeleton && skeleton->bones && skeleton->bones[0], ResourceException(a_skeletonFile + " has no bones!"));
			rootBone = skeleton->bones[0];

			animationState = spAnimationState_create(spAnimationStateData_create(skeleton->data));
			animationState->rendererObject = this;
			
			spineWorldVertices = new float[SPINE_MESH_VERTEX_COUNT_MAX]();
			update(1.0);
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
					spSkeleton_updateWorldTransform(skeleton);
				}
			}
		}

		void Spine::drawImplementation(){
			shaderProgram->use();
			update(1.0);
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

				MV::Scene::appendQuadVertexIndices(vertexIndices, points.size());
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

	}
}
