#pragma once

#include "../Core/MathTypes.h"

struct TextVertex
{
    Float3 pos;
    Float2 uv;
    Color4u color;
};

struct TextGeometry
{
    TextVertex* vertices;
    uint32* indices;
    uint32 numIndices;
    uint32 numVertices;
};

namespace TextBuilder
{

    TextGeometry Create(const char* text, MemAllocator* alloc = Mem::GetDefaultAlloc());
    void Destroy(TextGeometry& geo, MemAllocator* alloc = Mem::GetDefaultAlloc());
} // TextBuilder