#ifndef _MV_SPINE_MV_
#define _MV_SPINE_MV_

#include "Render/Scene/node.h"
#include "Render/Scene/drawable.h"
#include "spine/AnimationState.h"

#include <cereal/types/set.hpp>

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
			Signal<void(AnimationTrack &)> onStartSignal;
			Signal<void(AnimationTrack &)> onEndSignal;
			Signal<void(AnimationTrack &, int)> onCompleteSignal;
			Signal<void(AnimationTrack &, const AnimationEventData &)> onEventSignal;

			friend void spineTrackEntryCallback(spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount);
		public:
			SignalRegister<void(AnimationTrack &)> onStart;
			SignalRegister<void(AnimationTrack &)> onEnd;
			SignalRegister<void(AnimationTrack &, int)> onComplete;
			SignalRegister<void(AnimationTrack &, const AnimationEventData &)> onEvent;

			typedef Signal<void(AnimationTrack &)>::SharedRecieverType BasicSignal;
			typedef Signal<void(AnimationTrack &, int)>::SharedRecieverType LoopSignal;
			typedef Signal<void(AnimationTrack &, const AnimationEventData &)>::SharedRecieverType EventSignal;

			std::map<std::string, BasicSignal> basicSignals;
			std::map<std::string, LoopSignal> loopSignals;
			std::map<std::string, EventSignal> eventSignals;

			AnimationTrack(int a_trackIndex, spAnimationState *a_animationState, spSkeleton *a_skeleton):
				myTrackIndex(a_trackIndex),
				animationState(a_animationState),
				skeleton(a_skeleton),
				onStart(onStartSignal),
				onEnd(onEndSignal),
				onComplete(onCompleteSignal),
				onEvent(onEventSignal){
			}

			int trackIndex() const{
				return myTrackIndex;
			}

			AnimationTrack& animate(const std::string &a_animationName, bool a_loop = true);
			AnimationTrack& queueAnimation(const std::string &a_animationName, double a_delay, bool a_loop = true);
			AnimationTrack& queueAnimation(const std::string &a_animationName, bool a_loop = true);

			std::string name() const;
			double duration() const;

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
		
		class Spine : public Drawable{
			friend cereal::access;
			friend Node;
			friend void spineAnimationCallback(spAnimationState* state, int trackIndex, spEventType type, spEvent* event, int loopCount);

			Signal<void(std::shared_ptr<Spine>, int)> onStartSignal;
			Signal<void(std::shared_ptr<Spine>, int)> onEndSignal;
			Signal<void(std::shared_ptr<Spine>, int, int)> onCompleteSignal;
			Signal<void(std::shared_ptr<Spine>, int, const AnimationEventData &)> onEventSignal;
		public:
			SignalRegister<void(std::shared_ptr<Spine>, int)> onStart;
			SignalRegister<void(std::shared_ptr<Spine>, int)> onEnd;
			SignalRegister<void(std::shared_ptr<Spine>, int, int)> onComplete;
			SignalRegister<void(std::shared_ptr<Spine>, int, const AnimationEventData &)> onEvent;

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

			DrawableDerivedAccessors(Spine)

			virtual ~Spine();

			std::shared_ptr<Spine> bindNodeToSlot(const std::string &a_slotId, const std::string &a_nodeId);
			std::shared_ptr<Spine> unbindSlot(const std::string &a_slotId);
			std::shared_ptr<Spine> unbindNodeInSlot(const std::string &a_slotId, const std::string &a_nodeId);

			std::shared_ptr<Spine> timeScale(double a_timeScale);
			double timeScale() const;

			std::shared_ptr<Spine> crossfade(const std::string &a_fromAnimation, const std::string &a_toAnimation, double a_duration);

			AnimationTrack& track(int a_index);

			std::shared_ptr<Spine> animate(const std::string &a_animationName, bool a_loop = true);
			std::shared_ptr<Spine> queueAnimation(const std::string &a_animationName, double a_delay, bool a_loop = true);
			std::shared_ptr<Spine> queueAnimation(const std::string &a_animationName, bool a_loop = true);
		protected:
			Spine(const std::weak_ptr<Node> &a_owner, const FileBundle &a_fileBundle);

			virtual void defaultDrawImplementation() override;

			void applySpineBlendMode(spBlendMode previousBlending);

			virtual void updateImplementation(double a_delta) override;
		private:
			//called from spineAnimationCallback
			void onAnimationStateEvent(int trackIndex, spEventType type, spEvent* event, int loopCount);

			virtual bool preDraw();
			virtual bool postDraw();

			template <class Archive>
			void serialize(Archive & archive){
				archive(
					cereal::make_nvp("fileBundle", fileBundle),
					cereal::make_nvp("slotsToNodes", slotsToNodes),
					cereal::make_nvp("Spine", cereal::base_class<Drawable>(this))
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
				require<PointerException>(renderer != nullptr, "Error: Failed to load a renderer for Spine node.");
				construct(std::shared_ptr<Node>(), fileBundle);
				archive(
					cereal::make_nvp("slotsToNodes", construct->slotsToNodes),
					cereal::make_nvp("Spine", cereal::base_class<Drawable>(construct.ptr())));
				construct->initialize();
			}

			bool skeletonRenderStateChangedSinceLastIteration(spBlendMode a_previousBlending, spBlendMode a_currentBlending, FileTextureDefinition * a_previousTexture, FileTextureDefinition * a_texture);

			size_t renderSkeletonBatch(size_t lastRenderedIndex, GLuint a_textureId, spBlendMode a_blendMode);

			FileTextureDefinition * loadSpineSlotIntoPoints(spSlot* slot);
			FileTextureDefinition *getSpineTextureFromSlot(spSlot* slot) const;

			FileBundle fileBundle;

			spSkeleton* skeleton = nullptr;
			spAnimationState* animationState = nullptr;
			spBone* rootBone = nullptr;
			spAtlas* atlas = nullptr;
			float* spineWorldVertices = nullptr;
			static const int SPINE_MESH_VERTEX_COUNT_MAX = 1024;

			static const int MAX_UPDATES = 10;
			static const double TIME_BETWEEN_UPDATES;
			bool autoUpdate;
			Stopwatch timer;

			int defaultTrack = 0;
			std::map<int, AnimationTrack> tracks;
			std::map<std::string, std::set<std::string>> slotsToNodes;
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