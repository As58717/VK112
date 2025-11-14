// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCSession.h"

#if WITH_OMNI_NVENC

#include "NVENC/NVENCCommon.h"
#include "NVENC/NVENCDefs.h"
#include "NVENC/NVENCParameters.h"
#include "NVENC/NVEncodeAPILoader.h"
#include "Logging/LogMacros.h"
#include "HAL/PlatformProcess.h"

#include "Misc/ScopeExit.h"
#include "Math/UnrealMathUtility.h"

#ifndef UE_NVENC_HAS_FLUSH_FUNCTION
#if defined(NVENCAPI_MAJOR_VERSION) && NVENCAPI_MAJOR_VERSION < 12
#define UE_NVENC_HAS_FLUSH_FUNCTION 1
#else
#define UE_NVENC_HAS_FLUSH_FUNCTION 0
#endif
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNVENCSession, Log, All);

namespace OmniNVENC
{
    namespace
    {
#if PLATFORM_WINDOWS
        constexpr NV_ENC_DEVICE_TYPE GetDirectX11DeviceType()
        {
#if defined(NV_ENC_DEVICE_TYPE_DIRECTX11)
            return NV_ENC_DEVICE_TYPE_DIRECTX11;
#else
            return static_cast<NV_ENC_DEVICE_TYPE>(0x3);
#endif
        }

        FString DeviceTypeToString(NV_ENC_DEVICE_TYPE Type)
        {
            switch (Type)
            {
            case NV_ENC_DEVICE_TYPE_DIRECTX:
                return TEXT("DirectX");
            case NV_ENC_DEVICE_TYPE_CUDA:
                return TEXT("CUDA");
            case NV_ENC_DEVICE_TYPE_OPENGL:
                return TEXT("OpenGL");
            default:
                if (Type == GetDirectX11DeviceType())
                {
                    return TEXT("DirectX11");
                }
                return FString::Printf(TEXT("0x%x"), static_cast<uint32>(Type));
            }
        }

        GUID ToWindowsGuid(const FGuid& InGuid)
        {
            GUID Guid;
            Guid.Data1 = static_cast<uint32>(InGuid.A);
            Guid.Data2 = static_cast<uint16>((static_cast<uint32>(InGuid.B) >> 16) & 0xFFFF);
            Guid.Data3 = static_cast<uint16>(static_cast<uint32>(InGuid.B) & 0xFFFF);

            const uint32 C = static_cast<uint32>(InGuid.C);
            const uint32 D = static_cast<uint32>(InGuid.D);

            Guid.Data4[0] = static_cast<uint8>((C >> 24) & 0xFF);
            Guid.Data4[1] = static_cast<uint8>((C >> 16) & 0xFF);
            Guid.Data4[2] = static_cast<uint8>((C >> 8) & 0xFF);
            Guid.Data4[3] = static_cast<uint8>(C & 0xFF);
            Guid.Data4[4] = static_cast<uint8>((D >> 24) & 0xFF);
            Guid.Data4[5] = static_cast<uint8>((D >> 16) & 0xFF);
            Guid.Data4[6] = static_cast<uint8>((D >> 8) & 0xFF);
            Guid.Data4[7] = static_cast<uint8>(D & 0xFF);
            return Guid;
        }

        FGuid FromWindowsGuid(const GUID& InGuid)
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
            return FGuid(InGuid.Data1, B, C, D);
        }

        FString GuidToDebugString(const GUID& InGuid)
        {
            return FromWindowsGuid(InGuid).ToString(EGuidFormats::DigitsWithHyphensInBraces);
        }

        bool PresetEnumToGuid(ENVENCPreset Preset, GUID& OutGuid)
        {
            switch (Preset)
            {
            case ENVENCPreset::Default:
                OutGuid = ToWindowsGuid(FNVENCDefs::PresetDefaultGuid());
                return true;
            case ENVENCPreset::LowLatencyHighQuality:
                OutGuid = ToWindowsGuid(FNVENCDefs::PresetLowLatencyHighQualityGuid());
                return true;
            case ENVENCPreset::P1:
                OutGuid = ToWindowsGuid(FNVENCDefs::PresetP1Guid());
                return true;
            case ENVENCPreset::P2:
                OutGuid = ToWindowsGuid(FNVENCDefs::PresetP2Guid());
                return true;
            case ENVENCPreset::P3:
                OutGuid = ToWindowsGuid(FNVENCDefs::PresetP3Guid());
                return true;
            case ENVENCPreset::P4:
                OutGuid = ToWindowsGuid(FNVENCDefs::PresetP4Guid());
                return true;
            case ENVENCPreset::P5:
                OutGuid = ToWindowsGuid(FNVENCDefs::PresetP5Guid());
                return true;
            case ENVENCPreset::P6:
                OutGuid = ToWindowsGuid(FNVENCDefs::PresetP6Guid());
                return true;
            case ENVENCPreset::P7:
                OutGuid = ToWindowsGuid(FNVENCDefs::PresetP7Guid());
                return true;
            default:
                return false;
            }
        }

        NV_ENC_TUNING_INFO ToNVTuning(ENVENCTuningMode Mode)
        {
            switch (Mode)
            {
            case ENVENCTuningMode::HighQuality: return NV_ENC_TUNING_INFO_HIGH_QUALITY;
            case ENVENCTuningMode::LowLatency: return NV_ENC_TUNING_INFO_LOW_LATENCY;
            case ENVENCTuningMode::UltraLowLatency: return NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
            case ENVENCTuningMode::Lossless: return NV_ENC_TUNING_INFO_LOSSLESS;
            default: return NV_ENC_TUNING_INFO_UNDEFINED;
            }
        }

        ENVENCTuningMode FromNVTuning(NV_ENC_TUNING_INFO Mode)
        {
            switch (Mode)
            {
            case NV_ENC_TUNING_INFO_HIGH_QUALITY: return ENVENCTuningMode::HighQuality;
            case NV_ENC_TUNING_INFO_LOW_LATENCY: return ENVENCTuningMode::LowLatency;
            case NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY: return ENVENCTuningMode::UltraLowLatency;
            case NV_ENC_TUNING_INFO_LOSSLESS: return ENVENCTuningMode::Lossless;
            default: return ENVENCTuningMode::Automatic;
            }
        }

        FString ProfileGuidToString(const GUID& InGuid)
        {
            if (FMemory::Memcmp(&InGuid, &NV_ENC_H264_PROFILE_BASELINE_GUID, sizeof(GUID)) == 0)
            {
                return TEXT("NV_ENC_H264_PROFILE_BASELINE");
            }
            if (FMemory::Memcmp(&InGuid, &NV_ENC_H264_PROFILE_MAIN_GUID, sizeof(GUID)) == 0)
            {
                return TEXT("NV_ENC_H264_PROFILE_MAIN");
            }
            if (FMemory::Memcmp(&InGuid, &NV_ENC_H264_PROFILE_HIGH_GUID, sizeof(GUID)) == 0)
            {
                return TEXT("NV_ENC_H264_PROFILE_HIGH");
            }
            if (FMemory::Memcmp(&InGuid, &NV_ENC_H264_PROFILE_HIGH_444_GUID, sizeof(GUID)) == 0)
            {
                return TEXT("NV_ENC_H264_PROFILE_HIGH_444");
            }
            if (FMemory::Memcmp(&InGuid, &NV_ENC_HEVC_PROFILE_MAIN_GUID, sizeof(GUID)) == 0)
            {
                return TEXT("NV_ENC_HEVC_PROFILE_MAIN");
            }
            if (FMemory::Memcmp(&InGuid, &NV_ENC_HEVC_PROFILE_MAIN10_GUID, sizeof(GUID)) == 0)
            {
                return TEXT("NV_ENC_HEVC_PROFILE_MAIN10");
            }
            if (FMemory::Memcmp(&InGuid, &NV_ENC_HEVC_PROFILE_FREXT_GUID, sizeof(GUID)) == 0)
            {
                return TEXT("NV_ENC_HEVC_PROFILE_FREXT");
            }
            return GuidToDebugString(InGuid);
        }

        FString LevelToString(uint32 Level)
        {
            if (Level == NV_ENC_LEVEL_AUTOSELECT)
            {
                return TEXT("NV_ENC_LEVEL_AUTOSELECT");
            }

            return FString::Printf(TEXT("0x%02x"), Level);
        }

        NV_ENC_BUFFER_FORMAT ToNVFormat(ENVENCBufferFormat Format)
        {
            switch (Format)
            {
            case ENVENCBufferFormat::P010:
                return NV_ENC_BUFFER_FORMAT_YUV420_10BIT;
            case ENVENCBufferFormat::BGRA:
                return NV_ENC_BUFFER_FORMAT_ARGB;
            case ENVENCBufferFormat::NV12:
            default:
                return NV_ENC_BUFFER_FORMAT_NV12;
            }
        }

        NV_ENC_BIT_DEPTH ToNVBitDepth(NV_ENC_BUFFER_FORMAT Format)
        {
            switch (Format)
            {
            case NV_ENC_BUFFER_FORMAT_YUV420_10BIT:
#if defined(NV_ENC_BUFFER_FORMAT_YUV444_10BIT)
            case NV_ENC_BUFFER_FORMAT_YUV444_10BIT:
#endif
                return NV_ENC_BIT_DEPTH_10;
            default:
                return NV_ENC_BIT_DEPTH_8;
            }
        }

        uint32 GetChromaFormatIDC(NV_ENC_BUFFER_FORMAT Format)
        {
            switch (Format)
            {
            case NV_ENC_BUFFER_FORMAT_ARGB:
#if defined(NV_ENC_BUFFER_FORMAT_ABGR)
            case NV_ENC_BUFFER_FORMAT_ABGR:
#endif
#if defined(NV_ENC_BUFFER_FORMAT_ARGB10)
            case NV_ENC_BUFFER_FORMAT_ARGB10:
#endif
                return 3u;
            default:
                return 1u;
            }
        }

        NV_ENC_PARAMS_RC_MODE ToNVRateControl(ENVENCRateControlMode Mode)
        {
            switch (Mode)
            {
            case ENVENCRateControlMode::CONSTQP:
                return NV_ENC_PARAMS_RC_CONSTQP;
            case ENVENCRateControlMode::VBR:
                return NV_ENC_PARAMS_RC_VBR;
            case ENVENCRateControlMode::CBR:
            default:
                return NV_ENC_PARAMS_RC_CBR;
            }
        }

        NV_ENC_MULTI_PASS ToNVMultiPass(ENVENCMultipassMode Mode)
        {
            switch (Mode)
            {
            case ENVENCMultipassMode::QUARTER:
                return NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
            case ENVENCMultipassMode::FULL:
                return NV_ENC_TWO_PASS_FULL_RESOLUTION;
            case ENVENCMultipassMode::DISABLED:
            default:
                return NV_ENC_MULTI_PASS_DISABLED;
            }
        }

        template <typename TFunc>
        bool ValidateFunction(const ANSICHAR* Name, TFunc* Function)
        {
            if (!Function)
            {
                UE_LOG(LogNVENCSession, Error, TEXT("Required NVENC export '%s' is missing."), ANSI_TO_TCHAR(Name));
                return false;
            }
            return true;
        }
#endif
    }

void FNVENCSession::SetLogContext(const FString& InContext)
    {
        if (InContext.IsEmpty())
        {
            LogContext = TEXT("NVENC session");
        }
        else
        {
            LogContext = InContext;
        }
    }

const TCHAR* FNVENCSession::GetLogContextLabel() const
    {
        return LogContext.IsEmpty() ? TEXT("NVENC session") : *LogContext;
    }

bool FNVENCSession::Open(ENVENCCodec Codec, void* InDevice, NV_ENC_DEVICE_TYPE InDeviceType)
    {
        LastErrorMessage.Reset();
#if !PLATFORM_WINDOWS
        UE_LOG(LogNVENCSession, Warning, TEXT("NVENC session is only available on Windows builds."));
        LastErrorMessage = TEXT("NVENC session is only available on Windows builds.");
        return false;
#else
        if (bIsOpen)
        {
            return true;
        }

        if (!InDevice)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("Failed to open NVENC session – no encoder device was provided."));
            LastErrorMessage = TEXT("Failed to open NVENC session – no encoder device was provided.");
            return false;
        }

        FNVEncodeAPILoader& Loader = FNVEncodeAPILoader::Get();
        if (!Loader.Load())
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Failed to open NVENC session for codec %s – runtime is unavailable."), *FNVENCDefs::CodecToString(Codec));
            LastErrorMessage = TEXT("Failed to open NVENC session – NVENC runtime is unavailable.");
            return false;
        }

        ApiVersion = NVENCAPI_VERSION;
        const uint32 CompileTimeApiVersion = ApiVersion;
        FNVENCAPIVersion NegotiatedVersion = FNVENCDefs::DecodeApiVersion(ApiVersion);

        using TNvEncodeAPIGetMaxSupportedVersion = NVENCSTATUS(NVENCAPI*)(uint32_t*);

        void* const NvencHandle = FNVENCCommon::GetHandle();
        if (NvencHandle)
        {
            void* const MaxVersionExport = FPlatformProcess::GetDllExport(NvencHandle, TEXT("NvEncodeAPIGetMaxSupportedVersion"));
            if (MaxVersionExport)
            {
                TNvEncodeAPIGetMaxSupportedVersion GetMaxSupportedVersion = reinterpret_cast<TNvEncodeAPIGetMaxSupportedVersion>(MaxVersionExport);
                uint32 RuntimeApiVersionRaw = 0;
                const NVENCSTATUS VersionStatus = GetMaxSupportedVersion ? GetMaxSupportedVersion(&RuntimeApiVersionRaw) : NV_ENC_ERR_INVALID_PTR;
                if (VersionStatus == NV_ENC_SUCCESS && RuntimeApiVersionRaw != 0)
                {
                    const FNVENCAPIVersion RuntimeVersion = FNVENCDefs::DecodeRuntimeVersion(RuntimeApiVersionRaw);
                    if (RuntimeVersion.Major != 0 || RuntimeVersion.Minor != 0)
                    {
                        const uint32 RuntimeApiVersion = FNVENCDefs::EncodeApiVersion(RuntimeVersion);
                        if (FNVENCDefs::IsVersionOlder(RuntimeVersion, NegotiatedVersion))
                        {
                            UE_LOG(LogNVENCSession, Log,
                                TEXT("NVENC runtime API version %s (0x%08x) is lower than compile-time version %s (0x%08x). Downgrading."),
                                *FNVENCDefs::VersionToString(RuntimeVersion), RuntimeApiVersion,
                                *FNVENCDefs::VersionToString(NegotiatedVersion), CompileTimeApiVersion);
                            NegotiatedVersion = RuntimeVersion;
                            ApiVersion = RuntimeApiVersion;
                            UE_LOG(LogNVENCSession, Display, TEXT("\u2192 Adjusted apiVersion to runtime version: 0x%08x"), ApiVersion);
                        }
                        else if (FNVENCDefs::IsVersionOlder(NegotiatedVersion, RuntimeVersion))
                        {
                            UE_LOG(LogNVENCSession, Verbose,
                                TEXT("NVENC runtime reports newer API version %s (0x%08x); using compile-time version %s (0x%08x)."),
                                *FNVENCDefs::VersionToString(RuntimeVersion), RuntimeApiVersion,
                                *FNVENCDefs::VersionToString(NegotiatedVersion), CompileTimeApiVersion);
                        }
                    }
                }
                else if (VersionStatus != NV_ENC_SUCCESS)
                {
                    UE_LOG(LogNVENCSession, Verbose, TEXT("NvEncodeAPIGetMaxSupportedVersion failed: %s"), *FNVENCDefs::StatusToString(VersionStatus));
                }
            }
            else
            {
                UE_LOG(LogNVENCSession, Verbose, TEXT("NVENC runtime does not export NvEncodeAPIGetMaxSupportedVersion."));
            }
        }

        const FNVENCAPIVersion MinimumSupportedVersion = FNVENCDefs::GetMinimumAPIVersion();
        if (FNVENCDefs::IsVersionOlder(NegotiatedVersion, MinimumSupportedVersion))
        {
            UE_LOG(LogNVENCSession, Error,
                TEXT("NVENC runtime API version %s (0x%08x) is below the minimum supported version %s (0x%08x)."),
                *FNVENCDefs::VersionToString(NegotiatedVersion), FNVENCDefs::EncodeApiVersion(NegotiatedVersion),
                *FNVENCDefs::VersionToString(MinimumSupportedVersion), FNVENCDefs::EncodeApiVersion(MinimumSupportedVersion));
            LastErrorMessage = TEXT("NVENC runtime API version is below the minimum supported version.");
            return false;
        }

        using TNvEncodeAPICreateInstance = NVENCSTATUS(NVENCAPI*)(NV_ENCODE_API_FUNCTION_LIST*);

        TNvEncodeAPICreateInstance CreateInstance = reinterpret_cast<TNvEncodeAPICreateInstance>(Loader.GetFunctions().NvEncodeAPICreateInstance);

        if (!ValidateFunction("NvEncodeAPICreateInstance", CreateInstance))
        {
            LastErrorMessage = TEXT("Required NVENC export 'NvEncodeAPICreateInstance' is missing.");
            return false;
        }

        FMemory::Memzero(FunctionList);
        FunctionList.version = FNVENCDefs::PatchStructVersion(NV_ENCODE_API_FUNCTION_LIST_VER, ApiVersion);

        NVENCSTATUS Status = CreateInstance(&FunctionList);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NvEncodeAPICreateInstance failed: %s"), *FNVENCDefs::StatusToString(Status));
            LastErrorMessage = FString::Printf(TEXT("NvEncodeAPICreateInstance failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        using TNvEncOpenEncodeSessionEx = NVENCSTATUS(NVENCAPI*)(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS*, void**);
        TNvEncOpenEncodeSessionEx OpenSession = reinterpret_cast<TNvEncOpenEncodeSessionEx>(FunctionList.nvEncOpenEncodeSessionEx);

        if (!ValidateFunction("NvEncOpenEncodeSessionEx", OpenSession))
        {
            LastErrorMessage = TEXT("Required NVENC export 'NvEncOpenEncodeSessionEx' is missing.");
            return false;
        }

        NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS OpenParams = {};
        OpenParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER, ApiVersion);
        OpenParams.apiVersion = ApiVersion;
        OpenParams.device = InDevice;

        NV_ENC_DEVICE_TYPE SelectedDeviceType = InDeviceType;
        auto TryAddCandidate = [](NV_ENC_DEVICE_TYPE* Candidates, int32& Count, NV_ENC_DEVICE_TYPE Candidate)
        {
            for (int32 Index = 0; Index < Count; ++Index)
            {
                if (Candidates[Index] == Candidate)
                {
                    return;
                }
            }

            if (Count < 4)
            {
                Candidates[Count++] = Candidate;
            }
        };

        NV_ENC_DEVICE_TYPE CandidateTypes[4] = {};
        int32 CandidateCount = 0;
#if PLATFORM_WINDOWS
        if (InDeviceType == NV_ENC_DEVICE_TYPE_DIRECTX || InDeviceType == GetDirectX11DeviceType())
        {
            TryAddCandidate(CandidateTypes, CandidateCount, GetDirectX11DeviceType());
        }
#endif
        TryAddCandidate(CandidateTypes, CandidateCount, InDeviceType);
#if PLATFORM_WINDOWS
        if (InDeviceType == GetDirectX11DeviceType())
        {
            TryAddCandidate(CandidateTypes, CandidateCount, NV_ENC_DEVICE_TYPE_DIRECTX);
        }
        else if (InDeviceType == NV_ENC_DEVICE_TYPE_DIRECTX)
        {
            TryAddCandidate(CandidateTypes, CandidateCount, NV_ENC_DEVICE_TYPE_DIRECTX);
        }
#endif

        Status = NV_ENC_ERR_INVALID_PARAM;
        Encoder = nullptr;

        for (int32 CandidateIndex = 0; CandidateIndex < CandidateCount; ++CandidateIndex)
        {
            const NV_ENC_DEVICE_TYPE Candidate = CandidateTypes[CandidateIndex];
            OpenParams.deviceType = Candidate;

            void* CandidateEncoder = nullptr;
            Status = OpenSession(&OpenParams, &CandidateEncoder);
            if (Status == NV_ENC_SUCCESS)
            {
                Encoder = CandidateEncoder;
                SelectedDeviceType = Candidate;
                if (CandidateIndex > 0)
                {
#if PLATFORM_WINDOWS
                    UE_LOG(LogNVENCSession, Display, TEXT("NvEncOpenEncodeSessionEx succeeded after retrying with device type %s."),
                        *DeviceTypeToString(Candidate));
#else
                    UE_LOG(LogNVENCSession, Display, TEXT("NvEncOpenEncodeSessionEx succeeded after retry."));
#endif
                }
                break;
            }

#if PLATFORM_WINDOWS
            UE_LOG(LogNVENCSession, Verbose, TEXT("NvEncOpenEncodeSessionEx failed with device type %s: %s"),
                *DeviceTypeToString(Candidate), *FNVENCDefs::StatusToString(Status));
#else
            UE_LOG(LogNVENCSession, Verbose, TEXT("NvEncOpenEncodeSessionEx failed (status=%s)."), *FNVENCDefs::StatusToString(Status));
#endif
        }

        if (!Encoder)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NvEncOpenEncodeSessionEx failed: %s"), *FNVENCDefs::StatusToString(Status));
            LastErrorMessage = FString::Printf(TEXT("NvEncOpenEncodeSessionEx failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        Device = InDevice;
        DeviceType = SelectedDeviceType;
        CurrentParameters.Codec = Codec;
        bIsOpen = true;
        LastErrorMessage.Reset();

        if (!ValidatePresetConfiguration(Codec, /*bAllowNullFallback=*/false))
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NVENC session preset validation failed immediately after opening. Closing session."));
            Destroy();
            return false;
        }
        return true;
#endif
    }

    bool FNVENCSession::ValidatePresetConfiguration(ENVENCCodec Codec, bool bAllowNullFallback)
    {
        LastErrorMessage.Reset();
#if !PLATFORM_WINDOWS
        UE_LOG(LogNVENCSession, Warning, TEXT("Cannot validate NVENC preset configuration on this platform."));
        LastErrorMessage = TEXT("Cannot validate NVENC preset configuration on this platform.");
        return false;
#else
        if (!bIsOpen || !Encoder)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Cannot validate NVENC preset configuration – encoder is not open."));
            LastErrorMessage = TEXT("Cannot validate NVENC preset configuration – encoder is not open.");
            return false;
        }

        using TNvEncGetEncodePresetConfig = NVENCSTATUS(NVENCAPI*)(void*, GUID, GUID, NV_ENC_PRESET_CONFIG*);
        using TNvEncGetEncodePresetConfigEx = NVENCSTATUS(NVENCAPI*)(void*, GUID, GUID, NV_ENC_TUNING_INFO, NV_ENC_PRESET_CONFIG*);

        TNvEncGetEncodePresetConfig GetPresetConfig = FunctionList.nvEncGetEncodePresetConfig;
        TNvEncGetEncodePresetConfigEx GetPresetConfigEx = reinterpret_cast<TNvEncGetEncodePresetConfigEx>(FunctionList.nvEncGetEncodePresetConfigEx);

        if (!ValidateFunction("NvEncGetEncodePresetConfig", GetPresetConfig))
        {
            LastErrorMessage = TEXT("Required NVENC export 'NvEncGetEncodePresetConfig' is missing.");
            return false;
        }

        const GUID CodecGuid = ToWindowsGuid(FNVENCDefs::CodecGuid(Codec));

        struct FValidationPreset
        {
            GUID Guid;
            NV_ENC_TUNING_INFO PreferredTuning = NV_ENC_TUNING_INFO_UNDEFINED;
            const TCHAR* FriendlyName = TEXT("");
        };

        TArray<FValidationPreset, TInlineAllocator<8>> ValidationPresets;
        auto AddValidationPreset = [&ValidationPresets](const GUID& InGuid, NV_ENC_TUNING_INFO PreferredTuning, const TCHAR* FriendlyName)
        {
            ValidationPresets.Add({ InGuid, PreferredTuning, FriendlyName });
        };

        AddValidationPreset(ToWindowsGuid(FNVENCDefs::PresetDefaultGuid()), NV_ENC_TUNING_INFO_UNDEFINED, TEXT("NV_ENC_PRESET_DEFAULT"));
        AddValidationPreset(ToWindowsGuid(FNVENCDefs::PresetLowLatencyHighQualityGuid()), NV_ENC_TUNING_INFO_LOW_LATENCY, TEXT("NV_ENC_PRESET_LOW_LATENCY_HQ"));
        AddValidationPreset(ToWindowsGuid(FNVENCDefs::PresetP1Guid()), NV_ENC_TUNING_INFO_LOW_LATENCY, TEXT("NV_ENC_PRESET_P1"));
        AddValidationPreset(ToWindowsGuid(FNVENCDefs::PresetP2Guid()), NV_ENC_TUNING_INFO_LOW_LATENCY, TEXT("NV_ENC_PRESET_P2"));
        AddValidationPreset(ToWindowsGuid(FNVENCDefs::PresetP3Guid()), NV_ENC_TUNING_INFO_HIGH_QUALITY, TEXT("NV_ENC_PRESET_P3"));
        AddValidationPreset(ToWindowsGuid(FNVENCDefs::PresetP4Guid()), NV_ENC_TUNING_INFO_HIGH_QUALITY, TEXT("NV_ENC_PRESET_P4"));
        AddValidationPreset(ToWindowsGuid(FNVENCDefs::PresetP5Guid()), NV_ENC_TUNING_INFO_HIGH_QUALITY, TEXT("NV_ENC_PRESET_P5"));
        AddValidationPreset(ToWindowsGuid(FNVENCDefs::PresetP6Guid()), NV_ENC_TUNING_INFO_HIGH_QUALITY, TEXT("NV_ENC_PRESET_P6"));
        AddValidationPreset(ToWindowsGuid(FNVENCDefs::PresetP7Guid()), NV_ENC_TUNING_INFO_LOSSLESS, TEXT("NV_ENC_PRESET_P7"));

        auto QueryPreset = [&](void* InEncoderHandle, const GUID& InPresetGuid, NV_ENC_TUNING_INFO PreferredTuning, NV_ENC_PRESET_CONFIG& OutPresetConfig) -> NVENCSTATUS
        {
            NV_ENC_PRESET_CONFIG PresetConfig = {};
            PresetConfig.version = FNVENCDefs::PatchStructVersion(NV_ENC_PRESET_CONFIG_VER, ApiVersion);
            PresetConfig.presetCfg.version = FNVENCDefs::PatchStructVersion(NV_ENC_CONFIG_VER, ApiVersion);

            NVENCSTATUS Status = GetPresetConfig(InEncoderHandle, CodecGuid, InPresetGuid, &PresetConfig);
            if (Status != NV_ENC_SUCCESS && GetPresetConfigEx)
            {
                TArray<NV_ENC_TUNING_INFO, TInlineAllocator<5>> TuningAttempts;
                TuningAttempts.Add(PreferredTuning);
                auto AddAttempt = [&TuningAttempts](NV_ENC_TUNING_INFO Attempt)
                {
                    if (Attempt != NV_ENC_TUNING_INFO_UNDEFINED && !TuningAttempts.Contains(Attempt))
                    {
                        TuningAttempts.Add(Attempt);
                    }
                };

                AddAttempt(NV_ENC_TUNING_INFO_LOW_LATENCY);
                AddAttempt(NV_ENC_TUNING_INFO_HIGH_QUALITY);
                AddAttempt(NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY);
                AddAttempt(NV_ENC_TUNING_INFO_LOSSLESS);
                TuningAttempts.Add(NV_ENC_TUNING_INFO_UNDEFINED);

                for (NV_ENC_TUNING_INFO Tuning : TuningAttempts)
                {
                    Status = GetPresetConfigEx(InEncoderHandle, CodecGuid, InPresetGuid, Tuning, &PresetConfig);
                    if (Status == NV_ENC_SUCCESS)
                    {
                        break;
                    }
                }
            }

            if (Status == NV_ENC_SUCCESS)
            {
                OutPresetConfig = PresetConfig;
            }

            return Status;
        };

        NVENCSTATUS LastPresetStatus = NV_ENC_ERR_NO_ENCODE_DEVICE;
        bool bValidated = false;

        for (const FValidationPreset& ValidationPreset : ValidationPresets)
        {
            const FString PresetName = ValidationPreset.FriendlyName && *ValidationPreset.FriendlyName
                ? FString(ValidationPreset.FriendlyName)
                : FNVENCDefs::PresetGuidToString(FromWindowsGuid(ValidationPreset.Guid));

            NV_ENC_PRESET_CONFIG PresetConfig = {};
            LastPresetStatus = QueryPreset(Encoder, ValidationPreset.Guid, ValidationPreset.PreferredTuning, PresetConfig);

            if (LastPresetStatus != NV_ENC_SUCCESS && bAllowNullFallback
                && (LastPresetStatus == NV_ENC_ERR_INVALID_PARAM || LastPresetStatus == NV_ENC_ERR_INVALID_ENCODERDEVICE))
            {
                UE_LOG(LogNVENCSession, Verbose, TEXT("Retrying NVENC preset %s validation without encoder handle due to %s."),
                    *PresetName, *FNVENCDefs::StatusToString(LastPresetStatus));
                LastPresetStatus = QueryPreset(nullptr, ValidationPreset.Guid, ValidationPreset.PreferredTuning, PresetConfig);
            }

            if (LastPresetStatus == NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCSession, Verbose, TEXT("NVENC preset validation ✓ %s"), *PresetName);
                bValidated = true;
                break;
            }

            const FString StatusString = FNVENCDefs::StatusToString(LastPresetStatus);

            if (LastPresetStatus == NV_ENC_ERR_INVALID_PARAM || LastPresetStatus == NV_ENC_ERR_UNSUPPORTED_PARAM)
            {
                UE_LOG(LogNVENCSession, Warning,
                    TEXT("NVENC preset %s unavailable (%s). Will attempt alternate presets during initialisation."),
                    *PresetName, *StatusString);
                LastErrorMessage.Reset();
                continue;
            }

            if (LastPresetStatus == NV_ENC_ERR_INVALID_ENCODERDEVICE)
            {
                UE_LOG(LogNVENCSession, Warning,
                    TEXT("NVENC preset %s rejected the provided DirectX device (%s). Will attempt alternate presets during initialisation."),
                    *PresetName, *StatusString);
                LastErrorMessage.Reset();
                continue;
            }

            UE_LOG(LogNVENCSession, Warning, TEXT("NvEncGetEncodePresetConfig validation failed for %s preset: %s"),
                *PresetName, *StatusString);
            break;
        }

        if (!bValidated)
        {
            const FString StatusString = FNVENCDefs::StatusToString(LastPresetStatus);
            if (LastPresetStatus == NV_ENC_ERR_INVALID_ENCODERDEVICE)
            {
                LastErrorMessage = FString::Printf(TEXT("NVENC runtime rejected the provided DirectX device (NV_ENC_ERR_INVALID_ENCODERDEVICE). Ensure that a supported NVIDIA GPU and recent drivers are installed. (%s)"), *StatusString);
            }
            else
            {
                LastErrorMessage = FString::Printf(TEXT("NvEncGetEncodePresetConfig validation failed: %s"), *StatusString);
            }
            return false;
        }

        return true;
#endif
    }

    bool FNVENCSession::Initialize(const FNVENCParameters& Parameters)
    {
        LastErrorMessage.Reset();
#if !PLATFORM_WINDOWS
        UE_LOG(LogNVENCSession, Warning, TEXT("Cannot initialise NVENC session on this platform."));
        LastErrorMessage = TEXT("Cannot initialise NVENC session on this platform.");
        return false;
#else
        if (!bIsOpen || !Encoder)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Cannot initialise NVENC session – encoder is not open."));
            LastErrorMessage = TEXT("Cannot initialise NVENC session – encoder is not open.");
            return false;
        }

        using TNvEncGetEncodePresetConfig = NVENCSTATUS(NVENCAPI*)(void*, GUID, GUID, NV_ENC_PRESET_CONFIG*);
        using TNvEncGetEncodePresetConfigEx = NVENCSTATUS(NVENCAPI*)(void*, GUID, GUID, NV_ENC_TUNING_INFO, NV_ENC_PRESET_CONFIG*);
        using TNvEncInitializeEncoder = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_INITIALIZE_PARAMS*);
        using TNvEncGetEncodePresetGUIDs = NVENCSTATUS(NVENCAPI*)(void*, GUID, GUID*, uint32, uint32*);

        TNvEncGetEncodePresetConfig GetPresetConfig = FunctionList.nvEncGetEncodePresetConfig;
        TNvEncGetEncodePresetConfigEx GetPresetConfigEx = reinterpret_cast<TNvEncGetEncodePresetConfigEx>(FunctionList.nvEncGetEncodePresetConfigEx);
        TNvEncInitializeEncoder InitializeEncoder = FunctionList.nvEncInitializeEncoder;
        TNvEncGetEncodePresetGUIDs GetPresetGUIDs = reinterpret_cast<TNvEncGetEncodePresetGUIDs>(FunctionList.nvEncGetEncodePresetGUIDs);
        NVENCSTATUS Status = NV_ENC_SUCCESS;
        const TCHAR* ContextLabel = GetLogContextLabel();

        if (!ValidateFunction("NvEncGetEncodePresetConfig", GetPresetConfig))
        {
            LastErrorMessage = TEXT("Required NVENC export 'NvEncGetEncodePresetConfig' is missing.");
            return false;
        }
        if (!ValidateFunction("NvEncInitializeEncoder", InitializeEncoder))
        {
            LastErrorMessage = TEXT("Required NVENC export 'NvEncInitializeEncoder' is missing.");
            return false;
        }

        GUID CodecGuid = ToWindowsGuid(FNVENCDefs::CodecGuid(Parameters.Codec));
        struct FPresetCandidate
        {
            GUID Guid;
            NV_ENC_TUNING_INFO Tuning = NV_ENC_TUNING_INFO_HIGH_QUALITY;
            FString Description;
        };

        TArray<FPresetCandidate> PresetCandidates;
        PresetCandidates.Reserve(12);

        auto AddCandidate = [&PresetCandidates](const GUID& InGuid, NV_ENC_TUNING_INFO Tuning, const FString& Description)
        {
            for (const FPresetCandidate& Existing : PresetCandidates)
            {
                if (FMemory::Memcmp(&Existing.Guid, &InGuid, sizeof(GUID)) == 0)
                {
                    return;
                }
            }

            FPresetCandidate Candidate;
            Candidate.Guid = InGuid;
            Candidate.Tuning = Tuning;
            Candidate.Description = Description;
            PresetCandidates.Add(Candidate);
        };

        AddCandidate(ToWindowsGuid(FNVENCDefs::PresetDefaultGuid()), NV_ENC_TUNING_INFO_UNDEFINED, TEXT("NV_ENC_PRESET_DEFAULT"));
        AddCandidate(ToWindowsGuid(FNVENCDefs::PresetLowLatencyHighQualityGuid()), NV_ENC_TUNING_INFO_LOW_LATENCY, TEXT("NV_ENC_PRESET_LOW_LATENCY_HQ"));
        AddCandidate(ToWindowsGuid(FNVENCDefs::PresetP1Guid()), NV_ENC_TUNING_INFO_LOW_LATENCY, TEXT("NV_ENC_PRESET_P1"));
        AddCandidate(ToWindowsGuid(FNVENCDefs::PresetP2Guid()), NV_ENC_TUNING_INFO_LOW_LATENCY, TEXT("NV_ENC_PRESET_P2"));
        AddCandidate(ToWindowsGuid(FNVENCDefs::PresetP3Guid()), NV_ENC_TUNING_INFO_HIGH_QUALITY, TEXT("NV_ENC_PRESET_P3"));
        AddCandidate(ToWindowsGuid(FNVENCDefs::PresetP4Guid()), NV_ENC_TUNING_INFO_HIGH_QUALITY, TEXT("NV_ENC_PRESET_P4"));
        AddCandidate(ToWindowsGuid(FNVENCDefs::PresetP5Guid()), NV_ENC_TUNING_INFO_HIGH_QUALITY, TEXT("NV_ENC_PRESET_P5"));
        AddCandidate(ToWindowsGuid(FNVENCDefs::PresetP6Guid()), NV_ENC_TUNING_INFO_HIGH_QUALITY, TEXT("NV_ENC_PRESET_P6"));
        AddCandidate(ToWindowsGuid(FNVENCDefs::PresetP7Guid()), NV_ENC_TUNING_INFO_LOSSLESS, TEXT("NV_ENC_PRESET_P7"));

        const ENVENCPreset RequestedPreset = Parameters.RequestedPreset;
        const ENVENCTuningMode RequestedTuningMode = Parameters.RequestedTuning;
        GUID RequestedPresetGuid = {};
        const bool bHasRequestedPreset = PresetEnumToGuid(RequestedPreset, RequestedPresetGuid);
        const NV_ENC_TUNING_INFO RequestedTuning = ToNVTuning(RequestedTuningMode);

        if (bHasRequestedPreset)
        {
            int32 FoundIndex = INDEX_NONE;
            for (int32 CandidateIndex = 0; CandidateIndex < PresetCandidates.Num(); ++CandidateIndex)
            {
                if (FMemory::Memcmp(&PresetCandidates[CandidateIndex].Guid, &RequestedPresetGuid, sizeof(GUID)) == 0)
                {
                    FoundIndex = CandidateIndex;
                    break;
                }
            }

            if (FoundIndex == INDEX_NONE)
            {
                FPresetCandidate PreferredCandidate;
                PreferredCandidate.Guid = RequestedPresetGuid;
                PreferredCandidate.Tuning = RequestedTuning;
                PreferredCandidate.Description = FNVENCDefs::PresetGuidToString(FromWindowsGuid(RequestedPresetGuid));
                PresetCandidates.Insert(PreferredCandidate, 0);
            }
            else
            {
                if (RequestedTuning != NV_ENC_TUNING_INFO_UNDEFINED)
                {
                    PresetCandidates[FoundIndex].Tuning = RequestedTuning;
                }
                if (FoundIndex != 0)
                {
                    PresetCandidates.Swap(0, FoundIndex);
                }
            }
        }
        else if (RequestedTuning != NV_ENC_TUNING_INFO_UNDEFINED && PresetCandidates.Num() > 0)
        {
            PresetCandidates[0].Tuning = RequestedTuning;
        }

        uint32 RuntimePresetCount = 0;

        if (GetPresetGUIDs)
        {
            uint32 AvailablePresetCount = 0;
            NVENCSTATUS EnumStatus = GetPresetGUIDs(Encoder, CodecGuid, nullptr, 0, &AvailablePresetCount);
            if (EnumStatus == NV_ENC_SUCCESS && AvailablePresetCount > 0)
            {
                TArray<GUID> RuntimePresets;
                RuntimePresets.SetNumZeroed(AvailablePresetCount);

                EnumStatus = GetPresetGUIDs(Encoder, CodecGuid, RuntimePresets.GetData(), AvailablePresetCount, &AvailablePresetCount);
                if (EnumStatus == NV_ENC_SUCCESS)
                {
                    RuntimePresets.SetNum(AvailablePresetCount);
                    RuntimePresetCount = AvailablePresetCount;
                    for (const GUID& RuntimeGuid : RuntimePresets)
                    {
                        const FString FriendlyName = FNVENCDefs::PresetGuidToString(FromWindowsGuid(RuntimeGuid));
                        AddCandidate(RuntimeGuid, NV_ENC_TUNING_INFO_HIGH_QUALITY, FriendlyName);
                    }
                }
            }
        }

        if (RuntimePresetCount > 0)
        {
            UE_LOG(LogNVENCSession, Log, TEXT("NVENC session ✓ Queried %u encode preset GUIDs."), RuntimePresetCount);
        }

        NV_ENC_PRESET_CONFIG PresetConfig = {};
        int32 SelectedPresetIndex = INDEX_NONE;
        NVENCSTATUS LastPresetStatus = NV_ENC_SUCCESS;

        auto QueryPresetConfig = [&](void* InEncoderHandle, const FPresetCandidate& Candidate, NV_ENC_PRESET_CONFIG& OutConfig)
        {
            NV_ENC_PRESET_CONFIG AttemptConfig = {};
            AttemptConfig.version = FNVENCDefs::PatchStructVersion(NV_ENC_PRESET_CONFIG_VER, ApiVersion);
            AttemptConfig.presetCfg.version = FNVENCDefs::PatchStructVersion(NV_ENC_CONFIG_VER, ApiVersion);

            NVENCSTATUS Status = GetPresetConfig(InEncoderHandle, CodecGuid, Candidate.Guid, &AttemptConfig);

            if (Status != NV_ENC_SUCCESS && GetPresetConfigEx)
            {
                TArray<NV_ENC_TUNING_INFO, TInlineAllocator<4>> TuningAttempts;
                TuningAttempts.Add(Candidate.Tuning);
                auto AddUniqueTuning = [&TuningAttempts](NV_ENC_TUNING_INFO InTuning)
                {
                    if (!TuningAttempts.Contains(InTuning))
                    {
                        TuningAttempts.Add(InTuning);
                    }
                };

                AddUniqueTuning(NV_ENC_TUNING_INFO_UNDEFINED);
                AddUniqueTuning(NV_ENC_TUNING_INFO_HIGH_QUALITY);
                AddUniqueTuning(NV_ENC_TUNING_INFO_LOW_LATENCY);
                AddUniqueTuning(NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY);
                AddUniqueTuning(NV_ENC_TUNING_INFO_LOSSLESS);

                for (NV_ENC_TUNING_INFO TuningAttempt : TuningAttempts)
                {
                    Status = GetPresetConfigEx(InEncoderHandle, CodecGuid, Candidate.Guid, TuningAttempt, &AttemptConfig);
                    if (Status == NV_ENC_SUCCESS)
                    {
                        break;
                    }
                }
            }

            if (Status == NV_ENC_SUCCESS)
            {
                OutConfig = AttemptConfig;
            }

            return Status;
        };

        for (int32 CandidateIndex = 0; CandidateIndex < PresetCandidates.Num(); ++CandidateIndex)
        {
            const FPresetCandidate& Candidate = PresetCandidates[CandidateIndex];
            LastPresetStatus = QueryPresetConfig(Encoder, Candidate, PresetConfig);

            const bool bShouldRetryWithoutHandle =
                (LastPresetStatus == NV_ENC_ERR_INVALID_PARAM || LastPresetStatus == NV_ENC_ERR_INVALID_ENCODERDEVICE) && Encoder;

            if (bShouldRetryWithoutHandle)
            {
                const FString PresetName = Candidate.Description.IsEmpty()
                    ? FNVENCDefs::PresetGuidToString(FromWindowsGuid(Candidate.Guid))
                    : Candidate.Description;
                UE_LOG(LogNVENCSession, Verbose, TEXT("Retrying NVENC preset %s query without encoder handle due to %s."), *PresetName, *FNVENCDefs::StatusToString(LastPresetStatus));

                LastPresetStatus = QueryPresetConfig(nullptr, Candidate, PresetConfig);
            }

            if (LastPresetStatus == NV_ENC_SUCCESS)
            {
                SelectedPresetIndex = CandidateIndex;
                break;
            }

            const FString PresetName = Candidate.Description.IsEmpty()
                ? FNVENCDefs::PresetGuidToString(FromWindowsGuid(Candidate.Guid))
                : Candidate.Description;
            UE_LOG(LogNVENCSession, Warning, TEXT("NvEncGetEncodePresetConfig failed for %s preset: %s"), *PresetName, *FNVENCDefs::StatusToString(LastPresetStatus));

            if (LastPresetStatus == NV_ENC_ERR_INVALID_ENCODERDEVICE)
            {
                break;
            }
        }

        if (SelectedPresetIndex == INDEX_NONE)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NvEncGetEncodePresetConfig failed for all attempted presets: %s"), *FNVENCDefs::StatusToString(LastPresetStatus));
            const FString StatusString = FNVENCDefs::StatusToString(LastPresetStatus);
            if (LastPresetStatus == NV_ENC_ERR_INVALID_ENCODERDEVICE)
            {
                LastErrorMessage = FString::Printf(TEXT("NVENC runtime rejected the provided DirectX device (NV_ENC_ERR_INVALID_ENCODERDEVICE). Ensure that a supported NVIDIA GPU and recent drivers are installed. (%s)"), *StatusString);
            }
            else
            {
                LastErrorMessage = FString::Printf(TEXT("NvEncGetEncodePresetConfig failed for all attempted presets: %s"), *StatusString);
            }
            return false;
        }

        const FPresetCandidate& SelectedPreset = PresetCandidates[SelectedPresetIndex];
        const FString SelectedPresetName = SelectedPreset.Description.IsEmpty()
            ? FNVENCDefs::PresetGuidToString(FromWindowsGuid(SelectedPreset.Guid))
            : SelectedPreset.Description;

        UE_LOG(LogNVENCSession, Log, TEXT("NVENC session ✓ Selected preset configuration: %s"), *SelectedPresetName);

        if (SelectedPresetIndex > 0)
        {
            UE_LOG(LogNVENCSession, Log, TEXT("Using fallback NVENC preset %s after trying %d options."), *SelectedPresetName, SelectedPresetIndex + 1);
        }

        EncodeConfig = PresetConfig.presetCfg;
        EncodeConfig.version = FNVENCDefs::PatchStructVersion(NV_ENC_CONFIG_VER, ApiVersion);
        EncodeConfig.rcParams.rateControlMode = ToNVRateControl(Parameters.RateControlMode);
        EncodeConfig.rcParams.averageBitRate = Parameters.TargetBitrate;
        EncodeConfig.rcParams.maxBitRate = Parameters.MaxBitrate;
        EncodeConfig.rcParams.enableLookahead = Parameters.bEnableLookahead ? 1u : 0u;
        EncodeConfig.rcParams.enableAQ = Parameters.bEnableAdaptiveQuantization ? 1u : 0u;
        EncodeConfig.rcParams.enableTemporalAQ = Parameters.bEnableAdaptiveQuantization ? 1u : 0u;
        EncodeConfig.rcParams.enableInitialRCQP = (Parameters.QPMax >= 0 || Parameters.QPMin >= 0) ? 1u : 0u;
        EncodeConfig.rcParams.constQP.qpInterB = Parameters.QPMax >= 0 ? Parameters.QPMax : EncodeConfig.rcParams.constQP.qpInterB;
        EncodeConfig.rcParams.constQP.qpInterP = Parameters.QPMax >= 0 ? Parameters.QPMax : EncodeConfig.rcParams.constQP.qpInterP;
        EncodeConfig.rcParams.constQP.qpIntra = Parameters.QPMin >= 0 ? Parameters.QPMin : EncodeConfig.rcParams.constQP.qpIntra;
        EncodeConfig.rcParams.multiPass = ToNVMultiPass(Parameters.MultipassMode);
        EncodeConfig.gopLength = Parameters.GOPLength == 0 ? NVENC_INFINITE_GOPLENGTH : Parameters.GOPLength;
        EncodeConfig.frameIntervalP = 1;
        EncodeConfig.frameFieldMode = NV_ENC_PARAMS_FRAME_FIELD_MODE_FRAME;
        EncodeConfig.mvPrecision = NV_ENC_MV_PRECISION_QUARTER_PEL;

        if (Parameters.Codec == ENVENCCodec::H264)
        {
            EncodeConfig.profileGUID = NV_ENC_H264_PROFILE_MAIN_GUID;
            EncodeConfig.encodeCodecConfig.h264Config.idrPeriod = EncodeConfig.gopLength;
        }

        ENVENCBufferFormat EffectiveBufferFormat = Parameters.BufferFormat;
        NvBufferFormat = ToNVFormat(EffectiveBufferFormat);
        if (Parameters.Codec == ENVENCCodec::H264 && EffectiveBufferFormat != ENVENCBufferFormat::NV12)
        {
            UE_LOG(LogNVENCSession, Warning,
                TEXT("NVENC session switching H.264 input format to NV12 8-bit 4:2:0 for compatibility."));
            NvBufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
            EffectiveBufferFormat = ENVENCBufferFormat::NV12;
        }
        else if (Parameters.Codec == ENVENCCodec::HEVC && EffectiveBufferFormat == ENVENCBufferFormat::BGRA)
        {
            UE_LOG(LogNVENCSession, Warning,
                TEXT("NVENC session switching HEVC input format to NV12 8-bit 4:2:0 for compatibility."));
            NvBufferFormat = NV_ENC_BUFFER_FORMAT_NV12;
            EffectiveBufferFormat = ENVENCBufferFormat::NV12;
        }

        const NV_ENC_BIT_DEPTH NvBitDepth = ToNVBitDepth(NvBufferFormat);
        const uint32 NvChromaFormat = GetChromaFormatIDC(NvBufferFormat);

        if (Parameters.Codec == ENVENCCodec::H264)
        {
            EncodeConfig.encodeCodecConfig.h264Config.chromaFormatIDC = NvChromaFormat;
            EncodeConfig.encodeCodecConfig.h264Config.inputBitDepth = NvBitDepth;
            EncodeConfig.encodeCodecConfig.h264Config.outputBitDepth = NvBitDepth;
        }
        else
        {
            const bool bIs10Bit = NvBitDepth == NV_ENC_BIT_DEPTH_10;
#if defined(NV_ENC_HEVC_PROFILE_MAIN10_GUID)
            EncodeConfig.profileGUID = bIs10Bit ? NV_ENC_HEVC_PROFILE_MAIN10_GUID : NV_ENC_HEVC_PROFILE_MAIN_GUID;
#else
            EncodeConfig.profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
#endif

            NV_ENC_CONFIG_HEVC& HevcConfig = EncodeConfig.encodeCodecConfig.hevcConfig;
            FMemory::Memzero(&HevcConfig, sizeof(NV_ENC_CONFIG_HEVC));

            HevcConfig.level = NV_ENC_LEVEL_AUTOSELECT;
            HevcConfig.tier = NV_ENC_TIER_HEVC_MAIN;
            HevcConfig.minCUSize = NV_ENC_HEVC_CUSIZE_AUTOSELECT;
            HevcConfig.maxCUSize = NV_ENC_HEVC_CUSIZE_32x32;
            HevcConfig.chromaFormatIDC = NvChromaFormat;
            HevcConfig.inputBitDepth = NvBitDepth;
            HevcConfig.outputBitDepth = NvBitDepth;
            HevcConfig.idrPeriod = EncodeConfig.gopLength;
            HevcConfig.useBFramesAsRef = NV_ENC_BFRAME_REF_MODE_DISABLED;
            HevcConfig.numRefL0 = NV_ENC_NUM_REF_FRAMES_AUTOSELECT;
            HevcConfig.numRefL1 = NV_ENC_NUM_REF_FRAMES_AUTOSELECT;

#if defined(NV_ENC_CONFIG_HEVC_PIXEL_BIT_DEPTH_MINUS8)
            HevcConfig.pixelBitDepthMinus8 = bIs10Bit ? 2u : 0u;
#endif
        }

        InitializeParams = {};
        InitializeParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_INITIALIZE_PARAMS_VER, ApiVersion);
        InitializeParams.encodeGUID = CodecGuid;
        InitializeParams.presetGUID = SelectedPreset.Guid;
        InitializeParams.tuningInfo = SelectedPreset.Tuning;
        InitializeParams.encodeWidth = Parameters.Width;
        InitializeParams.encodeHeight = Parameters.Height;
        InitializeParams.darWidth = Parameters.Width;
        InitializeParams.darHeight = Parameters.Height;
        InitializeParams.frameRateNum = Parameters.Framerate == 0 ? 60 : Parameters.Framerate;
        InitializeParams.frameRateDen = 1;
        InitializeParams.enablePTD = 1;
        InitializeParams.encodeConfig = &EncodeConfig;
        InitializeParams.maxEncodeWidth = Parameters.Width;
        InitializeParams.maxEncodeHeight = Parameters.Height;
        InitializeParams.bufferFormat = NvBufferFormat;
        InitializeParams.enableEncodeAsync = 0;

        Status = InitializeEncoder(Encoder, &InitializeParams);
        if (Status != NV_ENC_SUCCESS)
        {
            const FString StatusString = FNVENCDefs::StatusToString(Status);
            const FString CodecString = FNVENCDefs::CodecToString(Parameters.Codec);
            const FString PresetString = SelectedPresetName;
            const FString ProfileString = ProfileGuidToString(EncodeConfig.profileGUID);
            const uint32 LevelValue = Parameters.Codec == ENVENCCodec::H264
                ? EncodeConfig.encodeCodecConfig.h264Config.level
                : EncodeConfig.encodeCodecConfig.hevcConfig.level;
            const FString LevelString = LevelToString(LevelValue);
            const FNVENCAPIVersion RuntimeVersion = FNVENCDefs::DecodeApiVersion(ApiVersion);
            const FNVENCAPIVersion BuildVersion = FNVENCDefs::DecodeApiVersion(NVENCAPI_VERSION);

            UE_LOG(LogNVENCSession, Error,
                TEXT("%s ✗ NvEncInitializeEncoder failed: %s (Codec=%s, Preset=%s, Profile=%s, Level=%s, API runtime=%s (0x%08x), API build=%s (0x%08x))"),
                ContextLabel,
                *StatusString,
                *CodecString,
                *PresetString,
                *ProfileString,
                *LevelString,
                *FNVENCDefs::VersionToString(RuntimeVersion),
                ApiVersion,
                *FNVENCDefs::VersionToString(BuildVersion),
                NVENCAPI_VERSION);

            LastErrorMessage = FString::Printf(
                TEXT("NvEncInitializeEncoder failed: %s (Codec=%s, Preset=%s, Profile=%s, Level=%s, API runtime=%s (0x%08x), API build=%s (0x%08x))"),
                *StatusString,
                *CodecString,
                *PresetString,
                *ProfileString,
                *LevelString,
                *FNVENCDefs::VersionToString(RuntimeVersion),
                ApiVersion,
                *FNVENCDefs::VersionToString(BuildVersion),
                NVENCAPI_VERSION);
            return false;
        }

        CurrentParameters = Parameters;
        CurrentParameters.BufferFormat = EffectiveBufferFormat;
        CurrentParameters.ActivePresetGuid = FromWindowsGuid(SelectedPreset.Guid);
        CurrentParameters.ActiveTuning = FromNVTuning(SelectedPreset.Tuning);
        bIsInitialised = true;
        UE_LOG(LogNVENCSession, Log, TEXT("%s ✓ Encoder initialised: %s"), ContextLabel, *FNVENCParameterMapper::ToDebugString(CurrentParameters));
        return true;
#endif
    }

    bool FNVENCSession::Reconfigure(const FNVENCParameters& Parameters)
    {
        LastErrorMessage.Reset();
#if !PLATFORM_WINDOWS
        LastErrorMessage = TEXT("Cannot reconfigure NVENC session on this platform.");
        return false;
#else
        if (!bIsInitialised)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("Cannot reconfigure NVENC session – encoder has not been initialised."));
            LastErrorMessage = TEXT("Cannot reconfigure NVENC session – encoder has not been initialised.");
            return false;
        }

        using TNvEncReconfigureEncoder = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_RECONFIGURE_PARAMS*);

        TNvEncReconfigureEncoder ReconfigureEncoder = FunctionList.nvEncReconfigureEncoder;
        if (!ValidateFunction("NvEncReconfigureEncoder", ReconfigureEncoder))
        {
            LastErrorMessage = TEXT("Required NVENC export 'NvEncReconfigureEncoder' is missing.");
            return false;
        }

        NV_ENC_CONFIG NewConfig = EncodeConfig;
        NewConfig.rcParams.rateControlMode = ToNVRateControl(Parameters.RateControlMode);
        NewConfig.rcParams.averageBitRate = Parameters.TargetBitrate;
        NewConfig.rcParams.maxBitRate = Parameters.MaxBitrate;
        NewConfig.rcParams.enableLookahead = Parameters.bEnableLookahead ? 1u : 0u;
        NewConfig.rcParams.enableAQ = Parameters.bEnableAdaptiveQuantization ? 1u : 0u;
        NewConfig.rcParams.enableTemporalAQ = Parameters.bEnableAdaptiveQuantization ? 1u : 0u;
        NewConfig.rcParams.multiPass = ToNVMultiPass(Parameters.MultipassMode);
        NewConfig.gopLength = Parameters.GOPLength == 0 ? NVENC_INFINITE_GOPLENGTH : Parameters.GOPLength;

        NV_ENC_RECONFIGURE_PARAMS ReconfigureParams = {};
        ReconfigureParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_RECONFIGURE_PARAMS_VER, ApiVersion);
        ReconfigureParams.reInitEncodeParams = InitializeParams;
        ReconfigureParams.reInitEncodeParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_INITIALIZE_PARAMS_VER, ApiVersion);
        ReconfigureParams.reInitEncodeParams.encodeWidth = Parameters.Width;
        ReconfigureParams.reInitEncodeParams.encodeHeight = Parameters.Height;
        ReconfigureParams.reInitEncodeParams.darWidth = Parameters.Width;
        ReconfigureParams.reInitEncodeParams.darHeight = Parameters.Height;
        ReconfigureParams.reInitEncodeParams.encodeConfig = &NewConfig;
        ReconfigureParams.reInitEncodeParams.maxEncodeWidth = Parameters.Width;
        ReconfigureParams.reInitEncodeParams.maxEncodeHeight = Parameters.Height;
        ReconfigureParams.reInitEncodeParams.bufferFormat = NvBufferFormat;
        ReconfigureParams.forceIDR = 1;
        ReconfigureParams.resetEncoder = 1;

        NVENCSTATUS Status = ReconfigureEncoder(Encoder, &ReconfigureParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCSession, Error, TEXT("NvEncReconfigureEncoder failed: %s"), *FNVENCDefs::StatusToString(Status));
            LastErrorMessage = FString::Printf(TEXT("NvEncReconfigureEncoder failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        EncodeConfig = NewConfig;
        InitializeParams = ReconfigureParams.reInitEncodeParams;
        const FGuid PreviousPresetGuid = CurrentParameters.ActivePresetGuid;
        const ENVENCTuningMode PreviousTuning = CurrentParameters.ActiveTuning;
        CurrentParameters = Parameters;
        if (!CurrentParameters.ActivePresetGuid.IsValid())
        {
            CurrentParameters.ActivePresetGuid = PreviousPresetGuid;
        }
        if (CurrentParameters.ActiveTuning == ENVENCTuningMode::Automatic)
        {
            CurrentParameters.ActiveTuning = PreviousTuning;
        }
        UE_LOG(LogNVENCSession, Verbose, TEXT("NVENC session reconfigured: %s"), *FNVENCParameterMapper::ToDebugString(CurrentParameters));
        return true;
#endif
    }

    void FNVENCSession::Flush()
    {
#if PLATFORM_WINDOWS
        if (!bIsInitialised)
        {
            return;
        }

#if UE_NVENC_HAS_FLUSH_FUNCTION
        using TNvEncFlushEncoderQueue = NVENCSTATUS(NVENCAPI*)(void*, void*);
        TNvEncFlushEncoderQueue FlushEncoder = FunctionList.nvEncFlushEncoderQueue;
        if (FlushEncoder)
        {
            NVENCSTATUS Status = FlushEncoder(Encoder, nullptr);
            if (Status != NV_ENC_SUCCESS && Status != NV_ENC_ERR_NEED_MORE_INPUT)
            {
                UE_LOG(LogNVENCSession, Warning, TEXT("NvEncFlushEncoderQueue returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }
#endif
#endif
    }

    void FNVENCSession::Destroy()
    {
#if PLATFORM_WINDOWS
        if (!bIsOpen)
        {
            return;
        }

        using TNvEncDestroyEncoder = NVENCSTATUS(NVENCAPI*)(void*);
        TNvEncDestroyEncoder DestroyEncoder = FunctionList.nvEncDestroyEncoder;
        if (Encoder && DestroyEncoder)
        {
            NVENCSTATUS Status = DestroyEncoder(Encoder);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCSession, Warning, TEXT("NvEncDestroyEncoder returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }

        Encoder = nullptr;
        Device = nullptr;
        bIsInitialised = false;
        bIsOpen = false;
        FunctionList = {};
#endif
        CurrentParameters = FNVENCParameters();
        ApiVersion = NVENCAPI_VERSION;
    }

    bool FNVENCSession::GetSequenceParams(TArray<uint8>& OutData)
    {
#if !PLATFORM_WINDOWS
        return false;
#else
        if (!bIsInitialised || !Encoder)
        {
            return false;
        }

        using TNvEncGetSequenceParams = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_SEQUENCE_PARAM_PAYLOAD*);
        TNvEncGetSequenceParams GetSequenceParamsFn = FunctionList.nvEncGetSequenceParams;
        if (!GetSequenceParamsFn)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("NvEncGetSequenceParams is unavailable in this NVENC runtime."));
            return false;
        }

        uint32 OutputSize = 0;
        TArray<uint8> Buffer;
        Buffer.SetNumZeroed(1024);

        NV_ENC_SEQUENCE_PARAM_PAYLOAD Payload = {};
        Payload.version = FNVENCDefs::PatchStructVersion(NV_ENC_SEQUENCE_PARAM_PAYLOAD_VER, ApiVersion);
        Payload.inBufferSize = Buffer.Num();
        Payload.spsppsBuffer = Buffer.GetData();
        Payload.outSPSPPSPayloadSize = &OutputSize;

        NVENCSTATUS Status = GetSequenceParamsFn(Encoder, &Payload);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCSession, Warning, TEXT("NvEncGetSequenceParams failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        if (OutputSize == 0)
        {
            return false;
        }

        if (OutputSize > static_cast<uint32>(Buffer.Num()))
        {
            Buffer.SetNumZeroed(OutputSize);
            Payload.inBufferSize = Buffer.Num();
            Payload.spsppsBuffer = Buffer.GetData();

            Status = GetSequenceParamsFn(Encoder, &Payload);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCSession, Warning, TEXT("NvEncGetSequenceParams failed on resized buffer: %s"), *FNVENCDefs::StatusToString(Status));
                return false;
            }

            OutputSize = FMath::Min<uint32>(OutputSize, static_cast<uint32>(Buffer.Num()));
        }

        Buffer.SetNum(OutputSize);
        OutData = MoveTemp(Buffer);
        return true;
#endif
    }
}

#endif // WITH_OMNI_NVENC

