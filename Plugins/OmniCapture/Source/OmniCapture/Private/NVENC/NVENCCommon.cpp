// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCCommon.h"

#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCCommon, Log, All);

namespace OmniNVENC
{
    namespace
    {
        struct FNVENCLoader
        {
            void* ModuleHandle = nullptr;
            FString OverrideDllPath;
            FString SearchDirectory;
        };

        FNVENCLoader& GetLoader()
        {
            static FNVENCLoader Loader;
            return Loader;
        }
    }

    void FNVENCCommon::SetOverrideDllPath(const FString& InOverridePath)
    {
        FScopeLock Lock(&GetMutex());
        GetLoader().OverrideDllPath = InOverridePath;
    }

    void FNVENCCommon::SetSearchDirectory(const FString& InSearchDirectory)
    {
        FScopeLock Lock(&GetMutex());
        GetLoader().SearchDirectory = InSearchDirectory;
    }

    FString FNVENCCommon::GetOverrideDllPath()
    {
        FScopeLock Lock(&GetMutex());
        return GetLoader().OverrideDllPath;
    }

    FString FNVENCCommon::GetSearchDirectory()
    {
        FScopeLock Lock(&GetMutex());
        return GetLoader().SearchDirectory;
    }

    FString FNVENCCommon::GetResolvedDllPath()
    {
        FScopeLock Lock(&GetMutex());
        return ResolveDllPath();
    }

    bool FNVENCCommon::EnsureLoaded()
    {
        FScopeLock Lock(&GetMutex());
        FNVENCLoader& Loader = GetLoader();
        if (Loader.ModuleHandle)
        {
            return true;
        }

#if PLATFORM_WINDOWS
        const FString DllPath = ResolveDllPath();
        if (DllPath.IsEmpty())
        {
            UE_LOG(LogNVENCCommon, Warning, TEXT("Unable to determine NVENC runtime path."));
            return false;
        }

        Loader.ModuleHandle = FPlatformProcess::GetDllHandle(*DllPath);
        if (!Loader.ModuleHandle)
        {
            UE_LOG(LogNVENCCommon, Warning, TEXT("Unable to load NVENC runtime module '%s'."), *DllPath);
            return false;
        }
        return true;
#else
        UE_LOG(LogNVENCCommon, Warning, TEXT("NVENC runtime loading only implemented on Windows."));
        return false;
#endif
    }

    void* FNVENCCommon::GetHandle()
    {
        FScopeLock Lock(&GetMutex());
        return GetLoader().ModuleHandle;
    }

    void FNVENCCommon::Shutdown()
    {
        FScopeLock Lock(&GetMutex());
        FNVENCLoader& Loader = GetLoader();
        if (Loader.ModuleHandle)
        {
            FPlatformProcess::FreeDllHandle(Loader.ModuleHandle);
            Loader.ModuleHandle = nullptr;
        }
    }

    FString FNVENCCommon::ResolveDllPath()
    {
        FNVENCLoader& Loader = GetLoader();
        FString Candidate = Loader.OverrideDllPath;
        if (!Candidate.IsEmpty())
        {
            return Candidate;
        }

        Candidate = GetDefaultDllName();
        if (!Loader.SearchDirectory.IsEmpty())
        {
            FString Directory = Loader.SearchDirectory;
            FPaths::NormalizeDirectoryName(Directory);
            return FPaths::Combine(Directory, Candidate);
        }

#if PLATFORM_WINDOWS
        const FString SystemRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("SystemRoot"));
        if (!SystemRoot.IsEmpty())
        {
            const FString SystemDirectory = FPaths::Combine(SystemRoot, TEXT("System32"));
            const FString SystemDllPath = FPaths::Combine(SystemDirectory, Candidate);
            if (FPaths::FileExists(SystemDllPath))
            {
                return SystemDllPath;
            }
        }
#endif

        return Candidate;
    }

    FString FNVENCCommon::GetDefaultDllName()
    {
#if PLATFORM_WINDOWS
    #if PLATFORM_64BITS
        return TEXT("nvEncodeAPI64.dll");
    #else
        return TEXT("nvEncodeAPI.dll");
    #endif
#else
        return FString();
#endif
    }

    FCriticalSection& FNVENCCommon::GetMutex()
    {
        static FCriticalSection Mutex;
        return Mutex;
    }
}

