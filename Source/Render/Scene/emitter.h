#ifndef _MV_SCENE_EMITTER_H_
#define _MV_SCENE_EMITTER_H_

#include "Render/Scene/primitives.h"

namespace MV {
	namespace Scene {

		struct Particle {
			//return true if dead.
			bool update(double a_dt){
				float timeScale = static_cast<float>(a_dt);
				totalLifespan += timeScale;

				direction += directionalChange * timeScale;
				rotationalChange += rotationalChange * timeScale;
				scale += scaleChange * timeScale;
				color += colorChange * timeScale;
				speed += speedChange * timeScale;

				Point<> normal(0.0f, -speed, 0.0f);
				rotatePoint3D(normal.x, normal.y, normal.z, direction.x, direction.y, direction.z);
				position += normal * speed * timeScale;

				currentFrame = static_cast<int>(boundBetween(static_cast<float>(myTextures.size() * (myAnimationSpeed/timeScale)), 0.0f, static_cast<float>(myTextures.size())));
				return totalLifespan >= maxLifespan;
			}

			void reset(){
				totalLifespan = 0.0f;
			}

			Point<> position;
			float speed;
			AxisAngles direction;
			AxisAngles rotation;
			Size<> scale;
			Color color;

			float speedChange;
			AxisAngles directionalChange;
			AxisAngles rotationalChange;
			Size<> scaleChange;
			Color colorChange;

			std::vector<std::shared_ptr<TextureHandle>> myTextures;
			float myAnimationSpeed = 1.0f;

			int currentFrame;

			float totalLifespan;
			float maxLifespan;
		};

		struct ParticleSpawnProperties {
			float particleSpawnRate = .25f;
			float particleSpawnRateVariance = .0f;

			Point<> positionVariance;
			float speedVariance = 0.0f;
			AxisAngles rotationVariance;
			Size<> scaleVariance;
			Color colorVariance;
		};

		class Emitter :
			public Node{

			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS(Emitter)

			static std::shared_ptr<Emitter> make(Draw2D* a_renderer);
		protected:
			Emitter(Draw2D *a_renderer):
				Node(a_renderer){
			}
			virtual void drawImplementation();
		private:
			void update(double a_dt);
			void spawnParticle();

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("node", cereal::base_class<Node>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Emitter> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require(renderer != nullptr, MV::PointerException("Error: Failed to load a renderer for Sliced node."));
				construct(renderer);
				archive(cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}
			void loadParticlesToPoints();

			Stopwatch timer;

			ParticleSpawnProperties spawnProperties;

			std::vector<Particle> particles;

			static const double TIME_BETWEEN_UPDATES;
			double timeSinceLastParticle = 0.0;
		};
	}
}

#endif