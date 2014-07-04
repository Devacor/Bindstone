#ifndef _MV_SPINE_MV_
#define _MV_SPINE_MV_

#include "Render/Scene/node.h"
#include "spine/AnimationState.h"

struct spSlot;
struct spSkeleton;
struct spAnimationState;
struct spBone;
struct spAtlas;


namespace MV {
	namespace Scene {
		void spineAnimationCallback(spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount);
		void spineTrackEntryCallback(spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount);

		class Spine : public Node{
			friend cereal::access;
			friend Node;
			friend void spineAnimationCallback(spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount);
			friend void spineTrackEntryCallback(spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount);

			Slot<void(int)> startSlot;
			Slot<void(int)> endSlot;
			Slot<void(int, int)> completeSlot;
			Slot<void(int, spEvent* event)> eventSlot;
		public:

			struct FileBundle {
				FileBundle(const std::string &a_skeletonFile, const std::string &a_atlasFile, float a_loadScale = 1.0f);

				FileBundle();

				std::string skeletonFile;
				std::string atlasFile;
				float loadScale;
			private:
				friend cereal::access;
				template <class Archive>
				void serialize(Archive & archive);
			};


			SCENE_MAKE_FACTORY_METHODS(Spine)

			static std::shared_ptr<Spine> make(Draw2D* a_renderer, const FileBundle &a_fileBundle);

			virtual ~Spine();

			void update(double a_delta);
			void disableAutoUpdate();
			void enableAutoUpdate();

			std::shared_ptr<Spine> animateTimeScale(double a_timescale);
			double animateTimeScale() const;
		protected:
			Spine(Draw2D *a_renderer, const FileBundle &a_fileBundle);

			virtual void drawImplementation();

		private:

			void onAnimationStateEvent(int trackIndex, spEventType type, spEvent* event, int loopCount);
			void onTrackEntryEvent(int trackIndex, spEventType type, spEvent* event, int loopCount);

			void conditionalUpdate();

			template <class Archive>
			void serialize(Archive & archive){
				archive(
					cereal::make_nvp("fileBundle", fileBundle),
					cereal::make_nvp("timeScale", timeScale),
					cereal::make_nvp("node", cereal::base_class<Node>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Spine> &construct){
				Draw2D *renderer = nullptr;
				FileBundle fileBundle;
				archive(
					cereal::make_nvp("fileBundle", fileBundle)
				).extract(
					cereal::make_nvp("renderer", renderer)
				);
				require(renderer != nullptr, MV::PointerException("Error: Failed to load a renderer for Spine node."));
				construct(renderer, fileBundle);
				archive(cereal::make_nvp("timeScale", construct->timeScale), cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}

			bool skeletonRenderStateChangedSinceLastIteration(bool a_previousBlendingWasAdditive, bool a_currentAdditiveBlending, FileTextureDefinition * a_previousTexture, FileTextureDefinition * a_texture);

			size_t renderSkeletonBatch(size_t lastRenderedIndex, GLuint a_textureId);

			FileTextureDefinition * loadSpineSlotIntoPoints(spSlot* slot);


			FileBundle fileBundle;

			spSkeleton* skeleton = nullptr;
			spAnimationState* animationState = nullptr;
			spBone* rootBone = nullptr;
			spAtlas* atlas = nullptr;
			float* spineWorldVertices = nullptr;
			static const int SPINE_MESH_VERTEX_COUNT_MAX = 2000;

			float timeScale;
			static const double TIME_BETWEEN_UPDATES;
			bool autoUpdate;
			Stopwatch timer;
		};


		template <class Archive>
		void Spine::FileBundle::serialize(Archive & archive) {
			archive(
				CEREAL_NVP(skeletonFile),
				CEREAL_NVP(atlasFile),
				CEREAL_NVP(loadScale)
			);
		}

	}
}


#endif