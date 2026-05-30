#ifndef _MV_SCENE_EMITTER_H_
#define _MV_SCENE_EMITTER_H_

#include "sprite.h"
#include "MV/Utility/threadPool.hpp"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <atomic>

namespace MV {
	namespace Scene {

		Point<> randomMix(const Point<> &a_rhs, const Point<> &a_lhs);
		Color randomMix(const Color &a_rhs, const Color &a_lhs);

		struct ParticleChangeValues {
		private:
			AxisAngles directionalChangeTemplate;
			AxisAngles directionalChangeCurrent;
		public:
			inline void rateOfChangeDeg(AxisAngles a_rateOfChange) { rateOfChange = toRadians(a_rateOfChange); }
			inline AxisAngles rateOfChangeDeg() { return toDegrees(rateOfChange); }

			AxisAngles rateOfChange;

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
				return toDegrees(directionalChangeTemplate);
			}
			AxisAngles currentDirectionalChangeDeg() const {
				return toDegrees(directionalChangeCurrent);
			}

			AxisAngles directionalChangeRad() const {
				return directionalChangeTemplate;
			}
			AxisAngles currentDirectionalChangeRad() const {
				return directionalChangeCurrent;
			}

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

			template<class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					archive(
						JAI_NVP(rateOfChange),
						jai::serialization::make_nvp("directionalChange", directionalChangeTemplate),
						JAI_NVP(rotationalChange),
						JAI_NVP(beginSpeed),
						JAI_NVP(endSpeed),
						JAI_NVP(beginScale),
						JAI_NVP(endScale),
						JAI_NVP(beginColor),
						JAI_NVP(endColor),
						JAI_NVP(maxLifespan),
						JAI_NVP(gravityMagnitude),
						JAI_NVP(gravityDirection),
						JAI_NVP(animationFramesPerSecond)
					);
				} else {
					archive(
						cereal::make_nvp("rateOfChange", rateOfChange),
						cereal::make_nvp("directionalChange", directionalChangeTemplate),
						cereal::make_nvp("rotationalChange", rotationalChange),
						cereal::make_nvp("beginSpeed", beginSpeed),
						cereal::make_nvp("endSpeed", endSpeed),
						cereal::make_nvp("beginScale", beginScale),
						cereal::make_nvp("endScale", endScale),
						cereal::make_nvp("beginColor", beginColor),
						cereal::make_nvp("endColor", endColor),
						cereal::make_nvp("maxLifespan", maxLifespan),
						cereal::make_nvp("gravityMagnitude", gravityMagnitude),
						cereal::make_nvp("gravityDirection", gravityDirection),
						cereal::make_nvp("animationFramesPerSecond", animationFramesPerSecond)
					);
				}
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					archive(
						JAI_NVP(rateOfChange),
						jai::serialization::make_nvp("directionalChange", directionalChangeTemplate),
						JAI_NVP(rotationalChange),
						JAI_NVP(beginSpeed),
						JAI_NVP(endSpeed),
						JAI_NVP(beginScale),
						JAI_NVP(endScale),
						JAI_NVP(beginColor),
						JAI_NVP(endColor),
						JAI_NVP(maxLifespan),
						JAI_NVP(gravityMagnitude),
						JAI_NVP(gravityDirection),
						JAI_NVP(animationFramesPerSecond)
					);
				} else {
					archive(
						cereal::make_nvp("rateOfChange", rateOfChange),
						cereal::make_nvp("directionalChange", directionalChangeTemplate),
						cereal::make_nvp("rotationalChange", rotationalChange),
						cereal::make_nvp("beginSpeed", beginSpeed),
						cereal::make_nvp("endSpeed", endSpeed),
						cereal::make_nvp("beginScale", beginScale),
						cereal::make_nvp("endScale", endScale),
						cereal::make_nvp("beginColor", beginColor),
						cereal::make_nvp("endColor", endColor),
						cereal::make_nvp("maxLifespan", maxLifespan),
						cereal::make_nvp("gravityMagnitude", gravityMagnitude),
						cereal::make_nvp("gravityDirection", gravityDirection),
						cereal::make_nvp("animationFramesPerSecond", animationFramesPerSecond)
					);
					if (version < 1) {
						toRadiansInPlace(rateOfChange);
						toRadiansInPlace(directionalChangeTemplate);
						toRadiansInPlace(rotationalChange);
						toRadiansInPlace(gravityDirection);
					}
				}
				directionalChangeCurrent = directionalChangeTemplate;
			}
		};

		struct Particle {
			//return true if dead.
			inline bool update(double a_dt) {
				float timeScale = static_cast<float>(a_dt);
				totalLifespan = std::min(totalLifespan + timeScale, change.maxLifespan);

				float mixValue = totalLifespan / change.maxLifespan;
				
				direction += change.currentDirectionalChangeRad(change.currentDirectionalChangeRad() + (change.rateOfChange * timeScale)) * timeScale;
				rotation += change.rotationalChange * timeScale;

				speed = mix(change.beginSpeed, change.endSpeed, mixValue);
				scale = mix(change.beginScale, change.endScale, mixValue);
				color = mix(change.beginColor, change.endColor, mixValue);

				Point<> distance(0.0f, speed * timeScale, 0.0f);
				TransformMatrix rotator;
				rotator.rotateXYZ(direction);
				distance = rotator * distance;
				position += distance;
				position += gravityConstant * timeScale;
				
				//currentFrame = static_cast<int>(wrap(0.0f, static_cast<float>(textureCount), static_cast<float>(textureCount * (change.animationFramesPerSecond / timeScale))));
				
				return totalLifespan == change.maxLifespan;
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

		struct EmitterSpawnProperties : public jai::property_owner<EmitterSpawnProperties> {
			JAI_PROPERTY((uint32_t), maximumParticles, std::numeric_limits<uint32_t>::max());

			JAI_PROPERTY((float), minimumSpawnRate, 0.0f);
			JAI_PROPERTY((float), maximumSpawnRate, 1.0f);

			JAI_PROPERTY((Point<>), minimumPosition);
			JAI_PROPERTY((Point<>), maximumPosition);
			std::function<Point<>()> getPosition;

			inline void minimumDirectionDeg(AxisAngles a_min) { minimumDirection = toRadians(a_min); }
			inline AxisAngles minimumDirectionDeg() { return toDegrees(*minimumDirection); }

			inline void maximumDirectionDeg(AxisAngles a_max) { maximumDirection = toRadians(a_max); }
			inline AxisAngles maximumDirectionDeg() { return toDegrees(*maximumDirection); }

			JAI_PROPERTY((AxisAngles), minimumDirection);
			JAI_PROPERTY((AxisAngles), maximumDirection);
			std::function<AxisAngles()> getDirection;

			inline void minimumRotationDeg(AxisAngles a_min) { minimumRotation = toRadians(a_min); }
			inline AxisAngles minimumRotationDeg() { return toDegrees(*minimumRotation); }

			inline void maximumRotationDeg(AxisAngles a_max) { maximumRotation = toRadians(a_max); }
			inline AxisAngles maximumRotationDeg() { return toDegrees(*maximumRotation); }

			JAI_PROPERTY((AxisAngles), minimumRotation);
			JAI_PROPERTY((AxisAngles), maximumRotation);
			std::function<AxisAngles()> getRotation;

			JAI_PROPERTY((ParticleChangeValues), minimum);
			JAI_PROPERTY((ParticleChangeValues), maximum);

			std::function<AxisAngles()> getRotationChange;
			std::function<AxisAngles()> getRateOfChange;
			std::function<AxisAngles()> getDirectionChange;
			std::function<void(float&, float&)> setSpeed;
			std::function<void(Scale&, Scale&)> setScale;
			std::function<void(Color&, Color&)> setColor;

			void initializeCallbacks();

			bool dirty = true;

			template<class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.save(archive);
				} else {
					archive(cereal::make_nvp("maximumParticles", maximumParticles.get()));
					archive(cereal::make_nvp("minimumSpawnRate", minimumSpawnRate.get()));
					archive(cereal::make_nvp("maximumSpawnRate", maximumSpawnRate.get()));
					archive(cereal::make_nvp("minimumPosition", minimumPosition.get()));
					archive(cereal::make_nvp("maximumPosition", maximumPosition.get()));
					archive(cereal::make_nvp("minimumDirection", minimumDirection.get()));
					archive(cereal::make_nvp("maximumDirection", maximumDirection.get()));
					archive(cereal::make_nvp("minimumRotation", minimumRotation.get()));
					archive(cereal::make_nvp("maximumRotation", maximumRotation.get()));
					archive(cereal::make_nvp("minimum", minimum.get()));
					archive(cereal::make_nvp("maximum", maximum.get()));
				}
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.load(archive);
				} else {
					archive(cereal::make_nvp("maximumParticles", maximumParticles.get()));
					archive(cereal::make_nvp("minimumSpawnRate", minimumSpawnRate.get()));
					archive(cereal::make_nvp("maximumSpawnRate", maximumSpawnRate.get()));
					archive(cereal::make_nvp("minimumPosition", minimumPosition.get()));
					archive(cereal::make_nvp("maximumPosition", maximumPosition.get()));
					archive(cereal::make_nvp("minimumDirection", minimumDirection.get()));
					archive(cereal::make_nvp("maximumDirection", maximumDirection.get()));
					archive(cereal::make_nvp("minimumRotation", minimumRotation.get()));
					archive(cereal::make_nvp("maximumRotation", maximumRotation.get()));
					archive(cereal::make_nvp("minimum", minimum.get()));
					archive(cereal::make_nvp("maximum", maximum.get()));
					if (version < 1) {
						toRadiansInPlace(*minimumDirection);
						toRadiansInPlace(*maximumDirection);
						toRadiansInPlace(*minimumRotation);
						toRadiansInPlace(*maximumRotation);
					}
				}
				dirty = true;
			}
		};

		void loadEmitterProperties(EmitterSpawnProperties& a_result, const std::string &a_file, MV::Services& a_services);

		class Emitter : public jai::property_owner<Emitter, Drawable> {
			friend Node;
			friend cereal::access;
			friend jai::access;
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

			template<class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.save(archive);
					Drawable::save(archive, 0);
				} else {
					archive(cereal::make_nvp("spawnProperties", spawnProperties.get()));
					archive(cereal::make_nvp("relativeNodePosition", relativeNodePosition.get()));
					archive(cereal::make_nvp("relativeParentCount", relativeParentCount.get()));
					archive(cereal::make_nvp("spawnParticles", spawnParticles.get()));
					archive(cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this)));
				}
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				if constexpr (jai::serialization::jai_archive<Archive>) {
					property_mgr.load(archive);
					Drawable::load(archive, 0);
				} else {
					archive(cereal::make_nvp("spawnProperties", spawnProperties.get()));
					archive(cereal::make_nvp("relativeNodePosition", relativeNodePosition.get()));
					archive(cereal::make_nvp("relativeParentCount", relativeParentCount.get()));
					archive(cereal::make_nvp("spawnParticles", spawnParticles.get()));
					archive(cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this)));
				}

				if (*relativeParentCount >= 0) {
					(*relativeNodePosition).reset();
				}
			}

			template<class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Emitter> &construct, std::uint32_t const version) {
				MV::Services& services = cereal::get_user_data<MV::Services>(archive);
				auto* pool = services.get<MV::ThreadPool>();

				construct(std::shared_ptr<Node>(), *pool);
				construct->load(archive, version);
				construct->initialize();
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Emitter>& construct) {
				auto* services = ar.template get_user_context<MV::Services>();
				construct(std::shared_ptr<Node>(), *services->template get<MV::ThreadPool>());
				construct->load(ar, 0);
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

				particle.change.animationFramesPerSecond = mix(spawnProperties->minimum->animationFramesPerSecond, spawnProperties->maximum->animationFramesPerSecond, randomNumber(0.0f, 1.0f));

				particle.change.maxLifespan = mix(spawnProperties->minimum->maxLifespan, spawnProperties->maximum->maxLifespan, randomNumber(0.0f, 1.0f));

				particle.setGravity(
					mix(spawnProperties->minimum->gravityMagnitude, spawnProperties->maximum->gravityMagnitude, randomNumber(0.0f, 1.0f)),
					randomMix(spawnProperties->minimum->gravityDirection, spawnProperties->maximum->gravityDirection)
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

			JAI_PROPERTY((EmitterSpawnProperties), spawnProperties);

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

			JAI_PROPERTY((std::weak_ptr<MV::Scene::Node>), relativeNodePosition);
			JAI_PROPERTY((int32_t), relativeParentCount, -1);
			JAI_PROPERTY((bool), spawnParticles, true);

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
