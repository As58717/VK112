// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCInputD3D11.h"

#if WITH_OMNI_NVENC

#include "NVENC/NVENCSession.h"
#include "NVENC/NVENCDefs.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogNVENCInputD3D11, Log, All);

namespace OmniNVENC
{
    namespace
    {
#if PLATFORM_WINDOWS
        template <typename TFunc>
        bool ValidateFunction(const ANSICHAR* Name, TFunc* Function)
        {
            if (!Function)
            {
                UE_LOG(LogNVENCInputD3D11, Error, TEXT("Required NVENC export '%s' is missing."), ANSI_TO_TCHAR(Name));
                return false;
            }
            return true;
        }
#endif
    }

    FNVENCInputD3D11::FNVENCInputD3D11() = default;

    bool FNVENCInputD3D11::Initialise(ID3D11Device* InDevice, FNVENCSession& InSession)
    {
#if !PLATFORM_WINDOWS
        UE_LOG(LogNVENCInputD3D11, Warning, TEXT("NVENC D3D11 input bridge is only available on Windows."));
        return false;
#else
        if (bIsInitialised)
        {
            return true;
        }

        if (!InDevice)
        {
            UE_LOG(LogNVENCInputD3D11, Error, TEXT("Cannot initialise NVENC D3D11 input without a valid device."));
            return false;
        }

        Device = InDevice;
        Device->AddRef();
        Session = &InSession;
        ApiVersion = InSession.GetApiVersion();
        bIsInitialised = true;
        return true;
#endif
    }

    void FNVENCInputD3D11::Shutdown()
    {
#if PLATFORM_WINDOWS
        if (!bIsInitialised)
        {
            return;
        }

        TArray<NV_ENC_INPUT_PTR> MappedKeys;
        ActiveMappings.GenerateKeyArray(MappedKeys);
        for (NV_ENC_INPUT_PTR Ptr : MappedKeys)
        {
            UnmapResource(Ptr);
        }
        ActiveMappings.Empty();

        TArray<ID3D11Texture2D*> RegisteredKeys;
        RegisteredResources.GenerateKeyArray(RegisteredKeys);
        for (ID3D11Texture2D* Texture : RegisteredKeys)
        {
            UnregisterResource(Texture);
        }
        RegisteredResources.Empty();

        if (Device)
        {
            Device->Release();
            Device = nullptr;
        }
        Session = nullptr;
        ApiVersion = NVENCAPI_VERSION;
        bIsInitialised = false;
#endif
    }

    bool FNVENCInputD3D11::RegisterResource(ID3D11Texture2D* InRHITexture)
    {
#if !PLATFORM_WINDOWS
        return false;
#else
        if (!bIsInitialised || !Session || !Session->IsInitialised() || !InRHITexture)
        {
            return false;
        }

        if (RegisteredResources.Contains(InRHITexture))
        {
            return true;
        }

        D3D11_TEXTURE2D_DESC Desc = {};
        InRHITexture->GetDesc(&Desc);

        using TNvEncRegisterResource = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_REGISTER_RESOURCE*);
        TNvEncRegisterResource RegisterResourceFn = Session->GetFunctionList().nvEncRegisterResource;
        if (!ValidateFunction("NvEncRegisterResource", RegisterResourceFn))
        {
            return false;
        }

        NV_ENC_REGISTER_RESOURCE RegisterParams = {};
        RegisterParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_REGISTER_RESOURCE_VER, ApiVersion);
        RegisterParams.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        RegisterParams.resourceToRegister = InRHITexture;
        RegisterParams.width = Desc.Width;
        RegisterParams.height = Desc.Height;
        RegisterParams.pitch = 0;
        RegisterParams.bufferFormat = Session->GetNVBufferFormat();
        RegisterParams.bufferUsage = NV_ENC_INPUT_IMAGE;

        NVENCSTATUS Status = RegisterResourceFn(Session->GetEncoderHandle(), &RegisterParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCInputD3D11, Error, TEXT("NvEncRegisterResource failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        FRegisteredResource ResourceInfo;
        ResourceInfo.Handle = RegisterParams.registeredResource;
        ResourceInfo.Description = Desc;
        RegisteredResources.Add(InRHITexture, ResourceInfo);
        return true;
#endif
    }

    void FNVENCInputD3D11::UnregisterResource(ID3D11Texture2D* InRHITexture)
    {
#if PLATFORM_WINDOWS
        if (!bIsInitialised || !Session || !InRHITexture)
        {
            return;
        }

        FRegisteredResource ResourceInfo;
        if (!RegisteredResources.RemoveAndCopyValue(InRHITexture, ResourceInfo))
        {
            return;
        }

        for (auto It = ActiveMappings.CreateIterator(); It; ++It)
        {
            if (It.Value() == InRHITexture)
            {
                UnmapResource(It.Key());
                It.RemoveCurrent();
            }
        }

        using TNvEncUnregisterResource = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_REGISTERED_PTR);
        TNvEncUnregisterResource UnregisterResourceFn = Session->GetFunctionList().nvEncUnregisterResource;
        if (UnregisterResourceFn && ResourceInfo.Handle)
        {
            NVENCSTATUS Status = UnregisterResourceFn(Session->GetEncoderHandle(), ResourceInfo.Handle);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCInputD3D11, Warning, TEXT("NvEncUnregisterResource returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }
#endif
    }

    bool FNVENCInputD3D11::MapResource(ID3D11Texture2D* InRHITexture, NV_ENC_INPUT_PTR& OutMappedResource)
    {
#if !PLATFORM_WINDOWS
        OutMappedResource = nullptr;
        return false;
#else
        OutMappedResource = nullptr;

        if (!bIsInitialised || !Session || !InRHITexture)
        {
            return false;
        }

        FRegisteredResource* ResourceInfo = RegisteredResources.Find(InRHITexture);
        if (!ResourceInfo)
        {
            if (!RegisterResource(InRHITexture))
            {
                return false;
            }
            ResourceInfo = RegisteredResources.Find(InRHITexture);
            if (!ResourceInfo)
            {
                return false;
            }
        }

        using TNvEncMapInputResource = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_MAP_INPUT_RESOURCE*);
        TNvEncMapInputResource MapResourceFn = Session->GetFunctionList().nvEncMapInputResource;
        if (!ValidateFunction("NvEncMapInputResource", MapResourceFn))
        {
            return false;
        }

        NV_ENC_MAP_INPUT_RESOURCE MapParams = {};
        MapParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_MAP_INPUT_RESOURCE_VER, ApiVersion);
        MapParams.registeredResource = ResourceInfo->Handle;

        NVENCSTATUS Status = MapResourceFn(Session->GetEncoderHandle(), &MapParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCInputD3D11, Error, TEXT("NvEncMapInputResource failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        OutMappedResource = MapParams.mappedResource;
        ActiveMappings.Add(MapParams.mappedResource, InRHITexture);
        return true;
#endif
    }

    void FNVENCInputD3D11::UnmapResource(NV_ENC_INPUT_PTR InMappedResource)
    {
#if PLATFORM_WINDOWS
        if (!bIsInitialised || !Session || !InMappedResource)
        {
            return;
        }

        using TNvEncUnmapInputResource = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_INPUT_PTR);
        TNvEncUnmapInputResource UnmapResourceFn = Session->GetFunctionList().nvEncUnmapInputResource;
        if (UnmapResourceFn)
        {
            NVENCSTATUS Status = UnmapResourceFn(Session->GetEncoderHandle(), InMappedResource);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCInputD3D11, Warning, TEXT("NvEncUnmapInputResource returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }

        ActiveMappings.Remove(InMappedResource);
#endif
    }
}

#endif // WITH_OMNI_NVENC

