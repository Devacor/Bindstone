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

		struct AnimationEventData {
			AnimationEventData(const std::string &a_name, const std::string & a_stringValue, int a_intValue, float a_floatValue):
			name(a_name),
			stringValue(a_stringValue),
			intValue(a_intValue),
			floatValue(a_floatValue){
			}
			std::string name;
			std::string stringValue;
			int intValue;
			float floatValue;
		};

		class AnimationTrack {
			Slot<void(AnimationTrack &)> onStartSlot;
			Slot<void(AnimationTrack &)> onEndSlot;
			Slot<void(AnimationTrack &, int)> onCompleteSlot;
			Slot<void(AnimationTrack &, const AnimationEventData &)> onEventSlot;

			friend void spineTrackEntryCallback(spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount);
		public:
			SlotRegister<void(AnimationTrack &)> onStart;
			SlotRegister<void(AnimationTrack &)> onEnd;
			SlotRegister<void(AnimationTrack &, int)> onComplete;
			SlotRegister<void(AnimationTrack &, const AnimationEventData &)> onEvent;

			typedef Slot<void(AnimationTrack &)>::SharedSignalType BasicSignal;
			typedef Slot<void(AnimationTrack &, int)>::SharedSignalType LoopSignal;
			typedef Slot<void(AnimationTrack &, const AnimationEventData &)>::SharedSignalType EventSignal;

			std::map<std::string, BasicSignal> basicSignals;
			std::map<std::string, LoopSignal> loopSignals;
			std::map<std::string, EventSignal> eventSignals;

			AnimationTrack(int a_trackIndex, spAnimationState *a_animationState, spSkeleton *a_skeleton):
				myTrackIndex(a_trackIndex),
				animationState(a_animationState),
				skeleton(a_skeleton),
				onStart(onStartSlot),
				onEnd(onEndSlot),
				onComplete(onCompleteSlot),
				onEvent(onEventSlot){
			}

			int trackIndex() const{
				return myTrackIndex;
			}

			AnimationTrack& animate(const std::string &a_animationName, bool a_loop = true);
			AnimationTrack& queueAnimation(const std::string &a_animationName, bool a_loop = true, double a_delay = 0.0);

			AnimationTrack& stop();
			AnimationTrack& time(double a_newTime);
			double time() const;

			AnimationTrack& crossfade(double a_newTime);
			double crossfade() const;

			AnimationTrack& timeScale(double a_newTime);
			double timeScale() const;
		private:
			//called from spineTrackEntryCallback
			void onAnimationStateEvent(int a_trackIndex, spEventType type, spEvent* event, int loopCount);

			int myTrackIndex;
			spAnimationState *animationState;
			spSkeleton *skeleton;
		};

		class Spine : public Node{
			friend cereal::access;
			friend Node;
			friend void spineAnimationCallback(spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount);

			Slot<void(std::shared_ptr<Spine>, int)> onStartSlot;
			Slot<void(std::shared_ptr<Spine>, int)> onEndSlot;
			Slot<void(std::shared_ptr<Spine>, int, int)> onCompleteSlot;
			Slot<void(std::shared_ptr<Spine>, int, const AnimationEventData &)> onEventSlot;
		public:
			SlotRegister<void(std::shared_ptr<Spine>, int)> onStart;
			SlotRegister<void(std::shared_ptr<Spine>, int)> onEnd;
			SlotRegister<void(std::shared_ptr<Spine>, int, int)> onComplete;
			SlotRegister<void(std::shared_ptr<Spine>, int, const AnimationEventData &)> onEvent;

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

			std::shared_ptr<Spine> timeScale(double a_timeScale);
			double timeScale() const;

			std::shared_ptr<Spine> crossfade(const std::string &a_fromAnimation, const std::string &a_toAnimation, double a_duration);

			AnimationTrack& track(int a_index);

			std::shared_ptr<Spine> animate(const std::string &a_animationName, bool a_loop = true);
			std::shared_ptr<Spine> queueAnimation(const std::string &a_animationName, bool a_loop = true, double a_delay = 0.0);
		protected:
			Spine(Draw2D *a_renderer, const FileBundle &a_fileBundle);

			virtual void drawImplementation();

		private:
			//called from spineAnimationCallback
			void onAnimationStateEvent(int trackIndex, spEventType type, spEvent* event, int loopCount);

			void conditionalUpdate();

			template <class Archive>
			void serialize(Archive & archive){
				archive(
					cereal::make_nvp("fileBundle", fileBundle),
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
				archive(cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
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
			static const int SPINE_MESH_VERTEX_COUNT_MAX = 1024;

			static const double TIME_BETWEEN_UPDATES;
			bool autoUpdate;
			Stopwatch timer;

			int defaultTrack = 0;
			std::map<int, AnimationTrack> tracks;
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