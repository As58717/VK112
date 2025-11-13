#pragma once

#include "CoreMinimal.h"
#include "OmniCaptureTypes.h"

#include "NVENC/NVENCPlatform.h"
#include "NVENC/NVENCDefs.h"

#if PLATFORM_WINDOWS && WITH_OMNI_NVENC
    #define OMNI_WITH_NVENC 1
    #include "NVENC/NVENCAnnexB.h"
    #include "NVENC/NVENCBitstream.h"
    #include "NVENC/NVENCCommon.h"
    #include "NVENC/NVENCCaps.h"
    #include "NVENC/NVENCDefs.h"
    #include "NVENC/NVENCInputD3D11.h"
    #include "NVENC/NVENCInputD3D12.h"
    #include "NVENC/NVENCParameters.h"
    #include "NVENC/NVENCSession.h"
    #include "NVENC/NVEncodeAPILoader.h"
#else
    #define OMNI_WITH_NVENC 0
#endif

struct FOmniNVENCCapabilities
{
    bool bHardwareAvailable = false;
    bool bDllPresent = false;
    bool bApisReady = false;
    bool bSessionOpenable = false;
    bool bSupportsNV12 = false;
    bool bSupportsP010 = false;
    bool bSupportsHEVC = false;
    bool bSupports10Bit = false;
    bool bSupportsBGRA = false;
    TMap<OmniNVENC::ENVENCCodec, OmniNVENC::FNVENCCapabilities> CodecCapabilities;
    FString DllFailureReason;
    FString ApiFailureReason;
    FString SessionFailureReason;
    FString CodecFailureReason;
    FString NV12FailureReason;
    FString P010FailureReason;
    FString BGRAFailureReason;
    FString HardwareFailureReason;
    FString AdapterName;
    FString DriverVersion;
};

class OMNICAPTURE_API FOmniCaptureNVENCEncoder
{
public:
    FOmniCaptureNVENCEncoder();
    ~FOmniCaptureNVENCEncoder();

    void Initialize(const FOmniCaptureSettings& Settings, const FString& OutputDirectory);
    void EnqueueFrame(const FOmniCaptureFrame& Frame);
    void Finalize();

    static bool IsNVENCAvailable();
    static FOmniNVENCCapabilities QueryCapabilities();
    static bool SupportsColorFormat(EOmniCaptureColorFormat Format);
    static bool SupportsZeroCopyRHI();
    static void SetRuntimeDirectoryOverride(const FString& InOverridePath);
    static void SetDllOverridePath(const FString& InOverridePath);
    static void InvalidateCachedCapabilities();
    static void LogRuntimeStatus();

    bool IsInitialized() const { return bInitialized; }
    FString GetOutputFilePath() const { return OutputFilePath; }
    const FString& GetLastError() const { return LastErrorMessage; }

private:
    FString OutputFilePath;
    bool bInitialized = false;
    EOmniCaptureColorFormat ColorFormat = EOmniCaptureColorFormat::NV12;
    bool bZeroCopyRequested = true;
    EOmniCaptureCodec RequestedCodec = EOmniCaptureCodec::HEVC;
    EOmniCaptureNVENCD3D12Interop ActiveD3D12InteropMode = EOmniCaptureNVENCD3D12Interop::Bridge;
    FString LastErrorMessage;

#if OMNI_WITH_NVENC
    OmniNVENC::FNVENCSession EncoderSession;
    OmniNVENC::FNVENCBitstream Bitstream;
    OmniNVENC::FNVENCInputD3D11 D3D11Input;
    OmniNVENC::FNVENCInputD3D12 D3D12Input;
    OmniNVENC::FNVENCAnnexB AnnexB;
    OmniNVENC::FNVENCParameters ActiveParameters;
    FCriticalSection EncoderCS;
    TUniquePtr<IFileHandle> BitstreamFile;
    bool bAnnexBHeaderWritten = false;

    bool WriteAnnexBHeader();

#if PLATFORM_WINDOWS
#if OMNI_WITH_D3D11_RHI
    bool EncodeFrameD3D11(const FOmniCaptureFrame& Frame);
#endif
#if OMNI_WITH_D3D12_RHI
    bool EncodeFrameD3D12(const FOmniCaptureFrame& Frame);
#endif
#endif
    bool EncodeFrameInternal(const FOmniCaptureFrame& Frame);
#endif
};

