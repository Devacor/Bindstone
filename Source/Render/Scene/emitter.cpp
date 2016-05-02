#include "emitter.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Emitter);

namespace MV {
	namespace Scene {

		const double Emitter::MAX_TIME_STEP  = .25;

		Point<> Emitter::randomMix(const Point<>& a_rhs, const Point<>& a_lhs) {
			return{
				mix(a_rhs.x, a_lhs.x, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.y, a_lhs.y, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.z, a_lhs.z, randomNumber(0.0f, 1.0f))
			};
		}

		Color Emitter::randomMix(const Color & a_rhs, const Color & a_lhs) {
			return{
				mix(a_rhs.R, a_lhs.R, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.G, a_lhs.G, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.B, a_lhs.B, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.A, a_lhs.A, randomNumber(0.0f, 1.0f))
			};
		}

		void Emitter::spawnParticle(size_t a_groupIndex) {
			Particle particle;

			particle.position = randomMix(spawnProperties.minimumPosition, spawnProperties.maximumPosition) - threadData[a_groupIndex].particleOffset;
			particle.rotation = randomMix(spawnProperties.minimumRotation, spawnProperties.maximumRotation);
			particle.change.rotationalChange = randomMix(spawnProperties.minimum.rotationalChange, spawnProperties.maximum.rotationalChange);

			particle.direction = randomMix(spawnProperties.minimumDirection, spawnProperties.maximumDirection);
			particle.change.rateOfChange = randomMix(spawnProperties.minimum.rateOfChange, spawnProperties.maximum.rateOfChange);
			particle.change.directionalChange(randomMix(spawnProperties.minimum.directionalChange(), spawnProperties.maximum.directionalChange()));

			particle.change.beginSpeed = mix(spawnProperties.minimum.beginSpeed, spawnProperties.maximum.beginSpeed, randomNumber(0.0f, 1.0f));
			particle.change.endSpeed = mix(spawnProperties.minimum.endSpeed, spawnProperties.maximum.endSpeed, randomNumber(0.0f, 1.0f));

			particle.change.beginScale = mix(spawnProperties.minimum.beginScale, spawnProperties.maximum.beginScale, randomNumber(0.0f, 1.0f));
			particle.change.endScale = mix(spawnProperties.minimum.endScale, spawnProperties.maximum.endScale, randomNumber(0.0f, 1.0f));

			particle.change.beginColor = randomMix(spawnProperties.minimum.beginColor, spawnProperties.maximum.beginColor);
			particle.change.endColor = randomMix(spawnProperties.minimum.endColor, spawnProperties.maximum.endColor);

			particle.change.animationFramesPerSecond = mix(spawnProperties.minimum.animationFramesPerSecond, spawnProperties.maximum.animationFramesPerSecond, randomNumber(0.0f, 1.0f));

			particle.change.maxLifespan = mix(spawnProperties.minimum.maxLifespan, spawnProperties.maximum.maxLifespan, randomNumber(0.0f, 1.0f));

			particle.setGravity(
				mix(spawnProperties.minimum.gravityMagnitude, spawnProperties.maximum.gravityMagnitude, randomNumber(0.0f, 1.0f)),
				randomMix(spawnProperties.minimum.gravityDirection, spawnProperties.maximum.gravityDirection)
			);

			particle.update(0.0f);

			threadData[a_groupIndex].particles.push_back(particle);
		}

		void Emitter::spawnParticlesOnMultipleThreads(double a_dt) {
			timeSinceLastParticle.store(timeSinceLastParticle.load() + a_dt);
			size_t particlesToSpawn = nextSpawnDelta <= 0 ? 0 : static_cast<size_t>(timeSinceLastParticle.load() / nextSpawnDelta);
			size_t totalParticles = std::min(std::accumulate(threadData.begin(), threadData.end(), static_cast<size_t>(0), [](size_t a_total, ThreadData& a_group) {return a_group.particles.size() + a_total; }), static_cast<size_t>(spawnProperties.maximumParticles));

			particlesToSpawn = std::min(particlesToSpawn + totalParticles, static_cast<size_t>(spawnProperties.maximumParticles)) - totalParticles;

			double maxParticlesPerFramePerThread = 500;

			if (particlesToSpawn >= emitterThreads) {
				ThreadPool::TaskList spawnTasks;
				for (size_t currentThread = 0; currentThread < emitterThreads; ++currentThread) {
					spawnTasks.emplace_back([=]() {
						for (size_t count = 0; count < maxParticlesPerFramePerThread && (count < particlesToSpawn / emitterThreads); ++count) {
							spawnParticle(currentThread);
						}
					});
				}
				timeSinceLastParticle = 0.0f;
				nextSpawnDelta = randomNumber(spawnProperties.minimumSpawnRate, spawnProperties.maximumSpawnRate);

				pool.tasks(spawnTasks, [=]() {
					updateParticlesOnMultipleThreads(a_dt);
				}, false);
			} else if (particlesToSpawn > 0) {
				auto randomOffset = randomInteger(0, emitterThreads);
				for (size_t count = 0; count < particlesToSpawn; ++count) {
					spawnParticle((count + randomOffset) % emitterThreads);
				}

				timeSinceLastParticle = 0.0f;
				nextSpawnDelta = randomNumber(spawnProperties.minimumSpawnRate, spawnProperties.maximumSpawnRate);
				updateParticlesOnMultipleThreads(a_dt);
			} else {
				updateParticlesOnMultipleThreads(a_dt);
			}
		}

		void Emitter::updateParticlesOnMultipleThreads(double a_dt) {
			ThreadPool::TaskList spawnTasks;
			for (size_t currentThread = 0; currentThread < emitterThreads; ++currentThread) {
				spawnTasks.emplace_back([=]() {
					threadData[currentThread].particles.erase(std::remove_if(threadData[currentThread].particles.begin(), threadData[currentThread].particles.end(), [&](Particle& a_particle) {
						return a_particle.update(a_dt);
					}), threadData[currentThread].particles.end());
					loadParticlesToPoints(currentThread);
				});
			}
			auto tmpUpdate = pool.tasks(spawnTasks, [=]() {
				loadParticlePointsFromGroups();
			}, false);
		}

		void Emitter::loadParticlesToPoints(size_t a_groupIndex) {
			threadData[a_groupIndex].points.clear();
			threadData[a_groupIndex].vertexIndices.clear();

			std::vector<TexturePoint> texturePoints;
			if (ourTexture) {
				texturePoints.push_back({ static_cast<float>(ourTexture->percentBounds().minPoint.x), static_cast<float>(ourTexture->percentBounds().minPoint.y) });
				texturePoints.push_back({ static_cast<float>(ourTexture->percentBounds().minPoint.x), static_cast<float>(ourTexture->percentBounds().maxPoint.y) });
				texturePoints.push_back({ static_cast<float>(ourTexture->percentBounds().maxPoint.x), static_cast<float>(ourTexture->percentBounds().maxPoint.y) });
				texturePoints.push_back({ static_cast<float>(ourTexture->percentBounds().maxPoint.x), static_cast<float>(ourTexture->percentBounds().minPoint.y) });
			} else {
				texturePoints.push_back({ 0.0f, 0.0f });
				texturePoints.push_back({ 0.0f, 1.0f });
				texturePoints.push_back({ 1.0f, 1.0f });
				texturePoints.push_back({ 1.0f, 0.0f });
			}
			for (auto &&particle : threadData[a_groupIndex].particles) {
				BoxAABB<> bounds(toPoint(particle.scale / 2.0f), toPoint(particle.scale / -2.0f));
				
				appendQuadVertexIndices(threadData[a_groupIndex].vertexIndices, static_cast<GLuint>(threadData[a_groupIndex].points.size()));
				
				for (size_t i = 0; i < 4; ++i) {
					auto corner = bounds[i];
					rotatePoint(corner, particle.rotation);
					corner += particle.position;
					threadData[a_groupIndex].points.push_back(DrawPoint(corner, particle.color, texturePoints[i]));
				}
			}
		}

		void Emitter::loadParticlePointsFromGroups() {
			pointBuffer.clear();
			vertexIndexBuffer.clear();

			size_t pointCount = 0;
			size_t vertexCount = 0;
			for (int group = 0; group < emitterThreads; ++group) {
				pointCount += threadData[group].points.size();
				vertexCount += threadData[group].vertexIndices.size();
			}

			pointBuffer.resize(pointCount);
			vertexIndexBuffer.resize(vertexCount);

			size_t pointOffset = 0;
			size_t vertexOffset = 0;
			ThreadPool::TaskList copyTasks;
			for (int group = 0; group < emitterThreads; ++group) {
				copyTasks.emplace_back([=]() {
					size_t indexSize = threadData[group].vertexIndices.size();
					moveCopy(pointBuffer, threadData[group].points, pointOffset);
					moveCopy(vertexIndexBuffer, threadData[group].vertexIndices, vertexOffset);
					for (size_t index = vertexOffset; index < vertexOffset + indexSize; ++index) {
						vertexIndexBuffer[index] += static_cast<GLuint>(pointOffset);
					}
				});
				pointOffset += threadData[group].points.size();
				vertexOffset += threadData[group].vertexIndices.size();
			}
			pool.tasks(copyTasks, [=]() {
				loadPointsFromBufferAndAllowUpdate();
			}, false);
		}

		void Emitter::loadPointsFromBufferAndAllowUpdate() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			points.clear();
			vertexIndices.clear();
			std::swap(points, pointBuffer);
			std::swap(vertexIndices, vertexIndexBuffer);
			updateInProgress.store(false);
		}

		std::weak_ptr<MV::Scene::Node> Emitter::relativeEmission() const {
			return relativeNodePosition;
		}

		std::shared_ptr<Emitter> Emitter::relativeEmission(std::shared_ptr<MV::Scene::Node> a_newRelativePosition) {
			relativeNodePosition = a_newRelativePosition;
			auto current = owner()->parent();
			int count = 0;
			while (current && current != a_newRelativePosition) {
				current = current->parent();
				++count;
			}
			if (current != a_newRelativePosition) {
				count = -1;
			}
			relativeParentCount = count;
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		std::shared_ptr<Emitter> Emitter::removeRelativeEmission() {
			relativeParentCount = -1;
			relativeNodePosition = std::weak_ptr<MV::Scene::Node>();
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		std::shared_ptr<Emitter> Emitter::makeRelativeToParent(int a_count) {
			relativeParentCount = a_count;
			relativeNodePosition.reset();
			if (a_count > 0) {
				relativeNodePosition = owner()->parent();
				--a_count;
				while (a_count > 0 && relativeNodePosition.lock()->parent()) {
					relativeNodePosition = relativeNodePosition.lock()->parent();
					--a_count;
				}
			}
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		std::shared_ptr<Emitter> Emitter::properties(const EmitterSpawnProperties & a_emitterProperties) {
			spawnProperties = a_emitterProperties;
			nextSpawnDelta = randomNumber(spawnProperties.minimumSpawnRate, spawnProperties.maximumSpawnRate);
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		const EmitterSpawnProperties & Emitter::properties() const {
			return spawnProperties;
		}

		EmitterSpawnProperties & Emitter::properties() {
			nextSpawnDelta = 0.0f; //zero this out since we return a reference to the properties and may need to re-evaluate our nextSpawnDelta.
			return spawnProperties;
		}

		bool Emitter::enabled() const {
			return spawnParticles;
		}

		bool Emitter::disabled() const {
			return !spawnParticles;
		}

		std::shared_ptr<Emitter> Emitter::enable() {
			spawnParticles = true;
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		std::shared_ptr<Emitter> Emitter::disable() {
			spawnParticles = false;
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		void Emitter::updateImplementation(double a_dt) {
			if (owner()->renderer().headless()) { return; }

			bool falseValue = false;
			accumulatedTimeDelta += a_dt;
			if (relativeNodePosition.expired() && relativeParentCount >= 0) {
				makeRelativeToParent(relativeParentCount);
			}
			if (updateInProgress.compare_exchange_strong(falseValue, true)) {
				MV::Point<> particleOffset;
				if (!relativeNodePosition.expired()) {
					particleOffset = owner()->localFromWorld(relativeNodePosition.lock()->worldPosition());
				}
				for (auto&& data : threadData) {
					data.particleOffset = particleOffset;
				}
				pool.task([&]() {
					double dt = std::min(accumulatedTimeDelta, MAX_TIME_STEP);
					accumulatedTimeDelta = 0.0;

					if (nextSpawnDelta == 0.0) {
						nextSpawnDelta = randomNumber(spawnProperties.minimumSpawnRate, spawnProperties.maximumSpawnRate);
					}
					if (enabled()) {
						spawnParticlesOnMultipleThreads(dt);
					} else {
						updateParticlesOnMultipleThreads(dt);
					}
				});
			}
		}

		Emitter::~Emitter() {
			while (updateInProgress.load()) {
				std::this_thread::sleep_for(std::chrono::nanoseconds(100));
			}
		}

		Emitter::Emitter(const std::weak_ptr<Node> &a_owner, ThreadPool &a_pool) :
			Drawable(a_owner),
			pool(a_pool),
			emitterThreads(a_pool.threads()),
			threadData(emitterThreads),
			updateInProgress(false){

			points.resize(4);
			clearTexturePoints(points);
			appendQuadVertexIndices(vertexIndices, 0);
		}

		BoxAABB<> Emitter::boundsImplementation() {
			return{ spawnProperties.minimumPosition, spawnProperties.maximumPosition };
		}

		std::shared_ptr<Component> Emitter::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Drawable::cloneHelper(a_clone);
			auto emitterClone = std::static_pointer_cast<Emitter>(a_clone);
			emitterClone->emitterThreads = emitterThreads;
			emitterClone->spawnProperties = spawnProperties;
			emitterClone->spawnParticles = spawnParticles;
			return a_clone;
		}

		MV::Scene::EmitterSpawnProperties loadEmitterProperties(const std::string &a_file) {
			try {
				std::ifstream file(a_file);
				std::shared_ptr<MV::Scene::Node> saveScene;
				cereal::JSONInputArchive archive(file);
				EmitterSpawnProperties EmitterProperties;
				archive(CEREAL_NVP(EmitterProperties));
				return EmitterProperties;
			} catch (::cereal::RapidJSONException &a_exception) {
				std::cerr << "Failed to load emitter: " << a_exception.what() << std::endl;
				return{};
			}
		}

		void Emitter::defaultDrawImplementation() {
			if (owner()->renderer().headless()) { return; }

			std::lock_guard<std::recursive_mutex> guard(lock);
			if (!vertexIndices.empty()) {
				require<ResourceException>(shaderProgram, "No shader program for Drawable!");
				shaderProgram->use();

				if (bufferId == 0) {
					glGenBuffers(1, &bufferId);
				}

				glBindBuffer(GL_ARRAY_BUFFER, bufferId);
				auto structSize = static_cast<GLsizei>(sizeof(points[0]));
				glBufferData(GL_ARRAY_BUFFER, points.size() * structSize, &(points[0]), GL_STATIC_DRAW);

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);

				auto positionOffset = static_cast<size_t>(offsetof(DrawPoint, x));
				auto textureOffset = static_cast<size_t>(offsetof(DrawPoint, textureX));
				auto colorOffset = static_cast<size_t>(offsetof(DrawPoint, R));
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)positionOffset); //Point
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)textureOffset); //UV
				glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)colorOffset); //Color

				shaderProgram->set("texture", ourTexture);
				auto emitterSpace = relativeNodePosition.expired() ? owner() : relativeNodePosition.lock();
				shaderProgram->set("transformation", emitterSpace->renderer().projectionMatrix().top() * emitterSpace->worldTransform());

				glDrawElements(drawType, static_cast<GLsizei>(vertexIndices.size()), GL_UNSIGNED_INT, &vertexIndices[0]);

				glDisableVertexAttribArray(0);
				glDisableVertexAttribArray(1);
				glDisableVertexAttribArray(2);
				glUseProgram(0);
			}
		}

	}
}
