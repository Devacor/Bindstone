#include "MV/Render/batchingDevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace MV {
	namespace Render {

		UniformRecord::Value& UniformRecord::valueNamed(const std::string& a_name) {
			for (auto&& value : values) {
				if (value.name == a_name) {
					return value;
				}
			}
			auto& result = values.emplace_back();
			result.name = a_name;
			return result;
		}

		void UniformRecord::setUniform(const std::string& a_name, const float* a_data, uint32_t a_floatCount) {
			auto& value = valueNamed(a_name);
			value.count = std::min<uint32_t>(a_floatCount, 16);
			value.isMatrix = false;
			std::memcpy(value.data, a_data, value.count * sizeof(float));
		}

		void UniformRecord::setUniformMatrix(const std::string& a_name, const float* a_columnMajor16) {
			auto& value = valueNamed(a_name);
			value.count = 16;
			value.isMatrix = true;
			std::memcpy(value.data, a_columnMajor16, 16 * sizeof(float));
		}

		void UniformRecord::setTexture(const std::string& a_name, BoundTexture a_texture, BoundSampler a_sampler) {
			for (auto&& texture : textures) {
				if (texture.name == a_name) {
					texture.texture = a_texture;
					texture.sampler = a_sampler;
					return;
				}
			}
			textures.push_back({ a_name, a_texture, a_sampler });
		}

		const float* UniformRecord::matrixNamed(const std::string& a_name) const {
			for (auto&& value : values) {
				if (value.isMatrix && value.name == a_name) {
					return value.data;
				}
			}
			return nullptr;
		}

		void UniformRecord::clear() {
			values.clear();
			textures.clear();
		}

		void UniformRecord::writeTo(Device& a_device, BoundUniformSet a_set) const {
			for (auto&& value : values) {
				if (value.isMatrix) {
					a_device.setUniformMatrix(a_set, value.name, value.data);
				} else {
					a_device.setUniform(a_set, value.name, value.data, value.count);
				}
			}
			for (auto&& texture : textures) {
				a_device.setTexture(a_set, texture.name, texture.texture, texture.sampler);
			}
		}

		bool UniformRecord::operator==(const UniformRecord& a_other) const {
			if (values.size() != a_other.values.size() || textures.size() != a_other.textures.size()) {
				return false;
			}
			for (size_t i = 0; i < values.size(); ++i) {
				const Value& left = values[i];
				const Value& right = a_other.values[i];
				// bitwise: a draw merges only with one whose shader would see the very same bits
				if (left.count != right.count || left.isMatrix != right.isMatrix || left.name != right.name || std::memcmp(left.data, right.data, left.count * sizeof(float)) != 0) {
					return false;
				}
			}
			for (size_t i = 0; i < textures.size(); ++i) {
				const Texture& left = textures[i];
				const Texture& right = a_other.textures[i];
				if (left.texture != right.texture || left.sampler != right.sampler || left.name != right.name) {
					return false;
				}
			}
			return true;
		}

		BatchingDevice::BatchingDevice(Device& a_backend) :
			wrapped(a_backend) {
		}

		BatchingDevice::~BatchingDevice() {
		}

		void BatchingDevice::batching(bool a_enabled) {
			flush();
			batchingEnabled = a_enabled;
		}

		UniformRecord& BatchingDevice::openRecord() {
			if (openRecords == records.size()) {
				records.push_back(std::make_unique<UniformRecord>());
			}
			UniformRecord& result = *records[openRecords++];
			result.clear();
			return result;
		}

		void BatchingDevice::closeRecord() {
			if (openRecords > 0) {
				--openRecords;
			}
		}

		const std::string BatchingDevice::TRANSFORMATION = "transformation";

		namespace {
			// A row of a_clipFromLocal times a_position (w = 1) rounded the way GLSL's mat4 * vec4 runs on the GPU, a
			// product then a fused multiply-add per column, so a merged vertex lands where its lone draw's shader puts it.
			float transformedRow(const float* a_clipFromLocal, const float* a_position, size_t a_row) {
				float result = a_clipFromLocal[a_row] * a_position[0];
				result = std::fma(a_clipFromLocal[4 + a_row], a_position[1], result);
				result = std::fma(a_clipFromLocal[8 + a_row], a_position[2], result);
				return std::fma(a_clipFromLocal[12 + a_row], 1.0f, result);
			}

			const float IDENTITY[16] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f };

			// A merged draw's positions are three floats, so only a transform that keeps w at 1 can be baked into them.
			bool keepsWOne(const float* a_clipFromLocal) {
				return a_clipFromLocal[3] == 0.0f && a_clipFromLocal[7] == 0.0f && a_clipFromLocal[11] == 0.0f && a_clipFromLocal[15] == 1.0f;
			}
		}

		void BatchingDevice::queue(const void* a_material, BoundPipeline a_pipeline, UniformRecord& a_uniforms, const OwnDraw& a_own,
				const void* a_vertices, uint32_t a_vertexStride, uint32_t a_vertexCount, const uint32_t* a_indices, uint32_t a_indexCount) {
			auto& names = pipelineReads[(static_cast<uint64_t>(a_pipeline.index) << 32) | a_pipeline.generation];
			a_uniforms.keepOnly([&](const std::string& a_name) { return readsUniform(names, a_pipeline, a_name); });
			float clipFromLocal[16];
			const float* recorded = a_uniforms.matrixNamed(TRANSFORMATION);
			const bool hasTransformation = recorded != nullptr;
			const bool mergeable = hasTransformation && keepsWOne(recorded);
			std::memcpy(clipFromLocal, hasTransformation ? recorded : IDENTITY, sizeof(clipFromLocal));
			if (hasTransformation) {
				a_uniforms.setUniformMatrix(TRANSFORMATION, IDENTITY);
			}
			const bool joins = mergeable && !pendingTakesNoMore && !pendingDraws.empty() && pendingMaterial == a_material && pendingPipeline == a_pipeline
				&& pendingStride == a_vertexStride && pendingUniforms == a_uniforms;
			if (!joins) {
				flush();
				pendingTakesNoMore = !mergeable;
				pendingMaterial = a_material;
				pendingPipeline = a_pipeline;
				pendingStride = a_vertexStride;
				std::swap(pendingUniforms, a_uniforms);
			}
			++currentFrame.queuedDraws;
			QueuedDraw& queued = pendingDraws.emplace_back();
			std::memcpy(queued.clipFromLocal, clipFromLocal, sizeof(queued.clipFromLocal));
			queued.hasTransformation = hasTransformation;
			queued.firstVertex = pendingVertexCount;
			queued.vertexCount = a_vertexCount;
			queued.own = a_own;

			pendingIndices.reserve(pendingIndices.size() + a_indexCount);
			for (uint32_t i = 0; i < a_indexCount; ++i) {
				pendingIndices.push_back(a_indices[i] + pendingVertexCount);
			}
			pendingVertexCount += a_vertexCount;
			const auto* vertexBytes = static_cast<const uint8_t*>(a_vertices);
			pendingVertices.insert(pendingVertices.end(), vertexBytes, vertexBytes + static_cast<size_t>(a_vertexCount) * a_vertexStride);
		}

		void BatchingDevice::flushIfQueued(BoundBuffer a_buffer) {
			for (auto&& queued : pendingDraws) {
				if (queued.own.vertices == a_buffer || queued.own.indices == a_buffer) {
					flush();
					return;
				}
			}
		}

		void BatchingDevice::flushIfQueued(BoundUniformSet a_set) {
			for (auto&& queued : pendingDraws) {
				if (queued.own.uniforms == a_set) {
					flush();
					return;
				}
			}
		}

		size_t BatchingDevice::append(Stream& a_stream, BufferUsage a_usage, const void* a_data, size_t a_bytes, size_t a_alignment) {
			size_t result = (a_stream.cursor + a_alignment - 1) / a_alignment * a_alignment;
			if (!a_stream.buffer.valid() || result + a_bytes > a_stream.bytes) {
				// Draws already issued this frame keep reading the old buffer; the backend frees it once they finish.
				if (a_stream.buffer.valid()) {
					wrapped.destroyBuffer(a_stream.buffer);
				}
				a_stream.bytes = std::max<size_t>({ a_stream.bytes * 2, a_bytes, 64 * 1024 });
				a_stream.buffer = wrapped.createBuffer(a_usage, BufferUpdateHint::Stream, nullptr, a_stream.bytes);
				result = 0;
			}
			wrapped.appendStreamBuffer(a_stream.buffer, a_data, a_bytes, result, result == 0);
			a_stream.cursor = result + a_bytes;
			return result;
		}

		void BatchingDevice::flush() {
			if (pendingDraws.empty()) {
				return;
			}
			const bool eachHasItsOwn = std::all_of(pendingDraws.begin(), pendingDraws.end(), [](const QueuedDraw& a_draw) { return a_draw.own.uniforms.valid(); });
			if (pendingDraws.size() < MINIMUM_MERGED_RUN && eachHasItsOwn) {
				for (auto&& queued : pendingDraws) {
					drawAlone(queued);
				}
			} else {
				drawMerged();
			}
			clearPending();
		}

		// As the drawable draws unbatched: its own buffers, which stay on the device from frame to frame, and its own set.
		void BatchingDevice::drawAlone(const QueuedDraw& a_draw) {
			pendingUniforms.writeTo(wrapped, a_draw.own.uniforms);
			if (a_draw.hasTransformation) {
				wrapped.setUniformMatrix(a_draw.own.uniforms, TRANSFORMATION, a_draw.clipFromLocal);
			}

			DrawItem item;
			item.pipeline = pendingPipeline;
			item.vertexBuffer = a_draw.own.vertices;
			item.indexBuffer = a_draw.own.indices;
			item.indexType = IndexType::Uint32;
			item.indexCount = a_draw.own.indexCount;
			item.uniforms = a_draw.own.uniforms;
			wrapped.draw(item);
			++currentFrame.draws;
			++currentFrame.loneDraws;
			if (a_draw.own.drewAlone) {
				*a_draw.own.drewAlone = true;
			}
		}

		void BatchingDevice::drawMerged() {
			for (auto&& queued : pendingDraws) {
				if (queued.own.drewAlone) {
					*queued.own.drewAlone = false;
				}
				uint8_t* vertex = pendingVertices.data() + static_cast<size_t>(queued.firstVertex) * pendingStride;
				for (uint32_t i = 0; i < queued.vertexCount; ++i, vertex += pendingStride) {
					float local[3];
					std::memcpy(local, vertex, sizeof(local));
					const float clip[3] = { transformedRow(queued.clipFromLocal, local, 0), transformedRow(queued.clipFromLocal, local, 1), transformedRow(queued.clipFromLocal, local, 2) };
					std::memcpy(vertex, clip, sizeof(clip));
				}
			}
			const size_t vertexBytes = pendingVertices.size();
			const size_t indexBytes = pendingIndices.size() * sizeof(uint32_t);
			const size_t vertexOffset = append(vertexStream, BufferUsage::Vertex, pendingVertices.data(), vertexBytes, pendingStride);
			const size_t indexOffset = append(indexStream, BufferUsage::Index, pendingIndices.data(), indexBytes, sizeof(uint32_t));

			BoundUniformSet uniforms = wrapped.createUniformSet(pendingPipeline);
			pendingUniforms.writeTo(wrapped, uniforms);

			DrawItem item;
			item.pipeline = pendingPipeline;
			item.vertexBuffer = vertexStream.buffer;
			item.indexBuffer = indexStream.buffer;
			item.indexType = IndexType::Uint32;
			item.indexCount = static_cast<uint32_t>(pendingIndices.size());
			item.firstIndex = static_cast<uint32_t>(indexOffset / sizeof(uint32_t));
			item.baseVertex = static_cast<int32_t>(vertexOffset / pendingStride);
			item.uniforms = uniforms;
			wrapped.draw(item);
			wrapped.destroyUniformSet(uniforms);
			++currentFrame.draws;
			++currentFrame.batches;
		}

		void BatchingDevice::clearPending() {
			pendingDraws.clear();
			pendingTakesNoMore = false;
			pendingIndices.clear();
			pendingVertices.clear();
			pendingVertexCount = 0;
		}

		void BatchingDevice::updateBuffer(BoundBuffer a_buffer, const void* a_data, size_t a_bytes, size_t a_offset) {
			flushIfQueued(a_buffer);
			wrapped.updateBuffer(a_buffer, a_data, a_bytes, a_offset);
		}

		void BatchingDevice::destroyBuffer(BoundBuffer a_buffer) {
			flushIfQueued(a_buffer);
			wrapped.destroyBuffer(a_buffer);
		}

		void BatchingDevice::destroyUniformSet(BoundUniformSet a_set) {
			flushIfQueued(a_set);
			wrapped.destroyUniformSet(a_set);
		}

		void BatchingDevice::setUniform(BoundUniformSet a_set, const std::string& a_name, const float* a_data, uint32_t a_floatCount) {
			flushIfQueued(a_set);
			wrapped.setUniform(a_set, a_name, a_data, a_floatCount);
		}

		void BatchingDevice::setUniformMatrix(BoundUniformSet a_set, const std::string& a_name, const float* a_columnMajor16) {
			flushIfQueued(a_set);
			wrapped.setUniformMatrix(a_set, a_name, a_columnMajor16);
		}

		void BatchingDevice::setTexture(BoundUniformSet a_set, const std::string& a_name, BoundTexture a_texture, BoundSampler a_sampler) {
			flushIfQueued(a_set);
			wrapped.setTexture(a_set, a_name, a_texture, a_sampler);
		}

		void BatchingDevice::releaseStreams() {
			clearPending();
			for (Stream* stream : { &vertexStream, &indexStream }) {
				if (stream->buffer.valid()) {
					wrapped.destroyBuffer(stream->buffer);
				}
				*stream = Stream{};
			}
		}

		void BatchingDevice::shutdown() {
			releaseStreams();
			wrapped.shutdown();
		}

		void BatchingDevice::updateTexture(BoundTexture a_texture, int a_mip, const Rect& a_rect, const void* a_pixels) {
			flush();
			wrapped.updateTexture(a_texture, a_mip, a_rect, a_pixels);
		}

		void BatchingDevice::generateMips(BoundTexture a_texture) {
			flush();
			wrapped.generateMips(a_texture);
		}

		void BatchingDevice::destroyTexture(BoundTexture a_texture) {
			flush();
			wrapped.destroyTexture(a_texture);
		}

		void BatchingDevice::readTexture(BoundTexture a_texture, std::vector<uint8_t>& a_out) {
			flush();
			wrapped.readTexture(a_texture, a_out);
		}

		bool BatchingDevice::readsUniform(BoundPipeline a_pipeline, const std::string& a_name) {
			return readsUniform(pipelineReads[(static_cast<uint64_t>(a_pipeline.index) << 32) | a_pipeline.generation], a_pipeline, a_name);
		}

		bool BatchingDevice::readsUniform(NamesRead& a_names, BoundPipeline a_pipeline, const std::string& a_name) {
			for (auto&& [name, reads] : a_names) {
				if (name == a_name) {
					return reads;
				}
			}
			const bool result = wrapped.readsUniform(a_pipeline, a_name);
			a_names.emplace_back(a_name, result);
			return result;
		}

		void BatchingDevice::destroyPipeline(BoundPipeline a_pipeline) {
			flush();
			pipelineReads.erase((static_cast<uint64_t>(a_pipeline.index) << 32) | a_pipeline.generation);
			wrapped.destroyPipeline(a_pipeline);
		}

		void BatchingDevice::destroyRenderTarget(BoundRenderTarget a_target) {
			flush();
			wrapped.destroyRenderTarget(a_target);
		}

		void BatchingDevice::beginFrame() {
			flush();
			wrapped.beginFrame();
		}

		void BatchingDevice::beginPass(BoundRenderTarget a_target, const RenderTargetDesc& a_clearInfo) {
			flush();
			wrapped.beginPass(a_target, a_clearInfo);
		}

		void BatchingDevice::beginDefaultPass(const float a_clearColor[4]) {
			flush();
			wrapped.beginDefaultPass(a_clearColor);
		}

		void BatchingDevice::setViewport(const Viewport& a_viewport) {
			flush();
			wrapped.setViewport(a_viewport);
		}

		void BatchingDevice::setScissor(const Rect& a_rect) {
			flush();
			wrapped.setScissor(a_rect);
		}

		void BatchingDevice::setStencilState(const StencilState& a_state) {
			flush();
			wrapped.setStencilState(a_state);
		}

		void BatchingDevice::clearStencil(uint8_t a_value) {
			flush();
			wrapped.clearStencil(a_value);
		}

		void BatchingDevice::draw(const DrawItem& a_item) {
			flush();
			wrapped.draw(a_item);
			++currentFrame.draws;
		}

		void BatchingDevice::endPass() {
			flush();
			wrapped.endPass();
		}

		void BatchingDevice::endFrame() {
			flush();
			wrapped.endFrame();
			previousFrame = currentFrame;
			currentFrame = DrawStatistics{};
			++frameIndex;
			vertexStream.cursor = 0;
			indexStream.cursor = 0;
		}

		void BatchingDevice::onSurfaceResized(int a_drawableWidthPixels, int a_drawableHeightPixels) {
			flush();
			wrapped.onSurfaceResized(a_drawableWidthPixels, a_drawableHeightPixels);
		}

	}
}
