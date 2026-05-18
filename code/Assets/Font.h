#pragma once

#include "../Core/MathTypes.h"
#include "../Common/CommonTypes.h"

struct AssetGroup;

struct FontGlyph
{
    float xadvance;
    RectFloat planeBounds;
    RectFloat uvBounds;
};

struct FontKerning
{
    uint16 firstId;
    uint16 secondId;
    float xadvance;
};

// SERIALIZED
struct FontData
{
    char name[32];
    uint32 size;
    AssetHandleImage atlas;
    uint32 atlasWidth;
    uint32 atlasHeight;
    float ascender;
    float descender;
    float underlineY;
    float lineHeight;
    uint32 numGlyphs;
    uint32 numKernings;
    uint32 fontSourceSize;
    RelativePtr<uint16> glyphIds;
    RelativePtr<FontGlyph> glyphs;
    RelativePtr<FontKerning> kernings;
    RelativePtr<uint8> fontSourceData;  // TTF/OTF file
};

namespace Font
{
    API bool InitializeManager();
    API void ReleaseManager();

    // DataType: AssetObjPtrScope<FontData>
    API AssetHandleFont Load(const char* path, const AssetGroup& group);
}