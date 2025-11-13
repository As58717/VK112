// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace OmniNVENC
{
    /**
     * Minimal helper responsible for loading the NVENC runtime module on demand.
     */
    class FNVENCCommon
    {
    public:
        /** Attempt to load the NVENC runtime. */
        static bool EnsureLoaded();

        /** Return the dll handle if it was successfully loaded. */
        static void* GetHandle();

        /** Unload the runtime when the module shuts down. */
        static void Shutdown();

        /** Overrides the DLL filename that will be loaded. */
        static void SetOverrideDllPath(const FString& InOverridePath);

        /** Overrides the directory used when attempting auto detection. */
        static void SetSearchDirectory(const FString& InSearchDirectory);

        /** Returns the currently configured override path. */
        static FString GetOverrideDllPath();

        /** Returns the current search directory used for automatic runtime discovery, if any. */
        static FString GetSearchDirectory();

        /** Returns the resolved DLL path based on the current overrides. */
        static FString GetResolvedDllPath();

    private:
        static FString ResolveDllPath();
        static FString GetDefaultDllName();
        static FCriticalSection& GetMutex();
    };
}

