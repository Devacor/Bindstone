/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

// MV::Render::Device — the RHI seam every backend implements (GLDevice reference,
// VulkanDevice, MetalDevice, HeadlessDevice for tests). Draw2D owns one and delegates
// GPU ops to it. Invariants this header must keep:
//   - No GL/SDL types here; resources are opaque generation+index Bound* handles
//     (distinct from the scene-graph's logical MV::TextureHandle).
//   - No heavy MV includes; matrices cross as const float* (16, column-major).
//   - Imperative GL state (blend/depth/cull/layout/topology) is immutable in Vk/Metal,
//     so it lives in a BoundPipeline; batching is a renderer layer above this. The stencil
//     TEST is the exception: it's dynamic (setStencilState) so a 2D clip mask can span the
//     draws of many components (the scene-graph's Stencil). Color-write-off for a stencil
//     mask is a pipeline property (Metal bakes it), not dynamic.

#ifndef _MV_RENDER_DEVICE_H_
#define _MV_RENDER_DEVICE_H_

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>
#include <string>

namespace MV {
	namespace Render {

		// ---- Opaque, type-safe resource handles ---------------------------------
		// A handle is a {index, generation} pair, NOT a GL name. Trivially copyable;
		// null == {0,0}. The backend maps it internally to VkBuffer / MTLBuffer /
		// GLuint. The generation lets the backend detect use-after-free of a slot.
		template <class Tag>
		struct Handle {
			uint32_t index = 0;
			uint32_t generation = 0;
			bool valid() const { return generation != 0; }
			bool operator==(const Handle& o) const { return index == o.index && generation == o.generation; }
			bool operator!=(const Handle& o) const { return !(*this == o); }
		};

		struct BufferTag; struct TextureTag; struct SamplerTag; struct ShaderModuleTag;
		struct PipelineTag; struct RenderTargetTag; struct UniformSetTag;

		using BoundBuffer       = Handle<BufferTag>;
		using BoundTexture      = Handle<TextureTag>;       // GPU texture resource — distinct from scene's UV-math MV::TextureHandle.
		using BoundSampler      = Handle<SamplerTag>;
		using BoundShaderModule = Handle<ShaderModuleTag>;
		using BoundPipeline     = Handle<PipelineTag>;
		using BoundRenderTarget = Handle<RenderTargetTag>;  // null/default handle == the swapchain target.
		using BoundUniformSet   = Handle<UniformSetTag>;    // a bound group of uniforms+textures (descriptor set / argument buffer).

		// ---- Enums replacing every GLenum/GLuint leak ---------------------------
		enum class PixelFormat : uint8_t {
			RGBA8_UNORM, RGBA8_SRGB, BGRA8_UNORM, BGRA8_SRGB,
			R8_UNORM, RG8_UNORM, RGBA16_FLOAT,
			D16_UNORM, D24_UNORM_S8_UINT, D32_FLOAT, D32_FLOAT_S8_UINT
		};
		enum class TextureDimension : uint8_t { Tex2D, Tex2DArray, TexCube, Tex3D };

		// Bitflags — see operator| / any() below.
		enum class TextureUsage : uint32_t {
			None = 0, Sampled = 1, ColorTarget = 2, DepthStencilTarget = 4, CopySrc = 8, CopyDst = 16
		};
		constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) {
			return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
		}
		constexpr bool any(TextureUsage v, TextureUsage flag) {
			return (static_cast<uint32_t>(v) & static_cast<uint32_t>(flag)) != 0;
		}

		enum class Filter : uint8_t { Nearest, Linear };
		enum class MipFilter : uint8_t { None, Nearest, Linear };
		enum class AddressMode : uint8_t { Repeat, ClampToEdge, MirrorRepeat, ClampToBorder };
		enum class PrimitiveTopology : uint8_t { Triangles, TriangleStrip, Lines, LineStrip, Points };
		enum class IndexType : uint8_t { Uint16, Uint32 };
		enum class BufferUsage : uint8_t { Vertex, Index, Uniform };
		// Static  = upload once (GL_STATIC_DRAW; device-local + staging on Vk/Mtl).
		// Dynamic = occasional churn honoring a dirty flag.
		// Stream  = rewritten every frame (batch buffers, particles) — backend ring-buffers
		//           it across frames-in-flight to avoid reading a buffer still in flight.
		enum class BufferUpdateHint : uint8_t { Static, Dynamic, Stream };
		enum class VertexAttributeFormat : uint8_t { Float1, Float2, Float3, Float4, UByte4_UNorm };
		enum class ShaderStage : uint8_t { Vertex, Fragment, Compute };
		enum class LoadOp : uint8_t { Load, Clear, DontCare };
		enum class StoreOp : uint8_t { Store, DontCare };
		enum class CompareOp : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
		enum class StencilOp : uint8_t { Keep, Zero, Replace, IncrementClamp, DecrementClamp, Invert, IncrementWrap, DecrementWrap };
		enum class CullMode : uint8_t { None, Front, Back };
		enum class BlendFactor : uint8_t {
			Zero, One, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor,
			SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha
		};
		enum class BlendOp : uint8_t { Add, Subtract, ReverseSubtract, Min, Max };

		// NDC / clip-space convention. The SAME scene math must A/B-match across
		// backends; the backend applies the matching correction in setUniformMatrix
		// and reports it in DeviceCaps so ProjectionDetails (picking) stays in sync.
		//   GL:    z in [-1,1], origin bottom-left.
		//   Vulkan z in [0,1], origin top-left  (Y down).
		//   Metal: z in [0,1], origin top-left  (Y up in NDC).
		enum class ClipSpace : uint8_t { GL_NegOneToOne, ZeroToOne_YDown, ZeroToOne_YUp };

		// ---- Descriptors --------------------------------------------------------
		struct VertexAttribute { uint32_t location = 0; VertexAttributeFormat format = VertexAttributeFormat::Float3; uint32_t offset = 0; };
		struct VertexLayout {
			uint32_t stride = 0;
			std::vector<VertexAttribute> attributes;
		};
		// The ONE canonical sprite layout — pos float3 @offsetof(DrawPoint,x),
		// uv float2 @textureX, color float4 @R — is registered once instead of
		// re-deriving offsetof on every draw (drawable.cpp / emitter / spine).
		// A second "mesh" layout (adds normal) is registered for the 3D path.

		struct BlendState {
			bool enabled = true;
			BlendFactor srcColor = BlendFactor::One,           dstColor = BlendFactor::OneMinusSrcAlpha;  BlendOp colorOp = BlendOp::Add;
			BlendFactor srcAlpha = BlendFactor::OneMinusDstAlpha, dstAlpha = BlendFactor::One;             BlendOp alphaOp = BlendOp::Add; // premultiplied-alpha default (matches Draw2D::defaultBlendFunction)
			uint8_t colorWriteMask = 0xF; // RGBA; 0 == stencil-only pass (replaces glColorMask(0,0,0,0)).
		};
		struct StencilFaceState {
			CompareOp compare = CompareOp::Always;
			StencilOp fail = StencilOp::Keep, pass = StencilOp::Keep, depthFail = StencilOp::Keep;
			uint8_t readMask = 0xFF, writeMask = 0xFF;
		};
		struct DepthStencilState {
			bool depthTest = false, depthWrite = false;   // 2D default: both OFF (painter's order). 3D: LessEqual + write ON.
			CompareOp depthCompare = CompareOp::LessEqual;
			bool stencilEnabled = false;
			StencilFaceState front, back;                  // stencil REFERENCE is dynamic (DrawItem.stencilRef), not in the PSO.
		};

		// Dynamic stencil applied to every subsequent draw until changed — decoupled from the
		// pipeline so a 2D clip mask can span draws issued by many components (the scene's Stencil).
		// GL: glStencil* (global); Vulkan: extended dynamic stencil state; Metal: MTLDepthStencilState + ref.
		struct StencilState {
			bool      enabled = false;                     // false == test off (everything passes).
			CompareOp compare = CompareOp::Always;         // compares reference&compareMask vs buffer&compareMask.
			StencilOp pass = StencilOp::Keep, fail = StencilOp::Keep, depthFail = StencilOp::Keep;
			uint8_t   reference = 0;
			uint8_t   compareMask = 0xFF;
			uint8_t   writeMask = 0xFF;                    // 0 == test only, never modify the buffer.
		};
		struct RasterState { CullMode cull = CullMode::None; }; // 2D: None; 3D mesh: Back.

		struct UniformBinding { uint32_t slot = 0; std::string name; ShaderStage stage = ShaderStage::Vertex; }; // name kept for GLSL/reflection compat.
		struct TextureBinding { uint32_t slot = 0; std::string name; };

		struct PipelineDesc {
			BoundShaderModule vertex, fragment;
			VertexLayout      vertexLayout;
			PrimitiveTopology topology = PrimitiveTopology::Triangles;
			BlendState        blend;
			DepthStencilState depthStencil;
			RasterState       raster;
			PixelFormat       colorFormat = PixelFormat::RGBA8_UNORM; // must match the bound render pass's color attachment.
			PixelFormat       depthFormat = PixelFormat::D24_UNORM_S8_UINT;
			uint32_t          sampleCount = 1;
			std::vector<UniformBinding> uniforms;          // reflection-derived or declared; resolves names->slots at build time.
			std::vector<TextureBinding> textures;
		};

		struct ShaderModuleDesc {
			ShaderStage stage = ShaderStage::Vertex;
			// The backend consumes whichever artifact it understands; callers supply
			// all they have. GLDevice compiles glslSource at runtime (today's
			// behavior + hot-reload); Vulkan consumes spirv; Metal uses metallib or
			// SPIRV-Cross(spirv)->MSL.
			std::string          glslSource;
			std::vector<uint8_t> spirv;
			std::vector<uint8_t> metallib;
			std::string          entryPoint = "main";
		};

		struct TextureDesc {
			int width = 0, height = 0, depthOrArrayLayers = 1;
			PixelFormat format = PixelFormat::RGBA8_UNORM;
			TextureDimension dimension = TextureDimension::Tex2D;
			uint32_t mipLevels = 1;                        // >1 enables the 3D mipmap path (GL no-mip 2D default = 1).
			TextureUsage usage = TextureUsage::Sampled;
			bool generateMips = false;
		};
		struct SamplerDesc {
			Filter min = Filter::Linear, mag = Filter::Linear;
			MipFilter mip = MipFilter::None;               // None today (no mips); Linear for 3D trilinear.
			AddressMode u = AddressMode::ClampToEdge, v = AddressMode::ClampToEdge, w = AddressMode::ClampToEdge;
			float maxAnisotropy = 1.0f, lodBias = 0.0f;
			bool operator==(const SamplerDesc& o) const {
				return min==o.min && mag==o.mag && mip==o.mip && u==o.u && v==o.v && w==o.w
					&& maxAnisotropy==o.maxAnisotropy && lodBias==o.lodBias;
			}
		};

		struct ColorAttachment {
			BoundTexture texture;                           // null == swapchain image.
			LoadOp  load  = LoadOp::Clear;
			StoreOp store = StoreOp::Store;
			float   clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		};
		struct DepthAttachment {
			BoundTexture texture;
			LoadOp  load  = LoadOp::Clear;
			StoreOp store = StoreOp::DontCare;
			float   clearDepth = 1.0f;
			uint8_t clearStencil = 0;
			bool    present = false;                        // depth attachment exists (2D ignored it; 3D needs it).
		};
		struct RenderTargetDesc {
			std::vector<ColorAttachment> color;
			DepthAttachment depth;
			int width = 0, height = 0;
		};

		struct Viewport { float x = 0, y = 0, width = 0, height = 0, minDepth = 0.0f, maxDepth = 1.0f; };
		struct Rect { int x = 0, y = 0, width = 0, height = 0; };

		struct SwapchainDesc {
			void* sdlWindow = nullptr;                      // SDL_Window*; the backend selects SDL_WINDOW_OPENGL/_VULKAN/_METAL.
			PixelFormat colorFormat = PixelFormat::BGRA8_UNORM;
			PixelFormat depthFormat = PixelFormat::D24_UNORM_S8_UINT; // reserve stencil bits for Clipped/Stencil.
			bool vsync = false;
			uint32_t imageCount = 2;                        // frames-in-flight; drives ring-buffer + deferred-destroy depth.
		};
		struct DeviceCaps {
			std::string deviceName, apiVersion;
			bool supportsTextureArrays = false, supportsAnisotropy = false, supportsCompute = false;
			ClipSpace clipSpace = ClipSpace::GL_NegOneToOne;
			bool flipViewportY = false;
		};

		// ---- A single recorded draw --------------------------------------------
		// In the batched 2D path the renderer-layer DrawList concatenates many
		// drawables into one shared Stream vertexBuffer+indexBuffer and emits ONE
		// DrawItem per flushed batch, spanning [firstIndex, firstIndex+indexCount)
		// with baseVertex into the shared vertex buffer. Vertices are pre-transformed
		// to world space on the CPU so the whole batch shares `uniforms`
		// (camera*projection). Spine sub-range draws use firstIndex/indexCount too.
		struct DrawItem {
			BoundPipeline   pipeline;                       // shader + blend + depth/stencil + topology + vertexLayout.
			BoundBuffer     vertexBuffer;
			BoundBuffer     indexBuffer;
			IndexType       indexType = IndexType::Uint32;
			uint32_t        indexCount = 0;
			uint32_t        firstIndex = 0;
			int32_t         baseVertex = 0;                 // added to every index; lets a batch share one big vertex buffer.
			BoundUniformSet uniforms;                       // bound uniform/texture group for this draw.
			uint8_t         stencilRef = 0;                 // dynamic stencil reference (Clipped/Stencil nesting depth).
			// --- reserved for the future GPU per-instance-transform path (CPU world-transform today) ---
			uint32_t        instanceCount = 1;
			uint32_t        firstInstance = 0;
			BoundBuffer     instanceBuffer;                 // null today.
		};

		// ============================ THE INTERFACE ============================
		class Device {
		public:
			virtual ~Device() = default;

			// -- lifecycle / device --
			virtual bool initialize(const SwapchainDesc&) = 0;
			virtual void shutdown() = 0;
			virtual const DeviceCaps& caps() const = 0;

			static std::unique_ptr<Device> makeGL();        // reference backend (existing code).
			static std::unique_ptr<Device> makeVulkan();
			static std::unique_ptr<Device> makeMetal();
			static std::unique_ptr<Device> makeHeadless();  // null backend for mv_tests.

			// -- resources (create fuses alloc+upload; destroy is DEFERRED until not in-flight) --
			virtual BoundBuffer createBuffer(BufferUsage, BufferUpdateHint, const void* data, size_t bytes) = 0;
			virtual void        updateBuffer(BoundBuffer, const void* data, size_t bytes, size_t offset = 0) = 0; // honors caller dirty flag.
			virtual void        destroyBuffer(BoundBuffer) = 0;

			virtual BoundTexture createTexture(const TextureDesc&, const void* initialPixels = nullptr) = 0;
			virtual void         updateTexture(BoundTexture, int mip, const Rect&, const void* pixels) = 0;
			virtual void         generateMips(BoundTexture) = 0;
			virtual void         destroyTexture(BoundTexture) = 0;
			virtual void         readTexture(BoundTexture, std::vector<uint8_t>& out) = 0; // saveLoadedTexture; sync on GL, fenced elsewhere.

			// Native GL name for a BoundTexture, for raw-GLuint consumers not yet on BoundTexture
			// (Framebuffer, spine). GL backend returns the GLuint; others return 0.
			virtual uint32_t nativeTextureId(BoundTexture) const { return 0; }

			virtual BoundSampler      createSampler(const SamplerDesc&) = 0;        // backend dedupes.
			virtual BoundShaderModule createShaderModule(const ShaderModuleDesc&) = 0;
			virtual BoundPipeline     createPipeline(const PipelineDesc&) = 0;      // backend caches by content hash.
			virtual void              destroyPipeline(BoundPipeline) = 0;
			virtual BoundRenderTarget createRenderTarget(const RenderTargetDesc&) = 0;
			virtual void              destroyRenderTarget(BoundRenderTarget) = 0;

			// -- uniform/texture binding group --
			// Replaces Shader::set(name,...) + glActiveTexture/glBindTexture/glUniform1i.
			// Names resolve to binding slots via the pipeline's reflection at build time.
			virtual BoundUniformSet createUniformSet(BoundPipeline) = 0;
			virtual void setUniform(BoundUniformSet, const std::string& name, const float* data, uint32_t floatCount) = 0; // 1/2/3/4.
			virtual void setUniformMatrix(BoundUniformSet, const std::string& name, const float* columnMajor16) = 0;       // backend applies ClipSpace correction.
			virtual void setTexture(BoundUniformSet, const std::string& name, BoundTexture, BoundSampler) = 0;

			// -- frame / pass / draw recording --
			// Replaces clearScreen/refreshContext/SwapWindow + Framebuffer start/stop.
			virtual void beginFrame() = 0;                                       // acquire swapchain image; reset cmd buffer.
			virtual void beginPass(BoundRenderTarget, const RenderTargetDesc& clearInfo) = 0; // null target == swapchain pass.
			virtual void beginDefaultPass(const float clearColor[4]) = 0;        // convenience: swapchain pass w/ clear.
			virtual void setViewport(const Viewport&) = 0;
			virtual void setScissor(const Rect&) = 0;
			// Dynamic stencil for 2D clip masking; applies to subsequent draw()s until changed (see StencilState).
			virtual void setStencilState(const StencilState&) = 0;
			virtual void clearStencil(uint8_t value = 0) = 0;    // clear the stencil buffer within the current pass.
			virtual void draw(const DrawItem&) = 0;
			virtual void endPass() = 0;
			virtual void endFrame() = 0;                                         // submit + present.
			virtual void onSurfaceResized(int drawableWidthPixels, int drawableHeightPixels) = 0; // recreate swapchain.
		};

	} // namespace Render
} // namespace MV

#endif
