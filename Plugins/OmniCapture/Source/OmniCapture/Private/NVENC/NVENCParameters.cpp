// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCParameters.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCParameters, Log, All);

namespace OmniNVENC
{
    FString FNVENCParameterMapper::ToDebugString(const FNVENCParameters& Params)
    {
        return FString::Printf(TEXT("Codec=%s Format=%s %ux%u %u fps Bitrate=%d/%d QP=[%d,%d] RC=%d MP=%d AQ=%s LA=%s IR=%s IRScene=%s GOP=%u"),
            *FNVENCDefs::CodecToString(Params.Codec),
            *FNVENCDefs::BufferFormatToString(Params.BufferFormat),
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

