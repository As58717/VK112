// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_OMNI_NVENC

#include "CoreMinimal.h"
#include "Containers/Map.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <d3d11.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#include "nvEncodeAPI.h"

struct ID3D11Device;
struct ID3D11Texture2D;

namespace OmniNVENC
{
    class FNVENCSession;

#if PLATFORM_WINDOWS
    class FNVENCInputD3D11
    {
    public:
        FNVENCInputD3D11();

        bool Initialise(ID3D11Device* InDevice, FNVENCSession& InSession);
        void Shutdown();

        bool IsValid() const { return bIsInitialised; }

        bool RegisterResource(ID3D11Texture2D* InRHITexture);
        void UnregisterResource(ID3D11Texture2D* InRHITexture);
        bool MapResource(ID3D11Texture2D* InRHITexture, NV_ENC_INPUT_PTR& OutMappedResource);
        void UnmapResource(NV_ENC_INPUT_PTR InMappedResource);

    private:
        struct FRegisteredResource
        {
            NV_ENC_REGISTERED_PTR Handle = nullptr;
            D3D11_TEXTURE2D_DESC Description = {};
        };

        ID3D11Device* Device = nullptr;
        FNVENCSession* Session = nullptr;
        TMap<ID3D11Texture2D*, FRegisteredResource> RegisteredResources;
        TMap<NV_ENC_INPUT_PTR, ID3D11Texture2D*> ActiveMappings;
        bool bIsInitialised = false;
        uint32 ApiVersion = NVENCAPI_VERSION;
    };
#else
    class FNVENCInputD3D11
    {
    public:
        FNVENCInputD3D11() = default;

        bool Initialise(ID3D11Device*, FNVENCSession&) { return false; }
        void Shutdown() {}

        bool IsValid() const { return false; }

        bool RegisterResource(ID3D11Texture2D*) { return false; }
        void UnregisterResource(ID3D11Texture2D*) {}
        bool MapResource(ID3D11Texture2D*, NV_ENC_INPUT_PTR& OutMappedResource) { OutMappedResource = nullptr; return false; }
        void UnmapResource(NV_ENC_INPUT_PTR) {}
    };
#endif
}

#endif // WITH_OMNI_NVENC

