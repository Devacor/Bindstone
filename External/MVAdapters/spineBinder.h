#ifndef __SPINE_BINDER_H_
#define __SPINE_BINDER_H_

//This binder allows us to set customization points at runtime from a different compilation boundary so we can compile spine separately from our library

#include "spine/spine.h"
#include "spine/extension.h"
#include <functional>

extern std::function<char* (const char*, int*)> Spine_ReadFileHandler;
extern std::function<void(spAtlasPage * self)> Spine_DisposeTextureHandler;
extern std::function<void(spAtlasPage * self, const char* path)> Spine_CreateTextureHandler;

#endif