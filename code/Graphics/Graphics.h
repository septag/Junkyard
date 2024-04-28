#pragma once

#include "../Core/Base.h"
#include "../Core/StringUtil.h" 
#include "../Core/MathTypes.h"

#include "../Common/CommonTypes.h"

struct MemTlsfAllocator;


//    ████████╗██╗   ██╗██████╗ ███████╗███████╗
//    ╚══██╔══╝╚██╗ ██╔╝██╔══██╗██╔════╝██╔════╝
//       ██║    ╚████╔╝ ██████╔╝█████╗  ███████╗
//       ██║     ╚██╔╝  ██╔═══╝ ██╔══╝  ╚════██║
//       ██║      ██║   ██║     ███████╗███████║
//       ╚═╝      ╚═╝   ╚═╝     ╚══════╝╚══════╝
                                              
// 1-1 to vulkan
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

enum class GfxSwapchainPresentMode: uint32
{
    Default = 0,
    Immediate,
    Fifo,
    Relaxed,
    Mailbox
};

enum class GfxBufferUsage: uint32
{
    Default = 0,
    Immutable,
    Stream
};

enum class GfxBufferType: uint32
{
    Default = 0,
    Vertex,
    Index,
    Uniform
};

struct GfxBufferDesc
{
    uint32          size;
    GfxBufferType   type;
    GfxBufferUsage  usage;
    const void*     content;
};

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

inline constexpr uint32 kGfxMaxMips = 16;

struct GfxImageDesc
{
    uint32                  width           = 0;
    uint32                  height          = 0;
    uint32                  numMips         = 1;
    GfxFormat               format          = GfxFormat::Undefined;
    GfxBufferUsage          usage           = GfxBufferUsage::Default;
    float                   anisotropy      = 1.0f;
    GfxSamplerFilterMode    samplerFilter   = GfxSamplerFilterMode::Nearest;
    GfxSamplerWrapMode      samplerWrap     = GfxSamplerWrapMode::Repeat;
    GfxSamplerBorderColor   borderColor     = GfxSamplerBorderColor::Default;
    bool                    frameBuffer     = false;
    bool                    sampled         = false;
    size_t                  size            = 0;
    const void*             content         = nullptr;
    const uint32*           mipOffsets      = nullptr;
};

// 1-1->vulkan
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

typedef struct GfxVertexBufferBindingDesc 
{
    uint32               binding;
    uint32               stride;
    GfxVertexInputRate   inputRate;
} GfxVertexBufferBindingDesc;

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

// 1-1->vulkan
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

struct GfxDescriptorSetLayoutBinding
{
    const char*         name;               // Binding index is extracted from shader and looked up with the name
    GfxDescriptorType   type;     
    GfxShaderStage      stages;             // Which shader stage the binding is being used (combination of GfxShaderStageFlagBits)
    uint32              arrayCount = 1;      
};

// 1-1 vulkan
enum class GfxPolygonMode : uint32
{
    Fill        = 0,
    Line        = 1,
    Point       = 2,
};

// 1-1 vulkan
enum class GfxCullModeFlags: uint32
{
    None           = 0,
    Front          = 0x00000001,
    Back           = 0x00000002,
    FrontAndBack   = 0x00000003,
};
ENABLE_BITMASK(GfxCullModeFlags);

// 1-1 vulkan
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
    GfxCullModeFlags cullMode;
    GfxFrontFace     frontFace;
    bool             depthBiasEnable;
    float            depthBiasConstantFactor;
    float            depthBiasClamp;
    float            depthBiasSlopeFactor;
    float            lineWidth = 1.0f;
};

struct GfxShaderDefine
{
    const char* define;
    const char* value;
};

// 1-1 vulkan
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

// 1-1 vulkan
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

// 1-1 vulkan
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

// 1-1 vulkan
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

struct GfxBlendDesc
{
    bool logicOpEnable;
    GfxLogicOp logicOp;
    uint32 numAttachments;
    const GfxBlendAttachmentDesc* attachments;
    float blendConstants[4];
};

// 1-1 vulkan
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

struct GfxBufferRange
{
    uint32 offset;
    uint32 size;
};

struct GfxPushConstantDesc
{
    const char* name;
    GfxShaderStage stages;
    GfxBufferRange range;
};

struct GfxShaderStageInfo
{
    GfxShaderStage stage;
    char entryName[32];
    uint32 dataSize;
    RelativePtr<uint8> data;
};

enum class GfxShaderParameterType : uint32
{
    Uniformbuffer,
    Samplerstate,
    Resource,
    Array
};

struct GfxShaderParameterInfo
{
    char name[32];
    GfxShaderParameterType type;
    GfxShaderStage stage;
    uint32 bindingIdx;
    bool isPushConstant;
};

struct GfxShaderVertexAttributeInfo 
{
    char        name[32];
    char        semantic[16];
    uint32      semanticIdx;
    uint32      location;
    GfxFormat   format;
};

// Note: Binary representation and it is serialized
struct GfxShader
{
    char   name[32];
    uint32 hash;           // This is actually the AssetId of the shader
    uint32 numStages;
    uint32 numParams;
    uint32 numVertexAttributes;
    RelativePtr<GfxShaderStageInfo> stages;
    RelativePtr<GfxShaderParameterInfo> params;
    RelativePtr<GfxShaderVertexAttributeInfo> vertexAttributes;
};

struct GfxPipelineDesc
{
    GfxShader* shader;
    GfxPrimitiveTopology inputAssemblyTopology;
    
    uint32 numDescriptorSetLayouts;
    const GfxDescriptorSetLayout* descriptorSetLayouts;

    uint32 numPushConstants;
    const GfxPushConstantDesc* pushConstants;

    uint32 numVertexInputAttributes;
    const GfxVertexInputAttributeDesc* vertexInputAttributes;
    uint32 numVertexBufferBindings;
    const GfxVertexBufferBindingDesc* vertexBufferBindings;

    GfxRasterizerDesc rasterizer;
    GfxBlendDesc blend;
    GfxDepthStencilDesc depthStencil;
};

// 1-1 vulkan
enum class GfxSampleCountFlags: uint32
{
    MSAA_1 = 0x00000001,
    MSAA_2 = 0x00000002,
    MSAA_4 = 0x00000004,
    MSAA_8 = 0x00000008,
    MSAA_16 = 0x00000010,
    MSAA_32 = 0x00000020,
    MSAA_64 = 0x00000040,
};
ENABLE_BITMASK(GfxSampleCountFlags);

// 1-1 vulkan
enum class GfxAttachmentLoadOp: uint32
{
    Load = 0,
    Clear = 1,
    Dontcare = 2,
    NoneExt = 1000400000,
};

// 1-1n
enum class GfxAttachmentStoreOp : uint32
{
    Store = 0,
    DontCare = 1,
    NoneExt = 1000301000
};

// 1-1n
enum class GfxImageLayout: uint32
{
    Undefined = 0,
    General = 1,
    ColorAttachmentOptimal = 2,
    DepthStencilAttachmentOptimal = 3,
    DepthStencilReadOnlyOptimal = 4,
    ShaderReadOnlyOptimal = 5,
    TransferSrcOptimal = 6,
    TransferDstOptimal = 7,
    Preinitialized = 8,
    DepthReadOnlyStencilAttachmentOptimal = 1000117000,
    DepthAttachmentStencilReadOnlyOptimal = 1000117001,
    DepthAttachmentOptimal = 1000241000,
    DepthReadOnlyOptimal = 1000241001,
    StencilAttachmentOptimal = 1000241002,
    StencilReadOnlyOptimal = 1000241003,
    PresentSrcKhr = 1000001002,
    VideoDecodeDstKhr = 1000024000,
    VideoDecodeSrcKhr = 1000024001,
    VideoDecodeDpbKhr = 1000024002,
    SharedPresentKhr = 1000111000,
    FragmentDensityMapOptimalExt = 1000218000,
    FragmentShadingRateAttachmentOptimalKhr = 1000164003,
    VideoEncodeDstKhr = 1000299000,
    VideoEncodeSrcKhr = 1000299001,
    VideoEncodeDpbKhr = 1000299002,
    ReadOnlyOptimalKhr = 1000314000,
    AttachmentOptimalKhr = 1000314001,
    DepthReadOnlyStencilAttachmentOptimalKhr = GfxImageLayout::DepthReadOnlyStencilAttachmentOptimal,
    DepthAttachmentStencilReadOnlyOptimalKhr = GfxImageLayout::DepthAttachmentStencilReadOnlyOptimal,
    ShadingRateOptimalNv = GfxImageLayout::FragmentShadingRateAttachmentOptimalKhr,
    DepthAttachmentOptimalKhr = GfxImageLayout::DepthAttachmentOptimal,
    DepthReadOnlyOptimalKhr = GfxImageLayout::DepthReadOnlyOptimal,
    StencilAttachmentOptimalKhr = GfxImageLayout::StencilAttachmentOptimal,
    StencilReadOnlyOptimalKhr = GfxImageLayout::StencilReadOnlyOptimal,
};

struct GfxAttachmentDesc
{
    GfxFormat format;
    GfxSampleCountFlags samples;
    GfxAttachmentLoadOp loadOp;
    GfxAttachmentStoreOp storeOp;
    GfxAttachmentLoadOp stencilLoadOp;
    GfxAttachmentStoreOp stencilStoreOp;
    GfxImageLayout initialLayout;
    GfxImageLayout finalLayout;
};

struct GfxRenderPassDesc
{
    uint32 numColorAttachments                  = 1;
    const GfxAttachmentDesc* colorAttachments   = nullptr;
    GfxAttachmentDesc depthAttachment;
};

struct GfxDescriptorBufferDesc
{
    GfxBuffer buffer;
    size_t    offset;
    size_t    size;
};

struct GfxDescriptorBindingDesc
{   
    const char* name;
    GfxDescriptorType type;
    uint32 imageArrayCount;

    union
    {
        GfxDescriptorBufferDesc buffer;
        GfxImage                image;
        const GfxImage*         imageArray;
    };
};

enum class GfxIndexType: uint32
{
    Uint16 = 0,
    Uint32 = 1,
};

struct GfxViewport
{
    float    x        = 0;
    float    y        = 0;
    float    width    = 0;
    float    height   = 0;
    float    minDepth = 0;
    float    maxDepth = 1.0f;
};

enum class GfxApiVersion : uint32
{
    Vulkan_1_0 = 100,
    Vulkan_1_1 = 110,
    Vulkan_1_2 = 120,
    Vulkan_1_3 = 130,
    _Vulkan
};

// We only include the properties that we are interested in
struct GfxPhysicalDeviceLimits
{
    float timestampPeriod;
    uint32 minTexelBufferOffsetAlignment;
    uint32 minUniformBufferOffsetAlignment;
    uint32 minStorageBufferOffsetAlignment;
};

struct GfxPhysicalDeviceProperties
{
    GfxPhysicalDeviceLimits limits;
};

struct GfxBudgetStats
{
    struct DescriptorBudgetStats
    {
        uint32 maxUniformBuffers;
        uint32 numUniformBuffers;

        uint32 maxDynUniformBuffers;
        uint32 numDynUniformBuffers;

        uint32 maxSampledImages;
        uint32 numSampledImages;

        uint32 maxSamplers;
        uint32 numSamplers;

        uint32 maxCombinedImageSamplers;
        uint32 numCombinedImageSamplers;
    };

    uint32 maxBuffers;
    uint32 numBuffers;

    uint32 maxImages;
    uint32 numImages;

    uint32 maxDescriptorSets;
    uint32 numDescriptorSets;

    uint32 maxPipelines;
    uint32 numPipelines;

    uint32 maxPipelineLayouts;
    uint32 numPipelineLayouts;

    uint32 maxGarbage;
    uint32 numGarbage;

    DescriptorBudgetStats descriptors;

    size_t initHeapStart;
    size_t initHeapSize;

    size_t runtimeHeapSize;
    size_t runtimeHeapMax;

    MemTlsfAllocator* runtimeHeap;
};

struct GfxImageInfo
{
    uint32         width;
    uint32         height;
    GfxBufferUsage memUsage;
    size_t         sizeBytes;
};


//     █████╗ ██████╗ ██╗
//    ██╔══██╗██╔══██╗██║
//    ███████║██████╔╝██║
//    ██╔══██║██╔═══╝ ██║
//    ██║  ██║██║     ██║
//    ╚═╝  ╚═╝╚═╝     ╚═╝
                       
API bool gfxHasDeviceExtension(const char* extension);
API bool gfxHasInstanceExtension(const char* extension);
API const GfxPhysicalDeviceProperties& gfxGetPhysicalDeviceProperties();

API void gfxDestroySurfaceAndSwapchain();
API void gfxRecreateSurfaceAndSwapchain();
API void gfxResizeSwapchain(uint16 width, uint16 height);

API void gfxWaitForIdle();
API void gfxGetBudgetStats(GfxBudgetStats* stats);
API float gfxGetRenderTimeNs();  // Note: This functions calls Vk functions. So not that cheap

// This function is mainly used for android platform where you need to transform the clip-space (MVPC)
// Depending on the orientation of the device
API Mat4 gfxGetClipspaceTransform();
API bool gfxIsRenderingToSwapchain();

//----------------------------------------------------------------------------------------------------------------------
// Create/Destroy resources
API GfxBuffer gfxCreateBuffer(const GfxBufferDesc& desc);
API void      gfxDestroyBuffer(GfxBuffer buffer);

API GfxImage  gfxCreateImage(const GfxImageDesc& desc);
API void      gfxDestroyImage(GfxImage image);
API GfxImageInfo gfxGetImageInfo(GfxImage img);

API GfxPipeline gfxCreatePipeline(const GfxPipelineDesc& desc);
API void        gfxDestroyPipeline(GfxPipeline pipeline);

API GfxRenderPass gfxCreateRenderPass(const GfxRenderPassDesc& desc);
API void gfxDestroyRenderPass(GfxRenderPass renderPass);

API GfxDescriptorSetLayout gfxCreateDescriptorSetLayout(const GfxShader& shader, 
                                                        const GfxDescriptorSetLayoutBinding* bindings, uint32 numBindings);
API void gfxDestroyDescriptorSetLayout(GfxDescriptorSetLayout layout);

API GfxDescriptorSet gfxCreateDescriptorSet(GfxDescriptorSetLayout layout);
API void gfxDestroyDescriptorSet(GfxDescriptorSet dset);

//----------------------------------------------------------------------------------------------------------------------
// CommandBuffer Begin/End
API bool gfxBeginCommandBuffer();
API void gfxEndCommandBuffer();

// Command functions
API void gfxCmdUpdateBuffer(GfxBuffer buffer, const void* data, uint32 size);
API void gfxCmdBindPipeline(GfxPipeline pipeline);
API void gfxCmdBindDescriptorSets(GfxPipeline pipeline, uint32 numDescriptorSets, const GfxDescriptorSet* descriptorSets, 
                                  const uint32* dynOffsets = nullptr, uint32 dynOffsetCount = 0);
API void gfxCmdBindVertexBuffers(uint32 firstBinding, uint32 numBindings, const GfxBuffer* vertexBuffers, const uint64* offsets);
API void gfxCmdBindIndexBuffer(GfxBuffer indexBuffer, uint64 offset, GfxIndexType indexType);
API void gfxCmdPushConstants(GfxPipeline pipeline, GfxShaderStage stage, const void* data, uint32 size);
API void gfxCmdBeginSwapchainRenderPass(Color bgColor = COLOR_BLACK);
API void gfxCmdEndSwapchainRenderPass();
API void gfxCmdDraw(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex, uint32 firstInstance);
API void gfxCmdDrawIndexed(uint32 indexCount, uint32 instanceCount, uint32 firstIndex, uint32 vertexOffset, uint32 firstInstance);
API void gfxCmdSetScissors(uint32 firstScissors, uint32 numScissors, const Recti* scissors, bool isSwapchain = false);
API void gfxCmdSetViewports(uint32 firstViewport, uint32 numViewports, const GfxViewport* viewports, bool isSwapchain = false);

//----------------------------------------------------------------------------------------------------------------------
// Update descriptor sets
// Should not come in between RenderPasses or you will get UB
API void gfxUpdateDescriptorSet(GfxDescriptorSet dset, uint32 numBindings, const GfxDescriptorBindingDesc* bindings);

// VkCommandBuffer
typedef struct VkCommandBuffer_T* VkCommandBuffer;

// VkBuffer
typedef uint32_t VkFlags;
struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;
typedef VkFlags VkMemoryPropertyFlags;
typedef struct VkBuffer_T* VkBuffer;

// VkDescriptorSet
typedef struct VkDescriptorSetLayout_T* VkDescriptorSetLayout;
enum VkDescriptorType : int;

// VkPipeline
struct VkGraphicsPipelineCreateInfo;
typedef struct VkPipelineLayout_T* VkPipelineLayout;
typedef struct VkPipeline_T* VkPipeline;

inline constexpr uint32 MAX_DESCRIPTOR_SETS_PER_LAYOUT = 2;

struct GfxBuffer2
{
    GfxBufferType           type;
    GfxBufferUsage          memUsage;
    uint32                  size;
    VmaAllocation           allocation;
    VkMemoryPropertyFlags   memFlags;
    VkBuffer                buffer;
    VkBuffer                stagingBuffer;
    VmaAllocation           stagingAllocation;
    void*                   mappedBuffer;
};

struct GfxDescriptorSetLayoutBinding2
{
    const char* name;
    uint32 nameHash;
    uint32 variableDescCount;
    uint32 bindingId;
    VkDescriptorType descriptorType;
};

struct GfxDescriptorSetLayout2
{
    VkDescriptorSetLayout layout;
    uint32 numBindings;
    GfxDescriptorSetLayoutBinding2* bindings;
};

struct GfxPipelineLayout2
{
    uint32 numDescriptorSetLayouts;
    GfxDescriptorSetLayout2 descriptorSetLayouts[MAX_DESCRIPTOR_SETS_PER_LAYOUT];
    VkPipelineLayout layout;
};

struct GfxPipeline2
{
    VkPipeline pipeline;
    GfxPipelineLayout2 pipelineLayout;
    VkGraphicsPipelineCreateInfo* createInfo;
};

struct GfxCommandBuffer2
{
    void UpdateBuffer(GfxBuffer2& buffer, const void* data, size_t size) const;

    void BindPipeline(const GfxPipeline2& pipeline) const;
    void BindDescriptorSets(const GfxPipelineLayout2& layout, uint32 numDescriptorSets, const GfxDescriptorSet* descriptorSets, 
                            const uint32* dynOffsets = nullptr, uint32 dynOffsetCount = 0) const;
    void BindVertexBuffers(uint32 firstBinding, uint32 numBindings, const GfxBuffer* vertexBuffers, const uint64* offsets) const;
    void BindIndexBuffer(const GfxBuffer2& indexBuffer, uint64 offset, GfxIndexType indexType) const;

    void PushConstants(const GfxPipeline2& pipeline, GfxShaderStage stage, const void* data, uint32 size) const;
    void Draw(uint32 vertexCount, uint32 instanceCount, uint32 firstVertex, uint32 firstInstance) const;
    void DrawIndexed(uint32 indexCount, uint32 instanceCount, uint32 firstIndex, uint32 vertexOffset, uint32 firstInstance) const;
    void SetScissors(uint32 firstScissors, uint32 numScissors, const Recti* scissors, bool isSwapchain = false) const;
    void SetViewports(uint32 firstViewport, uint32 numViewports, const GfxViewport* viewports, bool isSwapchain = false) const;

    static GfxCommandBuffer2 Begin();
    void End();

    void BeginSwapchainRenderPass(Color bgColor = COLOR_BLACK);
    void EndSwapchainRenderPass();

    VkCommandBuffer mCmdBuffer;
};

//    ██████╗ ██████╗  ██████╗ ███████╗██╗██╗     ██╗███╗   ██╗ ██████╗ 
//    ██╔══██╗██╔══██╗██╔═══██╗██╔════╝██║██║     ██║████╗  ██║██╔════╝ 
//    ██████╔╝██████╔╝██║   ██║█████╗  ██║██║     ██║██╔██╗ ██║██║  ███╗
//    ██╔═══╝ ██╔══██╗██║   ██║██╔══╝  ██║██║     ██║██║╚██╗██║██║   ██║
//    ██║     ██║  ██║╚██████╔╝██║     ██║███████╗██║██║ ╚████║╚██████╔╝
//    ╚═╝     ╚═╝  ╚═╝ ╚═════╝ ╚═╝     ╚═╝╚══════╝╚═╝╚═╝  ╚═══╝ ╚═════╝ 
#ifdef TRACY_ENABLE
    #include "../Core/TracyHelper.h"

    namespace _private
    {
        API void gfxProfileZoneBegin(uint64 srcloc);
        API void gfxProfileZoneEnd();

        struct TracyGpuZoneScope
        {
            bool _active;

            TracyGpuZoneScope() = delete;
            explicit TracyGpuZoneScope(bool active, uint64 srcloc) : 
                _active(active) 
            {
                if (active)
                    gfxProfileZoneBegin(srcloc);
            }
            
            ~TracyGpuZoneScope()
            {
                if (_active)
                    gfxProfileZoneEnd();
            }
        };

    }   // _private

    #define PROFILE_GPU_ZONE(active) \
        _private::TracyGpuZoneScope(active, _private::__tracy_alloc_source_loc(__LINE__, __FILE__, __func__))
    #define PROFILE_GPU_ZONE_NAME(name, active) \
        _private::TracyGpuZoneScope(active, _private::__tracy_alloc_source_loc(__LINE__, __FILE__, __func__, name))
    #define PROFILE_GPU_ZONE_BEGIN(active) \
        do { if (active) _private::profileGpuZoneBegin(_private::__tracy_alloc_source_loc(__LINE__, __FILE__, __func__));  } while(0)
    #define PROFILE_GPU_ZONE_NAME_BEGIN(name, active) \
        do { if (active) _private::profileGpuZoneBegin(_private::__tracy_alloc_source_loc(__LINE__, __FILE__, __func__, name));  } while(0)
    #define PROFILE_GPU_ZONE_END(active)  \
        do { if (active) _private::profileGpuZoneEnd();  } while(0)
#else
    #define PROFILE_GPU_ZONE(active)
    #define PROFILE_GPU_ZONE_NAME(name, active)
    #define PROFILE_GPU_ZONE_BEGIN(active)
    #define PROFILE_GPU_ZONE_END(active)
#endif // TRACY_ENABLE


//    ██████╗ ██╗   ██╗███╗   ██╗ █████╗ ███╗   ███╗██╗ ██████╗    ██╗   ██╗██████╗  ██████╗ 
//    ██╔══██╗╚██╗ ██╔╝████╗  ██║██╔══██╗████╗ ████║██║██╔════╝    ██║   ██║██╔══██╗██╔═══██╗
//    ██║  ██║ ╚████╔╝ ██╔██╗ ██║███████║██╔████╔██║██║██║         ██║   ██║██████╔╝██║   ██║
//    ██║  ██║  ╚██╔╝  ██║╚██╗██║██╔══██║██║╚██╔╝██║██║██║         ██║   ██║██╔══██╗██║   ██║
//    ██████╔╝   ██║   ██║ ╚████║██║  ██║██║ ╚═╝ ██║██║╚██████╗    ╚██████╔╝██████╔╝╚██████╔╝
//    ╚═════╝    ╚═╝   ╚═╝  ╚═══╝╚═╝  ╚═╝╚═╝     ╚═╝╚═╝ ╚═════╝     ╚═════╝ ╚═════╝  ╚═════╝ 
struct GfxDyanmicUniformBufferRange
{
    uint32 index;
    uint32 count;
};

struct GfxDynamicUniformBuffer
{
    void* Data(uint32 index);
    uint32 Offset(uint32 index);

    bool IsValid() const;
    void Flush(const GfxDyanmicUniformBufferRange* ranges, uint32 numRanges);
    void Flush(uint32 index, uint32 _count);

    GfxBuffer buffer;
    uint8* bufferPtr;
    uint32 stride;
    uint32 count;
};

GfxDynamicUniformBuffer gfxCreateDynamicUniformBuffer(uint32 count, uint32 stride);
void gfxDestroyDynamicUniformBuffer(GfxDynamicUniformBuffer& buffer);

//----------------------------------------------------------------------------------------------------------------------
namespace _private 
{
    bool gfxInitialize();
    void gfxRelease();

    void gfxReleaseImageManager();
    void gfxRecreatePipelinesWithNewShader(uint32 shaderHash, GfxShader* shader);

    using GfxUpdateImageDescriptorCallback = void(*)(GfxDescriptorSet dset, uint32 numBindings, const GfxDescriptorBindingDesc* bindings);
    void gfxSetUpdateImageDescriptorCallback(GfxUpdateImageDescriptorCallback callback);

    // Begin/End frame (TODO: Take this to private namespace)
    void gfxBeginFrame();
    void gfxEndFrame();


} // _private

//    ██╗███╗   ██╗██╗     ██╗███╗   ██╗███████╗███████╗
//    ██║████╗  ██║██║     ██║████╗  ██║██╔════╝██╔════╝
//    ██║██╔██╗ ██║██║     ██║██╔██╗ ██║█████╗  ███████╗
//    ██║██║╚██╗██║██║     ██║██║╚██╗██║██╔══╝  ╚════██║
//    ██║██║ ╚████║███████╗██║██║ ╚████║███████╗███████║
//    ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝╚═╝  ╚═══╝╚══════╝╚══════╝
inline void* GfxDynamicUniformBuffer::Data(uint32 index)
{
#ifdef CONFIG_CHECK_OUTOFBOUNDS    
    ASSERT_MSG(index < count, "Out of bounds access for dynamic buffer");
#endif
    
    return bufferPtr + stride*index;
}

inline uint32 GfxDynamicUniformBuffer::Offset(uint32 index)
{
    return stride*index;
}

inline void GfxDynamicUniformBuffer::Flush(uint32 index, uint32 _count)
{
    GfxDyanmicUniformBufferRange range { index, _count };
    Flush(&range, 1);
}


inline const GfxBlendAttachmentDesc* GfxBlendAttachmentDesc::GetDefault()
{
    static GfxBlendAttachmentDesc desc {
        .enable = true,
        .srcColorBlendFactor = GfxBlendFactor::One,
        .dstColorBlendFactor = GfxBlendFactor::Zero,
        .blendOp = GfxBlendOp::Add,
        .srcAlphaBlendFactor = GfxBlendFactor::One,
        .dstAlphaBlendFactor = GfxBlendFactor::Zero,
        .alphaBlendOp = GfxBlendOp::Add,
        .colorWriteMask = GfxColorComponentFlags::All
    };
    return &desc;
}

inline const GfxBlendAttachmentDesc* GfxBlendAttachmentDesc::GetAlphaBlending()
{
    static GfxBlendAttachmentDesc desc {
        .enable = true,
        .srcColorBlendFactor = GfxBlendFactor::SrcAlpha,
        .dstColorBlendFactor = GfxBlendFactor::OneMinusSrcAlpha,
        .blendOp = GfxBlendOp::Add,
        .srcAlphaBlendFactor = GfxBlendFactor::One,
        .dstAlphaBlendFactor = GfxBlendFactor::Zero,
        .alphaBlendOp = GfxBlendOp::Add,
        .colorWriteMask = GfxColorComponentFlags::RGB
    };
    return &desc;
}
