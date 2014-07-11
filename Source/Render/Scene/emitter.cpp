#include "emitter.h"

CEREAL_REGISTER_TYPE(MV::Scene::Emitter);

namespace MV {
	namespace Scene {

		const double Emitter::TIME_BETWEEN_UPDATES = 1.0 / 60.0;

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
			particle.color.set(.25f, .45f, .75f);
			particle.direction.locate(0.0f, 0.0f, 15.0f);
			particle.rotation.locate(0.0f, 0.0f, 15.0f);
			particle.rotationalChange.locate(0.0f, 0.0f, 15.0f);
			particle.speed = 0.0f;
			particle.speedChange = 5.0f * (random.number(0, 1) ? 1.0f : -1.0f);
			particle.scale.set(20.0f, 20.0f);
			particle.maxLifespan = 5.0f;
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