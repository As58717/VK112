// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCParameters.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCParameters, Log, All);

namespace OmniNVENC
{
    FString FNVENCParameterMapper::ToDebugString(const FNVENCParameters& Params)
    {
        const FString PresetString = Params.ActivePresetGuid.IsValid()
            ? FNVENCDefs::PresetGuidToString(Params.ActivePresetGuid)
            : TEXT("auto");

        auto TuningToString = [](ENVENCTuningMode Mode) -> const TCHAR*
        {
            switch (Mode)
            {
            case ENVENCTuningMode::HighQuality: return TEXT("high-quality");
            case ENVENCTuningMode::LowLatency: return TEXT("low-latency");
            case ENVENCTuningMode::UltraLowLatency: return TEXT("ultra-low-latency");
            case ENVENCTuningMode::Lossless: return TEXT("lossless");
            default: return TEXT("auto");
            }
        };

        return FString::Printf(TEXT("Codec=%s Format=%s Preset=%s Tuning=%s %ux%u %u fps Bitrate=%d/%d QP=[%d,%d] RC=%d MP=%d AQ=%s LA=%s IR=%s IRScene=%s GOP=%u"),
            *FNVENCDefs::CodecToString(Params.Codec),
            *FNVENCDefs::BufferFormatToString(Params.BufferFormat),
            *PresetString,
            TuningToString(Params.ActiveTuning),
            Params.Width,
            Params.Height,
            Params.Framerate,
            Params.TargetBitrate,
            Params.MaxBitrate,
            Params.QPMin,
            Params.QPMax,
            static_cast<int32>(Params.RateControlMode),
            static_cast<int32>(Params.MultipassMode),
            Params.bEnableAdaptiveQuantization ? TEXT("on") : TEXT("off"),
            Params.bEnableLookahead ? TEXT("on") : TEXT("off"),
            Params.bEnableIntraRefresh ? TEXT("on") : TEXT("off"),
            Params.bIntraRefreshOnSceneChange ? TEXT("on") : TEXT("off"),
            Params.GOPLength);
    }
}

