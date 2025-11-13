// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace OmniNVENC
{
    /**
     * Lightweight loader that resolves all NVENC entry points we rely on at runtime.
     *
     * The trimmed version of the encoder that ships with this repository does not
     * perform actual encoding but we still expose the loader to make it easier to
     * reason about the code and to provide meaningful validation errors when the
     * runtime is missing.
     */
    class FNVEncodeAPILoader
    {
    public:
        struct FFunctions
        {
            void* NvEncodeAPICreateInstance = nullptr;
            void* NvEncOpenEncodeSessionEx = nullptr;
            void* NvEncInitializeEncoder = nullptr;
            void* NvEncReconfigureEncoder = nullptr;
            void* NvEncEncodePicture = nullptr;
            void* NvEncDestroyEncoder = nullptr;
            void* NvEncFlushEncoderQueue = nullptr;
            void* NvEncGetEncodeCaps = nullptr;
            void* NvEncGetEncodePresetGUIDs = nullptr;
            void* NvEncGetEncodeProfileGUIDs = nullptr;
            void* NvEncGetEncodePresetConfig = nullptr;
            void* NvEncCreateInputBuffer = nullptr;
            void* NvEncDestroyInputBuffer = nullptr;
            void* NvEncCreateBitstreamBuffer = nullptr;
            void* NvEncDestroyBitstreamBuffer = nullptr;
            void* NvEncRegisterResource = nullptr;
            void* NvEncUnregisterResource = nullptr;
            void* NvEncMapInputResource = nullptr;
            void* NvEncUnmapInputResource = nullptr;
            void* NvEncLockInputBuffer = nullptr;
            void* NvEncUnlockInputBuffer = nullptr;
            void* NvEncLockBitstream = nullptr;
            void* NvEncUnlockBitstream = nullptr;
            void* NvEncGetSequenceParams = nullptr;
        };

    public:
        static FNVEncodeAPILoader& Get();

        /** Attempts to load the NVENC runtime and resolve all required functions. */
        bool Load();

        /** Releases any function pointers and unloads the module. */
        void Unload();

        /** Returns true if Load() succeeded. */
        bool IsLoaded() const { return bLoaded; }

        /** Returns the resolved function table. */
        const FFunctions& GetFunctions() const { return Functions; }

        /** Queries an individual function pointer by name. */
        void* GetFunction(const ANSICHAR* FunctionName) const;

    private:
        FNVEncodeAPILoader() = default;

        void Reset();

    private:
        bool bAttemptedLoad = false;
        bool bLoaded = false;
        FFunctions Functions;
    };
}

