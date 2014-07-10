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
			particle.direction.locate(15.0f, 15.0f, 15.0f);
			particle.rotation.locate(15.0f, 15.0f, 15.0f);
			particle.speed = 15.0f;
			particle.speedChange = -5.0f;
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
				BoxAABB bounds(particle.position - sizeToPoint(particle.scale / 2.0f), particle.position + sizeToPoint(particle.scale / 2.0f));
				points.push_back(DrawPoint(bounds.topLeftPoint(), particle.color));
				points.push_back(DrawPoint(bounds.topRightPoint(), particle.color));
				points.push_back(DrawPoint(bounds.bottomRightPoint(), particle.color));
				points.push_back(DrawPoint(bounds.bottomLeftPoint(), particle.color));
				appendQuadVertexIndices(vertexIndices, points.size());
			}
		}

	}
}