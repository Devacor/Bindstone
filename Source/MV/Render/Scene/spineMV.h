#ifndef _MV_SCENE_SPINE_H_
#define _MV_SCENE_SPINE_H_

#include "node.h"
#include "drawable.h"
#include "spine/AnimationState.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

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
			jai::signal_emitter<void(AnimationTrack &)> onDisposeSignal;
			jai::signal_emitter<void(AnimationTrack &)> onStartSignal;
			jai::signal_emitter<void(AnimationTrack &)> onEndSignal;
			jai::signal_emitter<void(AnimationTrack &, int)> onCompleteSignal;
			jai::signal_emitter<void(AnimationTrack &, const AnimationEventData &)> onEventSignal;

			friend void spineTrackEntryCallback(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event);
		public:
			jai::signal<void(AnimationTrack &)> onDispose;
			jai::signal<void(AnimationTrack &)> onStart;
			jai::signal<void(AnimationTrack &)> onEnd;
			jai::signal<void(AnimationTrack &, int)> onComplete;
			jai::signal<void(AnimationTrack &, const AnimationEventData &)> onEvent;

			typedef jai::signal<void(AnimationTrack &)>::shared_receiver_type BasicSignal;
			typedef jai::signal<void(AnimationTrack &, int)>::shared_receiver_type LoopSignal;
			typedef jai::signal<void(AnimationTrack &, const AnimationEventData &)>::shared_receiver_type EventSignal;

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
		
		class Spine : public jai::property_owner<Spine, Drawable> {
			friend jai::access;
			friend Node;
			friend void spineAnimationCallback(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event);

			jai::signal_emitter<void(std::shared_ptr<Spine>, int)> onStartSignal;
			jai::signal_emitter<void(std::shared_ptr<Spine>, int)> onEndSignal;
			jai::signal_emitter<void(std::shared_ptr<Spine>, int, int)> onCompleteSignal;
			jai::signal_emitter<void(Spine*, int)> onDisposeSignal;
			jai::signal_emitter<void(std::shared_ptr<Spine>, int, const AnimationEventData &)> onEventSignal;
		public:
			jai::signal<void(std::shared_ptr<Spine>, int)> onStart;
			jai::signal<void(std::shared_ptr<Spine>, int)> onEnd;
			jai::signal<void(std::shared_ptr<Spine>, int, int)> onComplete;
			jai::signal<void(Spine*, int)> onDispose;
			jai::signal<void(std::shared_ptr<Spine>, int, const AnimationEventData &)> onEvent;

			struct FileBundle : public jai::property_owner<FileBundle> {
				FileBundle(const std::string &a_skeletonFile, const std::string &a_atlasFile, float a_loadScale = 1.0f);

				FileBundle();

				// Add copy constructor and assignment operator
				FileBundle(const FileBundle& other) : jai::property_owner<FileBundle>(other) {
					// property_owner copy constructor handles property cloning
				}

				FileBundle& operator=(const FileBundle& other) {
					if (this != &other) {
						jai::property_owner<FileBundle>::operator=(other);
					}
					return *this;
				}

				JAI_PROPERTY((std::string), skeletonFile, "");
				JAI_PROPERTY((std::string), atlasFile, "");
				JAI_PROPERTY((float), loadScale, 1.0f);

			private:
			friend jai::access;
				template<class Archive>
				void save(Archive & archive, std::uint32_t const) const {
					property_mgr.save(archive);
				}

				template<class Archive>
				void load(Archive & archive, std::uint32_t const version) {
					property_mgr.load(archive);
				}
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

			virtual void updateImplementation(double a_delta) override;
			void unloadImplementation();
		private:
			virtual void boundsImplementation(const BoxAABB<> &) override {}
			virtual BoxAABB<> boundsImplementation() override;

			//called from spineAnimationCallback
			void onAnimationStateEvent(spAnimationState* a_state, spEventType a_type, spTrackEntry* a_entry, spEvent* a_event);

			virtual bool preDraw();
			virtual bool postDraw();

			template<class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const{
				property_mgr.save(archive);
				archive(
					jai::serialization::make_nvp("fileBundle", fileBundle),
					jai::serialization::make_nvp("slotsToNodes", slotsToNodes)
				);
				Drawable::save(archive, 0);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				property_mgr.load(archive);
				archive(
					jai::serialization::make_nvp("fileBundle", fileBundle),
					jai::serialization::make_nvp("slotsToNodes", slotsToNodes)
				);
				Drawable::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Spine>& construct) {
				construct(std::shared_ptr<Node>(), FileBundle());
				construct->load(ar, 0);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Spine>(fileBundle).self());
			}

			std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

			bool skeletonRenderStateChangedSinceLastIteration(spBlendMode a_previousBlending, spBlendMode a_currentBlending, FileTextureDefinition * a_previousTexture, FileTextureDefinition * a_texture);

			size_t renderSkeletonBatch(size_t a_lastRenderedIndex, FileTextureDefinition* a_texture, spBlendMode a_blendMode);

			// One uniform set per render batch (each batch binds its own texture/blend); reused across
			// frames, grown to the most batches ever seen. Reset to 0 at the top of each frame's draw.
			std::vector<Render::BoundUniformSet> deviceBatchSets;
			size_t deviceBatchCursor = 0;

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


	}
}

#endif


