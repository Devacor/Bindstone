#ifndef _MV_SCENE_EMITTER_H_
#define _MV_SCENE_EMITTER_H_

#include "Render/Scene/primitives.h"

namespace MV {
	namespace Scene {

		struct ParticleChangeValues {
			AxisAngles directionalChange;
			AxisAngles rotationalChange;

			float beginSpeed = 0.0f;
			float endSpeed = 0.0f;

			Size<> beginScale;
			Size<> endScale;

			Color beginColor;
			Color endColor;

			float maxLifespan = 1.0f;

			float gravityMagnitude = 0.0f;
			AxisAngles gravityDirection;

			float animationFramesPerSecond = 10.0f;
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
			Size<> scale;
			Color color;
			int currentFrame;

			float totalLifespan;

			ParticleChangeValues change;

			std::vector<std::shared_ptr<TextureHandle>> myTextures;

			void setGravity(float a_magnitude, const AxisAngles &a_direction = AxisAngles(0.0f, 180.0f, 0.0f));
		private:
			Point<> gravityConstant;
		};

		struct ParticleSpawnProperties {
			float particleSpawnRate = 1.0f;
			float particleSpawnRateVariance = 0.0f;

			Point<> maximumPosition;
			Point<> minimumPosition;

			AxisAngles maximumDirection;
			AxisAngles minimumDirection;

			AxisAngles maximumRotation;
			AxisAngles minimumRotation;

			ParticleChangeValues minimum;
			ParticleChangeValues maximum;
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

			Random random;
		};
	}
}

#endif
