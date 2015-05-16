#include "pch.h"
#include <vector>
#include "processenv.h"

//#ifdef _TREE_CLASSIC
//#include "Shlwapi.h"
//#endif

namespace XboxSampleFramework
{
    namespace Details
    {
        wchar_t    g_strCommonFileRoot[ 1024 ];
        wchar_t    g_strApplicationDataPath[ 1024 ];
	}
}

//--------------------------------------------------------------------------------------
// Name: SetContentFileRoot
// Desc: Retrieves a game package read-only data path and stores it for future use
//--------------------------------------------------------------------------------------
void XSF::SetContentFileRoot()
{
#ifdef _XBOX_ONE

    std::wstring installFolder = Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Data();

    // Remove any trailing "\\" from the end of installFolder path
    installFolder.erase( installFolder.find_last_not_of( L"\\" ) + 1 );

    _snwprintf_s( Details::g_strCommonFileRoot, _countof( Details::g_strCommonFileRoot ), _TRUNCATE, L"%s\\", installFolder.c_str() );

    // The d: drive designator is "developer scratch space"
    std::wstring  writeableFolder = L"d:\\";

    // Remove any trailing "\\" from the end of writeableFolder path
    writeableFolder.erase( writeableFolder.find_last_not_of( L"\\" ) + 1 );

    _snwprintf_s( Details::g_strApplicationDataPath, _countof( Details::g_strApplicationDataPath ), _TRUNCATE, L"%s\\", writeableFolder.c_str() );

#elif defined(_TREE_CLASSIC)
	GetModuleFileName( NULL, Details::g_strCommonFileRoot, MAX_PATH );
	//PathRemoveFileSpec(Details::g_strCommonFileRoot);
	wstring path = Details::g_strCommonFileRoot;
    size_t found = path.find_last_of(L"/\\");
    _snwprintf_s( Details::g_strCommonFileRoot, _countof( Details::g_strApplicationDataPath ), _TRUNCATE, L"%s", path.substr(0, found).c_str() );
	LOG(Details::g_strCommonFileRoot);

#else
//    wchar_t temp[ 1024 ];
//    GetCurrentDirectoryW( _countof( temp ), temp );
	wcscpy_s(Details::g_strCommonFileRoot, Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Begin());

    //swprintf_s( Details::g_strCommonFileRoot, L"%s\\", installFolder.front() );
    //swprintf_s( Details::g_strApplicationDataPath, L"%s\\", temp );
#endif
}

//--------------------------------------------------------------------------------------
// Name: LoadBlob
// Desc: Reads a file from the read only data content path
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT XSF::LoadBlob( const wchar_t* pFilename, std::vector< BYTE >& data )
{
    data.clear();

    wchar_t tmp[ 1024 ];
    _snwprintf_s( tmp, _countof( tmp ), _TRUNCATE, L"%s\\%s", Details::g_strCommonFileRoot, pFilename );

    FILE* fp = nullptr;

    _wfopen_s( &fp, tmp, L"rb" );

    if( fp )
    {
        fseek( fp, 0, SEEK_END );
        long sz = ftell( fp );
        fseek( fp, 0, SEEK_SET );

        try
        {
            data.resize( sz );
        }
        catch( std::bad_alloc & )
        {
            return E_OUTOFMEMORY;
        }

        if( sz > 0 )
        {
            fread( &data[ 0 ], sz, 1, fp );
        }

        fclose( fp );

#ifdef DEBUG
        DebugPrint( L"LoadBlob: Read file %s, size=%d\n", tmp, sz );
#endif

        return S_OK;
    }

    // Only output a failed message if this was not the "test" file
    if ( _wcsicmp( pFilename, L"test" ) )
    {
        DebugPrint( L"LoadBlob: Failed to open file %s\n", tmp );
    }

    return E_FAIL;
}

//--------------------------------------------------------------------------------------
// Name: LoadPixelShader()
// Desc: Load a pixel shader
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT XSF::LoadPixelShader( ID3D11Device* pDev, const wchar_t* path, ID3D11PixelShader** ppPS, std::vector< BYTE >* pData )
{
    std::vector< BYTE > data;
    if( !pData )
        pData = &data;

    HRESULT hr = XSF::LoadBlob( path, *pData );
    if( FAILED( hr ) )
        return hr;

    return pDev->CreatePixelShader( &(*pData)[ 0 ], pData->size(), nullptr, ppPS );
}

//--------------------------------------------------------------------------------------
// Name: LoadVertexShader()
// Desc: Load a vertex shader
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT XSF::LoadVertexShader( ID3D11Device* pDev, const wchar_t* path, ID3D11VertexShader** ppVS, 
                               const D3D11_INPUT_ELEMENT_DESC* pInputElementDesc, UINT numElements, ID3D11InputLayout** ppInputLayout,
                               std::vector< BYTE >* pData )
{
    if( ppInputLayout )
        *ppInputLayout = nullptr;

    std::vector< BYTE > data;
    if( !pData )
        pData = &data;

    HRESULT hr = XSF::LoadBlob( path, *pData );
    if( FAILED( hr ) )
    {
        return hr;
    }

    hr = pDev->CreateVertexShader( &(*pData)[ 0 ], pData->size(), nullptr, ppVS );
    if( FAILED( hr ) )
    {
        return hr;
    }

    if ( pInputElementDesc && numElements && ppInputLayout )
    {
        hr = pDev->CreateInputLayout( pInputElementDesc, numElements, &(*pData)[ 0 ], pData->size(), ppInputLayout );
        if ( FAILED( hr ) )
        {
            return hr;
        }        
    }

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Name: DebugPrint
// Desc: Outputs a message to the debugger
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void XSF::DebugPrint( const char* msg, ... )
{
    va_list ap;

    va_start( ap, msg );

    char   buf[ 4096 ];
    vsprintf_s( buf, msg, ap );
    
    va_end( ap );

#ifndef NDEBUG
    OutputDebugStringA( buf );
    printf( buf );
#endif

    // Pass to the intercept if given
    /*if( Details::g_pLoggingFunctionIntercept )
    {
        Details::g_pLoggingFunctionIntercept( buf );
    }*/
}

//--------------------------------------------------------------------------------------
// Name: DebugPrint
// Desc: Outputs a message to the debugger
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void XSF::DebugPrint( const wchar_t* msg, ... )
{
    wchar_t buf[ 4096 ];

    va_list ap;

    va_start( ap, msg );

    vswprintf_s( buf, _countof( buf ) - 1, msg, ap );
    buf[ _countof( buf ) - 1 ] = 0;

    va_end( ap );

    msg = buf;

    PrintNoVarargs( buf );
}

//--------------------------------------------------------------------------------------
// Name: PrintNoVarargs
// Desc: Outputs a message to the debugger
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void XSF::PrintNoVarargs( const wchar_t* msg )
{
    OutputDebugStringW( msg );

    // Pass to the intercept if given
    /*if( Details::g_pLoggingFunctionIntercept )
    {
        char buf[ 4096 ];
        size_t converted;
        wcstombs_s( &converted, buf, msg, _countof( buf ) - 1 );
        buf[ converted ] = 0;
        Details::g_pLoggingFunctionIntercept( buf );
    }*/
}

//--------------------------------------------------------------------------------------
// Name: DynamicBuffer
// Desc: Constructs an empty dynamic buffer
//-------------------------------------------------------------------------------------
XSF::DynamicBuffer::DynamicBuffer() : m_mappedOnContext( nullptr ),
                                      m_bufferTailOffset( 0 ),
                                      m_numBytesMapped( 0 ),
                                      m_bufferSize( 0 ),
                                      m_alwaysDiscard( FALSE )
{
}

//--------------------------------------------------------------------------------------
// Name: DynamicBuffer
// Desc: Constructs an empty dynamic buffer
//-------------------------------------------------------------------------------------
XSF::DynamicBuffer::~DynamicBuffer()
{
    Destroy();
}

//--------------------------------------------------------------------------------------
// Name: DynamicBuffer
// Desc: Constructs an empty dynamic buffer
//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT XSF::DynamicBuffer::Create( D3DDevice* pDev, D3D11_BIND_FLAG bindFlags, UINT size )
{
    VERBOSEATGPROFILETHIS;

    D3D11_BUFFER_DESC bufDesc = { 0 };
    bufDesc.BindFlags = bindFlags;
    bufDesc.ByteWidth = size;
    bufDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    XSF_RETURN_IF_FAILED( pDev->CreateBuffer( &bufDesc, nullptr, &m_spBuffer) );
    
    m_bufferTailOffset = 0;
    m_numBytesMapped = 0;
    m_bufferSize = size;
    m_alwaysDiscard = FALSE;

#ifndef _XBOX_ONE
    // determine if hw supports map no overwrite
    if( bindFlags & ( D3D11_BIND_CONSTANT_BUFFER | D3D11_BIND_SHADER_RESOURCE ) )
    {
        D3D11_FEATURE_DATA_D3D11_OPTIONS options;
        XSF_RETURN_IF_FAILED( pDev->CheckFeatureSupport( D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS) ) );
        if( ( ( bindFlags & D3D11_BIND_CONSTANT_BUFFER ) && !options.MapNoOverwriteOnDynamicConstantBuffer ) ||
            ( ( bindFlags & D3D11_BIND_SHADER_RESOURCE ) && !options.MapNoOverwriteOnDynamicBufferSRV ) )
        {
            m_alwaysDiscard = TRUE;
        }
    }
#endif

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Name: Destroy
// Desc: Release the internal buffer object
//-------------------------------------------------------------------------------------
void  XSF::DynamicBuffer::Destroy()
{
    m_spBuffer.Release();
    m_bufferTailOffset = 0;
    m_numBytesMapped = 0;
    m_bufferSize = 0;
}

//--------------------------------------------------------------------------------------
// Name: Map
// Desc: Returns a pointer to mapped buffer memory
//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT XSF::DynamicBuffer::Map( D3DDeviceContext* pCtx, UINT numBytesToMap, void** ppData, UINT* pOffset )
{
    VERBOSEATGPROFILETHIS;

    XSF_ASSERT( !m_mappedOnContext );
    XSF_ASSERT( pCtx );
    XSF_ASSERT( ppData );
    XSF_ASSERT( numBytesToMap <= m_bufferSize );

    m_mappedOnContext = pCtx;

    // The first Map on a deferred context should be with a DISCARD. There is no easy way to know
    // which one is the first call so just discard anyway
    const BOOL bDeferred = pCtx->GetType() == D3D11_DEVICE_CONTEXT_DEFERRED;

    if( m_bufferTailOffset + numBytesToMap > m_bufferSize ||
        bDeferred || m_alwaysDiscard )
    {
        D3D11_MAPPED_SUBRESOURCE data;
        XSF_RETURN_IF_FAILED( pCtx->Map( m_spBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &data ) );

        *ppData = data.pData;
        m_bufferTailOffset = 0;
    } else
    {
        D3D11_MAPPED_SUBRESOURCE data;
        XSF_RETURN_IF_FAILED( pCtx->Map( m_spBuffer, 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &data ) );
        
        *ppData = (BYTE*)data.pData + m_bufferTailOffset;
    }

    if( pOffset )
        *pOffset = m_bufferTailOffset;

    m_numBytesMapped = numBytesToMap;

    return S_OK;
}

//--------------------------------------------------------------------------------------
// Name: Unmap
// Desc: Unmaps buffer memory
//-------------------------------------------------------------------------------------
void XSF::DynamicBuffer::Unmap( UINT numBytesUsed )
{
    VERBOSEATGPROFILETHIS;

    XSF_ASSERT( m_mappedOnContext );

    if( !numBytesUsed )
        numBytesUsed = m_numBytesMapped;

    m_bufferTailOffset += numBytesUsed;

    m_mappedOnContext->Unmap( m_spBuffer, 0 );
    m_mappedOnContext = nullptr;
}

//--------------------------------------------------------------------------------------
// Name: GetBuffer
// Desc: Returns the buffer
//-------------------------------------------------------------------------------------
ID3D11Buffer* const&   XSF::DynamicBuffer::GetBuffer() const
{
    return m_spBuffer.ptr;
}

//--------------------------------------------------------------------------------------
// Name: GetNumBytesLastMapped
// Desc: Returns how many bytes was mapped last
//-------------------------------------------------------------------------------------
UINT XSF::DynamicBuffer::GetNumBytesLastMapped() const
{
    return m_numBytesMapped;
}

//--------------------------------------------------------------------------------------
// Name: GetTailOffset
// Desc: Returns the current tail offset
//-------------------------------------------------------------------------------------
UINT XSF::DynamicBuffer::GetTailOffset() const
{
    return m_bufferTailOffset;
}
