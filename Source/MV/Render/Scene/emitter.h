#ifndef _MV_SCENE_EMITTER_H_
#define _MV_SCENE_EMITTER_H_

#include "sprite.h"
#include "MV/Utility/threadPool.hpp"
#include "MV/Utility/properties.hpp"
#include <atomic>

namespace MV {
	namespace Scene {

		Point<> randomMix(const Point<> &a_rhs, const Point<> &a_lhs);
		Color randomMix(const Color &a_rhs, const Color &a_lhs);

		struct ParticleChangeValues : public PropertyOwner {
		private:
			MV_NAMED_PROPERTY((AxisAngles), "directionalChange", directionalChangeTemplate);
			AxisAngles directionalChangeCurrent;
		public:
			inline void rateOfChangeDeg(AxisAngles a_rateOfChange) { *rateOfChange = toRadians(a_rateOfChange); }
			inline AxisAngles rateOfChangeDeg() { return toDegrees(*rateOfChange); }

			MV_PROPERTY((AxisAngles), rateOfChange);

			AxisAngles directionalChangeDeg(const AxisAngles &a_newDirectionalChange) {
				return directionalChangeRad(toRadians(a_newDirectionalChange));
			}
			AxisAngles directionalChangeRad(const AxisAngles &a_newDirectionalChange) {
				directionalChangeTemplate = a_newDirectionalChange;
				directionalChangeCurrent = a_newDirectionalChange;
				return directionalChangeCurrent;
			}

			AxisAngles currentDirectionalChangeDeg(const AxisAngles &a_newDirectionalChange) {
				directionalChangeCurrent = toRadians(a_newDirectionalChange);
				return a_newDirectionalChange;
			}
			AxisAngles currentDirectionalChangeRad(const AxisAngles &a_newDirectionalChange) {
				directionalChangeCurrent = a_newDirectionalChange;
				return directionalChangeCurrent;
			}

			AxisAngles directionalChangeDeg() const {
				return toDegrees(*directionalChangeTemplate);
			}
			AxisAngles currentDirectionalChangeDeg() const {
				return toDegrees(directionalChangeCurrent);
			}

			AxisAngles directionalChangeRad() const {
				return *directionalChangeTemplate;
			}
			AxisAngles currentDirectionalChangeRad() const {
				return directionalChangeCurrent;
			}

			MV_PROPERTY((AxisAngles), rotationalChange);

			MV_PROPERTY((float), beginSpeed, 0.0f);
			MV_PROPERTY((float), endSpeed, 0.0f);

			MV_PROPERTY((Scale), beginScale);
			MV_PROPERTY((Scale), endScale);

			MV_PROPERTY((Color), beginColor);
			MV_PROPERTY((Color), endColor);

			MV_PROPERTY((float), maxLifespan, 1.0f);

			MV_PROPERTY((float), gravityMagnitude, 0.0f);
			MV_PROPERTY((AxisAngles), gravityDirection);

			MV_PROPERTY((float), animationFramesPerSecond, 10.0f);

			template <class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				reflection().save(archive);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if (version == 0) {
					reflection().load(archive, {
						"rateOfChange", "directionalChange", "rotationalChange",
						"beginSpeed", "endSpeed",
						"beginScale", "endScale",
						"beginColor", "endColor",
						"maxLifespan",
						"gravityMagnitude", "gravityDirection",
						"animationFramesPerSecond"
					});
					toRadiansInPlace(*rateOfChange);
					toRadiansInPlace(*directionalChangeTemplate);
					toRadiansInPlace(*rotationalChange);
					toRadiansInPlace(*gravityDirection);
				} else {
					reflection().load(archive);
				}
				directionalChangeCurrent = *directionalChangeTemplate;
			}
		};

		struct Particle {
			//return true if dead.
			inline bool update(double a_dt) {
				float timeScale = static_cast<float>(a_dt);
				totalLifespan = std::min(totalLifespan + timeScale, *change.maxLifespan);

				float mixValue = totalLifespan / *change.maxLifespan;
				
				direction += change.currentDirectionalChangeRad(change.currentDirectionalChangeRad() + (*change.rateOfChange * timeScale)) * timeScale;
				rotation += *change.rotationalChange * timeScale;

				speed = mix(*change.beginSpeed, *change.endSpeed, mixValue);
				scale = mix(*change.beginScale, *change.endScale, mixValue);
				color = mix(*change.beginColor, *change.endColor, mixValue);

				Point<> distance(0.0f, speed * timeScale, 0.0f);
				TransformMatrix rotator;
				rotator.rotateXYZ(direction);
				distance = rotator * distance;
				position += distance;
				position += gravityConstant * timeScale;
				
				//currentFrame = static_cast<int>(wrap(0.0f, static_cast<float>(textureCount), static_cast<float>(textureCount * (change.animationFramesPerSecond / timeScale))));
				
				return totalLifespan == *change.maxLifespan;
			}

			void reset() {
				totalLifespan = 0.0f;
			}

			Point<> position;
			float speed = 0;
			AxisAngles direction;
			AxisAngles rotation;
			Scale scale;
			Color color;
			int previousFrame = -1;
			int currentFrame = 0;

			float totalLifespan = 0.0f;

			ParticleChangeValues change;

			size_t textureCount = 0;

			void setGravity(float a_magnitude, const AxisAngles &a_direction = AxisAngles(0.0f, 0.0f, toRadians(180.0f))) {
				gravityConstant.locate(0.0f, a_magnitude, 0.0f);
				TransformMatrix rotator;
				rotator.rotateXYZ(a_direction);
				gravityConstant = rotator * gravityConstant;
			}
		private:
			Point<> gravityConstant;
		};

		struct EmitterSpawnProperties : public PropertyOwner {
			MV_PROPERTY((uint32_t), maximumParticles, std::numeric_limits<uint32_t>::max());

			MV_PROPERTY((float), minimumSpawnRate, 0.0f);
			MV_PROPERTY((float), maximumSpawnRate, 1.0f);

			MV_PROPERTY((Point<>), minimumPosition);
			MV_PROPERTY((Point<>), maximumPosition);
			std::function<Point<>()> getPosition;

			inline void minimumDirectionDeg(AxisAngles a_min) { minimumDirection = toRadians(a_min); }
			inline AxisAngles minimumDirectionDeg() { return toDegrees(*minimumDirection); }

			inline void maximumDirectionDeg(AxisAngles a_max) { maximumDirection = toRadians(a_max); }
			inline AxisAngles maximumDirectionDeg() { return toDegrees(*maximumDirection); }

			MV_PROPERTY((AxisAngles), minimumDirection);
			MV_PROPERTY((AxisAngles), maximumDirection);
			std::function<AxisAngles()> getDirection;

			inline void minimumRotationDeg(AxisAngles a_min) { minimumRotation = toRadians(a_min); }
			inline AxisAngles minimumRotationDeg() { return toDegrees(*minimumRotation); }

			inline void maximumRotationDeg(AxisAngles a_max) { maximumRotation = toRadians(a_max); }
			inline AxisAngles maximumRotationDeg() { return toDegrees(*maximumRotation); }
			
			MV_PROPERTY((AxisAngles), minimumRotation);
			MV_PROPERTY((AxisAngles), maximumRotation);
			std::function<AxisAngles()> getRotation;

			MV_PROPERTY((ParticleChangeValues), minimum);
			MV_PROPERTY((ParticleChangeValues), maximum);

			std::function<AxisAngles()> getRotationChange;
			std::function<AxisAngles()> getRateOfChange;
			std::function<AxisAngles()> getDirectionChange;
			std::function<void(float&, float&)> setSpeed;
			std::function<void(Scale&, Scale&)> setScale;
			std::function<void(Color&, Color&)> setColor;

			void initializeCallbacks();

			bool dirty = true;

			template <class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				reflection().save(archive);
			}
			
			template <class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if (version == 0) {
					reflection().load(archive, {
						"maximumParticles",
						"minimumSpawnRate", "maximumSpawnRate",
						"minimumPosition", "maximumPosition",
						"minimumDirection", "maximumDirection",
						"minimumRotation", "maximumRotation",
						"minimum", "maximum"
					});
					toRadiansInPlace(*minimumDirection);
					toRadiansInPlace(*maximumDirection);
					toRadiansInPlace(*minimumRotation);
					toRadiansInPlace(*maximumRotation);
				} else {
					reflection().load(archive);
				}
				dirty = true;
			}
		};

		EmitterSpawnProperties loadEmitterProperties(const std::string &a_file);

		class Emitter : public Drawable {
			friend Node;
			friend cereal::access;
		public:
			DrawableDerivedAccessors(Emitter)

			std::shared_ptr<Emitter> relativeEmission(std::shared_ptr<MV::Scene::Node> a_newRelativePosition);
			std::weak_ptr<MV::Scene::Node> relativeEmission() const;
			std::shared_ptr<Emitter> removeRelativeEmission();

			std::shared_ptr<Emitter> makeRelativeToParent(int a_count);

			std::shared_ptr<Emitter> properties(const EmitterSpawnProperties &a_emitterProperties);

			const EmitterSpawnProperties& properties() const;
			EmitterSpawnProperties& properties();

			bool enabled() const;
			bool disabled() const;

			std::shared_ptr<Emitter> enable();
			std::shared_ptr<Emitter> disable();

			~Emitter();

		protected:
			Emitter(const std::weak_ptr<Node> &a_owner, ThreadPool &a_pool);

			virtual void updateImplementation(double a_dt) override;

			virtual void defaultDrawImplementation() override;

			virtual bool serializePoints() const override { return false; }

			template <class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				archive(
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if (version == 0) {
					reflection().load(archive, {
						"spawnProperties",
						"spawnParticles", 
						"relativeParentCount",
						"relativeNodePosition"
					});
				}
				
				archive(cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this)));
				
				if (*relativeParentCount >= 0) {
					(*relativeNodePosition).reset();
				}
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Emitter> &construct, std::uint32_t const version) {
				MV::Services& services = cereal::get_user_data<MV::Services>(archive);
				auto* pool = services.get<MV::ThreadPool>();

				construct(std::shared_ptr<Node>(), *pool);
				construct->load(archive, version);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Emitter>(pool).self());
			}
			
			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			virtual BoxAABB<> boundsImplementation() override;
			virtual void boundsImplementation(const BoxAABB<> &a_bounds) override;

			inline void spawnParticle(size_t a_groupIndex) {
				Particle particle;

				particle.textureCount = ourTextures->size();

				particle.position = spawnProperties->getPosition() - threadData[a_groupIndex].particleOffset;
				particle.rotation = spawnProperties->getRotation();
				particle.change.rotationalChange = spawnProperties->getRotationChange();

				particle.direction = spawnProperties->getDirection();
				particle.change.rateOfChange = spawnProperties->getRateOfChange();
				particle.change.directionalChangeRad(spawnProperties->getDirectionChange());

				spawnProperties->setSpeed(particle.change.beginSpeed, particle.change.endSpeed);

				spawnProperties->setScale(particle.change.beginScale, particle.change.endScale);

				spawnProperties->setColor(particle.change.beginColor, particle.change.endColor);

				particle.change.animationFramesPerSecond = mix(*spawnProperties->minimum->animationFramesPerSecond, *spawnProperties->maximum->animationFramesPerSecond, randomNumber(0.0f, 1.0f));

				particle.change.maxLifespan = mix(*spawnProperties->minimum->maxLifespan, *spawnProperties->maximum->maxLifespan, randomNumber(0.0f, 1.0f));

				particle.setGravity(
					mix(*spawnProperties->minimum->gravityMagnitude, *spawnProperties->maximum->gravityMagnitude, randomNumber(0.0f, 1.0f)),
					randomMix(*spawnProperties->minimum->gravityDirection, *spawnProperties->maximum->gravityDirection)
				);

				//particle.update(0.0f);

				threadData[a_groupIndex].particles.emplace_back(std::move(particle));
			}

			void spawnParticlesOnMultipleThreads(double a_dt);

			void updateParticlesOnMultipleThreads(double a_dt);

			void loadParticlesToPoints(size_t a_groupIndex);

			void loadParticlePointsFromGroups();

			void loadPointsFromBufferAndAllowUpdate();


			double accumulatedTimeDelta = 0.0f;

			MV_PROPERTY((EmitterSpawnProperties), spawnProperties);

			size_t emitterThreads;

			struct ThreadData {
				std::vector<Particle> particles;
				std::vector<DrawPoint> points;
				std::vector<GLuint> vertexIndices;
				MV::Point<> particleOffset;
			};
			std::vector<DrawPoint> pointBuffer;
			std::vector<GLuint> vertexIndexBuffer;

			std::vector<ThreadData> threadData;

			MV_PROPERTY((std::weak_ptr<MV::Scene::Node>), relativeNodePosition);
			MV_PROPERTY((int32_t), relativeParentCount, -1);
			MV_PROPERTY((bool), spawnParticles, true);

			static const double MAX_TIME_STEP;
			static const int MAX_PARTICLES_PER_FRAME = 2500;
            std::atomic<double> timeSinceLastParticle = {0.0};
			double nextSpawnDelta = 0.0;

			ThreadPool& pool;

			std::atomic<bool> updateInProgress;

			std::recursive_mutex lock;
		};
	}
}

CEREAL_FORCE_DYNAMIC_INIT(mv_sceneemitter);

#endif
