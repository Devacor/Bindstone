#ifndef _MV_SCENE_EMITTER_H_
#define _MV_SCENE_EMITTER_H_

#include "Render/Scene/primitives.h"
#include "cereal/cereal.hpp"
#include "cereal/access.hpp"

#include <atomic>

namespace MV {
	namespace Scene {

		struct ParticleChangeValues {
			AxisAngles directionalChange;
			AxisAngles rotationalChange;

			float beginSpeed = 0.0f;
			float endSpeed = 0.0f;

			Scale beginScale;
			Scale endScale;

			Color beginColor;
			Color endColor;

			float maxLifespan = 1.0f;

			float gravityMagnitude = 0.0f;
			AxisAngles gravityDirection;

			float animationFramesPerSecond = 10.0f;

			template <class Archive>
			void serialize(Archive & archive){
				archive(CEREAL_NVP(directionalChange), CEREAL_NVP(rotationalChange),
					CEREAL_NVP(beginSpeed), CEREAL_NVP(endSpeed),
					CEREAL_NVP(beginScale), CEREAL_NVP(endScale),
					CEREAL_NVP(beginColor), CEREAL_NVP(endColor),
					CEREAL_NVP(maxLifespan),
					CEREAL_NVP(gravityMagnitude), CEREAL_NVP(gravityDirection),
					CEREAL_NVP(animationFramesPerSecond)
				);
			}
		};

		struct Particle {
			//return true if dead.
			bool update(double a_dt);

			void reset(){
				totalLifespan = 0.0f;
			}

			Point<> position;
			float speed;
			AxisAngles direction;
			AxisAngles rotation;
			Scale scale;
			Color color;
			int currentFrame;

			float totalLifespan = 0.0f;

			ParticleChangeValues change;

			std::vector<std::shared_ptr<TextureHandle>> myTextures;

			void setGravity(float a_magnitude, const AxisAngles &a_direction = AxisAngles(0.0f, 180.0f, 0.0f));
		private:
			Point<> gravityConstant;
		};

		struct EmitterSpawnProperties {
			uint32_t maximumParticles = std::numeric_limits<uint32_t>::max();

			float minimumSpawnRate = 0.0f;
			float maximumSpawnRate = 1.0f;

			Point<> maximumPosition;
			Point<> minimumPosition;

			AxisAngles maximumDirection;
			AxisAngles minimumDirection;

			AxisAngles maximumRotation;
			AxisAngles minimumRotation;

			ParticleChangeValues minimum;
			ParticleChangeValues maximum;

			template <class Archive>
			void serialize(Archive & archive){
				archive(CEREAL_NVP(maximumParticles),
					CEREAL_NVP(minimumSpawnRate), CEREAL_NVP(maximumSpawnRate),
					CEREAL_NVP(minimumPosition), CEREAL_NVP(maximumPosition),
					CEREAL_NVP(minimumDirection), CEREAL_NVP(maximumDirection),
					CEREAL_NVP(minimumRotation), CEREAL_NVP(maximumRotation),
					CEREAL_NVP(minimum), CEREAL_NVP(maximum)
				);
			}
		};

		EmitterSpawnProperties loadEmitterProperties(const std::string &a_file);

		class Emitter :
			public Node{

			friend cereal::access;
			friend Node;
		public:
			~Emitter();

			SCENE_MAKE_FACTORY_METHODS(Emitter)

			static std::shared_ptr<Emitter> make(Draw2D* a_renderer, ThreadPool *a_pool);
			static std::shared_ptr<Emitter> make(Draw2D* a_renderer, ThreadPool *a_pool, const EmitterSpawnProperties &a_emitterProperties);

			std::shared_ptr<Emitter> properties(const EmitterSpawnProperties &a_emitterProperties);

			const EmitterSpawnProperties& properties() const;
			EmitterSpawnProperties& properties();

			bool enabled() const;
			bool disabled() const;
			std::shared_ptr<Emitter> enable();
			std::shared_ptr<Emitter> disable();
		protected:
			Emitter(Draw2D *a_renderer, ThreadPool *a_pool):
				pool(a_pool),
				Node(a_renderer),
				threadData(emitterThreads),
				updateInProgress(false),
				emitterThreads(a_pool->threads()){
			}
			virtual void drawImplementation();

			virtual BoxAABB worldAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB screenAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB localAABBImplementation(bool a_includeChildren, bool a_nestedCall) override;
			virtual BoxAABB basicAABBImplementation() const override;

		private:

			void update();
			void spawnParticle(size_t a_groupIndex);

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("enabled", spawnParticles), cereal::make_nvp("spawnProperties", spawnProperties),
					cereal::make_nvp("node", cereal::base_class<Node>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Emitter> &construct){
				Draw2D *renderer = nullptr;
				ThreadPool *pool = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				archive.extract(cereal::make_nvp("pool", pool));
				require<PointerException>(renderer != nullptr, "Error: Failed to load a renderer for Emitter node.");
				require<PointerException>(pool != nullptr, "Error: Failed to load a pool for Emitter node.");
				construct(renderer, pool);
				archive(cereal::make_nvp("enabled", construct->spawnParticles), cereal::make_nvp("spawnProperties", construct->spawnProperties),
					cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}

			void loadParticlesToPoints(size_t a_groupIndex);
			
			MV::Color randomMix(const MV::Color &a_rhs, const MV::Color &a_lhs);
			MV::Point<> randomMix(const MV::Point<> &a_rhs, const MV::Point<> &a_lhs);
			void spawnParticlesOnMultipleThreads(double a_dt);
			void updateParticlesOnMultipleThreads(double a_dt);

			void loadParticlePointsFromGroups();
			void loadPointsFromBufferAndAllowUpdate();


			Stopwatch timer;

			EmitterSpawnProperties spawnProperties;

			size_t emitterThreads;
			std::atomic<size_t> buffersCopied = emitterThreads;
			std::atomic<size_t> updatesFinished = emitterThreads;
			std::atomic<size_t> spawnsFinished = emitterThreads;


			struct ThreadData {
				std::vector<Particle> particles;
				std::vector<DrawPoint> points;
				std::vector<GLuint> vertexIndices;
			};
			std::vector<DrawPoint> pointBuffer;
			std::vector<GLuint> vertexIndexBuffer;

			std::vector<ThreadData> threadData;

			bool spawnParticles = true;
			std::mutex lock;
			static const double MAX_TIME_STEP;
			static const int MAX_PARTICLES_PER_FRAME = 2500;
			std::atomic<double> timeSinceLastParticle = 0.0;
			double nextSpawnDelta = 0.0f;

			ThreadPool* pool;

			std::atomic<bool> updateInProgress;
		};
	}
}

#endif
