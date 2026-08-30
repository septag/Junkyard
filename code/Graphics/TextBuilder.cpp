#include "TextBuilder.h"

#include "../Assets/Font.h"

#include "../Core/StringUtil.h"
#include "../Core/MathAll.h"
#include "../Core/Log.h"

#include "GfxBackend.h"

#define KB_TEXT_SHAPE_IMPLEMENTATION
#define KB_TEXT_SHAPE_NO_CRT
#define KB_TEXT_SHAPE_STATIC
PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4100)    // Unreferenced parameter
PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4505)    // Unreferenced function withi internal linkage has been removed
// #include "../External/kb/kb_text_shape.h"
PRAGMA_DIAGNOSTIC_POP()

static constexpr uint32 TEXT_BUILDER_TAB_SIZE = 2;

namespace TextBuilder
{
    INLINE uint32 _FindCharIndex(const uint16* glyphIds, uint32 numIds, uint16 unicode, uint16 defaultCode = '?')
    {
        for (uint32 i = 0; i < numIds; i++) {
            if (glyphIds[i] == unicode)
                return i;
        }

        if (defaultCode != 0) {
            for (uint32 i = 0; i < numIds; i++) {
                if (glyphIds[i] == defaultCode)
                    return i;
            }
        }

        return uint32(-1);
    }

    INLINE float _GetKerning(const FontKerning* kernings, uint32 numKernings, uint16 firstId, uint16 secondId)
    {
        for (uint32 i = 0; i < numKernings; i++) {
            if (kernings[i].secondId == secondId && kernings[i].firstId == firstId) 
                return kernings[i].xadvance;
        }

        return 0;
    }

    #if 0
    static void _TextShapeAllocate(void*, kbts_allocator_op* op)
    {
        if (op->Kind == KBTS_ALLOCATOR_OP_KIND_ALLOCATE) {
            op->Allocate.Pointer = Mem::Alloc(op->Allocate.Size);
        }
        else if (op->Kind == KBTS_ALLOCATOR_OP_KIND_FREE) {
            Mem::Free(op->Free.Pointer);
        }
    }

    NO_INLINE static kbts_shape_context* _GetOrCreateShapeContext()
    {
        static thread_local kbts_shape_context* ShapeCtx = nullptr;

        if (!ShapeCtx)
            ShapeCtx = kbts_CreateShapeContext(_TextShapeAllocate, nullptr);

        return ShapeCtx;
    }
    #endif        
} // TextBuilder

void TextBuilder::CreateText(TextVertex* outVertices, uint32 maxVertices, uint32* outIndices, uint32 maxIndices,
                             const char* text, uint32 textLen, const FontData& font, Float2 pos, 
                             TextAlignment align, Color4u color, float scale)
{
    if (textLen == 0) 
        textLen = Str::Len(text);
    if (textLen == 0) {
        ASSERT_MSG(0, "Text length cannot be zero");
        return;
    }

    const uint16* glyphIds = font.glyphIds.Get();
    float x = 0;
    float fontSize = float(font.size) * scale;
    float textWidth = 0;
    float yoffset = float(font.descender);
    uint32 numVertices = 0;
    uint32 numIndices = 0;

    MemTempAllocator tempAlloc;
    Array<uint32> chars(&tempAlloc);
    chars.Reserve(textLen);

    uint32 spaceIndex = _FindCharIndex(glyphIds, font.numGlyphs, ' ');
    if (spaceIndex == -1) {
        ASSERT_MSG(0, "Font does not contain space character");
        return;
    }

    for (uint32 i = 0; i < textLen; i++) {
        // Whitespace
        if (text[i] == ' ') {
            textWidth += font.glyphs[spaceIndex].xadvance;
            chars.Push(uint32(-1));
        }
        else if (text[i] == '\r' || text[i] == '\n')    // Ignore new-lines, they should be handled at higher level
            continue;
        else if (text[i] == '\t') {
            for (uint32 s = 0; s < TEXT_BUILDER_TAB_SIZE; s++) {
                textWidth += font.glyphs[spaceIndex].xadvance;
                chars.Push(uint32(-1));
            }
        }
        else {
            // Normal characters
            uint32 charIndex = _FindCharIndex(glyphIds, font.numGlyphs, text[i]);
            if (charIndex == -1) {
                ASSERT_MSG(0, "Character not found: %c", text[i]);
                continue;
            }
            textWidth += font.glyphs[charIndex].xadvance;
            chars.Push(charIndex);

            if (i < textLen - 1)
                textWidth += _GetKerning(font.kernings.Get(), font.numKernings, text[i], text[i + 1]);
        }
    }

    textWidth *= fontSize;
    if (align == TextAlignment::Center)
        pos.x -= textWidth*0.5f;
    else if (align == TextAlignment::Right)
        pos.x -= textWidth;
    
    for (uint32 i = 0; i < chars.Count(); i++) {
        uint32 vertexIndex = numVertices;
        uint32 charIndex = chars[i];

        // Whitespace
        if (charIndex == -1) {
            x += font.glyphs[spaceIndex].xadvance;
            continue;
        }

        const FontGlyph& glyph = font.glyphs[chars[i]];

        // top-left
        outVertices[vertexIndex] = {
            .pos = pos + Float2(x + glyph.planeBounds.xmin, glyph.planeBounds.ymin + yoffset) * fontSize,
            .uv = Float2(glyph.uvBounds.xmin, glyph.uvBounds.ymin),
            .color = color
        };

        // bottom-left
        outVertices[vertexIndex + 1] = {
            .pos = pos + Float2(x + glyph.planeBounds.xmin, glyph.planeBounds.ymax + yoffset) * fontSize,
            .uv = Float2(glyph.uvBounds.xmin, glyph.uvBounds.ymax),
            .color = color
        };

        // bottom-right
        outVertices[vertexIndex + 2] = {
            .pos = pos + Float2(x + glyph.planeBounds.xmax, glyph.planeBounds.ymax + yoffset) * fontSize,
            .uv = Float2(glyph.uvBounds.xmax, glyph.uvBounds.ymax),
            .color = color
        };

        // top-right
        outVertices[vertexIndex + 3] = {
            .pos = pos + Float2(x + glyph.planeBounds.xmax,  glyph.planeBounds.ymin + yoffset) * fontSize,
            .uv = Float2(glyph.uvBounds.xmax, glyph.uvBounds.ymin),
            .color = color
        };

        if (i < chars.Count() - 1 && chars[i + 1] != -1)
            x += _GetKerning(font.kernings.Get(), font.numKernings, glyph.id, font.glyphs[chars[i + 1]].id);

        uint32 indicesIndex = numIndices;
        // Winding: CCW
        // Triangle 1
        outIndices[indicesIndex] = vertexIndex;
        outIndices[indicesIndex + 1] = vertexIndex + 1;
        outIndices[indicesIndex + 2] = vertexIndex + 2;

        // Triangle 2
        outIndices[indicesIndex + 3] = vertexIndex + 2;
        outIndices[indicesIndex + 4] = vertexIndex + 3;
        outIndices[indicesIndex + 5] = vertexIndex;

        x += glyph.xadvance;
        numVertices += 4;
        numIndices += 6;

        if (numVertices >= maxVertices || numIndices >= maxIndices)
            break;
    }
}

Float2 TextBuilder::CalculateTextSize(const FontData& font, float scale, const char* text, uint32 textLen, TextType type)
{
    UNUSED(type);
    ASSERT_MSG(type == TextType::Ascii, "Not implemented");

    if (textLen == 0) 
        textLen = Str::Len(text);
    if (textLen == 0) {
        ASSERT_MSG(0, "Text length cannot be zero");
        return {};
    }

    const uint16* glyphIds = font.glyphIds.Get();
    float fontSize = float(font.size) * scale;
    float textWidth = 0;

    uint32 spaceIndex = _FindCharIndex(glyphIds, font.numGlyphs, ' ');
    if (spaceIndex == -1) {
        ASSERT_MSG(0, "Font does not contain space character");
        return {};
    }

    for (uint32 i = 0; i < textLen; i++) {
        // Whitespace
        if (text[i] == ' ') {
            textWidth += font.glyphs[spaceIndex].xadvance;
        }
        else if (text[i] == '\r' || text[i] == '\n')    // Ignore new-lines, they should be handled at higher level
            continue;
        else if (text[i] == '\t') {
            for (uint32 s = 0; s < TEXT_BUILDER_TAB_SIZE; s++)
                textWidth += font.glyphs[spaceIndex].xadvance;
        }
        else {
            // Normal characters
            uint32 charIndex = _FindCharIndex(glyphIds, font.numGlyphs, text[i]);
            if (charIndex == -1) {
                ASSERT_MSG(0, "Character not found: %c", text[i]);
                continue;
            }
            textWidth += font.glyphs[charIndex].xadvance;

            if (i < textLen - 1)
                textWidth += _GetKerning(font.kernings.Get(), font.numKernings, text[i], text[i + 1]);
        }
    }

    return Float2(textWidth, font.lineHeight) * fontSize;
}

TextDrawGraphicsObjects TextBuilder::HelperCreateGraphicsObjects(const GfxShader& textDrawShader, TextEffect effect,
                                                                 GfxFormat colorAttachmentFmt, GfxFormat depthStencilAttachmentFmt)
{
    UNUSED(effect); // TODO
    GfxSamplerDesc samplerDesc {
        .samplerFilter = GfxSamplerFilterMode::LinearMipmapNearest,
        .samplerWrap = GfxSamplerWrapMode::ClampToEdge
    };
    GfxSamplerHandle sampler = GfxBackend::CreateSampler(samplerDesc);

    GfxVertexBufferBindingDesc vertexBufferBindingDescs[] = {
        {
            .binding = 0,
            .stride = sizeof(TextVertex),
            .inputRate = GfxVertexInputRate::Vertex
        }
    };

    GfxVertexInputAttributeDesc vertexInputAttDescs[] = {
        {
            .semantic = "POSITION",
            .binding = 0,
            .format = GfxFormat::R32G32_SFLOAT,
            .offset = offsetof(TextVertex, pos)
        },
        {
            .semantic = "TEXCOORD",
            .binding = 0,
            .format = GfxFormat::R32G32_SFLOAT,
            .offset = offsetof(TextVertex, uv)
        },
        {
            .semantic = "COLOR",
            .binding = 0,
            .format = GfxFormat::R8G8B8A8_UNORM,
            .offset = offsetof(TextVertex, color)
        }
    };

    GfxPipelineLayoutDesc::Binding bindings[] = {
        {
            .name = "FontTexture",
            .type = GfxDescriptorType::SampledImage,
            .stagesUsed = GfxShaderStage::Fragment
        },
        {
            .name = "FontSampler",
            .type = GfxDescriptorType::Sampler,
            .stagesUsed = GfxShaderStage::Fragment
        }
    };

    GfxPipelineLayoutDesc::PushConstant pushConstants[] = {
        {
            .name = "PerFrameData",
            .stagesUsed = GfxShaderStage::Vertex,
            .size = sizeof(Mat4)
        }
    };

    GfxPipelineLayoutDesc pipelineLayoutDesc {
        .type = GfxPipelineLayoutType::PushDescriptor,
        .numBindings = CountOf(bindings),
        .bindings = bindings,
        .numPushConstants = CountOf(pushConstants),
        .pushConstants = pushConstants,
    };

    GfxPipelineLayoutHandle pipelineLayout = GfxBackend::CreatePipelineLayout(textDrawShader, pipelineLayoutDesc);

    GfxGraphicsPipelineDesc pipelineDesc {
        .numVertexInputAttributes = CountOf(vertexInputAttDescs),
        .vertexInputAttributes = vertexInputAttDescs,
        .numVertexBufferBindings = CountOf(vertexBufferBindingDescs),
        .vertexBufferBindings = vertexBufferBindingDescs,
        .rasterizer = {
            .cullMode = GfxCullMode::Back
        },
        .blend = {
            .numAttachments = 1,
            .attachments = GfxBlendAttachmentDesc::GetAlphaBlending()
        },
        .numColorAttachments = 1,
        .colorAttachmentFormats = { colorAttachmentFmt },
        .depthAttachmentFormat = depthStencilAttachmentFmt,
        .stencilAttachmentFormat = depthStencilAttachmentFmt
    };

    GfxShaderPermutationVar permut("TextEffect", int(effect));
    GfxPipelineHandle pipeline = GfxBackend::CreateGraphicsPipeline(textDrawShader, pipelineLayout, pipelineDesc, 1, &permut);

    TextDrawGraphicsObjects r {
        .pipeline = pipeline,
        .pipelineLayout = pipelineLayout,
        .sampler = sampler
    };

    return r;
}