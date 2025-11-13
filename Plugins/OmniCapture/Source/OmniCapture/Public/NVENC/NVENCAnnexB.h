// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace OmniNVENC
{
    /** Utility responsible for packaging codec parameter sets into Annex B payloads. */
    class FNVENCAnnexB
    {
    public:
        /** Resets any cached state (e.g. SPS/PPS/VPS data). */
        void Reset();

        /** Returns cached codec configuration data to be emitted with the first packet. */
        const TArray<uint8>& GetCodecConfig() const { return CodecConfig; }
        bool HasCodecConfig() const { return CodecConfig.Num() > 0; }

        /**
         * Updates the cached configuration.  The trimmed build has no access to the
         * actual NVENC parameter sets so this simply records the byte arrays passed
         * in by higher level code.
         */
        void SetCodecConfig(const TArray<uint8>& InData);

    private:
        TArray<uint8> CodecConfig;
    };
}

