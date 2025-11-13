// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVEncodeAPILoader.h"

#if WITH_OMNI_NVENC

#include "NVENC/NVENCCommon.h"

#include "Containers/StringConv.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"

#if !defined(DEFINE_LOG_CATEGORY_STATIC)
#define DEFINE_LOG_CATEGORY_STATIC(LogCategoryName, DefaultVerbosity, CompileTimeVerbosity)
#endif

#if !defined(UE_LOG)
#define UE_LOG(CategoryName, Verbosity, Format, ...)
#endif

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#include "nvEncodeAPI.h"
#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#ifndef UE_NVENC_HAS_FLUSH_FUNCTION
#if defined(NVENCAPI_MAJOR_VERSION) && NVENCAPI_MAJOR_VERSION < 12
#define UE_NVENC_HAS_FLUSH_FUNCTION 1
#else
#define UE_NVENC_HAS_FLUSH_FUNCTION 0
#endif
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNVEncodeAPILoader, Log, All);

namespace OmniNVENC
{
    namespace
    {
        struct FFunctionLookup
        {
            const ANSICHAR* Name;
            void** Target;
        };

        template <int32 N>
        bool ResolveFunctions(void* LibraryHandle, const FFunctionLookup (&Entries)[N])
        {
            if (!LibraryHandle)
            {
                return false;
            }

            for (const FFunctionLookup& Entry : Entries)
            {
                if (!Entry.Target)
                {
                    continue;
                }

                const auto EntryNameWide = StringCast<TCHAR>(Entry.Name);
                *Entry.Target = FPlatformProcess::GetDllExport(LibraryHandle, EntryNameWide.Get());
                if (!*Entry.Target)
                {
                    UE_LOG(LogNVEncodeAPILoader, Verbose, TEXT("Failed to resolve NVENC export '%s'."), ANSI_TO_TCHAR(Entry.Name));
                    return false;
                }
            }

            return true;
        }
    }

    FNVEncodeAPILoader& FNVEncodeAPILoader::Get()
    {
        static FNVEncodeAPILoader Instance;
        return Instance;
    }

    bool FNVEncodeAPILoader::Load()
    {
        if (bLoaded)
        {
            return true;
        }

        if (bAttemptedLoad && !bLoaded)
        {
            return false;
        }

        bAttemptedLoad = true;

        if (!FNVENCCommon::EnsureLoaded())
        {
            UE_LOG(LogNVEncodeAPILoader, Warning, TEXT("Failed to load NVENC runtime module."));
            Reset();
            return false;
        }

        void* const LibraryHandle = FNVENCCommon::GetHandle();
        if (!LibraryHandle)
        {
            UE_LOG(LogNVEncodeAPILoader, Warning, TEXT("NVENC module handle was null."));
            Reset();
            return false;
        }

        const FFunctionLookup Lookups[] = {
            { "NvEncodeAPICreateInstance", &Functions.NvEncodeAPICreateInstance },
        };

        if (!ResolveFunctions(LibraryHandle, Lookups))
        {
            UE_LOG(LogNVEncodeAPILoader, Warning, TEXT("NVENC runtime is missing required exports."));
            Reset();
            return false;
        }

        bLoaded = true;
        return true;
    }

    void FNVEncodeAPILoader::Unload()
    {
        Reset();
        FNVENCCommon::Shutdown();
    }

    void* FNVEncodeAPILoader::GetFunction(const ANSICHAR* FunctionName) const
    {
        if (!FunctionName)
        {
            return nullptr;
        }

        const FFunctionLookup Lookups[] = {
            { "NvEncodeAPICreateInstance", const_cast<void**>(&Functions.NvEncodeAPICreateInstance) },
        };

        for (const FFunctionLookup& Lookup : Lookups)
        {
            if (FCStringAnsi::Stricmp(FunctionName, Lookup.Name) == 0)
            {
                return Lookup.Target ? *Lookup.Target : nullptr;
            }
        }

        return nullptr;
    }

    void FNVEncodeAPILoader::Reset()
    {
        Functions = FFunctions();
        bLoaded = false;
    }
}

#else

namespace OmniNVENC
{
    FNVEncodeAPILoader& FNVEncodeAPILoader::Get()
    {
        static FNVEncodeAPILoader Instance;
        return Instance;
    }

    bool FNVEncodeAPILoader::Load()
    {
        bAttemptedLoad = true;
        bLoaded = false;
        return false;
    }

    void FNVEncodeAPILoader::Unload()
    {
        Reset();
    }

    void* FNVEncodeAPILoader::GetFunction(const ANSICHAR*) const
    {
        return nullptr;
    }

    void FNVEncodeAPILoader::Reset()
    {
        bAttemptedLoad = false;
        bLoaded = false;
        Functions = FFunctions();
    }
}

#endif // WITH_OMNI_NVENC

