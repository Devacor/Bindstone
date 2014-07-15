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

		const double Emitter::TIME_BETWEEN_UPDATES = 1.0 / 120.0;

		std::shared_ptr<Emitter> Emitter::make(Draw2D* a_renderer) {
			auto emitter = std::shared_ptr<Emitter>(new Emitter(a_renderer));
			a_renderer->registerShader(emitter);
			return emitter;
		}

		void Emitter::drawImplementation() {
			while(timer.frame(TIME_BETWEEN_UPDATES)){
				update(TIME_BETWEEN_UPDATES);
			}
			defaultDraw(GL_TRIANGLES);
		}

		void Emitter::spawnParticle(){
			Particle particle;

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
			while(timeSinceLastParticle > spawnProperties.particleSpawnRate){
				timeSinceLastParticle -= spawnProperties.particleSpawnRate;
				spawnParticle();
			}
			loadParticlesToPoints();
		}



		void Emitter::loadParticlesToPoints() {
			points.clear();
			vertexIndices.clear();
			for(auto &particle : particles){
				BoxAABB bounds(sizeToPoint(particle.scale / 2.0f), sizeToPoint(particle.scale / -2.0f));
				std::vector<Point<>> corners;
				corners.push_back(bounds.topLeftPoint());
				corners.push_back(bounds.topRightPoint());
				corners.push_back(bounds.bottomRightPoint());
				corners.push_back(bounds.bottomLeftPoint());

				for(auto& corner : corners){
					rotatePoint3D(corner.x, corner.y, corner.z, particle.rotation.x, particle.rotation.y, particle.rotation.z);
					corner += particle.position;
					points.push_back(DrawPoint(corner, particle.color));
				}

				appendQuadVertexIndices(vertexIndices, static_cast<GLuint>(points.size()));
			}
		}

	}
}