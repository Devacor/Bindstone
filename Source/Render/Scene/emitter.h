#ifndef _MV_SCENE_EMITTER_H_
#define _MV_SCENE_EMITTER_H_

#include "sprite.h"
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
			void serialize(Archive & archive) {
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
			bool update(double a_dt) {
				float timeScale = static_cast<float>(a_dt);
				totalLifespan = std::min(totalLifespan + timeScale, change.maxLifespan);

				float mixValue = totalLifespan / change.maxLifespan;

				direction += change.directionalChange * timeScale;
				rotation += change.rotationalChange * timeScale;

				speed = mix(change.beginSpeed, change.endSpeed, mixValue);
				scale = mix(change.beginScale, change.endScale, mixValue);
				color = mix(change.beginColor, change.endColor, mixValue);

				Point<> distance(0.0f, -speed, 0.0f);
				rotatePoint(distance, direction);
				position += distance * timeScale;
				position += gravityConstant * timeScale;

				currentFrame = static_cast<int>(boundBetween(static_cast<float>(myTextures.size() * (change.animationFramesPerSecond / timeScale)), 0.0f, static_cast<float>(myTextures.size())));
				return totalLifespan == change.maxLifespan;
			}

			void reset() {
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

			void setGravity(float a_magnitude, const AxisAngles &a_direction = AxisAngles(0.0f, 180.0f, 0.0f)) {
				gravityConstant.locate(0.0f, -a_magnitude, 0.0f);
				rotatePoint(gravityConstant, a_direction);
			}
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
			void serialize(Archive & archive) {
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

		class Emitter : public Drawable {
			friend Node;
			friend cereal::access;
		public:
			DrawableDerivedAccessors(Emitter)

			std::shared_ptr<Emitter> properties(const EmitterSpawnProperties &a_emitterProperties);

			const EmitterSpawnProperties& properties() const;

			EmitterSpawnProperties& properties();

			bool enabled() const;
			bool disabled() const;

			std::shared_ptr<Emitter> enable();
			std::shared_ptr<Emitter> disable();

			virtual void update(double a_dt) override;

			~Emitter();

		protected:
			Emitter(const std::weak_ptr<Node> &a_owner, ThreadPool &a_pool);

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					CEREAL_NVP(spawnProperties),
					CEREAL_NVP(spawnParticles),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Emitter> &construct) {
				ThreadPool *pool = nullptr;
				archive.extract(cereal::make_nvp("pool", pool));
				MV::require<PointerException>(pool != nullptr, "Null thread pool in Emitter::load_and_construct.");
				construct(std::shared_ptr<Node>(), *pool);
				archive(
					cereal::make_nvp("spawnProperties", construct->spawnProperties),
					cereal::make_nvp("spawnParticles", construct->spawnParticles),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Emitter>(pool));
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			virtual BoxAABB<> boundsImplementation();

			Point<> randomMix(const Point<> &a_rhs, const Point<> &a_lhs);

			Color randomMix(const Color &a_rhs, const Color &a_lhs);

			void spawnParticle(size_t a_groupIndex);

			void spawnParticlesOnMultipleThreads(double a_dt);

			void updateParticlesOnMultipleThreads(double a_dt);

			void loadParticlesToPoints(size_t a_groupIndex);

			void loadParticlePointsFromGroups();

			void Emitter::loadPointsFromBufferAndAllowUpdate();


			double accumulatedTimeDelta = 0.0f;

			EmitterSpawnProperties spawnProperties;

			size_t emitterThreads;

			struct ThreadData {
				std::vector<Particle> particles;
				std::vector<DrawPoint> points;
				std::vector<GLuint> vertexIndices;
			};
			std::vector<DrawPoint> pointBuffer;
			std::vector<GLuint> vertexIndexBuffer;

			std::vector<ThreadData> threadData;

			bool spawnParticles = true;
			static const double MAX_TIME_STEP;
			static const int MAX_PARTICLES_PER_FRAME = 2500;
			std::atomic<double> timeSinceLastParticle = 0.0;
			double nextSpawnDelta = 0.0;

			ThreadPool& pool;

			std::atomic<bool> updateInProgress;
		};
	}
}

#endif
