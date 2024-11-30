#pragma once

#include "../Core/Pools.h"

static inline constexpr uint32 JUNKYARD_VERSION = MakeVersion(0, 1, 0);

// Memory
struct MemAllocator;

// Graphics
DEFINE_HANDLE(GfxBufferHandle);
DEFINE_HANDLE(GfxImageHandle);
DEFINE_HANDLE(GfxPipelineHandle);
DEFINE_HANDLE(GfxPipelineLayoutHandle);
DEFINE_HANDLE(GfxDescriptorSetLayoutHandle);
DEFINE_HANDLE(GfxRenderPassHandle);
DEFINE_HANDLE(GfxDescriptorSetHandle);

// AssetManager
DEFINE_HANDLE(AssetHandle);
DEFINE_HANDLE(AssetBarrier);

// Grahics assets
using AssetHandleImage = AssetHandle;
using AssetHandleShader = AssetHandle;
using AssetHandleModel = AssetHandle;

// Clipboard
DEFINE_HANDLE(ClipboardVarHandle);

