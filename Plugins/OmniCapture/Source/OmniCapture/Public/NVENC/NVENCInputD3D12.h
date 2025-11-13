// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_OMNI_NVENC

#include "CoreMinimal.h"
#include "Containers/Map.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
#include "Windows/HideWindowsPlatformTypes.h"
#include "nvEncodeAPI.h"
#endif

namespace OmniNVENC
{
    class FNVENCSession;
#if PLATFORM_WINDOWS
    class FNVENCInputD3D11;

    enum class ENVENCD3D12InteropMode : uint8
    {
        Bridge,
        Native
    };

    class FNVENCInputD3D12
    {
    public:
        FNVENCInputD3D12();

        /** Creates the bridge or native interop required to surface D3D12 textures to NVENC. */
        bool Initialise(ID3D12Device* InDevice, ENVENCD3D12InteropMode InMode);

        /** Binds an NVENC session so that resources can be registered and mapped. */
        bool BindSession(FNVENCSession& InSession);

        void Shutdown();

        bool IsInitialised() const { return bIsInitialised; }
        bool IsSessionBound() const { return bSessionBound; }
        bool IsValid() const { return bIsInitialised && bSessionBound; }

        ENVENCD3D12InteropMode GetInteropMode() const { return InteropMode; }

        ID3D11Device* GetD3D11Device() const { return D3D11Device.GetReference(); }

        bool RegisterResource(ID3D12Resource* InRHITexture);
        void UnregisterResource(ID3D12Resource* InRHITexture);
        bool MapResource(ID3D12Resource* InRHITexture, NV_ENC_INPUT_PTR& OutMappedResource);
        void UnmapResource(NV_ENC_INPUT_PTR InMappedResource);
        bool BuildInputDescriptor(NV_ENC_INPUT_PTR InMappedResource, NV_ENC_INPUT_RESOURCE_D3D12& OutDescriptor);

    private:
        struct FWrappedResource
        {
            TRefCountPtr<ID3D12Resource> D3D12Resource;
            TRefCountPtr<ID3D11Texture2D> D3D11Texture;
        };

        struct FNativeResource
        {
            NV_ENC_REGISTERED_PTR Handle = nullptr;
            D3D12_RESOURCE_DESC Description = {};
            uint64 LastSubmittedFenceValue = 0;
        };

        struct FNativeMapping
        {
            ID3D12Resource* Resource = nullptr;
            uint64 PendingFenceSignal = 0;
        };

        bool InitialiseBridge(ID3D12Device* InDevice);
        bool InitialiseNative(ID3D12Device* InDevice);

        bool EnsureWrappedResource(ID3D12Resource* InResource, ID3D11Texture2D*& OutTexture);
        bool EnsureNativeResource(ID3D12Resource* InResource, FNativeResource*& OutResource);
        void ReleaseActiveMapping(NV_ENC_INPUT_PTR InMappedResource);
        void ReleaseBridgeMapping(NV_ENC_INPUT_PTR InMappedResource);
        void ReleaseNativeMapping(NV_ENC_INPUT_PTR InMappedResource);
        void ResetBridge();
        void ResetNative();

        TRefCountPtr<ID3D12Device> D3D12Device;
        TRefCountPtr<ID3D12CommandQueue> CommandQueue;
        TRefCountPtr<ID3D11Device> D3D11Device;
        TRefCountPtr<ID3D11DeviceContext> D3D11Context;
        TRefCountPtr<ID3D11On12Device> D3D11On12Device;
        TRefCountPtr<ID3D12Fence> Fence;
        uint64 NextFenceValue = 1;
        void* FenceEvent = nullptr;
        FNVENCInputD3D11* D3D11Bridge = nullptr;
        FNVENCSession* Session = nullptr;
        ENVENCD3D12InteropMode InteropMode = ENVENCD3D12InteropMode::Bridge;
        TMap<ID3D12Resource*, FWrappedResource> WrappedResources;
        TMap<NV_ENC_INPUT_PTR, ID3D11Texture2D*> ActiveBridgeMappings;
        TMap<ID3D12Resource*, FNativeResource> NativeResources;
        TMap<NV_ENC_INPUT_PTR, FNativeMapping> ActiveNativeMappings;
        uint32 ApiVersion = NVENCAPI_VERSION;
        bool bIsInitialised = false;
        bool bSessionBound = false;
    };
#else
    class FNVENCInputD3D12
    {
    public:
        FNVENCInputD3D12() = default;

        bool Initialise(void*) { return false; }
        bool BindSession(FNVENCSession&) { return false; }
        void Shutdown() {}

        bool IsInitialised() const { return false; }
        bool IsSessionBound() const { return false; }
        bool IsValid() const { return false; }

        ID3D11Device* GetD3D11Device() const { return nullptr; }

        bool RegisterResource(void*) { return false; }
        void UnregisterResource(void*) {}
        bool MapResource(void*, NV_ENC_INPUT_PTR& OutMappedResource) { OutMappedResource = nullptr; return false; }
        void UnmapResource(NV_ENC_INPUT_PTR) {}
        bool BuildInputDescriptor(NV_ENC_INPUT_PTR, NV_ENC_INPUT_RESOURCE_D3D12&) { return false; }
    };
#endif
}

#endif // WITH_OMNI_NVENC

