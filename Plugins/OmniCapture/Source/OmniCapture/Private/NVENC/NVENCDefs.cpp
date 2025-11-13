// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCDefs.h"

#if WITH_OMNI_NVENC
    #if PLATFORM_WINDOWS
        #include "Windows/AllowWindowsPlatformTypes.h"
    #endif
    #include "nvEncodeAPI.h"
    #if PLATFORM_WINDOWS
        #include "Windows/HideWindowsPlatformTypes.h"
    #endif

    #ifndef NV_ENC_PRESET_DEFAULT_GUID
        // Default preset GUID is still required for legacy runtimes even though it is no longer
        // emitted by newer headers.
        static const GUID NV_ENC_PRESET_DEFAULT_GUID =
        { 0x60e4c05a, 0x5333, 0x4e09, { 0x9a, 0xb5, 0x00, 0xa3, 0x1e, 0x99, 0x75, 0x6f } };
    #endif

    #ifndef NV_ENC_PRESET_LOW_LATENCY_HQ_GUID
        static const GUID NV_ENC_PRESET_LOW_LATENCY_HQ_GUID =
        { 0xb3d9dc6f, 0x9f9a, 0x4ff2, { 0xb2, 0xea, 0xef, 0x0c, 0xde, 0x24, 0x82, 0x5b } };
    #endif
#endif // WITH_OMNI_NVENC

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCDefs, Log, All);

namespace OmniNVENC
{
    namespace
    {
        const FGuid& GuidFromComponents(uint32 A, uint32 B, uint32 C, uint32 D)
        {
            static TMap<uint64, FGuid> Cache;
            const uint64 Key = (static_cast<uint64>(A) << 32) | D;
            if (FGuid* Existing = Cache.Find(Key))
            {
                return *Existing;
            }

            FGuid Guid(A, B, C, D);
            Cache.Add(Key, Guid);
            return Cache[Key];
        }

#if WITH_OMNI_NVENC
        const FGuid& GuidFromNative(const GUID& InGuid)
        {
            const uint32 B = (static_cast<uint32>(InGuid.Data2) << 16) | static_cast<uint32>(InGuid.Data3);
            const uint32 C = (static_cast<uint32>(InGuid.Data4[0]) << 24)
                | (static_cast<uint32>(InGuid.Data4[1]) << 16)
                | (static_cast<uint32>(InGuid.Data4[2]) << 8)
                | static_cast<uint32>(InGuid.Data4[3]);
            const uint32 D = (static_cast<uint32>(InGuid.Data4[4]) << 24)
                | (static_cast<uint32>(InGuid.Data4[5]) << 16)
                | (static_cast<uint32>(InGuid.Data4[6]) << 8)
                | static_cast<uint32>(InGuid.Data4[7]);

            return GuidFromComponents(InGuid.Data1, B, C, D);
        }
#endif // WITH_OMNI_NVENC
    }

    const FGuid& FNVENCDefs::CodecGuid(ENVENCCodec Codec)
    {
#if WITH_OMNI_NVENC
        switch (Codec)
        {
        case ENVENCCodec::HEVC:
            return GuidFromNative(NV_ENC_CODEC_HEVC_GUID);
        case ENVENCCodec::H264:
        default:
            return GuidFromNative(NV_ENC_CODEC_H264_GUID);
        }
#else
        switch (Codec)
        {
        case ENVENCCodec::HEVC:
            return GuidFromComponents(0x790CDC88, 0x45224D7B, 0x9425BDA9, 0x975F7603);
        case ENVENCCodec::H264:
        default:
            return GuidFromComponents(0x6BC82762, 0x4E634CA4, 0xAA851E50, 0xF321F6BF);
        }
#endif
    }

    const FGuid& FNVENCDefs::PresetDefaultGuid()
    {
#if WITH_OMNI_NVENC
        return GuidFromNative(NV_ENC_PRESET_DEFAULT_GUID);
#else
        return GuidFromComponents(0x60E4C05A, 0x53334E09, 0x9AB500A3, 0x1E99756F);
#endif
    }

    const FGuid& FNVENCDefs::PresetP1Guid()
    {
#if WITH_OMNI_NVENC
        return GuidFromNative(NV_ENC_PRESET_P1_GUID);
#else
        return GuidFromComponents(0xFC0A8D3E, 0x45F84CF8, 0x80C72988, 0x71590EBF);
#endif
    }

    const FGuid& FNVENCDefs::PresetP2Guid()
    {
#if WITH_OMNI_NVENC
        return GuidFromNative(NV_ENC_PRESET_P2_GUID);
#else
        return GuidFromComponents(0xF581CFB8, 0x88D64381, 0x93F0DF13, 0xF9C27DAB);
#endif
    }

    const FGuid& FNVENCDefs::PresetP3Guid()
    {
#if WITH_OMNI_NVENC
        return GuidFromNative(NV_ENC_PRESET_P3_GUID);
#else
        return GuidFromComponents(0x36850110, 0x3A07441F, 0x94D53670, 0x631F91F6);
#endif
    }

    const FGuid& FNVENCDefs::PresetP4Guid()
    {
#if WITH_OMNI_NVENC
        return GuidFromNative(NV_ENC_PRESET_P4_GUID);
#else
        return GuidFromComponents(0x90A7B826, 0xDF064862, 0xB9D2CD6D, 0x73A08681);
#endif
    }

    const FGuid& FNVENCDefs::PresetP5Guid()
    {
#if WITH_OMNI_NVENC
        return GuidFromNative(NV_ENC_PRESET_P5_GUID);
#else
        return GuidFromComponents(0x21C6E6B4, 0x297A4CBA, 0x998FB6CB, 0xDE72ADE3);
#endif
    }

    const FGuid& FNVENCDefs::PresetP6Guid()
    {
#if WITH_OMNI_NVENC
        return GuidFromNative(NV_ENC_PRESET_P6_GUID);
#else
        return GuidFromComponents(0x8E75C279, 0x62994AB6, 0x83020B21, 0x5A335CF5);
#endif
    }

    const FGuid& FNVENCDefs::PresetP7Guid()
    {
#if WITH_OMNI_NVENC
        return GuidFromNative(NV_ENC_PRESET_P7_GUID);
#else
        return GuidFromComponents(0x84848C12, 0x6F714C13, 0x931B53E2, 0x83F57974);
#endif
    }

    const FGuid& FNVENCDefs::PresetHighPerformanceApproxGuid()
    {
        // Approx: map HP → P1 for legacy compatibility
        return PresetP1Guid();
    }

    const FGuid& FNVENCDefs::PresetHighQualityApproxGuid()
    {
        // Approx: map HQ → P5 for legacy compatibility
        return PresetP5Guid();
    }

    const FGuid& FNVENCDefs::PresetLowLatencyHighQualityGuid()
    {
#if WITH_OMNI_NVENC
        return GuidFromNative(NV_ENC_PRESET_LOW_LATENCY_HQ_GUID);
#else
        return GuidFromComponents(0xB3D9DC6F, 0x9F9A4FF2, 0xB2EAEF0C, 0xDE24825B);
#endif
    }

    FString FNVENCDefs::PresetGuidToString(const FGuid& Guid)
    {
        if (Guid == PresetDefaultGuid())
        {
            return TEXT("NV_ENC_PRESET_DEFAULT");
        }
        if (Guid == PresetP1Guid())
        {
            return TEXT("NV_ENC_PRESET_P1");
        }
        if (Guid == PresetP2Guid())
        {
            return TEXT("NV_ENC_PRESET_P2");
        }
        if (Guid == PresetP3Guid())
        {
            return TEXT("NV_ENC_PRESET_P3");
        }
        if (Guid == PresetP4Guid())
        {
            return TEXT("NV_ENC_PRESET_P4");
        }
        if (Guid == PresetP5Guid())
        {
            return TEXT("NV_ENC_PRESET_P5");
        }
        if (Guid == PresetP6Guid())
        {
            return TEXT("NV_ENC_PRESET_P6");
        }
        if (Guid == PresetP7Guid())
        {
            return TEXT("NV_ENC_PRESET_P7");
        }
        if (Guid == PresetLowLatencyHighQualityGuid())
        {
            return TEXT("NV_ENC_PRESET_LOW_LATENCY_HQ");
        }
        return Guid.ToString();
    }

    const FGuid& FNVENCDefs::TuningLatencyGuid()
    {
#if WITH_OMNI_NVENC
        static const GUID LowLatencyTuningGuid =
        { 0xd7363f6f, 0x84f0, 0x4176, { 0xa0, 0xe0, 0x0d, 0xa5, 0x46, 0x46, 0x0b, 0x7d } };
        return GuidFromNative(LowLatencyTuningGuid);
#else
        return GuidFromComponents(0xD7363F6F, 0x84F04176, 0xA0E00DA5, 0x46460B7D);
#endif
    }

    const FGuid& FNVENCDefs::TuningQualityGuid()
    {
#if WITH_OMNI_NVENC
        static const GUID HighQualityTuningGuid =
        { 0x1d69c67f, 0x0f3c, 0x4f25, { 0x9f, 0xa4, 0xdf, 0x7b, 0xfb, 0xb0, 0x2e, 0x59 } };
        return GuidFromNative(HighQualityTuningGuid);
#else
        return GuidFromComponents(0x1D69C67F, 0x0F3C4F25, 0x9FA4DF7B, 0xFBB02E59);
#endif
    }

    FString FNVENCDefs::BufferFormatToString(ENVENCBufferFormat Format)
    {
        switch (Format)
        {
        case ENVENCBufferFormat::P010:
            return TEXT("P010");
        case ENVENCBufferFormat::BGRA:
            return TEXT("BGRA");
        case ENVENCBufferFormat::NV12:
        default:
            return TEXT("NV12");
        }
    }

    FString FNVENCDefs::CodecToString(ENVENCCodec Codec)
    {
        switch (Codec)
        {
        case ENVENCCodec::HEVC:
            return TEXT("HEVC");
        case ENVENCCodec::H264:
        default:
            return TEXT("H.264");
        }
    }

    FString FNVENCDefs::StatusToString(int32 StatusCode)
    {
        switch (StatusCode)
        {
        case 0:
            return TEXT("NV_ENC_SUCCESS");
        case 1:
            return TEXT("NV_ENC_ERR_NO_ENCODE_DEVICE");
        case 2:
            return TEXT("NV_ENC_ERR_UNSUPPORTED_DEVICE");
        case 3:
            return TEXT("NV_ENC_ERR_INVALID_ENCODERDEVICE");
        case 4:
            return TEXT("NV_ENC_ERR_INVALID_DEVICE");
        case 5:
            return TEXT("NV_ENC_ERR_DEVICE_NOT_EXIST");
        case 6:
            return TEXT("NV_ENC_ERR_INVALID_PTR");
        case 7:
            return TEXT("NV_ENC_ERR_INVALID_EVENT");
        case 8:
            return TEXT("NV_ENC_ERR_INVALID_PARAM");
        case 9:
            return TEXT("NV_ENC_ERR_INVALID_CALL");
        case 10:
            return TEXT("NV_ENC_ERR_OUT_OF_MEMORY");
        case 11:
            return TEXT("NV_ENC_ERR_ENCODER_NOT_INITIALIZED");
        case 12:
            return TEXT("NV_ENC_ERR_UNSUPPORTED_PARAM");
        case 13:
            return TEXT("NV_ENC_ERR_LOCK_BUSY");
        case 14:
            return TEXT("NV_ENC_ERR_NOT_ENOUGH_BUFFER");
        case 0x18:
            return TEXT("NV_ENC_ERR_NEED_MORE_INPUT");
        default:
            return FString::Printf(TEXT("NVENC_STATUS_%d"), StatusCode);
        }
    }

    FNVENCAPIVersion FNVENCDefs::GetMinimumAPIVersion()
    {
        FNVENCAPIVersion Version;
        Version.Major = 1u;
        Version.Minor = 0u;
        return Version;
    }

    uint32 FNVENCDefs::EncodeApiVersion(const FNVENCAPIVersion& Version)
    {
        return (Version.Major & 0xFFu) | ((Version.Minor & 0xFFu) << 24);
    }

    FNVENCAPIVersion FNVENCDefs::DecodeApiVersion(uint32 EncodedVersion)
    {
        FNVENCAPIVersion Version;
        Version.Major = EncodedVersion & 0xFFu;
        Version.Minor = (EncodedVersion >> 24) & 0xFFu;
        return Version;
    }

    FNVENCAPIVersion FNVENCDefs::DecodeRuntimeVersion(uint32 RuntimeVersion)
    {
        if (RuntimeVersion == 0u)
        {
            return FNVENCAPIVersion();
        }

        if (RuntimeVersion > 0x0FFFu)
        {
            return DecodeApiVersion(RuntimeVersion);
        }

        FNVENCAPIVersion Version;
        Version.Major = (RuntimeVersion >> 4) & 0x0FFFu;
        Version.Minor = RuntimeVersion & 0x0Fu;
        return Version;
    }

    FString FNVENCDefs::VersionToString(const FNVENCAPIVersion& Version)
    {
        return FString::Printf(TEXT("%u.%u"), Version.Major, Version.Minor);
    }

    bool FNVENCDefs::IsVersionOlder(const FNVENCAPIVersion& Lhs, const FNVENCAPIVersion& Rhs)
    {
        if (Lhs.Major != Rhs.Major)
        {
            return Lhs.Major < Rhs.Major;
        }
        return Lhs.Minor < Rhs.Minor;
    }

    uint32 FNVENCDefs::PatchStructVersion(uint32 StructVersion, uint32 ApiVersion)
    {
        const uint32 Flags = StructVersion & 0xF0000000u;
        const uint32 StructId = (StructVersion >> 16) & 0x0FFFu;
        return (ApiVersion & 0x0FFFFFFFu) | (StructId << 16) | Flags;
    }
}

