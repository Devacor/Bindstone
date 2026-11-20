/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

// BatchingDevice: the Device Draw2D hands out. It forwards every call to the backend it wraps, counts the draws the
// backend issues each frame, and merges queued draws that sit next to each other in draw order and share a material
// (the same Material, pipeline and vertex stride, and the same uniforms and textures where the pipeline reads them)
// into one draw over a stream buffer. A run too short to earn that draws each of its draws alone, from the drawable's
// own buffers and uniform set, as an unbatched draw does. Every other call that could change what a queued draw would
// see (a pass, a viewport, the stencil, a texture's contents, a queued draw's own buffer or set, an unbatched draw,
// the frame's end) draws the queue first, so painter's order is exactly the order of the calls.

#ifndef _MV_RENDER_BATCHINGDEVICE_H_
#define _MV_RENDER_BATCHINGDEVICE_H_

#include "MV/Render/device.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace MV {
	namespace Render {

		// A uniform set kept on the CPU, so two draws can tell whether their materials match. Setting a name twice
		// keeps the last value, like a device set.
		class UniformRecord {
		public:
			void setUniform(const std::string& a_name, const float* a_data, uint32_t a_floatCount);
			void setUniformMatrix(const std::string& a_name, const float* a_columnMajor16);
			void setTexture(const std::string& a_name, BoundTexture a_texture, BoundSampler a_sampler);
			void clear();

			// Drops each uniform and texture a_reads(name) says the pipeline never reads: such a value cannot change a
			// pixel, but would keep draws apart.
			template <typename Reads>
			void keepOnly(Reads&& a_reads) {
				values.erase(std::remove_if(values.begin(), values.end(), [&](const Value& a_value) { return !a_reads(a_value.name); }), values.end());
				textures.erase(std::remove_if(textures.begin(), textures.end(), [&](const Texture& a_texture) { return !a_reads(a_texture.name); }), textures.end());
			}
			void writeTo(Device& a_device, BoundUniformSet a_set) const;

			bool operator==(const UniformRecord& a_other) const;
			bool operator!=(const UniformRecord& a_other) const { return !(*this == a_other); }

			// The matrix set under a_name; null when none is.
			const float* matrixNamed(const std::string& a_name) const;

		private:
			struct Value {
				std::string name;
				uint32_t count = 0;
				bool isMatrix = false;
				float data[16] = {};
			};
			struct Texture {
				std::string name;
				BoundTexture texture;
				BoundSampler sampler;
			};
			Value& valueNamed(const std::string& a_name);

			std::vector<Value> values;
			std::vector<Texture> textures;
		};

		struct DrawStatistics {
			uint32_t draws = 0;          // draws the backend issued
			uint32_t queuedDraws = 0;    // draws queued for merging
			uint32_t batches = 0;        // merged draws the queue issued from its stream buffer
			uint32_t loneDraws = 0;      // queued draws nothing joined, drawn from their own buffers
		};

		class BatchingDevice : public Device {
		public:
			explicit BatchingDevice(Device& a_backend);
			~BatchingDevice() override;

			// The uniform a batched pipeline multiplies each position by.
			static const std::string TRANSFORMATION;

			// What a queued draw needs to draw as it would unbatched: its own buffers and uniform set.
			struct OwnDraw {
				BoundBuffer vertices;
				BoundBuffer indices;
				uint32_t indexCount = 0;
				BoundUniformSet uniforms;
				bool* drewAlone = nullptr;   // set at the flush: whether nothing joined the draw
			};

			// Queues a draw of a_vertexCount vertices of a_vertexStride bytes, each starting with its position as three
			// floats in the draw's own space, and a_indexCount indices counting from its first vertex, for a_pipeline with
			// a_uniforms, whose TRANSFORMATION takes those positions to clip space. a_material is the identity of the
			// Material the draw uses; the draw joins the one queued before it when their material, pipeline, stride and
			// uniforms (TRANSFORMATION aside) match; a_uniforms is consumed. A run nothing joined draws alone from a_own,
			// which must stay as it is until then; a longer run moves each draw's vertices into clip space by its own
			// TRANSFORMATION and draws them together from a stream buffer with an identity one. A draw whose
			// TRANSFORMATION is missing or leaves w other than 1 never joins a run.
			void queue(const void* a_material, BoundPipeline a_pipeline, UniformRecord& a_uniforms, const OwnDraw& a_own,
				const void* a_vertices, uint32_t a_vertexStride, uint32_t a_vertexCount, const uint32_t* a_indices, uint32_t a_indexCount);
			// Issues the queued draws.
			void flush();

			// A scratch record for one queued draw; records open inside another's recording get their own.
			UniformRecord& openRecord();
			void closeRecord();

			// Whether a drawable whose last queued draw drew alone queues this frame: on one frame in PROBE_INTERVAL, the
			// same frame for every drawable so neighbours meet in the queue, it does and finds out whether something
			// joins it now; on the others it draws straight from its own buffers and spares the queue's bookkeeping.
			static constexpr uint64_t PROBE_INTERVAL = 8;
			// A shorter run draws each of its draws alone: merging costs a vertex transform and a stream upload per
			// draw, which a run this short does not earn back in draw calls saved.
			static constexpr size_t MINIMUM_MERGED_RUN = 3;
			bool probing() const { return frameIndex % PROBE_INTERVAL == 0; }

			// Off: drawables draw one at a time, each with its own buffers and uniform set.
			bool batching() const { return batchingEnabled; }
			void batching(bool a_enabled);

			const DrawStatistics& frame() const { return currentFrame; }
			const DrawStatistics& lastFrame() const { return previousFrame; }

			// Frees the stream buffers; the renderer calls it before its backend shuts down.
			void releaseStreams();

			Device& backend() { return wrapped; }

			bool initialize(const SwapchainDesc& a_desc) override { return wrapped.initialize(a_desc); }
			void shutdown() override;
			const DeviceCaps& caps() const override { return wrapped.caps(); }

			BoundBuffer createBuffer(BufferUsage a_usage, BufferUpdateHint a_hint, const void* a_data, size_t a_bytes) override { return wrapped.createBuffer(a_usage, a_hint, a_data, a_bytes); }
			void        updateBuffer(BoundBuffer a_buffer, const void* a_data, size_t a_bytes, size_t a_offset) override;
			void        destroyBuffer(BoundBuffer a_buffer) override;
			void        appendStreamBuffer(BoundBuffer a_buffer, const void* a_data, size_t a_bytes, size_t a_offset, bool a_startsOver) override { wrapped.appendStreamBuffer(a_buffer, a_data, a_bytes, a_offset, a_startsOver); }

			BoundTexture createTexture(const TextureDesc& a_desc, const void* a_initialPixels) override { return wrapped.createTexture(a_desc, a_initialPixels); }
			void         updateTexture(BoundTexture a_texture, int a_mip, const Rect& a_rect, const void* a_pixels) override;
			void         generateMips(BoundTexture a_texture) override;
			void         destroyTexture(BoundTexture a_texture) override;
			void         readTexture(BoundTexture a_texture, std::vector<uint8_t>& a_out) override;
			uint32_t     nativeTextureId(BoundTexture a_texture) const override { return wrapped.nativeTextureId(a_texture); }

			BoundSampler      createSampler(const SamplerDesc& a_desc) override { return wrapped.createSampler(a_desc); }
			BoundShaderModule createShaderModule(const ShaderModuleDesc& a_desc) override { return wrapped.createShaderModule(a_desc); }
			BoundPipeline     createPipeline(const PipelineDesc& a_desc) override { return wrapped.createPipeline(a_desc); }
			void              destroyPipeline(BoundPipeline a_pipeline) override;
			bool              readsUniform(BoundPipeline a_pipeline, const std::string& a_name) override;
			BoundRenderTarget createRenderTarget(const RenderTargetDesc& a_desc) override { return wrapped.createRenderTarget(a_desc); }
			void              destroyRenderTarget(BoundRenderTarget a_target) override;

			BoundUniformSet createUniformSet(BoundPipeline a_pipeline) override { return wrapped.createUniformSet(a_pipeline); }
			void destroyUniformSet(BoundUniformSet a_set) override;
			void setUniform(BoundUniformSet a_set, const std::string& a_name, const float* a_data, uint32_t a_floatCount) override;
			void setUniformMatrix(BoundUniformSet a_set, const std::string& a_name, const float* a_columnMajor16) override;
			void setTexture(BoundUniformSet a_set, const std::string& a_name, BoundTexture a_texture, BoundSampler a_sampler) override;

			void beginFrame() override;
			void beginPass(BoundRenderTarget a_target, const RenderTargetDesc& a_clearInfo) override;
			void beginDefaultPass(const float a_clearColor[4]) override;
			void setViewport(const Viewport& a_viewport) override;
			void setScissor(const Rect& a_rect) override;
			void setStencilState(const StencilState& a_state) override;
			void clearStencil(uint8_t a_value) override;
			void draw(const DrawItem& a_item) override;
			void endPass() override;
			void endFrame() override;
			void onSurfaceResized(int a_drawableWidthPixels, int a_drawableHeightPixels) override;

		private:
			struct Stream {
				BoundBuffer buffer;
				size_t bytes = 0;
				size_t cursor = 0;
			};
			// Writes a_bytes to a_stream after what this frame wrote, aligned to a_alignment, growing the buffer when the
			// frame has outgrown it; returns the offset.
			size_t append(Stream& a_stream, BufferUsage a_usage, const void* a_data, size_t a_bytes, size_t a_alignment);

			struct QueuedDraw {
				float clipFromLocal[16] = {};
				uint32_t firstVertex = 0;
				uint32_t vertexCount = 0;
				bool hasTransformation = false;
				OwnDraw own;
			};
			// Draws the queue first when a queued draw's own buffer or uniform set is about to change.
			void flushIfQueued(BoundBuffer a_buffer);
			void flushIfQueued(BoundUniformSet a_set);
			void drawAlone(const QueuedDraw& a_draw);
			void drawMerged();
			void clearPending();

			Device& wrapped;
			bool batchingEnabled = true;

			const void* pendingMaterial = nullptr;
			BoundPipeline pendingPipeline;
			UniformRecord pendingUniforms;
			uint32_t pendingStride = 0;
			uint32_t pendingVertexCount = 0;
			bool pendingTakesNoMore = false;
			std::vector<QueuedDraw> pendingDraws;
			std::vector<uint8_t> pendingVertices;
			std::vector<uint32_t> pendingIndices;

			Stream vertexStream;
			Stream indexStream;

			// Per pipeline (index and generation), the names asked about and whether it reads them.
			using NamesRead = std::vector<std::pair<std::string, bool>>;
			std::unordered_map<uint64_t, NamesRead> pipelineReads;
			bool readsUniform(NamesRead& a_names, BoundPipeline a_pipeline, const std::string& a_name);

			std::vector<std::unique_ptr<UniformRecord>> records;
			size_t openRecords = 0;

			DrawStatistics currentFrame;
			DrawStatistics previousFrame;
			uint64_t frameIndex = 0;
		};

	}
}

#endif
