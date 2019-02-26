//----------------------------------------------------------------------------------------------------------------------
// StockRenderStates12.cpp
// 
// Advanced Technology Group (ATG)
// Copyright (c) Microsoft Corporation. All rights reserved.
//----------------------------------------------------------------------------------------------------------------------


#include "pch.h"

#include "StockRenderStates12.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static Member variables
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

XSF::StockRenderStates* XSF::StockRenderStates::ms_pInstance = nullptr;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Templates for the stock states (_DESC's)
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//----------------------------------------------------------------------------------------------------------------------
// Name: s_StockBlendTypes
// Desc: Definitions for common blend states
//----------------------------------------------------------------------------------------------------------------------

#define EMPTY_BLEND_STATE { FALSE, FALSE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO,                                       \
    D3D12_BLEND_OP_ADD, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD, D3D12_LOGIC_OP_NOOP, 0 }

static const D3D12_BLEND_DESC s_StockBlendTypes[] = 
{
    // Overwrite
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            FALSE,                                      // Blend Enable
            FALSE,                                      // LogicOp Enable
            D3D12_BLEND_ONE,                            // SrcBlend
            D3D12_BLEND_ZERO,                           // DestBlend
            D3D12_BLEND_OP_ADD,                         // BlendOp
            D3D12_BLEND_ONE,                            // SrcBlendAlpha
            D3D12_BLEND_ZERO,                           // DestBlendAlpha
            D3D12_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D12_LOGIC_OP_NOOP,                        // LogicOp
            D3D12_COLOR_WRITE_ENABLE_ALL                // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
        }
    },

    // AlphaBlend
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            TRUE,                                       // Blend Enable
            FALSE,                                      // LogicOp Enable
            D3D12_BLEND_ONE,                            // SrcBlend
            D3D12_BLEND_INV_SRC_ALPHA,                  // DestBlend
            D3D12_BLEND_OP_ADD,                         // BlendOp
            D3D12_BLEND_ONE,                            // SrcBlendAlpha
            D3D12_BLEND_INV_SRC_ALPHA,                  // DestBlendAlpha
            D3D12_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D12_LOGIC_OP_NOOP,                        // LogicOp
            D3D12_COLOR_WRITE_ENABLE_ALL,               // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
        }
    },

    // PremultipliedAlphaBlend
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            TRUE,                                       // Blend Enable
            FALSE,                                      // LogicOp Enable
            D3D12_BLEND_SRC_ALPHA,                      // SrcBlend
            D3D12_BLEND_INV_SRC_ALPHA,                  // DestBlend
            D3D12_BLEND_OP_ADD,                         // BlendOp
            D3D12_BLEND_SRC_ALPHA,                      // SrcBlendAlpha
            D3D12_BLEND_INV_SRC_ALPHA,                  // DestBlendAlpha
            D3D12_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D12_LOGIC_OP_NOOP,                        // LogicOp
            D3D12_COLOR_WRITE_ENABLE_ALL,               // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
        }
    },

    // AdditiveBlendUseSrcAlphaRetainDestAlpha
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            TRUE,                                       // Blend Enable
            FALSE,                                      // LogicOp Enable
            D3D12_BLEND_SRC_ALPHA,                      // SrcBlend
            D3D12_BLEND_ONE,                            // DestBlend
            D3D12_BLEND_OP_ADD,                         // BlendOp
            D3D12_BLEND_ZERO,                           // SrcBlendAlpha
            D3D12_BLEND_SRC_ALPHA,                      // DestBlendAlpha
            D3D12_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D12_LOGIC_OP_NOOP,                        // LogicOp
            D3D12_COLOR_WRITE_ENABLE_ALL,               // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
        }
    },

    // AdditiveBlendIgnoreAlphaKeepDestAlpha,
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            TRUE,                                       // Blend Enable
            FALSE,                                      // LogicOp Enable
            D3D12_BLEND_ONE,                            // SrcBlend
            D3D12_BLEND_ONE,                            // DestBlend
            D3D12_BLEND_OP_ADD,                         // BlendOp
            D3D12_BLEND_ZERO,                           // SrcBlendAlpha
            D3D12_BLEND_ONE,                            // DestBlendAlpha
            D3D12_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D12_LOGIC_OP_NOOP,                        // LogicOp
            D3D12_COLOR_WRITE_ENABLE_ALL,               // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
        }
    }
};

static_assert(ARRAYSIZE(s_StockBlendTypes) == static_cast<UINT>(StockBlendStates::BlendStateCount),
              "Blend Description count doesn't match Blend State list");

#undef EMPTY_BLEND_STATE

//----------------------------------------------------------------------------------------------------------------------
// Name: s_StockSamplerTypes
// Desc: The D3D12_SAMPLER_DESC values for each of the stock sampler types.
//----------------------------------------------------------------------------------------------------------------------
static const D3D12_SAMPLER_DESC s_StockSamplerTypes[] = 
{

    // MinMagMipPointUVWClamp
    {
        D3D12_FILTER_MIN_MAG_MIP_POINT,                 // Filter mode
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,               // U address clamping
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,               // V address clamping
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,               // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D12_COMPARISON_FUNC_ALWAYS,                   // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D12_FLOAT32_MAX                               // MaxLOD
    },

    // MinMagLinearMipPointUVWClamp
    {
        D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,          // Filter mode
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,               // U address clamping
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,               // V address clamping
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,               // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D12_COMPARISON_FUNC_ALWAYS,                   // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D12_FLOAT32_MAX                               // MaxLOD
    },

    // MinMagMipLinearUVWClamp
    {
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,                // Filter mode
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,               // U address clamping
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,               // V address clamping
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,               // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D12_COMPARISON_FUNC_ALWAYS,                   // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D12_FLOAT32_MAX                               // MaxLOD
    },

    // MinMagMipLinearUVWWrap
    {
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,                // Filter mode
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,                // U address clamping
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,                // V address clamping
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,                // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D12_COMPARISON_FUNC_ALWAYS,                   // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D12_FLOAT32_MAX                               // MaxLOD
    },

    // MinMagLinearMipPointUVWWrap
    {
        D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT,          // Filter mode
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,                // U address clamping
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,                // V address clamping
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,                // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D12_COMPARISON_FUNC_ALWAYS,                   // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D12_FLOAT32_MAX                               // MaxLOD
    },
    // UseShadowMap
    {
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,// Filter mode
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,                    // U address clamping
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,                    // V address clamping
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,                    // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        { 1.0F, 1.0F, 1.0F, 1.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D12_FLOAT32_MAX                               // MaxLOD
    }

};


static_assert(ARRAYSIZE(s_StockSamplerTypes) == static_cast<UINT>(StockSamplerStates::SamplerStateCount),
              "Sampler Description count doesn't match Sampler State list");

//----------------------------------------------------------------------------------------------------------------------
// Name: s_StockRasterizerTypes
// Desc: The D3D11_RASTERIZER_DESC values for each of the stock rasterizer types.
//----------------------------------------------------------------------------------------------------------------------
static const D3D12_RASTERIZER_DESC s_StockRasterizerTypes[] = 
{
    // Solid
    {
        D3D12_FILL_MODE_SOLID,                          // FillMode
        D3D12_CULL_MODE_BACK,                           // CullMode
        FALSE,                                          // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
        0,                                              // ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF       // ConservativeRaster
    },

    // Solid_CCW
    {
        D3D12_FILL_MODE_SOLID,                          // FillMode
        D3D12_CULL_MODE_BACK,                           // CullMode
        TRUE,                                           // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
        0,                                              // ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF       // ConservativeRaster
    },

    // Solid_CulLFront
    {
        D3D12_FILL_MODE_SOLID,                          // FillMode
        D3D12_CULL_MODE_FRONT,                          // CullMode
        FALSE,                                          // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
        0,                                              // ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF       // ConservativeRaster
    },

    // Solid_CullFront_CCW
    {
        D3D12_FILL_MODE_SOLID,                          // FillMode
        D3D12_CULL_MODE_FRONT,                          // CullMode
        TRUE,                                           // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
        0,                                              // ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF       // ConservativeRaster
    },

    // Wireframe
    {
        D3D12_FILL_MODE_WIREFRAME,                      // FillMode
        D3D12_CULL_MODE_BACK,                           // CullMode
        FALSE,                                          // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
        0,                                              // ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF       // ConservativeRaster
    },

    // Wireframe_CCW
    {
        D3D12_FILL_MODE_WIREFRAME,                      // FillMode
        D3D12_CULL_MODE_BACK,                           // CullMode
        TRUE,                                           // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
        0,                                              // ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF       // ConservativeRaster
    },

    // Wireframe_NoBackFaceCull
    {
        D3D12_FILL_MODE_WIREFRAME,                      // FillMode
        D3D12_CULL_MODE_NONE,                           // CullMode
        FALSE,                                          // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
        0,                                              // ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF       // ConservativeRaster
    },
    // BuildShadowMap
    {
        D3D12_FILL_MODE_SOLID,                               // FillMode
        D3D12_CULL_MODE_BACK,                                // CullMode
        FALSE,                                          // FrontCounterClockwise
        100000,                                         // DepthBias
        3.0f,                                           // DepthBiasClamp
        4.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
        0,                                              // ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF       // ConservativeRaster
    }
};

static_assert( ARRAYSIZE(s_StockRasterizerTypes) == static_cast<UINT>(StockRasterizerStates::RasterizerStateCount),
              "Rasterizer Description count doesn't match Rasterizer State list");

static const D3D12_DEPTH_STENCIL_DESC s_StockDepthStencilTypes[StockDepthStencilStates::DepthStencilStateCount] =
{
    // AlwaysSucceed_WriteZOut_NoStencil,
    // No depth test, no stencil test, updates Z buffer
    {
        FALSE,                                          // DepthEnable
        D3D12_DEPTH_WRITE_MASK_ALL,                     // DepthWriteMask
        D3D12_COMPARISON_FUNC_ALWAYS,                   // DepthFunc
        FALSE,                                          // StencilEnable
        D3D12_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D12_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        }
    },

    // AlwaysSucceed_NoWriteZ_NoStencil,
    // No depth test, no stencil test, doesn't modify Z-buffer
    {
        FALSE,                                          // DepthEnable
        D3D12_DEPTH_WRITE_MASK_ZERO,                    // DepthWriteMask
        D3D12_COMPARISON_FUNC_ALWAYS,                   // DepthFunc
        FALSE,                                          // StencilEnable
        D3D12_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D12_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        }
    },

    // DepthLessThan_WriteZOut_NoStencil,
    // Pass the depth test if distance is < value in the depth buffer. No stencil test is performed. Updates Z buffer.
    {
        TRUE,                                           // DepthEnable
        D3D12_DEPTH_WRITE_MASK_ALL,                     // DepthWriteMask
        D3D12_COMPARISON_FUNC_LESS,                     // DepthFunc
        FALSE,                                          // StencilEnable
        D3D12_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D12_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        }
    },

    // DepthLessThan_NoWriteZ_NoStencil,
    // Pass the depth test if distance is < value in the depth buffer. No stencil test is performed. Z buffer isn't updated.
    {
        TRUE,                                           // DepthEnable
        D3D12_DEPTH_WRITE_MASK_ZERO,                    // DepthWriteMask
        D3D12_COMPARISON_FUNC_LESS,                     // DepthFunc
        FALSE,                                          // StencilEnable
        D3D12_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D12_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        }
    },

    // DepthMustBeLessThanOrEqual_WriteZOut_NoStencil,
    // Pass the depth test if distance is <= value in the depth buffer. No stencil test is performed. Updates Z buffer.
    {
        TRUE,                                           // DepthEnable
        D3D12_DEPTH_WRITE_MASK_ALL,                     // DepthWriteMask
        D3D12_COMPARISON_FUNC_LESS_EQUAL,               // DepthFunc
        FALSE,                                          // StencilEnable
        D3D12_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D12_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        }
    },

    // DepthMustBeLessThanOrEqual_NoWriteZ_NoStencil,
    // Pass the depth test if distance is <= value in the depth buffer. No stencil test is performed. Z Buffer isn't updated.
    {
        TRUE,                                           // DepthEnable
        D3D12_DEPTH_WRITE_MASK_ZERO,                    // DepthWriteMask
        D3D12_COMPARISON_FUNC_LESS_EQUAL,               // DepthFunc
        FALSE,                                          // StencilEnable
        D3D12_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D12_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D12_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D12_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D12_COMPARISON_FUNC_ALWAYS                // StencilFunc
        }
    }
};

static_assert(ARRAYSIZE(s_StockDepthStencilTypes) == static_cast<UINT>(StockDepthStencilStates::DepthStencilStateCount),
              "Depth Stencil Description count doesn't match Depth Stencil State list");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function implementations
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::GenerateStockBlendDesc
// Desc: Creates the Blend descriptors for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::GenerateStockBlendDesc()
{
    // Create the stock states...
    memcpy(m_BlendDesc, &s_StockBlendTypes, sizeof(s_StockBlendTypes));
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::GenerateStockSamplerHeap
// Desc: Creates the Sampler Heap for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT XboxSampleFramework::StockRenderStates::GenerateStockSamplerHeap(XSF::D3DDevice* const pDev)
{
    VERBOSEATGPROFILETHIS;

    // Create the stock states...
    memcpy(m_SamplerDesc, &s_StockSamplerTypes, sizeof(s_StockSamplerTypes));
    XSF_ERROR_IF_FAILED(m_SamplerHeap.Initialize(pDev, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, static_cast<int>(StockSamplerStates::SamplerStateCount), true));
    for (int i = 0; i < static_cast<int>(StockSamplerStates::SamplerStateCount); ++i)
    {
        pDev->CreateSampler(&m_SamplerDesc[i], m_SamplerHeap.hCPU(i));

        m_StaticSampler[i] = CD3DX12_STATIC_SAMPLER_DESC(
            0,
            m_SamplerDesc[i].Filter,
            m_SamplerDesc[i].AddressU,
            m_SamplerDesc[i].AddressV,
            m_SamplerDesc[i].AddressW,
            m_SamplerDesc[i].MipLODBias,
            m_SamplerDesc[i].MaxAnisotropy,
            m_SamplerDesc[i].ComparisonFunc,
            D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK,
            m_SamplerDesc[i].MinLOD,
            m_SamplerDesc[i].MaxLOD);
    }

    return S_OK;
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::DestroyStockSamplerHeap
// Desc: Destroys the Sampler Heap for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::DestroyStockSamplerHeap()
{
    m_SamplerHeap.Terminate();
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::GenerateStockRasterizerDesc
// Desc: Creates the Rasterizer descriptors for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
void XboxSampleFramework::StockRenderStates::GenerateStockRasterizerDesc()
{
    // Create the stock states...
    memcpy(m_RasterizerDesc, s_StockRasterizerTypes, sizeof(s_StockRasterizerTypes));
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::GenerateStockDepthStencilDesc
// Desc: Creates the Depth/Stencil descriptors for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::GenerateStockDepthStencilDesc()
{
    // Create the stock states...
    memcpy(m_DepthStencilDesc, s_StockDepthStencilTypes, sizeof(s_StockDepthStencilTypes));
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::~StockRenderStates
// Desc: Destroys the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
XboxSampleFramework::StockRenderStates::~StockRenderStates()
{
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::Initialize
// Desc: 
//----------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT XboxSampleFramework::StockRenderStates::Initialize(D3DDevice* const pDevice)
{
    ATGPROFILETHIS;

    if (ms_pInstance == nullptr)
    {
        ms_pInstance = new StockRenderStates;

        // (Note: if we fail within Generate, Generate unwinds for us)

        HRESULT hr = ms_pInstance->GenerateStockSamplerHeap(pDevice);
        if (FAILED(hr))
        {
            CleanUpAfterInitialize();
            return hr;
        }

        ms_pInstance->GenerateStockBlendDesc();
        ms_pInstance->GenerateStockRasterizerDesc();
        ms_pInstance->GenerateStockDepthStencilDesc();

        return hr;
    }
    else
    {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CleanUpAfterInitialize
// Desc: Performs clean up by destroying different states
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::CleanUpAfterInitialize()
{
    ms_pInstance->DestroyStockSamplerHeap();

    StockRenderStates* pTempInstance = ms_pInstance;
    ms_pInstance = nullptr;
    delete pTempInstance;
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::Shutdown
// Desc: Tears down the Stock states, destroys their objects and destroys the StockRenderStates singleton.
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::Shutdown()
{
    if (ms_pInstance == nullptr)
    {
        // Already shutdown.
        return;
    }

    ms_pInstance->DestroyStockSamplerHeap();
    
    StockRenderStates* pTempInstance = ms_pInstance;
    ms_pInstance = nullptr;
    delete pTempInstance;
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CopyBlendTemplate
// Desc: Copies a Blend state template to the destination
// Parameters:
//   Destination - The D3D12_BLEND_DESC struct to fill.
//   state       - The state to copy to the destination.
//----------------------------------------------------------------------------------------------------------------------
void XSF::StockRenderStates::CopyBlendTemplate( D3D12_BLEND_DESC& Destination, StockBlendStates state ) const
{
    memcpy(&Destination, &s_StockBlendTypes[static_cast<size_t>(state)], sizeof(Destination));
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CopySamplerTemplate
// Desc: Copies a Sampler template to the destination
// Parameters:
//   Destination - The D3D12_SAMPLER_DESC struct to fill.
//   state       - The state to copy to the destination.
//----------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
void XSF::StockRenderStates::CopySamplerTemplate(D3D12_SAMPLER_DESC& Destination, StockSamplerStates state) const
{
    memcpy(&Destination, &s_StockSamplerTypes[static_cast<size_t>(state)], sizeof(Destination));
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CopyRasterizerTemplate
// Desc: Copies a Rasterizer state template to the destination
// Parameters:
//   Destination - The D3D12_RASTERIZER_DESC struct to fill.
//   state       - The state to copy to the destination.
//----------------------------------------------------------------------------------------------------------------------
_Use_decl_annotations_
void XSF::StockRenderStates::CopyRasterizerTemplate(D3D12_RASTERIZER_DESC& Destination, StockRasterizerStates state) const
{
    memcpy(&Destination, &s_StockRasterizerTypes[static_cast<size_t>(state)], sizeof(Destination));
}

//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CopyDepthStencilTemplate
// Desc: Copies a Depth/Stencil state template to the destination
// Parameters:
//   Destination - The D3D12_DEPTH_STENCIL_DESC struct to fill.
//   state       - The state to copy to the destination.
//----------------------------------------------------------------------------------------------------------------------
void XSF::StockRenderStates::CopyDepthStencilTemplate(D3D12_DEPTH_STENCIL_DESC& Destination, StockDepthStencilStates state) const
{
    memcpy(&Destination, &s_StockDepthStencilTypes[static_cast<size_t>(state)], sizeof(Destination));
}
