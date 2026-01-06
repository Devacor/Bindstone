#include "emitter.h"

#include "MV/Serialization/serialize.h"
#include "MV/Utility/generalUtility.h"
#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"
#include <jaiscript/properties/property_cereal.hpp>

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

// JaiScript binding for Emitter
static jai::registrar<MV::Scene::Emitter, MV::Services> _hookEmitter("Emitter",
	[](jai::dynamic_binder<MV::Scene::Emitter>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Drawable>();
	builder.auto_bind();

	// Relative emission
	builder.method("relativeEmission", static_cast<std::weak_ptr<MV::Scene::Node>(MV::Scene::Emitter::*)() const>(&MV::Scene::Emitter::relativeEmission));
	builder.method("relativeEmission", static_cast<std::shared_ptr<MV::Scene::Emitter>(MV::Scene::Emitter::*)(std::shared_ptr<MV::Scene::Node>)>(&MV::Scene::Emitter::relativeEmission));
	builder.method("removeRelativeEmission", &MV::Scene::Emitter::removeRelativeEmission);
	builder.method("makeRelativeToParent", &MV::Scene::Emitter::makeRelativeToParent);

	// Properties
	builder.method("properties", static_cast<const MV::Scene::EmitterSpawnProperties&(MV::Scene::Emitter::*)() const>(&MV::Scene::Emitter::properties));
	builder.method("properties", static_cast<std::shared_ptr<MV::Scene::Emitter>(MV::Scene::Emitter::*)(const MV::Scene::EmitterSpawnProperties&)>(&MV::Scene::Emitter::properties));

	// Enable/disable
	builder.method("enabled", &MV::Scene::Emitter::enabled);
	builder.method("disabled", &MV::Scene::Emitter::disabled);
	builder.method("enable", &MV::Scene::Emitter::enable);
	builder.method("disable", &MV::Scene::Emitter::disable);
});

CEREAL_REGISTER_TYPE(MV::Scene::Emitter);
CEREAL_CLASS_VERSION(MV::Scene::ParticleChangeValues, 2);
CEREAL_CLASS_VERSION(MV::Scene::EmitterSpawnProperties, 2);
CEREAL_CLASS_VERSION(MV::Scene::Emitter, 2);
CEREAL_REGISTER_DYNAMIC_INIT(mv_sceneemitter);

namespace MV {
	namespace Scene {

		const double Emitter::MAX_TIME_STEP  = .25;

		Point<> randomMix(const Point<>& a_rhs, const Point<>& a_lhs) {
			return{
				mix(a_rhs.x, a_lhs.x, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.y, a_lhs.y, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.z, a_lhs.z, randomNumber(0.0f, 1.0f))
			};
		}

		Color randomMix(const Color & a_rhs, const Color & a_lhs) {
			return{
				mix(a_rhs.R, a_lhs.R, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.G, a_lhs.G, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.B, a_lhs.B, randomNumber(0.0f, 1.0f)),
				mix(a_rhs.A, a_lhs.A, randomNumber(0.0f, 1.0f))
			};
		}

		void EmitterSpawnProperties::initializeCallbacks() {
			if (!dirty) { return; }

			if (minimumPosition == maximumPosition) {
				getPosition = [this] {return *minimumPosition; };
			} else {
				getPosition = [this] { return randomMix(minimumPosition, maximumPosition); };
			}

			if (minimumRotation == maximumRotation) {
				getRotation = [this] {return *minimumRotation; };
			} else {
				getRotation = [this] {return randomMix(minimumRotation, maximumRotation); };
			}

			if (minimumDirection == maximumDirection) {
				getDirection = [this] {return *minimumDirection; };
			} else {
				getDirection = [this] {return randomMix(minimumDirection, maximumDirection); };
			}

			if (minimum->rotationalChange == maximum->rotationalChange) {
				getRotationChange = [this] {return minimum->rotationalChange; };
			} else {
				getRotationChange = [this] {return randomMix(minimum->rotationalChange, maximum->rotationalChange); };
			}

			if (minimum->rateOfChange == maximum->rateOfChange) {
				getRateOfChange = [this] {return minimum->rateOfChange; };
			} else {
				getRateOfChange = [this] {return randomMix(minimum->rateOfChange, maximum->rateOfChange); };
			}

			if (minimum->directionalChangeRad() == maximum->directionalChangeRad()) {
				getDirectionChange = [this] {return minimum->directionalChangeRad(); };
			} else {
				getDirectionChange = [this] {return randomMix(minimum->directionalChangeRad(), maximum->directionalChangeRad()); };
			}

			if (MV::equals(minimum->beginSpeed, maximum->beginSpeed) && MV::equals(minimum->endSpeed, maximum->endSpeed)) {
				setSpeed = [this](float& begin, float& end) { begin = minimum->beginSpeed; end = minimum->endSpeed; };
			} else {
				setSpeed = [this](float& begin, float& end) {
					begin = mix(minimum->beginSpeed, maximum->beginSpeed, randomNumber(0.0f, 1.0f));
					end = mix(minimum->endSpeed, maximum->endSpeed, randomNumber(0.0f, 1.0f));
				};
			}

			if (minimum->beginScale == maximum->beginScale && minimum->endScale == maximum->endScale) {
				setScale = [this](Scale& begin, Scale& end) { begin = minimum->beginScale; end = minimum->endScale; };
			} else {
				setScale = [this](Scale& begin, Scale& end) {
					begin = mix(minimum->beginScale, maximum->beginScale, randomNumber(0.0f, 1.0f));
					end = mix(minimum->endScale, maximum->endScale, randomNumber(0.0f, 1.0f));
				};
			}

			if (minimum->beginColor == maximum->beginColor && minimum->endColor == maximum->endColor) {
				setColor = [this](Color& begin, Color& end) { begin = minimum->beginColor; end = minimum->endColor; };
			} else {
				setColor = [this](Color& begin, Color& end) {
					begin = randomMix(minimum->beginColor, maximum->beginColor);
					end = randomMix(minimum->endColor, maximum->endColor);
				};
			}

			dirty = false;
		}

		void Emitter::spawnParticlesOnMultipleThreads(double a_dt) {
			timeSinceLastParticle.store(timeSinceLastParticle.load() + a_dt);
			size_t particlesToSpawn = nextSpawnDelta <= 0 ? 0 : static_cast<size_t>(timeSinceLastParticle.load() / nextSpawnDelta);
			size_t totalParticles = std::min(std::accumulate(threadData.begin(), threadData.end(), static_cast<size_t>(0), [](size_t a_total, ThreadData& a_group) {return a_group.particles.size() + a_total; }), static_cast<size_t>(*spawnProperties->maximumParticles));

			particlesToSpawn = std::min(particlesToSpawn + totalParticles, static_cast<size_t>(*spawnProperties->maximumParticles)) - totalParticles;

			double maxParticlesPerFramePerThread = 500;

			if (particlesToSpawn >= emitterThreads) {
				std::vector<MV::ThreadPool::Job> spawnTasks;
				spawnTasks.reserve(emitterThreads);
				for (size_t currentThread = 0; currentThread < emitterThreads; ++currentThread) {
					spawnTasks.emplace_back([this, currentThread, a_dt, particlesToSpawn, maxParticlesPerFramePerThread]() {
						size_t particlesToSpawnThisThread = std::min(particlesToSpawn / emitterThreads, static_cast<size_t>(maxParticlesPerFramePerThread));
						threadData[currentThread].particles.reserve(particlesToSpawnThisThread);
						for (size_t count = 0; count < particlesToSpawnThisThread; ++count) {
							spawnParticle(currentThread);
						}
					});
				}
				timeSinceLastParticle = 0.0f;
				nextSpawnDelta = randomNumber(spawnProperties->minimumSpawnRate, spawnProperties->maximumSpawnRate);

				pool.tasks(spawnTasks, [this, a_dt]() {
					updateParticlesOnMultipleThreads(a_dt);
				});
			} else if (particlesToSpawn > 0) {
				auto randomOffset = randomInteger(0, emitterThreads);
				for (size_t count = 0; count < particlesToSpawn; ++count) {
					spawnParticle((count + randomOffset) % emitterThreads);
				}

				timeSinceLastParticle = 0.0f;
				nextSpawnDelta = randomNumber(spawnProperties->minimumSpawnRate, spawnProperties->maximumSpawnRate);
				updateParticlesOnMultipleThreads(a_dt);
			} else {
				updateParticlesOnMultipleThreads(a_dt);
			}
		}

		void Emitter::updateParticlesOnMultipleThreads(double a_dt) {
			std::vector<MV::ThreadPool::Job> spawnTasks;
			spawnTasks.reserve(emitterThreads);
			for (size_t currentThread = 0; currentThread < emitterThreads; ++currentThread) {
				spawnTasks.emplace_back([this, currentThread, a_dt]() {
					threadData[currentThread].particles.erase(std::remove_if(threadData[currentThread].particles.begin(), threadData[currentThread].particles.end(), [&](Particle& a_particle) {
						return a_particle.update(a_dt);
					}), threadData[currentThread].particles.end());
					loadParticlesToPoints(currentThread);
				});
			}
			pool.tasks(spawnTasks, [this]() {
				loadParticlePointsFromGroups();
			});
		}

		void Emitter::loadParticlesToPoints(size_t a_groupIndex) {
			threadData[a_groupIndex].points.clear();
			threadData[a_groupIndex].vertexIndices.clear();

			std::vector<TexturePoint> texturePoints;
			texturePoints.reserve(4);
			auto foundTexture = ourTextures->find(0);
			if (foundTexture != ourTextures->end()) {
				texturePoints.emplace_back( static_cast<float>((foundTexture->second)->rawPercent().minPoint.x), static_cast<float>((foundTexture->second)->rawPercent().minPoint.y) );
				texturePoints.emplace_back( static_cast<float>((foundTexture->second)->rawPercent().minPoint.x), static_cast<float>((foundTexture->second)->rawPercent().maxPoint.y) );
				texturePoints.emplace_back( static_cast<float>((foundTexture->second)->rawPercent().maxPoint.x), static_cast<float>((foundTexture->second)->rawPercent().maxPoint.y) );
				texturePoints.emplace_back( static_cast<float>((foundTexture->second)->rawPercent().maxPoint.x), static_cast<float>((foundTexture->second)->rawPercent().minPoint.y) );
			} else {
				texturePoints.emplace_back( 0.0f, 0.0f );
				texturePoints.emplace_back( 0.0f, 1.0f );
				texturePoints.emplace_back( 1.0f, 1.0f );
				texturePoints.emplace_back( 1.0f, 0.0f );
			}

			threadData[a_groupIndex].points.reserve(threadData[a_groupIndex].particles.size() * 4);
			threadData[a_groupIndex].vertexIndices.reserve(threadData[a_groupIndex].particles.size() * 6);
			for (auto &&particle : threadData[a_groupIndex].particles) {
				BoxAABB<> bounds(MV::Point<>(particle.scale.x / -2.0f, particle.scale.y / -2.0f, 0.0f), MV::Point<>(particle.scale.x / 2.0f, particle.scale.y / 2.0f, 0.0f));
				bounds.sanitize();

				appendQuadVertexIndices(threadData[a_groupIndex].vertexIndices, static_cast<GLuint>(threadData[a_groupIndex].points.size()));

				auto c = cos(particle.rotation.z);
				auto s = sin(particle.rotation.z);
				for (size_t i = 0; i < 4; ++i) {
					auto corner = bounds[i];
					rotatePoint2DRad(corner.x, corner.y, c, s);
					corner += particle.position;
					threadData[a_groupIndex].points.emplace_back(corner, particle.color, texturePoints[i]);
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
			std::vector<MV::ThreadPool::Job> copyTasks;
			copyTasks.reserve(emitterThreads);
			for (int group = 0; group < emitterThreads; ++group) {
				copyTasks.emplace_back([this, group, pointOffset, vertexOffset]() {
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
			pool.tasks(copyTasks, [this]() {
				loadPointsFromBufferAndAllowUpdate();
			});
		}

		void Emitter::loadPointsFromBufferAndAllowUpdate() {
			{
				std::lock_guard<std::recursive_mutex> guard(lock);
				points->clear();
				vertexIndices->clear();
				std::swap(*points, pointBuffer);
				std::swap(*vertexIndices, vertexIndexBuffer);
				dirtyVertexBuffer = true;
			}
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
			relativeNodePosition->reset();
			if (a_count > 0) {
				relativeNodePosition = owner()->parent();
				--a_count;
				while (a_count > 0 && relativeNodePosition->lock()->parent()) {
					relativeNodePosition = relativeNodePosition->lock()->parent();
					--a_count;
				}
			}
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		std::shared_ptr<Emitter> Emitter::properties(const EmitterSpawnProperties & a_emitterProperties) {
			spawnProperties = a_emitterProperties;
			nextSpawnDelta = randomNumber(spawnProperties->minimumSpawnRate, spawnProperties->maximumSpawnRate);
			spawnProperties->dirty = true;
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		const EmitterSpawnProperties & Emitter::properties() const {
			return *spawnProperties;
		}

		EmitterSpawnProperties & Emitter::properties() {
			nextSpawnDelta = 0.0f; //zero this out since we return a reference to the properties and may need to re-evaluate our nextSpawnDelta.
			spawnProperties->dirty = true;
			return *spawnProperties;
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
			if (updateInProgress.compare_exchange_strong(falseValue, true)) {
				if (relativeNodePosition->expired() && relativeParentCount >= 0) {
					makeRelativeToParent(relativeParentCount);
				}
				spawnProperties->initializeCallbacks();

				MV::Point<> particleOffset;
				if (auto lockedRelativeNodePosition = relativeNodePosition->lock()) {
					particleOffset = owner()->localFromWorld(lockedRelativeNodePosition->worldPosition());
				}
				for (auto&& data : threadData) {
					data.particleOffset = particleOffset;
				}
				pool.task([this, a_dt]() {
					double dt = std::min(accumulatedTimeDelta, MAX_TIME_STEP);
					accumulatedTimeDelta = 0.0;

					if (nextSpawnDelta == 0.0) {
						nextSpawnDelta = randomNumber(spawnProperties->minimumSpawnRate, spawnProperties->maximumSpawnRate);
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
			spawnParticles = false;
			while (updateInProgress.load()) {
				std::this_thread::sleep_for(std::chrono::microseconds(100));
			}
		}

		Emitter::Emitter(const std::weak_ptr<Node> &a_owner, ThreadPool &a_pool) :
			jai::property_owner<Emitter, Drawable>(a_owner),
			pool(a_pool),
			emitterThreads(a_pool.threads()),
			threadData(emitterThreads),
			updateInProgress(false){

			points->resize(4);
			clearTexturePoints(*points);
			appendQuadVertexIndices(*vertexIndices, 0);
		}

		BoxAABB<> Emitter::boundsImplementation() {
			return{ spawnProperties->minimumPosition, spawnProperties->maximumPosition };
		}

		void Emitter::boundsImplementation(const BoxAABB<> &a_bounds) {
			spawnProperties->minimumPosition = a_bounds.minPoint;
			spawnProperties->maximumPosition = a_bounds.maxPoint;
		}

		std::shared_ptr<Component> Emitter::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Drawable::cloneHelper(a_clone);
			auto emitterClone = std::static_pointer_cast<Emitter>(a_clone);
			emitterClone->emitterThreads = emitterThreads;
			return a_clone;
		}

		MV::Scene::EmitterSpawnProperties loadEmitterProperties(const std::string &a_file) {
			try {
				return MV::fromJson<EmitterSpawnProperties>(MV::fileContents(a_file));
			} catch (::cereal::RapidJSONException &a_exception) {
				std::cerr << "Failed to load emitter: " << a_exception.what() << std::endl;
				return{};
			}
		}

		void Emitter::defaultDrawImplementation() {
			auto ourOwner = owner();
			auto& ourRenderer = ourOwner->renderer();
			if (ourRenderer.headless()) { return; }

			std::lock_guard<std::recursive_mutex> guard(lock); //important!
			if (!vertexIndices->empty()) {
				require<ResourceException>(shaderProgram, "No shader program for Drawable!");
				shaderProgram->use();

				if (bufferId == 0) {
					glGenBuffers(1, &bufferId);
				}

				applyPresetBlendMode(ourRenderer);

				glBindBuffer(GL_ARRAY_BUFFER, bufferId);
				auto structSize = static_cast<GLsizei>(sizeof(points[0]));
				if (dirtyVertexBuffer) {
					dirtyVertexBuffer = false;
					glBufferData(GL_ARRAY_BUFFER, points->size() * structSize, &(points[0]), GL_STATIC_DRAW);
				}

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);

				auto positionOffset = static_cast<size_t>(offsetof(DrawPoint, x));
				auto textureOffset = static_cast<size_t>(offsetof(DrawPoint, textureX));
				auto colorOffset = static_cast<size_t>(offsetof(DrawPoint, R));
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)positionOffset); //Point
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)textureOffset); //UV
				glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)colorOffset); //Color

				std::set<std::shared_ptr<MV::TextureDefinition>> actuallyRegistered;
				addTexturesToShader();
				auto emitterSpace = relativeNodePosition->expired() ? ourOwner : relativeNodePosition->lock();
				shaderProgram->set("transformation", ourRenderer.cameraProjectionMatrix(ourOwner->cameraId()) * emitterSpace->worldTransform());
				if (userMaterialSettings) {
					try { userMaterialSettings(*shaderProgram); } catch (std::exception &e) { MV::error("Emitter::defaultDrawImplementation. Exception in userMaterialSettings: ", e.what()); }
				}

				glDrawElements(drawType, static_cast<GLsizei>(vertexIndices->size()), GL_UNSIGNED_INT, &vertexIndices[0]);

				glDisableVertexAttribArray(0);
				glDisableVertexAttribArray(1);
				glDisableVertexAttribArray(2);
				glUseProgram(0);
				if (blendModePreset != BLEND_DEFAULT) {
					ourOwner->renderer().defaultBlendFunction();
				}
			}
		}

	}
}
