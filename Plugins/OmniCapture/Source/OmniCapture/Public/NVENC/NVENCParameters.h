// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "NVENC/NVENCDefs.h"

namespace OmniNVENC
{
    enum class ENVENCRateControlMode : uint8
    {
        CBR,
        VBR,
        CONSTQP,
    };

    enum class ENVENCMultipassMode : uint8
    {
        DISABLED,
        QUARTER,
        FULL,
    };

    /**
     * Aggregated NVENC configuration used by the OmniCapture integration.
     */
    struct FNVENCParameters
    {
        ENVENCCodec Codec = ENVENCCodec::H264;
        ENVENCBufferFormat BufferFormat = ENVENCBufferFormat::NV12;
        uint32 Width = 0;
        uint32 Height = 0;
        uint32 Framerate = 0;
        int32 MaxBitrate = 0;
        int32 TargetBitrate = 0;
        int32 QPMin = -1;
        int32 QPMax = -1;
        ENVENCRateControlMode RateControlMode = ENVENCRateControlMode::CBR;
        ENVENCMultipassMode MultipassMode = ENVENCMultipassMode::FULL;
        bool bEnableLookahead = false;
        bool bEnableAdaptiveQuantization = false;
        bool bEnableIntraRefresh = false;
        bool bIntraRefreshOnSceneChange = false;
        uint32 GOPLength = 0;
    };

    /** Helper that serialises NVENC parameters for debugging. */
    class FNVENCParameterMapper
    {
    public:
        /** Creates a readable string representation of the parameter set. */
        static FString ToDebugString(const FNVENCParameters& Params);
    };
}

