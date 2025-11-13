// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCAnnexB.h"

namespace OmniNVENC
{
    void FNVENCAnnexB::Reset()
    {
        CodecConfig.Reset();
    }

    void FNVENCAnnexB::SetCodecConfig(const TArray<uint8>& InData)
    {
        CodecConfig.Reset();

        if (InData.Num() == 0)
        {
            return;
        }

        static const uint8 AnnexBStartCode[] = { 0x00, 0x00, 0x00, 0x01 };

        if (InData.Num() >= UE_ARRAY_COUNT(AnnexBStartCode) &&
            FMemory::Memcmp(InData.GetData(), AnnexBStartCode, sizeof(AnnexBStartCode)) == 0)
        {
            CodecConfig = InData;
            return;
        }

        CodecConfig.Append(AnnexBStartCode, UE_ARRAY_COUNT(AnnexBStartCode));
        CodecConfig.Append(InData);
    }
}

