#pragma once

#include "../Core/TracyHelper.h"

FORCE_INLINE constexpr uint32 ProfilerMakeColor(uint8 R, uint8 G, uint8 B)
{
    return (uint32(R)<<16) | (uint32(G)<<8) | uint32(B) | 0xff000000;
}

inline constexpr uint32 PROFILE_COLOR_ASSET1 = ProfilerMakeColor(204, 0, 204);
inline constexpr uint32 PROFILE_COLOR_ASSET2 = ProfilerMakeColor(153, 0, 153);
inline constexpr uint32 PROFILE_COLOR_ASSET3 = ProfilerMakeColor(102, 0, 102);

inline constexpr uint32 PROFILE_COLOR_GFX1 = ProfilerMakeColor(218, 90, 50);
inline constexpr uint32 PROFILE_COLOR_GFX2 = ProfilerMakeColor(166, 68, 38);
inline constexpr uint32 PROFILE_COLOR_GFX3 = ProfilerMakeColor(115, 47, 26);


