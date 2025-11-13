#pragma once

// ── UTextureRenderTarget2D ───────────────────────────────────
#if __has_include("Engine/TextureRenderTarget2D.h")
    #include "Engine/TextureRenderTarget2D.h"
#elif __has_include("TextureRenderTarget2D.h")
    #include "TextureRenderTarget2D.h"
#else
    class UTextureRenderTarget2D;
#endif

// ── FTextureRenderTargetResource ───────────────────────
#if __has_include("Rendering/TextureRenderTargetResource.h")
    #include "Rendering/TextureRenderTargetResource.h"
#elif __has_include("Engine/TextureRenderTargetResource.h")
    #include "Engine/TextureRenderTargetResource.h"
#elif __has_include("Engine/TextureRenderTarget.h")
    #include "Engine/TextureRenderTarget.h"
#elif __has_include("TextureRenderTargetResource.h")
    #include "TextureRenderTargetResource.h"
#elif __has_include("TextureRenderTarget.h")
    #include "TextureRenderTarget.h"
#else
    class FTextureRenderTargetResource;
#endif

// ── FTexture2DResource ────────────────────────────────
#if __has_include("Rendering/Texture2DResource.h")
    #include "Rendering/Texture2DResource.h"
#elif __has_include("Engine/Texture2DResource.h")
    #include "Engine/Texture2DResource.h"
#elif __has_include("Rendering/TextureResource.h")
    #include "Rendering/TextureResource.h"
#elif __has_include("Texture2DResource.h")
    #include "Texture2DResource.h"
#elif __has_include("TextureResource.h")
    #include "TextureResource.h"
#else
    class FTexture2DResource;
#endif
