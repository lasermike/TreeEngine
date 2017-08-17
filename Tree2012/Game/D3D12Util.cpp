#include <pch.h>

#include <nmmintrin.h>
#include <algorithm>

namespace XboxSampleFramework
{


//--------------------------------------------------------------------------------------
// Macros
//--------------------------------------------------------------------------------------
#ifndef MAKEFOURCC
    #define MAKEFOURCC(ch0, ch1, ch2, ch3)                              \
                ((uint32_t)(uint8_t)(ch0) | ((uint32_t)(uint8_t)(ch1) << 8) |       \
                ((uint32_t)(uint8_t)(ch2) << 16) | ((uint32_t)(uint8_t)(ch3) << 24))
#endif /* defined(MAKEFOURCC) */

//--------------------------------------------------------------------------------------
// DDS file structure definitions
//
// See DDS.h in the 'Texconv' sample and the 'DirectXTex' library
//--------------------------------------------------------------------------------------
#pragma pack(push,1)

const uint32_t DDS_MAGIC = 0x20534444; // "DDS "

struct DDS_PIXELFORMAT
{
    uint32_t    size;
    uint32_t    flags;
    uint32_t    fourCC;
    uint32_t    RGBBitCount;
    uint32_t    RBitMask;
    uint32_t    GBitMask;
    uint32_t    BBitMask;
    uint32_t    ABitMask;
};

#define DDS_FOURCC      0x00000004  // DDPF_FOURCC
#define DDS_RGB         0x00000040  // DDPF_RGB
#define DDS_LUMINANCE   0x00020000  // DDPF_LUMINANCE
#define DDS_ALPHA       0x00000002  // DDPF_ALPHA

#define DDS_HEADER_FLAGS_VOLUME         0x00800000  // DDSD_DEPTH

#define DDS_HEIGHT 0x00000002 // DDSD_HEIGHT
#define DDS_WIDTH  0x00000004 // DDSD_WIDTH

#define DDS_CUBEMAP_POSITIVEX 0x00000600 // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEX
#define DDS_CUBEMAP_NEGATIVEX 0x00000a00 // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEX
#define DDS_CUBEMAP_POSITIVEY 0x00001200 // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEY
#define DDS_CUBEMAP_NEGATIVEY 0x00002200 // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEY
#define DDS_CUBEMAP_POSITIVEZ 0x00004200 // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_POSITIVEZ
#define DDS_CUBEMAP_NEGATIVEZ 0x00008200 // DDSCAPS2_CUBEMAP | DDSCAPS2_CUBEMAP_NEGATIVEZ

#define DDS_CUBEMAP_ALLFACES (DDS_CUBEMAP_POSITIVEX | DDS_CUBEMAP_NEGATIVEX |\
                               DDS_CUBEMAP_POSITIVEY | DDS_CUBEMAP_NEGATIVEY |\
                               DDS_CUBEMAP_POSITIVEZ | DDS_CUBEMAP_NEGATIVEZ)

#define DDS_CUBEMAP 0x00000200 // DDSCAPS2_CUBEMAP

enum DDS_MISC_FLAGS2
{
    DDS_MISC_FLAGS2_ALPHA_MODE_MASK = 0x7L,
};

struct DDS_HEADER
{
    uint32_t        size;
    uint32_t        flags;
    uint32_t        height;
    uint32_t        width;
    uint32_t        pitchOrLinearSize;
    uint32_t        depth; // only if DDS_HEADER_FLAGS_VOLUME is set in flags
    uint32_t        mipMapCount;
    uint32_t        reserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t        caps;
    uint32_t        caps2;
    uint32_t        caps3;
    uint32_t        caps4;
    uint32_t        reserved2;
};

struct DDS_HEADER_DXT10
{
    DXGI_FORMAT     dxgiFormat;
    uint32_t        resourceDimension;
    uint32_t        miscFlag; // see D3D11_RESOURCE_MISC_FLAG
    uint32_t        arraySize;
    uint32_t        miscFlags2;
};

#pragma pack(pop)

//--------------------------------------------------------------------------------------
#define ISBITMASK(r,g,b,a) (ddpf.RBitMask == r && ddpf.GBitMask == g && ddpf.BBitMask == b && ddpf.ABitMask == a)

static DXGI_FORMAT GetDXGIFormat(const DDS_PIXELFORMAT& ddpf)
{
    if (ddpf.flags & DDS_RGB)
    {
        // Note that sRGB formats are written using the "DX10" extended header

        switch (ddpf.RGBBitCount)
        {
        case 32:
            if (ISBITMASK(0x000000ff,0x0000ff00,0x00ff0000,0xff000000))
            {
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            }

            if (ISBITMASK(0x00ff0000,0x0000ff00,0x000000ff,0xff000000))
            {
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            }

            if (ISBITMASK(0x00ff0000,0x0000ff00,0x000000ff,0x00000000))
            {
                return DXGI_FORMAT_B8G8R8X8_UNORM;
            }

            // No DXGI format maps to ISBITMASK(0x000000ff,0x0000ff00,0x00ff0000,0x00000000) aka D3DFMT_X8B8G8R8

            // Note that many common DDS reader/writers (including D3DX) swap the
            // the RED/BLUE masks for 10:10:10:2 formats. We assumme
            // below that the 'backwards' header mask is being used since it is most
            // likely written by D3DX. The more robust solution is to use the 'DX10'
            // header extension and specify the DXGI_FORMAT_R10G10B10A2_UNORM format directly

            // For 'correct' writers, this should be 0x000003ff,0x000ffc00,0x3ff00000 for RGB data
            if (ISBITMASK(0x3ff00000,0x000ffc00,0x000003ff,0xc0000000))
            {
                return DXGI_FORMAT_R10G10B10A2_UNORM;
            }

            // No DXGI format maps to ISBITMASK(0x000003ff,0x000ffc00,0x3ff00000,0xc0000000) aka D3DFMT_A2R10G10B10

            if (ISBITMASK(0x0000ffff,0xffff0000,0x00000000,0x00000000))
            {
                return DXGI_FORMAT_R16G16_UNORM;
            }

            if (ISBITMASK(0xffffffff,0x00000000,0x00000000,0x00000000))
            {
                // Only 32-bit color channel format in D3D9 was R32F
                return DXGI_FORMAT_R32_FLOAT; // D3DX writes this out as a FourCC of 114
            }
            break;

        case 24:
            // No 24bpp DXGI formats aka D3DFMT_R8G8B8
            break;

        case 16:
            if (ISBITMASK(0x7c00,0x03e0,0x001f,0x8000))
            {
                return DXGI_FORMAT_B5G5R5A1_UNORM;
            }
            if (ISBITMASK(0xf800,0x07e0,0x001f,0x0000))
            {
                return DXGI_FORMAT_B5G6R5_UNORM;
            }

            // No DXGI format maps to ISBITMASK(0x7c00,0x03e0,0x001f,0x0000) aka D3DFMT_X1R5G5B5

            if (ISBITMASK(0x0f00,0x00f0,0x000f,0xf000))
            {
                return DXGI_FORMAT_B4G4R4A4_UNORM;
            }

            // No DXGI format maps to ISBITMASK(0x0f00,0x00f0,0x000f,0x0000) aka D3DFMT_X4R4G4B4

            // No 3:3:2, 3:3:2:8, or paletted DXGI formats aka D3DFMT_A8R3G3B2, D3DFMT_R3G3B2, D3DFMT_P8, D3DFMT_A8P8, etc.
            break;
        }
    }
    else if (ddpf.flags & DDS_LUMINANCE)
    {
        if (8 == ddpf.RGBBitCount)
        {
            if (ISBITMASK(0x000000ff,0x00000000,0x00000000,0x00000000))
            {
                return DXGI_FORMAT_R8_UNORM; // D3DX10/11 writes this out as DX10 extension
            }

            // No DXGI format maps to ISBITMASK(0x0f,0x00,0x00,0xf0) aka D3DFMT_A4L4
        }

        if (16 == ddpf.RGBBitCount)
        {
            if (ISBITMASK(0x0000ffff,0x00000000,0x00000000,0x00000000))
            {
                return DXGI_FORMAT_R16_UNORM; // D3DX10/11 writes this out as DX10 extension
            }
            if (ISBITMASK(0x000000ff,0x00000000,0x00000000,0x0000ff00))
            {
                return DXGI_FORMAT_R8G8_UNORM; // D3DX10/11 writes this out as DX10 extension
            }
        }
    }
    else if (ddpf.flags & DDS_ALPHA)
    {
        if (8 == ddpf.RGBBitCount)
        {
            return DXGI_FORMAT_A8_UNORM;
        }
    }
    else if (ddpf.flags & DDS_FOURCC)
    {
        if (MAKEFOURCC('D', 'X', 'T', '1') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC1_UNORM;
        }
        if (MAKEFOURCC('D', 'X', 'T', '3') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC2_UNORM;
        }
        if (MAKEFOURCC('D', 'X', 'T', '5') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC3_UNORM;
        }

        // While pre-mulitplied alpha isn't directly supported by the DXGI formats,
        // they are basically the same as these BC formats so they can be mapped
        if (MAKEFOURCC('D', 'X', 'T', '2') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC2_UNORM;
        }
        if (MAKEFOURCC('D', 'X', 'T', '4') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC3_UNORM;
        }

        if (MAKEFOURCC('A', 'T', 'I', '1') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC4_UNORM;
        }
        if (MAKEFOURCC('B', 'C', '4', 'U') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC4_UNORM;
        }
        if (MAKEFOURCC('B', 'C', '4', 'S') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC4_SNORM;
        }

        if (MAKEFOURCC('A', 'T', 'I', '2') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC5_UNORM;
        }
        if (MAKEFOURCC('B', 'C', '5', 'U') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC5_UNORM;
        }
        if (MAKEFOURCC('B', 'C', '5', 'S') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC5_SNORM;
        }

        // BC6H and BC7 are written using the "DX10" extended header

        if (MAKEFOURCC('R', 'G', 'B', 'G') == ddpf.fourCC)
        {
            return DXGI_FORMAT_R8G8_B8G8_UNORM;
        }
        if (MAKEFOURCC('G', 'R', 'G', 'B') == ddpf.fourCC)
        {
            return DXGI_FORMAT_G8R8_G8B8_UNORM;
        }

        if (MAKEFOURCC('Y','U','Y','2') == ddpf.fourCC)
        {
            return DXGI_FORMAT_YUY2;
        }

        // Check for D3DFORMAT enums being set here
        switch(ddpf.fourCC)
        {
        case 36: // D3DFMT_A16B16G16R16
            return DXGI_FORMAT_R16G16B16A16_UNORM;

        case 110: // D3DFMT_Q16W16V16U16
            return DXGI_FORMAT_R16G16B16A16_SNORM;

        case 111: // D3DFMT_R16F
            return DXGI_FORMAT_R16_FLOAT;

        case 112: // D3DFMT_G16R16F
            return DXGI_FORMAT_R16G16_FLOAT;

        case 113: // D3DFMT_A16B16G16R16F
            return DXGI_FORMAT_R16G16B16A16_FLOAT;

        case 114: // D3DFMT_R32F
            return DXGI_FORMAT_R32_FLOAT;

        case 115: // D3DFMT_G32R32F
            return DXGI_FORMAT_R32G32_FLOAT;

        case 116: // D3DFMT_A32B32G32R32F
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        }
    }

    return DXGI_FORMAT_UNKNOWN;
}



//--------------------------------------------------------------------------------------
// Determines if the format is block compressed
//--------------------------------------------------------------------------------------
static bool IsCompressed(_In_ DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return true;

    default:
        return false;
    }
}


//--------------------------------------------------------------------------------------
// Return the BPP for a particular format
//--------------------------------------------------------------------------------------
UINT32 BitsPerPixel(_In_ DXGI_FORMAT fmt)
{
    switch(fmt)
    {
    case DXGI_FORMAT_R32G32B32A32_TYPELESS:
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 128;

    case DXGI_FORMAT_R32G32B32_TYPELESS:
    case DXGI_FORMAT_R32G32B32_FLOAT:
    case DXGI_FORMAT_R32G32B32_UINT:
    case DXGI_FORMAT_R32G32B32_SINT:
        return 96;

    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32G32_TYPELESS:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
    case DXGI_FORMAT_X32_TYPELESS_G8X24_UINT:
    case DXGI_FORMAT_Y416:
    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
        return 64;

    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_UINT:
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UINT:
    case DXGI_FORMAT_R8G8B8A8_SNORM:
    case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16G16_TYPELESS:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R16G16_UINT:
    case DXGI_FORMAT_R16G16_SNORM:
    case DXGI_FORMAT_R16G16_SINT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_UINT:
    case DXGI_FORMAT_R32_SINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X24_TYPELESS_G8_UINT:
    case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
    case DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
    case DXGI_FORMAT_AYUV:
    case DXGI_FORMAT_Y410:
    case DXGI_FORMAT_YUY2:
        return 32;

    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
        return 24;

    case DXGI_FORMAT_R8G8_TYPELESS:
    case DXGI_FORMAT_R8G8_UNORM:
    case DXGI_FORMAT_R8G8_UINT:
    case DXGI_FORMAT_R8G8_SNORM:
    case DXGI_FORMAT_R8G8_SINT:
    case DXGI_FORMAT_R16_TYPELESS:
    case DXGI_FORMAT_R16_FLOAT:
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_UNORM:
    case DXGI_FORMAT_R16_UINT:
    case DXGI_FORMAT_R16_SNORM:
    case DXGI_FORMAT_R16_SINT:
    case DXGI_FORMAT_B5G6R5_UNORM:
    case DXGI_FORMAT_B5G5R5A1_UNORM:
    case DXGI_FORMAT_A8P8:
    case DXGI_FORMAT_B4G4R4A4_UNORM:
        return 16;

    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_420_OPAQUE:
    case DXGI_FORMAT_NV11:
        return 12;

    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
    case DXGI_FORMAT_AI44:
    case DXGI_FORMAT_IA44:
    case DXGI_FORMAT_P8:
        return 8;

    case DXGI_FORMAT_R1_UNORM:
        return 1;

    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return 4;

    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return 8;

#if defined(_XBOX_ONE) && defined(_TITLE)

    case DXGI_FORMAT_R10G10B10_7E3_A2_FLOAT:
    case DXGI_FORMAT_R10G10B10_6E4_A2_FLOAT:
        return 32;

    case DXGI_FORMAT_D16_UNORM_S8_UINT:
    case DXGI_FORMAT_R16_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X16_TYPELESS_G8_UINT:
        return 24;

#endif // _XBOX_ONE && _TITLE

    default:
        return 0;
    }
}


//-------------------------------------------------------------------------------------------------
// Calculates the 32-bit CRC for the specified dword-aligned buffer.
//-------------------------------------------------------------------------------------------------

//-------------------------------------------------------------------------------------------------
// Detect SSE4 support.

extern "C" void __cpuid(int cpu_info[4], int info_type);

#if defined(_X86_) || defined(_AMD64_)
#pragma intrinsic (__cpuid)
#endif

#define FORCE_SCALAR_FPU 0

static bool
can_use_sse4()
{
#if (defined(_X86_) || defined(_AMD64_)) && !FORCE_SCALAR_FPU
    int cpu_info[4] = {};

#ifdef _X86_
    // Note that cpuid instruction is not support on some older CPUs. This
    // is fine given that this test will not produce results in any reasonable
    // amount of time on such processors.
#endif /* _X86 */

    __cpuid(cpu_info, 1);

    return ((cpu_info[2] & 0x100000) != 0);
#else
    return false;
#endif /* (_X86_ || _AMD64_) && !FORCE_SCALAR_FPU */
}

bool s_can_use_sse4 = can_use_sse4();


//-------------------------------------------------------------------------------------------------
// Calculates the 32-bit CRC for the specified dword-aligned buffer. XBOX only, needs crc32 instruction which might not be present on PC.
#ifdef _AMD64_
UINT32 ComputeCrc32SSE4(UINT32 crc, const void* pBuffer, size_t byteCount)
{
    size_t byteRemainder = byteCount;
    UINT64 crc32 = crc;

    const UINT64* pSource64 = (const UINT64*) pBuffer;
    while (byteRemainder >= 32)
    {
        crc32 = _mm_crc32_u64(crc32, pSource64[0]);
        crc32 = _mm_crc32_u64(crc32, pSource64[1]);
        crc32 = _mm_crc32_u64(crc32, pSource64[2]);
        crc32 = _mm_crc32_u64(crc32, pSource64[3]);
        pSource64 += 4;
        byteRemainder -= 32;
    }

    const UINT32* pSource32 = (const UINT32*) pSource64;
    while (byteRemainder >= 4)
    {
        crc32 = _mm_crc32_u32(static_cast<UINT32>(crc32), *pSource32);
        pSource32++;
        byteRemainder -= 4;
    }

    if (byteRemainder > 0)
    {
        // Mask out the extra bytes in the last dword by simply shifting them away:
        const BYTE * pSource8 = (const BYTE*)pSource32;
        union
        {
            UINT32 u32;
            BYTE   bytes[4];
        };
        bytes[0] = (byteRemainder > 0) ? pSource8[0] : 0;
        bytes[1] = (byteRemainder > 1) ? pSource8[1] : 0;
        bytes[2] = (byteRemainder > 2) ? pSource8[2] : 0;
        bytes[3] = 0;
        crc32 = _mm_crc32_u32(static_cast<UINT32>(crc32), u32);
    }

    return static_cast<UINT32>(crc32);
}
#endif

const UINT32 CrcTable32[ 256 ] = {
0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F,
0xE963A535, 0x9E6495A3, 0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91, 0x1DB71064, 0x6AB020F2,
0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9,
0xFA0F3D63, 0x8D080DF5, 0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B, 0x35B5A8FA, 0x42B2986C,
0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423,
0xCFBA9599, 0xB8BDA50F, 0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D, 0x76DC4190, 0x01DB7106,
0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D,
0x91646C97, 0xE6635C01, 0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457, 0x65B0D9C6, 0x12B7E950,
0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7,
0xA4D1C46D, 0xD3D6F4FB, 0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9, 0x5005713C, 0x270241AA,
0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81,
0xB7BD5C3B, 0xC0BA6CAD, 0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683, 0xE3630B12, 0x94643B84,
0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB,
0x196C3671, 0x6E6B06E7, 0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5, 0xD6D6A3E8, 0xA1D1937E,
0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55,
0x316E8EEF, 0x4669BE79, 0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F, 0xC5BA3BBE, 0xB2BD0B28,
0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F,
0x72076785, 0x05005713, 0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21, 0x86D3D2D4, 0xF1D4E242,
0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69,
0x616BFFD3, 0x166CCF45, 0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB, 0xAED16A4A, 0xD9D65ADC,
0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693,
0x54DE5729, 0x23D967BF, 0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D };

//
//  Crc32 implementation in 'C' for other platforms.
//

UINT32 ComputeCrc32Table(UINT32 InitialCrc, const void* Buffer, size_t Bytes)
{
    UINT32 Crc = InitialCrc;
    const unsigned char *p = (unsigned char *)Buffer;
    size_t Count = Bytes;

    while (Count--)
        Crc = (Crc >> 8) ^ CrcTable32[ ((unsigned char)(Crc)) ^ *p++ ];

    return Crc;
}

UINT32 ComputeCrc32(const void* Buffer, size_t Bytes, _In_opt_ UINT32 InitialCrc = 0xffffffff)
{
#ifdef _AMD64_
	if (s_can_use_sse4)
	{
		return ComputeCrc32SSE4(InitialCrc, Buffer, Bytes);
	}
#endif
	return ComputeCrc32Table(InitialCrc, Buffer, Bytes);
}


//--------------------------------------------------------------------------------------
// Prepares the backbuffer for rendering
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void PrepareBackBufferForRendering(D3DCommandList* pCmdList, ID3D12Resource* pBackBufferTexture)
{
    D3D12_RESOURCE_BARRIER barrierDesc = {};

    barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Transition.pResource = pBackBufferTexture;
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    pCmdList->ResourceBarrier(1, &barrierDesc);
}

//--------------------------------------------------------------------------------------
// Prepares the depth stencil for rendering
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void PrepareDepthStencilBufferForRendering(D3DCommandList* pCmdList, ID3D12Resource* pDepthStencilTexture)
{
    D3D12_RESOURCE_BARRIER barrierDesc = {};

    barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Transition.pResource = pDepthStencilTexture;
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    pCmdList->ResourceBarrier(1, &barrierDesc);
}

//--------------------------------------------------------------------------------------
// Prepares the backbuffer for presentation
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void PrepareBackBufferForPresent(D3DCommandList* pCmdList, ID3D12Resource* pBackBufferTexture)
{
    D3D12_RESOURCE_BARRIER barrierDesc = {};

    barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Transition.pResource = pBackBufferTexture;
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

    pCmdList->ResourceBarrier(1, &barrierDesc);
}

//--------------------------------------------------------------------------------------
// Inserts a resource transition operation in the command list
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void ResourceBarrier(D3DCommandList* pCmdList, ID3D12Resource* pResource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After, D3D12_RESOURCE_BARRIER_FLAGS Flags)
{
    D3D12_RESOURCE_BARRIER barrierDesc = {};

    barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Flags = Flags;
    barrierDesc.Transition.pResource = pResource;
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = Before;
    barrierDesc.Transition.StateAfter = After;

    pCmdList->ResourceBarrier(1, &barrierDesc);
}


//--------------------------------------------------------------------------------------
// Determines if the format is compressed
//--------------------------------------------------------------------------------------
bool IsBlockCompressedFormat(DXGI_FORMAT Format)
{
    switch (Format)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}

//--------------------------------------------------------------------------------------
// Operator() for hash function
//--------------------------------------------------------------------------------------
std::size_t PSOCache::PSODescHashFunction::operator() (const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODesc) const
{
    return ComputeCrc32(&PSODesc, sizeof(PSODesc));
}

//--------------------------------------------------------------------------------------
// Operator() for equal function
//--------------------------------------------------------------------------------------
bool PSOCache::PSODescEqualFunction::operator() (const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODescA, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODescB) const
{
    return memcmp(&PSODescA, &PSODescB, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC)) == 0;
}

//--------------------------------------------------------------------------------------
// Duplicates the data in pSrc and adds it to the allocation list
//--------------------------------------------------------------------------------------
void* PSOCache::DuplicateMemory(const void* pSrc, size_t SizeBytes)
{
    void* pDupe = malloc(SizeBytes);
    if (pDupe == nullptr)
    {
        return nullptr;
    }
    memcpy(pDupe, pSrc, SizeBytes);
    m_Allocations.push_back(pDupe);

    return pDupe;
}

//--------------------------------------------------------------------------------------
// Initializes the caches with the given template. Adds a ref to the device
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void PSOCache::Initialize(D3DDevice* pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSOTemplate)
{
    if (!m_Map.empty())
    {
        Terminate();
    }
    m_PSOTemplate = PSOTemplate;
    m_pDevice = pDevice;
    pDevice->AddRef();
}

//--------------------------------------------------------------------------------------
// Terminate the cache and frees up the memory
//--------------------------------------------------------------------------------------
void PSOCache::Terminate()
{
    {
        auto iter = m_Map.begin();
        auto end = m_Map.end();
        while (iter != end)
        {
            ID3D12PipelineState* pPSO = iter->second;
            pPSO->Release();
            ++iter;
        }
        m_Map.clear();
    }

    {
        auto iter = m_Allocations.begin();
        auto end = m_Allocations.end();
        while (iter != end)
        {
            void* pMem = *iter;
            free(pMem);
            ++iter;
        }
        m_Allocations.clear();
    }

    XSF_SAFE_RELEASE(m_pDevice);
}

//--------------------------------------------------------------------------------------
// Find a cached PSO and return a pointer to it, or null if not found
//--------------------------------------------------------------------------------------
ID3D12PipelineState* PSOCache::FindPSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODesc) const
{
    auto iter = m_Map.find(PSODesc);
    if (iter != m_Map.end())
    {
        return iter->second;
    }
    return nullptr;
}

//--------------------------------------------------------------------------------------
// Find and return an existing PSO, or create a new one
//--------------------------------------------------------------------------------------
ID3D12PipelineState* PSOCache::FindOrCreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODesc)
{
    ID3D12PipelineState* pPSO = FindPSO(PSODesc);
    if (pPSO != nullptr)
    {
        return pPSO;
    }

    HRESULT hr = m_pDevice->CreateGraphicsPipelineState(&PSODesc, IID_GRAPHICS_PPV_ARGS(&pPSO));
    if (SUCCEEDED(hr))
    {
        m_Map[PSODesc] = pPSO;
        return pPSO;
    }

    return nullptr;
}


//--------------------------------------------------------------------------------------
// Initialize the rename buffer array
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT D3D12DynamicBuffer::Create(D3DDevice* const pDevice, ID3D12Fence* const pFence, UINT32 ByteWidth, UINT32 VertexStrideBytes, UINT32 InitialRenameCount)
{
    m_CurrentBufferIndex = 0;
    m_ByteWidth = static_cast<UINT32>(NextMultiple(ByteWidth, 256U));
    m_VertexStrideBytes = VertexStrideBytes;

    m_pDevice = pDevice;
    m_pDevice->AddRef();

    m_pFence = pFence;
    m_pFence->AddRef();

    InitialRenameCount = std::min(256U, std::max(1U, InitialRenameCount));

    for (UINT32 i = 0; i < InitialRenameCount; ++i)
    {
        UINT32 Index = FindOrCreateRenameBuffer(nullptr, true);
        if (Index == -1)
        {
            return E_FAIL;
        }
    }
    return S_OK;
}

//--------------------------------------------------------------------------------------
// Terminate the class and free up its memory
//--------------------------------------------------------------------------------------
void D3D12DynamicBuffer::Terminate()
{
    UINT32 BufferCount = static_cast<UINT32>(m_RenameBuffers.size());
    for (UINT32 i = 0; i < BufferCount; ++i)
    {
        RenameBuffer& RB = m_RenameBuffers[i];
        XSF_SAFE_RELEASE(RB.m_pBuffer);
    }
    m_RenameBuffers.clear();
    XSF_SAFE_RELEASE(m_pFence);
    XSF_SAFE_RELEASE(m_pDevice);
}

//--------------------------------------------------------------------------------------
// Emulate the MapDiscard functionality
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT D3D12DynamicBuffer::MapDiscard(D3DCommandQueue* pCmdQueue, void** ppMapData)
{
    UINT32 RenameIndex = FindOrCreateRenameBuffer(pCmdQueue, false);
    if (RenameIndex == -1)
    {
        return E_OUTOFMEMORY;
    }
    m_CurrentBufferIndex = RenameIndex;

    return MapNoOverwrite(ppMapData);
}

//--------------------------------------------------------------------------------------
// Emulate the MapNoOverwrite functionality
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT D3D12DynamicBuffer::MapNoOverwrite(void** ppMapData)
{
    XSF_ASSERT(m_CurrentBufferIndex < m_RenameBuffers.size());
    XSF_ASSERT(ppMapData != nullptr);
    RenameBuffer& RB = m_RenameBuffers[m_CurrentBufferIndex];
    RB.m_writeFence = m_pFence->GetCompletedValue() + 1;
    HRESULT hr = RB.m_pBuffer->Map(0, nullptr, ppMapData);

    return hr;
}

//--------------------------------------------------------------------------------------
// Emulate the Unmap functionality
//--------------------------------------------------------------------------------------
void D3D12DynamicBuffer::Unmap(D3DCommandQueue* /*pCmdQueue*/)
{
    XSF_ASSERT(m_CurrentBufferIndex < m_RenameBuffers.size());
    RenameBuffer& RB = m_RenameBuffers[m_CurrentBufferIndex];
    RB.m_pBuffer->Unmap(0, nullptr);
}

//--------------------------------------------------------------------------------------
// Finds the first available rename buffer, or creates a new one
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
UINT32 D3D12DynamicBuffer::FindOrCreateRenameBuffer(D3DCommandQueue* pCmdQueue, bool ForceCreate)
{
    UINT32 FoundIndex = static_cast<UINT32>(-1);
    if (!ForceCreate)
    {
        XSF_ASSERT(pCmdQueue != nullptr);

        // find an unused rename buffer
        const UINT64 CompletedFence = m_pFence->GetCompletedValue();
        if (CompletedFence == 0)
        {
            return 0;
        }

        const UINT32 BufferCount = static_cast<UINT32>(m_RenameBuffers.size());
        for (UINT32 i = 0; i < BufferCount; ++i)
        {
            const UINT32 OffsetIndex = (i + m_CurrentBufferIndex) % BufferCount;
            const RenameBuffer& RB = m_RenameBuffers[OffsetIndex];
            if (RB.m_writeFence <= CompletedFence)
            {
                return OffsetIndex;
            }
        }
        ForceCreate = true;
    }

    if (ForceCreate)
    {
        RenameBuffer NewBuffer;
        NewBuffer.m_writeFence = 0;

        const D3D12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_ByteWidth);
        HRESULT hr = m_pDevice->CreateCommittedResource(
            &uploadHeapProperties, 
            D3D12_HEAP_FLAG_NONE, 
            &bufferDesc, 
            D3D12_RESOURCE_STATE_GENERIC_READ, 
            nullptr, 
            IID_GRAPHICS_PPV_ARGS(&NewBuffer.m_pBuffer));
        if (FAILED(hr))
        {
            XSF_SAFE_RELEASE(NewBuffer.m_pBuffer);
            return static_cast<UINT32>(-1);
        }

        FoundIndex = static_cast<UINT32>(m_RenameBuffers.size());
        m_RenameBuffers.push_back(NewBuffer);
    }

    return FoundIndex;
}

//--------------------------------------------------------------------------------------
// Get a constant buffer descriptor
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void D3D12DynamicBuffer::GetCBDesc(D3D12_CONSTANT_BUFFER_VIEW_DESC* pCBDesc) const
{
    pCBDesc->BufferLocation = GetBuffer()->GetGPUVirtualAddress();
    pCBDesc->SizeInBytes = m_ByteWidth;
}

//--------------------------------------------------------------------------------------
// Get a vertex buffer descriptor
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void D3D12DynamicBuffer::GetVBDesc(D3D12_VERTEX_BUFFER_VIEW* pVBDesc) const
{
    pVBDesc->BufferLocation = GetBuffer()->GetGPUVirtualAddress();
    pVBDesc->StrideInBytes = m_VertexStrideBytes;
    pVBDesc->SizeInBytes = m_ByteWidth;
}

//--------------------------------------------------------------------------------------
// Get an index buffer descriptor
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void D3D12DynamicBuffer::GetIBDesc(D3D12_INDEX_BUFFER_VIEW* pIBDesc, DXGI_FORMAT IndexFormat) const
{
    XSF_ASSERT(IndexFormat == DXGI_FORMAT_R16_UINT || IndexFormat == DXGI_FORMAT_R32_UINT);

    pIBDesc->BufferLocation = GetBuffer()->GetGPUVirtualAddress();
    pIBDesc->SizeInBytes = m_ByteWidth;
    pIBDesc->Format = IndexFormat;
}

//--------------------------------------------------------------------------------------
// Create a constant buffer view
//--------------------------------------------------------------------------------------
void D3D12DynamicBuffer::CreateCBView(D3D12_CPU_DESCRIPTOR_HANDLE DestHandle)
{
    D3D12_CONSTANT_BUFFER_VIEW_DESC CBDesc = {};
    GetCBDesc(&CBDesc);
    m_pDevice->CreateConstantBufferView(&CBDesc, DestHandle);
}


//--------------------------------------------------------------------------------------
// Initialize the heap set manager with empty entries
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT DescriptorHeapSetManager::Initialize(
    D3DDevice* const pDevice, 
    ID3D12Fence* const pFence, 
    ID3D12DescriptorHeap* const pHeap, 
    UINT32 HeapOffset, 
    UINT32 HeapStride, 
    UINT32 MaxHandleCount)
{
    const UINT32 SetCount = MaxHandleCount / HeapStride;
    if (SetCount == 0)
    {
        return E_INVALIDARG;
    }

    m_pDevice = pDevice;
    m_pDevice->AddRef();

    m_pFence = pFence;
    m_pFence->AddRef();

    m_pHeap = pHeap;
    m_pHeap->AddRef();
    m_HeapDesc = m_pHeap->GetDesc();

    m_HandleSize = m_pDevice->GetDescriptorHandleIncrementSize(m_HeapDesc.Type);

    m_BaseHeapOffset = HeapOffset;
    m_HeapStride = HeapStride;

    for (UINT32 i = 0; i < SetCount; ++i)
    {
        HandleSet HS = {};
        HS.Fence = 0;
        HS.HeapOffset = HeapOffset + HeapStride * i;
        m_Sets.push_back(HS);
    }
    m_LastSetIndex = SetCount - 1;

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Terminate the heap set manager and free us its memory
//--------------------------------------------------------------------------------------
void DescriptorHeapSetManager::Terminate()
{
    XSF_SAFE_RELEASE(m_pHeap);
    XSF_SAFE_RELEASE(m_pFence);
    XSF_SAFE_RELEASE(m_pDevice);
}

//--------------------------------------------------------------------------------------
// Add a buffer to the heap
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
DescriptorHeapSetManager::ManagedBuffer* DescriptorHeapSetManager::AddBuffer()
{
    const UINT32 Size = static_cast<UINT32>(m_Buffers.size());
    if (Size >= m_HeapStride)
    {
        return nullptr;
    }

    ManagedBuffer MB = {};
    m_Buffers.push_back(MB);

    return &m_Buffers[Size];
}

//--------------------------------------------------------------------------------------
// Add a dynamic constant buffer to the heap
//-------------------------------------------------------------------------------------
_Use_decl_annotations_
bool DescriptorHeapSetManager::AddDynamicCB(D3D12DynamicBuffer* const pBuffer)
{
    if (m_HeapDesc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        return false;
    }
    ManagedBuffer* pMB = AddBuffer();
    if (pMB == nullptr)
    {
        return false;
    }
    pMB->m_usage = ManagedBuffer::Constant;
    pMB->m_dynamicBuffer = true;
    pMB->m_pResource = pBuffer;

    return true;
}

//--------------------------------------------------------------------------------------
// Add a dynamic index buffer to the heap
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
bool DescriptorHeapSetManager::AddDynamicIB(D3D12DynamicBuffer* const pBuffer, DXGI_FORMAT IndexFormat)
{
    ManagedBuffer* pMB = AddBuffer();
    if (pMB == nullptr)
    {
        return false;
    }
    pMB->m_usage = ManagedBuffer::Index;
    pMB->m_dynamicBuffer = true;
    pMB->m_pResource = pBuffer;
    pMB->IndexFormat = IndexFormat;

    return true;
}

//--------------------------------------------------------------------------------------
// Add a dynamic vertex buffer to the heap
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
bool DescriptorHeapSetManager::AddDynamicVB(D3D12DynamicBuffer* const pBuffer)
{
    ManagedBuffer* pMB = AddBuffer();
    if (pMB == nullptr)
    {
        return false;
    }
    pMB->m_usage = ManagedBuffer::Vertex;
    pMB->m_dynamicBuffer = true;
    pMB->m_pResource = pBuffer;

    return true;
}

//--------------------------------------------------------------------------------------
// Add an empty slot to the heap
//--------------------------------------------------------------------------------------
bool DescriptorHeapSetManager::AddEmptySlot()
{
    ManagedBuffer* pMB = AddBuffer();
    if (pMB == nullptr)
    {
        return false;
    }
    pMB->m_usage = ManagedBuffer::Undefined;
    pMB->m_dynamicBuffer = false;
    pMB->m_pResource = nullptr;

    return true;
}

//--------------------------------------------------------------------------------------
// Add a static descriptor to the heap
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
bool DescriptorHeapSetManager::AddStaticDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* pHandle)
{
    ManagedBuffer* pMB = AddBuffer();
    if (pMB == nullptr)
    {
        return false;
    }
    pMB->m_usage = ManagedBuffer::Undefined;
    pMB->m_dynamicBuffer = false;

    const UINT32 IndexWithinSet = static_cast<UINT32>(m_Buffers.size()) - 1;
    const UINT32 HeapIndex = m_BaseHeapOffset + IndexWithinSet;
    pMB->OriginalHeapIndex = HeapIndex;

    CD3DX12_CPU_DESCRIPTOR_HANDLE Handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pHeap->GetCPUDescriptorHandleForHeapStart());
    Handle.Offset(HeapIndex, m_HandleSize);
    *pHandle = Handle;

    return true;
}

//--------------------------------------------------------------------------------------
// Add a shader resource view to the heap
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
bool DescriptorHeapSetManager::AddStaticSRV(ID3D12Resource* const pResource, D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc)
{
    if (m_HeapDesc.Type != D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        return false;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE Handle;
    bool Result = AddStaticDescriptor(&Handle);
    if (!Result)
    {
        return false;
    }
    m_pDevice->CreateShaderResourceView(pResource, &SRVDesc, Handle);

    return true;
}

//--------------------------------------------------------------------------------------
// Finalize descriptor table
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void DescriptorHeapSetManager::FinalizeDescriptorTable(D3D12_GPU_DESCRIPTOR_HANDLE* pGpuHandle, D3D12_CPU_DESCRIPTOR_HANDLE* pCpuHandle)
{
    const UINT64 CurrentFence = m_pFence->GetCompletedValue() + 1;

    HandleSet* pSet = FindUnusedSet();
    XSF_ASSERT(pSet != nullptr);
    pSet->Fence = CurrentFence;

    const UINT32 BufferCount = static_cast<UINT32>(m_Buffers.size());
    for (UINT32 i = 0; i < BufferCount; ++i)
    {
        UINT32 HeapIndex = pSet->HeapOffset + i;
        PrepareDescriptor(m_Buffers[i], HeapIndex);
    }

    if (pGpuHandle != nullptr)
    {
        CD3DX12_GPU_DESCRIPTOR_HANDLE GpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pHeap->GetGPUDescriptorHandleForHeapStart());
        GpuHandle.Offset(pSet->HeapOffset, m_HandleSize);
        *pGpuHandle = GpuHandle;
    }

    if (pCpuHandle != nullptr)
    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE CpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pHeap->GetCPUDescriptorHandleForHeapStart());
        CpuHandle.Offset(pSet->HeapOffset, m_HandleSize);
        *pCpuHandle = CpuHandle;
    }
}


//--------------------------------------------------------------------------------------
// Finalize index buffer view
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void DescriptorHeapSetManager::FinalizeIndexBuffer(D3D12_INDEX_BUFFER_VIEW* pIBView)
{
    const UINT32 BufferCount = static_cast<UINT32>(m_Buffers.size());
    for (UINT32 i = 0; i < BufferCount; ++i)
    {
        if (m_Buffers[i].m_usage == ManagedBuffer::Index)
        {
            XSF_ASSERT(m_Buffers[i].m_dynamicBuffer);
            D3D12DynamicBuffer* pDynamic = reinterpret_cast<D3D12DynamicBuffer*>(m_Buffers[i].m_pResource);
            pDynamic->GetIBDesc(pIBView, m_Buffers[i].IndexFormat);

            break;
        }
    }
}


//--------------------------------------------------------------------------------------
// Finalize vertex buffer views
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void DescriptorHeapSetManager::FinalizeVertexBuffers(UINT* pNumViews, D3D12_VERTEX_BUFFER_VIEW* pVBView, UINT maxNumViews)
{
    const UINT32 BufferCount = static_cast<UINT32>(m_Buffers.size());
    *pNumViews = 0;
    for (UINT32 i = 0; i < BufferCount; ++i)
    {
        if (m_Buffers[i].m_usage == ManagedBuffer::Vertex)
        {
            XSF_ASSERT(m_Buffers[i].m_dynamicBuffer);
            D3D12DynamicBuffer* pDynamic = reinterpret_cast<D3D12DynamicBuffer*>(m_Buffers[i].m_pResource);
            pDynamic->GetVBDesc(&pVBView[*pNumViews]);

            if ((++(*pNumViews)) == maxNumViews)
            {
                break;
            }
        }
    }
}


//--------------------------------------------------------------------------------------
// Prepare the descriptor for the given buffer
//--------------------------------------------------------------------------------------
void DescriptorHeapSetManager::PrepareDescriptor(const ManagedBuffer& MB, UINT32 HeapIndex) const
{
    if (MB.m_pResource == nullptr)
    {
        return;
    }

    CD3DX12_CPU_DESCRIPTOR_HANDLE Handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pHeap->GetCPUDescriptorHandleForHeapStart());
    Handle = Handle.Offset(HeapIndex, m_HandleSize);

    if (MB.m_dynamicBuffer)
    {
        D3D12DynamicBuffer* pDynamic = reinterpret_cast<D3D12DynamicBuffer*>(MB.m_pResource);
        switch (MB.m_usage)
        {
        case ManagedBuffer::Constant:
            pDynamic->CreateCBView(Handle);
            break;
        }
    }
    else
    {
        if (MB.OriginalHeapIndex != HeapIndex)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE SrcHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pHeap->GetCPUDescriptorHandleForHeapStart());
            SrcHandle.Offset(MB.OriginalHeapIndex, m_HandleSize);
            m_pDevice->CopyDescriptorsSimple(1, Handle, SrcHandle, m_HeapDesc.Type);
        }
    }
}

//--------------------------------------------------------------------------------------
// Find an unused set in the heap. Loop until the fence advances.
//--------------------------------------------------------------------------------------
DescriptorHeapSetManager::HandleSet* DescriptorHeapSetManager::FindUnusedSet()
{
    const UINT32 SetCount = static_cast<UINT32>(m_Sets.size());
    UINT64 LowestFence = static_cast<UINT64>(-1);
#pragma warning(disable:4127)
    while (true)
    {
        UINT64 currentFenceValue = m_pFence->GetCompletedValue();
        for (UINT32 i = 0; i < SetCount; ++i)
        {
            UINT32 SetIndex = (i + m_LastSetIndex) % SetCount;
            HandleSet& HS = m_Sets[SetIndex];
            if (HS.Fence <= currentFenceValue)
            {
                m_LastSetIndex = SetIndex;
                return &HS;
            }
            else
            {
                LowestFence = std::min(LowestFence, HS.Fence);
            }
        }
        m_pFence->Signal(currentFenceValue+1);
        while (m_pFence->GetCompletedValue() <= LowestFence)
        {
            Sleep(1);
        }
    }
}


//--------------------------------------------------------------------------------------
// Initial data barrier
//--------------------------------------------------------------------------------------
void InitialDataBarrier(_In_ D3DCommandList* const pCmdList, _In_ ID3D12Resource* const pResource)
{
    D3D12_RESOURCE_BARRIER barrierDesc = {};
    barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Transition.pResource = pResource;
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrierDesc.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    pCmdList->ResourceBarrier(1, &barrierDesc);
}

//--------------------------------------------------------------------------------------
// Initial data barrier
//--------------------------------------------------------------------------------------
void InitialDataBarrier(_In_ D3DDevice* const pDevice, _In_ ID3D12CommandQueue* const pCmdQueue, _In_ ID3D12CommandAllocator* const pCmdAllocator, _In_ ID3D12Resource* const pResource)
{
    D3DCommandList* pCmdList = nullptr;
    HRESULT hr = pDevice->CreateCommandList(c_nodeMask, D3D12_COMMAND_LIST_TYPE_DIRECT, pCmdAllocator, nullptr, IID_GRAPHICS_PPV_ARGS(&pCmdList));
    if (SUCCEEDED(hr))
    {
        InitialDataBarrier(pCmdList, pResource);
        pCmdList->Close();
        pCmdQueue->ExecuteCommandLists(1, (ID3D12CommandList**)(&pCmdList));
        XSF_SAFE_RELEASE(pCmdList);
    }
}

//--------------------------------------------------------------------------------------
// Initialized the CpuGpu heap
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CpuGpuHeap::Initialize(D3DDevice* pDevice, ID3D12Fence* const pFence, SIZE_T HeapSizeBytesPerSlab, bool Readback, UINT32 MaxSlabCount, wchar_t* heapName)
{
    if (Readback)
    {
        return E_NOTIMPL;
    }

    Terminate();

    m_pDevice = pDevice;
    m_pDevice->AddRef();

    m_pFence = pFence;
    m_pFence->AddRef();

    m_MaxSlabCount = std::max(1U, MaxSlabCount);
    m_Readback = Readback;
    m_SlabSizeBytes = HeapSizeBytesPerSlab;
    m_CurrentSlabIndex = 0;

    if (heapName != nullptr)
    {
        wcscpy_s(m_heapName, heapName);
    }
    else
    {
        m_heapName[0] = L'\0';
    }

    for (UINT32 i = 0; i < m_MaxSlabCount; ++i)
    {
        HRESULT hr = CreateNewSlab(nullptr);
        if (FAILED(hr))
        {
            Terminate();
            return hr;
        }
    }

    m_hBlockEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Terminate the heap and free up the memory
//--------------------------------------------------------------------------------------
void CpuGpuHeap::Terminate()
{
    UINT32 SlabCount = static_cast<UINT32>(m_Slabs.size());
    for (UINT32 i = 0; i < SlabCount; ++i)
    {
        HeapSlab& HS = m_Slabs[i];
        HS.m_pHeap->Unmap(0, nullptr);
        XSF_SAFE_RELEASE(HS.m_pHeap);
    }
    m_Slabs.clear();

    CloseHandle(m_hBlockEvent);
    m_hBlockEvent = 0;
    XSF_SAFE_RELEASE(m_pFence);
    XSF_SAFE_RELEASE(m_pDevice);
}

//--------------------------------------------------------------------------------------
// Create a new slab
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CpuGpuHeap::CreateNewSlab(HeapSlab** ppSlab)
{
    HeapSlab NewSlab;

    const D3D12_HEAP_PROPERTIES uploadOrReadBackHeapProperties = 
        m_Readback? CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK) : 
                    CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(m_SlabSizeBytes);
    XSF_ERROR_IF_FAILED(m_pDevice->CreateCommittedResource(
        &uploadOrReadBackHeapProperties, 
        D3D12_HEAP_FLAG_NONE, 
        &bufferDesc, 
        D3D12_RESOURCE_STATE_GENERIC_READ, 
        nullptr, 
        IID_GRAPHICS_PPV_ARGS(&NewSlab.m_pHeap)));
    if (m_heapName[0] != L'\0')
    {
        XSF_ERROR_IF_FAILED(NewSlab.m_pHeap->SetName(m_heapName));
    }

    XSF_ERROR_IF_FAILED(NewSlab.m_pHeap->Map(0, nullptr, reinterpret_cast<void**>(&NewSlab.m_pBase)));

    NewSlab.m_currentOffset = 0;
    NewSlab.m_fence = 0;

    const UINT32 SlabIndex = static_cast<UINT32>(m_Slabs.size());
    m_Slabs.push_back(NewSlab);

    if (ppSlab != nullptr)
    {
        *ppSlab = &m_Slabs[SlabIndex];
    }

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Find an available slab in the heap
//--------------------------------------------------------------------------------------
HRESULT CpuGpuHeap::FindAvailableSlab(HeapSlab** ppSlab, SIZE_T SizeBytes, SIZE_T AlignmentBytes)
{
    const UINT64 LastCompletedFence = m_pFence->GetCompletedValue();
    const UINT64 CurrentFence = m_currentFence != static_cast<UINT64>(-1) ? m_currentFence : LastCompletedFence + 1;
    const UINT32 SlabCount = static_cast<UINT32>(m_Slabs.size());

    for (UINT32 i = 0; i < SlabCount; ++i)
    {
        UINT32 Index = (m_CurrentSlabIndex + i) % SlabCount;
        HeapSlab& S = m_Slabs[Index];

        if (S.m_fence == CurrentFence || S.m_fence == 0)
        {
            // Slab is available; try to fit allocation:
            const SIZE_T EndPos = static_cast<SIZE_T>(NextMultiple(S.m_currentOffset, AlignmentBytes)) + SizeBytes;
            if (EndPos <= m_SlabSizeBytes)
            {
                m_CurrentSlabIndex = Index;
                *ppSlab = &S;

                return S_OK;
            }
        }

        if (S.m_fence <= LastCompletedFence)
        {
            // Recycle this slab:
            S.m_fence = 0;
            S.m_currentOffset = 0;
        }    
    }

    return E_OUTOFMEMORY;
}

//--------------------------------------------------------------------------------------
// Allocate a new slab in the heap
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CpuGpuHeap::Allocate(SIZE_T SizeBytes, SIZE_T AlignmentBytes, HeapSlab** ppSlab, SIZE_T* pOffsetWithinSlab)
{
    if (m_SlabSizeBytes < SizeBytes)
        return E_OUTOFMEMORY;

    HeapSlab* pSlab = nullptr;
    HRESULT hr = FindAvailableSlab(&pSlab, SizeBytes, AlignmentBytes);

    // If we couldn't find an available slab, try to create a new slab if we haven't hit the slab limit:
    if (FAILED(hr) && (UINT32)m_Slabs.size() < m_MaxSlabCount)
    {
        hr = CreateNewSlab(&pSlab);
        if (SUCCEEDED(hr))
        {
            m_CurrentSlabIndex = static_cast<UINT32>(m_Slabs.size()) - 1;
        }
    }

    // If we couldn't create a new slab, then wait for a slab to be available:
    if (FAILED(hr))
    {
        UINT64 LowestFence = static_cast<UINT64>(-1);
        UINT32 SlabIndex = 0;
        const UINT32 SlabCount = static_cast<UINT32>(m_Slabs.size());
        for (UINT32 i = 0; i < SlabCount; ++i)
        {
            const HeapSlab& S = m_Slabs[i];
            if (S.m_fence < LowestFence)
            {
                LowestFence = S.m_fence;
                SlabIndex = i;
            }
        }
        if (LowestFence != 0)
        {
            m_pFence->SetEventOnCompletion(LowestFence, m_hBlockEvent);
            WaitForSingleObject(m_hBlockEvent, INFINITE);
        }
        m_CurrentSlabIndex = SlabIndex;
        hr = FindAvailableSlab(&pSlab, SizeBytes, AlignmentBytes);
    }

    if (SUCCEEDED(hr))
    {
        XSF_ASSERT(pSlab != nullptr);
        pSlab->m_fence = m_currentFence != static_cast<UINT64>(-1) ? m_currentFence : m_pFence->GetCompletedValue() + 1;
        SIZE_T ReturnOffset = static_cast<SIZE_T>(NextMultiple(pSlab->m_currentOffset, AlignmentBytes));
        SIZE_T NewOffset = ReturnOffset + SizeBytes;
        XSF_ASSERT(NewOffset <= m_SlabSizeBytes);
        pSlab->m_currentOffset = NewOffset;

        *ppSlab = pSlab;
        *pOffsetWithinSlab = ReturnOffset;

        return S_OK;
    }
    return E_OUTOFMEMORY;
}

//--------------------------------------------------------------------------------------
// Get a heap pointer for the required size
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CpuGpuHeap::GetHeapPointer(SIZE_T SizeBytes, ID3D12Resource** ppHeap, SIZE_T& DestOffsetWithinHeap, BYTE **ppData, SIZE_T AlignmentBytes)
{
    if (m_threadSafe)
    {
        while (TRUE == InterlockedCompareExchange((volatile UINT*)(&m_InUse), TRUE, FALSE))
        {
            SwitchToThread();
        }
    }

    HeapSlab* pSlab = nullptr;
    XSF_RETURN_IF_FAILED(Allocate(SizeBytes, AlignmentBytes, &pSlab, &DestOffsetWithinHeap));
    
    XSF_ASSERT(pSlab != nullptr && pSlab->m_pBase != nullptr);
    *ppHeap = pSlab->m_pHeap;
    *ppData = pSlab->m_pBase + DestOffsetWithinHeap;

    if (m_threadSafe)
    {
        InterlockedExchange((volatile UINT*)(&m_InUse), FALSE);
    }

    return S_OK;
}

_Use_decl_annotations_
HRESULT CpuGpuHeap::GetHeapPointer(SIZE_T SizeBytes, D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress, BYTE **ppData, SIZE_T AlignmentBytes)
{
    ID3D12Resource* pHeap;
    SIZE_T DestOffsetWithinHeap;

    gpuAddress = 0;
    XSF_RETURN_IF_FAILED(GetHeapPointer(SizeBytes, &pHeap, DestOffsetWithinHeap, ppData, AlignmentBytes));

    gpuAddress = pHeap->GetGPUVirtualAddress() + DestOffsetWithinHeap;

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Copy the buffer data to the heap
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CpuGpuHeap::CopyBufferDataToHeap(const BYTE* pData, SIZE_T SizeBytes, ID3D12Resource** ppHeap, SIZE_T& DestOffsetWithinHeap)
{
    BYTE* pDstRow = nullptr;
    XSF_RETURN_IF_FAILED(GetHeapPointer(SizeBytes, ppHeap, DestOffsetWithinHeap, &pDstRow));

    const BYTE* pSrcRow = pData;
    memcpy(pDstRow, pSrcRow, SizeBytes);

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Copy the buffer data to the default resource
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CpuGpuHeap::CopyBufferDataToDefaultBuffer(const BYTE* pData, SIZE_T SizeBytes, D3DCommandList* pCommandList, ID3D12Resource* pDefaultResource)
{
    SIZE_T DestOffsetWithinHeap = 0;
    ID3D12Resource* pHeapResource = nullptr;
    XSF_ERROR_IF_FAILED(CopyBufferDataToHeap(pData, SizeBytes, &pHeapResource, DestOffsetWithinHeap));
    
    XSF_ASSERT(pHeapResource != nullptr);

    pCommandList->CopyBufferRegion(
        pDefaultResource,
        0,
        pHeapResource,
        DestOffsetWithinHeap,
        SizeBytes);

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Copy the texture subresource to heap
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CpuGpuHeap::CopyTextureSubresourceToHeap(
    const BYTE* pData, 
    const D3D12_SUBRESOURCE_FOOTPRINT& srcSubresourceDesc, 
    const D3D12_SUBRESOURCE_FOOTPRINT& destSubresourceDesc, 
    ID3D12Resource** ppHeap, 
    SIZE_T& DestOffsetWithinHeap)
{
    XSF_ASSERT(destSubresourceDesc.RowPitch % XSF_TEXTURE_DATA_PITCH_ALIGNMENT == 0);
    // Initialize
    const UINT32 srcRowPitchBytes = srcSubresourceDesc.RowPitch;
    const UINT32 destRowPitchBytes = destSubresourceDesc.RowPitch;
    UINT32 srcHeightRows = srcSubresourceDesc.Height;
    UINT32 destHeightRows = destSubresourceDesc.Height;

    if (IsBlockCompressedFormat(srcSubresourceDesc.Format))
    {
        srcHeightRows = static_cast< UINT32 >(NextMultiple(srcHeightRows, 4U) >> 2);
    }
    if (IsBlockCompressedFormat(destSubresourceDesc.Format))
    {
        destHeightRows = static_cast< UINT32 >(NextMultiple(destHeightRows, 4U) >> 2);
    }
    XSF_ASSERT(srcRowPitchBytes <= destRowPitchBytes && srcHeightRows <= destHeightRows);
    
    // Allocate on heap
    const SIZE_T LinearSizeBytesToCopy = srcSubresourceDesc.Depth * srcRowPitchBytes * srcHeightRows;
    const SIZE_T LinearSizeBytesToAllocate = destSubresourceDesc.Depth * destRowPitchBytes * destHeightRows;
    HeapSlab* pSlab = nullptr;
    XSF_ERROR_IF_FAILED(Allocate(LinearSizeBytesToAllocate, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, &pSlab, &DestOffsetWithinHeap));
    
    // Copy to heap
    XSF_ASSERT(pSlab != nullptr && pSlab->m_pBase != nullptr);
    *ppHeap = pSlab->m_pHeap;
    const BYTE* pSrcBase = pData;
    BYTE* pDestBase = pSlab->m_pBase + DestOffsetWithinHeap;

    if (srcRowPitchBytes == destRowPitchBytes)
    {
        memcpy(pDestBase, pSrcBase, LinearSizeBytesToCopy);
    }
    else
    {
        size_t srcNumBytes;
        size_t srcRowBytes;
        GetSurfaceInfo(srcSubresourceDesc.Width, srcSubresourceDesc.Height, srcSubresourceDesc.Format, &srcNumBytes, &srcRowBytes, nullptr);

        size_t destNumBytes;
        size_t destRowBytes;
        GetSurfaceInfo(destSubresourceDesc.Width, destSubresourceDesc.Height, destSubresourceDesc.Format, &destNumBytes, &destRowBytes, nullptr);
        if (destRowPitchBytes > destRowBytes)
        {
            destNumBytes *= destRowPitchBytes / destRowBytes;
        }

        // Row-by-row memcpy
        for (UINT d = 0; d < srcSubresourceDesc.Depth; ++d)
        {
            const BYTE* pSrcDepthBase = pSrcBase + d * srcNumBytes;
            BYTE* pDestDepthBase = pDestBase + d * destNumBytes;
            for (UINT i = 0; i < srcHeightRows; i++)
            {
                const BYTE* pSrcRow = pSrcDepthBase + i * srcRowPitchBytes;
                BYTE* pDestRow = pDestDepthBase + i * destRowPitchBytes;
                memcpy(pDestRow, pSrcRow, srcRowPitchBytes);
            }
        }
    }

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Copy the texture subresource into the default texture
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CpuGpuHeap::CopyTextureSubresourceToDefaultTexture(
    const BYTE* pData, 
    const D3D12_SUBRESOURCE_FOOTPRINT& srcSubresourceDesc, 
    D3DCommandList* pCommandList, 
    ID3D12Resource* const pDefaultResource, 
    UINT32 DestSubresource,
    const D3D12_SUBRESOURCE_FOOTPRINT* pDestSubresourceDesc)
{
    SIZE_T DestOffsetWithinHeap = 0;
    ID3D12Resource* pHeapResource = nullptr;
    
    // First, copy to a staging place matching dest alignment on the heap
    // if dest desc is null, default dest to match src desc
    const D3D12_SUBRESOURCE_FOOTPRINT& destSubresourceDesc = pDestSubresourceDesc ? *pDestSubresourceDesc : srcSubresourceDesc;
    XSF_ERROR_IF_FAILED(CopyTextureSubresourceToHeap(pData, srcSubresourceDesc, destSubresourceDesc, &pHeapResource, DestOffsetWithinHeap));
    
    // Second, have D3D copy it from the heap to the dest resource
    XSF_ASSERT(pHeapResource != nullptr);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT PlacedDesc = {};
    PlacedDesc.Offset = DestOffsetWithinHeap;
    PlacedDesc.Footprint = destSubresourceDesc;

    CD3DX12_TEXTURE_COPY_LOCATION Dest(pDefaultResource, DestSubresource);
    CD3DX12_TEXTURE_COPY_LOCATION Source(pHeapResource, PlacedDesc);

    pCommandList->CopyTextureRegion(
        &Dest,
        0, 0, 0,
        &Source,
        nullptr);

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Initialize the mip generator
//--------------------------------------------------------------------------------------
/*_Use_decl_annotations_
HRESULT MipsGenerator::Initialize(D3DDevice* const pDevice, DXGI_FORMAT Format, D3D12_RESOURCE_DIMENSION Dimension)
{
    Terminate();

    m_format = Format;
    m_pDevice = pDevice;
    m_pDevice->AddRef();

    // GenMips uses a custom RootSig to allow binding constants without needing to burn heap space for constant buffers
    // Total bindings: One SRV (the GenMips SRV), and two root constants.
    CD3DX12_DESCRIPTOR_RANGE SRVSlot;
    SRVSlot.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER RootParams[2];
    RootParams[0].InitAsDescriptorTable(1, &SRVSlot, D3D12_SHADER_VISIBILITY_PIXEL);
    RootParams[1].InitAsConstants(2, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

    CD3DX12_ROOT_SIGNATURE_DESC RootSigDesc(ARRAYSIZE(RootParams), RootParams, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    D3DBlobPtr spRootSigBlob;
    XSF_RETURN_IF_FAILED(D3D12SerializeRootSignature(&RootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, spRootSigBlob.GetAddressOf(), nullptr));
    XSF_RETURN_IF_FAILED(m_pDevice->CreateRootSignature(c_nodeMask, spRootSigBlob->GetBufferPointer(), spRootSigBlob->GetBufferSize(), IID_GRAPHICS_PPV_ARGS(m_spRootSignature.ReleaseAndGetAddressOf())));

    D3D12_DEPTH_STENCIL_DESC descDepth = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    descDepth.DepthEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPSO = {};
    descPSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    descPSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    descPSO.DepthStencilState = descDepth;
    descPSO.pRootSignature = m_spRootSignature;
    descPSO.VS.pShaderBytecode = m_spVS->GetBufferPointer();
    descPSO.VS.BytecodeLength = m_spVS->GetBufferSize();
    switch(Dimension)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            descPSO.PS.pShaderBytecode = m_spPS1D->GetBufferPointer();
            descPSO.PS.BytecodeLength = m_spPS1D->GetBufferSize();
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            descPSO.PS.pShaderBytecode = m_spPS2D->GetBufferPointer();
            descPSO.PS.BytecodeLength = m_spPS2D->GetBufferSize();
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            descPSO.PS.pShaderBytecode = m_spPS3D->GetBufferPointer();
            descPSO.PS.BytecodeLength = m_spPS3D->GetBufferSize();
            break;

        default:
            XSF_ASSERT(false);
    }
    descPSO.DSVFormat = DXGI_FORMAT_UNKNOWN;
    descPSO.RTVFormats[0] = Format;
    descPSO.NumRenderTargets = 1;
    descPSO.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_0xFFFFFFFF;
    descPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    descPSO.SampleDesc.Count = 1;
    descPSO.SampleMask = UINT_MAX;
    XSF_RETURN_IF_FAILED(m_pDevice->CreateGraphicsPipelineState(&descPSO, IID_GRAPHICS_PPV_ARGS(m_spPSO.ReleaseAndGetAddressOf())));

    XSF_RETURN_IF_FAILED(m_SRVHeap.Initialize(m_pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true));
    XSF_RETURN_IF_FAILED(m_RTVHeap.Initialize(m_pDevice, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, c_maxRTV));
    m_iRTV = 0;

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Terminate the mip generator, freeing its resources
//--------------------------------------------------------------------------------------
void MipsGenerator::Terminate()
{
    XSF_SAFE_RELEASE(m_pDevice);
    m_spPSO.Reset();
    m_spRootSignature.Reset();
    m_SRVHeap.Terminate();
    m_RTVHeap.Terminate();
}

//--------------------------------------------------------------------------------------
// Generate the mip levels
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT MipsGenerator::GenerateMips(D3DCommandList* const pCmdList, _In_ ID3D12Resource* const pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDescSRV, 
                                    D3D12_CPU_DESCRIPTOR_HANDLE* phSRV, D3D12_RESOURCE_STATES resourceState)
{
    XSFScopedNamedEvent(pCmdList, XTF_COLOR_DRAW_TEXT, L"GenerateMips");

    D3D12_RESOURCE_DESC descResource = pResource->GetDesc();

    // the format of the resource must match the format given in Initialize
    // else, it's incompatible with the PSO
    if (descResource.Format != m_format)
    {
        return E_INVALIDARG;
    }

    D3D12_RENDER_TARGET_VIEW_DESC descRTV = {};
    descRTV.Format = m_format;

    D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = *pDescSRV;

    // Prep RTV desc with properties that are common across all mips
    // Not using arrayed RTVs because that would require a geometry shader
    switch(descSRV.ViewDimension)
    {
    case D3D12_SRV_DIMENSION_TEXTURE1D:
        descRTV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
        descRTV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
        descRTV.Texture1DArray.ArraySize = 1;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2D:
        descRTV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
    case D3D12_SRV_DIMENSION_TEXTURECUBE:
    case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
        descRTV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        descRTV.Texture2DArray.ArraySize = 1;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE3D:
        descRTV.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        descRTV.Texture3D.WSize = 1;
        break;
    default:
        XSF_ASSERT(false);
    }

    // All state that is applied for this operation must be overridden by the app's state on the next draw
    ID3D12DescriptorHeap* pHeaps[] = { m_SRVHeap };
    pCmdList->SetDescriptorHeaps(ARRAYSIZE(pHeaps), pHeaps);
    pCmdList->SetPipelineState(m_spPSO);
    pCmdList->SetGraphicsRootSignature(m_spRootSignature);
    pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    {
        // Unbind all VBs
        D3D12_VERTEX_BUFFER_VIEW VBVArray[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        memset(VBVArray, 0, sizeof(VBVArray));
        pCmdList->IASetVertexBuffers(0, ARRAYSIZE(VBVArray), VBVArray);
    }

    // Bind SRV
    if (descSRV.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE || descSRV.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBEARRAY)
    {
        UINT NumCubes = 1;
        UINT MipLevels = 0;
        UINT FirstSlice = 0;
        UINT FirstMip = 0;
        if (descSRV.ViewDimension == D3D12_SRV_DIMENSION_TEXTURECUBE)
        {
            MipLevels = descSRV.TextureCube.MipLevels;
            FirstMip = descSRV.TextureCube.MostDetailedMip;
        }
        else
        {
            MipLevels = descSRV.TextureCubeArray.MipLevels;
            FirstMip = descSRV.TextureCubeArray.MostDetailedMip;
            FirstSlice = descSRV.TextureCubeArray.First2DArrayFace;
            NumCubes = descSRV.TextureCubeArray.NumCubes;
        }
        descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        descSRV.Texture2DArray.MostDetailedMip = FirstMip;
        descSRV.Texture2DArray.MipLevels = MipLevels;
        descSRV.Texture2DArray.FirstArraySlice = FirstSlice;
        descSRV.Texture2DArray.ArraySize = NumCubes * 6;
        descSRV.Texture2DArray.ResourceMinLODClamp = 0.0f;
        m_pDevice->CreateShaderResourceView(pResource, &descSRV, m_SRVHeap.hCPU(0));
    }
    else
    {
        if (phSRV != nullptr)
        {
            m_pDevice->CopyDescriptorsSimple(1, m_SRVHeap.hCPU(0), *phSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        else
        {
            m_pDevice->CreateShaderResourceView(pResource, &descSRV, m_SRVHeap.hCPU(0));
        }
        
    }
    pCmdList->SetGraphicsRootDescriptorTable(0, m_SRVHeap.hGPU(0));

    UINT iMinArrayOrDepth = 0;
    UINT iMinMipLevel = 1;
    switch(descSRV.ViewDimension)
    {
    case D3D12_SRV_DIMENSION_TEXTURE1D:
        iMinMipLevel = descSRV.Texture1D.MostDetailedMip + 1;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
        iMinMipLevel = descSRV.Texture1DArray.MostDetailedMip;
        iMinArrayOrDepth = descSRV.Texture1DArray.FirstArraySlice;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE2D:
        iMinMipLevel = descSRV.Texture2D.MostDetailedMip + 1;
        break;
    case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
        iMinMipLevel = descSRV.Texture2DArray.MostDetailedMip + 1;
        iMinArrayOrDepth = descSRV.Texture2DArray.FirstArraySlice;
        break;
    case D3D12_SRV_DIMENSION_TEXTURE3D:
        iMinMipLevel = descSRV.Texture3D.MostDetailedMip + 1;
        break;
    default:
        XSF_ASSERT(false);
    }

    if (resourceState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    {
        ResourceBarrier(pCmdList, pResource, resourceState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    for (UINT iArrayOrDepth = iMinArrayOrDepth; iArrayOrDepth < descResource.DepthOrArraySize; ++iArrayOrDepth)
    {
        D3D12_RESOURCE_BARRIER descBarrier = {};

        descBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        descBarrier.Transition.pResource = pResource;

        for (UINT iMipLevel = iMinMipLevel; iMipLevel < descResource.MipLevels; ++iMipLevel)
        {
            XSFScopedNamedEvent(pCmdList, XTF_COLOR_DRAW_TEXT, L"GenerateMips (Array: %d, Mip: %d)", iArrayOrDepth, iMipLevel);

            // transition the lower mip level to RT
            descBarrier.Transition.Subresource = iMipLevel + iArrayOrDepth * descResource.MipLevels;
            descBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            descBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            pCmdList->ResourceBarrier(1, &descBarrier);
            
            UINT width = static_cast<UINT>(descResource.Width) >> iMipLevel;
            UINT height = descResource.Height >> iMipLevel;

            D3D12_VIEWPORT viewport =  {0, 0, static_cast<FLOAT>(width), static_cast<FLOAT>(height), 0, 1.0f };
            D3D12_RECT scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
            pCmdList->RSSetViewports(1, &viewport);
            pCmdList->RSSetScissorRects(1, &scissor);

            switch(descRTV.ViewDimension)
            {
            case D3D12_RTV_DIMENSION_TEXTURE1D:
                descRTV.Texture1D.MipSlice = iMipLevel;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
                descRTV.Texture1DArray.MipSlice = iMipLevel;
                descRTV.Texture1DArray.FirstArraySlice = iArrayOrDepth;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2D:
                descRTV.Texture2D.MipSlice = iMipLevel;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
                descRTV.Texture2DArray.MipSlice = iMipLevel;
                descRTV.Texture2DArray.FirstArraySlice = iArrayOrDepth;
                break;
            case D3D12_RTV_DIMENSION_TEXTURE3D:
                descRTV.Texture3D.MipSlice = iMipLevel;
                descRTV.Texture3D.FirstWSlice = iArrayOrDepth;
                break;
            default:
                XSF_ASSERT(false);
            }
            
            D3D12_CPU_DESCRIPTOR_HANDLE hRTV = m_RTVHeap.hCPU(m_iRTV);
            m_pDevice->CreateRenderTargetView(pResource, &descRTV, hRTV);
            pCmdList->OMSetRenderTargets(1, &hRTV, FALSE, nullptr);
            m_iRTV = ++ m_iRTV % c_maxRTV;
            
			pCmdList->SetGraphicsRoot32BitConstant(1, iMipLevel - 1, 0);
            pCmdList->SetGraphicsRoot32BitConstant(1, iArrayOrDepth, 1);

            pCmdList->DrawInstanced(4, 1, 0, 0);

            // transition the lower mip level to SR
            descBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            descBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            pCmdList->ResourceBarrier(1, &descBarrier);
        }
    }

    if (resourceState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
    {
        ResourceBarrier(pCmdList, pResource, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, resourceState);
    }

    return S_OK;
}
*/

#if defined(_XBOX_ONE) && defined(_TITLE)
//--------------------------------------------------------------------------------------
// Free up the shader memory used by the deserialized pso
//--------------------------------------------------------------------------------------
void CD3DX12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE::Terminate()
{
    VirtualFree(pPacket, 0, MEM_RELEASE);
    VirtualFree(pMetaData, 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(PS.pGpuInstructions), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(PS.pMetaData), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(VS.pGpuInstructions), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(VS.pMetaData), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(GS.pGpuInstructions), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(GS.pMetaData), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(ES.pGpuInstructions), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(ES.pMetaData), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(HS.pGpuInstructions), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(HS.pMetaData), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(LS.pGpuInstructions), 0, MEM_RELEASE);
    VirtualFree(const_cast<LPVOID>(LS.pMetaData), 0, MEM_RELEASE);

    ZeroMemory(this, sizeof(D3D12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE));
}

//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CD3DX12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE::InitializeFromBlob(ID3DBlob* pBlob, ID3D12RootSignature* pD3D12RootSignature)
{
    Terminate();

    void* pBlobPointer = reinterpret_cast<BYTE*>(pBlob->GetBufferPointer());

    pRootSignature = pD3D12RootSignature;
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, false, &pPacket, &PacketSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, false, &pMetaData, &MetaDataSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&PS.pGpuInstructions), &PS.GpuInstructionsSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&PS.pMetaData), &PS.MetaDataSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&VS.pGpuInstructions), &VS.GpuInstructionsSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&VS.pMetaData), &VS.MetaDataSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&GS.pGpuInstructions), &GS.GpuInstructionsSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&GS.pMetaData), &GS.MetaDataSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&ES.pGpuInstructions), &ES.GpuInstructionsSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&ES.pMetaData), &ES.MetaDataSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&HS.pGpuInstructions), &HS.GpuInstructionsSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&HS.pMetaData), &HS.MetaDataSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&LS.pGpuInstructions), &LS.GpuInstructionsSize));
    XSF_RETURN_IF_FAILED(InitializeFromMemory(&pBlobPointer, true, const_cast<void**>(&LS.pMetaData), &LS.MetaDataSize));

    XSF_ASSERT(pBlobPointer <= reinterpret_cast<BYTE*>(pBlob->GetBufferPointer()) + pBlob->GetBufferSize());

    return S_OK;
}

//--------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CD3DX12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE::InitializeFromMemory(void** ppBlobPointer, bool vMem, void** ppMemory, SIZE_T* blobSize)
{
    BYTE **ppBufferPointer = reinterpret_cast<BYTE**>(ppBlobPointer);

    *blobSize = *reinterpret_cast<const SIZE_T*>(*ppBufferPointer);
    *ppBufferPointer += sizeof(SIZE_T);
    if (*blobSize > 0)
    {
        DWORD flAllocationType = MEM_RESERVE | MEM_COMMIT;
        DWORD flProtect = PAGE_READWRITE;
        if (vMem)
        {
            flAllocationType |= MEM_GRAPHICS | MEM_LARGE_PAGES;
            flProtect |= PAGE_WRITECOMBINE;
        }

        *ppMemory = VirtualAlloc(nullptr, *blobSize, flAllocationType, flProtect);
        if (*ppMemory == nullptr)
        {
            return E_OUTOFMEMORY;
        }

        memcpy(*ppMemory, *ppBufferPointer, *blobSize);
        *ppBufferPointer += *blobSize;
    }

    return S_OK;
}
#endif


//--------------------------------------------------------------------------------------
// Initialize the DDS loader
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT DDSLoader12::Initialize(D3DDevice* const pDevice, D3DCommandQueue* const pCmdQueue, D3DCommandAllocator* const pCmdAlloc, CpuGpuHeap* pUploadHeap)
{
    Terminate();

    m_pDevice = pDevice;
    m_pDevice->AddRef();

    m_pCmdAllocator = pCmdAlloc;
    m_pCmdAllocator->AddRef();

    m_pCmdQueue = pCmdQueue;
    m_pCmdQueue->AddRef();

    m_pUploadHeap = pUploadHeap;

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Terminate the DDS loader, freeing its references
//--------------------------------------------------------------------------------------
void DDSLoader12::Terminate()
{
    XSF_ASSERT(m_pCmdList == nullptr);

    m_pUploadHeap = nullptr;
    XSF_SAFE_RELEASE(m_pCmdQueue);
    XSF_SAFE_RELEASE(m_pCmdAllocator);
    XSF_SAFE_RELEASE(m_pDevice);
}

//--------------------------------------------------------------------------------------
// Begin loading the file by creating a command list
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void DDSLoader12::BeginLoading(D3DCommandList* pExistingCmdList)
{
    XSF_ASSERT(m_pCmdList == nullptr);
    if (pExistingCmdList != nullptr)
    {
        m_pCmdList = pExistingCmdList;
        m_pCmdList->AddRef();
    }
    else
    {
        XSF_ERROR_IF_FAILED(m_pDevice->CreateCommandList(c_nodeMask, D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCmdAllocator, nullptr, IID_GRAPHICS_PPV_ARGS(&m_pCmdList)));
    }
}

//--------------------------------------------------------------------------------------
// Finish loading the file by executing the command list
//--------------------------------------------------------------------------------------
void DDSLoader12::FinishLoading(bool SubmitCmdList)
{
    XSF_ASSERT(m_pCmdList != nullptr);

    if (m_FinalDescs.size() > 0)
    {
        m_pCmdList->ResourceBarrier(static_cast<UINT>(m_FinalDescs.size()), &m_FinalDescs.front());
        m_FinalDescs.clear();
    }

    if (SubmitCmdList)
    {
        m_pCmdList->Close();
        m_pCmdQueue->ExecuteCommandLists(1, (ID3D12CommandList**)(&m_pCmdList));
    }
    XSF_SAFE_RELEASE(m_pCmdList);
}

//--------------------------------------------------------------------------------------
// Return the BPE for a particular format
//--------------------------------------------------------------------------------------
static UINT32 BitsPerElement(_In_ DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
        return 8;
        break;

    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
    case DXGI_FORMAT_BC6H_TYPELESS:
    case DXGI_FORMAT_BC6H_UF16:
    case DXGI_FORMAT_BC6H_SF16:
    case DXGI_FORMAT_BC7_TYPELESS:
    case DXGI_FORMAT_BC7_UNORM:
    case DXGI_FORMAT_BC7_UNORM_SRGB:
        return 16;
        break;

    case DXGI_FORMAT_R8G8_B8G8_UNORM:
    case DXGI_FORMAT_G8R8_G8B8_UNORM:
    case DXGI_FORMAT_YUY2:
        return 4;
        break;

    case DXGI_FORMAT_Y210:
    case DXGI_FORMAT_Y216:
        return 8;
        break;

    case DXGI_FORMAT_NV12:
    case DXGI_FORMAT_420_OPAQUE:
        return 2;
        break;

    case DXGI_FORMAT_P010:
    case DXGI_FORMAT_P016:
        return 4;
        break;

    case DXGI_FORMAT_R32_FLOAT:
        return 32;
        break;
#if defined(_XBOX_ONE) && defined(_TITLE)

    case DXGI_FORMAT_D16_UNORM_S8_UINT:
    case DXGI_FORMAT_R16_UNORM_X8_TYPELESS:
    case DXGI_FORMAT_X16_TYPELESS_G8_UINT:
        return 4;
        break;

#endif
    }

    return 1;
}

//--------------------------------------------------------------------------------------
// Get surface information for a particular format
//--------------------------------------------------------------------------------------
void GetSurfaceInfo(
    _In_ size_t width,
    _In_ size_t height,
    _In_ DXGI_FORMAT fmt,
    _Out_opt_ size_t* outNumBytes,
    _Out_opt_ size_t* outRowBytes,
    _Out_opt_ size_t* outNumRows)
{
    size_t numBytes = 0;
    size_t rowBytes = 0;
    size_t numRows = 0;

    bool packed = false;
    bool planar = false;
    size_t bpe = BitsPerElement(fmt);
 
    if (IsCompressed(fmt))
    {
        size_t numBlocksWide = 0;
        if (width > 0)
        {
            numBlocksWide = std::max<size_t >(1, (width + 3) / 4);
        }
        size_t numBlocksHigh = 0;
        if (height > 0)
        {
            numBlocksHigh = std::max< size_t >(1, (height + 3) / 4);
        }
        rowBytes = numBlocksWide * bpe;
        numRows = numBlocksHigh;
        numBytes = rowBytes * numBlocksHigh;
    }
    else if (packed)
    {
        rowBytes = ((width + 1) >> 1) * bpe;
        numRows = height;
        numBytes = rowBytes * height;
    }
    else if (fmt == DXGI_FORMAT_NV11)
    {
        rowBytes = ((width + 3) >> 2) * 4;
        numRows = height * 2; // Direct3D makes this simplifying assumption, although it is larger than the 4:1:1 data
        numBytes = rowBytes * numRows;
    }
    else if (planar)
    {
        rowBytes = ((width + 1) >> 1) * bpe;
        numBytes = (rowBytes * height) + ((rowBytes * height + 1) >> 1);
        numRows = height + ((height + 1) >> 1);
    }
    else
    {
        size_t bpp = BitsPerPixel(fmt);
        rowBytes = (width * bpp + 7) / 8; // round up to nearest byte
        numRows = height;
        numBytes = rowBytes * height;
    }

    if (outNumBytes)
    {
        *outNumBytes = numBytes;
    }
    if (outRowBytes)
    {
        *outRowBytes = rowBytes;
    }
    if (outNumRows)
    {
        *outNumRows = numRows;
    }
}



//--------------------------------------------------------------------------------------
// Creates a buffer
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CreateBuffer(
    D3DDevice* pDevice,
    D3DCommandList* pCmdList,
    CpuGpuHeap* pUploadHeap, 
    const D3D12_RESOURCE_DESC& desc,
    const BYTE* const pInitialData,
    ID3D12Resource **ppBuffer)
{
    D3D12_RESOURCE_STATES initialUsage = pInitialData ? D3D12_RESOURCE_STATE_COPY_DEST : D3D12_RESOURCE_STATE_COMMON;
    const D3D12_HEAP_PROPERTIES defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
 
    XSF_RETURN_IF_FAILED(pDevice->CreateCommittedResource(
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialUsage,
        nullptr,
        IID_GRAPHICS_PPV_ARGS(ppBuffer)));

	if (pInitialData)
	{
		HRESULT hr;
		if (FAILED(hr = pUploadHeap->CopyBufferDataToDefaultBuffer(pInitialData, static_cast<SIZE_T>(desc.Width), pCmdList, *ppBuffer)))
		{
			XSF_SAFE_RELEASE(*ppBuffer);
			return hr;
		}
		XSF::ResourceBarrier(pCmdList, *ppBuffer, initialUsage, D3D12_RESOURCE_STATE_COMMON);
	}

    return S_OK;
}


//--------------------------------------------------------------------------------------
// Creates a vertex buffer
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CreateVertexBuffer(
    D3DDevice* pDevice,
    D3DCommandList* pCmdList,
    CpuGpuHeap* pUploadHeap, 
    const D3D12_RESOURCE_DESC& desc,
    UINT strideInBytes,
    const BYTE* const pInitialData,
    ID3D12Resource **ppBuffer,
    D3D12_VERTEX_BUFFER_VIEW* pVBView)
{
    XSF_RETURN_IF_FAILED(CreateBuffer(pDevice, pCmdList, pUploadHeap, desc, pInitialData, ppBuffer));
    XSF::ResourceBarrier(pCmdList, *ppBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    if (pVBView)
	{
		ZeroMemory(pVBView, sizeof(*pVBView));
		pVBView->BufferLocation = (*ppBuffer)->GetGPUVirtualAddress();
		pVBView->StrideInBytes = strideInBytes;
		pVBView->SizeInBytes = static_cast< UINT >(desc.Width);
	}
    return S_OK;
}


//--------------------------------------------------------------------------------------
// Creates an index buffer
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CreateIndexBuffer(
    D3DDevice* pDevice,
    D3DCommandList* pCmdList,
    CpuGpuHeap* pUploadHeap, 
    const D3D12_RESOURCE_DESC& desc,
    DXGI_FORMAT format,
    const BYTE* const pInitialData,
    ID3D12Resource **ppBuffer,
    D3D12_INDEX_BUFFER_VIEW* pIBView)
{
    D3D12_RESOURCE_DESC descIB = desc;
#if defined(_XBOX_ONE) && defined(_TITLE)
    descIB.Flags |= D3D12XBOX_RESOURCE_FLAG_PREFER_INDEX_BUFFER;
#endif
    XSF_RETURN_IF_FAILED(CreateBuffer(pDevice, pCmdList, pUploadHeap, descIB, pInitialData, ppBuffer));
    XSF::ResourceBarrier(pCmdList, *ppBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    if (pIBView)
	{
		ZeroMemory(pIBView, sizeof(*pIBView));
		pIBView->BufferLocation = (*ppBuffer)->GetGPUVirtualAddress();
		pIBView->Format = format;
		pIBView->SizeInBytes = static_cast<UINT>(desc.Width);
	}
    return S_OK;
}



//--------------------------------------------------------------------------------------
// Creates a resource with optional SRV/RTV
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CreateResource(
    D3DDevice* pDevice,
    D3DCommandList* pCmdList,
    D3D12_RESOURCE_DESC* pDescTex,
    ID3D12Resource** ppTex,
    CpuGpuHeap* pUploadHeap,
    const BYTE* pInitialData,
    D3D12_RESOURCE_STATES usage)
{
    D3D12_RESOURCE_STATES initialUsage = pInitialData ? D3D12_RESOURCE_STATE_COPY_DEST : usage;
    const D3D12_HEAP_PROPERTIES defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    
    const float aClearColor[] = { 0, 0, 0, 0 };
    CD3DX12_CLEAR_VALUE optimizedClearValue(pDescTex->Format, aClearColor);
    bool bClear = pInitialData == nullptr 
                && ((pDescTex->Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
                || (pDescTex->Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL));

    XSF_RETURN_IF_FAILED(pDevice->CreateCommittedResource(
        &defaultHeapProperties,
        D3D12_HEAP_FLAG_NONE,
        pDescTex,
        initialUsage,
        bClear ? &optimizedClearValue : nullptr,
        IID_GRAPHICS_PPV_ARGS(ppTex)));

    if (pInitialData)
    {
        const UINT arraySize = pDescTex->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D? 1 : pDescTex->DepthOrArraySize;
        const UINT depth = pDescTex->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D? pDescTex->DepthOrArraySize : 1;
        for (UINT arr = 0 ; arr < arraySize; ++arr)
        {
            for (UINT i = 0 ; i < pDescTex->MipLevels; ++i)
            {
                size_t numBytes;
                size_t rowPitchBytes;
                size_t width = static_cast<SIZE_T>(pDescTex->Width) >> i;
                size_t height = static_cast<SIZE_T>(pDescTex->Height) >> i;
                GetSurfaceInfo(width, height, pDescTex->Format, &numBytes, &rowPitchBytes, nullptr);

                UINT subresource = D3D12CalcSubresource(i, arr, 0, pDescTex->MipLevels, pDescTex->DepthOrArraySize);
                D3D12_SUBRESOURCE_FOOTPRINT pitchDescSrc = CD3DX12_SUBRESOURCE_FOOTPRINT(pDescTex->Format, static_cast<UINT>(width), static_cast<UINT>(height), depth, static_cast<UINT>(rowPitchBytes));
                D3D12_SUBRESOURCE_FOOTPRINT pitchDescDst = CD3DX12_SUBRESOURCE_FOOTPRINT(pDescTex->Format, static_cast<UINT>(width), static_cast<UINT>(height), depth, static_cast<UINT>(NextMultiple(rowPitchBytes, XSF_TEXTURE_DATA_PITCH_ALIGNMENT)));

                pUploadHeap->CopyTextureSubresourceToDefaultTexture(pInitialData, pitchDescSrc, pCmdList, *ppTex, subresource, &pitchDescDst);

                pInitialData += height * rowPitchBytes;
            }
        }
        ResourceBarrier(pCmdList, *ppTex, initialUsage, usage);
    }

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Updates a constant buffer
// The caller still needs to call SetGraphicsRootDescriptorTable on the GPU handle afterwards
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT UpdateConstantBuffer(
	D3DDevice* pDevice,
	SIZE_T cbSize,
	XSF::CpuGpuHeap* pUploadHeap,
	D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle,
	const void* pData)
{
    D3D12_CONSTANT_BUFFER_VIEW_DESC descCB = {};
    BYTE* pHeap;
    XSF_RETURN_IF_FAILED(pUploadHeap->GetHeapPointer(cbSize, descCB.BufferLocation, &pHeap));
    memcpy(pHeap, pData, cbSize);

    descCB.SizeInBytes = static_cast< UINT >(NextMultiple(cbSize, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT));
	pDevice->CreateConstantBufferView(&descCB, cpuDescriptorHandle);
		
	return S_OK;
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
static HRESULT FillInitData(
    UINT width,
    UINT height,
    UINT depth,
    UINT16 mipCount,
    UINT16 arraySize,
    DXGI_FORMAT format,
    size_t maxsize,
    size_t bitSize,
    const uint8_t* bitData,
    UINT16* pSkipMip,
    D3D12_SUBRESOURCE_DATA* initData)
{
    if (!bitData || !initData)
    {
        return E_POINTER;
    }

    *pSkipMip = 0;
    size_t numBytes = 0;
    size_t rowBytes = 0;
    const uint8_t* pSrcBits = bitData;
    const uint8_t* pEndBits = bitData + bitSize;
    
    UINT minW = 1;
    UINT minH = 1;

    // ToDo: hack, fix it
    if (IsCompressed(format))
    {
        minW = minH = 4;
    }

    size_t index = 0;
    for(size_t j = 0; j < arraySize; j++)
    {
        for(size_t i = 0; i < mipCount; i++)
        {
            UINT w = std::max(width >> i, minW);
            UINT h = std::max(height >> i, minH);
            UINT d = std::max(depth >> i, 1u);

            GetSurfaceInfo(w, h, format, &numBytes, &rowBytes, nullptr);

            if ((mipCount <= 1) || !maxsize || (w <= maxsize && h <= maxsize && d <= maxsize))
            {
                XSF_ASSERT(index < static_cast<size_t>(mipCount * arraySize));
                _Analysis_assume_(index < mipCount * arraySize);
                initData[ index ].pData = reinterpret_cast< const void* >(pSrcBits);
                initData[ index ].RowPitch = static_cast< UINT >(rowBytes);
                initData[ index ].SlicePitch = static_cast< UINT >(numBytes);
                ++index;
            }
            else if (!j)
            {
                // Count number of skipped mipmaps (first item only)
                ++(*pSkipMip);
            }

            if (pSrcBits + (numBytes * d) > pEndBits)
            {
                return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
            }
  
            pSrcBits += numBytes * d;
        }
    }

    return (index > 0) ? S_OK : E_FAIL;
}


//--------------------------------------------------------------------------------------
static HRESULT CreateD3DResources(
    _In_ D3DDevice* pDevice,
    _In_ D3DCommandList* pCmdList,
    _In_ D3D12_RESOURCE_DIMENSION resDim,
    _In_ UINT width,
    _In_ UINT height,
    _In_ UINT depth,
    _In_ UINT16 mipCount,
    _In_ UINT16 arraySize,
    _In_ DXGI_FORMAT format,
    _In_ bool isCubeMap,
    _In_ D3D12_RESOURCE_FLAGS miscFlags,
    _In_ CpuGpuHeap* pUploadHeap,
    _In_reads_opt_(mipCount * arraySize) D3D12_SUBRESOURCE_DATA* initData,
    _Outptr_opt_ D3D12_CPU_DESCRIPTOR_HANDLE descHandle, 
    _Outptr_opt_ ID3D12Resource** ppTexture)
{
    if (!pDevice || ppTexture == nullptr)
    {
        return E_POINTER;
    }

    ID3D12Resource* pTex = nullptr;
    const D3D12_HEAP_PROPERTIES defaultHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_RESOURCE_DESC texDesc;

    switch (resDim) 
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
           return E_NOTIMPL;
           break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            texDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, arraySize, mipCount, 1, 0, miscFlags);
            break;
        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            texDesc = CD3DX12_RESOURCE_DESC::Tex3D(format, width, height, static_cast<UINT16>(depth), mipCount, miscFlags);
            break; 
    }

    XSF_ERROR_IF_FAILED(pDevice->CreateCommittedResource(
        &defaultHeapProperties, 
        D3D12_HEAP_FLAG_NONE, 
        &texDesc, 
        D3D12_RESOURCE_STATE_COPY_DEST, 
        nullptr, 
        IID_GRAPHICS_PPV_ARGS(&pTex)));
            
    D3D12_SUBRESOURCE_FOOTPRINT pitchSrcDesc = CD3DX12_SUBRESOURCE_FOOTPRINT(format, width, height, depth, 0);
    UINT subresourceIndex = 0;
    UINT minW = 1;
    UINT minH = 1;

    // ToDo: hack, fix it
    if (IsCompressed(format))
    {
        minW = minH = 4;
    }

    for (UINT arr = 0 ; arr < arraySize; ++arr)
    {
        for (UINT i = 0 ; i < mipCount; ++i)
        {
			pitchSrcDesc.RowPitch = static_cast<UINT>(initData[subresourceIndex].RowPitch);
			pitchSrcDesc.Width = std::max(width >> i, minW);
            pitchSrcDesc.Height = std::max(height >> i, minH);
			pitchSrcDesc.Depth = std::max(depth >> i, 1u);
            if (IsCompressed(format))
            {
                pitchSrcDesc.Width = static_cast<UINT32>(NextMultiple(pitchSrcDesc.Width, 4));
                pitchSrcDesc.Height = static_cast<UINT32>(NextMultiple(pitchSrcDesc.Height, 4));
            }
           
			D3D12_SUBRESOURCE_FOOTPRINT pitchDestDesc = pitchSrcDesc;
			pitchDestDesc.RowPitch  = static_cast<UINT32>(NextMultiple(pitchDestDesc.RowPitch , XSF_TEXTURE_DATA_PITCH_ALIGNMENT));

			XSF_ERROR_IF_FAILED(pUploadHeap->CopyTextureSubresourceToDefaultTexture(
				reinterpret_cast<const BYTE*>(initData[subresourceIndex].pData), pitchSrcDesc, pCmdList, pTex, subresourceIndex, &pitchDestDesc));
 
			subresourceIndex++;
        }
    }


    ResourceBarrier(pCmdList, pTex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    
    D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
    descSRV.Format = format;
    descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

    if (isCubeMap)
    {
        descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        descSRV.TextureCube.MipLevels = mipCount;
    }
    else if (arraySize > 1)
    {
        descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        descSRV.Texture2DArray.ArraySize = arraySize;
        descSRV.Texture2DArray.MipLevels = mipCount;
    }
    else if (resDim == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
    {
        descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        descSRV.Texture3D.MipLevels = mipCount;
    }
    else
    {
        descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        descSRV.Texture2D.MipLevels = mipCount;
    }

    if (descHandle.ptr != 0)
    {
        pDevice->CreateShaderResourceView(pTex, &descSRV, descHandle);
    }
	
    if (ppTexture != nullptr)
    {
        *ppTexture = pTex;
    }
    else
    {
        XSF_SAFE_RELEASE(pTex);
    }

    return S_OK;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT DDSLoader12::LoadDDSFile(
    const WCHAR* strFileName, 
    D3D12_CPU_DESCRIPTOR_HANDLE DescHandle, 
    ID3D12Resource** ppTexture, 
    D3D12_RESOURCE_FLAGS miscFlags)
{
    std::vector< BYTE > DDSFile;
    if (FAILED(LoadBlob(strFileName, DDSFile)))
    {
        return E_FAIL;
    }
    const BYTE* pDDSFile = &DDSFile[0];

    return LoadDDSFromMemory(pDDSFile, DDSFile.size(), DescHandle, ppTexture, miscFlags);
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT DDSLoader12::LoadDDSFromMemory(
    const uint8_t* ddsData, size_t ddsDataSize,
    D3D12_CPU_DESCRIPTOR_HANDLE DescHandle, 
    ID3D12Resource** ppTexture, 
    D3D12_RESOURCE_FLAGS miscFlags)
{
    assert(m_pCmdList != nullptr);

    DWORD Magic = *(DWORD*) ddsData;
    if (Magic != ' SDD')
    {
        return E_FAIL;
    }

    const DDS_HEADER* pHeader = reinterpret_cast<const DDS_HEADER*>(ddsData + 4);

    // Check for DX10 extension
    bool bDXT10Header = false;
    if ((pHeader->ddspf.flags & DDS_FOURCC) &&
        (pHeader->ddspf.fourCC == MAKEFOURCC('D', 'X', '1', '0')))
    {
        // Must be long enough for both headers and magic value
        if (ddsDataSize < (sizeof(DDS_HEADER) + sizeof(uint32_t) + sizeof(DDS_HEADER_DXT10)))
        {
            return E_FAIL;
        }

        bDXT10Header = true;
    }

    // setup the pointers in the process request
    ptrdiff_t offset = sizeof(uint32_t) + sizeof(DDS_HEADER)
                     + (bDXT10Header ? sizeof(DDS_HEADER_DXT10) : 0);
    const BYTE* pBits = ddsData + offset;
    size_t bitSize = ddsDataSize - offset;

    const UINT32 width = pHeader->width;
    UINT32 height = pHeader->height;
    UINT32 depth = pHeader->depth;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    D3D12_RESOURCE_DIMENSION resDim = D3D12_RESOURCE_DIMENSION_UNKNOWN;
    UINT16 arraySize = 1;
    bool isCubeMap = false;

    UINT16 mipCount = static_cast<UINT16>(pHeader->mipMapCount);

    if (mipCount == 0)
    {
        mipCount = 1;
    }

    // Check for DX10 extension
    if (bDXT10Header)
    {
        const DDS_HEADER_DXT10* pHeader10 = reinterpret_cast<const DDS_HEADER_DXT10*>(ddsData + sizeof(uint32_t) + sizeof(DDS_HEADER));

        arraySize = static_cast< UINT16 >(pHeader10->arraySize);
        if (arraySize == 0)
        {
           return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        }

        switch(pHeader10->dxgiFormat)
        {
        case DXGI_FORMAT_AI44:
        case DXGI_FORMAT_IA44:
        case DXGI_FORMAT_P8:
        case DXGI_FORMAT_A8P8:
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);

        default:
            if (BitsPerPixel(pHeader10->dxgiFormat) == 0)
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            }
        }
           
        format = pHeader10->dxgiFormat;
        const UINT D3D11_RESOURCE_MISC_TEXTURECUBE = 0x4L; 
        switch (pHeader10->resourceDimension)
        {
        case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
            // D3DX writes 1D textures with a fixed Height of 1
            if ((pHeader->flags & DDS_HEIGHT) && height != 1)
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }
            height = depth = 1;
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (pHeader10->miscFlag & D3D11_RESOURCE_MISC_TEXTURECUBE)
            {
                arraySize *= 6;
                isCubeMap = true;
            }
            depth = 1;
            break;

        case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
            if (!(pHeader->flags & DDS_HEADER_FLAGS_VOLUME))
            {
                return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            }

            if (arraySize > 1)
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            }
            break;

        default:
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }

        resDim = static_cast< D3D12_RESOURCE_DIMENSION >(pHeader10->resourceDimension);
    }
    else
    {
        format = GetDXGIFormat(pHeader->ddspf);
        if (format == DXGI_FORMAT_UNKNOWN)
        {
           return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        }
        
        if (pHeader->flags & DDS_HEADER_FLAGS_VOLUME)
        {
            resDim = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        }
        else 
        {
            if (pHeader->caps2 & DDS_CUBEMAP)
            {
                // We require all six faces to be defined
                if ((pHeader->caps2 & DDS_CUBEMAP_ALLFACES) != DDS_CUBEMAP_ALLFACES)
                {
                    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
                }

                arraySize = 6;
                isCubeMap = true;
            }

            depth = 1;
            resDim = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

            // Note there's no way for a legacy Direct3D 9 DDS to express a '1D' texture
        }

        XSF_ASSERT(BitsPerPixel(format) != 0);
    }

    switch (resDim)
    {
        case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
            if (isCubeMap)
            {
                // This is the right bound because we set arraySize to (NumCubes * 6) above
                if ((arraySize > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) ||
                    (width > D3D12_REQ_TEXTURECUBE_DIMENSION) ||
                    (height > D3D12_REQ_TEXTURECUBE_DIMENSION))
                {
                    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
                }
            }
            else if ((arraySize > D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION) ||
                     (width > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION) ||
                     (height > D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION))
            {
                return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            }
            break;
        default:
            // ToDo
            break;
    }
    
    // Create the texture
    std::unique_ptr<D3D12_SUBRESOURCE_DATA[]> initData(new (std::nothrow) D3D12_SUBRESOURCE_DATA[mipCount * arraySize]);
    if (!initData)
    {
        return E_OUTOFMEMORY;
    }

    UINT16 skipMip = 0;
    size_t maxSize = 0;

    XSF_ERROR_IF_FAILED(FillInitData(width, height, depth, mipCount, arraySize, format, maxSize, 
        bitSize, pBits, &skipMip, initData.get()));
        
    XSF_ERROR_IF_FAILED(CreateD3DResources(m_pDevice, m_pCmdList, resDim, width, height, depth, mipCount - skipMip, 
        arraySize, format, isCubeMap, miscFlags, m_pUploadHeap, initData.get(), DescHandle, ppTexture));


    return S_OK;
}


}   // XboxSampleFramework

