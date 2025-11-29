#pragma once

//    ____ ___  __  __ __  __  ___  _   _ 
//   / ___/ _ \|  \/  |  \/  |/ _ \| \ | |
//  | |  | | | | |\/| | |\/| | | | |  \| |
//  | |__| |_| | |  | | |  | | |_| | |\  |
//   \____\___/|_|  |_|_|  |_|\___/|_| \_|
//                                        
#include "../Common/CommonTypes.h"

#include "../Core/MathTypes.h"
#include "../Core/StringUtil.h"

inline constexpr uint32 GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS = 8;
inline constexpr uint32 GFXBACKEND_MAX_MIPS_PER_IMAGE = 12;  // up to 4096
inline constexpr uint32 GFXBACKEND_MAX_SHADER_MUTATION_VARS = 4;

enum class GfxFormat: uint32
{
    Undefined = 0,
    R4G4_UNORM_PACK8 = 1,
    R4G4B4A4_UNORM_PACK16 = 2,
    B4G4R4A4_UNORM_PACK16 = 3,
    R5G6B5_UNORM_PACK16 = 4,
    B5G6R5_UNORM_PACK16 = 5,
    R5G5B5A1_UNORM_PACK16 = 6,
    B5G5R5A1_UNORM_PACK16 = 7,
    A1R5G5B5_UNORM_PACK16 = 8,
    R8_UNORM = 9,
    R8_SNORM = 10,
    R8_USCALED = 11,
    R8_SSCALED = 12,
    R8_UINT = 13,
    R8_SINT = 14,
    R8_SRGB = 15,
    R8G8_UNORM = 16,
    R8G8_SNORM = 17,
    R8G8_USCALED = 18,
    R8G8_SSCALED = 19,
    R8G8_UINT = 20,
    R8G8_SINT = 21,
    R8G8_SRGB = 22,
    R8G8B8_UNORM = 23,
    R8G8B8_SNORM = 24,
    R8G8B8_USCALED = 25,
    R8G8B8_SSCALED = 26,
    R8G8B8_UINT = 27,
    R8G8B8_SINT = 28,
    R8G8B8_SRGB = 29,
    B8G8R8_UNORM = 30,
    B8G8R8_SNORM = 31,
    B8G8R8_USCALED = 32,
    B8G8R8_SSCALED = 33,
    B8G8R8_UINT = 34,
    B8G8R8_SINT = 35,
    B8G8R8_SRGB = 36,
    R8G8B8A8_UNORM = 37,
    R8G8B8A8_SNORM = 38,
    R8G8B8A8_USCALED = 39,
    R8G8B8A8_SSCALED = 40,
    R8G8B8A8_UINT = 41,
    R8G8B8A8_SINT = 42,
    R8G8B8A8_SRGB = 43,
    B8G8R8A8_UNORM = 44,
    B8G8R8A8_SNORM = 45,
    B8G8R8A8_USCALED = 46,
    B8G8R8A8_SSCALED = 47,
    B8G8R8A8_UINT = 48,
    B8G8R8A8_SINT = 49,
    B8G8R8A8_SRGB = 50,
    A8B8G8R8_UNORM_PACK32 = 51,
    A8B8G8R8_SNORM_PACK32 = 52,
    A8B8G8R8_USCALED_PACK32 = 53,
    A8B8G8R8_SSCALED_PACK32 = 54,
    A8B8G8R8_UINT_PACK32 = 55,
    A8B8G8R8_SINT_PACK32 = 56,
    A8B8G8R8_SRGB_PACK32 = 57,
    A2R10G10B10_UNORM_PACK32 = 58,
    A2R10G10B10_SNORM_PACK32 = 59,
    A2R10G10B10_USCALED_PACK32 = 60,
    A2R10G10B10_SSCALED_PACK32 = 61,
    A2R10G10B10_UINT_PACK32 = 62,
    A2R10G10B10_SINT_PACK32 = 63,
    A2B10G10R10_UNORM_PACK32 = 64,
    A2B10G10R10_SNORM_PACK32 = 65,
    A2B10G10R10_USCALED_PACK32 = 66,
    A2B10G10R10_SSCALED_PACK32 = 67,
    A2B10G10R10_UINT_PACK32 = 68,
    A2B10G10R10_SINT_PACK32 = 69,
    R16_UNORM = 70,
    R16_SNORM = 71,
    R16_USCALED = 72,
    R16_SSCALED = 73,
    R16_UINT = 74,
    R16_SINT = 75,
    R16_SFLOAT = 76,
    R16G16_UNORM = 77,
    R16G16_SNORM = 78,
    R16G16_USCALED = 79,
    R16G16_SSCALED = 80,
    R16G16_UINT = 81,
    R16G16_SINT = 82,
    R16G16_SFLOAT = 83,
    R16G16B16_UNORM = 84,
    R16G16B16_SNORM = 85,
    R16G16B16_USCALED = 86,
    R16G16B16_SSCALED = 87,
    R16G16B16_UINT = 88,
    R16G16B16_SINT = 89,
    R16G16B16_SFLOAT = 90,
    R16G16B16A16_UNORM = 91,
    R16G16B16A16_SNORM = 92,
    R16G16B16A16_USCALED = 93,
    R16G16B16A16_SSCALED = 94,
    R16G16B16A16_UINT = 95,
    R16G16B16A16_SINT = 96,
    R16G16B16A16_SFLOAT = 97,
    R32_UINT = 98,
    R32_SINT = 99,
    R32_SFLOAT = 100,
    R32G32_UINT = 101,
    R32G32_SINT = 102,
    R32G32_SFLOAT = 103,
    R32G32B32_UINT = 104,
    R32G32B32_SINT = 105,
    R32G32B32_SFLOAT = 106,
    R32G32B32A32_UINT = 107,
    R32G32B32A32_SINT = 108,
    R32G32B32A32_SFLOAT = 109,
    R64_UINT = 110,
    R64_SINT = 111,
    R64_SFLOAT = 112,
    R64G64_UINT = 113,
    R64G64_SINT = 114,
    R64G64_SFLOAT = 115,
    R64G64B64_UINT = 116,
    R64G64B64_SINT = 117,
    R64G64B64_SFLOAT = 118,
    R64G64B64A64_UINT = 119,
    R64G64B64A64_SINT = 120,
    R64G64B64A64_SFLOAT = 121,
    B10G11R11_UFLOAT_PACK32 = 122,
    E5B9G9R9_UFLOAT_PACK32 = 123,
    D16_UNORM = 124,
    X8_D24_UNORM_PACK32 = 125,
    D32_SFLOAT = 126,
    S8_UINT = 127,
    D16_UNORM_S8_UINT = 128,
    D24_UNORM_S8_UINT = 129,
    D32_SFLOAT_S8_UINT = 130,
    BC1_RGB_UNORM_BLOCK = 131,
    BC1_RGB_SRGB_BLOCK = 132,
    BC1_RGBA_UNORM_BLOCK = 133,
    BC1_RGBA_SRGB_BLOCK = 134,
    BC2_UNORM_BLOCK = 135,
    BC2_SRGB_BLOCK = 136,
    BC3_UNORM_BLOCK = 137,
    BC3_SRGB_BLOCK = 138,
    BC4_UNORM_BLOCK = 139,
    BC4_SNORM_BLOCK = 140,
    BC5_UNORM_BLOCK = 141,
    BC5_SNORM_BLOCK = 142,
    BC6H_UFLOAT_BLOCK = 143,
    BC6H_SFLOAT_BLOCK = 144,
    BC7_UNORM_BLOCK = 145,
    BC7_SRGB_BLOCK = 146,
    ETC2_R8G8B8_UNORM_BLOCK = 147,
    ETC2_R8G8B8_SRGB_BLOCK = 148,
    ETC2_R8G8B8A1_UNORM_BLOCK = 149,
    ETC2_R8G8B8A1_SRGB_BLOCK = 150,
    ETC2_R8G8B8A8_UNORM_BLOCK = 151,
    ETC2_R8G8B8A8_SRGB_BLOCK = 152,
    EAC_R11_UNORM_BLOCK = 153,
    EAC_R11_SNORM_BLOCK = 154,
    EAC_R11G11_UNORM_BLOCK = 155,
    EAC_R11G11_SNORM_BLOCK = 156,
    ASTC_4x4_UNORM_BLOCK = 157,
    ASTC_4x4_SRGB_BLOCK = 158,
    ASTC_5x4_UNORM_BLOCK = 159,
    ASTC_5x4_SRGB_BLOCK = 160,
    ASTC_5x5_UNORM_BLOCK = 161,
    ASTC_5x5_SRGB_BLOCK = 162,
    ASTC_6x5_UNORM_BLOCK = 163,
    ASTC_6x5_SRGB_BLOCK = 164,
    ASTC_6x6_UNORM_BLOCK = 165,
    ASTC_6x6_SRGB_BLOCK = 166,
    ASTC_8x5_UNORM_BLOCK = 167,
    ASTC_8x5_SRGB_BLOCK = 168,
    ASTC_8x6_UNORM_BLOCK = 169,
    ASTC_8x6_SRGB_BLOCK = 170,
    ASTC_8x8_UNORM_BLOCK = 171,
    ASTC_8x8_SRGB_BLOCK = 172,
    ASTC_10x5_UNORM_BLOCK = 173,
    ASTC_10x5_SRGB_BLOCK = 174,
    ASTC_10x6_UNORM_BLOCK = 175,
    ASTC_10x6_SRGB_BLOCK = 176,
    ASTC_10x8_UNORM_BLOCK = 177,
    ASTC_10x8_SRGB_BLOCK = 178,
    ASTC_10x10_UNORM_BLOCK = 179,
    ASTC_10x10_SRGB_BLOCK = 180,
    ASTC_12x10_UNORM_BLOCK = 181,
    ASTC_12x10_SRGB_BLOCK = 182,
    ASTC_12x12_UNORM_BLOCK = 183,
    ASTC_12x12_SRGB_BLOCK = 184,
    G8B8G8R8_422_UNORM = 1000156000,
    B8G8R8G8_422_UNORM = 1000156001,
    G8_B8_R8_3PLANE_420_UNORM = 1000156002,
    G8_B8R8_2PLANE_420_UNORM = 1000156003,
    G8_B8_R8_3PLANE_422_UNORM = 1000156004,
    G8_B8R8_2PLANE_422_UNORM = 1000156005,
    G8_B8_R8_3PLANE_444_UNORM = 1000156006,
    R10X6_UNORM_PACK16 = 1000156007,
    R10X6G10X6_UNORM_2PACK16 = 1000156008,
    R10X6G10X6B10X6A10X6_UNORM_4PACK16 = 1000156009,
    G10X6B10X6G10X6R10X6_422_UNORM_4PACK16 = 1000156010,
    B10X6G10X6R10X6G10X6_422_UNORM_4PACK16 = 1000156011,
    G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16 = 1000156012,
    G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16 = 1000156013,
    G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16 = 1000156014,
    G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16 = 1000156015,
    G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16 = 1000156016,
    R12X4_UNORM_PACK16 = 1000156017,
    R12X4G12X4_UNORM_2PACK16 = 1000156018,
    R12X4G12X4B12X4A12X4_UNORM_4PACK16 = 1000156019,
    G12X4B12X4G12X4R12X4_422_UNORM_4PACK16 = 1000156020,
    B12X4G12X4R12X4G12X4_422_UNORM_4PACK16 = 1000156021,
    G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16 = 1000156022,
    G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16 = 1000156023,
    G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16 = 1000156024,
    G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16 = 1000156025,
    G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16 = 1000156026,
    G16B16G16R16_422_UNORM = 1000156027,
    B16G16R16G16_422_UNORM = 1000156028,
    G16_B16_R16_3PLANE_420_UNORM = 1000156029,
    G16_B16R16_2PLANE_420_UNORM = 1000156030,
    G16_B16_R16_3PLANE_422_UNORM = 1000156031,
    G16_B16R16_2PLANE_422_UNORM = 1000156032,
    G16_B16_R16_3PLANE_444_UNORM = 1000156033,
    PVRTC1_2BPP_UNORM_BLOCK_IMG = 1000054000,
    PVRTC1_4BPP_UNORM_BLOCK_IMG = 1000054001,
    PVRTC2_2BPP_UNORM_BLOCK_IMG = 1000054002,
    PVRTC2_4BPP_UNORM_BLOCK_IMG = 1000054003,
    PVRTC1_2BPP_SRGB_BLOCK_IMG = 1000054004,
    PVRTC1_4BPP_SRGB_BLOCK_IMG = 1000054005,
    PVRTC2_2BPP_SRGB_BLOCK_IMG = 1000054006,
    PVRTC2_4BPP_SRGB_BLOCK_IMG = 1000054007,
    ASTC_4x4_SFLOAT_BLOCK_EXT = 1000066000,
    ASTC_5x4_SFLOAT_BLOCK_EXT = 1000066001,
    ASTC_5x5_SFLOAT_BLOCK_EXT = 1000066002,
    ASTC_6x5_SFLOAT_BLOCK_EXT = 1000066003,
    ASTC_6x6_SFLOAT_BLOCK_EXT = 1000066004,
    ASTC_8x5_SFLOAT_BLOCK_EXT = 1000066005,
    ASTC_8x6_SFLOAT_BLOCK_EXT = 1000066006,
    ASTC_8x8_SFLOAT_BLOCK_EXT = 1000066007,
    ASTC_10x5_SFLOAT_BLOCK_EXT = 1000066008,
    ASTC_10x6_SFLOAT_BLOCK_EXT = 1000066009,
    ASTC_10x8_SFLOAT_BLOCK_EXT = 1000066010,
    ASTC_10x10_SFLOAT_BLOCK_EXT = 1000066011,
    ASTC_12x10_SFLOAT_BLOCK_EXT = 1000066012,
    ASTC_12x12_SFLOAT_BLOCK_EXT = 1000066013,
    G8_B8R8_2PLANE_444_UNORM_EXT = 1000330000,
    G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16_EXT = 1000330001,
    G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16_EXT = 1000330002,
    G16_B16R16_2PLANE_444_UNORM_EXT = 1000330003,
    A4R4G4B4_UNORM_PACK16_EXT = 1000340000,
    A4B4G4R4_UNORM_PACK16_EXT = 1000340001,
    G8B8G8R8_422_UNORM_KHR = G8B8G8R8_422_UNORM,
    B8G8R8G8_422_UNORM_KHR = B8G8R8G8_422_UNORM,
    G8_B8_R8_3PLANE_420_UNORM_KHR = G8_B8_R8_3PLANE_420_UNORM,
    G8_B8R8_2PLANE_420_UNORM_KHR = G8_B8R8_2PLANE_420_UNORM,
    G8_B8_R8_3PLANE_422_UNORM_KHR = G8_B8_R8_3PLANE_422_UNORM,
    G8_B8R8_2PLANE_422_UNORM_KHR = G8_B8R8_2PLANE_422_UNORM,
    G8_B8_R8_3PLANE_444_UNORM_KHR = G8_B8_R8_3PLANE_444_UNORM,
    R10X6_UNORM_PACK16_KHR = R10X6_UNORM_PACK16,
    R10X6G10X6_UNORM_2PACK16_KHR = R10X6G10X6_UNORM_2PACK16,
    R10X6G10X6B10X6A10X6_UNORM_4PACK16_KHR = R10X6G10X6B10X6A10X6_UNORM_4PACK16,
    G10X6B10X6G10X6R10X6_422_UNORM_4PACK16_KHR = G10X6B10X6G10X6R10X6_422_UNORM_4PACK16,
    B10X6G10X6R10X6G10X6_422_UNORM_4PACK16_KHR = B10X6G10X6R10X6G10X6_422_UNORM_4PACK16,
    G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16_KHR = G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16,
    G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16_KHR = G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16,
    G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16_KHR = G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16,
    G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16_KHR = G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16,
    G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16_KHR = G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16,
    R12X4_UNORM_PACK16_KHR = R12X4_UNORM_PACK16,
    R12X4G12X4_UNORM_2PACK16_KHR = R12X4G12X4_UNORM_2PACK16,
    R12X4G12X4B12X4A12X4_UNORM_4PACK16_KHR = R12X4G12X4B12X4A12X4_UNORM_4PACK16,
    G12X4B12X4G12X4R12X4_422_UNORM_4PACK16_KHR = G12X4B12X4G12X4R12X4_422_UNORM_4PACK16,
    B12X4G12X4R12X4G12X4_422_UNORM_4PACK16_KHR = B12X4G12X4R12X4G12X4_422_UNORM_4PACK16,
    G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16_KHR = G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16,
    G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16_KHR = G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16,
    G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16_KHR = G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16,
    G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16_KHR = G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16,
    G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16_KHR = G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16,
    G16B16G16R16_422_UNORM_KHR = G16B16G16R16_422_UNORM,
    B16G16R16G16_422_UNORM_KHR = B16G16R16G16_422_UNORM,
    G16_B16_R16_3PLANE_420_UNORM_KHR = G16_B16_R16_3PLANE_420_UNORM,
    G16_B16R16_2PLANE_420_UNORM_KHR = G16_B16R16_2PLANE_420_UNORM,
    G16_B16_R16_3PLANE_422_UNORM_KHR = G16_B16_R16_3PLANE_422_UNORM,
    G16_B16R16_2PLANE_422_UNORM_KHR = G16_B16R16_2PLANE_422_UNORM,
    G16_B16_R16_3PLANE_444_UNORM_KHR = G16_B16_R16_3PLANE_444_UNORM,
};

enum class GfxMemoryArena : uint8 
{
    PersistentGPU = 0,  // Always Device_Local
    PersistentCPU,      // Permanent staging resources
    TransientCPU,       // Temp staging resources
    DynamicImageGPU,    // Device_Local but dynamically allocated
    DynamicBufferGPU    // Device_Local but dynamically allocated
#if PLATFORM_APPLE || PLATFORM_ANDROID
    , TiledGPU          // Only on Tiled GPUs, transient virtual resources on Tile mem
#endif
};

enum class GfxQueueType
{
    None = 0,
    Graphics = 0x1,
    Compute = 0x2,
    ComputeAsync = 0x4,
    Transfer = 0x8,
    Present = 0x10
};
ENABLE_BITMASK(GfxQueueType);

struct GfxMapResult
{
    void* dataPtr;
    size_t dataSize;
};

struct GfxViewport
{
    float x;
    float y;
    float width;
    float height;
    float minDepth;
    float maxDepth = 1.0f;
};

using GfxResourceTransferCallback = void(*)(void* userData);

// VkShaderStageFlags
enum class GfxShaderStage: uint32
{
    Vertex                    = 0x00000001,
    TessellationControl       = 0x00000002,
    TessellationEvaluation    = 0x00000004,
    Geometry                  = 0x00000008,
    Fragment                  = 0x00000010,
    Compute                   = 0x00000020,
    AllGraphics               = 0x0000001f,
    All                       = 0x7fffffff,
    Raygen                    = 0x00000100,
    AnyHit                    = 0x00000200,
    ClosestHit                = 0x00000400,
    Miss                      = 0x00000800,
    Intersection              = 0x00001000,
    Callable                  = 0x00002000,
    TaskNV                    = 0x00000040,
    MeshNV                    = 0x00000080,
};
ENABLE_BITMASK(GfxShaderStage);

//   ____  _   _ _____ _____ _____ ____  
//  | __ )| | | |  ___|  ___| ____|  _ \ 
//  |  _ \| | | | |_  | |_  |  _| | |_) |
//  | |_) | |_| |  _| |  _| | |___|  _ < 
//  |____/ \___/|_|   |_|   |_____|_| \_\
//                                       

// VkIndexType
enum class GfxIndexType: uint32
{
    Uint16 = 0,
    Uint32 = 1,
    Uint8 = 1000265000, // VK_EXT_index_type_uint8
    None = 1000165000 // VK_KHR_acceleration_structure
};

// VkBufferUsageFlagBits
enum class GfxBufferUsageFlags : uint32
{
    TransferSrc = 0x00000001,
    TransferDst = 0x00000002,
    UniformTexel = 0x00000004,
    StorageTexel = 0x00000008,
    Uniform = 0x00000010,
    Storage = 0x00000020,
    Index = 0x00000040,
    Vertex = 0x00000080,
    Indirect = 0x00000100
};
ENABLE_BITMASK(GfxBufferUsageFlags);

// Note: Serialized for asset data. Changing this will affect cache data
// TODO: Probably should remove this dependency to AssetManager and use AssetManager internals instead
struct GfxBufferDesc
{
    uint64 sizeBytes;
    GfxBufferUsageFlags usageFlags;
    GfxMemoryArena arena = GfxMemoryArena::PersistentGPU;
};

enum class GfxBufferTransition
{
    TransferWrite,
    ComputeRead,
    ComputeWrite,
    FragmentRead
};

struct GfxCopyBufferToBufferParams
{
    GfxBufferHandle srcHandle;
    GfxBufferHandle dstHandle;
    GfxShaderStage stagesUsed;
    size_t srcOffset;
    size_t dstOffset;
    size_t sizeBytes;
    GfxResourceTransferCallback resourceTransferedCallback;
    void* resourceTransferedUserData;
};

//   ___ __  __    _    ____ _____ 
//  |_ _|  \/  |  / \  / ___| ____|
//   | || |\/| | / _ \| |  _|  _|  
//   | || |  | |/ ___ \ |_| | |___ 
//  |___|_|  |_/_/   \_\____|_____|
//

// VkImageUsageFlagBits
enum class GfxImageUsageFlags : uint32
{
    TransferSrc = 0x00000001,
    TransferDst = 0x00000002,
    Sampled = 0x00000004,
    Storage = 0x00000008,
    ColorAttachment = 0x00000010,
    DepthStencilAttachment = 0x00000020,
    TransientAttachment = 0x00000040,
    InputAttachment = 0x00000080
};
ENABLE_BITMASK(GfxImageUsageFlags);

// VkImageType
enum class GfxImageType : uint32
{
    Image1D = 0,
    Image2D = 1,
    Image3D = 2
};
ENABLE_BITMASK(GfxImageType);

// VkSampleCountFlagBits
enum class GfxMultiSampleCount : uint32
{
    SampleCount1 = 0x00000001,
    SampleCount2 = 0x00000002,
    SampleCount4 = 0x00000004,
    SampleCount8 = 0x00000008,
    SampleCount16 = 0x00000010,
    SampleCount32 = 0x00000020,
    SampleCount64 = 0x00000040
};

// Serialized for asset cache. Changing this will affect asset data
struct GfxImageDesc
{
    uint16 width;
    uint16 height;
    uint16 depth = 1;
    uint16 numMips = 1;
    uint16 numArrayLayers = 1;
    GfxMultiSampleCount multisampleFlags = GfxMultiSampleCount::SampleCount1;
    GfxImageType type = GfxImageType::Image2D;
    GfxFormat format;
    GfxImageUsageFlags usageFlags = GfxImageUsageFlags::Sampled;
    GfxMemoryArena arena = GfxMemoryArena::PersistentGPU;
    uint32 mipOffsets[GFXBACKEND_MAX_MIPS_PER_IMAGE];
};

struct GfxCopyBufferToImageParams
{
    GfxBufferHandle srcHandle;
    GfxImageHandle dstHandle;
    GfxShaderStage stagesUsed;
    uint16 startMipIndex;
    uint16 mipCount;
    GfxResourceTransferCallback resourceTransferedCallback;
    void* resourceTransferedUserData;
};

enum class GfxImageTransition
{
    ShaderRead,
    ComputeWrite,
    CopySource,
    RenderTarget
};

enum class GfxImageTransitionFlags : uint32 
{
    None = 0,
    DepthWrite = 0x1,
    DepthRead = 0x2,
    DepthResolve = 0x4
};
ENABLE_BITMASK(GfxImageTransitionFlags);

// Binary representation (SERIALIZED)
struct GfxImage
{
    GfxImageHandle handle;
    uint32 width;
    uint32 height;
    uint32 depth;
    uint32 numMips;
    GfxFormat format;
    uint32 contentSize;
    uint32 mipOffsets[GFXBACKEND_MAX_MIPS_PER_IMAGE];
};

//   ____    _    __  __ ____  _     _____ ____  
//  / ___|  / \  |  \/  |  _ \| |   | ____|  _ \ 
//  \___ \ / _ \ | |\/| | |_) | |   |  _| | |_) |
//   ___) / ___ \| |  | |  __/| |___| |___|  _ < 
//  |____/_/   \_\_|  |_|_|   |_____|_____|_| \_\
//                                               
enum class GfxSamplerFilterMode : uint32
{
    Default = 0,
    Nearest,
    Linear,
    NearestMipmapNearest,
    NearestMipmapLinear,
    LinearMipmapNearest,
    LinearMipmapLinear
};

enum class GfxSamplerWrapMode : uint32
{
    Default = 0,
    Repeat,
    ClampToEdge,
    ClampToBorder,
    MirroredRepeat
};

enum class GfxSamplerBorderColor: uint32
{
    Default = 0,
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite
};

// VkCompareOp
enum class GfxCompareOp : uint32
{
    Never = 0,
    Less = 1,
    Equal = 2,
    LessOrEqual = 3,
    Greater = 4,
    NotEqual = 5,
    GreaterOrEqual = 6,
    Always = 7,
};

struct GfxSamplerDesc
{
    GfxSamplerFilterMode samplerFilter = GfxSamplerFilterMode::Nearest;
    GfxSamplerWrapMode samplerWrap = GfxSamplerWrapMode::Repeat;
    GfxSamplerBorderColor borderColor = GfxSamplerBorderColor::Default;
    float anisotropy = 1.0f;
    float mipLODBias = 0;
    GfxCompareOp compareOp = GfxCompareOp::Always;
};


//   ____  _     _____ _   _ ____  
//  | __ )| |   | ____| \ | |  _ \ 
//  |  _ \| |   |  _| |  \| | | | |
//  | |_) | |___| |___| |\  | |_| |
//  |____/|_____|_____|_| \_|____/ 
//                                 
// VkLogicOp
enum class GfxLogicOp : uint32
{
    Clear = 0,
    And = 1,
    AndReverse = 2,
    Copy = 3,
    AndInverted = 4,
    NoOp = 5,
    Xor = 6,
    Or = 7,
    Nor = 8,
    Equivalent = 9,
    Invert = 10,
    OrReverse = 11,
    CopyInverted = 12,
    OrInverted = 13,
    Nand = 14,
    Set = 15
};

// VkBlendFactor
enum class GfxBlendFactor : uint32
{
    Zero = 0,
    One = 1,
    SrcColor = 2,
    OneMinusSrcColor = 3,
    DstColor = 4,
    OneMinusDstColor = 5,
    SrcAlpha = 6,
    OneMinusSrcAlpha = 7,
    DstAlpha = 8,
    OneMinusDstAlpha = 9,
    ConstantColor = 10,
    OneMinusConstantColor = 11,
    ConstantAlpha = 12,
    OneMinusConstantAlpha = 13,
    SrcAlphaSaturate = 14,
    Src1Color = 15,
    OneMinusSrc1Color = 16,
    Src1Alpha = 17,
    OneMinusSrc1Alpha = 18,
};

// VkBlendOp
enum class GfxBlendOp : uint32
{
    Add = 0,
    Subtract = 1,
    Reverse_subtract = 2,
    Min = 3,
    Max = 4,
    ZeroExt = 1000148000,
    SrcExt = 1000148001,
    DstExt = 1000148002,
    SrcOverExt = 1000148003,
    DstOverExt = 1000148004,
    SrcInExt = 1000148005,
    DstInExt = 1000148006,
    SrcOutExt = 1000148007,
    DstOutExt = 1000148008,
    SrcAtopExt = 1000148009,
    DstAtopExt = 1000148010,
    XorExt = 1000148011,
    MultiplyExt = 1000148012,
    ScreenExt = 1000148013,
    OverlayExt = 1000148014,
    DarkenExt = 1000148015,
    LightenExt = 1000148016,
    ColordodgeExt = 1000148017,
    ColorburnExt = 1000148018,
    HardlightExt = 1000148019,
    SoftlightExt = 1000148020,
    DifferenceExt = 1000148021,
    ExclusionExt = 1000148022,
    InvertExt = 1000148023,
    InvertRgbExt = 1000148024,
    LineardodgeExt = 1000148025,
    LinearburnExt = 1000148026,
    VividlightExt = 1000148027,
    LinearlightExt = 1000148028,
    PinlightExt = 1000148029,
    HardmixExt = 1000148030,
    HslHueExt = 1000148031,
    HslSaturationExt = 1000148032,
    HslColorExt = 1000148033,
    HslLuminosityExt = 1000148034,
    PlusExt = 1000148035,
    PlusClampedExt = 1000148036,
    PlusClampedAlphaExt = 1000148037,
    PlusDarkerExt = 1000148038,
    MinusExt = 1000148039,
    MinusClampedExt = 1000148040,
    ContrastExt = 1000148041,
    InvertOvgExt = 1000148042,
    RedExt = 1000148043,
    GreenExt = 1000148044,
    BlueExt = 1000148045,
};

// VkColorComponentFlags
enum class GfxColorComponentFlags: uint32
{
    R = 0x00000001,
    G = 0x00000002,
    B = 0x00000004,
    A = 0x00000008,
    RGB = 0x7,
    All = 0xf
};
ENABLE_BITMASK(GfxColorComponentFlags);

// Blending: pseudo code
// if (blendEnable) {
//    finalColor.rgb = (srcColorBlendFactor * newColor.rgb) <colorBlendOp> (dstColorBlendFactor * oldColor.rgb);
//    finalColor.a = (srcAlphaBlendFactor * newColor.a) <alphaBlendOp> (dstAlphaBlendFactor * oldColor.a);
// }
// else {
//    finalColor = newColor;
// }
//
// finalColor = finalColor & colorWriteMask;
struct GfxBlendAttachmentDesc 
{
    bool enable;
    GfxBlendFactor srcColorBlendFactor;
    GfxBlendFactor dstColorBlendFactor;
    GfxBlendOp blendOp;
    GfxBlendFactor srcAlphaBlendFactor;
    GfxBlendFactor dstAlphaBlendFactor;
    GfxBlendOp alphaBlendOp;
    GfxColorComponentFlags colorWriteMask;

    static const GfxBlendAttachmentDesc* GetDefault();
    static const GfxBlendAttachmentDesc* GetAlphaBlending();
};

struct GfxBlendDesc
{
    bool logicOpEnable;
    GfxLogicOp logicOp;
    uint32 numAttachments;
    const GfxBlendAttachmentDesc* attachments;
    float blendConstants[4];
};


//   ____             _   _     ____  _                  _ _ 
//  |  _ \  ___ _ __ | |_| |__ / ___|| |_ ___ _ __   ___(_) |
//  | | | |/ _ \ '_ \| __| '_ \\___ \| __/ _ \ '_ \ / __| | |
//  | |_| |  __/ |_) | |_| | | |___) | ||  __/ | | | (__| | |
//  |____/ \___| .__/ \__|_| |_|____/ \__\___|_| |_|\___|_|_|
//             |_|                                           
//

enum class GfxStencilFaceFlags : uint32 
{
    Front = 0x00000001,
    Back = 0x00000002,
    FrontAndBack = 0x00000003
};
ENABLE_BITMASK(GfxStencilFaceFlags);

// VkStencilOp
enum class GfxStencilOp : uint32
{
    Keep = 0,
    Zero = 1,
    Replace = 2,
    IncrementAndClamp = 3,
    DecrementAndClamp = 4,
    Invert = 5,
    IncrementAndWrap = 6,
    DecrementAndWrap = 7
};

struct GfxStencilOpDesc
{
    GfxStencilFaceFlags faceFlags;  // Only used in GfxDynamicState. DepthStencilDesc already has all cases included
    GfxStencilOp    failOp;
    GfxStencilOp    passOp;
    GfxStencilOp    depthFailOp;
    GfxCompareOp    compareOp;
    uint32          compareMask;
    uint32          writeMask;
    uint32          reference;
};

struct GfxDepthStencilDesc
{
    bool             depthTestEnable;
    bool             depthWriteEnable;
    GfxCompareOp     depthCompareOp;
    bool             depthBoundsTestEnable;
    bool             stencilTestEnable;
    GfxStencilOpDesc stencilFront;
    GfxStencilOpDesc stencilBack;
    float            minDepthBounds;
    float            maxDepthBounds;
};

//   ____      _    ____ _____ _____ ____  ___ __________ ____  
//  |  _ \    / \  / ___|_   _| ____|  _ \|_ _|__  / ____|  _ \ 
//  | |_) |  / _ \ \___ \ | | |  _| | |_) || |  / /|  _| | |_) |
//  |  _ <  / ___ \ ___) || | | |___|  _ < | | / /_| |___|  _ < 
//  |_| \_\/_/   \_\____/ |_| |_____|_| \_\___/____|_____|_| \_\
//                                                              

// VkPolygonMode
enum class GfxPolygonMode : uint32
{
    Fill        = 0,
    Line        = 1,
    Point       = 2,
};

// VkCullModeFlags
enum class GfxCullMode: uint32
{
    None           = 0,
    Front          = 0x00000001,
    Back           = 0x00000002,
    FrontAndBack   = 0x00000003,
};
ENABLE_BITMASK(GfxCullMode);

// VkFrontFace
enum class GfxFrontFace : uint32
{
    CounterClockwise  = 0,
    Clockwise         = 1,
};

struct GfxRasterizerDesc 
{
    bool             depthClampEnable;
    bool             rasterizerDiscardEnable;
    GfxPolygonMode   polygonMode;
    GfxCullMode      cullMode;
    GfxFrontFace     frontFace;
    bool             depthBiasEnable;
    float            depthBiasConstantFactor;
    float            depthBiasClamp;
    float            depthBiasSlopeFactor;
    float            lineWidth = 1.0f;
};

struct GfxMultiSampleDesc
{
    GfxMultiSampleCount sampleCount = GfxMultiSampleCount::SampleCount1;
    bool sampleShadingEnable;
    float minSampleShading = 1.0f;
    const uint32* sampleMask;
    bool alphaToCoverageEnable;
    bool alphaToOneEnable;
};

//   ____ ___ ____  _____ _     ___ _   _ _____ ____  
//  |  _ \_ _|  _ \| ____| |   |_ _| \ | | ____/ ___| 
//  | |_) | || |_) |  _| | |    | ||  \| |  _| \___ \ 
//  |  __/| ||  __/| |___| |___ | || |\  | |___ ___) |
//  |_|  |___|_|   |_____|_____|___|_| \_|_____|____/ 
//                                                    

// VkPrimitiveTopology
enum class GfxPrimitiveTopology: uint32
{
    PointList = 0,
    LineList = 1,
    LineStrip = 2,
    TriangleList = 3,
    TriangleStrip = 4,
    TriangleFan = 5,
    LineListWithAdjacency = 6,
    LineStripWithAdjacency = 7,
    TriangleListWithAdjacency = 8,
    TriangleStripWithAdjacency = 9,
    PatchList = 10,
};

// VkDescriptorType
enum class GfxDescriptorType : uint32
{
    Sampler                   = 0,
    CombinedImageSampler      = 1,
    SampledImage              = 2,
    StorageImage              = 3,
    UniformTexelBuffer        = 4,
    StorageTexelBuffer        = 5,
    UniformBuffer             = 6,
    StorageBuffer             = 7,
    UniformBufferDynamic      = 8,
    StorageBufferDynamic      = 9,
    InputAttachment           = 10,
    InlineUniformBlockExt     = 1000138000,
    AccelerationStructureKhr  = 1000150000,
    AccelerationStructureNv   = 1000165000,
    MutableValve              = 1000351000,
};

struct GfxPipelineLayoutDesc
{
    struct Binding 
    {
        const char* name;
        GfxDescriptorType type;
        GfxShaderStage stagesUsed;
        uint32 arrayCount = 1;
        uint8 setIndex = 0;        // DescriptorSet Id
    };

    // Push constants are declared in the shaders by putting [[vk_push_constant]] annotation before cbuffers
    // Setting them are done with GfxCommandBuffer::PushConstants
    struct PushConstant
    {
        const char* name;
        GfxShaderStage stagesUsed;
        uint32 offset;
        uint32 size;
    };
    
    uint32 numBindings;
    const Binding* bindings;
    uint32 numPushConstants;
    const PushConstant* pushConstants;
    bool usePushDescriptors = true;    
};

// VkVertexInputRate
enum class GfxVertexInputRate : uint32
{
    Vertex = 0,
    Instance = 1,
};

struct GfxVertexInputAttributeDesc 
{
    String<12>  semantic;
    uint32      semanticIdx;
    uint32      binding;
    GfxFormat   format;
    uint32      offset;
};

struct GfxVertexBufferBindingDesc 
{
    uint32               binding;
    uint32               stride;
    GfxVertexInputRate   inputRate;
};

struct GfxShaderPermutationVar
{
    enum Type
    {
        Void = 0,
        Boolean,
        Int,
        Float
    };

    const char* name = nullptr;
    Type type = Void;

    union {
        bool b;
        int i;
        float f;
    } value;

    GfxShaderPermutationVar() = default;
    explicit GfxShaderPermutationVar(const char* _name, bool _b) : name(_name), type(Boolean) { value.b = _b; }
    explicit GfxShaderPermutationVar(const char* _name, int _i) : name(_name), type(Int) { value.i = _i; }
    explicit GfxShaderPermutationVar(const char* _name, float _f) : name(_name), type(Float) { value.f = _f; }
};

struct GfxGraphicsPipelineDesc
{
    GfxPrimitiveTopology inputAssemblyTopology = GfxPrimitiveTopology::TriangleList;
    
    uint32 numVertexInputAttributes;
    const GfxVertexInputAttributeDesc* vertexInputAttributes;

    uint32 numVertexBufferBindings;
    const GfxVertexBufferBindingDesc* vertexBufferBindings;

    GfxRasterizerDesc rasterizer;
    GfxBlendDesc blend;
    GfxDepthStencilDesc depthStencil;
    GfxMultiSampleDesc msaa;

    uint32 numColorAttachments;
    GfxFormat colorAttachmentFormats[GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS];
    GfxFormat depthAttachmentFormat;
    GfxFormat stencilAttachmentFormat;
};

struct GfxBindingDesc
{   
    const char* name;

    union {
        struct {
            uint32 offset = 0;
            uint32 size = 0;
        } bufferRange;
    };

    uint32 imageArrayCount = 1;

    union
    {
        GfxBufferHandle buffer;
        GfxImageHandle image;
        GfxSamplerHandle sampler;
        const GfxImageHandle* imageArray;
    };
};

struct GfxDynamicState
{
    union {
        GfxCullMode cullMode;
        GfxCompareOp depthCompareOp;
        GfxFrontFace frontFace;
        GfxPrimitiveTopology primitiveTopology;
        GfxStencilOpDesc stencilOp;
        GfxLogicOp logicOp;
        bool depthBoundsTestEnable;
        bool depthTestEnable;
        bool depthWriteEnable;
        bool depthBiasEnable;
        bool stencilTestEnable;
        bool rasterizerDiscardEnable;
    };

    uint16 setCullMode : 1;
    uint16 setDepthBoundsTestEnable : 1;
    uint16 setDepthCompareOp : 1;
    uint16 setDepthTestEnable : 1;
    uint16 setDepthBiasEnable : 1;
    uint16 setFrontFace : 1;
    uint16 setPrimitiveTopology : 1;
    uint16 setStencilOp : 1;
    uint16 setStencilTestEnable : 1;
    uint16 setLogicOp : 1;
    uint16 setRasterizerDiscardEnable : 1;
};

//   ____                _           ____               
//  |  _ \ ___ _ __   __| | ___ _ __|  _ \ __ _ ___ ___ 
//  | |_) / _ \ '_ \ / _` |/ _ \ '__| |_) / _` / __/ __|
//  |  _ <  __/ | | | (_| |  __/ |  |  __/ (_| \__ \__ \
//  |_| \_\___|_| |_|\__,_|\___|_|  |_|   \__,_|___/___/
//

struct GfxRenderPassAttachment
{
    GfxImageHandle image;
    GfxImageHandle resolveImage;
    bool load;
    bool clear;
    bool resolveToSwapchain;

    struct {
        Float4 color;
        float depth;
        uint32 stencil;
    } clearValue;
};

struct GfxBackendRenderPass
{
    RectInt cropRect = RECTINT_EMPTY;
    uint32 numAttachments;
    GfxRenderPassAttachment colorAttachments[GFXBACKEND_MAX_RENDERPASS_COLOR_ATTACHMENTS];
    GfxRenderPassAttachment depthAttachment;
    GfxRenderPassAttachment stencilAttachment;
    bool swapchain;
    bool hasDepth;
    bool hasStencil;
};


//   ____  _   _    _    ____  _____ ____  
//  / ___|| | | |  / \  |  _ \| ____|  _ \ 
//  \___ \| |_| | / _ \ | | | |  _| | |_) |
//   ___) |  _  |/ ___ \| |_| | |___|  _ < 
//  |____/|_| |_/_/   \_\____/|_____|_| \_\
//                                        

// SERIALIZED
struct GfxShaderStageInfo
{
    GfxShaderStage stage;
    char entryName[32];
    uint32 dataSize;
    RelativePtr<uint8> data;
};

// SERIALIZED
enum class GfxShaderParameterType : uint32
{
    Uniformbuffer,
    Samplerstate,
    Resource,
    Array,
    Scalar
};

enum class GfxShaderScalarType : uint32
{
    Void = 0,
    Bool,
    Int32,
    Float32
};

// SERIALIZED
struct GfxShaderParameterInfo
{
    char name[32];
    GfxShaderParameterType type;
    GfxShaderStage stage;
    GfxShaderScalarType scalarType;
    uint32 bindingIdx;
    bool isPushConstant;
    bool isSpecialization;
};

// SERIALIZED
struct GfxShaderVertexAttributeInfo 
{
    char name[32];
    char semantic[16];
    uint32 semanticIdx;
    uint32 location;
    GfxFormat format;
};

// SERIALIZED
struct GfxShader
{
    char name[32];
    uint32 paramsHash;           // ParamsHash of the shader asset: Passed to pipelines to recreate them whenever shader is reloaded
    uint32 numStages;
    uint32 numParams;
    uint32 numVertexAttributes;
    RelativePtr<GfxShaderStageInfo> stages;
    RelativePtr<GfxShaderParameterInfo> params;
    RelativePtr<GfxShaderVertexAttributeInfo> vertexAttributes;
};

