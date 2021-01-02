#pragma once

#include <unordered_map>
#include <vector>
#include <list>
#include <queue>


namespace XboxSampleFramework
{
    class CpuGpuHeap;
	class DescriptorHeapWrapper;

#ifdef _XBOX_ONE
    static const UINT c_nodeMask = D3D12XBOX_NODE_MASK;
#else
    static const UINT c_nodeMask = 1;
#endif

    //--------------------------------------------------------------------------------------
    // Helper methods
    //--------------------------------------------------------------------------------------
    void PrepareBackBufferForRendering(_In_ D3DCommandList* const pCmdList, _In_ ID3D12Resource* const pBackBufferTexture);
    void PrepareBackBufferForPresent(_In_ D3DCommandList* const pCmdList, _In_ ID3D12Resource* const pBackBufferTexture);
    void PrepareDepthStencilBufferForRendering(_In_ D3DCommandList* const pCmdList, _In_ ID3D12Resource* const pDepthStencilTexture);
    void ResourceBarrier(_In_ D3DCommandList* const pCmdList, _In_ ID3D12Resource* const pResource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After, _In_opt_ D3D12_RESOURCE_BARRIER_FLAGS Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE);

    HRESULT CreateBuffer( _In_ D3DDevice* pDevice, _In_ D3DCommandList* pCmdList, _In_ CpuGpuHeap* pUploadHeap, _In_ const D3D12_RESOURCE_DESC& desc, _In_opt_ const BYTE* pInitialData, _COM_Outptr_ ID3D12Resource **ppBuffer );
    HRESULT CreateVertexBuffer( _In_ D3DDevice* pDevice, _In_ D3DCommandList* pCmdList, _In_ CpuGpuHeap* pUploadHeap, _In_ const D3D12_RESOURCE_DESC& desc, _In_ UINT strideInBytes, _In_opt_ const BYTE* pInitialData, _COM_Outptr_ ID3D12Resource **ppBuffer, _Outptr_ D3D12_VERTEX_BUFFER_VIEW* pVBView = nullptr );
    HRESULT CreateIndexBuffer( _In_ D3DDevice* pDevice, _In_ D3DCommandList* pCmdList, _In_ CpuGpuHeap* pUploadHeap, _In_ const D3D12_RESOURCE_DESC& desc, _In_ DXGI_FORMAT format, _In_opt_ const BYTE* pInitialData, _COM_Outptr_ ID3D12Resource **ppBuffer, _Outptr_ D3D12_INDEX_BUFFER_VIEW* pIBView = nullptr );
    HRESULT CreateResource( _In_ D3DDevice* pDevice, _In_ D3DCommandList* pCmdList, _In_ D3D12_RESOURCE_DESC* pDescTex, _COM_Outptr_ ID3D12Resource** ppTex, _In_opt_ CpuGpuHeap* pUploadHeap = nullptr, _In_opt_ const BYTE* pInitialData = nullptr, D3D12_RESOURCE_STATES usage = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
    HRESULT UpdateConstantBuffer( _In_ D3DDevice* pDevice, SIZE_T cbSize, _In_ XSF::CpuGpuHeap* pUploadHeap, _In_ D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle, _In_ const void* pData );

    bool IsBlockCompressedFormat(DXGI_FORMAT Format);
    UINT32 BitsPerPixel( _In_ DXGI_FORMAT fmt );
    void GetSurfaceInfo( _In_ size_t width, _In_ size_t height, _In_ DXGI_FORMAT fmt, _Out_opt_ size_t* outNumBytes, _Out_opt_ size_t* outRowBytes, _Out_opt_ size_t* outNumRows );
 
    //--------------------------------------------------------------------------------------
    // Maintains a cache of pipeline state objects.
    // At app startup time, initialize it with a partially complete PSO desc, then at runtime, fill in the remainder of the desc and create a PSO if one has not already been created for that combination.
    // Useful for porting D3D11 utility code that does something like rendering to the active rendertarget without knowing ahead of time the attributes of the render target
    //--------------------------------------------------------------------------------------
    class PSOCache
    {
    private:
        class PSODescHashFunction
        {
        public:
            std::size_t operator() (const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODesc) const;
        };

        class PSODescEqualFunction
        {
        public:
            bool operator() (const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODescA, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODescB) const;
        };

        D3DDevice* m_pDevice;
        D3D12_GRAPHICS_PIPELINE_STATE_DESC m_PSOTemplate;
        std::unordered_map< D3D12_GRAPHICS_PIPELINE_STATE_DESC, ID3D12PipelineState*, PSODescHashFunction, PSODescEqualFunction > m_Map;
        std::list<void*> m_Allocations;

    public:
        PSOCache() {}
        ~PSOCache()
        {
            Terminate();
        }

        void* DuplicateMemory(const void* pSrc, size_t SizeBytes);

        void Initialize(_In_ D3DDevice* const pDevice, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSOTemplate);
        void Terminate();

        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& GetPSOTemplate() const { return m_PSOTemplate; }
        ID3D12PipelineState* FindPSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODesc) const;
        ID3D12PipelineState* FindOrCreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& PSODesc);
    };

    //--------------------------------------------------------------------------------------
    // Interface to cycle through subsequent indices
    //--------------------------------------------------------------------------------------
    class RoundRobinIndexer
    {
        UINT m_startIndex;
        UINT m_availableIndex;
        UINT m_numIndices;
    public:
        void Initialize( UINT startIndex, UINT numIndices )
        {
			m_startIndex = startIndex;
            m_availableIndex = startIndex;
            m_numIndices = numIndices;
        }
        
        UINT GetNextAvailable()
        {
            UINT available = m_availableIndex;
            m_availableIndex = m_startIndex + ((( m_availableIndex + 1 ) - m_startIndex ) % m_numIndices );
            return available;
        }
    };

    //--------------------------------------------------------------------------------------
    // Rename buffer implementation for dynamic VB, IB, CB emulation
    //--------------------------------------------------------------------------------------
    class D3D12DynamicBuffer
    {
    private:
        struct RenameBuffer
        {
            RenameBuffer() :
                m_pBuffer(nullptr),
                m_writeFence(0)
            {}

            ID3D12Resource* m_pBuffer;
            UINT64 m_writeFence;
        };
        std::vector<RenameBuffer> m_RenameBuffers;
        UINT32 m_CurrentBufferIndex;
        UINT32 m_ByteWidth;
        UINT32 m_VertexStrideBytes;
        D3DDevice* m_pDevice;
        ID3D12Fence *m_pFence;

    public:
        D3D12DynamicBuffer()
            : m_CurrentBufferIndex(0),
            m_pDevice(nullptr),
            m_pFence(nullptr)
        {}
        ~D3D12DynamicBuffer()
        {
            Terminate();
        }

        HRESULT Create(_In_ D3DDevice* const pDevice, _In_ ID3D12Fence* const pFence, UINT32 ByteWidth, UINT32 VertexStrideBytes, UINT32 InitialRenameCount);
        void Terminate();

        HRESULT MapDiscard(_In_ D3DCommandQueue* const pCmdQueue, _Out_ void** ppMapData);
        HRESULT MapNoOverwrite(_Out_ void** ppMapData);
        void Unmap(_In_ D3DCommandQueue* const pCmdQueue);

        UINT32 GetRenameBufferCount() const { return static_cast<UINT32>(m_RenameBuffers.size()); }
        ID3D12Resource* GetBufferByIndex(UINT32 Index) const { return m_RenameBuffers[Index].m_pBuffer; }

        UINT32 GetCurrentRenameBufferIndex() const { return m_CurrentBufferIndex; }
        ID3D12Resource* GetBuffer() const { return m_RenameBuffers[m_CurrentBufferIndex].m_pBuffer; }

        void CreateCBView(D3D12_CPU_DESCRIPTOR_HANDLE DestHandle);

        void GetCBDesc(_Out_ D3D12_CONSTANT_BUFFER_VIEW_DESC* pCBDesc) const;
        void GetVBDesc(_Out_ D3D12_VERTEX_BUFFER_VIEW* pVBDesc) const;
        void GetIBDesc(_Out_ D3D12_INDEX_BUFFER_VIEW* pIBDesc, DXGI_FORMAT IndexFormat) const;

    private:
        UINT32 FindOrCreateRenameBuffer(_In_ D3DCommandQueue* const pCmdQueue, bool ForceCreate);
    };

    
    //--------------------------------------------------------------------------------------
    // Thin wrapper around a descriptor heap with some handy convenience methods for handle access
    //--------------------------------------------------------------------------------------
    class DescriptorHeapWrapper
    {
    public:
        DescriptorHeapWrapper(_In_ ID3D12DescriptorHeap* const pDH = nullptr)
            : m_pDH(nullptr)
        {
            if (pDH != nullptr)
            {
                InitializeFromExistingHeap(pDH);
            }
        }
        ~DescriptorHeapWrapper()
        {
            Terminate();
        }

        HRESULT InitializeFromExistingHeap(_In_ ID3D12DescriptorHeap* const pDH)
        {
            XSF_SAFE_RELEASE(m_pDH);
            m_pDH = pDH;
            m_pDH->AddRef();

			CComPtr<ID3D12Device> spDevice;
            //D3DDevicePtr spDevice;
            pDH->GetDevice(IID_GRAPHICS_PPV_ARGS(&spDevice));
            m_Desc = pDH->GetDesc();
            m_HandleIncrementSize = spDevice->GetDescriptorHandleIncrementSize(m_Desc.Type);

            m_hCPUHeapStart = pDH->GetCPUDescriptorHandleForHeapStart();
            if (m_Desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
            {
                m_hGPUHeapStart = pDH->GetGPUDescriptorHandleForHeapStart();
            }
            else
            {
                m_hGPUHeapStart.ptr = 0;
            }

            return S_OK;
        }

        HRESULT Initialize(
            _In_ D3DDevice* const pDevice,
            D3D12_DESCRIPTOR_HEAP_TYPE Type,
            UINT NumDescriptors,
            bool bShaderVisible = false)
        {
            ZeroMemory(&m_Desc, sizeof(m_Desc));
            m_Desc.Type = Type;
            m_Desc.NumDescriptors = NumDescriptors;
            m_Desc.Flags = bShaderVisible? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

            XSF_ERROR_IF_FAILED(pDevice->CreateDescriptorHeap(&m_Desc, IID_GRAPHICS_PPV_ARGS(&m_pDH)));

            m_hCPUHeapStart = m_pDH->GetCPUDescriptorHandleForHeapStart();
            if (bShaderVisible)
            {
                m_hGPUHeapStart = m_pDH->GetGPUDescriptorHandleForHeapStart();
            }
            else
            {
                m_hGPUHeapStart.ptr = 0;
            }
            m_HandleIncrementSize = pDevice->GetDescriptorHandleIncrementSize(m_Desc.Type);

            return S_OK;
        }

        void Terminate()
        {
            XSF_SAFE_RELEASE(m_pDH);
        }

        operator ID3D12DescriptorHeap*() { return m_pDH; }
        operator ID3D12DescriptorHeap*() const { return m_pDH; }
        operator ID3D12DescriptorHeap* const* () const { return &m_pDH; }

        D3D12_CPU_DESCRIPTOR_HANDLE hCPU(UINT index) const
        {
            return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_hCPUHeapStart, index, m_HandleIncrementSize);
        }
        D3D12_GPU_DESCRIPTOR_HANDLE hGPU(UINT index) const
        {
            XSF_ASSERT(m_Desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
            return CD3DX12_GPU_DESCRIPTOR_HANDLE(m_hGPUHeapStart, index, m_HandleIncrementSize);
        }

        UINT32 GetIncrementSize() const { return m_HandleIncrementSize; }

    private:
        D3D12_DESCRIPTOR_HEAP_DESC m_Desc;
        ID3D12DescriptorHeap* m_pDH;
        CD3DX12_CPU_DESCRIPTOR_HANDLE m_hCPUHeapStart;
        CD3DX12_GPU_DESCRIPTOR_HANDLE m_hGPUHeapStart;
        UINT32 m_HandleIncrementSize;
    };


    //--------------------------------------------------------------------------------------
    // Maintains a set of descriptor heap ranges that are fence tracked
    // Useful for managing and scheduling the descriptor heap location of dynamic VB/IB/CB buffer descriptors.
    // Can include static descriptors as well, so the system manages fence-tracked sets of static & dynamic descriptors
    //--------------------------------------------------------------------------------------
    class DescriptorHeapSetManager
    {
    private:
        D3DDevice* m_pDevice;
        ID3D12Fence* m_pFence;

        struct HandleSet
        {
            UINT64 Fence;
            UINT32 HeapOffset;
        };
        std::vector<HandleSet> m_Sets;
        UINT32 m_LastSetIndex;

        ID3D12DescriptorHeap* m_pHeap;
        D3D12_DESCRIPTOR_HEAP_DESC m_HeapDesc;
        UINT32 m_HandleSize;
        UINT32 m_BaseHeapOffset;
        UINT32 m_HeapStride;

        struct ManagedBuffer
        {
            enum Usage
            {
                Undefined,
                Constant,
                Vertex,
                Index
            };
            void* m_pResource;
            bool  m_dynamicBuffer;
            Usage m_usage;
            union
            {
                DXGI_FORMAT IndexFormat;
                UINT32 OriginalHeapIndex;
            };
        };
        std::vector<ManagedBuffer> m_Buffers;

    public:
        DescriptorHeapSetManager() :
            m_pDevice(nullptr),
            m_pFence(nullptr),
            m_pHeap(nullptr)
        {}
        ~DescriptorHeapSetManager()
        {
            Terminate();
        }

        HRESULT Initialize(_In_ D3DDevice* const pDevice, _In_ ID3D12Fence* const pFence, _In_ ID3D12DescriptorHeap* const pHeap, UINT32 HeapOffset, UINT32 HeapStride, UINT32 MaxHandleCount);
        void Terminate();

        bool AddDynamicCB(_In_ D3D12DynamicBuffer* const pBuffer);
        bool AddDynamicIB(_In_ D3D12DynamicBuffer* const pBuffer, DXGI_FORMAT IndexFormat);
        bool AddDynamicVB(_In_ D3D12DynamicBuffer* const pBuffer);
        bool AddEmptySlot();
        bool AddStaticDescriptor(_Out_ D3D12_CPU_DESCRIPTOR_HANDLE* pHandle);
        bool AddStaticSRV(_In_ ID3D12Resource* const pResource, D3D12_SHADER_RESOURCE_VIEW_DESC SRVDesc);

        void FinalizeDescriptorTable(_Out_opt_ D3D12_GPU_DESCRIPTOR_HANDLE* pGpuHandle, _Out_opt_ D3D12_CPU_DESCRIPTOR_HANDLE* pCpuHandle);
        void FinalizeIndexBuffer(_Out_ D3D12_INDEX_BUFFER_VIEW* pIBView);
        void FinalizeVertexBuffers(_Out_ UINT* pNumViews, _Out_writes_(maxNumViews) D3D12_VERTEX_BUFFER_VIEW* pVBView, _In_ UINT maxNumViews);
        void SetGraphicsRootDescriptorTable(_In_ D3DCommandList* const pCmdList, UINT32 RootParamIndex)
        {
            XSF_ASSERT(m_HeapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
            D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle;
            FinalizeDescriptorTable(&GpuHandle, nullptr);
            pCmdList->SetGraphicsRootDescriptorTable(RootParamIndex, GpuHandle);
        }
        void SetComputeRootDescriptorTable(_In_ D3DCommandList* const pCmdList, UINT32 RootParamIndex)
        {
            XSF_ASSERT(m_HeapDesc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
            D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle;
            FinalizeDescriptorTable(&GpuHandle, nullptr);
            pCmdList->SetComputeRootDescriptorTable(RootParamIndex, GpuHandle);
        }
        void SetIndexBuffer(_In_ D3DCommandList* const pCmdList)
        {
            D3D12_INDEX_BUFFER_VIEW IBView;
            FinalizeIndexBuffer(&IBView);
            pCmdList->IASetIndexBuffer(&IBView);
        }
        void SetVertexBuffers(_In_ D3DCommandList* const pCmdList, UINT32 StartSlot)
        {
            D3D12_VERTEX_BUFFER_VIEW VBView;
            UINT numViews;
            FinalizeVertexBuffers(&numViews, &VBView, 1);
            pCmdList->IASetVertexBuffers(StartSlot, numViews, &VBView);
        }

    protected:
        HandleSet* FindUnusedSet();
        ManagedBuffer* AddBuffer();
        void PrepareDescriptor(const ManagedBuffer& MB, UINT32 HeapIndex) const;
    };


    //--------------------------------------------------------------------------------------
    // Wrapper around upload heap buffer objects that is useful for initializing buffers and textures with initial data
    //--------------------------------------------------------------------------------------
    class CpuGpuHeap
    {
    private:
        D3DDevice* m_pDevice;
        ID3D12Fence* m_pFence;
        HANDLE m_hBlockEvent;
        UINT64 m_currentFence;

        bool m_Readback;
        SIZE_T m_SlabSizeBytes;
        UINT32 m_MaxSlabCount;

        volatile BOOL m_InUse;

        struct HeapSlab
        {
            HeapSlab() : 
                m_pHeap(nullptr),
                m_pBase(nullptr),
                m_currentOffset(0),
                m_fence(0)
            {}

            ID3D12Resource* m_pHeap;
            BYTE* m_pBase;
            SIZE_T m_currentOffset;
            UINT64 m_fence;
        };
        std::vector<HeapSlab> m_Slabs;
        UINT32 m_CurrentSlabIndex;
        bool m_threadSafe;
        wchar_t m_heapName[256];

    public:
        CpuGpuHeap()
            : m_pDevice(nullptr),
            m_pFence(nullptr),
            m_hBlockEvent(nullptr),
            m_threadSafe(false),
            m_currentFence(static_cast<UINT64>(-1))
        {
            InterlockedExchange((volatile UINT*)(&m_InUse), FALSE);
            m_heapName[0] = L'\0';
        }

        void SetThreadSafe(bool isThreadSafe) { m_threadSafe = isThreadSafe; }

        ~CpuGpuHeap()
        {
            Terminate();
        }

        bool IsTerminated() const { return m_pDevice == nullptr; }

        HRESULT Initialize(_In_ D3DDevice* const pDevice, _In_ ID3D12Fence* const pFence, SIZE_T HeapSizeBytesPerSlab, bool Readback = false, UINT32 MaxSlabCount = 2, const wchar_t* heapName = nullptr);
        void Terminate();

        void SetCurrentFence(UINT64 currentFence) { m_currentFence = currentFence; }

        HRESULT GetHeapPointer(SIZE_T SizeBytes, _COM_Outptr_ ID3D12Resource** ppHeap, _Out_ SIZE_T& DestOffsetWithinHeap, _Out_ BYTE **ppData, _In_opt_ SIZE_T AlignmentBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        HRESULT GetHeapPointer(SIZE_T SizeBytes, _Out_ D3D12_GPU_VIRTUAL_ADDRESS& gpuAddress, _Out_ BYTE **ppData, _In_opt_ SIZE_T AlignmentBytes = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);

        HRESULT CopyBufferDataToHeap(const BYTE* pData, SIZE_T SizeBytes, _COM_Outptr_ ID3D12Resource** ppHeap, _Out_ SIZE_T& DestOffsetWithinHeap);
        HRESULT CopyBufferDataToDefaultBuffer(
            const BYTE* pData,
            SIZE_T SizeBytes,
            _In_ D3DCommandList* const pCommandList,
            _In_ ID3D12Resource* const pDefaultResource);

        HRESULT CopyTextureSubresourceToHeap( const BYTE* pData, const D3D12_SUBRESOURCE_FOOTPRINT& srcSubresourceDesc, const D3D12_SUBRESOURCE_FOOTPRINT& destSubresourceDesc, _COM_Outptr_ ID3D12Resource** ppHeap, _Out_ SIZE_T& DestOffsetWithinHeap);
        HRESULT CopyTextureSubresourceToDefaultTexture( const BYTE* pData, const D3D12_SUBRESOURCE_FOOTPRINT& SubresourceDesc, _In_ D3DCommandList* const pCommandList, _In_ ID3D12Resource* const pDefaultResource, UINT32 DestSubresource, _In_opt_ const D3D12_SUBRESOURCE_FOOTPRINT* pDestSubresourceDesc = nullptr );

    private:
        HRESULT Allocate(SIZE_T SizeBytes, SIZE_T AlignmentBytes, _Out_ HeapSlab** ppSlab, _Out_ SIZE_T* pOffsetWithinSlab);
        HRESULT FindAvailableSlab(HeapSlab** ppSlab, SIZE_T SizeBytes, SIZE_T AlignmentBytes);
        HRESULT CreateNewSlab(_Out_opt_ HeapSlab** ppSlab);
    };


    //--------------------------------------------------------------------------------------
    // Funtionality similar to GenerateMips
    //--------------------------------------------------------------------------------------
    //class MipsGenerator
    //{
    //private:
    //    static const UINT c_maxMipLevels = 16;
    //    static const UINT c_maxArrayLevels = 16;
    //    static const UINT c_maxRTV = c_maxMipLevels * c_maxArrayLevels * DXGI_MAX_SWAP_CHAIN_BUFFERS;
    //    _Field_range_(0, c_maxRTV - 1) UINT m_iRTV;

    //    D3DDevice* m_pDevice;
    //    D3DRootSignaturePtr m_spRootSignature;
    //    D3DPipelineStatePtr m_spPSO;
    //    DXGI_FORMAT m_format;

    //    DescriptorHeapWrapper m_SRVHeap;
    //    DescriptorHeapWrapper m_RTVHeap;

    //    D3DBlobPtr m_spVS;
    //    D3DBlobPtr m_spPS1D;
    //    D3DBlobPtr m_spPS2D;
    //    D3DBlobPtr m_spPS3D;
    //    
    //public:
    //    MipsGenerator():
    //        m_pDevice(nullptr),
    //        m_format(DXGI_FORMAT_UNKNOWN),
    //        m_iRTV(0)
    //    {
    //        XSF_ERROR_IF_FAILED(LoadShader(L"Media\\shaders\\GenMipsVS.bin", m_spVS));
    //        XSF_ERROR_IF_FAILED(LoadShader(L"Media\\shaders\\GenMipsPS1D.bin", m_spPS1D));
    //        XSF_ERROR_IF_FAILED(LoadShader(L"Media\\shaders\\GenMipsPS2D.bin", m_spPS2D));
    //        XSF_ERROR_IF_FAILED(LoadShader(L"Media\\shaders\\GenMipsPS3D.bin", m_spPS3D));
    //    }
    //    ~MipsGenerator()
    //    {
    //        Terminate();
    //    }

    //    HRESULT Initialize(_In_ D3DDevice* const pDevice, DXGI_FORMAT Format, D3D12_RESOURCE_DIMENSION Dimension);
    //    HRESULT GenerateMips(_In_ D3DCommandList* const pCmdList, _In_ ID3D12Resource* const pResource, const D3D12_SHADER_RESOURCE_VIEW_DESC* pDescSRV, 
    //                         _In_opt_ D3D12_CPU_DESCRIPTOR_HANDLE* phSRV, _In_opt_ D3D12_RESOURCE_STATES resourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    //    void Terminate();
    //};


#if defined(_XBOX_ONE) && defined(_TITLE)
    //--------------------------------------------------------------------------------------
    // Extension for D3D12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE
    // with initialization and lifetime management
    //--------------------------------------------------------------------------------------
    struct CD3DX12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE : public D3D12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE
    {
    public:
        CD3DX12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE()
        {
            ZeroMemory(this, sizeof(D3D12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE));
        }

        ~CD3DX12XBOX_DESERIALIZE_GRAPHICS_PIPELINE_STATE()
        {
            Terminate();
        }

        void Terminate();

        HRESULT InitializeFromBlob(_In_ ID3DBlob* pBlob, _In_opt_ ID3D12RootSignature* pRootSignature);
        HRESULT InitializeFromMemory(_Inout_ void** ppBlobPointer, bool vMem, _Out_ void** ppMemory, _Out_ SIZE_T* blobSize);
    };
#endif


    //--------------------------------------------------------------------------------------
    // Load single-mip texture 2D DDS files
    //--------------------------------------------------------------------------------------
    class DDSLoader12
    {
    private:
        D3DDevice* m_pDevice;
        D3DCommandAllocator* m_pCmdAllocator;
        D3DCommandQueue* m_pCmdQueue;
        D3DCommandList* m_pCmdList;
        CpuGpuHeap* m_pUploadHeap;

        std::vector<D3D12_RESOURCE_BARRIER> m_FinalDescs;

    public:
        DDSLoader12()
            : m_pDevice(nullptr),
            m_pCmdAllocator(nullptr),
            m_pCmdQueue(nullptr),
            m_pCmdList(nullptr),
            m_pUploadHeap(nullptr)
        {};
        ~DDSLoader12()
        {
            Terminate();
        }


        HRESULT Initialize(_In_ D3DDevice* const pDevice, _In_ D3DCommandQueue* const pCmdQueue, _In_ D3DCommandAllocator* const pCmdAlloc, _In_ CpuGpuHeap* const pUploadHeap);
        void Terminate();

        void BeginLoading(_In_opt_ D3DCommandList* pExistingCmdList = nullptr);
        void FinishLoading(bool SubmitCmdList = true);

        HRESULT LoadDDSFile(_In_z_ const WCHAR* strFileName, D3D12_CPU_DESCRIPTOR_HANDLE DescHandle, _COM_Outptr_ ID3D12Resource** ppTexture, _In_opt_ D3D12_RESOURCE_FLAGS miscFlags = D3D12_RESOURCE_FLAG_NONE);
        HRESULT LoadDDSFromMemory(_In_z_ const uint8_t* ddsData, size_t ddsDataSize, D3D12_CPU_DESCRIPTOR_HANDLE DescHandle, _COM_Outptr_ ID3D12Resource** ppTexture, _In_opt_ D3D12_RESOURCE_FLAGS miscFlags = D3D12_RESOURCE_FLAG_NONE);
    };

}    // XboxSampleFramework


namespace XSF = XboxSampleFramework;
