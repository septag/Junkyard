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

TextGeometry TextBuilder::CreateText(const FontData& font, Float2 pos, float scale, const char* text, uint32 textLen, 
                                     Color4u color, TextType type, MemAllocator* alloc)
{
    ASSERT(alloc);
    UNUSED(type);
    ASSERT_MSG(type == TextType::Ascii, "Not implemented");

    if (textLen == 0) 
        textLen = Str::Len(text);
    if (textLen == 0) {
        ASSERT_MSG(0, "Text length cannot be zero");
        return {};
    }

    TextGeometry geo {
        .alloc = alloc,
        .vertices = Mem::AllocTyped<TextVertex>(textLen*4, alloc),
        .indices = Mem::AllocTyped<uint32>(textLen*6, alloc)
    };

    const uint16* glyphIds = font.glyphIds.Get();
    float x = 0;
    float y = 0;
    float fontSize = float(font.size) * scale;
    float yoffset = float(font.descender);
    uint32 numVertices = 0;
    uint32 numIndices = 0;

    for (uint32 i = 0; i < textLen; i++) {
        // Whitespace
        if (text[i] == ' ') {
            uint32 spaceIndex = _FindCharIndex(glyphIds, font.numGlyphs, text[i]);
            ASSERT_MSG(spaceIndex != -1, "Font does not contain space character");
            x += font.glyphs[spaceIndex].xadvance;
            continue;
        }
        else if (text[i] == '\r')
            continue;
        else if (text[i] == '\t') {
            uint32 spaceIndex = _FindCharIndex(glyphIds, font.numGlyphs, text[i]);
            ASSERT_MSG(spaceIndex != -1, "Font does not contain space character");
            for (uint32 s = 0; s < TEXT_BUILDER_TAB_SIZE; s++)
                x += font.glyphs[spaceIndex].xadvance;
            continue;
        }
        else if (text[i] == '\n') {
            y += font.lineHeight;
            x = 0;
            continue;
        }

        // Normal characters
        uint32 charIndex = _FindCharIndex(glyphIds, font.numGlyphs, text[i]);
        if (charIndex == -1) {
            ASSERT_MSG(0, "Character not found: %c", text[i]);
            continue;
        }

        uint32 vertexIndex = numVertices;
        const FontGlyph& glyph = font.glyphs[charIndex];

        // top-left
        geo.vertices[vertexIndex] = {
            .pos = pos + Float2(x + glyph.planeBounds.xmin, y + glyph.planeBounds.ymin + yoffset) * fontSize,
            .uv = Float2(glyph.uvBounds.xmin, glyph.uvBounds.ymin),
            .color = color
        };

        // bottom-left
        geo.vertices[vertexIndex + 1] = {
            .pos = pos + Float2(x + glyph.planeBounds.xmin, y + glyph.planeBounds.ymax + yoffset) * fontSize,
            .uv = Float2(glyph.uvBounds.xmin, glyph.uvBounds.ymax),
            .color = color
        };

        // bottom-right
        geo.vertices[vertexIndex + 2] = {
            .pos = pos + Float2(x + glyph.planeBounds.xmax, y + glyph.planeBounds.ymax + yoffset) * fontSize,
            .uv = Float2(glyph.uvBounds.xmax, glyph.uvBounds.ymax),
            .color = color
        };

        // top-right
        geo.vertices[vertexIndex + 3] = {
            .pos = pos + Float2(x + glyph.planeBounds.xmax, y + glyph.planeBounds.ymin + yoffset) * fontSize,
            .uv = Float2(glyph.uvBounds.xmax, glyph.uvBounds.ymin),
            .color = color
        };

        if (i < textLen - 1)
            x += _GetKerning(font.kernings.Get(), font.numKernings, text[i], text[i + 1]);

        uint32 indicesIndex = numIndices;
        // Winding: CCW
        // Triangle 1
        geo.indices[indicesIndex] = vertexIndex;
        geo.indices[indicesIndex + 1] = vertexIndex + 1;
        geo.indices[indicesIndex + 2] = vertexIndex + 2;

        // Triangle 2
        geo.indices[indicesIndex + 3] = vertexIndex + 2;
        geo.indices[indicesIndex + 4] = vertexIndex + 3;
        geo.indices[indicesIndex + 5] = vertexIndex;

        x += glyph.xadvance;
        numVertices += 4;
        numIndices += 6;
    }

    geo.numVertices = numVertices;
    geo.numIndices = numIndices;

    return geo;
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
    float x = 0;
    float y = 0;
    float fontSize = float(font.size) * scale;
    float yoffset = float(font.descender);
    RectFloat bounds = RECTFLOAT_EMPTY;

    for (uint32 i = 0; i < textLen; i++) {
        // Whitespace
        if (text[i] == ' ') {
            uint32 spaceIndex = _FindCharIndex(glyphIds, font.numGlyphs, text[i]);
            ASSERT_MSG(spaceIndex != -1, "Font does not contain space character");
            x += font.glyphs[spaceIndex].xadvance;
            continue;
        }
        else if (text[i] == '\r')
            continue;
        else if (text[i] == '\t') {
            uint32 spaceIndex = _FindCharIndex(glyphIds, font.numGlyphs, text[i]);
            ASSERT_MSG(spaceIndex != -1, "Font does not contain space character");
            for (uint32 s = 0; s < TEXT_BUILDER_TAB_SIZE; s++)
                x += font.glyphs[spaceIndex].xadvance;
            continue;
        }
        else if (text[i] == '\n') {
            y += font.lineHeight;
            x = 0;
            continue;
        }

        // Normal characters
        uint32 charIndex = _FindCharIndex(glyphIds, font.numGlyphs, text[i]);
        if (charIndex == -1) {
            ASSERT_MSG(0, "Character not found: %c", text[i]);
            continue;
        }

        const FontGlyph& glyph = font.glyphs[charIndex];

        Float2 vmin = Float2(x + glyph.planeBounds.xmin, y + glyph.planeBounds.ymin + yoffset) * fontSize;
        Float2 vmax = Float2(x + glyph.planeBounds.xmax, y + glyph.planeBounds.ymax + yoffset) * fontSize;
        RectFloat::AddPoint(bounds, vmin);
        RectFloat::AddPoint(bounds, vmax);

        if (i < textLen - 1)
            x += _GetKerning(font.kernings.Get(), font.numKernings, text[i], text[i + 1]);

        x += glyph.xadvance;
    }

    return Float2(bounds.Width(), bounds.Height());
}

void TextBuilder::Destroy(TextGeometry& geo)
{
    if (geo.alloc) {
        Mem::Free(geo.vertices, geo.alloc);
        Mem::Free(geo.indices, geo.alloc);
        geo.alloc = nullptr;
    }
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