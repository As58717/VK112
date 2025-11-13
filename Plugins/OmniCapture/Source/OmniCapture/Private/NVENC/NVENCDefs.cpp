// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCDefs.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCDefs, Log, All);

namespace OmniNVENC
{
    namespace
    {
        // These GUID values mirror the ones defined in the NVIDIA headers.  Having the
        // constants locally allows the trimmed down project to compile without pulling
        // in the proprietary SDK while still providing deterministic values for higher
        // level logic and unit tests.
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
    }

    const FGuid& FNVENCDefs::CodecGuid(ENVENCCodec Codec)
    {
        switch (Codec)
        {
        case ENVENCCodec::HEVC:
            // NV_ENC_CODEC_HEVC_GUID {0x790CDC65,0x7C5D,0x4FDE,{0x80,0x02,0x71,0xA5,0x15,0xC8,0x1A,0x6F}}
            return GuidFromComponents(0x790CDC65, 0x7C5D4FDE, 0x800271A5, 0x15C81A6F);
        case ENVENCCodec::H264:
        default:
            // NV_ENC_CODEC_H264_GUID {0x6BC82762,0x4E63,0x11D3,{0x9C,0xC1,0x00,0x80,0xC7,0xB3,0x12,0x97}}
            return GuidFromComponents(0x6BC82762, 0x4E6311D3, 0x9CC10080, 0xC7B31297);
        }
    }

    const FGuid& FNVENCDefs::PresetDefaultGuid()
    {
        // NV_ENC_PRESET_DEFAULT_GUID {0x60E4C05A,0x5333,0x4E09,{0x9A,0xB5,0x00,0xA3,0x1E,0x99,0x75,0x6F}}
        return GuidFromComponents(0x60E4C05A, 0x53334E09, 0x9AB500A3, 0x1E99756F);
    }

    const FGuid& FNVENCDefs::PresetP1Guid()
    {
        // NV_ENC_PRESET_P1_GUID {0xFC0A8D3E,0x45F8,0x4CF8,{0x80,0xC7,0x29,0x88,0x71,0x59,0x0E,0xBF}}
        return GuidFromComponents(0xFC0A8D3E, 0x45F84CF8, 0x80C72988, 0x71590EBF);
    }

    const FGuid& FNVENCDefs::PresetP2Guid()
    {
        // NV_ENC_PRESET_P2_GUID {0xF581CFB8,0x88D6,0x4381,{0x93,0xF0,0xDF,0x13,0xF9,0xC2,0x7D,0xAB}}
        return GuidFromComponents(0xF581CFB8, 0x88D64381, 0x93F0DF13, 0xF9C27DAB);
    }

    const FGuid& FNVENCDefs::PresetP3Guid()
    {
        // NV_ENC_PRESET_P3_GUID {0x36850110,0x3A07,0x441F,{0x94,0xD5,0x36,0x70,0x63,0x1F,0x91,0xF6}}
        return GuidFromComponents(0x36850110, 0x3A07441F, 0x94D53670, 0x631F91F6);
    }

    const FGuid& FNVENCDefs::PresetP4Guid()
    {
        // NV_ENC_PRESET_P4_GUID {0x90A7B826,0xDF06,0x4862,{0xB9,0xD2,0xCD,0x6D,0x73,0xA0,0x86,0x81}}
        return GuidFromComponents(0x90A7B826, 0xDF064862, 0xB9D2CD6D, 0x73A08681);
    }

    const FGuid& FNVENCDefs::PresetP5Guid()
    {
        // NV_ENC_PRESET_P5_GUID {0x21C6E6B4,0x297A,0x4CBA,{0x99,0x8F,0xB6,0xCB,0xDE,0x72,0xAD,0xE3}}
        return GuidFromComponents(0x21C6E6B4, 0x297A4CBA, 0x998FB6CB, 0xDE72ADE3);
    }

    const FGuid& FNVENCDefs::PresetP6Guid()
    {
        // NV_ENC_PRESET_P6_GUID {0x8E75C279,0x6299,0x4AB6,{0x83,0x02,0x0B,0x21,0x5A,0x33,0x5C,0xF5}}
        return GuidFromComponents(0x8E75C279, 0x62994AB6, 0x83020B21, 0x5A335CF5);
    }

    const FGuid& FNVENCDefs::PresetP7Guid()
    {
        // NV_ENC_PRESET_P7_GUID {0x84848C12,0x6F71,0x4C13,{0x93,0x1B,0x53,0xE2,0x83,0xF5,0x79,0x74}}
        return GuidFromComponents(0x84848C12, 0x6F714C13, 0x931B53E2, 0x83F57974);
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
        // NV_ENC_PRESET_LOW_LATENCY_HQ_GUID {0xB3D9DC6F,0x9F9A,0x4FF2,{0xB2,0xEA,0xEF,0x0C,0xDE,0x24,0x82,0x5B}}
        return GuidFromComponents(0xB3D9DC6F, 0x9F9A4FF2, 0xB2EAEF0C, 0xDE24825B);
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
        // NV_ENC_TUNING_INFO_LOW_LATENCY {0xD7363F6F,0x84F0,0x4176,{0xA0,0xE0,0x0D,0xA5,0x46,0x46,0x0B,0x7D}}
        return GuidFromComponents(0xD7363F6F, 0x84F04176, 0xA0E00DA5, 0x46460B7D);
    }

    const FGuid& FNVENCDefs::TuningQualityGuid()
    {
        // NV_ENC_TUNING_INFO_HIGH_QUALITY {0x1D69C67F,0x0F3C,0x4F25,{0x9F,0xA4,0xDF,0x7B,0xFB,0xB0,0x2E,0x59}}
        return GuidFromComponents(0x1D69C67F, 0x0F3C4F25, 0x9FA4DF7B, 0xFBB02E59);
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

