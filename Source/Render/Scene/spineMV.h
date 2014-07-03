#ifndef _MV_SPINE_MV_
#define _MV_SPINE_MV_

#include "Render/Scene/node.h"

struct spSlot;
struct spSkeleton;
struct spAnimationState;
struct spBone;
struct spAtlas;

namespace MV {
	namespace Scene {

		class Spine : public Node{
			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS(Spine)

			static std::shared_ptr<Spine> make(Draw2D* a_renderer, const std::string &a_skeletonFile, const std::string &a_atlasFile);
			static std::shared_ptr<Spine> make(Draw2D* a_renderer, const std::string &a_skeletonFile, const std::string &a_atlasFile, float a_loadScale);

			virtual ~Spine();

			void update(double a_delta);
		protected:
			Spine(Draw2D *a_renderer, const std::string &a_skeletonFile, const std::string &a_atlasFile, float a_loadScale);

			virtual void drawImplementation();

		private:

			template <class Archive>
			void serialize(Archive & archive){
				archive(
					cereal::make_nvp("skeleton", skeletonFile),
					cereal::make_nvp("atlas", atlasFile),
					cereal::make_nvp("loadScale", loadScale),
					cereal::make_nvp("node", cereal::base_class<Node>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Spine> &construct){
				Draw2D *renderer = nullptr;
				std::string skeletonFile;
				std::string atlasFile;
				float loadScale;
				archive(
					cereal::make_nvp("skeleton", skeletonFile),
					cereal::make_nvp("atlas", atlasFile),
					cereal::make_nvp("loadScale", loadScale)
				).extract(
					cereal::make_nvp("renderer", renderer)
				);
				require(renderer != nullptr, MV::PointerException("Error: Failed to load a renderer for Rectangle node."));
				construct(renderer, skeletonFile, atlasFile, loadScale);
				archive(cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}

			bool skeletonRenderStateChangedSinceLastIteration(bool a_previousBlendingWasAdditive, bool a_currentAdditiveBlending, FileTextureDefinition * a_previousTexture, FileTextureDefinition * a_texture);

			size_t renderSkeletonBatch(size_t lastRenderedIndex, GLuint a_textureId);

			FileTextureDefinition * loadSpineSlotIntoPoints(spSlot* slot);


			std::string skeletonFile;
			std::string atlasFile;
			float loadScale;

			spSkeleton* skeleton = nullptr;
			spAnimationState* animationState = nullptr;
			spBone* rootBone = nullptr;
			spAtlas* atlas = nullptr;
			float* spineWorldVertices = nullptr;
			static const int SPINE_MESH_VERTEX_COUNT_MAX = 2000;
		};
	}
}


#endif