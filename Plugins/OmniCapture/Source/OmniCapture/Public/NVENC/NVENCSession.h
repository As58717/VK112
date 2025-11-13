// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_OMNI_NVENC

#include "CoreMinimal.h"

#include "NVENC/NVENCParameters.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#include "nvEncodeAPI.h"
#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace OmniNVENC
{
    /**
     * Thin wrapper that models the lifecycle of an NVENC encoder instance.
     * The full implementation is proprietary but the skeleton is useful for
     * integration tests and for keeping the public API stable.
     */
    class FNVENCSession
    {
    public:
        FNVENCSession() = default;

        bool Open(ENVENCCodec Codec, void* InDevice = nullptr, NV_ENC_DEVICE_TYPE InDeviceType = NV_ENC_DEVICE_TYPE_DIRECTX);
        bool Initialize(const FNVENCParameters& Parameters);
        bool ValidatePresetConfiguration(ENVENCCodec Codec, bool bAllowNullFallback = true);
        bool Reconfigure(const FNVENCParameters& Parameters);
        void Flush();
        void Destroy();

        bool GetSequenceParams(TArray<uint8>& OutData);

        bool IsOpen() const { return bIsOpen; }
        bool IsInitialised() const { return bIsInitialised; }

        const FNVENCParameters& GetParameters() const { return CurrentParameters; }

        void* GetEncoderHandle() const { return Encoder; }

        const NV_ENCODE_API_FUNCTION_LIST& GetFunctionList() const { return FunctionList; }

        const NV_ENC_INITIALIZE_PARAMS& GetInitializeParams() const { return InitializeParams; }
        const NV_ENC_CONFIG& GetEncodeConfig() const { return EncodeConfig; }
        NV_ENC_BUFFER_FORMAT GetNVBufferFormat() const { return NvBufferFormat; }
        uint32 GetApiVersion() const { return ApiVersion; }
        const FString& GetLastError() const { return LastErrorMessage; }

    private:
        bool bIsOpen = false;
        bool bIsInitialised = false;
        FNVENCParameters CurrentParameters;
        void* Encoder = nullptr;
        void* Device = nullptr;
        NV_ENC_DEVICE_TYPE DeviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
        NV_ENCODE_API_FUNCTION_LIST FunctionList = {};
        NV_ENC_INITIALIZE_PARAMS InitializeParams = {};
        NV_ENC_CONFIG EncodeConfig = {};
        NV_ENC_BUFFER_FORMAT NvBufferFormat = NV_ENC_BUFFER_FORMAT_UNDEFINED;
        uint32 ApiVersion = NVENCAPI_VERSION;
        FString LastErrorMessage;
    };
}

#endif // WITH_OMNI_NVENC

