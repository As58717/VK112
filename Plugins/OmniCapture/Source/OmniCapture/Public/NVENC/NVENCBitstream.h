// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_OMNI_NVENC

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#endif
#include "nvEncodeAPI.h"
#if PLATFORM_WINDOWS
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace OmniNVENC
{
    struct FNVENCEncodedPacket
    {
        TArray<uint8> Data;
        bool bKeyFrame = false;
        uint64 Timestamp = 0;
    };

    /** Utility that wraps the nvEncLockBitstream/nvEncUnlockBitstream pair. */
    class FNVENCBitstream
    {
    public:
        bool Initialize(void* InEncoder, const NV_ENCODE_API_FUNCTION_LIST& InFunctions, uint32 InApiVersion, uint32 InBufferSize = 0);
        void Release();

        bool IsValid() const { return OutputBuffer != nullptr; }

        NV_ENC_OUTPUT_PTR GetBitstreamBuffer() const { return OutputBuffer; }

        bool Lock(void*& OutBitstreamBuffer, int32& OutSizeInBytes);
        void Unlock();

        bool ExtractPacket(FNVENCEncodedPacket& OutPacket);

    private:
        void* Encoder = nullptr;
        const NV_ENCODE_API_FUNCTION_LIST* Functions = nullptr;
        NV_ENC_OUTPUT_PTR OutputBuffer = nullptr;
        NV_ENC_LOCK_BITSTREAM LockedParams = {};
        bool bIsLocked = false;
        uint32 ApiVersion = NVENCAPI_VERSION;
    };
}

#endif // WITH_OMNI_NVENC

