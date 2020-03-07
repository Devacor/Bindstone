#include "MVAdapters/spineBinder.h"

std::function<char* (const char*, int*)> Spine_ReadFileHandler;
std::function<void(spAtlasPage *)> Spine_DisposeTextureHandler;
std::function<void(spAtlasPage *, const char*)> Spine_CreateTextureHandler;

//these implementations are required for spine!
void _spAtlasPage_createTexture(spAtlasPage* self, const char* path) {
	Spine_CreateTextureHandler(self, path);
}

void _spAtlasPage_disposeTexture(spAtlasPage* self) {
	Spine_DisposeTextureHandler(self);
}

char* _spUtil_readFile(const char* path, int* length) {
	return Spine_ReadFileHandler(path, length);
}