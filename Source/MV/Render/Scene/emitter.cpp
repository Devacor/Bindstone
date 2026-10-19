#include "emitter.h"

#include "MV/Serialization/serialize.h"
#include "MV/Utility/generalUtility.h"
#include <jaiscript/serialization/archive.hpp>

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>

static jai::registrar<MV::Scene::Emitter, MV::Services> _hookEmitter("Emitter",
	[](jai::dynamic_binder<MV::Scene::Emitter>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Drawable>();

	builder.method("relativeEmission", static_cast<std::weak_ptr<MV::Scene::Node>(MV::Scene::Emitter::*)() const>(&MV::Scene::Emitter::relativeEmission));
	builder.method("relativeEmission", static_cast<std::shared_ptr<MV::Scene::Emitter>(MV::Scene::Emitter::*)(std::shared_ptr<MV::Scene::Node>)>(&MV::Scene::Emitter::relativeEmission));
	builder.method("removeRelativeEmission", &MV::Scene::Emitter::removeRelativeEmission);
	builder.method("makeRelativeToParent", &MV::Scene::Emitter::makeRelativeToParent);

	builder.method("properties", static_cast<const MV::Scene::EmitterSpawnProperties&(MV::Scene::Emitter::*)() const>(&MV::Scene::Emitter::properties));
	builder.method("properties", static_cast<std::shared_ptr<MV::Scene::Emitter>(MV::Scene::Emitter::*)(const MV::Scene::EmitterSpawnProperties&)>(&MV::Scene::Emitter::properties));

	builder.method("enabled", &MV::Scene::Emitter::enabled);
	builder.method("disabled", &MV::Scene::Emitter::disabled);
	builder.method("enable", &MV::Scene::Emitter::enable);
	builder.method("disable", &MV::Scene::Emitter::disable);
});

namespace MV {
	namespace Scene {

		const double Emitter::MAX_TIME_STEP  = .25;

		namespace {
			constexpr size_t MAX_SPAWNS_PER_SLOT_PER_FRAME = 500;

			//xorshift64*: one per pipeline slot, eight bytes where an mt19937 per slot per emitter would be five kilobytes.
			class ParticleRandom {
			public:
				explicit ParticleRandom(uint64_t a_seed) :
					state(a_seed != 0 ? a_seed : 0x9E3779B97F4A7C15ull) {
				}

				float unit() {
					state ^= state >> 12;
					state ^= state << 25;
					state ^= state >> 27;
					return static_cast<float>((state * 0x2545F4914F6CDD1Dull) >> 40) / 16777215.0f;
				}

			private:
				uint64_t state;
			};

			Point<> randomMix(const Point<> &a_from, const Point<> &a_to, ParticleRandom &a_random) {
				return{
					mix(a_from.x, a_to.x, a_random.unit()),
					mix(a_from.y, a_to.y, a_random.unit()),
					mix(a_from.z, a_to.z, a_random.unit())
				};
			}

			Color randomMix(const Color &a_from, const Color &a_to, ParticleRandom &a_random) {
				return{
					mix(a_from.R, a_to.R, a_random.unit()),
					mix(a_from.G, a_to.G, a_random.unit()),
					mix(a_from.B, a_to.B, a_random.unit()),
					mix(a_from.A, a_to.A, a_random.unit())
				};
			}
		}

		struct EmitterSnapshot {
			Point<> minimumPosition;
			Point<> maximumPosition;
			AxisAngles minimumDirection;
			AxisAngles maximumDirection;
			AxisAngles minimumRotation;
			AxisAngles maximumRotation;
			ParticleChangeValues minimum;
			ParticleChangeValues maximum;
			size_t textureCount = 0;
			std::array<TexturePoint, 4> textureCorners;
			double dt = 0.0;
			Point<> particleOffset;
			std::vector<size_t> spawns;
			std::function<void(size_t)> probe;

			Particle spawn(ParticleRandom &a_random) const {
				Particle result;
				result.textureCount = textureCount;
				result.position = randomMix(minimumPosition, maximumPosition, a_random) - particleOffset;
				result.rotation = randomMix(minimumRotation, maximumRotation, a_random);
				result.change.rotationalChange = randomMix(minimum.rotationalChange, maximum.rotationalChange, a_random);
				result.direction = randomMix(minimumDirection, maximumDirection, a_random);
				result.change.rateOfChange = randomMix(minimum.rateOfChange, maximum.rateOfChange, a_random);
				result.change.directionalChangeRad(randomMix(minimum.directionalChangeRad(), maximum.directionalChangeRad(), a_random));
				result.change.beginSpeed = mix(minimum.beginSpeed, maximum.beginSpeed, a_random.unit());
				result.change.endSpeed = mix(minimum.endSpeed, maximum.endSpeed, a_random.unit());
				result.change.beginScale = mix(minimum.beginScale, maximum.beginScale, a_random.unit());
				result.change.endScale = mix(minimum.endScale, maximum.endScale, a_random.unit());
				result.change.beginColor = randomMix(minimum.beginColor, maximum.beginColor, a_random);
				result.change.endColor = randomMix(minimum.endColor, maximum.endColor, a_random);
				result.change.animationFramesPerSecond = mix(minimum.animationFramesPerSecond, maximum.animationFramesPerSecond, a_random.unit());
				result.change.maxLifespan = mix(minimum.maxLifespan, maximum.maxLifespan, a_random.unit());
				result.setGravity(
					mix(minimum.gravityMagnitude, maximum.gravityMagnitude, a_random.unit()),
					randomMix(minimum.gravityDirection, maximum.gravityDirection, a_random)
				);
				return result;
			}
		};

		struct EmitterPipeline {
			struct Slot {
				explicit Slot(uint64_t a_seed) :
					random(a_seed) {
				}

				std::vector<Particle> particles;
				std::vector<DrawPoint> points;
				std::vector<GLuint> vertexIndices;
				ParticleRandom random;
			};

			explicit EmitterPipeline(size_t a_slots) {
				slots.reserve(a_slots);
				for (size_t i = 0; i < a_slots; ++i) {
					slots.emplace_back(static_cast<uint64_t>(randomInteger(0, std::numeric_limits<int64_t>::max())));
				}
			}

			size_t particleCount() const {
				size_t result = 0;
				for (auto&& slot : slots) {
					result += slot.particles.size();
				}
				return result;
			}

			void simulate(const EmitterSnapshot &a_snapshot, size_t a_slot) {
				if (a_snapshot.probe) { a_snapshot.probe(a_slot); }
				auto& slot = slots[a_slot];
				slot.particles.reserve(slot.particles.size() + a_snapshot.spawns[a_slot]);
				for (size_t count = 0; count < a_snapshot.spawns[a_slot]; ++count) {
					slot.particles.push_back(a_snapshot.spawn(slot.random));
				}
				slot.particles.erase(std::remove_if(slot.particles.begin(), slot.particles.end(), [&](Particle& a_particle) {
					return a_particle.update(a_snapshot.dt);
				}), slot.particles.end());

				slot.points.clear();
				slot.vertexIndices.clear();
				slot.points.reserve(slot.particles.size() * 4);
				slot.vertexIndices.reserve(slot.particles.size() * 6);
				for (auto&& particle : slot.particles) {
					BoxAABB<> bounds(MV::Point<>(particle.scale.x / -2.0f, particle.scale.y / -2.0f, 0.0f), MV::Point<>(particle.scale.x / 2.0f, particle.scale.y / 2.0f, 0.0f));
					bounds.sanitize();

					appendQuadVertexIndices(slot.vertexIndices, static_cast<GLuint>(slot.points.size()));

					auto c = cos(particle.rotation.z);
					auto s = sin(particle.rotation.z);
					for (size_t i = 0; i < 4; ++i) {
						auto corner = bounds[i];
						rotatePoint2DRad(corner.x, corner.y, c, s);
						corner += particle.position;
						slot.points.emplace_back(corner, particle.color, a_snapshot.textureCorners[i]);
					}
				}
			}

			void copySlot(size_t a_slot, size_t a_pointOffset, size_t a_indexOffset) {
				auto& slot = slots[a_slot];
				std::copy(slot.points.begin(), slot.points.end(), points.begin() + a_pointOffset);
				std::transform(slot.vertexIndices.begin(), slot.vertexIndices.end(), vertexIndices.begin() + a_indexOffset, [&](GLuint a_index) {
					return a_index + static_cast<GLuint>(a_pointOffset);
				});
			}

			std::vector<Slot> slots;
			std::vector<DrawPoint> points;
			std::vector<GLuint> vertexIndices;
			double nextSpawnDelta = 0.0;
			double timeSinceLastParticle = 0.0;
			std::atomic<bool> failed{false};
			bool finished = false;
		};

		namespace {
			template<typename Stage>
			void runReportingFailure(EmitterPipeline &a_pipeline, Stage &&a_stage) {
				try {
					a_stage();
				} catch (...) {
					a_pipeline.failed = true;
					throw;
				}
			}

			void finishPipeline(const std::shared_ptr<EmitterPipeline> &a_pipeline, jai::thread_pool &a_pool) {
				a_pool.post([a_pipeline]() { a_pipeline->finished = true; });
			}

			void copySlotsIntoOnePointList(const std::shared_ptr<EmitterPipeline> &a_pipeline, jai::thread_pool &a_pool) {
				if (a_pipeline->failed) {
					finishPipeline(a_pipeline, a_pool);
					return;
				}
				try {
					size_t pointCount = 0;
					size_t indexCount = 0;
					for (auto&& slot : a_pipeline->slots) {
						pointCount += slot.points.size();
						indexCount += slot.vertexIndices.size();
					}
					a_pipeline->points.resize(pointCount);
					a_pipeline->vertexIndices.resize(indexCount);

					std::vector<jai::thread_pool::task_type> copies;
					copies.reserve(a_pipeline->slots.size());
					size_t pointOffset = 0;
					size_t indexOffset = 0;
					for (size_t slot = 0; slot < a_pipeline->slots.size(); ++slot) {
						copies.emplace_back([a_pipeline, slot, pointOffset, indexOffset]() {
							runReportingFailure(*a_pipeline, [&]() { a_pipeline->copySlot(slot, pointOffset, indexOffset); });
						});
						pointOffset += a_pipeline->slots[slot].points.size();
						indexOffset += a_pipeline->slots[slot].vertexIndices.size();
					}
					a_pool.submit_batch(std::move(copies), [a_pipeline]() { a_pipeline->finished = true; }, jai::thread_pool::then_on::posted);
				} catch (...) {
					a_pipeline->failed = true;
					finishPipeline(a_pipeline, a_pool);
					throw;
				}
			}
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
			rerollSpawnDelta = true;
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		const EmitterSpawnProperties & Emitter::properties() const {
			return *spawnProperties;
		}

		EmitterSpawnProperties & Emitter::properties() {
			rerollSpawnDelta = true;
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

		std::shared_ptr<Emitter> Emitter::simulateWhileHeadless(bool a_simulate) {
			simulatesWhileHeadless = a_simulate;
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		std::shared_ptr<Emitter> Emitter::pipelineProbe(std::function<void(size_t)> a_probe) {
			ourPipelineProbe = std::move(a_probe);
			return std::static_pointer_cast<Emitter>(shared_from_this());
		}

		void Emitter::updateImplementation(double a_dt) {
			if (owner()->renderer().headless() && !simulatesWhileHeadless) { return; }

			accumulatedTimeDelta += a_dt;
			if (pipelineRunning) {
				if (!pipeline->finished) { return; }
				takePipelineResult();
			}
			startPipeline();
		}

		void Emitter::takePipelineResult() {
			pipelineRunning = false;
			if (pipeline->failed) { return; }
			points->swap(pipeline->points);
			vertexIndices->swap(pipeline->vertexIndices);
			dirtyVertexBuffer = true;
			dirtyIndexBuffer = true;
		}

		void Emitter::startPipeline() {
			auto snapshot = std::make_shared<EmitterSnapshot>();
			snapshot->dt = std::min(accumulatedTimeDelta, MAX_TIME_STEP);
			accumulatedTimeDelta = 0.0;

			auto& state = *pipeline;
			const size_t slots = state.slots.size();
			snapshot->spawns.assign(slots, 0);
			if (rerollSpawnDelta || state.nextSpawnDelta == 0.0) {
				state.nextSpawnDelta = randomNumber(spawnProperties->minimumSpawnRate, spawnProperties->maximumSpawnRate);
				rerollSpawnDelta = false;
			}
			const size_t alive = state.particleCount();
			size_t spawning = 0;
			if (spawnParticles) {
				state.timeSinceLastParticle += snapshot->dt;
				const size_t maximum = spawnProperties->maximumParticles;
				const size_t room = alive < maximum ? maximum - alive : 0;
				const double due = state.nextSpawnDelta <= 0.0 ? 0.0 : state.timeSinceLastParticle / state.nextSpawnDelta;
				spawning = due >= static_cast<double>(room) ? room : static_cast<size_t>(due);
				if (spawning > 0) {
					const size_t first = static_cast<size_t>(randomInteger(0, static_cast<int64_t>(slots) - 1));
					for (size_t i = 0; i < slots; ++i) {
						const size_t extra = ((i + slots - first) % slots) < spawning % slots ? 1 : 0;
						snapshot->spawns[i] = std::min(spawning / slots + extra, MAX_SPAWNS_PER_SLOT_PER_FRAME);
					}
					state.timeSinceLastParticle = 0.0;
					state.nextSpawnDelta = randomNumber(spawnProperties->minimumSpawnRate, spawnProperties->maximumSpawnRate);
				}
			}

			if (alive == 0 && spawning == 0) {
				if (!points->empty() || !vertexIndices->empty()) {
					points->clear();
					vertexIndices->clear();
					dirtyVertexBuffer = true;
					dirtyIndexBuffer = true;
				}
				return;
			}

			if (relativeNodePosition->expired() && relativeParentCount >= 0) {
				makeRelativeToParent(relativeParentCount);
			}
			if (auto lockedRelativeNodePosition = relativeNodePosition->lock()) {
				snapshot->particleOffset = owner()->localFromWorld(lockedRelativeNodePosition->worldPosition());
			}

			snapshot->minimumPosition = spawnProperties->minimumPosition;
			snapshot->maximumPosition = spawnProperties->maximumPosition;
			snapshot->minimumDirection = spawnProperties->minimumDirection;
			snapshot->maximumDirection = spawnProperties->maximumDirection;
			snapshot->minimumRotation = spawnProperties->minimumRotation;
			snapshot->maximumRotation = spawnProperties->maximumRotation;
			snapshot->minimum = spawnProperties->minimum;
			snapshot->maximum = spawnProperties->maximum;
			snapshot->textureCount = ourTextures->size();
			auto foundTexture = ourTextures->find(0);
			if (foundTexture != ourTextures->end() && foundTexture->second) {
				const auto& percent = foundTexture->second->rawPercent();
				snapshot->textureCorners = {
					TexturePoint(static_cast<float>(percent.minPoint.x), static_cast<float>(percent.minPoint.y)),
					TexturePoint(static_cast<float>(percent.minPoint.x), static_cast<float>(percent.maxPoint.y)),
					TexturePoint(static_cast<float>(percent.maxPoint.x), static_cast<float>(percent.maxPoint.y)),
					TexturePoint(static_cast<float>(percent.maxPoint.x), static_cast<float>(percent.minPoint.y))
				};
			} else {
				snapshot->textureCorners = { TexturePoint(0.0f, 0.0f), TexturePoint(0.0f, 1.0f), TexturePoint(1.0f, 1.0f), TexturePoint(1.0f, 0.0f) };
			}
			snapshot->probe = ourPipelineProbe;

			state.failed = false;
			state.finished = false;
			pipelineRunning = true;
			std::vector<jai::thread_pool::task_type> stages;
			stages.reserve(slots);
			for (size_t slot = 0; slot < slots; ++slot) {
				stages.emplace_back([pipeline = pipeline, snapshot, slot]() {
					runReportingFailure(*pipeline, [&]() { pipeline->simulate(*snapshot, slot); });
				});
			}
			pool.submit_batch(std::move(stages), [pipeline = pipeline, pool = &pool]() {
				copySlotsIntoOnePointList(pipeline, *pool);
			});
		}

		Emitter::Emitter(const std::weak_ptr<Node> &a_owner, jai::thread_pool &a_pool) :
			jai::property_owner<Emitter, Drawable>(a_owner),
			pool(a_pool),
			pipeline(std::make_shared<EmitterPipeline>(a_pool.worker_count())) {

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
			emitterClone->simulatesWhileHeadless = simulatesWhileHeadless;
			return a_clone;
		}

		void loadEmitterProperties(EmitterSpawnProperties& a_result, const std::string &a_file, MV::Services& a_services) {
			try {
				auto* engine = a_services.get<jai::engine>();
				auto contents = MV::fileContents(a_file);
				jai::serialization::json_archive_reader ar(contents, engine);
				ar.set_user_context<MV::Services>(&a_services);
				std::string type_name;
				uint32_t version;
				ar.begin_object(type_name, version);
				ar(a_result);
				ar.end_object();
			} catch (std::exception &a_exception) {
				std::cerr << "Failed to load emitter: " << a_exception.what() << std::endl;
			}
		}

		// Emitter records its own uniform pass (no material/alpha): textures + emitter-space transform + user settings.
		void Emitter::recordEmitterUniforms() {
			auto ourOwner = owner();
			addTexturesToShader();
			auto emitterSpace = relativeNodePosition->expired() ? ourOwner : relativeNodePosition->lock();
			shaderProgram->set("transformation", ourOwner->renderer().cameraProjectionMatrix(ourOwner->cameraId()) * emitterSpace->worldTransform());
			if (userMaterialSettings) {
				try { userMaterialSettings(*shaderProgram); } catch (std::exception &e) { MV::error("Emitter::defaultDrawImplementation. Exception in userMaterialSettings: ", e.what()); }
			}
		}

		void Emitter::defaultDrawImplementation() {
			auto ourOwner = owner();
			auto& ourRenderer = ourOwner->renderer();
			if (vertexIndices->empty()) { return; }
			require<ResourceException>(shaderProgram, "No shader program for Drawable!");

			Render::Device* dev = ourRenderer.device();
			syncDeviceBuffers(dev, Render::BufferUpdateHint::Stream); // particles churn every frame.

			if (devicePipeline != cachedPipelineForKey || cachedBlendMode != blendModePreset
			    || cachedTopologyDrawType != drawType || !deviceUniformSet.valid()) {
				devicePipeline = ourRenderer.pipelineFor(shaderProgram, blendModePreset, drawType);
				cachedPipelineForKey = devicePipeline;
				cachedBlendMode = blendModePreset;
				cachedTopologyDrawType = drawType;
				deviceUniformSet = dev->createUniformSet(devicePipeline);
			}

			Render::BoundUniformSet prevRecording = shaderProgram->beginRecording(deviceUniformSet);
			recordEmitterUniforms();
			shaderProgram->endRecording(prevRecording);

			Render::DrawItem item;
			item.pipeline = devicePipeline;
			item.vertexBuffer = deviceVertexBuffer;
			item.indexBuffer = deviceIndexBuffer;
			item.indexType = Render::IndexType::Uint32;
			item.indexCount = static_cast<uint32_t>(vertexIndices->size());
			item.uniforms = deviceUniformSet;
			dev->draw(item);
		}

	}
}
