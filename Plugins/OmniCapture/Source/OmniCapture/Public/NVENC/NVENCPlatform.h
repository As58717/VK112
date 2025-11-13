// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// Provide consistent compile-time switches that describe whether the NVENC
// implementation is allowed to use the D3D11 and D3D12 backends.  When the
// build scripts define explicit values we honour them, otherwise we fall back
// to the engine wide WITH_D3D* macros so the behaviour stays compatible with
// older configurations.

#if !defined(OMNI_WITH_D3D11_RHI)
    #if defined(WITH_D3D11_RHI)
        #define OMNI_WITH_D3D11_RHI WITH_D3D11_RHI
    #else
        #define OMNI_WITH_D3D11_RHI 0
    #endif
#endif

#if !defined(OMNI_WITH_D3D12_RHI)
    #if defined(WITH_D3D12_RHI)
        #define OMNI_WITH_D3D12_RHI WITH_D3D12_RHI
    #else
        #define OMNI_WITH_D3D12_RHI 0
    #endif
#endif

