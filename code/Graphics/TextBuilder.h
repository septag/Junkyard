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

enum class TextAlignment 
{
    Left = 0,
    Center,
    Right
};

struct TextDrawGraphicsObjects
{
    GfxPipelineHandle pipeline;
    GfxPipelineLayoutHandle pipelineLayout;
    GfxSamplerHandle sampler;
};

namespace TextBuilder
{
    // numIndices = textLen * 6
    // numVertices = textLen * 4
    void CreateText(TextVertex* outVertices, uint32 maxVertices, uint32* outIndices, uint32 maxIndices,
                    const char* text, uint32 textLen, const FontData& font, Float2 pos, 
                    TextAlignment align = TextAlignment::Left, Color4u color = COLOR4U_WHITE, float scale = 1);
    Float2 CalculateTextSize(const FontData& font, float scale, 
                             const char* text, uint32 textLen = 0, 
                             TextType type = TextType::Ascii);

    TextDrawGraphicsObjects HelperCreateGraphicsObjects(const GfxShader& textDrawShader, TextEffect effect,
                                                        GfxFormat colorAttachmentFmt, GfxFormat depthStencilAttachmentFmt);
} // TextBuilder