#ifndef _MV_SPINE_MV_
#define _MV_SPINE_MV_

#include "node.h"
#include "drawable.h"
#include "spine/AnimationState.h"

struct spSlot;
struct spSkeleton;
struct spAnimationState;
struct spBone;
struct spAtlas;

namespace MV {
	void initializeSpineBindings();

	namespace Scene {
		void spineAnimationCallback(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event);
		void spineTrackEntryCallback(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event);
		
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

		class Spine;
		class AnimationTrack {
			friend Spine;
			Signal<void(AnimationTrack &)> onDisposeSignal;
			Signal<void(AnimationTrack &)> onStartSignal;
			Signal<void(AnimationTrack &)> onEndSignal;
			Signal<void(AnimationTrack &, int)> onCompleteSignal;
			Signal<void(AnimationTrack &, const AnimationEventData &)> onEventSignal;

			friend void spineTrackEntryCallback(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event);
		public:
			SignalRegister<void(AnimationTrack &)> onDispose;
			SignalRegister<void(AnimationTrack &)> onStart;
			SignalRegister<void(AnimationTrack &)> onEnd;
			SignalRegister<void(AnimationTrack &, int)> onComplete;
			SignalRegister<void(AnimationTrack &, const AnimationEventData &)> onEvent;

			typedef Signal<void(AnimationTrack &)>::SharedReceiverType BasicSignal;
			typedef Signal<void(AnimationTrack &, int)>::SharedReceiverType LoopSignal;
			typedef Signal<void(AnimationTrack &, const AnimationEventData &)>::SharedReceiverType EventSignal;

			std::map<std::string, BasicSignal> basicSignals;
			std::map<std::string, LoopSignal> loopSignals;
			std::map<std::string, EventSignal> eventSignals;

			AnimationTrack(int a_trackIndex, spAnimationState* a_animationState, spSkeleton* a_skeleton) :
				onStart(onStartSignal),
				onEnd(onEndSignal),
				onComplete(onCompleteSignal),
				onDispose(onDisposeSignal),
				onEvent(onEventSignal),
				myTrackIndex(a_trackIndex),
				animationState(a_animationState),
				skeleton(a_skeleton) {
			}

			int trackIndex() const{
				return myTrackIndex;
			}

			AnimationTrack& animate(const std::string &a_animationName, bool a_loop = true);
			AnimationTrack& queueAnimation(const std::string &a_animationName, double a_delay, bool a_loop = true);
			AnimationTrack& queueAnimation(const std::string &a_animationName, bool a_loop = true);

			std::string name() const;
			double duration() const;
			bool looping() const;

			AnimationTrack& stop();
			AnimationTrack& time(double a_newTime);
			double time() const;

			AnimationTrack& crossfade(double a_newTime);
			double crossfade() const;

			AnimationTrack& timeScale(double a_newTime);
			double timeScale() const;
		private:
			//called from spineTrackEntryCallback
			void onAnimationStateEvent(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event);

			bool destroying = false;
			int myTrackIndex;
			spAnimationState *animationState;
			spSkeleton *skeleton;
			spTrackEntry *recentTrack = nullptr;
		};
		
		class Spine : public Drawable{
			friend cereal::access;
			friend Node;
			friend void spineAnimationCallback(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event);

			Signal<void(std::shared_ptr<Spine>, int)> onStartSignal;
			Signal<void(std::shared_ptr<Spine>, int)> onEndSignal;
			Signal<void(std::shared_ptr<Spine>, int, int)> onCompleteSignal;
			Signal<void(Spine*, int)> onDisposeSignal;
			Signal<void(std::shared_ptr<Spine>, int, const AnimationEventData &)> onEventSignal;
		public:
			SignalRegister<void(std::shared_ptr<Spine>, int)> onStart;
			SignalRegister<void(std::shared_ptr<Spine>, int)> onEnd;
			SignalRegister<void(std::shared_ptr<Spine>, int, int)> onComplete;
			SignalRegister<void(Spine*, int)> onDispose;
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
				void serialize(Archive & archive, std::uint32_t const version);
			};

			DrawableDerivedAccessors(Spine)

			virtual ~Spine();

			std::shared_ptr<Spine> bindNode(const std::string &a_slotId, const std::string &a_nodeId);
			std::shared_ptr<Spine> unbindSlot(const std::string &a_slotId);
			std::shared_ptr<Spine> unbindNode(const std::string &a_slotId, const std::string &a_nodeId);
			std::shared_ptr<Spine> unbindAll();

			std::map<std::string, std::set<std::string>> boundSlots() const {
				return slotsToNodes;
			}

			MV::Point<> slotPosition(const std::string &a_slotId) const;

			std::shared_ptr<Spine> timeScale(double a_timeScale);
			double timeScale() const;

			std::shared_ptr<Spine> crossfade(const std::string &a_fromAnimation, const std::string &a_toAnimation, double a_duration);

			AnimationTrack& track(int a_index);
			AnimationTrack& track();
			int currentTrack() const {
				return defaultTrack;
			}

			std::shared_ptr<Spine> animate(const std::string &a_animationName, bool a_loop = true);
			std::shared_ptr<Spine> queueAnimation(const std::string &a_animationName, double a_delay, bool a_loop = true);
			std::shared_ptr<Spine> queueAnimation(const std::string &a_animationName, bool a_loop = true);

			std::shared_ptr<Spine> load(const FileBundle &a_fileBundle);
			std::shared_ptr<Spine> unload();
			bool loaded() const;

			FileBundle bundle() const {
				return fileBundle;
			}
		protected:
			virtual void initialize() override;

			virtual bool serializePoints() const override { return false; }

			Spine(const std::weak_ptr<Node> &a_owner, const FileBundle &a_fileBundle);
			Spine(const std::weak_ptr<Node> &a_owner);
			void loadImplementation(const FileBundle &a_fileBundle, bool a_refreshBounds = true);

			virtual void defaultDrawImplementation() override;

			void applySpineBlendMode(spBlendMode previousBlending);

			virtual void updateImplementation(double a_delta) override;
			void unloadImplementation();
		private:
			virtual void boundsImplementation(const BoxAABB<> &) override {}
			virtual BoxAABB<> boundsImplementation() override;

			//called from spineAnimationCallback
			void onAnimationStateEvent(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event);

			virtual bool preDraw();
			virtual bool postDraw();

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const{
				archive(
					cereal::make_nvp("fileBundle", fileBundle),
					cereal::make_nvp("slotsToNodes", slotsToNodes),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					cereal::make_nvp("fileBundle", fileBundle),
					cereal::make_nvp("slotsToNodes", slotsToNodes),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Spine> &construct, std::uint32_t const version){
				FileBundle fileBundle;
				archive(
					cereal::make_nvp("fileBundle", fileBundle)
				);
				construct(std::shared_ptr<Node>(), fileBundle);
				construct->load(archive, version);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Spine>(fileBundle).self());
			}

			std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

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

			bool inUpdate = false;
			bool pendingDelete = false;

			bool destroying = false;

			int defaultTrack = 0;
			std::map<int, std::unique_ptr<AnimationTrack>> tracks;
			std::map<std::string, std::set<std::string>> slotsToNodes;
		};


		template <class Archive>
		void Spine::FileBundle::serialize(Archive & archive, std::uint32_t const /*version*/) {
			archive(
				CEREAL_NVP(skeletonFile),
				CEREAL_NVP(atlasFile),
				CEREAL_NVP(loadScale)
			);
		}

	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_scenespine);

#endif
