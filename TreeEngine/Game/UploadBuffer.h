#pragma once

template<typename T>
class UploadBuffer
{
public:
#if defined(TREE3D12)
    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
        mIsConstantBuffer(isConstantBuffer)
    {
        mElementByteSize = sizeof(T);

        // Constant buffer elements need to be multiples of 256 bytes.
        // This is because the hardware can only view constant data 
        // at m*256 byte offsets and of n*256 byte lengths. 
        // typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
        // UINT64 OffsetInBytes; // multiple of 256
        // UINT   SizeInBytes;   // multiple of 256
        // } D3D12_CONSTANT_BUFFER_VIEW_DESC;
        if (isConstantBuffer)
        {
            mElementByteSize = (sizeof(T) + 255) & ~255;
        }

        HR(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize*elementCount),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            __uuidof(ID3D12Resource), (void**)&mUploadBuffer));

        HR(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));

        mapped = true;

        // We do not need to unmap until we are done with the resource.  However, we must not write to
        // the resource while it is in use by the GPU (so we must use synchronization techniques).
    }

    UploadBuffer(const UploadBuffer& rhs) = delete;
    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
    ~UploadBuffer()
    {
        if (mUploadBuffer != nullptr)
        {
            Unmap();
        }

        mMappedData = nullptr;
    }

    HRESULT Map(T** mappedStruct)
    {
        HRESULT hr = E_FAIL;
        if (!mapped)
        {
            *mappedStruct = nullptr;

            hr = mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData));
            if (SUCCEEDED(hr))
            {
                mapped = true;
                *mappedStruct = (T*) mMappedData;
            }
        }

        return hr;
    }

    void Unmap()
    {
        if (mapped)
        {
            mUploadBuffer->Unmap(0, nullptr);
            mapped = false;
        }
    }

    ID3D12Resource* Resource()const
    {
        return mUploadBuffer;
    }

    void CopyData(int elementIndex, const T& data)
    {
        memcpy(&mMappedData[elementIndex*mElementByteSize], &data, sizeof(T));
    }

    void CopyData(int startElementIndex, int numElements, const T* data)
    {
        memcpy(&mMappedData[startElementIndex * mElementByteSize], data, sizeof(T) * numElements);
    }

    D3D12_CONSTANT_BUFFER_VIEW_DESC View()
    {
        return D3D12_CONSTANT_BUFFER_VIEW_DESC
        {
            /*BufferLocation*/ mUploadBuffer->GetGPUVirtualAddress(),
            /*SizeInBytes*/ mElementByteSize
        };
    }

    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress(int index)
    {
        return this->Resource()->GetGPUVirtualAddress() + index * mElementByteSize;
    }

private:
    CComPtr<ID3D12Resource> mUploadBuffer;
    BYTE* mMappedData = nullptr;

    UINT mElementByteSize = 0;
    bool mIsConstantBuffer = false;
    bool mapped = false;

#else
    UploadBuffer(ID3D11Device* device, UINT elementCount, bool isConstantBuffer) //:
        //mIsConstantBuffer(isConstantBuffer)
    {
        // Create constants for per frame 
        D3D11_BUFFER_DESC bd;
        ZeroMemory(&bd, sizeof(bd));
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(T);
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = 0;
        HR(device->CreateBuffer(&bd, nullptr, &mUploadBuffer));
    }

    ID3D11Buffer* Resource() const
    {
        return mUploadBuffer;
    }

private:
    CComPtr<ID3D11Buffer> mUploadBuffer;

#endif
};
