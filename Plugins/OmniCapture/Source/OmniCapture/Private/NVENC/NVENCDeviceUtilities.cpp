// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCDeviceUtilities.h"

#if WITH_OMNI_NVENC

#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeExit.h"
#include "Containers/StringConv.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNVENCDeviceUtilities, Log, All);

namespace OmniNVENC
{
#if PLATFORM_WINDOWS
    namespace
    {
        FString AdapterDescriptionToString(const DXGI_ADAPTER_DESC1& Desc)
        {
            return FString(Desc.Description);
        }

        bool IsSoftwareAdapter(const DXGI_ADAPTER_DESC1& Desc)
        {
            return (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        }
    }

    bool TryGetNvidiaAdapter(TRefCountPtr<IDXGIAdapter1>& OutAdapter, DXGI_ADAPTER_DESC1* OutDesc)
    {
        OutAdapter = nullptr;

        TRefCountPtr<IDXGIFactory1> DxgiFactory;
        HRESULT Hr = CreateDXGIFactory1(IID_PPV_ARGS(DxgiFactory.GetInitReference()));
        if (FAILED(Hr))
        {
            UE_LOG(LogNVENCDeviceUtilities, Warning, TEXT("CreateDXGIFactory1 failed while searching for NVIDIA adapter (0x%08x)."), Hr);
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

            if (IsSoftwareAdapter(AdapterDesc))
            {
                continue;
            }

            const FString Description = AdapterDescriptionToString(AdapterDesc);
            if (Description.Contains(TEXT("NVIDIA"), ESearchCase::IgnoreCase))
            {
                OutAdapter = Adapter;
                if (OutDesc)
                {
                    *OutDesc = AdapterDesc;
                }

                UE_LOG(LogNVENCDeviceUtilities, Log, TEXT("Selected NVIDIA adapter '%s' (LUID: 0x%08x%08x)."),
                    *Description,
                    static_cast<uint32>(AdapterDesc.AdapterLuid.HighPart),
                    static_cast<uint32>(AdapterDesc.AdapterLuid.LowPart));
                return true;
            }
        }

        UE_LOG(LogNVENCDeviceUtilities, Verbose, TEXT("No NVIDIA adapter detected while initialising NVENC."));
        return false;
    }

    enum class ENVMLStatus : uint32
    {
        Success = 0,
    };

    using TNvmlInit = ENVMLStatus(__cdecl*)();
    using TNvmlShutdown = ENVMLStatus(__cdecl*)();
    using TNvmlSystemGetDriverVersion = ENVMLStatus(__cdecl*)(char*, unsigned int);

    FString QueryNvidiaDriverVersion()
    {
        void* NvmlHandle = FPlatformProcess::GetDllHandle(TEXT("nvml.dll"));
        if (!NvmlHandle)
        {
            UE_LOG(LogNVENCDeviceUtilities, Verbose, TEXT("nvml.dll not found – unable to query NVIDIA driver version."));
            return FString();
        }

        ON_SCOPE_EXIT
        {
            FPlatformProcess::FreeDllHandle(NvmlHandle);
        };

        TNvmlInit NvmlInit = reinterpret_cast<TNvmlInit>(FPlatformProcess::GetDllExport(NvmlHandle, TEXT("nvmlInit_v2")));
        TNvmlSystemGetDriverVersion NvmlGetDriverVersion = reinterpret_cast<TNvmlSystemGetDriverVersion>(FPlatformProcess::GetDllExport(NvmlHandle, TEXT("nvmlSystemGetDriverVersion")));
        TNvmlShutdown NvmlShutdown = reinterpret_cast<TNvmlShutdown>(FPlatformProcess::GetDllExport(NvmlHandle, TEXT("nvmlShutdown")));

        if (!NvmlInit || !NvmlGetDriverVersion)
        {
            UE_LOG(LogNVENCDeviceUtilities, Verbose, TEXT("nvml.dll is missing required exports – cannot query driver version."));
            return FString();
        }

        const ENVMLStatus InitStatus = NvmlInit();
        if (InitStatus != ENVMLStatus::Success)
        {
            UE_LOG(LogNVENCDeviceUtilities, Verbose, TEXT("nvmlInit_v2 failed (status=%u)."), static_cast<uint32>(InitStatus));
            return FString();
        }

        ON_SCOPE_EXIT
        {
            if (NvmlShutdown)
            {
                NvmlShutdown();
            }
        };

        char DriverVersionBuffer[96] = {};
        const ENVMLStatus VersionStatus = NvmlGetDriverVersion(DriverVersionBuffer, UE_ARRAY_COUNT(DriverVersionBuffer));
        if (VersionStatus != ENVMLStatus::Success)
        {
            UE_LOG(LogNVENCDeviceUtilities, Verbose, TEXT("nvmlSystemGetDriverVersion failed (status=%u)."), static_cast<uint32>(VersionStatus));
            return FString();
        }

        const FString DriverVersion = UTF8_TO_TCHAR(DriverVersionBuffer);
        UE_LOG(LogNVENCDeviceUtilities, Verbose, TEXT("Detected NVIDIA driver version %s via NVML."), *DriverVersion);
        return DriverVersion;
    }
#endif // PLATFORM_WINDOWS
}

#endif // WITH_OMNI_NVENC

