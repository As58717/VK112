// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCBitstream.h"

#if WITH_OMNI_NVENC

#include "NVENC/NVENCDefs.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCBitstream, Log, All);

namespace OmniNVENC
{
    namespace
    {
#if PLATFORM_WINDOWS
        template <typename TFunc>
        bool ValidateFunction(const ANSICHAR* Name, TFunc* Function)
        {
            if (!Function)
            {
                UE_LOG(LogNVENCBitstream, Error, TEXT("Required NVENC export '%s' is missing."), ANSI_TO_TCHAR(Name));
                return false;
            }
            return true;
        }
#endif
    }

    bool FNVENCBitstream::Initialize(void* InEncoder, const NV_ENCODE_API_FUNCTION_LIST& InFunctions, uint32 InApiVersion, uint32 InBufferSize)
    {
#if !PLATFORM_WINDOWS
        UE_LOG(LogNVENCBitstream, Warning, TEXT("NVENC bitstream buffers are only available on Windows builds."));
        return false;
#else
        Release();

        ApiVersion = InApiVersion;
        Encoder = InEncoder;
        Functions = &InFunctions;

        if (!Encoder)
        {
            UE_LOG(LogNVENCBitstream, Error, TEXT("Cannot create NVENC bitstream buffer without a valid encoder handle."));
            return false;
        }

        using TNvEncCreateBitstreamBuffer = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_CREATE_BITSTREAM_BUFFER*);
        TNvEncCreateBitstreamBuffer CreateBitstream = Functions->nvEncCreateBitstreamBuffer;
        if (!ValidateFunction("NvEncCreateBitstreamBuffer", CreateBitstream))
        {
            return false;
        }

        NV_ENC_CREATE_BITSTREAM_BUFFER CreateParams = {};
        CreateParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_CREATE_BITSTREAM_BUFFER_VER, ApiVersion);
        CreateParams.memoryHeap = NV_ENC_MEMORY_HEAP_AUTOSELECT;
        CreateParams.size = InBufferSize;

        NVENCSTATUS Status = CreateBitstream(Encoder, &CreateParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCBitstream, Error, TEXT("NvEncCreateBitstreamBuffer failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        OutputBuffer = CreateParams.bitstreamBuffer;
        return true;
#endif
    }

    void FNVENCBitstream::Release()
    {
#if PLATFORM_WINDOWS
        if (!OutputBuffer || !Functions)
        {
            return;
        }

        using TNvEncDestroyBitstreamBuffer = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_OUTPUT_PTR);
        TNvEncDestroyBitstreamBuffer DestroyBitstream = Functions->nvEncDestroyBitstreamBuffer;
        if (DestroyBitstream)
        {
            NVENCSTATUS Status = DestroyBitstream(Encoder, OutputBuffer);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCBitstream, Warning, TEXT("NvEncDestroyBitstreamBuffer returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }
#endif
        OutputBuffer = nullptr;
        Functions = nullptr;
        Encoder = nullptr;
        bIsLocked = false;
        LockedParams = {};
        ApiVersion = NVENCAPI_VERSION;
    }

    bool FNVENCBitstream::Lock(void*& OutBitstreamBuffer, int32& OutSizeInBytes)
    {
#if !PLATFORM_WINDOWS
        OutBitstreamBuffer = nullptr;
        OutSizeInBytes = 0;
        return false;
#else
        if (bIsLocked)
        {
            UE_LOG(LogNVENCBitstream, Warning, TEXT("Bitstream already locked."));
            OutBitstreamBuffer = nullptr;
            OutSizeInBytes = 0;
            return false;
        }

        if (!OutputBuffer || !Functions)
        {
            UE_LOG(LogNVENCBitstream, Error, TEXT("Cannot lock NVENC bitstream – buffer has not been initialised."));
            OutBitstreamBuffer = nullptr;
            OutSizeInBytes = 0;
            return false;
        }

        using TNvEncLockBitstream = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_LOCK_BITSTREAM*);
        TNvEncLockBitstream LockBitstream = Functions->nvEncLockBitstream;
        if (!ValidateFunction("NvEncLockBitstream", LockBitstream))
        {
            OutBitstreamBuffer = nullptr;
            OutSizeInBytes = 0;
            return false;
        }

        LockedParams = {};
        LockedParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_LOCK_BITSTREAM_VER, ApiVersion);
        LockedParams.outputBitstream = OutputBuffer;
        LockedParams.doNotWait = 0;

        NVENCSTATUS Status = LockBitstream(Encoder, &LockedParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCBitstream, Error, TEXT("NvEncLockBitstream failed: %s"), *FNVENCDefs::StatusToString(Status));
            OutBitstreamBuffer = nullptr;
            OutSizeInBytes = 0;
            return false;
        }

        bIsLocked = true;
        OutBitstreamBuffer = LockedParams.bitstreamBufferPtr;
        OutSizeInBytes = static_cast<int32>(LockedParams.bitstreamSizeInBytes);
        return true;
#endif
    }

    void FNVENCBitstream::Unlock()
    {
#if PLATFORM_WINDOWS
        if (!bIsLocked || !Functions || !OutputBuffer)
        {
            return;
        }

        using TNvEncUnlockBitstream = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_OUTPUT_PTR);
        TNvEncUnlockBitstream UnlockBitstream = Functions->nvEncUnlockBitstream;
        if (UnlockBitstream)
        {
            NVENCSTATUS Status = UnlockBitstream(Encoder, OutputBuffer);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCBitstream, Warning, TEXT("NvEncUnlockBitstream returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }
#endif
        bIsLocked = false;
        LockedParams = {};
    }

    bool FNVENCBitstream::ExtractPacket(FNVENCEncodedPacket& OutPacket)
    {
#if !PLATFORM_WINDOWS
        OutPacket = FNVENCEncodedPacket();
        return false;
#else
        if (!bIsLocked)
        {
            UE_LOG(LogNVENCBitstream, Warning, TEXT("Attempted to extract NVENC packet without a locked bitstream."));
            OutPacket = FNVENCEncodedPacket();
            return false;
        }

        if (!LockedParams.bitstreamBufferPtr || LockedParams.bitstreamSizeInBytes == 0)
        {
            OutPacket = FNVENCEncodedPacket();
            return false;
        }

        OutPacket.Data.SetNumUninitialized(LockedParams.bitstreamSizeInBytes);
        FMemory::Memcpy(OutPacket.Data.GetData(), LockedParams.bitstreamBufferPtr, LockedParams.bitstreamSizeInBytes);
        OutPacket.bKeyFrame = LockedParams.pictureType == NV_ENC_PIC_TYPE_IDR || LockedParams.pictureType == NV_ENC_PIC_TYPE_I;
        OutPacket.Timestamp = LockedParams.outputTimeStamp;
        return true;
#endif
    }
}

#endif // WITH_OMNI_NVENC

