//----------------------------------------------------------------------------------------------------------------------
// StockRenderStates.cpp
// 
// Advanced Technology Group (ATG)
// Copyright (c) Microsoft Corporation. All rights reserved.
//----------------------------------------------------------------------------------------------------------------------


#include "pch.h"
#include "StockRenderStates.h"

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

#define EMPTY_BLEND_STATE { FALSE, D3D11_BLEND_ONE, D3D11_BLEND_ZERO,                                       \
    D3D11_BLEND_OP_ADD, D3D11_BLEND_ONE, D3D11_BLEND_ZERO, D3D11_BLEND_OP_ADD, 0 }

static const D3D11_BLEND_DESC s_StockBlendTypes[] = 
{
    // Overwrite
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            FALSE,                                      // Blend Enable
            D3D11_BLEND_ONE,                            // SrcBlend
            D3D11_BLEND_ZERO,                           // DestBlend
            D3D11_BLEND_OP_ADD,                         // BlendOp
            D3D11_BLEND_ONE,                            // SrcBlendAlpha
            D3D11_BLEND_ZERO,                           // DestBlendAlpha
            D3D11_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D11_COLOR_WRITE_ENABLE_ALL                // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
    }},

    // AlphaBlend
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            TRUE,                                       // Blend Enable
            D3D11_BLEND_SRC_ALPHA,                      // SrcBlend
            D3D11_BLEND_INV_SRC_ALPHA,                  // DestBlend
            D3D11_BLEND_OP_ADD,                         // BlendOp
            D3D11_BLEND_SRC_ALPHA,                      // SrcBlendAlpha
            D3D11_BLEND_INV_SRC_ALPHA,                  // DestBlendAlpha
            D3D11_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D11_COLOR_WRITE_ENABLE_ALL,               // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
    }},

    // PremultipliedAlphaBlend
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            TRUE,                                       // Blend Enable
            D3D11_BLEND_SRC_ALPHA,                      // SrcBlend
            D3D11_BLEND_INV_SRC_ALPHA,                  // DestBlend
            D3D11_BLEND_OP_ADD,                         // BlendOp
            D3D11_BLEND_SRC_ALPHA,                      // SrcBlendAlpha
            D3D11_BLEND_INV_SRC_ALPHA,                  // DestBlendAlpha
            D3D11_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D11_COLOR_WRITE_ENABLE_ALL,               // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
        }},

    
    // AdditiveBlendUseSrcAlphaRetainDestAlpha
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            TRUE,                                       // Blend Enable
            D3D11_BLEND_SRC_ALPHA,                      // SrcBlend
            D3D11_BLEND_ONE,                            // DestBlend
            D3D11_BLEND_OP_ADD,                         // BlendOp
            D3D11_BLEND_ZERO,                           // SrcBlendAlpha
            D3D11_BLEND_SRC_ALPHA,                      // DestBlendAlpha
            D3D11_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D11_COLOR_WRITE_ENABLE_ALL,               // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
        }},

        
    // AdditiveBlendIgnoreAlphaKeepDestAlpha,
    {
        FALSE,                                          // AlphaToCoverageEnable
        FALSE,                                          // IndependentBlendEnable
        {{                                              // Blend Target 0
            TRUE,                                       // Blend Enable
            D3D11_BLEND_ONE,                            // SrcBlend
            D3D11_BLEND_ONE,                            // DestBlend
            D3D11_BLEND_OP_ADD,                         // BlendOp
            D3D11_BLEND_ZERO,                           // SrcBlendAlpha
            D3D11_BLEND_ONE,                            // DestBlendAlpha
            D3D11_BLEND_OP_ADD,                         // BlendOpAlpha
            D3D11_COLOR_WRITE_ENABLE_ALL,               // RenderTargetWriteMask
        },
        EMPTY_BLEND_STATE,                              // Blend Target 1
        EMPTY_BLEND_STATE,                              // Blend Target 2
        EMPTY_BLEND_STATE,                              // Blend Target 3
        EMPTY_BLEND_STATE,                              // Blend Target 4
        EMPTY_BLEND_STATE,                              // Blend Target 5
        EMPTY_BLEND_STATE,                              // Blend Target 6
        EMPTY_BLEND_STATE,                              // Blend Target 7
        }}
};

static_assert( ARRAYSIZE( s_StockBlendTypes ) == (UINT)XSF::StockBlendStates::BlendStateCount,
              "Blend Description count doesn't match Blend State list" );

#undef EMPTY_BLEND_STATE

//----------------------------------------------------------------------------------------------------------------------
// Name: s_StockSamplerTypes
// Desc: The D3D11_SAMPLER_DESC values for each of the stock sampler types.
//----------------------------------------------------------------------------------------------------------------------
static const D3D11_SAMPLER_DESC s_StockSamplerTypes[] = 
{
    
    // MinMagMipPointUVWClamp
    {
        D3D11_FILTER_MIN_MAG_MIP_POINT,                 // Filter mode
        D3D11_TEXTURE_ADDRESS_CLAMP,                    // U address clamping
        D3D11_TEXTURE_ADDRESS_CLAMP,                    // V address clamping
        D3D11_TEXTURE_ADDRESS_CLAMP,                    // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D11_COMPARISON_ALWAYS,                        // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D11_FLOAT32_MAX                               // MaxLOD
    },
    // MinMagLinearMipPointUVWClamp
    {
        D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,          // Filter mode
        D3D11_TEXTURE_ADDRESS_CLAMP,                    // U address clamping
        D3D11_TEXTURE_ADDRESS_CLAMP,                    // V address clamping
        D3D11_TEXTURE_ADDRESS_CLAMP,                    // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D11_COMPARISON_ALWAYS,                        // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D11_FLOAT32_MAX                               // MaxLOD
    },
    // MinMagMipLinearUVWClamp
    {
        D3D11_FILTER_MIN_MAG_MIP_LINEAR,                // Filter mode
        D3D11_TEXTURE_ADDRESS_CLAMP,                    // U address clamping
        D3D11_TEXTURE_ADDRESS_CLAMP,                    // V address clamping
        D3D11_TEXTURE_ADDRESS_CLAMP,                    // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D11_COMPARISON_ALWAYS,                        // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D11_FLOAT32_MAX                               // MaxLOD
    },
    // MinMagMipLinearUVWWrap
    {
        D3D11_FILTER_MIN_MAG_MIP_LINEAR,                // Filter mode
        D3D11_TEXTURE_ADDRESS_WRAP,                     // U address clamping
        D3D11_TEXTURE_ADDRESS_WRAP,                     // V address clamping
        D3D11_TEXTURE_ADDRESS_WRAP,                     // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D11_COMPARISON_ALWAYS,                        // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D11_FLOAT32_MAX                               // MaxLOD
    },
    // MinMagLinearMipPointUVWWrap
    {
        D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT,          // Filter mode
        D3D11_TEXTURE_ADDRESS_WRAP,                     // U address clamping
        D3D11_TEXTURE_ADDRESS_WRAP,                     // V address clamping
        D3D11_TEXTURE_ADDRESS_WRAP,                     // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D11_COMPARISON_ALWAYS,                        // Comparison Func - always pass
        { 0.0F, 0.0F, 0.0F, 0.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D11_FLOAT32_MAX                               // MaxLOD
    },
    // UseShadowMap
    {
        D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,// Filter mode
        D3D11_TEXTURE_ADDRESS_BORDER,                    // U address clamping
        D3D11_TEXTURE_ADDRESS_BORDER,                    // V address clamping
        D3D11_TEXTURE_ADDRESS_BORDER,                    // W address clamping
        0.0F,                                           // Mip LOD bias
        0,                                              // Max Anisotropy - applies if using ANISOTROPIC filtering only
        D3D11_COMPARISON_LESS_EQUAL,                        
        { 0.0F, 0.0F, 0.0F, 1.0F },                     // BorderColor float values - used if D3D11_TEXTURE_ADDRESS_BORDER is set.
        0.0F,                                           // MinLOD
        D3D11_FLOAT32_MAX                               // MaxLOD
    }

};


static_assert( ARRAYSIZE( s_StockSamplerTypes ) == (UINT)XSF::StockSamplerStates::SamplerStateCount,
              "Sampler Description count doesn't match Sampler State list" );

//----------------------------------------------------------------------------------------------------------------------
// Name: s_StockRasterizerTypes
// Desc: The D3D11_RASTERIZER_DESC values for each of the stock rasterizer types.
//----------------------------------------------------------------------------------------------------------------------
#if defined( XSF_USE_DX_11_1 )
static const D3D11_RASTERIZER_DESC1 s_StockRasterizerTypes[] = 
#else
static const D3D11_RASTERIZER_DESC s_StockRasterizerTypes[] = 
#endif
{
    // Solid
    {
        D3D11_FILL_SOLID,                               // FillMode
        D3D11_CULL_BACK,                                // CullMode
        FALSE,                                          // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // ScissorEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
#if defined( XSF_USE_DX_11_1 )
        0                                               // ForcedSampleCount
#endif
    },

    // Solid_CCW
    {
        D3D11_FILL_SOLID,                               // FillMode
        D3D11_CULL_BACK,                                // CullMode
        TRUE,                                           // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // ScissorEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
#if defined( XSF_USE_DX_11_1 )
        0                                               // ForcedSampleCount
#endif
    },

    // Solid_CulLFront
    {
        D3D11_FILL_SOLID,                               // FillMode
        D3D11_CULL_FRONT,                                // CullMode
        FALSE,                                          // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // ScissorEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
#if defined( XSF_USE_DX_11_1 )
        0                                               // ForcedSampleCount
#endif
    },

    // Solid_CullFront_CCW
    {
        D3D11_FILL_SOLID,                               // FillMode
        D3D11_CULL_FRONT,                               // CullMode
        TRUE,                                           // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // ScissorEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
#if defined( XSF_USE_DX_11_1 )
        0                                               // ForcedSampleCount
#endif
    },

    // Wireframe
    {
        D3D11_FILL_WIREFRAME,                           // FillMode
        D3D11_CULL_BACK,                                // CullMode
        FALSE,                                          // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // ScissorEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
#if defined( XSF_USE_DX_11_1 )
        0                                               // ForcedSampleCount
#endif
    },

    // Wireframe_CCW
    {
        D3D11_FILL_WIREFRAME,                           // FillMode
        D3D11_CULL_BACK,                                // CullMode
        TRUE,                                           // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // ScissorEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
#if defined( XSF_USE_DX_11_1 )
        0                                               // ForcedSampleCount
#endif
    },

    // Wireframe_NoBackFaceCull
    {
        D3D11_FILL_WIREFRAME,                           // FillMode
        D3D11_CULL_NONE,                                // CullMode
        FALSE,                                          // FrontCounterClockwise
        0,                                              // DepthBias
        0.0f,                                           // DepthBiasClamp
        0.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // ScissorEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
#if defined( XSF_USE_DX_11_1 )
        0                                               // ForcedSampleCount
#endif
    },

    // BuildShadowMap
    {
        D3D11_FILL_SOLID,                               // FillMode
        D3D11_CULL_BACK,                                // CullMode
        FALSE,                                          // FrontCounterClockwise
        100000,                                         // DepthBias
        0.0f,                                           // DepthBiasClamp
        1.0f,                                           // SlopeScaledDepthBias
        TRUE,                                           // DepthClipEnable
        FALSE,                                          // ScissorEnable
        FALSE,                                          // MultisampleEnable
        FALSE,                                          // AntialiasedLineEnable
#if defined( XSF_USE_DX_11_1 )
        0                                               // ForcedSampleCount
#endif
    },

};

static_assert( ARRAYSIZE( s_StockRasterizerTypes ) == (UINT)XSF::StockRasterizerStates::RasterizerStateCount,
              "Rasterizer Description count doesn't match Rasterizer State list" );

static const D3D11_DEPTH_STENCIL_DESC s_StockDepthStencilTypes[ XSF::StockDepthStencilStates::DepthStencilStateCount ] =
{
    // AlwaysSucceed_WriteZOut_NoStencil,
    // No depth test, no stencil test, updates Z buffer
    {
        FALSE,                                          // DepthEnable
        D3D11_DEPTH_WRITE_MASK_ALL,                     // DepthWriteMask
        D3D11_COMPARISON_ALWAYS,                        // DepthFunc
        FALSE,                                          // StencilEnable
        D3D11_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D11_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        }
    },

    // AlwaysSucceed_NoWriteZ_NoStencil,
    // No depth test, no stencil test, doesn't modify Z-buffer
    {
        FALSE,                                          // DepthEnable
        D3D11_DEPTH_WRITE_MASK_ZERO,                    // DepthWriteMask
        D3D11_COMPARISON_ALWAYS,                        // DepthFunc
        FALSE,                                          // StencilEnable
        D3D11_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D11_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        }
    },

    // DepthLessThan_WriteZOut_NoStencil,
    // Pass the depth test if distance is < value in the depth buffer. No stencil test is performed. Updates Z buffer.
    {
        TRUE,                                           // DepthEnable
        D3D11_DEPTH_WRITE_MASK_ALL,                     // DepthWriteMask
        D3D11_COMPARISON_LESS,                          // DepthFunc
        FALSE,                                          // StencilEnable
        D3D11_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D11_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        }
    },

    // DepthLessThan_NoWriteZ_NoStencil,
    // Pass the depth test if distance is < value in the depth buffer. No stencil test is performed. Z buffer isn't updated.
    {
        TRUE,                                           // DepthEnable
        D3D11_DEPTH_WRITE_MASK_ZERO,                    // DepthWriteMask
        D3D11_COMPARISON_LESS,                          // DepthFunc
        FALSE,                                          // StencilEnable
        D3D11_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D11_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        }
    },

    // DepthMustBeLessThanOrEqual_WriteZOut_NoStencil,
    // Pass the depth test if distance is <= value in the depth buffer. No stencil test is performed. Updates Z buffer.
    {
        TRUE,                                           // DepthEnable
        D3D11_DEPTH_WRITE_MASK_ALL,                     // DepthWriteMask
        D3D11_COMPARISON_LESS_EQUAL,                    // DepthFunc
        FALSE,                                          // StencilEnable
        D3D11_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D11_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        }
    },

    // DepthMustBeLessThanOrEqual_NoWriteZ_NoStencil,
    // Pass the depth test if distance is <= value in the depth buffer. No stencil test is performed. Z Buffer isn't updated.
    {
        TRUE,                                           // DepthEnable
        D3D11_DEPTH_WRITE_MASK_ZERO,                    // DepthWriteMask
        D3D11_COMPARISON_LESS_EQUAL,                    // DepthFunc
        FALSE,                                          // StencilEnable
        D3D11_DEFAULT_STENCIL_READ_MASK,                // StencilReadMask
        D3D11_DEFAULT_STENCIL_WRITE_MASK,               // StencilWriteMask
        {                                               // FrontFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        },
        {                                               // BackFace - D3D11_DEPTH_STENCILOP_DESC
            D3D11_STENCIL_OP_KEEP,                      // StencilFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilDepthFailOp
            D3D11_STENCIL_OP_KEEP,                      // StencilPassOp
            D3D11_COMPARISON_ALWAYS                     // StencilFunc
        }
    }
};

static_assert( ARRAYSIZE( s_StockDepthStencilTypes ) == (UINT)XSF::StockDepthStencilStates::DepthStencilStateCount,
              "Depth Stencil Description count doesn't match Depth Stencil State list" );


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Function implementations
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::GenerateStockBlendStates
// Desc: Creates the Blend States for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
HRESULT XboxSampleFramework::StockRenderStates::GenerateStockBlendStates( _In_ XSF::D3DDevice* pDev )
{
    VERBOSEATGPROFILETHIS;

    // Create the stock states...

    for( int i = 0; i < (int)StockBlendStates::BlendStateCount; ++i )
    {
        HRESULT hr = pDev->CreateBlendState( &s_StockBlendTypes[ i ], &m_BlendStates[ i ] );

        XSF_ASSERT( SUCCEEDED( hr ) && "Couldn't create state object" );
        if ( FAILED( hr ) )
        {
            // Unwind the object creation.
            for( --i; i >= 0; --i )
            {
                XSF_SAFE_RELEASE( m_BlendStates[ i ] );
            }

            return hr;
        }
    }

    return S_OK;
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::DestroyStockBlendStates
// Desc: Destroys the Blend States for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::DestroyStockBlendStates()
{
    for( int i = 0; i < (int)StockBlendStates::BlendStateCount; ++i )
    {
        XSF_SAFE_RELEASE( m_BlendStates[ i ] );
    }
}

//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::GenerateStockSamplerStates
// Desc: Creates the Sampler States for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
HRESULT XboxSampleFramework::StockRenderStates::GenerateStockSamplerStates( _In_ XSF::D3DDevice* pDev )
{
    VERBOSEATGPROFILETHIS;

    // Create the stock states...

    for( int i = 0; i < (int)StockSamplerStates::SamplerStateCount; ++i )
    {
        HRESULT hr = pDev->CreateSamplerState( &s_StockSamplerTypes[ i ], &m_SamplerStates[ i ] );
        XSF_ASSERT( SUCCEEDED( hr ) && "Couldn't create state object" );
        if ( FAILED( hr ) )
        {
            // Unwind the object creation.
            for( --i; i >= 0; --i )
            {
                XSF_SAFE_RELEASE( m_SamplerStates[ i ] );
            }

            return hr;
        }

    }

    return S_OK;
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::DestroyStockSamplerStates
// Desc: Destroys the Sampler States for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::DestroyStockSamplerStates()
{
    for( int i = 0; i < (int)StockSamplerStates::SamplerStateCount; ++i )
    {
        XSF_SAFE_RELEASE( m_SamplerStates[ i ] );
    }
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::GenerateStockRasterizerStates
// Desc: Creates the Rasterizer States for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
HRESULT XboxSampleFramework::StockRenderStates::GenerateStockRasterizerStates( _In_ XSF::D3DDevice* pDev )
{
    VERBOSEATGPROFILETHIS;

    // Create the stock states...

    for( int i = 0; i < (int)StockRasterizerStates::RasterizerStateCount; ++i )
    {
#if defined( XSF_USE_DX_11_1 )
        HRESULT hr = pDev->CreateRasterizerState1( &s_StockRasterizerTypes[ i ], &m_RasterizerStates[ i ] );
#else
        HRESULT hr = pDev->CreateRasterizerState( &s_StockRasterizerTypes[ i ], &m_RasterizerStates[ i ] );
#endif
        XSF_ASSERT( SUCCEEDED( hr ) && "Couldn't create state object" );
        if ( FAILED( hr ) )
        {
            // Unwind the object creation.
            for( --i; i >= 0; --i )
            {
                XSF_SAFE_RELEASE( m_RasterizerStates[ i ] );
            }

            return hr;
        }
    }

    return S_OK;
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::DestroyStockRasterizerStates
// Desc: Destroys the Rasterizer States for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::DestroyStockRasterizerStates()
{
    for( int i = 0; i < (int)StockRasterizerStates::RasterizerStateCount; ++i )
    {
        XSF_SAFE_RELEASE( m_RasterizerStates[ i ] );
    }
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::GenerateStockDepthStencilStates
// Desc: Creates the Depth/Stencil States for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
HRESULT XboxSampleFramework::StockRenderStates::GenerateStockDepthStencilStates( _In_ XSF::D3DDevice* pDev )
{
    VERBOSEATGPROFILETHIS;

    // Create the stock states...

    for( int i = 0; i < (int)StockDepthStencilStates::DepthStencilStateCount; ++i )
    {
        HRESULT hr = pDev->CreateDepthStencilState( &s_StockDepthStencilTypes[ i ], &m_DepthStencilStates[ i ] );
        XSF_ASSERT( SUCCEEDED( hr ) && "Couldn't create state object" );
        if ( FAILED( hr ) )
        {
            // Unwind the object creation.
            for( i = i - 1; i >= 0; --i )
            {
                XSF_SAFE_RELEASE( m_DepthStencilStates[ i ] );
            }

            return hr;
        }
    }

    return S_OK;
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::DestroyStockDepthStencilStates
// Desc: Destroys the Depth/Stencil States for the stock state bundle.
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::DestroyStockDepthStencilStates()
{
    for( int i = 0; i < (int)StockDepthStencilStates::DepthStencilStateCount; ++i )
    {
        XSF_SAFE_RELEASE( m_DepthStencilStates[ i ] );
    }
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
HRESULT XboxSampleFramework::StockRenderStates::Initialize( _In_ D3DDevice* pDevice )
{
    ATGPROFILETHIS;

    if ( ms_pInstance == nullptr )
    {
        ms_pInstance = new StockRenderStates;
        bool blendState = false;
        bool samplerState = false;
        bool rasterizerState = false;
        
        // (Note: if we fail within Generate, Generate unwinds for us)

        HRESULT hr = ms_pInstance->GenerateStockBlendStates( pDevice );
        
        if ( FAILED(hr) )
        {
            CleanUpAfterInitialize(  blendState, samplerState, rasterizerState );
            return hr;
        }
        blendState = true;
        
        hr = ms_pInstance->GenerateStockSamplerStates( pDevice );
        
        if ( FAILED(hr) )
        {
            CleanUpAfterInitialize(  blendState, samplerState, rasterizerState );
            return hr;
        }
        samplerState = true;
    
        hr = ms_pInstance->GenerateStockRasterizerStates( pDevice );
        
        if ( FAILED(hr) )
        {
            CleanUpAfterInitialize(  blendState, samplerState, rasterizerState );
            return hr;
        }
        rasterizerState = true;

        hr = ms_pInstance->GenerateStockDepthStencilStates( pDevice );

        if ( SUCCEEDED(hr) )
            return S_OK;

        CleanUpAfterInitialize(  blendState, samplerState, rasterizerState );
        return hr;
    }
    else
    {
        return HRESULT_FROM_WIN32( ERROR_ALREADY_EXISTS );
    }
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CleanUpAfterInitialize
// Desc: Performs clean up by destroying different states
//----------------------------------------------------------------------------------------------------------------------
void XboxSampleFramework::StockRenderStates::CleanUpAfterInitialize( bool blendState, bool samplerState, bool rasterizerState )
{
    if ( rasterizerState )
        ms_pInstance->DestroyStockRasterizerStates();

    if ( samplerState )
        ms_pInstance->DestroyStockSamplerStates();

    if ( blendState )
        ms_pInstance->DestroyStockBlendStates();

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
    if ( ms_pInstance == nullptr )
    {
        // Already shutdown.
        return;
    }

    ms_pInstance->DestroyStockBlendStates();
    ms_pInstance->DestroyStockRasterizerStates();
    ms_pInstance->DestroyStockSamplerStates();
    ms_pInstance->DestroyStockDepthStencilStates();

    StockRenderStates* pTempInstance = ms_pInstance;
    ms_pInstance = nullptr;
    delete pTempInstance;
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CopyBlendStateTemplate
// Desc: Copies a Blend state template to the destination
// Parameters:
//   Destination - The D3D11_BLEND_DESC struct to fill.
//   state       - The state to copy to the destination.
//----------------------------------------------------------------------------------------------------------------------
void XSF::StockRenderStates::CopyBlendStateTemplate( D3D11_BLEND_DESC& Destination, StockBlendStates state ) const
{
    memcpy( &Destination, &s_StockBlendTypes[ (size_t)state ], sizeof( Destination ) );
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CopySamplerStateTemplate
// Desc: Copies a Sampler state template to the destination
// Parameters:
//   Destination - The D3D11_SAMPLER_DESC struct to fill.
//   state       - The state to copy to the destination.
//----------------------------------------------------------------------------------------------------------------------
void XSF::StockRenderStates::CopySamplerStateTemplate( D3D11_SAMPLER_DESC& Destination,
                                                       StockSamplerStates state ) const
{
    memcpy( &Destination, &s_StockSamplerTypes[ (size_t)state ], sizeof( Destination ) );
}


//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CopyRasterizerStateTemplate
// Desc: Copies a Rasterizer state template to the destination
// Parameters:
//   Destination - The D3D11_RASTERIZER_DESC struct to fill.
//   state       - The state to copy to the destination.
//----------------------------------------------------------------------------------------------------------------------
void XSF::StockRenderStates::CopyRasterizerStateTemplate( D3DRasterizerDesc& Destination,
                                                          StockRasterizerStates state ) const
{
    memcpy( &Destination, &s_StockRasterizerTypes[ (size_t)state ], sizeof( Destination ) );
}

//----------------------------------------------------------------------------------------------------------------------
// Name: XboxSampleFramework::StockRenderStates::CopyDepthStencilStateTemplate
// Desc: Copies a Depth/Stencil state template to the destination
// Parameters:
//   Destination - The D3D11_SAMPLER_DESC struct to fill.
//   state       - The state to copy to the destination.
//----------------------------------------------------------------------------------------------------------------------
void XSF::StockRenderStates::CopyDepthStencilStateTemplate( D3D11_DEPTH_STENCIL_DESC& Destination,
                                                            StockDepthStencilStates state ) const
{
    memcpy( &Destination, &s_StockDepthStencilTypes[ (size_t)state ], sizeof( Destination ) );
}