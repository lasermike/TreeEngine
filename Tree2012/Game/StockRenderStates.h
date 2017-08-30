//----------------------------------------------------------------------------------------------------------------------
// StockRenderStates.h
// 
// Stock Render States for D3D 11 devices
//
// Advanced Technology Group (ATG)
// Copyright (c) Microsoft Corporation. All rights reserved.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

//------------------------------------------------------------------------------------------------------------------
// Name: enum class StockBlendStates
// Desc: A list of common Blend States used by the sample framework.
//------------------------------------------------------------------------------------------------------------------
enum class StockBlendStates : unsigned int
{
    // The default state - you can use NULL instead. Overwrites Dest with
    // Src values.
    Overwrite,

    // Alpha blends the Src with the Dest
    AlphaBlend,

    // Alpha blends the Src with the Dest. The Src is Premultiplied by its alpha values.
    PremultipliedAlphaBlend,

    // Additive blend where the Src alpha is mulitplied with the Src color, but
    // the Dest alpha is kept as the alpha for the final pixel.
    AdditiveBlendUseSrcAlphaKeepDestAlpha,

    // Additive blend where the Src alpha is ignored, and the Dest alpha is kept
    // as the alpha for the final pixel. Equivalent to AdditiveBlendUseSrcAlphaKeepDestAlpha, but
    // intended for Src data with premultiplied alpha.
    AdditiveBlendIgnoreSrcAlphaKeepDestAlpha,

    // The total number of stock blend states.
    BlendStateCount
};

//------------------------------------------------------------------------------------------------------------------
// Name: StockSamplerStates 
// Desc: A set of stock sampler states commonly used by titles.
//------------------------------------------------------------------------------------------------------------------
enum class StockSamplerStates : unsigned int
{
    // Sampler state with MIN/MAG and MIP filter = POINT, UVW = ADDRESS_CLAMP, comparison = ALWAYS, MaxLOD = no limit
    MinMagMipPointUVWClamp,

    // Sampler state with MIN/MAG filter = LINEAR, MIP filter = POINT, UVW = ADDRESS_CLAMP, comparison = ALWAYS,
    // MaxLOD = no limit
    MinMagLinearMipPointUVWClamp,

    // Sampler state with MIN/MAG/MIP filter = LINEAR, UVW = ADDRESS_CLAMP, comparison = ALWAYS, MaxLOD = no limit
    MinMagMipLinearUVWClamp,

    // Sampler state with MIN/MAG/MIP filter = LINEAR, UVW = ADDRESS_WRAP, comparison = ALWAYS, MaxLOD = no limit
    MinMagMipLinearUVWWrap,

    // Sampler state with MIN/MAG filter = bilinear, MIP filter = POINT, UVW = ADDRESS_WRAP, comparison = ALWAYS, 
    // MaxLOD = No Limit.
    MinMagLinearMipPointUVWWrap,

    UseShadowMap,

    // The total number of stock sampler states.
    SamplerStateCount
};

//------------------------------------------------------------------------------------------------------------------
// Name: StockRasterizerStates
// Desc: A set of commonly used Rasterizer states
//------------------------------------------------------------------------------------------------------------------
enum class StockRasterizerStates : unsigned int
{
    // Solid rasterizer with:
    // - back-face culling
    // - clockwise winding is front facing
    // - depth clip is enabled, no depth bias, no scissoring, no line antialias/MSAA

    Solid,

    // Same as Solid rasterizer but with counter-clockwise winding = front facing
    SolidCCW,

    // Same as Solid rasterizer, but culls front-facing tris
    SolidCullFront,

    // Same as Solid rasterizer, but counter-clockwise winding, and culls front-facing tris.
    SolidCullFrontCCW,

    // Wireframe rasterizer with 
    // - back face culling
    // - clockwise winding is front-facing
    // - depth clip is enabled, no depth bias, no scissoring, no line antialias/MSAA
    Wireframe,

    // Same as Wireframe rasterizer, but with counter-clockwise winding = front-facing
    WireframeCCW,

    // Same as Wireframe rasterizer but with no back-face culling (implies winding order agnostic)
    WireframeNoCulling,

    BuildShadowMap,

    // The total number of stock rasterizer states
    RasterizerStateCount
};

//------------------------------------------------------------------------------------------------------------------
// Name: StockDepthStencilStates
// Desc: A set of commonly used Depth/Stencil states
//------------------------------------------------------------------------------------------------------------------
enum class StockDepthStencilStates : unsigned int
{
    // No depth test, no stencil test, updates Z buffer
    AlwaysSucceedWriteZNoStencil,

    // No depth test, no stencil test, doesn't modify Z-buffer
    AlwaysSucceedNoZWriteNoStencil,

    // Pass the depth test if distance is < value in the depth buffer. No stencil test is performed.
    // Updates Z buffer.
    DepthLessThanWriteZNoStencil,

    // Pass the depth test if distance is < value in the depth buffer. No stencil test is performed.
    // Z buffer isn't updated.
    DepthLessThanNoZWriteNoStencil,

    // Pass the depth test if distance is <= value in the depth buffer. No stencil test is performed.
    // Updates Z buffer.
    DepthMustBeLessThanOrEqualZWriteNoStencil,

    // Pass the depth test if distance is <= value in the depth buffer. No stencil test is performed.
    // Z Buffer isn't updated.
    DepthMustBeLessThanOrEqualNoZWriteNoStencil,

    // The total number of stock depth-stencil states
    DepthStencilStateCount
};

struct StockRenderState
{
    StockBlendStates blendState : 8;
    StockSamplerStates samplerState : 8;
    StockRasterizerStates rasterizerState : 8;
    StockDepthStencilStates depthStencilState : 8;

    StockRenderState() : blendState(StockBlendStates::Overwrite), 
                         samplerState(StockSamplerStates::MinMagLinearMipPointUVWClamp),
                         rasterizerState(StockRasterizerStates::Solid),
                         depthStencilState(StockDepthStencilStates::DepthLessThanWriteZNoStencil) 
    {}
};
