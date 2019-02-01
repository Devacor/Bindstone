#ifndef _MV_SCENE_EMITTER_H_
#define _MV_SCENE_EMITTER_H_

#include "sprite.h"
#include "MV/Utility/threadPool.hpp"
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

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
				a_script.add(chaiscript::user_type<ParticleChangeValues>(), "ParticleChangeValues");

				a_script.add(chaiscript::fun(&ParticleChangeValues::rateOfChange), "rateOfChange");
				a_script.add(chaiscript::fun(&ParticleChangeValues::directionalChangeTemplate), "directionalChange");
				a_script.add(chaiscript::fun(&ParticleChangeValues::rotationalChange), "rotationalChange");

				a_script.add(chaiscript::fun(&ParticleChangeValues::beginSpeed), "beginSpeed");
				a_script.add(chaiscript::fun(&ParticleChangeValues::endSpeed), "endSpeed");

				a_script.add(chaiscript::fun(&ParticleChangeValues::beginColor), "beginColor");
				a_script.add(chaiscript::fun(&ParticleChangeValues::endColor), "endColor");

				a_script.add(chaiscript::fun(&ParticleChangeValues::maxLifespan), "maxLifespan");

				a_script.add(chaiscript::fun(&ParticleChangeValues::gravityMagnitude), "gravityMagnitude");
				a_script.add(chaiscript::fun(&ParticleChangeValues::gravityDirection), "gravityDirection");

				return a_script;
			}

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				archive(CEREAL_NVP(rateOfChange), cereal::make_nvp("directionalChange", directionalChangeTemplate), CEREAL_NVP(rotationalChange),
					CEREAL_NVP(beginSpeed), CEREAL_NVP(endSpeed),
					CEREAL_NVP(beginScale), CEREAL_NVP(endScale),
					CEREAL_NVP(beginColor), CEREAL_NVP(endColor),
					CEREAL_NVP(maxLifespan),
					CEREAL_NVP(gravityMagnitude), CEREAL_NVP(gravityDirection),
					CEREAL_NVP(animationFramesPerSecond)
				);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				archive(CEREAL_NVP(rateOfChange), cereal::make_nvp("directionalChange", directionalChangeTemplate), CEREAL_NVP(rotationalChange),
					CEREAL_NVP(beginSpeed), CEREAL_NVP(endSpeed),
					CEREAL_NVP(beginScale), CEREAL_NVP(endScale),
					CEREAL_NVP(beginColor), CEREAL_NVP(endColor),
					CEREAL_NVP(maxLifespan),
					CEREAL_NVP(gravityMagnitude), CEREAL_NVP(gravityDirection),
					CEREAL_NVP(animationFramesPerSecond)
				);
				if (version < 1) {
					toRadiansInPlace(rateOfChange);
					toRadiansInPlace(directionalChangeTemplate);
					toRadiansInPlace(rotationalChange);
					toRadiansInPlace(gravityDirection);
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
				PointRotator(direction).apply(distance);
				position += distance;
				position += gravityConstant * timeScale;
				
				//currentFrame = static_cast<int>(wrap(0.0f, static_cast<float>(textureCount), static_cast<float>(textureCount * (change.animationFramesPerSecond / timeScale))));
				
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
			int previousFrame = -1;
			int currentFrame = 0;

			float totalLifespan = 0.0f;

			ParticleChangeValues change;

			size_t textureCount;

			void setGravity(float a_magnitude, const AxisAngles &a_direction = AxisAngles(0.0f, 0.0f, toRadians(180.0f))) {
				gravityConstant.locate(0.0f, a_magnitude, 0.0f);
				PointRotator(a_direction).apply(gravityConstant);
			}
		private:
			Point<> gravityConstant;
		};

		struct EmitterSpawnProperties {
			uint32_t maximumParticles = std::numeric_limits<uint32_t>::max();

			float minimumSpawnRate = 0.0f;
			float maximumSpawnRate = 1.0f;

			Point<> minimumPosition;
			Point<> maximumPosition;
			std::function<Point<>()> getPosition;

			inline void minimumDirectionDeg(AxisAngles a_minimumDirection) { minimumDirection = toRadians(a_minimumDirection); }
			inline AxisAngles minimumDirectionDeg() { return toDegrees(minimumDirection); }

			inline void maximumDirectionDeg(AxisAngles a_maximumDirection) { maximumDirection = toRadians(a_maximumDirection); }
			inline AxisAngles maximumDirectionDeg() { return toDegrees(maximumDirection); }

			AxisAngles minimumDirection;
			AxisAngles maximumDirection;
			std::function<AxisAngles()> getDirection;

			inline void minimumRotationDeg(AxisAngles a_minimumRotation) { minimumRotation = toRadians(a_minimumRotation); }
			inline AxisAngles minimumRotationDeg() { return toDegrees(minimumRotation); }

			inline void maximumRotationDeg(AxisAngles a_maximumRotation) { maximumRotation = toRadians(a_maximumRotation); }
			inline AxisAngles maximumRotationDeg() { return toDegrees(maximumRotation); }

			AxisAngles minimumRotation;
			AxisAngles maximumRotation;
			std::function<AxisAngles()> getRotation;

			ParticleChangeValues minimum;
			ParticleChangeValues maximum;

			std::function<AxisAngles()> getRotationChange;
			std::function<AxisAngles()> getRateOfChange;
			std::function<AxisAngles()> getDirectionChange;
			std::function<void(float&, float&)> setSpeed;
			std::function<void(Scale&, Scale&)> setScale;
			std::function<void(Color&, Color&)> setColor;

			void initializeCallbacks();

			bool dirty = true;

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
				a_script.add(chaiscript::user_type<EmitterSpawnProperties>(), "EmitterSpawnProperties");

				a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumParticles), "maximumParticles");

				a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimumSpawnRate), "minimumSpawnRate");
				a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumSpawnRate), "maximumSpawnRate");

				a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimumPosition), "minimumPosition");
				a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumPosition), "maximumPosition");

				a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimumDirection), "minimumDirection");
				a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumDirection), "maximumDirection");

				a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimumRotation), "minimumRotation");
				a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumRotation), "maximumRotation");

				a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimum), "minimum");
				a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximum), "maximum");

				ParticleChangeValues::hook(a_script);

				return a_script;
			}

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const version) {
				archive(CEREAL_NVP(maximumParticles),
					CEREAL_NVP(minimumSpawnRate), CEREAL_NVP(maximumSpawnRate),
					CEREAL_NVP(minimumPosition), CEREAL_NVP(maximumPosition),
					CEREAL_NVP(minimumDirection), CEREAL_NVP(maximumDirection),
					CEREAL_NVP(minimumRotation), CEREAL_NVP(maximumRotation),
					CEREAL_NVP(minimum), CEREAL_NVP(maximum)
				);
				if (version < 1) {
					toRadiansInPlace(minimumDirection);
					toRadiansInPlace(maximumDirection);
					toRadiansInPlace(minimumRotation);
					toRadiansInPlace(maximumRotation);
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

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, ThreadPool &a_pool) {
				a_script.add(chaiscript::user_type<Emitter>(), "Emitter");
				a_script.add(chaiscript::base_class<Drawable, Emitter>());
				a_script.add(chaiscript::base_class<Component, Emitter>());

				a_script.add(chaiscript::fun([&](Node &a_self) {
					return a_self.attach<Emitter>(a_pool);
				}), "attachEmitter");

				a_script.add(chaiscript::fun([](Node &a_self) {
					return a_self.componentInChildren<Emitter>();
				}), "emitterComponent");

				a_script.add(chaiscript::fun(&Emitter::enabled), "enabled");
				a_script.add(chaiscript::fun(&Emitter::disabled), "disabled");

				a_script.add(chaiscript::fun(&Emitter::enable), "enable");
				a_script.add(chaiscript::fun(&Emitter::disable), "disable");

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Emitter>(Emitter::*)(const EmitterSpawnProperties &a_emitterProperties)>(&Emitter::properties)), "properties");
				a_script.add(chaiscript::fun(static_cast<EmitterSpawnProperties&(Emitter::*)()>(&Emitter::properties)), "properties");
				a_script.add(chaiscript::fun(static_cast<const EmitterSpawnProperties&(Emitter::*)() const>(&Emitter::properties)), "properties");

				a_script.add(chaiscript::type_conversion<SafeComponent<Emitter>, std::shared_ptr<Emitter>>([](const SafeComponent<Emitter> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Emitter>, std::shared_ptr<Drawable>>([](const SafeComponent<Emitter> &a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Emitter>, std::shared_ptr<Component>>([](const SafeComponent<Emitter> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));

				EmitterSpawnProperties::hook(a_script);

				return a_script;
			}

		protected:
			Emitter(const std::weak_ptr<Node> &a_owner, ThreadPool &a_pool);

			virtual void updateImplementation(double a_dt) override;

			virtual void defaultDrawImplementation() override;

			virtual bool serializePoints() const override { return false; }

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				archive(
					CEREAL_NVP(spawnProperties),
					CEREAL_NVP(spawnParticles),
					CEREAL_NVP(relativeParentCount),
					cereal::make_nvp("relativeNodePosition", (relativeParentCount >= 0 ? std::weak_ptr<MV::Scene::Node>() : relativeNodePosition)),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					CEREAL_NVP(spawnProperties),
					CEREAL_NVP(spawnParticles),
					CEREAL_NVP(relativeParentCount),
					cereal::make_nvp("relativeNodePosition", (relativeParentCount >= 0 ? std::weak_ptr<MV::Scene::Node>() : relativeNodePosition)),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Emitter> &construct, std::uint32_t const version) {
				auto& services = cereal::get_user_data<MV::Services>(archive);
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

				particle.textureCount = ourTextures.size();

				particle.position = spawnProperties.getPosition() - threadData[a_groupIndex].particleOffset;
				particle.rotation = spawnProperties.getRotation();
				particle.change.rotationalChange = spawnProperties.getRotationChange();

				particle.direction = spawnProperties.getDirection();
				particle.change.rateOfChange = spawnProperties.getRateOfChange();
				particle.change.directionalChangeRad(spawnProperties.getDirectionChange());

				spawnProperties.setSpeed(particle.change.beginSpeed, particle.change.endSpeed);

				spawnProperties.setScale(particle.change.beginScale, particle.change.endScale);

				spawnProperties.setColor(particle.change.beginColor, particle.change.endColor);

				particle.change.animationFramesPerSecond = mix(spawnProperties.minimum.animationFramesPerSecond, spawnProperties.maximum.animationFramesPerSecond, randomNumber(0.0f, 1.0f));

				particle.change.maxLifespan = mix(spawnProperties.minimum.maxLifespan, spawnProperties.maximum.maxLifespan, randomNumber(0.0f, 1.0f));

				particle.setGravity(
					mix(spawnProperties.minimum.gravityMagnitude, spawnProperties.maximum.gravityMagnitude, randomNumber(0.0f, 1.0f)),
					randomMix(spawnProperties.minimum.gravityDirection, spawnProperties.maximum.gravityDirection)
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

			EmitterSpawnProperties spawnProperties;

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

			std::weak_ptr<MV::Scene::Node> relativeNodePosition;

			int relativeParentCount = -1;

			bool spawnParticles = true;
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
