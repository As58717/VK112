// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct IDXGIAdapter1;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D12Device;
struct DXGI_ADAPTER_DESC1;

namespace OmniNVENC
{
#if PLATFORM_WINDOWS
    /** Attempts to locate an NVIDIA adapter in the current system. */
    bool TryGetNvidiaAdapter(TRefCountPtr<IDXGIAdapter1>& OutAdapter, DXGI_ADAPTER_DESC1* OutDesc = nullptr);

    /** Queries the installed NVIDIA driver version via NVML. */
    FString QueryNvidiaDriverVersion();
#endif
}

