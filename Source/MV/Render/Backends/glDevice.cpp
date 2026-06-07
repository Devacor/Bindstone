/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#include "MV/Render/Backends/glDevice.h"
#include "MV/Render/Backends/handlePool.h"
#include "MV/Render/render.h" // GL + SDL headers, plus MV::require / ResourceException.

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MV {
	namespace Render {

		// ---- GL object tables (PIMPL payloads; see glDevice.h) ---------------------
		struct GLDevice::State {
			struct GLTexture {
				GLuint name = 0;
				int width = 0, height = 0;
				GLint internalFormat = GL_RGBA8;
				GLenum format = GL_RGBA;
				GLenum type = GL_UNSIGNED_BYTE;
			};
			struct GLBuffer {
				GLuint name = 0;
				GLenum target = GL_ARRAY_BUFFER;
			};
			struct GLSampler {
				GLuint name = 0;
			};
			struct GLShaderModule {
				GLuint shader = 0;
				ShaderStage stage = ShaderStage::Vertex;
			};
			// Linked GL program plus the immutable render state the PSO bakes in.
			struct GLPipeline {
				GLuint program = 0;
				PrimitiveTopology topology = PrimitiveTopology::Triangles;
				BlendState        blend;
				DepthStencilState depthStencil;
				RasterState       raster;
				VertexLayout      vertexLayout;
				std::unordered_map<std::string, GLint> uniformLocations;
				GLint location(const std::string &a_name) {
					auto found = uniformLocations.find(a_name);
					if (found != uniformLocations.end()) { return found->second; }
					GLint loc = glGetUniformLocation(program, a_name.c_str());
					uniformLocations[a_name] = loc; // cache misses (-1) too, so we never re-query.
					return loc;
				}
			};
			// GL has no descriptor-set object; a uniform set is a CPU-side bag draw() replays.
			struct UniformValue { float data[16] = {}; uint32_t count = 0; bool isMatrix = false; };
			struct TextureSlot { BoundTexture texture; BoundSampler sampler; };
			struct GLUniformSet {
				BoundPipeline pipeline;
				std::unordered_map<std::string, UniformValue> uniforms;
				std::unordered_map<std::string, TextureSlot> textures;
			};
			struct GLRenderTarget {
				GLuint fbo = 0;
				int width = 0, height = 0;
				bool ownsDepth = false;
				GLuint depthRenderbuffer = 0;
			};
			HandlePool<BoundTexture,      GLTexture>      textures;
			HandlePool<BoundBuffer,       GLBuffer>       buffers;
			HandlePool<BoundSampler,      GLSampler>      samplers; // no per-object destroy in the RHI; drained at shutdown.
			HandlePool<BoundShaderModule, GLShaderModule> shaderModules;
			HandlePool<BoundPipeline,     GLPipeline>     pipelines;
			HandlePool<BoundUniformSet,   GLUniformSet>   uniformSets;
			HandlePool<BoundRenderTarget, GLRenderTarget> renderTargets;
			std::vector<GLuint> passStack; // bound-FBO stack for nested beginPass/endPass (default target == 0).
			StencilState currentStencil;   // dynamic stencil applied via setStencilState; persists across draws.
		};

		namespace {
			// Maps an RHI PixelFormat to the GL (internalFormat, clientFormat, clientType) triple.
			void glFormatTriple(PixelFormat a_format, GLint &a_internal, GLenum &a_client, GLenum &a_type) {
				switch (a_format) {
				case PixelFormat::RGBA8_UNORM:        a_internal = GL_RGBA8;             a_client = GL_RGBA;            a_type = GL_UNSIGNED_BYTE; break;
				case PixelFormat::RGBA8_SRGB:         a_internal = GL_SRGB8_ALPHA8;      a_client = GL_RGBA;            a_type = GL_UNSIGNED_BYTE; break;
				case PixelFormat::BGRA8_UNORM:        a_internal = GL_RGBA8;             a_client = GL_BGRA;            a_type = GL_UNSIGNED_BYTE; break;
				case PixelFormat::BGRA8_SRGB:         a_internal = GL_SRGB8_ALPHA8;      a_client = GL_BGRA;            a_type = GL_UNSIGNED_BYTE; break;
				case PixelFormat::R8_UNORM:           a_internal = GL_R8;                a_client = GL_RED;            a_type = GL_UNSIGNED_BYTE; break;
				case PixelFormat::RG8_UNORM:          a_internal = GL_RG8;               a_client = GL_RG;             a_type = GL_UNSIGNED_BYTE; break;
				case PixelFormat::RGBA16_FLOAT:       a_internal = GL_RGBA16F;           a_client = GL_RGBA;           a_type = GL_HALF_FLOAT; break;
				case PixelFormat::D16_UNORM:          a_internal = GL_DEPTH_COMPONENT16; a_client = GL_DEPTH_COMPONENT;a_type = GL_UNSIGNED_SHORT; break;
				case PixelFormat::D24_UNORM_S8_UINT:  a_internal = GL_DEPTH24_STENCIL8;  a_client = GL_DEPTH_STENCIL;  a_type = GL_UNSIGNED_INT_24_8; break;
				case PixelFormat::D32_FLOAT:          a_internal = GL_DEPTH_COMPONENT32F;a_client = GL_DEPTH_COMPONENT;a_type = GL_FLOAT; break;
				case PixelFormat::D32_FLOAT_S8_UINT:  a_internal = GL_DEPTH32F_STENCIL8; a_client = GL_DEPTH_STENCIL;  a_type = GL_FLOAT_32_UNSIGNED_INT_24_8_REV; break;
				default:                              a_internal = GL_RGBA8;             a_client = GL_RGBA;           a_type = GL_UNSIGNED_BYTE; break;
				}
			}
			GLenum glWrap(AddressMode a_mode) {
				switch (a_mode) {
				case AddressMode::Repeat:        return GL_REPEAT;
				case AddressMode::MirrorRepeat:  return GL_MIRRORED_REPEAT;
				case AddressMode::ClampToBorder: return GL_CLAMP_TO_BORDER;
				case AddressMode::ClampToEdge:
				default:                         return GL_CLAMP_TO_EDGE;
				}
			}
			GLint glMinFilter(Filter a_min, MipFilter a_mip) {
				const bool linear = (a_min == Filter::Linear);
				switch (a_mip) {
				case MipFilter::None:    return linear ? GL_LINEAR : GL_NEAREST;
				case MipFilter::Nearest: return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
				case MipFilter::Linear:  return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
				default:                 return linear ? GL_LINEAR : GL_NEAREST;
				}
			}
			GLenum glTopology(PrimitiveTopology a_topology) {
				switch (a_topology) {
				case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
				case PrimitiveTopology::Lines:         return GL_LINES;
				case PrimitiveTopology::LineStrip:     return GL_LINE_STRIP;
				case PrimitiveTopology::Points:        return GL_POINTS;
				case PrimitiveTopology::Triangles:
				default:                               return GL_TRIANGLES;
				}
			}
			GLenum glBlendFactor(BlendFactor a_factor) {
				switch (a_factor) {
				case BlendFactor::Zero:             return GL_ZERO;
				case BlendFactor::One:              return GL_ONE;
				case BlendFactor::SrcColor:         return GL_SRC_COLOR;
				case BlendFactor::OneMinusSrcColor: return GL_ONE_MINUS_SRC_COLOR;
				case BlendFactor::DstColor:         return GL_DST_COLOR;
				case BlendFactor::OneMinusDstColor: return GL_ONE_MINUS_DST_COLOR;
				case BlendFactor::SrcAlpha:         return GL_SRC_ALPHA;
				case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
				case BlendFactor::DstAlpha:         return GL_DST_ALPHA;
				case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
				default:                            return GL_ONE;
				}
			}
			GLenum glBlendEquation(BlendOp a_op) {
				switch (a_op) {
				case BlendOp::Subtract:        return GL_FUNC_SUBTRACT;
				case BlendOp::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
				case BlendOp::Min:             return GL_MIN;
				case BlendOp::Max:             return GL_MAX;
				case BlendOp::Add:
				default:                       return GL_FUNC_ADD;
				}
			}
			GLenum glCompare(CompareOp a_op) {
				switch (a_op) {
				case CompareOp::Never:        return GL_NEVER;
				case CompareOp::Less:         return GL_LESS;
				case CompareOp::Equal:        return GL_EQUAL;
				case CompareOp::LessEqual:    return GL_LEQUAL;
				case CompareOp::Greater:      return GL_GREATER;
				case CompareOp::NotEqual:     return GL_NOTEQUAL;
				case CompareOp::GreaterEqual: return GL_GEQUAL;
				case CompareOp::Always:
				default:                      return GL_ALWAYS;
				}
			}
			GLenum glStencilOp(StencilOp a_op) {
				switch (a_op) {
				case StencilOp::Zero:           return GL_ZERO;
				case StencilOp::Replace:        return GL_REPLACE;
				case StencilOp::IncrementClamp: return GL_INCR;
				case StencilOp::DecrementClamp: return GL_DECR;
				case StencilOp::Invert:         return GL_INVERT;
				case StencilOp::IncrementWrap:  return GL_INCR_WRAP;
				case StencilOp::DecrementWrap:  return GL_DECR_WRAP;
				case StencilOp::Keep:
				default:                        return GL_KEEP;
				}
			}
			GLint glAttribComponents(VertexAttributeFormat a_format) {
				switch (a_format) {
				case VertexAttributeFormat::Float1:       return 1;
				case VertexAttributeFormat::Float2:       return 2;
				case VertexAttributeFormat::Float3:       return 3;
				case VertexAttributeFormat::Float4:       return 4;
				case VertexAttributeFormat::UByte4_UNorm: return 4;
				default:                                  return 4;
				}
			}
		} // namespace

		std::unique_ptr<Device> Device::makeGL() {
			return std::make_unique<GLDevice>();
		}

		GLDevice::GLDevice() : state(std::make_unique<State>()) {
		}

		GLDevice::~GLDevice() {
		}

		bool GLDevice::initialize(const SwapchainDesc &a_desc) {
			// Draw2D::Window owns the GL context; this backend borrows the window only to present.
			sdlWindow = a_desc.sdlWindow;
			deviceCaps.deviceName = "OpenGL (reference backend)";
			deviceCaps.clipSpace = ClipSpace::GL_NegOneToOne;
			deviceCaps.flipViewportY = false;
			return true;
		}

		void GLDevice::shutdown() {
			// Must run while the GL context is current; Draw2D calls this before tearing it down.
			if (state) {
				state->textures.forEachLive([](State::GLTexture &t) { if (t.name) { glDeleteTextures(1, &t.name); } });
				state->buffers.forEachLive([](State::GLBuffer &b) { if (b.name) { glDeleteBuffers(1, &b.name); } });
				state->samplers.forEachLive([](State::GLSampler &s) { if (s.name) { glDeleteSamplers(1, &s.name); } });
				state->shaderModules.forEachLive([](State::GLShaderModule &m) { if (m.shader) { glDeleteShader(m.shader); } });
				state->pipelines.forEachLive([](State::GLPipeline &p) { if (p.program) { glDeleteProgram(p.program); } });
				state->renderTargets.forEachLive([](State::GLRenderTarget &rt) {
					if (rt.depthRenderbuffer) { glDeleteRenderbuffers(1, &rt.depthRenderbuffer); }
					if (rt.fbo) { glDeleteFramebuffers(1, &rt.fbo); }
				});
				state->textures.clear();
				state->buffers.clear();
				state->samplers.clear();
				state->shaderModules.clear();
				state->pipelines.clear();
				state->uniformSets.clear();
				state->renderTargets.clear();
			}
		}

		const DeviceCaps& GLDevice::caps() const {
			return deviceCaps;
		}

		// ---- buffers ----------------------------------------------------------------
		BoundBuffer GLDevice::createBuffer(BufferUsage a_usage, BufferUpdateHint a_hint, const void *a_data, size_t a_bytes) {
			const GLenum target =
				a_usage == BufferUsage::Index   ? GL_ELEMENT_ARRAY_BUFFER :
				a_usage == BufferUsage::Uniform ? GL_UNIFORM_BUFFER :
				                                  GL_ARRAY_BUFFER;
			const GLenum glHint =
				a_hint == BufferUpdateHint::Static ? GL_STATIC_DRAW :
				a_hint == BufferUpdateHint::Stream ? GL_STREAM_DRAW :
				                                     GL_DYNAMIC_DRAW;
			GLuint name = 0;
			glGenBuffers(1, &name);
			glBindBuffer(target, name);
			glBufferData(target, static_cast<GLsizeiptr>(a_bytes), a_data, glHint);
			return state->buffers.create(State::GLBuffer{ name, target });
		}
		void GLDevice::updateBuffer(BoundBuffer a_handle, const void *a_data, size_t a_bytes, size_t a_offset) {
			State::GLBuffer *b = state->buffers.get(a_handle);
			require<ResourceException>(b != nullptr, "GLDevice::updateBuffer called with a stale/null buffer handle");
			glBindBuffer(b->target, b->name);
			glBufferSubData(b->target, static_cast<GLintptr>(a_offset), static_cast<GLsizeiptr>(a_bytes), a_data);
		}
		void GLDevice::destroyBuffer(BoundBuffer a_handle) {
			State::GLBuffer b;
			if (state->buffers.remove(a_handle, b) && b.name) { glDeleteBuffers(1, &b.name); }
		}

		// ---- textures ---------------------------------------------------------------
		BoundTexture GLDevice::createTexture(const TextureDesc &a_desc, const void *a_initialPixels) {
			require<ResourceException>(a_desc.dimension == TextureDimension::Tex2D,
				"GLDevice::createTexture currently supports only Tex2D (arrays/cube/3D arrive with the 3D path)");
			GLint internalFormat; GLenum clientFormat, clientType;
			glFormatTriple(a_desc.format, internalFormat, clientFormat, clientType);

			GLuint name = 0;
			glGenTextures(1, &name);
			glBindTexture(GL_TEXTURE_2D, name);
			// BoundSampler overrides these at bind time; defaults keep the no-sampler-object path usable.
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, a_desc.width, a_desc.height, 0, clientFormat, clientType, a_initialPixels);
			if (a_desc.generateMips && a_initialPixels) { glGenerateMipmap(GL_TEXTURE_2D); }

			return state->textures.create(State::GLTexture{ name, a_desc.width, a_desc.height, internalFormat, clientFormat, clientType });
		}
		void GLDevice::updateTexture(BoundTexture a_handle, int a_mip, const Rect &a_rect, const void *a_pixels) {
			State::GLTexture *t = state->textures.get(a_handle);
			require<ResourceException>(t != nullptr, "GLDevice::updateTexture called with a stale/null texture handle");
			glBindTexture(GL_TEXTURE_2D, t->name);
			glTexSubImage2D(GL_TEXTURE_2D, a_mip, a_rect.x, a_rect.y, a_rect.width, a_rect.height, t->format, t->type, a_pixels);
		}
		void GLDevice::generateMips(BoundTexture a_handle) {
			State::GLTexture *t = state->textures.get(a_handle);
			require<ResourceException>(t != nullptr, "GLDevice::generateMips called with a stale/null texture handle");
			glBindTexture(GL_TEXTURE_2D, t->name);
			glGenerateMipmap(GL_TEXTURE_2D);
		}
		void GLDevice::destroyTexture(BoundTexture a_handle) {
			State::GLTexture t;
			if (state->textures.remove(a_handle, t) && t.name) { glDeleteTextures(1, &t.name); }
		}
		void GLDevice::readTexture(BoundTexture a_handle, std::vector<uint8_t> &a_out) {
			State::GLTexture *t = state->textures.get(a_handle);
			require<ResourceException>(t != nullptr, "GLDevice::readTexture called with a stale/null texture handle");
			a_out.resize(static_cast<size_t>(t->width) * static_cast<size_t>(t->height) * 4u);
			glBindTexture(GL_TEXTURE_2D, t->name);
			glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, a_out.data());
		}
		uint32_t GLDevice::nativeTextureId(BoundTexture a_handle) const {
			const State::GLTexture *t = state->textures.get(a_handle);
			return t ? static_cast<uint32_t>(t->name) : 0u;
		}

		// ---- samplers ---------------------------------------------------------------
		BoundSampler GLDevice::createSampler(const SamplerDesc &a_desc) {
			GLuint name = 0;
			glGenSamplers(1, &name);
			glSamplerParameteri(name, GL_TEXTURE_MIN_FILTER, glMinFilter(a_desc.min, a_desc.mip));
			glSamplerParameteri(name, GL_TEXTURE_MAG_FILTER, (a_desc.mag == Filter::Linear) ? GL_LINEAR : GL_NEAREST);
			glSamplerParameteri(name, GL_TEXTURE_WRAP_S, glWrap(a_desc.u));
			glSamplerParameteri(name, GL_TEXTURE_WRAP_T, glWrap(a_desc.v));
			glSamplerParameteri(name, GL_TEXTURE_WRAP_R, glWrap(a_desc.w));
			glSamplerParameterf(name, GL_TEXTURE_LOD_BIAS, a_desc.lodBias);
#ifdef GL_TEXTURE_MAX_ANISOTROPY
			if (a_desc.maxAnisotropy > 1.0f) { glSamplerParameterf(name, GL_TEXTURE_MAX_ANISOTROPY, a_desc.maxAnisotropy); }
#endif
			return state->samplers.create(State::GLSampler{ name });
		}

		// ---- frame / pass / draw recording -----------------------------------------
		void GLDevice::beginFrame() {
			// GL is immediate-mode and Draw2D makes the context current; nothing to acquire.
		}

		void GLDevice::beginDefaultPass(const float a_clearColor[4]) {
			// glClear obeys the color/depth/stencil write masks; force them full so a prior draw's
			// partial mask (a stencil mask's colorWriteMask=0, the stencil test's writeMask=0, or a
			// depthWrite=false pipeline) can't suppress this clear. draw() re-applies masks per draw.
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glDepthMask(GL_TRUE);
			glStencilMask(0xFF);
			glClearColor(a_clearColor[0], a_clearColor[1], a_clearColor[2], a_clearColor[3]);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		}

		void GLDevice::endPass() {
			// Pop back to the enclosing target (default framebuffer 0 when the stack empties).
			if (!state->passStack.empty()) { state->passStack.pop_back(); }
			glBindFramebuffer(GL_FRAMEBUFFER, state->passStack.empty() ? 0 : state->passStack.back());
		}

		void GLDevice::endFrame() {
			if (sdlWindow) {
				SDL_GL_SwapWindow(static_cast<SDL_Window*>(sdlWindow));
			}
		}

		// ---- shader modules ---------------------------------------------------------
		BoundShaderModule GLDevice::createShaderModule(const ShaderModuleDesc &a_desc) {
			require<ResourceException>(!a_desc.glslSource.empty(),
				"GLDevice::createShaderModule needs glslSource (the GL backend compiles GLSL, not SPIR-V)");
			const GLenum glStage = (a_desc.stage == ShaderStage::Fragment) ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER;
			GLuint shader = glCreateShader(glStage);
			const char *src = a_desc.glslSource.c_str();
			glShaderSource(shader, 1, &src, nullptr);
			glCompileShader(shader);
			GLint ok = GL_FALSE;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
			if (ok != GL_TRUE) {
				GLint logLen = 0;
				glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
				std::string log(static_cast<size_t>(logLen > 1 ? logLen : 1), '\0');
				glGetShaderInfoLog(shader, logLen, nullptr, log.data());
				glDeleteShader(shader);
				require<ResourceException>(false, "GLDevice::createShaderModule failed to compile: " + log);
			}
			return state->shaderModules.create(State::GLShaderModule{ shader, a_desc.stage });
		}

		// ---- pipelines --------------------------------------------------------------
		BoundPipeline GLDevice::createPipeline(const PipelineDesc &a_desc) {
			State::GLShaderModule *vs = state->shaderModules.get(a_desc.vertex);
			State::GLShaderModule *fs = state->shaderModules.get(a_desc.fragment);
			require<ResourceException>(vs && fs, "GLDevice::createPipeline given a stale/null shader module");

			GLuint program = glCreateProgram();
			glAttachShader(program, vs->shader);
			glAttachShader(program, fs->shader);
			glLinkProgram(program);
			GLint ok = GL_FALSE;
			glGetProgramiv(program, GL_LINK_STATUS, &ok);
			if (ok != GL_TRUE) {
				GLint logLen = 0;
				glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
				std::string log(static_cast<size_t>(logLen > 1 ? logLen : 1), '\0');
				glGetProgramInfoLog(program, logLen, nullptr, log.data());
				glDeleteProgram(program);
				require<ResourceException>(false, "GLDevice::createPipeline failed to link: " + log);
			}
			// Shaders can be detached after a successful link; the program owns the linked code.
			glDetachShader(program, vs->shader);
			glDetachShader(program, fs->shader);

			State::GLPipeline pipe;
			pipe.program = program;
			pipe.topology = a_desc.topology;
			pipe.blend = a_desc.blend;
			pipe.depthStencil = a_desc.depthStencil;
			pipe.raster = a_desc.raster;
			pipe.vertexLayout = a_desc.vertexLayout;
			return state->pipelines.create(std::move(pipe));
		}
		void GLDevice::destroyPipeline(BoundPipeline a_handle) {
			State::GLPipeline pipe;
			if (state->pipelines.remove(a_handle, pipe) && pipe.program) { glDeleteProgram(pipe.program); }
		}

		// ---- render targets (FBO) ---------------------------------------------------
		BoundRenderTarget GLDevice::createRenderTarget(const RenderTargetDesc &a_desc) {
			require<ResourceException>(!a_desc.color.empty(), "GLDevice::createRenderTarget needs at least one color attachment");
			State::GLRenderTarget rt;
			rt.width = a_desc.width;
			rt.height = a_desc.height;
			glGenFramebuffers(1, &rt.fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo);

			State::GLTexture *colorTex = state->textures.get(a_desc.color[0].texture);
			require<ResourceException>(colorTex != nullptr, "GLDevice::createRenderTarget color attachment is a stale/null texture");
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex->name, 0);

			if (a_desc.depth.present) {
				rt.ownsDepth = true;
				glGenRenderbuffers(1, &rt.depthRenderbuffer);
				glBindRenderbuffer(GL_RENDERBUFFER, rt.depthRenderbuffer);
				glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, a_desc.width, a_desc.height);
				glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rt.depthRenderbuffer);
			}
			const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			glBindFramebuffer(GL_FRAMEBUFFER, state->passStack.empty() ? 0 : state->passStack.back());
			require<ResourceException>(status == GL_FRAMEBUFFER_COMPLETE, "GLDevice::createRenderTarget produced an incomplete framebuffer");
			return state->renderTargets.create(rt);
		}
		void GLDevice::destroyRenderTarget(BoundRenderTarget a_handle) {
			State::GLRenderTarget rt;
			if (state->renderTargets.remove(a_handle, rt)) {
				if (rt.depthRenderbuffer) { glDeleteRenderbuffers(1, &rt.depthRenderbuffer); }
				if (rt.fbo) { glDeleteFramebuffers(1, &rt.fbo); }
			}
		}

		// ---- uniform sets (CPU-recorded; replayed in draw) --------------------------
		BoundUniformSet GLDevice::createUniformSet(BoundPipeline a_pipeline) {
			State::GLUniformSet set;
			set.pipeline = a_pipeline;
			return state->uniformSets.create(std::move(set));
		}
		void GLDevice::setUniform(BoundUniformSet a_handle, const std::string &a_name, const float *a_data, uint32_t a_floatCount) {
			State::GLUniformSet *set = state->uniformSets.get(a_handle);
			require<ResourceException>(set != nullptr, "GLDevice::setUniform called with a stale/null uniform set");
			require<ResourceException>(a_floatCount >= 1 && a_floatCount <= 4, "GLDevice::setUniform supports 1..4 floats (use setUniformMatrix for mat4)");
			State::UniformValue v;
			v.count = a_floatCount;
			v.isMatrix = false;
			for (uint32_t i = 0; i < a_floatCount; ++i) { v.data[i] = a_data[i]; }
			set->uniforms[a_name] = v;
		}
		void GLDevice::setUniformMatrix(BoundUniformSet a_handle, const std::string &a_name, const float *a_columnMajor16) {
			State::GLUniformSet *set = state->uniformSets.get(a_handle);
			require<ResourceException>(set != nullptr, "GLDevice::setUniformMatrix called with a stale/null uniform set");
			State::UniformValue v;
			v.count = 16;
			v.isMatrix = true;
			for (uint32_t i = 0; i < 16; ++i) { v.data[i] = a_columnMajor16[i]; }
			// GL clip space matches the engine's matrices; no ClipSpace correction needed.
			set->uniforms[a_name] = v;
		}
		void GLDevice::setTexture(BoundUniformSet a_handle, const std::string &a_name, BoundTexture a_texture, BoundSampler a_sampler) {
			State::GLUniformSet *set = state->uniformSets.get(a_handle);
			require<ResourceException>(set != nullptr, "GLDevice::setTexture called with a stale/null uniform set");
			set->textures[a_name] = State::TextureSlot{ a_texture, a_sampler };
		}

		// ---- passes -----------------------------------------------------------------
		void GLDevice::beginPass(BoundRenderTarget a_target, const RenderTargetDesc &a_clearInfo) {
			GLuint fbo = 0;
			int w = 0, h = 0;
			if (a_target.valid()) {
				State::GLRenderTarget *rt = state->renderTargets.get(a_target);
				require<ResourceException>(rt != nullptr, "GLDevice::beginPass given a stale/null render target");
				fbo = rt->fbo;
				w = rt->width;
				h = rt->height;
			}
			state->passStack.push_back(fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			if (a_target.valid()) {
				glViewport(0, 0, w, h);
			}
			if (!a_clearInfo.color.empty() && a_clearInfo.color[0].load == LoadOp::Clear) {
				// Same caveat as beginDefaultPass: a clear obeys the write masks, so force them full.
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glDepthMask(GL_TRUE);
				glStencilMask(0xFF);
				const float *c = a_clearInfo.color[0].clearColor;
				glClearColor(c[0], c[1], c[2], c[3]);
				GLbitfield mask = GL_COLOR_BUFFER_BIT;
				if (a_clearInfo.depth.present && a_clearInfo.depth.load == LoadOp::Clear) {
					mask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
				}
				glClear(mask);
			}
		}

		void GLDevice::setViewport(const Viewport &a_viewport) {
			glViewport(static_cast<GLint>(a_viewport.x), static_cast<GLint>(a_viewport.y),
			           static_cast<GLsizei>(a_viewport.width), static_cast<GLsizei>(a_viewport.height));
		}
		void GLDevice::setScissor(const Rect &a_rect) {
			glEnable(GL_SCISSOR_TEST);
			glScissor(a_rect.x, a_rect.y, a_rect.width, a_rect.height);
		}

		// GL stencil state is global and persists across draws, so apply it here; draw() leaves it alone.
		void GLDevice::setStencilState(const StencilState &a_state) {
			state->currentStencil = a_state;
			if (!a_state.enabled) {
				glDisable(GL_STENCIL_TEST);
				return;
			}
			glEnable(GL_STENCIL_TEST);
			glStencilFunc(glCompare(a_state.compare), a_state.reference, a_state.compareMask);
			// glStencilOpSeparate (not the 3-arg glStencilOp) avoids clashing with the glStencilOp() enum helper.
			glStencilOpSeparate(GL_FRONT_AND_BACK, glStencilOp(a_state.fail), glStencilOp(a_state.depthFail), glStencilOp(a_state.pass));
			glStencilMask(a_state.writeMask);
		}

		void GLDevice::clearStencil(uint8_t a_value) {
			glStencilMask(0xFF);          // glClear honors the stencil write mask.
			glClearStencil(a_value);
			glClear(GL_STENCIL_BUFFER_BIT);
		}

		// ---- draw -------------------------------------------------------------------
		void GLDevice::draw(const DrawItem &a_item) {
			State::GLPipeline *pipe = state->pipelines.get(a_item.pipeline);
			require<ResourceException>(pipe != nullptr, "GLDevice::draw given a stale/null pipeline");

			GLint unitsBound = 0; // texture units this draw touched; restored to defaults at end.
			glUseProgram(pipe->program);

			if (pipe->blend.enabled) {
				glEnable(GL_BLEND);
				glBlendFuncSeparate(glBlendFactor(pipe->blend.srcColor), glBlendFactor(pipe->blend.dstColor),
				                    glBlendFactor(pipe->blend.srcAlpha), glBlendFactor(pipe->blend.dstAlpha));
				glBlendEquationSeparate(glBlendEquation(pipe->blend.colorOp), glBlendEquation(pipe->blend.alphaOp));
			} else {
				glDisable(GL_BLEND);
			}
			// Color write mask is a pipeline property (Metal bakes it); apply it every draw so a stencil
			// mask's colorWriteMask=0 never leaks into the next draw. Stencil itself is dynamic device
			// state (setStencilState) and persists across draws — draw() must not touch it here.
			const uint8_t cwm = pipe->blend.colorWriteMask;
			glColorMask((cwm & 0x1) ? GL_TRUE : GL_FALSE, (cwm & 0x2) ? GL_TRUE : GL_FALSE,
			            (cwm & 0x4) ? GL_TRUE : GL_FALSE, (cwm & 0x8) ? GL_TRUE : GL_FALSE);

			if (pipe->depthStencil.depthTest) {
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(glCompare(pipe->depthStencil.depthCompare));
			} else {
				glDisable(GL_DEPTH_TEST);
			}
			glDepthMask(pipe->depthStencil.depthWrite ? GL_TRUE : GL_FALSE);

			if (pipe->raster.cull == CullMode::None) {
				glDisable(GL_CULL_FACE);
			} else {
				glEnable(GL_CULL_FACE);
				glCullFace(pipe->raster.cull == CullMode::Front ? GL_FRONT : GL_BACK);
			}

			if (a_item.uniforms.valid()) {
				State::GLUniformSet *set = state->uniformSets.get(a_item.uniforms);
				require<ResourceException>(set != nullptr, "GLDevice::draw given a stale/null uniform set");
				for (auto &kv : set->uniforms) {
					const GLint loc = pipe->location(kv.first);
					if (loc < 0) { continue; }
					const State::UniformValue &v = kv.second;
					if (v.isMatrix) {
						glUniformMatrix4fv(loc, 1, GL_FALSE, v.data);
					} else {
						switch (v.count) {
						case 1: glUniform1fv(loc, 1, v.data); break;
						case 2: glUniform2fv(loc, 1, v.data); break;
						case 3: glUniform3fv(loc, 1, v.data); break;
						case 4: glUniform4fv(loc, 1, v.data); break;
						default: break;
						}
					}
				}
				for (auto &kv : set->textures) {
					const GLint loc = pipe->location(kv.first);
					if (loc < 0) { continue; }
					// Always wire the sampler uniform (bind GL 0 for null/stale) so it never samples a stale unit-0 binding.
					State::GLTexture *tex = state->textures.get(kv.second.texture);
					glActiveTexture(GL_TEXTURE0 + unitsBound);
					glBindTexture(GL_TEXTURE_2D, tex ? tex->name : 0);
					State::GLSampler *smp = state->samplers.get(kv.second.sampler);
					glBindSampler(static_cast<GLuint>(unitsBound), smp ? smp->name : 0);
					glUniform1i(loc, unitsBound);
					++unitsBound;
				}
			}

			State::GLBuffer *vb = state->buffers.get(a_item.vertexBuffer);
			require<ResourceException>(vb != nullptr, "GLDevice::draw given a stale/null vertex buffer");
			glBindBuffer(GL_ARRAY_BUFFER, vb->name);
			for (const auto &attr : pipe->vertexLayout.attributes) {
				glEnableVertexAttribArray(attr.location);
				glVertexAttribPointer(attr.location, glAttribComponents(attr.format), GL_FLOAT, GL_FALSE,
				                      static_cast<GLsizei>(pipe->vertexLayout.stride),
				                      reinterpret_cast<const GLvoid*>(static_cast<size_t>(attr.offset)));
			}

			const GLenum mode = glTopology(pipe->topology);
			const GLenum indexType = (a_item.indexType == IndexType::Uint16) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
			const size_t indexBytes = (a_item.indexType == IndexType::Uint16) ? 2u : 4u;
			State::GLBuffer *ib = state->buffers.get(a_item.indexBuffer);
			require<ResourceException>(ib != nullptr, "GLDevice::draw given a stale/null index buffer");
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->name);
			const GLvoid *indexOffset = reinterpret_cast<const GLvoid*>(static_cast<size_t>(a_item.firstIndex) * indexBytes);
			if (a_item.baseVertex != 0) {
				glDrawElementsBaseVertex(mode, static_cast<GLsizei>(a_item.indexCount), indexType,
				                         const_cast<GLvoid*>(indexOffset), a_item.baseVertex);
			} else {
				glDrawElements(mode, static_cast<GLsizei>(a_item.indexCount), indexType, indexOffset);
			}

			for (const auto &attr : pipe->vertexLayout.attributes) {
				glDisableVertexAttribArray(attr.location);
			}

			// Engine uses no VAO, so reset the global buffer/sampler/active-unit bindings the legacy
			// immediate-mode paths (emitter/spine) still depend on.
			for (GLint u = unitsBound - 1; u >= 0; --u) {
				glActiveTexture(GL_TEXTURE0 + u);
				glBindSampler(static_cast<GLuint>(u), 0);
				glBindTexture(GL_TEXTURE_2D, 0);
			}
			glActiveTexture(GL_TEXTURE0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glUseProgram(0);
		}

		void GLDevice::onSurfaceResized(int a_widthPixels, int a_heightPixels) {
			// No swapchain to recreate; just reset the default-target viewport to the new size.
			glViewport(0, 0, a_widthPixels, a_heightPixels);
		}

	} // namespace Render
} // namespace MV
