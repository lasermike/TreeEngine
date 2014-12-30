#include "pch.h"
#include <vector>
#include "processenv.h"

namespace XboxSampleFramework
{
    namespace Details
    {
        wchar_t    g_strCommonFileRoot[ 1024 ];
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
HRESULT XSF::LoadPixelShader( XSF::D3DDevice* pDev, const wchar_t* path, ID3D11PixelShader** ppPS, std::vector< BYTE >* pData )
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
HRESULT XSF::LoadVertexShader( XSF::D3DDevice* pDev, const wchar_t* path, ID3D11VertexShader** ppVS, 
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
