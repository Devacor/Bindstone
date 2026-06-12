#ifndef _MV_SHAREDTEXTURES_H_
#define _MV_SHAREDTEXTURES_H_

#include <fstream>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <atomic>

#include <filesystem>

#include "texturePacker.h"

#include "MV/Serialization/serialize.h"

namespace MV {
	class ThreadPool;
	class Draw2D;
	namespace Render { class Device; }

	class SharedTextures {
		friend FileTextureDefinition;
	public:
		// Injected by the owner (see Managers) so the texture system can route GPU uploads through
		// the active Render::Device instead of issuing raw GL. Borrowed, not owned; may be null in
		// headless/tool contexts (then loads stay CPU-side / no upload).
		void renderer(Draw2D* a_renderer) { ourRenderer = a_renderer; }
		Draw2D* renderer() const { return ourRenderer; }
		Render::Device* device() const;

		inline static const std::vector<std::string> validExtensions { ".jpg", ".png", ".bmp", ".tga", ".gif", ".webp" };
		struct PackItem {
			std::string id;
			std::shared_ptr<FileTextureDefinition> texture;
			BoxAABB<float> sliceBounds;

			bool operator<(const PackItem& a_rhs) const {
				return texture->size().area() > a_rhs.texture->size().area();
			}

			bool operator>(const PackItem& a_rhs) const {
				return texture->size().area() > a_rhs.texture->size().area();
			}
		};

		std::shared_ptr<TexturePack> pack(const std::string &a_name);
		std::shared_ptr<TexturePack> pack(const std::string &a_name, Draw2D* a_renderer);
		std::shared_ptr<FileTextureDefinition> file(const std::string &a_filename, bool a_repeat = false, bool a_pixel = false);
		std::shared_ptr<DynamicTextureDefinition> dynamic(const std::string &a_identifier, const Size<int> &a_size);
		std::shared_ptr<SurfaceTextureDefinition> surface(const std::string &a_identifier, std::function<std::shared_ptr<OwnedSurface>()> a_surfaceGenerator);

		void files(const std::string &a_rootDirectory, bool a_repeat = false, bool a_pixel = false);

		std::vector<std::pair<std::string, bool>> fileIds() const;
		std::vector<std::string> packIds() const;

		void assemblePacks(const std::string &a_rootDirectory, Draw2D* a_renderer);
		std::shared_ptr<TexturePack> assemblePack(const std::string &a_packPath, Draw2D* a_renderer);

		template<class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/){
			archive(
				jai::serialization::make_nvp("texturePacks", texturePacks),
				jai::serialization::make_nvp("fileDefinitions", fileDefinitions),
				jai::serialization::make_nvp("dynamicDefinitions", dynamicDefinitions),
				jai::serialization::make_nvp("surfaceDefinitions", surfaceDefinitions));
		}

		static std::shared_ptr<TextureHandle> white(){
			static std::shared_ptr<DynamicTextureDefinition> defaultTexture;
			static std::shared_ptr<TextureHandle> defaultHandle;
			if(!defaultTexture){
				defaultTexture = DynamicTextureDefinition::make("defaultTexture", {1, 1}, {1.0f, 1.0f, 1.0f, 1.0f});
				defaultHandle = defaultTexture->makeHandle();
			}
			return defaultHandle;
		}

		static std::string fileId(const std::string &a_filename, bool a_repeat, bool a_pixel = false) {
			return a_filename + (a_repeat ? "1" : "0") + (a_pixel ? "1" : "");
		}

		// ---- Streaming / parallel texture loading (manager-owned) ------------------------
		// loadFile decodes a file texture's image on the thread pool and uploads it to GL on
		// the MAIN thread (delivered when the pool's main-thread completion callbacks run from
		// the game loop, i.e. ThreadPool::run()). Until then the definition reports the
		// magenta/black "loading" checker placeholder. on_loaded (optional) fires on the main
		// thread once the texture is live. a_blocking=true decodes + uploads inline instead.
		// This is owned entirely by the texture system, so every load path benefits — the scene
		// loader no longer brackets anything.
		void loadThreadPool(ThreadPool* a_pool) { ourLoadThreadPool = a_pool; }
		ThreadPool* loadThreadPool() const { return ourLoadThreadPool; }

		void loadFile(const std::shared_ptr<FileTextureDefinition>& a_definition,
		              bool a_blocking = false,
		              std::function<void()> a_onLoaded = {});

		// Textures still decoding/uploading — for blocking waits and load screens.
		int pendingTextureLoads() const { return ourPendingTextureLoads.load(); }

		// GL id of the magenta/black checker shown while a texture streams in (lazily created
		// on the GL thread; 0 in headless).
		static GLuint loadingPlaceholderId();

	private:
		std::vector<std::string> getImagesInFolder(const std::string& a_packPath) const;
		std::vector<SharedTextures::PackItem> getSortedPackItems(std::vector<std::string> imagePaths) const;

		std::map<std::string, std::shared_ptr<TexturePack>> texturePacks;
		std::map<std::string, std::shared_ptr<FileTextureDefinition>> fileDefinitions;
		std::map<std::string, std::shared_ptr<DynamicTextureDefinition>> dynamicDefinitions;
		std::map<std::string, std::shared_ptr<SurfaceTextureDefinition>> surfaceDefinitions;

		ThreadPool* ourLoadThreadPool = nullptr;
		Draw2D* ourRenderer = nullptr;   // borrowed; supplies the active Render::Device for uploads.
		std::atomic<int> ourPendingTextureLoads{ 0 };

		// In-flight decode coalescing: many definitions can reference one image file. The first
		// kicks the decode; the rest wait on it (no redundant decode) and are all finalized
		// together when that single decode lands. Touched only on the main thread.
		struct PendingTextureLoad {
			std::shared_ptr<FileTextureDefinition> definition;
			std::function<void()> onLoaded;
		};
		std::map<TextureParameters, std::vector<PendingTextureLoad>> ourInFlightLoads;
	};
}

#endif
