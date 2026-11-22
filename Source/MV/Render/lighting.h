#ifndef _MV_RENDER_LIGHTING_H_
#define _MV_RENDER_LIGHTING_H_

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace MV {
	namespace Render {

		// One point light as lit shaders see it, in the render world (Draw2D::worldFromLocal's space: y down, the
		// camera applied), so a light and a drawable in different layers meet in one space.
		struct PointLight {
			float x = 0.0f;
			float y = 0.0f;
			float height = 0.0f;   // above the plane, toward the viewer
			float radius = 0.0f;   // reach on the plane
			float red = 0.0f;      // colour times intensity, pulse included
			float green = 0.0f;
			float blue = 0.0f;
			float falloff = 1.0f;  // exponent of the fade from the centre to the radius
			const void* scope = nullptr;   // the scene (its root node) the light belongs to; it lights only that scene
		};

		// The renderer's lights: each source registers a gather that fills in its light, and lights() asks every source
		// once per frame, so a lit draw reads a list instead of walking the scene.
		class Lighting {
		public:
			// The most lights one draw receives; a lit shader declares its arrays with this length.
			static constexpr size_t LIGHTS_PER_DRAW = 16;

			using Gather = std::function<bool(PointLight&)>;

			void add(const void* a_source, Gather a_gather) {
				remove(a_source);
				sources.emplace_back(a_source, std::move(a_gather));
				stale = true;
			}
			void remove(const void* a_source) {
				sources.erase(std::remove_if(sources.begin(), sources.end(), [&](const auto& a_entry) { return a_entry.first == a_source; }), sources.end());
				stale = true;
			}
			size_t sourceCount() const { return sources.size(); }

			// The next lights() asks the sources again.
			void beginFrame() { stale = true; }

			const std::vector<PointLight>& lights() {
				if (stale) {
					stale = false;
					current.clear();
					PointLight light;
					for (auto&& [source, gather] : sources) {
						if (gather(light)) {
							current.push_back(light);
						}
						light = PointLight{};
					}
				}
				return current;
			}

			// Light every lit surface gets: white leaves unlit art as it is painted, and the point lights add to it.
			void ambient(float a_red, float a_green, float a_blue) {
				ambientLight[0] = a_red;
				ambientLight[1] = a_green;
				ambientLight[2] = a_blue;
			}
			const float* ambient() const { return ambientLight; }

		private:
			std::vector<std::pair<const void*, Gather>> sources;
			std::vector<PointLight> current;
			float ambientLight[3] = { 1.0f, 1.0f, 1.0f };
			bool stale = true;
		};

	}
}

#endif
