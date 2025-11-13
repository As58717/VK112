// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace OmniNVENC
{
    /** Enumerates the codecs exposed by the NVENC backend. */
    enum class ENVENCCodec : uint8
    {
        H264,
        HEVC,
    };

    /** Pixel formats supported by the NVENC entry points we expose in this trimmed build. */
    enum class ENVENCBufferFormat : uint8
    {
        NV12,
        P010,
        BGRA,
    };

    /** Simple view over the capabilities that we query from the runtime. */
    struct FNVENCCapabilities
    {
        bool bSupports10Bit = false;
        bool bSupportsBFrames = false;
        bool bSupportsYUV444 = false;
        bool bSupportsLookahead = false;
        bool bSupportsAdaptiveQuantization = false;
        int32 MaxWidth = 0;
        int32 MaxHeight = 0;
    };

    struct FNVENCAPIVersion
    {
        uint32 Major = 0;
        uint32 Minor = 0;
    };

    /** Handy helpers that keep commonly used constants and conversions together. */
    class FNVENCDefs
    {
    public:
        static const FGuid& CodecGuid(ENVENCCodec Codec);
        static const FGuid& PresetDefaultGuid();
        static const FGuid& PresetP1Guid();
        static const FGuid& PresetP2Guid();
        static const FGuid& PresetP3Guid();
        static const FGuid& PresetP4Guid();
        static const FGuid& PresetP5Guid();
        static const FGuid& PresetP6Guid();
        static const FGuid& PresetP7Guid();
        static const FGuid& PresetHighPerformanceApproxGuid();
        static const FGuid& PresetHighQualityApproxGuid();
        static const FGuid& PresetLowLatencyHighQualityGuid();
        static FString PresetGuidToString(const FGuid& Guid);
        static const FGuid& TuningLatencyGuid();
        static const FGuid& TuningQualityGuid();

        static FString BufferFormatToString(ENVENCBufferFormat Format);
        static FString CodecToString(ENVENCCodec Codec);

        /** Converts well known NVENC status codes into log friendly text. */
        static FString StatusToString(int32 StatusCode);

        /** Returns the minimum API version supported by this trimmed build. */
        static FNVENCAPIVersion GetMinimumAPIVersion();

        /** Builds an encoded API version suitable for NVENC structures. */
        static uint32 EncodeApiVersion(const FNVENCAPIVersion& Version);

        /** Decodes an encoded NVENC API version into discrete components. */
        static FNVENCAPIVersion DecodeApiVersion(uint32 EncodedVersion);

        /**
         * Converts the version integer returned by NvEncodeAPIGetMaxSupportedVersion
         * into discrete components.
         */
        static FNVENCAPIVersion DecodeRuntimeVersion(uint32 RuntimeVersion);

        /** Human friendly string representation of an NVENC API version. */
        static FString VersionToString(const FNVENCAPIVersion& Version);

        /** True when Lhs represents an older API version than Rhs. */
        static bool IsVersionOlder(const FNVENCAPIVersion& Lhs, const FNVENCAPIVersion& Rhs);

        /**
         * Takes a struct version constant generated with NVENCAPI_STRUCT_VERSION and rewrites the
         * encoded API version so that it matches the runtime we negotiated with.
         */
        static uint32 PatchStructVersion(uint32 StructVersion, uint32 ApiVersion);
    };
}

