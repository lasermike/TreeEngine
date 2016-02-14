//----------------------------------------------------------------------------------------------------------------------
// StockRenderStates.h
// 
// Stock Render States for D3D 11 devices
//
// Advanced Technology Group (ATG)
// Copyright (c) Microsoft Corporation. All rights reserved.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#if defined(TREE3D12)
#include <StockRenderStates12.h>
#else 

#ifndef STOCKRENDERSTATES_H_GUARD
#define STOCKRENDERSTATES_H_GUARD

namespace XSF = XboxSampleFramework;

namespace XboxSampleFramework
{
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

    //------------------------------------------------------------------------------------------------------------------
    // Name: class StockRenderStates
    // Desc: A bundle of stock/standard states used in D3D11 by the framework.
    //------------------------------------------------------------------------------------------------------------------
    class StockRenderStates
    {
        // Single instance of StockRenderStates
        static StockRenderStates* ms_pInstance;

        // Cached state object instances:
        D3DRasterizerState* m_RasterizerStates[ StockRasterizerStates::RasterizerStateCount ];
        ID3D11BlendState* m_BlendStates[ StockBlendStates::BlendStateCount ];
        ID3D11SamplerState* m_SamplerStates[ StockSamplerStates::SamplerStateCount ];
        ID3D11DepthStencilState* m_DepthStencilStates[ StockDepthStencilStates::DepthStencilStateCount ];

        // State creation/destruction functionality:
        HRESULT GenerateStockBlendStates( _In_ XSF::D3DDevice* pDev );
        HRESULT GenerateStockSamplerStates( _In_ XSF::D3DDevice* pDev );
        HRESULT GenerateStockRasterizerStates( _In_ XSF::D3DDevice* pDev );
        HRESULT GenerateStockDepthStencilStates( _In_ XSF::D3DDevice* pDev );

        void DestroyStockBlendStates();
        void DestroyStockSamplerStates();
        void DestroyStockRasterizerStates();
        void DestroyStockDepthStencilStates();

        // Construction/Destruction/Copy (disallow copying by constructor or assignment)
        StockRenderStates( const StockRenderStates& ); /*= delete;*/
        StockRenderStates& operator=( const StockRenderStates& ); /*= delete;*/
        StockRenderStates() {} /* = default; */
        ~StockRenderStates();

    public:
        //--------------------------------------------------------------------------------------------------------------
        // Functions which allow you to use the stock states as the starting point for your own modifications (e.g.
        // if you need to add stencil functionality to a depth state, you can copy an existing one and modify it if you
        // prefer).
        //--------------------------------------------------------------------------------------------------------------
        
        void CopyBlendStateTemplate( D3D11_BLEND_DESC& Destination, StockBlendStates state ) const;
        void CopySamplerStateTemplate( D3D11_SAMPLER_DESC& Destination, StockSamplerStates state ) const;
        void CopyRasterizerStateTemplate( D3DRasterizerDesc& Destination, StockRasterizerStates state ) const;
        void CopyDepthStencilStateTemplate( D3D11_DEPTH_STENCIL_DESC& Destination,
                                            StockDepthStencilStates state ) const;

        //--------------------------------------------------------------------------------------------------------------
        // Apply State to Device Context functions
        //--------------------------------------------------------------------------------------------------------------
        FORCEINLINE void ApplyBlendState( _In_ D3DDeviceContext* pCtx, StockBlendStates state, 
                                          _In_opt_ const FLOAT BlendFactor[4] = nullptr,
                                          UINT SampleMask = 0xFFFFFFFF ) const;
        FORCEINLINE void ApplyRasterizerState( _In_ D3DDeviceContext* pCtx, StockRasterizerStates state ) const;
        FORCEINLINE void ApplyDepthStencilState( _In_ D3DDeviceContext* pCtx, StockDepthStencilStates state,
                                                 UINT StencilRef = 0 ) const;

        //--------------------------------------------------------------------------------------------------------------
        // State Object lookup functions. Note: You do not need to Release these pointers; the StockRenderStates object
        // manages their lifetime. (Addref on an ownership boundary transition only applies to COM interfaces; we're
        // not transferring ownership here).
        //--------------------------------------------------------------------------------------------------------------
        FORCEINLINE ID3D11BlendState* GetBlendState( StockBlendStates state ) const;
        FORCEINLINE ID3D11SamplerState* GetSamplerState( StockSamplerStates state ) const;
        FORCEINLINE D3DRasterizerState* GetRasterizerState( StockRasterizerStates state ) const;
        FORCEINLINE ID3D11DepthStencilState* GetDepthStencilState( StockDepthStencilStates state ) const;

        //--------------------------------------------------------------------------------------------------------------
        // Accessor for the single instance of the StockRenderStates object.
        //--------------------------------------------------------------------------------------------------------------
        FORCEINLINE static const StockRenderStates& GetStates();

        //--------------------------------------------------------------------------------------------------------------
        // Initialization / Teardown
        //--------------------------------------------------------------------------------------------------------------
        static HRESULT Initialize( _In_ D3DDevice* pDevice );
        static void CleanUpAfterInitialize( bool blendState, bool samplerState, bool rasterizerState );
        static void Shutdown();
    };


    // Inline functions (declared above)
    
    //----------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::ApplyBlendState
    // Desc: Applies a Blend state to the provided Device Context.
    // Parameters:
    //   pCtx           - The device context to which the state should be applied.
    //   state          - The stock blend state to use
    //   BlendFactor    - The blend factors to use (default = nullptr)
    //   SampleMask     - The sample mask to use (default = 0xFFFFFFFF)
    //----------------------------------------------------------------------------------------------------------------------
    void StockRenderStates::ApplyBlendState( _In_ D3DDeviceContext* pCtx, StockBlendStates state, 
        _In_opt_ const FLOAT BlendFactor[4] /*= nullptr*/, UINT SampleMask /*= 0xFFFFFFFF*/ ) const
    {
        VERBOSEATGPROFILETHIS;

        pCtx->OMSetBlendState( m_BlendStates[ (UINT)state ], BlendFactor, SampleMask );
    }

    
    //----------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::ApplyRasterizerState
    // Desc: Applies a Rasterizer state to the provided Device Context.
    // Parameters:
    //   pCtx   - The Device Context to apply the rasterizer state to.
    //   state  - The stock rasterizer state to apply.
    //----------------------------------------------------------------------------------------------------------------------
    FORCEINLINE void StockRenderStates::ApplyRasterizerState( _In_ D3DDeviceContext* pCtx,
                                                              StockRasterizerStates state ) const
    {
        VERBOSEATGPROFILETHIS;

        pCtx->RSSetState( m_RasterizerStates[ (UINT)state ] );
    }

    
    //----------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::ApplyDepthStencilState
    // Desc: Applies a depth/stencil state to the provided Device Context
    // Parameters:
    //   pCtx       - The device context to which the state should be applied.
    //   state      - The stock depth-stencil state to apply
    //   StencilRef - The stencil ref parameter (defaults to 0)
    //----------------------------------------------------------------------------------------------------------------------
    FORCEINLINE void StockRenderStates::ApplyDepthStencilState( _In_ D3DDeviceContext* pCtx,
                                                                StockDepthStencilStates state,
                                                                UINT StencilRef /*= 0*/ ) const
    {
        VERBOSEATGPROFILETHIS;

        pCtx->OMSetDepthStencilState( m_DepthStencilStates[ (UINT)state ], StencilRef );
    }

    
    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetBlendState
    // Desc: Obtains the Blend State object for the requested StockBlendState.
    // 
    // NOTE: Do NOT call Release on this pointer; the StockRenderStates object manages its lifetime, and it lives for
    //       the entire process lifetime.
    //------------------------------------------------------------------------------------------------------------------
    FORCEINLINE ID3D11BlendState* StockRenderStates::GetBlendState( StockBlendStates state ) const
    {
        return m_BlendStates[ (UINT)state ];
    }

    
    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetSamplerState
    // Desc: Obtains the Render State object for the requested StockRenderState
    // 
    // NOTE: There is no equivalent ApplySamplerState method for Sampler States; they're typically used in ways where
    //       such an operation by itself would not be useful.
    //
    // NOTE: Do NOT call Release on this pointer; the StockRenderStates object manages its lifetime, and it lives for
    //       the entire process lifetime.
    //------------------------------------------------------------------------------------------------------------------
    FORCEINLINE ID3D11SamplerState* StockRenderStates::GetSamplerState( StockSamplerStates state ) const
    {
        return m_SamplerStates[ (UINT) state ];
    }
    

    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetRasterizerState
    // Desc: Obtains the Rasterizer State object for the requested StockRasterizerState
    //
    // NOTE: Do NOT call Release on this pointer; the StockRenderStates object manages its lifetime, and it lives for
    //       the entire process lifetime.
    //------------------------------------------------------------------------------------------------------------------
    FORCEINLINE D3DRasterizerState* StockRenderStates::GetRasterizerState( StockRasterizerStates state ) const
    {
        return m_RasterizerStates[ (UINT)state ];
    }

    
    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetDepthStencilState
    // Desc: Obtains the Depth/Stencil State object for the requested StockDepthStencilState.
    //
    // NOTE: Do NOT call Release on this pointer; the StockRenderStates object manages its lifetime, and it lives for
    //       the entire process lifetime.
    //------------------------------------------------------------------------------------------------------------------
    FORCEINLINE ID3D11DepthStencilState* StockRenderStates::GetDepthStencilState( StockDepthStencilStates state ) const
    {
        return m_DepthStencilStates[ (UINT)state ];
    }

    
    //----------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::GetStockRenderStates
    // Returns: A reference to the StockRenderState object.
    //----------------------------------------------------------------------------------------------------------------------
    FORCEINLINE const StockRenderStates& StockRenderStates::GetStates()
    {
        XSF_ASSERT( ms_pInstance != nullptr && "Not yet initialized" );
        return *ms_pInstance;
    }

}


#endif //STOCKRENDERSTATES_H_GUARD

#endif //dx12