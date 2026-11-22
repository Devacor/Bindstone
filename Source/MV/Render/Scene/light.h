#ifndef _MV_SCENE_LIGHT_H_
#define _MV_SCENE_LIGHT_H_

#include "node.h"
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {
		// A point light for drawables with a lit shader, Sprite Lamp style: a colour, a reach on the plane, a height above
		// it and a pulse. It shines from its node's world position, so a light in a parallax layer moves with the layer,
		// and it lights only the scene its node is in.
		class Light : public jai::property_owner<Light, Component> {
			friend Node;
			friend jai::access;

		public:
			static void jai_auto_bind(jai::dynamic_binder<Light>& a_builder);

			ComponentDerivedAccessors(Light)

			~Light() override;

			std::shared_ptr<Light> color(const Color& a_color) {
				ourColor = a_color;
				return std::static_pointer_cast<Light>(shared_from_this());
			}
			Color color() const { return ourColor; }

			std::shared_ptr<Light> intensity(PointPrecision a_intensity) {
				ourIntensity = a_intensity;
				return std::static_pointer_cast<Light>(shared_from_this());
			}
			PointPrecision intensity() const { return ourIntensity; }

			// How far it reaches on the plane, in its node's units.
			std::shared_ptr<Light> radius(PointPrecision a_radius) {
				ourRadius = a_radius;
				return std::static_pointer_cast<Light>(shared_from_this());
			}
			PointPrecision radius() const { return ourRadius; }

			// The exponent of the fade from the centre to the radius: 1 fades evenly, higher keeps it near the centre.
			std::shared_ptr<Light> falloff(PointPrecision a_falloff) {
				ourFalloff = a_falloff;
				return std::static_pointer_cast<Light>(shared_from_this());
			}
			PointPrecision falloff() const { return ourFalloff; }

			// How far above the plane it sits, in its node's units: low lights graze the surface, high ones face it.
			std::shared_ptr<Light> height(PointPrecision a_height) {
				ourHeight = a_height;
				return std::static_pointer_cast<Light>(shared_from_this());
			}
			PointPrecision height() const { return ourHeight; }

			// The intensity swings by a_amplitude (a share of it) once every a_period seconds, a_phase (a share of the
			// period) into the swing.
			std::shared_ptr<Light> pulse(PointPrecision a_amplitude, PointPrecision a_period, PointPrecision a_phase) {
				ourPulseAmplitude = a_amplitude;
				ourPulsePeriod = a_period;
				ourPulsePhase = a_phase;
				return std::static_pointer_cast<Light>(shared_from_this());
			}
			PointPrecision pulseAmplitude() const { return ourPulseAmplitude; }
			PointPrecision pulsePeriod() const { return ourPulsePeriod; }
			PointPrecision pulsePhase() const { return ourPulsePhase; }

			// The light as lit draws receive it this frame; false while it gives none (hidden, no reach, no intensity).
			bool gather(Render::PointLight& a_light);

		protected:
			Light(const std::weak_ptr<Node>& a_owner);

			template<class Archive>
			void save(Archive& archive, std::uint32_t const) const {
				property_mgr.save(archive);
				Component::save(archive, 0);
			}

			template<class Archive>
			void load(Archive& archive, std::uint32_t const) {
				property_mgr.load(archive);
				Component::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Light>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node>& a_parent) override {
				return cloneHelper(a_parent->attach<Light>().self());
			}

			void initialize() override;
			void postLoadInitialize() override;
			void reattachImplementation() override;
			void detachImplementation() override;
			void updateImplementation(double a_delta) override;

		private:
			void joinLighting();
			void leaveLighting();

			JAI_NAMED_PROPERTY((Color), "color", ourColor, (Color(1.0f, 0.62f, 0.28f, 1.0f)));
			JAI_NAMED_PROPERTY((PointPrecision), "intensity", ourIntensity, 1.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "radius", ourRadius, 256.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "falloff", ourFalloff, 2.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "height", ourHeight, 64.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "pulseAmplitude", ourPulseAmplitude, 0.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "pulsePeriod", ourPulsePeriod, 2.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "pulsePhase", ourPulsePhase, 0.0f);

			// Seconds of updates, the pulse's clock; unlike accumulatedDelta it starts at zero on load.
			double pulseSeconds = 0.0;
			std::weak_ptr<Render::Lighting> joinedLighting;
		};

		// Records into a_shader the lights of a_scope that reach a_worldBounds (the strongest there, at most
		// Render::Lighting::LIGHTS_PER_DRAW), the ambient light and the mapping from clip space to the render world.
		void applyLights(Shader& a_shader, Draw2D& a_renderer, const BoxAABB<>& a_worldBounds, const void* a_scope);

		// The normal map Sprite Lamp exports for an image, beside it: "Map/Rock.png" looks for Map/Rock_Normal and then
		// Map/Rock_NormalDepth (normals in the colour, depth in the alpha), in the image's own format first. Empty when
		// there is none.
		std::string spriteLampNormalMapBeside(const std::string& a_imagePath);
	}
}

#endif
