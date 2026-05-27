#pragma once

#include "../Core/MathTypes.h"
#include "../Common/CommonTypes.h"
#include "GfxBackendTypes.h"

struct FontData;    // Font.h

struct TextVertex
{
    Float2 pos;
    Float2 uv;
    Color4u color;
};

struct TextGeometry
{
    uint32 numIndices;
    uint32 numVertices;
    MemAllocator* alloc;
    TextVertex* vertices;
    uint32* indices;
};

enum class TextType
{
    Ascii = 0,
    Utf8
};

// The values in this enum map to defines in TextDraw.hlsl
enum class TextEffect : int
{
    None = 0,
    DropShadow,
    Outline
};


struct TextDrawGraphicsObjects
{
    GfxPipelineHandle pipeline;
    GfxPipelineLayoutHandle pipelineLayout;
    GfxSamplerHandle sampler;
};

namespace TextBuilder
{
    TextGeometry CreateText(const FontData& font, Float2 pos, float scale,
                            const char* text, uint32 textLen = 0, 
                            Color4u color = COLOR4U_WHITE, TextType type = TextType::Ascii, 
                            MemAllocator* alloc = Mem::GetDefaultAlloc());
    Float2 CalculateTextSize(const FontData& font, float scale, 
                             const char* text, uint32 textLen = 0, 
                             TextType type = TextType::Ascii);

    void Destroy(TextGeometry& geo);

    TextDrawGraphicsObjects HelperCreateGraphicsObjects(const GfxShader& textDrawShader, TextEffect effect,
                                                        GfxFormat colorAttachmentFmt, GfxFormat depthStencilAttachmentFmt);
} // TextBuilder