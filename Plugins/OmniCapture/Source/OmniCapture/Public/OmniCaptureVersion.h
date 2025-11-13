#pragma once

#include "Misc/EngineVersionComparison.h"

#if !defined(ENGINE_MAJOR_VERSION)
#include "Runtime/Launch/Resources/Version.h"
#endif

#ifndef ENGINE_PATCH_VERSION
#define ENGINE_PATCH_VERSION 0
#endif

#if defined(UE_VERSION_AT_LEAST)
#define OMNICAPTURE_UE_VERSION_AT_LEAST(Major, Minor, Patch) UE_VERSION_AT_LEAST(Major, Minor, Patch)
#else
#define OMNICAPTURE_UE_VERSION_AT_LEAST(Major, Minor, Patch) \
    ((ENGINE_MAJOR_VERSION > (Major)) || \
    (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION > (Minor)) || \
    (ENGINE_MAJOR_VERSION == (Major) && ENGINE_MINOR_VERSION == (Minor) && ENGINE_PATCH_VERSION >= (Patch)))
#endif

