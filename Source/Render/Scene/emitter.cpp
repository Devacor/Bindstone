#include "emitter.h"

CEREAL_REGISTER_TYPE(MV::Scene::Emitter);

namespace MV {
	namespace Scene {

		bool Particle::update(double a_dt){
			float timeScale = static_cast<float>(a_dt);
			totalLifespan = std::min(totalLifespan + timeScale, change.maxLifespan);

			float mixValue = totalLifespan / change.maxLifespan;

			direction += change.directionalChange * timeScale;
			rotation += change.rotationalChange * timeScale;

			speed = mix(change.beginSpeed, change.endSpeed, mixValue);
			scale = mix(change.beginScale, change.endScale, mixValue);
			color = mix(change.beginColor, change.endColor, mixValue);

			Point<> distance(0.0f, -speed, 0.0f);
			rotatePoint3D(distance.x, distance.y, distance.z, direction.x, direction.y, direction.z);
			position += distance * timeScale;
			position += gravityConstant * timeScale;

			currentFrame = static_cast<int>(boundBetween(static_cast<float>(myTextures.size() * (change.animationFramesPerSecond / timeScale)), 0.0f, static_cast<float>(myTextures.size())));
			return totalLifespan == change.maxLifespan;
		}

		void Particle::setGravity(float a_magnitude, const AxisAngles &a_direction){
			gravityConstant.locate(0.0f, -a_magnitude, 0.0f);
			rotatePoint3D(gravityConstant.x, gravityConstant.y, gravityConstant.z, a_direction.x, a_direction.y, a_direction.z);
		}

		std::shared_ptr<Emitter> Emitter::make(Draw2D* a_renderer) {
			auto emitter = std::shared_ptr<Emitter>(new Emitter(a_renderer));
			a_renderer->registerShader(emitter);
			return emitter;
		}

		std::shared_ptr<Emitter> Emitter::make(Draw2D* a_renderer, const EmitterSpawnProperties &a_values) {
			auto emitter = std::shared_ptr<Emitter>(new Emitter(a_renderer));
			a_renderer->registerShader(emitter);
			emitter->spawnProperties = a_values;
			return emitter;
		}

		void Emitter::drawImplementation() {
			update(std::min(timer.delta(), MAX_TIME_STEP));
			if(!points.empty()){
				defaultDraw(GL_TRIANGLES);
			}
		}

		void Emitter::spawnParticle(){
			Particle particle;

			particle.position = mix(spawnProperties.minimumPosition, spawnProperties.maximumPosition, RandomNumber(0.0f, 1.0f));
			particle.rotation = mix(spawnProperties.minimumRotation, spawnProperties.maximumRotation, RandomNumber(0.0f, 1.0f));
			particle.change.rotationalChange = mix(spawnProperties.minimum.rotationalChange, spawnProperties.maximum.rotationalChange, RandomNumber(0.0f, 1.0f));

			particle.direction = mix(spawnProperties.minimumDirection, spawnProperties.maximumDirection, RandomNumber(0.0f, 1.0f));

			particle.change.beginSpeed = mix(spawnProperties.minimum.beginSpeed, spawnProperties.maximum.beginSpeed, RandomNumber(0.0f, 1.0f));
			particle.change.endSpeed = mix(spawnProperties.minimum.endSpeed, spawnProperties.maximum.endSpeed, RandomNumber(0.0f, 1.0f));

			particle.change.beginScale = mix(spawnProperties.minimum.beginScale, spawnProperties.maximum.beginScale, RandomNumber(0.0f, 1.0f));
			particle.change.endScale = mix(spawnProperties.minimum.endScale, spawnProperties.maximum.endScale, RandomNumber(0.0f, 1.0f));

			particle.change.beginColor = mix(spawnProperties.minimum.beginColor, spawnProperties.maximum.beginColor, RandomNumber(0.0f, 1.0f));
			particle.change.endColor = mix(spawnProperties.minimum.endColor, spawnProperties.maximum.endColor, RandomNumber(0.0f, 1.0f));

			particle.change.animationFramesPerSecond = mix(spawnProperties.minimum.animationFramesPerSecond, spawnProperties.maximum.animationFramesPerSecond, RandomNumber(0.0f, 1.0f));

			particle.change.maxLifespan = mix(spawnProperties.minimum.maxLifespan, spawnProperties.maximum.maxLifespan, RandomNumber(0.0f, 1.0f));

			particle.setGravity(
				mix(spawnProperties.minimum.gravityMagnitude, spawnProperties.maximum.gravityMagnitude, RandomNumber(0.0f, 1.0f)),
				mix(spawnProperties.minimum.gravityDirection, spawnProperties.maximum.gravityDirection, RandomNumber(0.0f, 1.0f))
			);
			
			particles.push_back(particle);
		}

		void Emitter::update(double a_dt) {
			particles.erase(std::remove_if(particles.begin(), particles.end(), [&](Particle& a_particle){
				return a_particle.update(a_dt);
			}), particles.end());
			timeSinceLastParticle += a_dt;
			bool spawned = false;
			int particlesThisFrame = 0;
			while(timeSinceLastParticle > nextSpawnDelta && particles.size() <= spawnProperties.maximumParticles && particlesThisFrame++ < MAX_PARTICLES_PER_FRAME){
				timeSinceLastParticle -= nextSpawnDelta;
				spawnParticle();
				nextSpawnDelta = RandomNumber(spawnProperties.minimumSpawnRate, spawnProperties.maximumSpawnRate);
			}
			if(particlesThisFrame == MAX_PARTICLES_PER_FRAME){
				timeSinceLastParticle = 0.0f;
			}
			loadParticlesToPoints();
		}



		void Emitter::loadParticlesToPoints() {
			points.clear();
			vertexIndices.clear();
			for(auto &particle : particles){
				BoxAABB bounds(scaleToPoint(particle.scale / 2.0f), scaleToPoint(particle.scale / -2.0f));
				std::vector<TexturePoint> texturePoints;
				if(ourTexture){
					texturePoints.push_back({static_cast<float>(ourTexture->percentLeft()), static_cast<float>(ourTexture->percentTop())});
					texturePoints.push_back({static_cast<float>(ourTexture->percentLeft()), static_cast<float>(ourTexture->percentBottom())});
					texturePoints.push_back({static_cast<float>(ourTexture->percentRight()), static_cast<float>(ourTexture->percentBottom())});
					texturePoints.push_back({static_cast<float>(ourTexture->percentRight()), static_cast<float>(ourTexture->percentTop())});
				} else{
					texturePoints.push_back({0.0f, 0.0f});
					texturePoints.push_back({0.0f, 1.0f});
					texturePoints.push_back({1.0f, 1.0f});
					texturePoints.push_back({1.0f, 0.0f});
				}

				for(size_t i = 0;i < 4;++i){
					auto corner = bounds[i];
					rotatePoint3D(corner.x, corner.y, corner.z, particle.rotation.x, particle.rotation.y, particle.rotation.z);
					corner += particle.position;
					points.push_back(DrawPoint(corner, particle.color, texturePoints[i]));
				}

				appendQuadVertexIndices(vertexIndices, static_cast<GLuint>(points.size()));
			}
		}

		const double Emitter::MAX_TIME_STEP = 1.0f;

		MV::Scene::EmitterSpawnProperties loadEmitterProperties(const std::string &a_file) {
			try{
				std::ifstream file(a_file);
				std::shared_ptr<MV::Scene::Node> saveScene;
				cereal::JSONInputArchive archive(file);
				MV::Scene::EmitterSpawnProperties EmitterProperties;
				archive(CEREAL_NVP(EmitterProperties));
				return EmitterProperties;
			} catch(::cereal::RapidJSONException &a_exception){
				std::cerr << "Failed to load emitter: " << a_exception.what() << std::endl;
				return {};
			}
		}

	}
}