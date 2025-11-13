#include "OmniCaptureNVENCEncoder.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "OmniCaptureTypes.h"
#include "Math/UnrealMathUtility.h"
#include "PixelFormat.h"
#include "RHI.h"
#include "UObject/UnrealType.h"
#include "Logging/LogMacros.h"
#include "Templates/RefCounting.h"
#if __has_include("RHIAdapter.h")
#include "RHIAdapter.h"
#define OMNI_HAS_RHI_ADAPTER 1
#else
#define OMNI_HAS_RHI_ADAPTER 0
#endif
#include "GenericPlatform/GenericPlatformDriver.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogOmniCaptureNVENC, Log, All);

#if OMNI_WITH_NVENC
#include "RHICommandList.h"
#include "RHIResources.h"
#include "NVENC/NVENCDeviceUtilities.h"
#if PLATFORM_WINDOWS
#if OMNI_WITH_D3D11_RHI
#include "D3D11RHI.h"
#endif
#if OMNI_WITH_D3D12_RHI
#include "D3D12RHI.h"
#endif
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/WindowsHWrapper.h"
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#endif

namespace
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    using namespace OmniNVENC;

    struct FNVENCHardwareProbeResult
    {
        bool bDllPresent = false;
        bool bApisReady = false;
        bool bSessionOpenable = false;
        bool bSupportsH264 = false;
        bool bSupportsHEVC = false;
        bool bSupportsNV12 = false;
        bool bSupportsP010 = false;
        bool bSupportsBGRA = false;
        bool bSupports10Bit = false;
        FString DllFailureReason;
        FString ApiFailureReason;
        FString SessionFailureReason;
        FString CodecFailureReason;
        FString NV12FailureReason;
        FString P010FailureReason;
        FString BGRAFailureReason;
        FString HardwareFailureReason;
        FString DriverVersion;
        TMap<ENVENCCodec, FNVENCCapabilities> CodecCapabilities;
    };

    FCriticalSection& GetProbeCacheMutex()
    {
        static FCriticalSection Mutex;
        return Mutex;
    }

    bool& GetProbeValidFlag()
    {
        static bool bValid = false;
        return bValid;
    }

    FNVENCHardwareProbeResult& GetCachedProbe()
    {
        static FNVENCHardwareProbeResult Cached;
        return Cached;
    }

    FString& GetDllOverridePath()
    {
        static FString OverridePath;
        return OverridePath;
    }

    FString& GetRuntimeDirectoryOverride()
    {
        static FString OverridePath;
        return OverridePath;
    }

    FString NormalizePath(const FString& InPath)
    {
        FString Result = InPath;
        Result.TrimStartAndEndInline();
        if (!Result.IsEmpty())
        {
            Result = FPaths::ConvertRelativePathToFull(Result);
            FPaths::MakePlatformFilename(Result);
        }
        return Result;
    }

    FString ResolveRuntimeDirectoryOverride()
    {
        FString OverridePath = NormalizePath(GetRuntimeDirectoryOverride());
        if (OverridePath.IsEmpty())
        {
            return FString();
        }

        if (FPaths::FileExists(OverridePath))
        {
            OverridePath = FPaths::GetPath(OverridePath);
            FPaths::MakePlatformFilename(OverridePath);
        }
        return OverridePath;
    }

    FString ResolveDllOverridePath()
    {
        FString OverridePath = NormalizePath(GetDllOverridePath());
        if (OverridePath.IsEmpty())
        {
            return FString();
        }

        if (FPaths::DirectoryExists(OverridePath))
        {
#if PLATFORM_64BITS
            OverridePath = FPaths::Combine(OverridePath, TEXT("nvEncodeAPI64.dll"));
#else
            OverridePath = FPaths::Combine(OverridePath, TEXT("nvEncodeAPI.dll"));
#endif
        }
        return OverridePath;
    }

    FString FindBundledRuntimeDirectory()
    {
#if PLATFORM_WINDOWS
        if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("OmniCapture")))
        {
            const FString BaseDir = Plugin->GetBaseDir();
            TArray<FString> CandidateDirectories;
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("Binaries/Win64")));
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("Binaries")));
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("Binaries/ThirdParty/Win64")));
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("ThirdParty/NVENC/Win64")));
            CandidateDirectories.Add(FPaths::Combine(BaseDir, TEXT("ThirdParty/NVENC")));

#if PLATFORM_64BITS
            const FString DllName = TEXT("nvEncodeAPI64.dll");
#else
            const FString DllName = TEXT("nvEncodeAPI.dll");
#endif

            for (const FString& CandidateDirectory : CandidateDirectories)
            {
                if (CandidateDirectory.IsEmpty())
                {
                    continue;
                }

                const FString AbsoluteDirectory = FPaths::ConvertRelativePathToFull(CandidateDirectory);
                const FString CandidateDllPath = FPaths::Combine(AbsoluteDirectory, DllName);
                if (FPaths::FileExists(CandidateDllPath))
                {
                    FString NormalizedDirectory = AbsoluteDirectory;
                    FPaths::NormalizeDirectoryName(NormalizedDirectory);
                    return NormalizedDirectory;
                }
            }
        }
#endif
        return FString();
    }

    void ApplyRuntimeOverrides()
    {
        FString RuntimeDirectory = ResolveRuntimeDirectoryOverride();
        if (RuntimeDirectory.IsEmpty())
        {
            RuntimeDirectory = FindBundledRuntimeDirectory();
        }

        FNVENCCommon::SetSearchDirectory(RuntimeDirectory);

        const FString DllPath = ResolveDllOverridePath();
        FNVENCCommon::SetOverrideDllPath(DllPath);
    }

    ERHIInterfaceType GetCurrentRHIType()
    {
        return GDynamicRHI ? GDynamicRHI->GetInterfaceType() : ERHIInterfaceType::Null;
    }

    bool SupportsEnginePixelFormat(EOmniCaptureColorFormat Format)
    {
        switch (Format)
        {
        case EOmniCaptureColorFormat::NV12:
            return GPixelFormats[PF_NV12].Supported != 0;
        case EOmniCaptureColorFormat::P010:
#if defined(PF_P010)
            return GPixelFormats[PF_P010].Supported != 0;
#else
            return false;
#endif
        case EOmniCaptureColorFormat::BGRA:
            return GPixelFormats[PF_B8G8R8A8].Supported != 0;
        default:
            return false;
        }
    }

    ENVENCCodec ToCodec(EOmniCaptureCodec Codec)
    {
        return Codec == EOmniCaptureCodec::HEVC ? ENVENCCodec::HEVC : ENVENCCodec::H264;
    }

    ENVENCBufferFormat ToBufferFormat(EOmniCaptureColorFormat Format)
    {
        switch (Format)
        {
        case EOmniCaptureColorFormat::P010:
            return ENVENCBufferFormat::P010;
        case EOmniCaptureColorFormat::BGRA:
            return ENVENCBufferFormat::BGRA;
        case EOmniCaptureColorFormat::NV12:
        default:
            return ENVENCBufferFormat::NV12;
        }
    }

    ENVENCRateControlMode ToRateControlMode(EOmniCaptureRateControlMode Mode)
    {
        switch (Mode)
        {
        case EOmniCaptureRateControlMode::VariableBitrate:
            return ENVENCRateControlMode::VBR;
        case EOmniCaptureRateControlMode::Lossless:
            return ENVENCRateControlMode::CONSTQP;
        case EOmniCaptureRateControlMode::ConstantBitrate:
        default:
            return ENVENCRateControlMode::CBR;
        }
    }

#if PLATFORM_WINDOWS && OMNI_WITH_D3D12_RHI
    bool CreateProbeD3D12Device(TRefCountPtr<ID3D12Device>& OutDevice, FString& OutFailureReason, FString* OutAdapterName = nullptr)
    {
        DXGI_ADAPTER_DESC1 PreferredAdapterDesc = {};
        TRefCountPtr<IDXGIAdapter1> PreferredAdapter;
        if (TryGetNvidiaAdapter(PreferredAdapter, &PreferredAdapterDesc))
        {
            const HRESULT PreferredResult = D3D12CreateDevice(PreferredAdapter.GetReference(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(OutDevice.GetInitReference()));
            if (SUCCEEDED(PreferredResult))
            {
                const FString AdapterName = FString(PreferredAdapterDesc.Description);
                if (OutAdapterName)
                {
                    *OutAdapterName = AdapterName;
                }
                UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC probe ✓ D3D12 device initialised on adapter: %s"), *AdapterName);
                return true;
            }

            UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("Failed to create D3D12 device on NVIDIA adapter %s (0x%08x)."),
                *FString(PreferredAdapterDesc.Description),
                PreferredResult);
        }

        TRefCountPtr<IDXGIFactory1> DxgiFactory;
        HRESULT Hr = CreateDXGIFactory1(IID_PPV_ARGS(DxgiFactory.GetInitReference()));
        if (FAILED(Hr))
        {
            OutFailureReason = FString::Printf(TEXT("Failed to create DXGI factory for D3D12 probe (0x%08x)."), Hr);
            return false;
        }

        for (UINT AdapterIndex = 0;; ++AdapterIndex)
        {
            TRefCountPtr<IDXGIAdapter1> Adapter;
            Hr = DxgiFactory->EnumAdapters1(AdapterIndex, Adapter.GetInitReference());
            if (Hr == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            if (FAILED(Hr) || !Adapter.IsValid())
            {
                continue;
            }

            DXGI_ADAPTER_DESC1 AdapterDesc;
            if (FAILED(Adapter->GetDesc1(&AdapterDesc)))
            {
                continue;
            }

            if ((AdapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                continue;
            }

            Hr = D3D12CreateDevice(Adapter.GetReference(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(OutDevice.GetInitReference()));
            if (SUCCEEDED(Hr))
            {
                const FString AdapterName = FString(AdapterDesc.Description);
                if (OutAdapterName)
                {
                    *OutAdapterName = AdapterName;
                }
                UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC probe ✓ D3D12 device initialised on adapter: %s"), *AdapterName);
                return true;
            }
        }

        Hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(OutDevice.GetInitReference()));
        if (FAILED(Hr))
        {
            OutFailureReason = FString::Printf(TEXT("Failed to create probing D3D12 device (0x%08x)."), Hr);
            return false;
        }

        if (OutAdapterName)
        {
            *OutAdapterName = TEXT("Default hardware adapter");
        }
        UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC probe ✓ D3D12 device initialised using default hardware selection."));
        return true;
    }
#endif // PLATFORM_WINDOWS && OMNI_WITH_D3D12_RHI

    bool TryCreateProbeSession(ENVENCCodec Codec, ENVENCBufferFormat Format, FString& OutFailureReason)
    {
#if !PLATFORM_WINDOWS
        OutFailureReason = TEXT("NVENC probe requires Windows.");
        return false;
#else

#if OMNI_WITH_D3D11_RHI || OMNI_WITH_D3D12_RHI
        ID3D11Device* Device = nullptr;
        TRefCountPtr<ID3D11Device> LocalDevice;
        TRefCountPtr<ID3D11DeviceContext> LocalContext;
        FString ActiveAdapterName = TEXT("<unknown>");

#if OMNI_WITH_D3D11_RHI
        const uint32 Flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        const D3D_FEATURE_LEVEL FeatureLevels[] =
        {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        DXGI_ADAPTER_DESC1 PreferredDesc = {};
        TRefCountPtr<IDXGIAdapter1> PreferredAdapter;
        if (TryGetNvidiaAdapter(PreferredAdapter, &PreferredDesc))
        {
            ActiveAdapterName = FString(PreferredDesc.Description);
        }
        HRESULT Hr = D3D11CreateDevice(
            PreferredAdapter.IsValid() ? PreferredAdapter.GetReference() : nullptr,
            PreferredAdapter.IsValid() ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            Flags,
            FeatureLevels,
            UE_ARRAY_COUNT(FeatureLevels),
            D3D11_SDK_VERSION,
            LocalDevice.GetInitReference(),
            nullptr,
            LocalContext.GetInitReference());
        if (FAILED(Hr))
        {
            OutFailureReason = FString::Printf(TEXT("Failed to create probing D3D11 device (0x%08x)."), Hr);
            return false;
        }

        if (!PreferredAdapter.IsValid())
        {
            TRefCountPtr<IDXGIDevice> DxgiDevice;
            if (SUCCEEDED(LocalDevice->QueryInterface(IID_PPV_ARGS(DxgiDevice.GetInitReference()))) && DxgiDevice.IsValid())
            {
                TRefCountPtr<IDXGIAdapter> ActiveAdapter;
                if (SUCCEEDED(DxgiDevice->GetAdapter(ActiveAdapter.GetInitReference())) && ActiveAdapter.IsValid())
                {
                    DXGI_ADAPTER_DESC AdapterDesc;
                    if (SUCCEEDED(ActiveAdapter->GetDesc(&AdapterDesc)))
                    {
                        ActiveAdapterName = FString(AdapterDesc.Description);
                    }
                }
            }

            if (ActiveAdapterName == TEXT("<unknown>"))
            {
                ActiveAdapterName = TEXT("Default hardware adapter");
            }
        }

        TRefCountPtr<ID3D11VideoDevice> VideoDevice;
        const HRESULT VideoHr = LocalDevice->QueryInterface(IID_PPV_ARGS(VideoDevice.GetInitReference()));
        if (FAILED(VideoHr) || !VideoDevice.IsValid())
        {
            OutFailureReason = FString::Printf(TEXT("D3D11 device does not expose ID3D11VideoDevice (0x%08x)."), VideoHr);
            UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("NVENC probe ✗ D3D11 device missing ID3D11VideoDevice interface (0x%08x)."), VideoHr);
            return false;
        }

        UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC probe ✓ D3D11 device initialised on adapter: %s"), *ActiveAdapterName);
#elif OMNI_WITH_D3D12_RHI
        const D3D_FEATURE_LEVEL FeatureLevels[] =
        {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };

        TRefCountPtr<ID3D12Device> D3D12Device;
        if (!CreateProbeD3D12Device(D3D12Device, OutFailureReason, &ActiveAdapterName))
        {
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
        QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        QueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        TRefCountPtr<ID3D12CommandQueue> CommandQueue;
        HRESULT Hr = D3D12Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(CommandQueue.GetInitReference()));
        if (FAILED(Hr))
        {
            OutFailureReason = FString::Printf(TEXT("Failed to create D3D12 command queue for NVENC probe (0x%08x)."), Hr);
            return false;
        }

        ID3D12CommandQueue* CommandQueues[] = { CommandQueue.GetReference() };

        Hr = D3D11On12CreateDevice(
            D3D12Device.GetReference(),
            D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
            FeatureLevels,
            UE_ARRAY_COUNT(FeatureLevels),
            reinterpret_cast<IUnknown**>(CommandQueues),
            UE_ARRAY_COUNT(CommandQueues),
            0,
            LocalDevice.GetInitReference(),
            LocalContext.GetInitReference(),
            nullptr);

        if (FAILED(Hr))
        {
            OutFailureReason = FString::Printf(TEXT("D3D11On12CreateDevice failed during NVENC probe (0x%08x)."), Hr);
            return false;
        }

        TRefCountPtr<ID3D11VideoDevice> VideoDevice;
        const HRESULT VideoHr = LocalDevice->QueryInterface(IID_PPV_ARGS(VideoDevice.GetInitReference()));
        if (FAILED(VideoHr) || !VideoDevice.IsValid())
        {
            OutFailureReason = FString::Printf(TEXT("D3D11-on-12 bridge device does not expose ID3D11VideoDevice (0x%08x)."), VideoHr);
            UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("NVENC probe ✗ D3D11-on-12 bridge missing ID3D11VideoDevice interface (0x%08x)."), VideoHr);
            return false;
        }

        if (ActiveAdapterName == TEXT("<unknown>"))
        {
            UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC probe ✓ D3D11-on-12 bridge initialised."));
        }
        else
        {
            UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC probe ✓ D3D11-on-12 bridge initialised on adapter: %s"), *ActiveAdapterName);
        }
#endif // OMNI_WITH_D3D11_RHI

        Device = LocalDevice.GetReference();

        FNVENCSession Session;
        if (!Session.Open(Codec, Device, NV_ENC_DEVICE_TYPE_DIRECTX))
        {
            const FString SessionError = Session.GetLastError();
            OutFailureReason = SessionError.IsEmpty() ? TEXT("Unable to open NVENC session for probe.") : SessionError;
            return false;
        }

        if (!Session.ValidatePresetConfiguration(Codec))
        {
            const FString SessionError = Session.GetLastError();
            OutFailureReason = SessionError.IsEmpty() ? TEXT("Failed to validate NVENC preset configuration during probe.") : SessionError;
            Session.Destroy();
            return false;
        }

        UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC probe ✓ Opened encode session using adapter: %s"), *ActiveAdapterName);

        FNVENCParameters Parameters;
        Parameters.Codec = Codec;
        Parameters.BufferFormat = Format;
        Parameters.Width = 256;
        Parameters.Height = 144;
        Parameters.Framerate = 60;
        Parameters.TargetBitrate = 5 * 1000 * 1000;
        Parameters.MaxBitrate = 10 * 1000 * 1000;
        Parameters.GOPLength = 60;
        Parameters.RateControlMode = ENVENCRateControlMode::CBR;
        Parameters.MultipassMode = ENVENCMultipassMode::DISABLED;

        if (!Session.Initialize(Parameters))
        {
            const FString SessionError = Session.GetLastError();
            OutFailureReason = SessionError.IsEmpty() ? TEXT("Failed to initialise NVENC session during probe.") : SessionError;
            Session.Destroy();
            return false;
        }

        UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC probe ✓ Session initialisation completed."));

        FNVENCBitstream Bitstream;
        if (!Bitstream.Initialize(Session.GetEncoderHandle(), Session.GetFunctionList(), Session.GetApiVersion()))
        {
            OutFailureReason = TEXT("Failed to allocate NVENC bitstream during probe.");
            Session.Destroy();
            return false;
        }

        Session.Destroy();
        return true;
#else
        OutFailureReason = TEXT("D3D11 or D3D12 support is required for NVENC probing in this build.");
        return false;
#endif // OMNI_WITH_D3D11_RHI || OMNI_WITH_D3D12_RHI

#endif // PLATFORM_WINDOWS
    }

    FNVENCHardwareProbeResult RunNVENCHardwareProbe()
    {
        ApplyRuntimeOverrides();

        FNVENCHardwareProbeResult Result;

        Result.DriverVersion = QueryNvidiaDriverVersion();
        if (!Result.DriverVersion.IsEmpty())
        {
            UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC probe ✓ NVIDIA driver version: %s"), *Result.DriverVersion);
        }
        else
        {
            UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC probe could not determine NVIDIA driver version via NVML."));
        }

        const FString RuntimeOverride = ResolveRuntimeDirectoryOverride();
        const FString BundledRuntime = FindBundledRuntimeDirectory();
        const FString ActiveSearchDirectory = OmniNVENC::FNVENCCommon::GetSearchDirectory();
        const FString DllOverride = ResolveDllOverridePath();
        const FString ResolvedDllPath = OmniNVENC::FNVENCCommon::GetResolvedDllPath();
        const bool bDllExists = !ResolvedDllPath.IsEmpty() && FPaths::FileExists(ResolvedDllPath);

        UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC probe starting. Runtime override: %s, bundled runtime: %s, active search dir: %s, DLL override: %s, resolved DLL: %s%s"),
            RuntimeOverride.IsEmpty() ? TEXT("<none>") : *RuntimeOverride,
            BundledRuntime.IsEmpty() ? TEXT("<none>") : *BundledRuntime,
            ActiveSearchDirectory.IsEmpty() ? TEXT("<none>") : *ActiveSearchDirectory,
            DllOverride.IsEmpty() ? TEXT("<none>") : *DllOverride,
            ResolvedDllPath.IsEmpty() ? TEXT("<none>") : *ResolvedDllPath,
            bDllExists ? TEXT("") : TEXT(" (missing)"));

        if (!bDllExists)
        {
            UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("Resolved NVENC runtime path does not exist. The encoder will be unavailable until the DLL is provided."));
        }

        if (!FNVENCCommon::EnsureLoaded())
        {
            Result.DllFailureReason = TEXT("Unable to load nvEncodeAPI runtime.");
            UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("NVENC probe failed to load runtime: %s"), *Result.DllFailureReason);
            return Result;
        }

        Result.bDllPresent = true;
        UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC probe loaded runtime module successfully."));

        FNVEncodeAPILoader& Loader = FNVEncodeAPILoader::Get();
        if (!Loader.Load())
        {
            Result.ApiFailureReason = TEXT("Failed to resolve NVENC exports.");
            UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("NVENC probe failed to resolve exports: %s"), *Result.ApiFailureReason);
            return Result;
        }

        Result.bApisReady = true;
        UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC probe resolved exports successfully."));

        Result.CodecCapabilities.Empty();
        const ENVENCCodec CapabilityCodecs[] = { ENVENCCodec::H264, ENVENCCodec::HEVC };
        for (ENVENCCodec Codec : CapabilityCodecs)
        {
            FNVENCCapabilities CodecCaps;
            const bool bSupported = FNVENCCaps::Query(Codec, CodecCaps);
            Result.CodecCapabilities.Add(Codec, CodecCaps);

            if (bSupported)
            {
                UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC runtime capabilities for %s: %s"),
                    *FNVENCDefs::CodecToString(Codec), *FNVENCCaps::ToDebugString(CodecCaps));
                if (Codec == ENVENCCodec::HEVC)
                {
                    Result.bSupports10Bit = CodecCaps.bSupports10Bit;
                }
            }
            else
            {
                UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC capability probe reported %s as unsupported."),
                    *FNVENCDefs::CodecToString(Codec));
                if (Codec == ENVENCCodec::HEVC && Result.CodecFailureReason.IsEmpty())
                {
                    Result.CodecFailureReason = TEXT("NVENC runtime reported HEVC as unavailable.");
                }
                if (Codec == ENVENCCodec::H264 && Result.SessionFailureReason.IsEmpty())
                {
                    Result.SessionFailureReason = TEXT("NVENC runtime reported H.264 as unavailable.");
                }
            }
        }

        FString SessionFailure;
        if (!TryCreateProbeSession(ENVENCCodec::H264, ENVENCBufferFormat::NV12, SessionFailure))
        {
            Result.SessionFailureReason = SessionFailure;
            Result.HardwareFailureReason = Result.SessionFailureReason;
            UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("NVENC probe could not open a session: %s"), *Result.SessionFailureReason);
            return Result;
        }

        Result.bSessionOpenable = true;
        Result.bSupportsH264 = true;
        Result.bSupportsNV12 = true;
        Result.SessionFailureReason.Empty();
        UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC probe opened H.264/NV12 session successfully."));

        FString Nv12Failure;
        if (TryCreateProbeSession(ENVENCCodec::H264, ENVENCBufferFormat::BGRA, Nv12Failure))
        {
            Result.bSupportsBGRA = true;
            UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC probe verified BGRA upload support."));
        }
        else
        {
            Result.BGRAFailureReason = Nv12Failure;
            UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC probe BGRA session failed: %s"), *Result.BGRAFailureReason);
        }

        FString HevcFailure;
        if (TryCreateProbeSession(ENVENCCodec::HEVC, ENVENCBufferFormat::NV12, HevcFailure))
        {
            Result.bSupportsHEVC = true;
            Result.CodecFailureReason.Empty();
            UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC probe verified HEVC/NV12 support."));
        }
        else
        {
            Result.CodecFailureReason = HevcFailure;
            UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC probe HEVC session failed: %s"), *Result.CodecFailureReason);
        }

        FString P010Failure;
        if (TryCreateProbeSession(ENVENCCodec::HEVC, ENVENCBufferFormat::P010, P010Failure))
        {
            Result.bSupportsP010 = true;
            UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC probe verified HEVC/P010 support."));
        }
        else
        {
            Result.P010FailureReason = P010Failure;
            UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC probe P010 session failed: %s"), *Result.P010FailureReason);
        }

        Result.bSupports10Bit = Result.bSupports10Bit && Result.bSupportsP010;

        UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC probe completed. HEVC:%s NV12:%s P010:%s BGRA:%s 10bit:%s"),
            Result.bSupportsHEVC ? TEXT("yes") : TEXT("no"),
            Result.bSupportsNV12 ? TEXT("yes") : TEXT("no"),
            Result.bSupportsP010 ? TEXT("yes") : TEXT("no"),
            Result.bSupportsBGRA ? TEXT("yes") : TEXT("no"),
            Result.bSupports10Bit ? TEXT("yes") : TEXT("no"));

        return Result;
    }
#endif
}

FOmniCaptureNVENCEncoder::FOmniCaptureNVENCEncoder() = default;
FOmniCaptureNVENCEncoder::~FOmniCaptureNVENCEncoder()
{
    Finalize();
}

bool FOmniCaptureNVENCEncoder::IsNVENCAvailable()
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    FScopeLock Lock(&GetProbeCacheMutex());
    if (!GetProbeValidFlag())
    {
        GetCachedProbe() = RunNVENCHardwareProbe();
        GetProbeValidFlag() = true;
    }
    const FNVENCHardwareProbeResult& Probe = GetCachedProbe();
    return Probe.bDllPresent && Probe.bApisReady && Probe.bSessionOpenable;
#else
    return false;
#endif
}

FOmniNVENCCapabilities FOmniCaptureNVENCEncoder::QueryCapabilities()
{
    FOmniNVENCCapabilities Caps;
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    FScopeLock Lock(&GetProbeCacheMutex());
    if (!GetProbeValidFlag())
    {
        GetCachedProbe() = RunNVENCHardwareProbe();
        GetProbeValidFlag() = true;
    }

    const FNVENCHardwareProbeResult& Probe = GetCachedProbe();
    Caps.bDllPresent = Probe.bDllPresent;
    Caps.bApisReady = Probe.bApisReady;
    Caps.bSessionOpenable = Probe.bSessionOpenable;
    Caps.bSupportsHEVC = Probe.bSupportsHEVC;
    Caps.bSupportsNV12 = Probe.bSupportsNV12 && SupportsEnginePixelFormat(EOmniCaptureColorFormat::NV12);
    Caps.bSupportsP010 = Probe.bSupportsP010 && SupportsEnginePixelFormat(EOmniCaptureColorFormat::P010);
    Caps.bSupportsBGRA = Probe.bSupportsBGRA && SupportsEnginePixelFormat(EOmniCaptureColorFormat::BGRA);
    Caps.bSupports10Bit = Probe.bSupports10Bit && Caps.bSupportsP010;
    Caps.bHardwareAvailable = Caps.bDllPresent && Caps.bApisReady && Caps.bSessionOpenable;
    Caps.DllFailureReason = Probe.DllFailureReason;
    Caps.ApiFailureReason = Probe.ApiFailureReason;
    Caps.SessionFailureReason = Probe.SessionFailureReason;
    Caps.CodecFailureReason = Probe.CodecFailureReason;
    Caps.NV12FailureReason = Probe.NV12FailureReason;
    Caps.P010FailureReason = Probe.P010FailureReason;
    Caps.BGRAFailureReason = Probe.BGRAFailureReason;
    Caps.HardwareFailureReason = Probe.HardwareFailureReason;
    Caps.CodecCapabilities = Probe.CodecCapabilities;
#else
    Caps.bHardwareAvailable = false;
    Caps.DllFailureReason = TEXT("NVENC is only available on Windows builds.");
    Caps.HardwareFailureReason = Caps.DllFailureReason;
#endif

    Caps.AdapterName = FPlatformMisc::GetPrimaryGPUBrand();
#if PLATFORM_WINDOWS
    FString DeviceDescription;
#if OMNI_HAS_RHI_ADAPTER
    if (GDynamicRHI)
    {
        FRHIAdapterInfo AdapterInfo;
        GDynamicRHI->RHIGetAdapterInfo(AdapterInfo);
        DeviceDescription = AdapterInfo.Description;
    }
#endif
    if (DeviceDescription.IsEmpty())
    {
        DeviceDescription = Caps.AdapterName;
    }
    if (!Probe.DriverVersion.IsEmpty())
    {
        Caps.DriverVersion = Probe.DriverVersion;
    }
    else
    {
        const FGPUDriverInfo DriverInfo = FPlatformMisc::GetGPUDriverInfo(DeviceDescription);
#if UE_VERSION_NEWER_THAN(5, 5, 0)
        Caps.DriverVersion = DriverInfo.UserDriverVersion;
#else
        Caps.DriverVersion = DriverInfo.DriverVersion;
#endif
    }
#endif

    return Caps;
}

bool FOmniCaptureNVENCEncoder::SupportsColorFormat(EOmniCaptureColorFormat Format)
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    return SupportsEnginePixelFormat(Format);
#else
    return false;
#endif
}

bool FOmniCaptureNVENCEncoder::SupportsZeroCopyRHI()
{
#if PLATFORM_WINDOWS
    if (!GDynamicRHI)
    {
        return false;
    }

    const ERHIInterfaceType InterfaceType = GDynamicRHI->GetInterfaceType();
#if OMNI_WITH_D3D11_RHI
    if (InterfaceType == ERHIInterfaceType::D3D11)
    {
        return true;
    }
#endif
#if OMNI_WITH_D3D12_RHI
    if (InterfaceType == ERHIInterfaceType::D3D12)
    {
        return true;
    }
#endif
    return false;
#else
    return false;
#endif
}

void FOmniCaptureNVENCEncoder::SetRuntimeDirectoryOverride(const FString& InOverridePath)
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    GetRuntimeDirectoryOverride() = InOverridePath;
    InvalidateCachedCapabilities();
#else
    (void)InOverridePath;
#endif
}

void FOmniCaptureNVENCEncoder::SetDllOverridePath(const FString& InOverridePath)
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    GetDllOverridePath() = InOverridePath;
    InvalidateCachedCapabilities();
#else
    (void)InOverridePath;
#endif
}

void FOmniCaptureNVENCEncoder::InvalidateCachedCapabilities()
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    FScopeLock Lock(&GetProbeCacheMutex());
    GetProbeValidFlag() = false;
    FNVENCCaps::InvalidateCache();
#endif
}

void FOmniCaptureNVENCEncoder::LogRuntimeStatus()
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    ApplyRuntimeOverrides();

    const FString RuntimeOverride = ResolveRuntimeDirectoryOverride();
    const FString BundledRuntime = FindBundledRuntimeDirectory();
    const FString ActiveSearchDirectory = OmniNVENC::FNVENCCommon::GetSearchDirectory();
    const FString DllOverride = ResolveDllOverridePath();
    const FString ResolvedDllPath = OmniNVENC::FNVENCCommon::GetResolvedDllPath();
    const bool bDllExists = !ResolvedDllPath.IsEmpty() && FPaths::FileExists(ResolvedDllPath);

    UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC runtime configuration. Runtime override: %s, bundled runtime: %s, active search dir: %s, DLL override: %s, resolved DLL: %s%s"),
        RuntimeOverride.IsEmpty() ? TEXT("<none>") : *RuntimeOverride,
        BundledRuntime.IsEmpty() ? TEXT("<none>") : *BundledRuntime,
        ActiveSearchDirectory.IsEmpty() ? TEXT("<none>") : *ActiveSearchDirectory,
        DllOverride.IsEmpty() ? TEXT("<none>") : *DllOverride,
        ResolvedDllPath.IsEmpty() ? TEXT("<none>") : *ResolvedDllPath,
        bDllExists ? TEXT("") : TEXT(" (missing)"));

    if (!bDllExists)
    {
        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("NVENC runtime DLL is missing at the resolved path. Encoding will fail until the file is supplied."));
    }
#else
    UE_LOG(LogOmniCaptureNVENC, Display, TEXT("NVENC runtime logging is not available on this platform."));
#endif
}

void FOmniCaptureNVENCEncoder::Initialize(const FOmniCaptureSettings& Settings, const FString& OutputDirectory)
{
    Finalize();

    LastErrorMessage.Reset();
    bInitialized = false;

#if OMNI_WITH_NVENC
    AnnexB.Reset();
    bAnnexBHeaderWritten = false;
#endif

    const FString FileName = FString::Printf(TEXT("%s.%s"), *Settings.OutputFileName, Settings.Codec == EOmniCaptureCodec::HEVC ? TEXT("h265") : TEXT("h264"));
    OutputFilePath = FPaths::Combine(OutputDirectory, FileName);

    ColorFormat = Settings.NVENCColorFormat;
    RequestedCodec = Settings.Codec;
    bZeroCopyRequested = Settings.bZeroCopy;
    ActiveD3D12InteropMode = Settings.D3D12InteropMode;

#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    ApplyRuntimeOverrides();

    if (!IsNVENCAvailable())
    {
        LastErrorMessage = TEXT("NVENC runtime is unavailable. Falling back to image sequence.");
        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("%s"), *LastErrorMessage);
        return;
    }

    if (!SupportsEnginePixelFormat(ColorFormat))
    {
        LastErrorMessage = TEXT("Requested NVENC pixel format is not supported by the engine or GPU.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return;
    }

    if (bZeroCopyRequested && !SupportsZeroCopyRHI())
    {
        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("Zero-copy NVENC capture requested but RHI does not support it. Falling back to auto copy."));
        bZeroCopyRequested = false;
    }

    const FIntPoint OutputSize = Settings.GetOutputResolution();
    ActiveParameters = FNVENCParameters();
    ActiveParameters.Codec = ToCodec(RequestedCodec);
    ActiveParameters.BufferFormat = ToBufferFormat(ColorFormat);
    ActiveParameters.Width = OutputSize.X;
    ActiveParameters.Height = OutputSize.Y;
    ActiveParameters.Framerate = FMath::Clamp<int32>(FMath::RoundToInt(Settings.TargetFrameRate), 1, 120);
    ActiveParameters.TargetBitrate = Settings.Quality.TargetBitrateKbps * 1000;
    ActiveParameters.MaxBitrate = FMath::Max(Settings.Quality.MaxBitrateKbps * 1000, ActiveParameters.TargetBitrate);
    ActiveParameters.RateControlMode = ToRateControlMode(Settings.Quality.RateControlMode);
    ActiveParameters.MultipassMode = Settings.Quality.bLowLatency ? ENVENCMultipassMode::DISABLED : ENVENCMultipassMode::FULL;
    ActiveParameters.GOPLength = Settings.Quality.GOPLength;
    ActiveParameters.bEnableAdaptiveQuantization = Settings.Quality.RateControlMode != EOmniCaptureRateControlMode::Lossless;
    ActiveParameters.bEnableLookahead = !Settings.Quality.bLowLatency;
    if (Settings.Quality.RateControlMode == EOmniCaptureRateControlMode::Lossless)
    {
        ActiveParameters.QPMin = 0;
        ActiveParameters.QPMax = 0;
    }
    else
    {
        ActiveParameters.QPMin = 0;
        ActiveParameters.QPMax = 51;
    }

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    BitstreamFile.Reset(PlatformFile.OpenWrite(*OutputFilePath, /*bAppend=*/false));
    if (!BitstreamFile)
    {
        LastErrorMessage = FString::Printf(TEXT("Unable to open NVENC output file at %s."), *OutputFilePath);
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return;
    }

    bInitialized = true;
    UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC encoder primed – waiting for first frame to initialise session (%dx%d, %s)."),
        ActiveParameters.Width,
        ActiveParameters.Height,
        RequestedCodec == EOmniCaptureCodec::HEVC ? TEXT("HEVC") : TEXT("H.264"));
#else
    (void)Settings;
    (void)OutputDirectory;
    LastErrorMessage = TEXT("NVENC is only available on Windows builds.");
    UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("%s"), *LastErrorMessage);
#endif
}

#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
namespace
{
    ID3D11Texture2D* GetD3D11TextureFromRHI(const FTextureRHIRef& Texture)
    {
        return Texture.IsValid() ? static_cast<ID3D11Texture2D*>(Texture->GetNativeResource()) : nullptr;
    }

#if OMNI_WITH_D3D12_RHI
    ID3D12Resource* GetD3D12ResourceFromRHI(const FTextureRHIRef& Texture)
    {
        return Texture.IsValid() ? static_cast<ID3D12Resource*>(Texture->GetNativeResource()) : nullptr;
    }
#endif
}
#endif // PLATFORM_WINDOWS && OMNI_WITH_NVENC

void FOmniCaptureNVENCEncoder::EnqueueFrame(const FOmniCaptureFrame& Frame)
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    if (!bInitialized || !BitstreamFile)
    {
        return;
    }

    if (Frame.ReadyFence.IsValid())
    {
        while (!Frame.ReadyFence->Poll())
        {
            FPlatformProcess::Sleep(0.0f);
        }
    }

    if (Frame.bUsedCPUFallback)
    {
        UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("Skipping NVENC submission because frame used CPU fallback."));
        return;
    }

    if (!Frame.Texture.IsValid())
    {
        return;
    }

    FScopeLock Lock(&EncoderCS);
    EncodeFrameInternal(Frame);
#else
    (void)Frame;
#endif
}

void FOmniCaptureNVENCEncoder::Finalize()
{
#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
    FScopeLock Lock(&EncoderCS);

    if (BitstreamFile)
    {
        BitstreamFile->Flush();
        BitstreamFile.Reset();
    }

    Bitstream.Release();
    D3D11Input.Shutdown();
    D3D12Input.Shutdown();
    EncoderSession.Flush();
    EncoderSession.Destroy();
    AnnexB.Reset();
    bAnnexBHeaderWritten = false;
#endif

    bInitialized = false;
    LastErrorMessage.Reset();
}

#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
bool FOmniCaptureNVENCEncoder::WriteAnnexBHeader()
{
    if (bAnnexBHeaderWritten || !EncoderSession.IsInitialised())
    {
        return bAnnexBHeaderWritten;
    }

    TArray<uint8> SequenceData;
    if (!EncoderSession.GetSequenceParams(SequenceData) || SequenceData.Num() == 0)
    {
        return false;
    }

    AnnexB.SetCodecConfig(SequenceData);
    const TArray<uint8>& Header = AnnexB.GetCodecConfig();
    if (Header.Num() == 0 || !BitstreamFile)
    {
        return false;
    }

    BitstreamFile->Write(Header.GetData(), Header.Num());
    bAnnexBHeaderWritten = true;
    UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("Wrote NVENC Annex B header (%d bytes)."), Header.Num());
    return true;
}
#else
bool FOmniCaptureNVENCEncoder::WriteAnnexBHeader()
{
    return false;
}
#endif

#if PLATFORM_WINDOWS && OMNI_WITH_NVENC
#if OMNI_WITH_D3D11_RHI
bool FOmniCaptureNVENCEncoder::EncodeFrameD3D11(const FOmniCaptureFrame& Frame)
{
    ID3D11Texture2D* Texture = GetD3D11TextureFromRHI(Frame.Texture);
    if (!Texture)
    {
        LastErrorMessage = TEXT("D3D11 texture was unavailable for NVENC capture.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return false;
    }

    if (!EncoderSession.IsOpen())
    {
        TRefCountPtr<ID3D11Device> Device;
        Texture->GetDevice(Device.GetInitReference());

        if (!Device.IsValid())
        {
            LastErrorMessage = TEXT("Unable to retrieve D3D11 device from capture texture.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!EncoderSession.Open(ActiveParameters.Codec, Device.GetReference(), NV_ENC_DEVICE_TYPE_DIRECTX))
        {
            LastErrorMessage = TEXT("Failed to open NVENC session.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!EncoderSession.ValidatePresetConfiguration(ActiveParameters.Codec))
        {
            LastErrorMessage = EncoderSession.GetLastError().IsEmpty()
                ? TEXT("Failed to validate NVENC preset configuration.")
                : EncoderSession.GetLastError();
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            EncoderSession.Destroy();
            return false;
        }

        if (!EncoderSession.Initialize(ActiveParameters))
        {
            LastErrorMessage = TEXT("Failed to initialise NVENC session.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!Bitstream.Initialize(EncoderSession.GetEncoderHandle(), EncoderSession.GetFunctionList(), EncoderSession.GetApiVersion()))
        {
            LastErrorMessage = TEXT("Failed to create NVENC bitstream buffer.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!D3D11Input.Initialise(Device.GetReference(), EncoderSession))
        {
            LastErrorMessage = TEXT("Failed to initialise NVENC D3D11 input bridge.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!WriteAnnexBHeader())
        {
            UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC did not supply Annex B headers prior to first frame."));
        }

        UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC session initialised (%dx%d)."), ActiveParameters.Width, ActiveParameters.Height);
    }
    else if (!bAnnexBHeaderWritten)
    {
        WriteAnnexBHeader();
    }

    if (!D3D11Input.RegisterResource(Texture))
    {
        LastErrorMessage = TEXT("Failed to register input texture with NVENC.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return false;
    }

    NV_ENC_INPUT_PTR MappedInput = nullptr;
    if (!D3D11Input.MapResource(Texture, MappedInput) || !MappedInput)
    {
        LastErrorMessage = TEXT("Failed to map input texture for NVENC encoding.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return false;
    }

    NV_ENC_PIC_PARAMS PicParams = {};
    PicParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_PIC_PARAMS_VER, EncoderSession.GetApiVersion());
    PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    PicParams.inputBuffer = MappedInput;
    PicParams.bufferFmt = EncoderSession.GetNVBufferFormat();
    PicParams.inputWidth = ActiveParameters.Width;
    PicParams.inputHeight = ActiveParameters.Height;
    PicParams.outputBitstream = Bitstream.GetBitstreamBuffer();
    PicParams.inputTimeStamp = static_cast<uint64>(Frame.Metadata.Timecode * 1'000'000.0);
    PicParams.frameIdx = Frame.Metadata.FrameIndex;
    if (Frame.Metadata.bKeyFrame)
    {
        PicParams.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEINTRA;
    }

    auto EncodePicture = EncoderSession.GetFunctionList().nvEncEncodePicture;
    if (!EncodePicture)
    {
        LastErrorMessage = TEXT("NVENC function table missing nvEncEncodePicture.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        D3D11Input.UnmapResource(MappedInput);
        return false;
    }

    NVENCSTATUS Status = EncodePicture(EncoderSession.GetEncoderHandle(), &PicParams);
    if (Status != NV_ENC_SUCCESS)
    {
        LastErrorMessage = FString::Printf(TEXT("nvEncEncodePicture failed: %s"), *FNVENCDefs::StatusToString(Status));
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        D3D11Input.UnmapResource(MappedInput);
        return false;
    }

    void* BitstreamData = nullptr;
    int32 BitstreamSize = 0;
    if (!Bitstream.Lock(BitstreamData, BitstreamSize))
    {
        LastErrorMessage = TEXT("Failed to lock NVENC bitstream.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        D3D11Input.UnmapResource(MappedInput);
        return false;
    }

    OmniNVENC::FNVENCEncodedPacket Packet;
    if (Bitstream.ExtractPacket(Packet) && Packet.Data.Num() > 0 && BitstreamFile)
    {
        BitstreamFile->Write(Packet.Data.GetData(), Packet.Data.Num());
    }

    Bitstream.Unlock();
    D3D11Input.UnmapResource(MappedInput);
    return true;
}
#endif

#if OMNI_WITH_D3D12_RHI
bool FOmniCaptureNVENCEncoder::EncodeFrameD3D12(const FOmniCaptureFrame& Frame)
{
    ID3D12Resource* Resource = GetD3D12ResourceFromRHI(Frame.Texture);
    if (!Resource)
    {
        LastErrorMessage = TEXT("D3D12 resource was unavailable for NVENC capture.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return false;
    }

    TRefCountPtr<ID3D12Device> Device12;
    HRESULT DeviceResult = Resource->GetDevice(IID_PPV_ARGS(Device12.GetInitReference()));
    if (FAILED(DeviceResult) || !Device12.IsValid())
    {
        LastErrorMessage = TEXT("Unable to retrieve D3D12 device from capture texture.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s (0x%08x)."), *LastErrorMessage, DeviceResult);
        return false;
    }

    OmniNVENC::ENVENCD3D12InteropMode DesiredMode = ActiveD3D12InteropMode == EOmniCaptureNVENCD3D12Interop::Native
        ? OmniNVENC::ENVENCD3D12InteropMode::Native
        : OmniNVENC::ENVENCD3D12InteropMode::Bridge;

    if (D3D12Input.IsInitialised() && D3D12Input.GetInteropMode() != DesiredMode)
    {
        D3D12Input.Shutdown();
    }

    if (!D3D12Input.IsInitialised())
    {
        if (!D3D12Input.Initialise(Device12.GetReference(), DesiredMode))
        {
            if (DesiredMode == OmniNVENC::ENVENCD3D12InteropMode::Native)
            {
                UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("Native D3D12 NVENC interop initialisation failed. Falling back to D3D11-on-12 bridge."));
                ActiveD3D12InteropMode = EOmniCaptureNVENCD3D12Interop::Bridge;
                DesiredMode = OmniNVENC::ENVENCD3D12InteropMode::Bridge;
                if (!D3D12Input.Initialise(Device12.GetReference(), DesiredMode))
                {
                    LastErrorMessage = TEXT("Failed to initialise NVENC D3D12 interop.");
                    UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
                    return false;
                }
            }
            else
            {
                LastErrorMessage = TEXT("Failed to initialise NVENC D3D12 interop.");
                UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
                return false;
            }
        }
    }

    const bool bUsingBridge = D3D12Input.GetInteropMode() == OmniNVENC::ENVENCD3D12InteropMode::Bridge;
    void* SessionDevice = bUsingBridge ? static_cast<void*>(D3D12Input.GetD3D11Device()) : static_cast<void*>(Device12.GetReference());

    if (!SessionDevice)
    {
        LastErrorMessage = bUsingBridge
            ? TEXT("D3D11-on-12 bridge device is unavailable for NVENC capture.")
            : TEXT("D3D12 device was unavailable for NVENC capture.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return false;
    }

    if (!EncoderSession.IsOpen())
    {
        if (!EncoderSession.Open(ActiveParameters.Codec, SessionDevice, NV_ENC_DEVICE_TYPE_DIRECTX))
        {
            LastErrorMessage = TEXT("Failed to open NVENC session.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!EncoderSession.ValidatePresetConfiguration(ActiveParameters.Codec))
        {
            LastErrorMessage = EncoderSession.GetLastError().IsEmpty()
                ? TEXT("Failed to validate NVENC preset configuration.")
                : EncoderSession.GetLastError();
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            EncoderSession.Destroy();
            return false;
        }

        if (!EncoderSession.Initialize(ActiveParameters))
        {
            LastErrorMessage = TEXT("Failed to initialise NVENC session.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!Bitstream.Initialize(EncoderSession.GetEncoderHandle(), EncoderSession.GetFunctionList(), EncoderSession.GetApiVersion()))
        {
            LastErrorMessage = TEXT("Failed to create NVENC bitstream buffer.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!D3D12Input.BindSession(EncoderSession))
        {
            LastErrorMessage = TEXT("Failed to bind NVENC session to D3D12 interop.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!WriteAnnexBHeader())
        {
            UE_LOG(LogOmniCaptureNVENC, Verbose, TEXT("NVENC did not provide Annex B headers before first D3D12 frame."));
        }

        UE_LOG(LogOmniCaptureNVENC, Log, TEXT("NVENC session initialised via %s (%dx%d)."),
            bUsingBridge ? TEXT("D3D11-on-12 bridge") : TEXT("native D3D12"),
            ActiveParameters.Width,
            ActiveParameters.Height);
    }
    else
    {
        if (!D3D12Input.IsSessionBound() && !D3D12Input.BindSession(EncoderSession))
        {
            LastErrorMessage = TEXT("Failed to rebind NVENC session to D3D12 interop.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            return false;
        }

        if (!bAnnexBHeaderWritten)
        {
            WriteAnnexBHeader();
        }
    }

    if (!D3D12Input.RegisterResource(Resource))
    {
        LastErrorMessage = TEXT("Failed to register D3D12 resource with NVENC.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return false;
    }

    NV_ENC_INPUT_PTR MappedInput = nullptr;
    if (!D3D12Input.MapResource(Resource, MappedInput) || !MappedInput)
    {
        LastErrorMessage = TEXT("Failed to map D3D12 resource for NVENC encoding.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        return false;
    }

    NV_ENC_INPUT_RESOURCE_D3D12 InputDescriptor = {};
    NV_ENC_INPUT_PTR SubmissionBuffer = MappedInput;
    if (!bUsingBridge)
    {
        if (!D3D12Input.BuildInputDescriptor(MappedInput, InputDescriptor))
        {
            LastErrorMessage = TEXT("Failed to prepare D3D12 input descriptor for NVENC.");
            UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
            D3D12Input.UnmapResource(MappedInput);
            return false;
        }

        SubmissionBuffer = reinterpret_cast<NV_ENC_INPUT_PTR>(&InputDescriptor);
    }

    NV_ENC_PIC_PARAMS PicParams = {};
    PicParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_PIC_PARAMS_VER, EncoderSession.GetApiVersion());
    PicParams.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    PicParams.inputBuffer = SubmissionBuffer;
    PicParams.bufferFmt = EncoderSession.GetNVBufferFormat();
    PicParams.inputWidth = ActiveParameters.Width;
    PicParams.inputHeight = ActiveParameters.Height;
    PicParams.outputBitstream = Bitstream.GetBitstreamBuffer();
    PicParams.inputTimeStamp = static_cast<uint64>(Frame.Metadata.Timecode * 1'000'000.0);
    PicParams.frameIdx = Frame.Metadata.FrameIndex;
    if (Frame.Metadata.bKeyFrame)
    {
        PicParams.encodePicFlags |= NV_ENC_PIC_FLAG_FORCEINTRA;
    }

    auto EncodePicture = EncoderSession.GetFunctionList().nvEncEncodePicture;
    if (!EncodePicture)
    {
        LastErrorMessage = TEXT("NVENC function table missing nvEncEncodePicture.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        D3D12Input.UnmapResource(MappedInput);
        return false;
    }

    NVENCSTATUS Status = EncodePicture(EncoderSession.GetEncoderHandle(), &PicParams);
    if (Status != NV_ENC_SUCCESS)
    {
        LastErrorMessage = FString::Printf(TEXT("nvEncEncodePicture failed: %s"), *FNVENCDefs::StatusToString(Status));
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        D3D12Input.UnmapResource(MappedInput);
        return false;
    }

    void* BitstreamData = nullptr;
    int32 BitstreamSize = 0;
    if (!Bitstream.Lock(BitstreamData, BitstreamSize))
    {
        LastErrorMessage = TEXT("Failed to lock NVENC bitstream.");
        UE_LOG(LogOmniCaptureNVENC, Error, TEXT("%s"), *LastErrorMessage);
        D3D12Input.UnmapResource(MappedInput);
        return false;
    }

    OmniNVENC::FNVENCEncodedPacket Packet;
    if (Bitstream.ExtractPacket(Packet) && Packet.Data.Num() > 0 && BitstreamFile)
    {
        BitstreamFile->Write(Packet.Data.GetData(), Packet.Data.Num());
    }

    Bitstream.Unlock();
    D3D12Input.UnmapResource(MappedInput);
    return true;
}
#endif

bool FOmniCaptureNVENCEncoder::EncodeFrameInternal(const FOmniCaptureFrame& Frame)
{
    if (!GDynamicRHI)
    {
        return false;
    }

    const ERHIInterfaceType InterfaceType = GDynamicRHI->GetInterfaceType();

#if OMNI_WITH_D3D11_RHI
    if (InterfaceType == ERHIInterfaceType::D3D11)
    {
        return EncodeFrameD3D11(Frame);
    }
#endif

#if OMNI_WITH_D3D12_RHI
    if (InterfaceType == ERHIInterfaceType::D3D12)
    {
        return EncodeFrameD3D12(Frame);
    }
#endif

    UE_LOG(LogOmniCaptureNVENC, Warning, TEXT("NVENC capture is not implemented for RHI interface %d."), static_cast<int32>(InterfaceType));
    return false;
}
#endif // PLATFORM_WINDOWS && OMNI_WITH_NVENC

