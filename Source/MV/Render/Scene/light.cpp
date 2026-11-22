#include "light.h"
#include "MV/Render/sharedTextures.h"
#include <array>
#include <cmath>

#include <jaiscript/core/dynamic_binder.hpp>

namespace MV {
	namespace Scene {
		void Light::jai_auto_bind(jai::dynamic_binder<Light>& a_builder) {
			a_builder.method("pulse", &Light::pulse);
		}

		Light::Light(const std::weak_ptr<Node>& a_owner) :
			jai::property_owner<Light, Component>(a_owner) {
		}

		Light::~Light() {
			leaveLighting();
		}

		void Light::initialize() {
			joinLighting();
		}

		// The component meets its node before the node is in the tree when a scene loads; join again once it is.
		void Light::postLoadInitialize() {
			joinLighting();
		}

		void Light::reattachImplementation() {
			joinLighting();
		}

		void Light::detachImplementation() {
			leaveLighting();
		}

		void Light::updateImplementation(double a_delta) {
			pulseSeconds += a_delta;
		}

		void Light::joinLighting() {
			if (!ownerIsAlive()) {
				return;
			}
			auto lighting = owner()->renderer().lightingHandle().lock();
			if (auto joined = joinedLighting.lock(); joined && joined != lighting) {
				joined->remove(this);
			}
			if (lighting) {
				lighting->add(this, [this](Render::PointLight& a_light) { return gather(a_light); });
				joinedLighting = lighting;
			}
		}

		void Light::leaveLighting() {
			if (auto joined = joinedLighting.lock()) {
				joined->remove(this);
			}
			joinedLighting.reset();
		}

		bool Light::gather(Render::PointLight& a_light) {
			if (!ownerIsAlive() || *ourRadius <= 0.0f) {
				return false;
			}
			auto node = owner();
			if (!node->visible()) {
				return false;
			}
			constexpr double TAU = 6.283185307179586;
			PointPrecision brightness = *ourIntensity;
			if (*ourPulseAmplitude != 0.0f && *ourPulsePeriod > 0.0f) {
				brightness *= std::max(0.0f, 1.0f + *ourPulseAmplitude * static_cast<PointPrecision>(std::sin(TAU * (pulseSeconds / *ourPulsePeriod + *ourPulsePhase))));
			}
			if (brightness <= 0.0f) {
				return false;
			}
			auto centre = node->worldFromLocal(Point<>());
			auto acrossX = node->worldFromLocal(Point<>(*ourRadius, 0.0f)) - centre;
			auto acrossY = node->worldFromLocal(Point<>(0.0f, *ourRadius)) - centre;
			PointPrecision worldPerLocal = (std::sqrt(acrossX.x * acrossX.x + acrossX.y * acrossX.y) + std::sqrt(acrossY.x * acrossY.x + acrossY.y * acrossY.y)) / (2.0f * *ourRadius);

			a_light.x = centre.x;
			a_light.y = centre.y;
			a_light.height = *ourHeight * worldPerLocal;
			a_light.radius = *ourRadius * worldPerLocal;
			a_light.red = ourColor->R * brightness;
			a_light.green = ourColor->G * brightness;
			a_light.blue = ourColor->B * brightness;
			a_light.falloff = std::max(*ourFalloff, 0.01f);
			a_light.scope = node->root().get();
			return a_light.radius > 0.0f;
		}

		namespace {
			struct LightUniformNames {
				std::array<std::string, Render::Lighting::LIGHTS_PER_DRAW> positions;
				std::array<std::string, Render::Lighting::LIGHTS_PER_DRAW> colors;
			};

			const LightUniformNames& lightUniformNames() {
				static const LightUniformNames result = [] {
					LightUniformNames names;
					for (size_t i = 0; i < Render::Lighting::LIGHTS_PER_DRAW; ++i) {
						names.positions[i] = "lightPositions[" + std::to_string(i) + "]";
						names.colors[i] = "lightColors[" + std::to_string(i) + "]";
					}
					return names;
				}();
				return result;
			}

			PointPrecision distanceToBox(const Render::PointLight& a_light, const BoxAABB<>& a_box) {
				PointPrecision outsideX = std::max({ a_box.minPoint.x - a_light.x, 0.0f, a_light.x - a_box.maxPoint.x });
				PointPrecision outsideY = std::max({ a_box.minPoint.y - a_light.y, 0.0f, a_light.y - a_box.maxPoint.y });
				return std::sqrt(outsideX * outsideX + outsideY * outsideY);
			}

			// Clip space to the render world; the y flip follows the device's clip space as setUniformMatrix corrects it.
			std::array<float, 4> worldFromClip(Draw2D& a_renderer) {
				const auto& worldSize = a_renderer.world().size();
				bool clipYDown = a_renderer.device() && a_renderer.device()->caps().clipSpace == Render::ClipSpace::ZeroToOne_YDown;
				return { worldSize.width * 0.5f, worldSize.height * (clipYDown ? 0.5f : -0.5f), worldSize.width * 0.5f, worldSize.height * 0.5f };
			}
		}

		void applyLights(Shader& a_shader, Draw2D& a_renderer, const BoxAABB<>& a_worldBounds, const void* a_scope) {
			auto& lighting = a_renderer.lighting();
			struct Reaching {
				const Render::PointLight* light;
				PointPrecision strength;
			};
			std::array<Reaching, Render::Lighting::LIGHTS_PER_DRAW> chosen;
			size_t count = 0;
			for (auto&& light : lighting.lights()) {
				if (light.scope != a_scope) {
					continue;
				}
				PointPrecision reach = 1.0f - distanceToBox(light, a_worldBounds) / light.radius;
				if (reach <= 0.0f) {
					continue;
				}
				PointPrecision strength = (light.red + light.green + light.blue) * std::pow(reach, light.falloff);
				if (count < chosen.size()) {
					chosen[count++] = { &light, strength };
				} else {
					auto weakest = std::min_element(chosen.begin(), chosen.end(), [](const Reaching& a_left, const Reaching& a_right) { return a_left.strength < a_right.strength; });
					if (weakest->strength < strength) {
						*weakest = { &light, strength };
					}
				}
			}

			const auto& names = lightUniformNames();
			for (size_t i = 0; i < count; ++i) {
				const auto& light = *chosen[i].light;
				const float position[4] = { light.x, light.y, light.height, light.radius };
				const float color[4] = { light.red, light.green, light.blue, light.falloff };
				a_shader.setFloats(names.positions[i], position, 4);
				a_shader.setFloats(names.colors[i], color, 4);
			}
			a_shader.set("lightCount", static_cast<PointPrecision>(count), false);
			a_shader.setFloats("ambient", lighting.ambient(), 3);
			auto mapping = worldFromClip(a_renderer);
			a_shader.setFloats("worldFromClip", mapping.data(), 4);
		}

		std::string spriteLampNormalMapBeside(const std::string& a_imagePath) {
			auto extensionAt = a_imagePath.find_last_of('.');
			if (a_imagePath.empty() || extensionAt == std::string::npos || a_imagePath.find_first_of("/\\", extensionAt) != std::string::npos) {
				return {};
			}
			std::string stem = a_imagePath.substr(0, extensionAt);
			std::vector<std::string> extensions{ a_imagePath.substr(extensionAt) };
			for (auto&& extension : SharedTextures::validExtensions) {
				if (extension != extensions.front()) {
					extensions.push_back(extension);
				}
			}
			for (const char* suffix : { "_Normal", "_NormalDepth" }) {
				for (auto&& extension : extensions) {
					std::string candidate = stem + suffix + extension;
					if (fileExistsInSearchPaths(candidate)) {
						return candidate;
					}
				}
			}
			return {};
		}
	}
}
