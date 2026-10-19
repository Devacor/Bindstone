#ifndef _MV_SCENE_EMITTER_H_
#define _MV_SCENE_EMITTER_H_

#include "sprite.h"
#include <jaiscript/detail/thread_pool.hpp>
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <functional>
#include <cmath>

namespace MV {
	namespace Scene {

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
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
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
				directionalChangeCurrent = directionalChangeTemplate;
			}
		};

		struct Particle {
			//return true if dead.
			inline bool update(double a_dt) {
				float timeScale = static_cast<float>(a_dt);
				totalLifespan = std::min(totalLifespan + timeScale, change.maxLifespan);

				// Guard maxLifespan == 0 (would be a 0/0 NaN that propagates into every mixed field).
				float mixValue = change.maxLifespan > 0.0f ? totalLifespan / change.maxLifespan : 1.0f;

				direction += change.currentDirectionalChangeRad(change.currentDirectionalChangeRad() + (change.rateOfChange * timeScale)) * timeScale;
				rotation += change.rotationalChange * timeScale;

				speed = mix(change.beginSpeed, change.endSpeed, mixValue);
				scale = mix(change.beginScale, change.endScale, mixValue);
				color = mix(change.beginColor, change.endColor, mixValue);

				position += rotatedYAxis(direction, speed * timeScale);
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
				gravityConstant = rotatedYAxis(a_direction, a_magnitude);
			}
		private:
			Point<> gravityConstant;

			// Rotates the +Y vector (0, a_magnitude, 0) by an XYZ-Euler rotation. Numerically identical
			// to (TransformMatrix().rotateXYZ(a_angles) * Point(0, a_magnitude, 0)) — since the input is
			// along +Y, only column 1 of the XYZ rotation matrix contributes, so we skip building and
			// multiplying a full 4x4 matrix.
			static inline Point<> rotatedYAxis(const AxisAngles &a_angles, float a_magnitude) {
				const float cX = std::cos(a_angles.x), sX = std::sin(a_angles.x);
				const float cY = std::cos(a_angles.y), sY = std::sin(a_angles.y);
				const float cZ = std::cos(a_angles.z), sZ = std::sin(a_angles.z);
				return Point<>(
					(-cY * sZ) * a_magnitude,
					(cX * cZ - sX * sY * sZ) * a_magnitude,
					(sX * cZ + cX * sY * sZ) * a_magnitude
				);
			}
		};

		struct EmitterSpawnProperties : public jai::property_owner<EmitterSpawnProperties> {
			JAI_PROPERTY((uint32_t), maximumParticles, std::numeric_limits<uint32_t>::max());

			JAI_PROPERTY((float), minimumSpawnRate, 0.0f);
			JAI_PROPERTY((float), maximumSpawnRate, 1.0f);

			JAI_PROPERTY((Point<>), minimumPosition);
			JAI_PROPERTY((Point<>), maximumPosition);

			inline void minimumDirectionDeg(AxisAngles a_min) { minimumDirection = toRadians(a_min); }
			inline AxisAngles minimumDirectionDeg() { return toDegrees(*minimumDirection); }

			inline void maximumDirectionDeg(AxisAngles a_max) { maximumDirection = toRadians(a_max); }
			inline AxisAngles maximumDirectionDeg() { return toDegrees(*maximumDirection); }

			JAI_PROPERTY((AxisAngles), minimumDirection);
			JAI_PROPERTY((AxisAngles), maximumDirection);

			inline void minimumRotationDeg(AxisAngles a_min) { minimumRotation = toRadians(a_min); }
			inline AxisAngles minimumRotationDeg() { return toDegrees(*minimumRotation); }

			inline void maximumRotationDeg(AxisAngles a_max) { maximumRotation = toRadians(a_max); }
			inline AxisAngles maximumRotationDeg() { return toDegrees(*maximumRotation); }

			JAI_PROPERTY((AxisAngles), minimumRotation);
			JAI_PROPERTY((AxisAngles), maximumRotation);

			JAI_PROPERTY((ParticleChangeValues), minimum);
			JAI_PROPERTY((ParticleChangeValues), maximum);

			template<class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				property_mgr.save(archive);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				property_mgr.load(archive);
			}
		};

		void loadEmitterProperties(EmitterSpawnProperties& a_result, const std::string &a_file, MV::Services& a_services);

		struct EmitterPipeline;

		//Pool stages read only a snapshot taken on the main thread, never the Emitter, so it can change or die mid-flight.
		class Emitter : public jai::property_owner<Emitter, Drawable> {
			friend Node;
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

			//A headless renderer skips the simulation unless asked (tests); only drawing needs a device.
			std::shared_ptr<Emitter> simulateWhileHeadless(bool a_simulate);
			bool simulateWhileHeadless() const { return simulatesWhileHeadless; }

			//Runs on a worker at the start of each slot's stage, so tests can hold or fail a pipeline.
			std::shared_ptr<Emitter> pipelineProbe(std::function<void(size_t)> a_probe);

		protected:
			Emitter(const std::weak_ptr<Node> &a_owner, jai::thread_pool &a_pool);

			virtual void updateImplementation(double a_dt) override;

			virtual void defaultDrawImplementation() override;
			void recordEmitterUniforms();

			virtual bool serializePoints() const override { return false; }

			template<class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				property_mgr.save(archive);
				Drawable::save(archive, 0);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const version) {
				property_mgr.load(archive);
				Drawable::load(archive, 0);

				if (*relativeParentCount >= 0) {
					(*relativeNodePosition).reset();
				}
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Emitter>& construct) {
				auto* services = ar.template get_user_context<MV::Services>();
				construct(std::shared_ptr<Node>(), *services->template get<jai::thread_pool>());
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

			void takePipelineResult();
			void startPipeline();

			JAI_PROPERTY((EmitterSpawnProperties), spawnProperties);

			JAI_PROPERTY((std::weak_ptr<MV::Scene::Node>), relativeNodePosition);
			JAI_PROPERTY((int32_t), relativeParentCount, -1);
			JAI_PROPERTY((bool), spawnParticles, true);

			static const double MAX_TIME_STEP;

			double accumulatedTimeDelta = 0.0;
			bool rerollSpawnDelta = true;
			bool simulatesWhileHeadless = false;
			std::function<void(size_t)> ourPipelineProbe;

			jai::thread_pool& pool;
			std::shared_ptr<EmitterPipeline> pipeline;
			bool pipelineRunning = false;
		};

	}
}

#endif
