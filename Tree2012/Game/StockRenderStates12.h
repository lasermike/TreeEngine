//----------------------------------------------------------------------------------------------------------------------
// StockRenderStates12.h
// 
// Stock Render States for D3D 12 devices
//
// Advanced Technology Group (ATG)
// Copyright (c) Microsoft Corporation. All rights reserved.
//----------------------------------------------------------------------------------------------------------------------

#pragma once

#ifndef STOCKRENDERSTATES12_H_GUARD
#define STOCKRENDERSTATES12_H_GUARD

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
    // Desc: A bundle of stock/standard heaps used in D3D12 by the framework.
    //------------------------------------------------------------------------------------------------------------------
    class StockRenderStates
    {
        // Single instance of StockRenderStates
        static StockRenderStates* ms_pInstance;

        // Cached state object instances:
        DescriptorHeapWrapper m_SamplerHeap;
        D3D12_STATIC_SAMPLER_DESC m_StaticSampler[StockSamplerStates::SamplerStateCount];
        D3D12_SAMPLER_DESC m_SamplerDesc[StockSamplerStates::SamplerStateCount];
        D3D12_BLEND_DESC m_BlendDesc[StockBlendStates::BlendStateCount];
        D3D12_RASTERIZER_DESC m_RasterizerDesc[StockRasterizerStates::RasterizerStateCount];
        D3D12_DEPTH_STENCIL_DESC m_DepthStencilDesc[StockDepthStencilStates::DepthStencilStateCount];

        // State creation/destruction functionality:
        HRESULT GenerateStockSamplerHeap(_In_ XSF::D3DDevice* const pDev);
        void GenerateStockBlendDesc();
        void GenerateStockRasterizerDesc();
        void GenerateStockDepthStencilDesc();

        void DestroyStockSamplerHeap();

        // Construction/Destruction/Copy (disallow copying by constructor or assignment)
        StockRenderStates(const StockRenderStates&); /*= delete;*/
        StockRenderStates& operator=(const StockRenderStates&); /*= delete;*/
        StockRenderStates() {} /* = default; */
        ~StockRenderStates();

    public:
        //--------------------------------------------------------------------------------------------------------------
        // Functions which allow you to use the stock states as the starting point for your own modifications (e.g.
        // if you need to add stencil functionality to a depth state, you can copy an existing one and modify it if you
        // prefer).
        //--------------------------------------------------------------------------------------------------------------
        
        void CopyBlendTemplate(_Out_ D3D12_BLEND_DESC& Destination, StockBlendStates state) const;
        void CopySamplerTemplate(_Out_ D3D12_SAMPLER_DESC& Destination, StockSamplerStates state) const;
        void CopyRasterizerTemplate(_Out_ D3D12_RASTERIZER_DESC& Destination, StockRasterizerStates state) const;
        void CopyDepthStencilTemplate(_Out_ D3D12_DEPTH_STENCIL_DESC& Destination, StockDepthStencilStates state) const;

        //--------------------------------------------------------------------------------------------------------------
        // Apply State to PSO
        //--------------------------------------------------------------------------------------------------------------
        FORCEINLINE void ApplyBlendState(_In_ XSF::D3DCommandList* const pCmdList, _Inout_ D3D12_GRAPHICS_PIPELINE_STATE_DESC* pPipelineStateDesc, StockBlendStates state, _In_opt_ const FLOAT BlendFactor[4] = nullptr, UINT SampleMask = 0xFFFFFFFF) const;
        FORCEINLINE void ApplyRasterizerState(_Inout_ D3D12_GRAPHICS_PIPELINE_STATE_DESC* pPipelineStateDesc, StockRasterizerStates state) const;
        FORCEINLINE void ApplyDepthStencilState(_In_ XSF::D3DCommandList* const pCmdList, _Inout_ D3D12_GRAPHICS_PIPELINE_STATE_DESC* pPipelineStateDesc, StockDepthStencilStates state, _In_opt_ UINT StencilRef = 0) const;

        //--------------------------------------------------------------------------------------------------------------
        // State Object lookup functions. Note: You do not need to Release these pointers; the StockRenderStates object
        // manages their lifetime. (Addref on an ownership boundary transition only applies to COM interfaces; we're
        // not transferring ownership here).
        //--------------------------------------------------------------------------------------------------------------
        FORCEINLINE ID3D12DescriptorHeap* StockRenderStates::GetSamplerHeap() const;
        FORCEINLINE D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerGPUHandle(StockSamplerStates state) const;
        FORCEINLINE D3D12_CPU_DESCRIPTOR_HANDLE GetSamplerCPUHandle(StockSamplerStates state) const;
        FORCEINLINE void GetStaticSampler(_Out_ D3D12_STATIC_SAMPLER_DESC* staticSampler, StockSamplerStates state, UINT shaderRegister, UINT registerSpace = 0, D3D12_SHADER_VISIBILITY shaderVisibility = D3D12_SHADER_VISIBILITY_ALL) const;
        FORCEINLINE const D3D12_SAMPLER_DESC* GetSamplerDesc(StockSamplerStates state) const;
        FORCEINLINE const D3D12_BLEND_DESC* GetBlendDesc(StockBlendStates state) const;
        FORCEINLINE const D3D12_RASTERIZER_DESC* GetRasterizerDesc(StockRasterizerStates state) const;
        FORCEINLINE const D3D12_DEPTH_STENCIL_DESC* GetDepthStencilDesc(StockDepthStencilStates state) const;

        //--------------------------------------------------------------------------------------------------------------
        // Accessor for the single instance of the StockRenderStates object.
        //--------------------------------------------------------------------------------------------------------------
        FORCEINLINE static const StockRenderStates& GetInstance();

        //--------------------------------------------------------------------------------------------------------------
        // Initialization / Teardown
        //--------------------------------------------------------------------------------------------------------------
        static HRESULT Initialize(_In_ D3DDevice* const pDevice);
        static void CleanUpAfterInitialize();
        static void Shutdown();
    };


    // Inline functions (declared above)
    
    //----------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::ApplyBlendState
    // Desc: Applies a Blend state to the provided Pipeline State Descriptor
    // Parameters:
    //   pCmdList       - The command list to apply the blend factor to
    //   pPipelineStateDesc - The pipeline state descriptor to which the state should be applied.
    //   state          - The stock blend state to use
    //   BlendFactor    - The blend factors to use (default = nullptr)
    //   SampleMask     - The sample mask to use (default = 0xFFFFFFFF)
    //----------------------------------------------------------------------------------------------------------------------
    _Use_decl_annotations_
    void StockRenderStates::ApplyBlendState(XSF::D3DCommandList* const pCmdList, D3D12_GRAPHICS_PIPELINE_STATE_DESC* pPipelineStateDesc, StockBlendStates state,  const FLOAT BlendFactor[4], UINT SampleMask) const
    {
        memcpy(&pPipelineStateDesc->BlendState, GetBlendDesc(state), sizeof(pPipelineStateDesc->BlendState));
        pPipelineStateDesc->SampleMask = SampleMask;
        
        pCmdList->OMSetBlendFactor(BlendFactor);
    }

    
    //----------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::ApplyRasterizerState
    // Desc: Applies a Rasterizer state to the provided Pipeline State Descriptor
    // Parameters:
    //   pPipelineStateDesc - The pipeline state descriptor to which the state should be applied.
    //   state  - The stock rasterizer state to apply.
    //----------------------------------------------------------------------------------------------------------------------
    _Use_decl_annotations_
    FORCEINLINE void StockRenderStates::ApplyRasterizerState(D3D12_GRAPHICS_PIPELINE_STATE_DESC* pPipelineStateDesc, StockRasterizerStates state) const
    {
        memcpy(&pPipelineStateDesc->RasterizerState, GetRasterizerDesc(state), sizeof(pPipelineStateDesc->RasterizerState));
    }

    
    //----------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::ApplyDepthStencilState
    // Desc: Applies a depth/stencil state to the provided Pipeline State Descriptor
    // Parameters:
    //   pPipelineStateDesc - The pipeline state descriptor to which the state should be applied.
    //   pCmdList   - The command list to apply the stencil ref to
    //   state      - The stock depth-stencil state to apply
    //   StencilRef - The stencil ref parameter (defaults to 0)
    //----------------------------------------------------------------------------------------------------------------------
    _Use_decl_annotations_
    FORCEINLINE void StockRenderStates::ApplyDepthStencilState(XSF::D3DCommandList* const pCmdList, D3D12_GRAPHICS_PIPELINE_STATE_DESC* pPipelineStateDesc, StockDepthStencilStates state, UINT StencilRef) const
    {
        memcpy(&pPipelineStateDesc->DepthStencilState, GetDepthStencilDesc(state),sizeof(pPipelineStateDesc->DepthStencilState));
        pCmdList->OMSetStencilRef(StencilRef);
    }

    
    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetStaticSampler
    // Desc: Obtains the static sampler for the requested StockSamplerState.
    //------------------------------------------------------------------------------------------------------------------
    _Use_decl_annotations_
    FORCEINLINE void StockRenderStates::GetStaticSampler(D3D12_STATIC_SAMPLER_DESC* staticSampler, StockSamplerStates state, UINT shaderRegister, UINT registerSpace, D3D12_SHADER_VISIBILITY shaderVisibility) const
    {
        XSF_ASSERT(staticSampler != nullptr);
        XSF_ASSERT(state != StockSamplerStates::SamplerStateCount);

        *staticSampler = m_StaticSampler[static_cast<UINT>(state)];
        staticSampler->ShaderRegister = shaderRegister;
        staticSampler->RegisterSpace = registerSpace;
        staticSampler->ShaderVisibility = shaderVisibility;
    }

    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetSamplerDesc
    // Desc: Obtains the Sampler State descriptor for the requested StockSamplerState.
    //------------------------------------------------------------------------------------------------------------------
    FORCEINLINE const D3D12_SAMPLER_DESC* StockRenderStates::GetSamplerDesc(StockSamplerStates state) const
    {
        XSF_ASSERT(state != StockSamplerStates::SamplerStateCount);
        return &m_SamplerDesc[static_cast<UINT>(state)];
    }


    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetBlendDesc
    // Desc: Obtains the Blend State descriptor for the requested StockBlendState.
    //------------------------------------------------------------------------------------------------------------------
    FORCEINLINE const D3D12_BLEND_DESC* StockRenderStates::GetBlendDesc( StockBlendStates state ) const
    {
        XSF_ASSERT(state != StockBlendStates::BlendStateCount);
        return &m_BlendDesc[static_cast<UINT>(state)];
    }

    
    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetSamplerGPU/CPUHandle
    // Desc: Obtains the Render State object for the requested StockRenderState
    // 
    // NOTE: There is no equivalent ApplySamplerState method for Sampler States; they're typically used in ways where
    //       such an operation by itself would not be useful.
    //------------------------------------------------------------------------------------------------------------------
    FORCEINLINE ID3D12DescriptorHeap* StockRenderStates::GetSamplerHeap() const
    {
        return m_SamplerHeap;
    }

    FORCEINLINE D3D12_GPU_DESCRIPTOR_HANDLE StockRenderStates::GetSamplerGPUHandle(StockSamplerStates state) const
    {
        XSF_ASSERT(state != StockSamplerStates::SamplerStateCount);
        return m_SamplerHeap.hGPU(static_cast<UINT>(state));
    }

    FORCEINLINE D3D12_CPU_DESCRIPTOR_HANDLE StockRenderStates::GetSamplerCPUHandle(StockSamplerStates state) const
    {
        XSF_ASSERT(state != StockSamplerStates::SamplerStateCount);
        return m_SamplerHeap.hCPU(static_cast<UINT>(state));
    }
    

    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetRasterizerDesc
    // Desc: Obtains the Rasterizer State descriptor for the requested StockRasterizerState
    //------------------------------------------------------------------------------------------------------------------
    FORCEINLINE const D3D12_RASTERIZER_DESC* StockRenderStates::GetRasterizerDesc(StockRasterizerStates state) const
    {
        XSF_ASSERT(state != StockRasterizerStates::RasterizerStateCount);
        return &m_RasterizerDesc[static_cast<UINT>(state)];
    }

    
    //------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetDepthStencilDesc
    // Desc: Obtains the Depth/Stencil State descriptor for the requested StockDepthStencilState.
    //------------------------------------------------------------------------------------------------------------------
    FORCEINLINE const D3D12_DEPTH_STENCIL_DESC* StockRenderStates::GetDepthStencilDesc(StockDepthStencilStates state) const
    {
        XSF_ASSERT(state != StockDepthStencilStates::DepthStencilStateCount);
        return &m_DepthStencilDesc[static_cast<UINT>(state)];
    }

    
    //----------------------------------------------------------------------------------------------------------------------
    // Name: XboxSampleFramework::StockRenderStates::GetInstance
    // Returns: A reference to the StockRenderStates object.
    //----------------------------------------------------------------------------------------------------------------------
    FORCEINLINE const StockRenderStates& StockRenderStates::GetInstance()
    {
        XSF_ASSERT(ms_pInstance != nullptr && "Not yet initialized");
        return *ms_pInstance;
    }

}


#endif //STOCKRENDERSTATES12_H_GUARD
