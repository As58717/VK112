// Copyright Epic Games, Inc. All Rights Reserved.

#include "NVENC/NVENCInputD3D12.h"

#if WITH_OMNI_NVENC

#include "NVENC/NVENCInputD3D11.h"
#include "NVENC/NVENCSession.h"
#include "NVENC/NVENCDefs.h"
#include "Logging/LogMacros.h"

#if PLATFORM_WINDOWS
    #include "Windows/AllowWindowsPlatformTypes.h"
    #include <windows.h>
    #include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNVENCInputD3D12, Log, All);

namespace OmniNVENC
{
#if PLATFORM_WINDOWS
    namespace
    {
        template <typename TFunc>
        bool ValidateFunction(const ANSICHAR* Name, TFunc* Function)
        {
            if (!Function)
            {
                UE_LOG(LogNVENCInputD3D12, Error, TEXT("Required NVENC export '%s' is missing."), ANSI_TO_TCHAR(Name));
                return false;
            }
            return true;
        }

        NV_ENC_INPUT_RESOURCE_TYPE GetDirectX12ResourceType()
        {
#if defined(NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX12)
            return NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX12;
#else
            return static_cast<NV_ENC_INPUT_RESOURCE_TYPE>(0x4);
#endif
        }
    }

    FNVENCInputD3D12::FNVENCInputD3D12() = default;

    bool FNVENCInputD3D12::Initialise(ID3D12Device* InDevice, ENVENCD3D12InteropMode InMode)
    {
        if (bIsInitialised && InteropMode == InMode)
        {
            return true;
        }

        Shutdown();

        if (!InDevice)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Cannot initialise NVENC D3D12 interop without a valid device."));
            return false;
        }

        D3D12Device = InDevice;

        D3D12_COMMAND_QUEUE_DESC QueueDesc = {};
        QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        QueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        HRESULT QueueResult = D3D12Device->CreateCommandQueue(&QueueDesc, IID_PPV_ARGS(CommandQueue.GetInitReference()));
        if (FAILED(QueueResult))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to create D3D12 command queue for NVENC interop (0x%08x)."), QueueResult);
            Shutdown();
            return false;
        }

        InteropMode = InMode;
        const bool bInitialised = (InteropMode == ENVENCD3D12InteropMode::Bridge)
            ? InitialiseBridge(InDevice)
            : InitialiseNative(InDevice);

        if (!bInitialised)
        {
            Shutdown();
            return false;
        }

        bIsInitialised = true;
        return true;
    }

    bool FNVENCInputD3D12::InitialiseBridge(ID3D12Device* InDevice)
    {
        ID3D12CommandQueue* CommandQueues[] = { CommandQueue.GetReference() };

        const D3D_FEATURE_LEVEL FeatureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        UINT DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

        HRESULT BridgeResult = D3D11On12CreateDevice(
            InDevice,
            DeviceFlags,
            FeatureLevels,
            UE_ARRAY_COUNT(FeatureLevels),
            reinterpret_cast<IUnknown**>(CommandQueues),
            UE_ARRAY_COUNT(CommandQueues),
            0,
            D3D11Device.GetInitReference(),
            D3D11Context.GetInitReference(),
            nullptr);

        if (FAILED(BridgeResult))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("D3D11On12CreateDevice failed (0x%08x)."), BridgeResult);
            return false;
        }

        TRefCountPtr<ID3D11VideoDevice> VideoDevice;
        const HRESULT VideoResult = D3D11Device->QueryInterface(IID_PPV_ARGS(VideoDevice.GetInitReference()));
        if (FAILED(VideoResult) || !VideoDevice.IsValid())
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("D3D11-on-12 bridge is missing ID3D11VideoDevice interface (0x%08x)."), VideoResult);
            return false;
        }

        HRESULT QueryResult = D3D11Device->QueryInterface(IID_PPV_ARGS(D3D11On12Device.GetInitReference()));
        if (FAILED(QueryResult))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to acquire ID3D11On12Device interface (0x%08x)."), QueryResult);
            return false;
        }

        return true;
    }

    bool FNVENCInputD3D12::InitialiseNative(ID3D12Device* InDevice)
    {
        HRESULT FenceResult = InDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(Fence.GetInitReference()));
        if (FAILED(FenceResult))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to create D3D12 fence for NVENC interop (0x%08x)."), FenceResult);
            return false;
        }

        HANDLE EventHandle = ::CreateEvent(nullptr, false, false, nullptr);
        if (!EventHandle)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to create fence event for NVENC interop (0x%08x)."), ::GetLastError());
            Fence = nullptr;
            return false;
        }

        FenceEvent = EventHandle;
        NextFenceValue = 1;
        return true;
    }

    bool FNVENCInputD3D12::BindSession(FNVENCSession& InSession)
    {
        if (!bIsInitialised)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Cannot bind NVENC session – D3D12 interop is not initialised."));
            return false;
        }

        if (bSessionBound && Session == &InSession)
        {
            return true;
        }

        Session = &InSession;
        ApiVersion = InSession.GetApiVersion();

        if (InteropMode == ENVENCD3D12InteropMode::Bridge)
        {
            if (!D3D11Bridge)
            {
                D3D11Bridge = new FNVENCInputD3D11();
            }

            if (!D3D11Bridge->Initialise(D3D11Device.GetReference(), InSession))
            {
                UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to initialise NVENC D3D11 bridge for D3D12 input."));
                return false;
            }
        }

        bSessionBound = true;
        return true;
    }

    void FNVENCInputD3D12::Shutdown()
    {
        if (!bIsInitialised)
        {
            return;
        }

        {
            TArray<NV_ENC_INPUT_PTR> MappingKeys;
            ActiveBridgeMappings.GenerateKeyArray(MappingKeys);
            for (NV_ENC_INPUT_PTR Mapping : MappingKeys)
            {
                ReleaseBridgeMapping(Mapping);
            }
            ActiveBridgeMappings.Empty();
        }

        {
            TArray<NV_ENC_INPUT_PTR> NativeKeys;
            ActiveNativeMappings.GenerateKeyArray(NativeKeys);
            for (NV_ENC_INPUT_PTR Mapping : NativeKeys)
            {
                ReleaseNativeMapping(Mapping);
            }
            ActiveNativeMappings.Empty();
        }

        ResetBridge();
        ResetNative();

        CommandQueue = nullptr;
        D3D12Device = nullptr;
        Session = nullptr;
        ApiVersion = NVENCAPI_VERSION;
        InteropMode = ENVENCD3D12InteropMode::Bridge;
        bSessionBound = false;
        bIsInitialised = false;
    }

    void FNVENCInputD3D12::ResetBridge()
    {
        for (auto It = WrappedResources.CreateIterator(); It; ++It)
        {
            if (D3D11Bridge)
            {
                D3D11Bridge->UnregisterResource(It.Value().D3D11Texture.GetReference());
            }

            if (D3D11On12Device.IsValid() && It.Value().D3D11Texture.IsValid())
            {
                ID3D11Resource* const ReleaseResource[] = { It.Value().D3D11Texture.GetReference() };
                D3D11On12Device->ReleaseWrappedResources(ReleaseResource, 1);
            }
        }
        WrappedResources.Empty();

        if (D3D11Bridge)
        {
            D3D11Bridge->Shutdown();
            delete D3D11Bridge;
            D3D11Bridge = nullptr;
        }

        if (D3D11Context.IsValid())
        {
            D3D11Context->Flush();
        }

        D3D11On12Device = nullptr;
        D3D11Context = nullptr;
        D3D11Device = nullptr;
    }

    void FNVENCInputD3D12::ResetNative()
    {
        if (Session && Session->IsOpen())
        {
            using TNvEncUnregisterResource = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_REGISTERED_PTR);
            TNvEncUnregisterResource UnregisterResourceFn = Session->GetFunctionList().nvEncUnregisterResource;
            if (UnregisterResourceFn)
            {
                for (auto& Pair : NativeResources)
                {
                    if (Pair.Value.Handle)
                    {
                        UnregisterResourceFn(Session->GetEncoderHandle(), Pair.Value.Handle);
                    }
                }
            }
        }

        NativeResources.Empty();

        if (FenceEvent)
        {
            ::CloseHandle(static_cast<HANDLE>(FenceEvent));
            FenceEvent = nullptr;
        }

        Fence = nullptr;
        NextFenceValue = 1;
    }

    bool FNVENCInputD3D12::RegisterResource(ID3D12Resource* InRHITexture)
    {
        if (!IsValid() || !InRHITexture)
        {
            return false;
        }

        if (InteropMode == ENVENCD3D12InteropMode::Bridge)
        {
            ID3D11Texture2D* WrappedTexture = nullptr;
            return EnsureWrappedResource(InRHITexture, WrappedTexture);
        }

        FNativeResource* Resource = nullptr;
        return EnsureNativeResource(InRHITexture, Resource);
    }

    void FNVENCInputD3D12::UnregisterResource(ID3D12Resource* InRHITexture)
    {
        if (!IsValid() || !InRHITexture)
        {
            return;
        }

        if (InteropMode == ENVENCD3D12InteropMode::Bridge)
        {
            FWrappedResource Resource;
            if (!WrappedResources.RemoveAndCopyValue(InRHITexture, Resource))
            {
                return;
            }

            if (D3D11Bridge)
            {
                D3D11Bridge->UnregisterResource(Resource.D3D11Texture.GetReference());
            }

            if (D3D11On12Device.IsValid() && Resource.D3D11Texture.IsValid())
            {
                ID3D11Resource* const ReleaseResource[] = { Resource.D3D11Texture.GetReference() };
                D3D11On12Device->ReleaseWrappedResources(ReleaseResource, 1);
            }
            return;
        }

        FNativeResource Resource;
        if (!NativeResources.RemoveAndCopyValue(InRHITexture, Resource))
        {
            return;
        }

        for (auto It = ActiveNativeMappings.CreateIterator(); It; ++It)
        {
            if (It.Value().Resource == InRHITexture)
            {
                ReleaseNativeMapping(It.Key());
                It.RemoveCurrent();
            }
        }

        using TNvEncUnregisterResource = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_REGISTERED_PTR);
        TNvEncUnregisterResource UnregisterResourceFn = Session ? Session->GetFunctionList().nvEncUnregisterResource : nullptr;
        if (UnregisterResourceFn && Resource.Handle)
        {
            NVENCSTATUS Status = UnregisterResourceFn(Session->GetEncoderHandle(), Resource.Handle);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCInputD3D12, Warning, TEXT("NvEncUnregisterResource returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }
    }

    bool FNVENCInputD3D12::MapResource(ID3D12Resource* InRHITexture, NV_ENC_INPUT_PTR& OutMappedResource)
    {
        OutMappedResource = nullptr;

        if (!IsValid() || !Session || !Session->IsInitialised() || !InRHITexture)
        {
            return false;
        }

        if (InteropMode == ENVENCD3D12InteropMode::Bridge)
        {
            ID3D11Texture2D* WrappedTexture = nullptr;
            if (!EnsureWrappedResource(InRHITexture, WrappedTexture))
            {
                return false;
            }

            if (!D3D11On12Device.IsValid())
            {
                return false;
            }

            ID3D11Resource* const AcquireResources[] = { WrappedTexture };
            D3D11On12Device->AcquireWrappedResources(AcquireResources, 1);

            NV_ENC_INPUT_PTR NvResource = nullptr;
            if (!D3D11Bridge->MapResource(WrappedTexture, NvResource))
            {
                D3D11On12Device->ReleaseWrappedResources(AcquireResources, 1);
                return false;
            }

            ActiveBridgeMappings.Add(NvResource, WrappedTexture);
            OutMappedResource = NvResource;
            return true;
        }

        FNativeResource* Resource = nullptr;
        if (!EnsureNativeResource(InRHITexture, Resource))
        {
            return false;
        }

        if (Fence.IsValid() && Resource->LastSubmittedFenceValue > 0)
        {
            const uint64 CompletedValue = Fence->GetCompletedValue();
            if (CompletedValue < Resource->LastSubmittedFenceValue && FenceEvent)
            {
                Fence->SetEventOnCompletion(Resource->LastSubmittedFenceValue, static_cast<HANDLE>(FenceEvent));
                ::WaitForSingleObject(static_cast<HANDLE>(FenceEvent), INFINITE);
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
        MapParams.registeredResource = Resource->Handle;

        NVENCSTATUS Status = MapResourceFn(Session->GetEncoderHandle(), &MapParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("NvEncMapInputResource failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        FNativeMapping Mapping;
        Mapping.Resource = InRHITexture;
        ActiveNativeMappings.Add(MapParams.mappedResource, Mapping);
        OutMappedResource = MapParams.mappedResource;
        return true;
    }

    void FNVENCInputD3D12::UnmapResource(NV_ENC_INPUT_PTR InMappedResource)
    {
        ReleaseActiveMapping(InMappedResource);
    }

    bool FNVENCInputD3D12::BuildInputDescriptor(NV_ENC_INPUT_PTR InMappedResource, NV_ENC_INPUT_RESOURCE_D3D12& OutDescriptor)
    {
        if (InteropMode != ENVENCD3D12InteropMode::Native)
        {
            return false;
        }

        FNativeMapping* Mapping = ActiveNativeMappings.Find(InMappedResource);
        if (!Mapping)
        {
            return false;
        }

        FNativeResource* Resource = NativeResources.Find(Mapping->Resource);
        if (!Resource)
        {
            return false;
        }

        FMemory::Memzero(&OutDescriptor, sizeof(NV_ENC_INPUT_RESOURCE_D3D12));
        OutDescriptor.version = FNVENCDefs::PatchStructVersion(NV_ENC_INPUT_RESOURCE_D3D12_VER, ApiVersion);
        OutDescriptor.pInputBuffer = InMappedResource;

        FMemory::Memzero(&OutDescriptor.inputFencePoint, sizeof(NV_ENC_FENCE_POINT_D3D12));
        OutDescriptor.inputFencePoint.version = FNVENCDefs::PatchStructVersion(NV_ENC_FENCE_POINT_D3D12_VER, ApiVersion);

        if (Fence.IsValid())
        {
            OutDescriptor.inputFencePoint.pFence = Fence.GetReference();
            OutDescriptor.inputFencePoint.waitValue = 0;
            OutDescriptor.inputFencePoint.bWait = 0;
            const uint64 SignalValue = ++NextFenceValue;
            OutDescriptor.inputFencePoint.signalValue = SignalValue;
            OutDescriptor.inputFencePoint.bSignal = 1;
            Mapping->PendingFenceSignal = SignalValue;
            Resource->LastSubmittedFenceValue = SignalValue;
        }
        else
        {
            Mapping->PendingFenceSignal = 0;
            Resource->LastSubmittedFenceValue = 0;
        }

        return true;
    }

    bool FNVENCInputD3D12::EnsureWrappedResource(ID3D12Resource* InResource, ID3D11Texture2D*& OutTexture)
    {
        OutTexture = nullptr;

        if (!IsValid() || !InResource)
        {
            return false;
        }

        if (FWrappedResource* Existing = WrappedResources.Find(InResource))
        {
            OutTexture = Existing->D3D11Texture.GetReference();
            return OutTexture != nullptr;
        }

        if (!D3D11On12Device.IsValid())
        {
            return false;
        }

        D3D11_RESOURCE_FLAGS Flags = {};
        TRefCountPtr<ID3D11Resource> WrappedResource;
        HRESULT Result = D3D11On12Device->CreateWrappedResource(
            InResource,
            &Flags,
            D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
            D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
            IID_PPV_ARGS(WrappedResource.GetInitReference()));

        if (FAILED(Result))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("CreateWrappedResource failed for %p (0x%08x)."), InResource, Result);
            return false;
        }

        TRefCountPtr<ID3D11Texture2D> WrappedTexture;
        Result = WrappedResource->QueryInterface(IID_PPV_ARGS(WrappedTexture.GetInitReference()));
        if (FAILED(Result))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to query ID3D11Texture2D for wrapped resource (0x%08x)."), Result);
            return false;
        }

        FWrappedResource ResourceEntry;
        ResourceEntry.D3D12Resource = InResource;
        ResourceEntry.D3D11Texture = WrappedTexture;

        if (!D3D11Bridge || !D3D11Bridge->RegisterResource(WrappedTexture.GetReference()))
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Failed to register wrapped D3D12 texture with NVENC."));
            return false;
        }

        WrappedResources.Add(InResource, ResourceEntry);
        OutTexture = WrappedTexture.GetReference();
        return true;
    }

    bool FNVENCInputD3D12::EnsureNativeResource(ID3D12Resource* InResource, FNativeResource*& OutResource)
    {
        OutResource = nullptr;

        if (!IsValid() || !Session || !Session->IsInitialised() || !InResource)
        {
            return false;
        }

        if (FNativeResource* Existing = NativeResources.Find(InResource))
        {
            OutResource = Existing;
            return true;
        }

        D3D12_RESOURCE_DESC Desc = InResource->GetDesc();
        if (Desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("Unsupported D3D12 resource dimension for NVENC registration."));
            return false;
        }

        using TNvEncRegisterResource = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_REGISTER_RESOURCE*);
        TNvEncRegisterResource RegisterResourceFn = Session->GetFunctionList().nvEncRegisterResource;
        if (!ValidateFunction("NvEncRegisterResource", RegisterResourceFn))
        {
            return false;
        }

        NV_ENC_REGISTER_RESOURCE RegisterParams = {};
        RegisterParams.version = FNVENCDefs::PatchStructVersion(NV_ENC_REGISTER_RESOURCE_VER, ApiVersion);
        RegisterParams.resourceType = GetDirectX12ResourceType();
        RegisterParams.resourceToRegister = InResource;
        RegisterParams.width = static_cast<uint32>(Desc.Width);
        RegisterParams.height = Desc.Height;
        RegisterParams.pitch = 0;
        RegisterParams.subResourceIndex = 0;
        RegisterParams.bufferFormat = Session->GetNVBufferFormat();
        RegisterParams.bufferUsage = NV_ENC_INPUT_IMAGE;
        RegisterParams.pInputFencePoint = nullptr;

        NVENCSTATUS Status = RegisterResourceFn(Session->GetEncoderHandle(), &RegisterParams);
        if (Status != NV_ENC_SUCCESS)
        {
            UE_LOG(LogNVENCInputD3D12, Error, TEXT("NvEncRegisterResource failed: %s"), *FNVENCDefs::StatusToString(Status));
            return false;
        }

        FNativeResource ResourceEntry;
        ResourceEntry.Handle = RegisterParams.registeredResource;
        ResourceEntry.Description = Desc;
        ResourceEntry.LastSubmittedFenceValue = 0;

        FNativeResource& Stored = NativeResources.Add(InResource, ResourceEntry);
        OutResource = &Stored;
        return true;
    }

    void FNVENCInputD3D12::ReleaseActiveMapping(NV_ENC_INPUT_PTR InMappedResource)
    {
        if (!InMappedResource)
        {
            return;
        }

        if (InteropMode == ENVENCD3D12InteropMode::Bridge)
        {
            ReleaseBridgeMapping(InMappedResource);
        }
        else
        {
            ReleaseNativeMapping(InMappedResource);
        }
    }

    void FNVENCInputD3D12::ReleaseBridgeMapping(NV_ENC_INPUT_PTR InMappedResource)
    {
        if (!D3D11Bridge)
        {
            return;
        }

        ID3D11Texture2D** WrappedTexturePtr = ActiveBridgeMappings.Find(InMappedResource);
        if (!WrappedTexturePtr || !*WrappedTexturePtr)
        {
            return;
        }

        ID3D11Texture2D* WrappedTexture = *WrappedTexturePtr;
        ID3D11Resource* const ReleaseResources[] = { WrappedTexture };

        D3D11Bridge->UnmapResource(InMappedResource);

        if (D3D11On12Device.IsValid())
        {
            D3D11On12Device->ReleaseWrappedResources(ReleaseResources, 1);
        }

        if (D3D11Context.IsValid())
        {
            D3D11Context->Flush();
        }

        ActiveBridgeMappings.Remove(InMappedResource);
    }

    void FNVENCInputD3D12::ReleaseNativeMapping(NV_ENC_INPUT_PTR InMappedResource)
    {
        FNativeMapping Mapping;
        if (!ActiveNativeMappings.RemoveAndCopyValue(InMappedResource, Mapping))
        {
            return;
        }

        using TNvEncUnmapInputResource = NVENCSTATUS(NVENCAPI*)(void*, NV_ENC_INPUT_PTR);
        TNvEncUnmapInputResource UnmapResourceFn = Session ? Session->GetFunctionList().nvEncUnmapInputResource : nullptr;
        if (UnmapResourceFn)
        {
            NVENCSTATUS Status = UnmapResourceFn(Session->GetEncoderHandle(), InMappedResource);
            if (Status != NV_ENC_SUCCESS)
            {
                UE_LOG(LogNVENCInputD3D12, Warning, TEXT("NvEncUnmapInputResource returned %s"), *FNVENCDefs::StatusToString(Status));
            }
        }
    }
#else
    FNVENCInputD3D12::FNVENCInputD3D12() = default;
#endif
}

#endif // WITH_OMNI_NVENC
