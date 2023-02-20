#pragma once

#include "Core/HandlePool.h"

// Memory
struct Allocator;

// Graphics
DEFINE_HANDLE(GfxBuffer);
DEFINE_HANDLE(GfxImage);
DEFINE_HANDLE(GfxPipeline);
DEFINE_HANDLE(GfxPipelineLayout);
DEFINE_HANDLE(GfxRenderPass);
DEFINE_HANDLE(GfxDescriptorSet);

// AssetManager
DEFINE_HANDLE(AssetHandle);
DEFINE_HANDLE(AssetBarrier);

// Grahics assets
struct AssetHandleImage : AssetHandle {};
struct AssetHandleShader : AssetHandle {};
struct AssetHandleModel : AssetHandle {};

