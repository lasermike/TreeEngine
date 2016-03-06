#include "pch.h"
#include <vector>
#include "processenv.h"

//#ifdef _TREE_CLASSIC
//#include "Shlwapi.h"
//#endif

#if defined(TREE3D12)
#include <D3Dcompiler.h>
#endif

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

#elif defined(WIN32)
	GetModuleFileName( NULL, Details::g_strCommonFileRoot, MAX_PATH );
	//PathRemoveFileSpec(Details::g_strCommonFileRoot);
	wstring path = Details::g_strCommonFileRoot;
    size_t found = path.find_last_of(L"/\\");
    _snwprintf_s( Details::g_strCommonFileRoot, _countof( Details::g_strApplicationDataPath ), _TRUNCATE, L"%s", path.substr(0, found).c_str() );
	LOG(Details::g_strCommonFileRoot);

#else
    wchar_t temp[ 1024 ];
//    GetCurrentDirectoryW( _countof( temp ), temp );
	wcscpy_s(temp, Windows::ApplicationModel::Package::Current->InstalledLocation->Path->Begin());

    swprintf_s( Details::g_strCommonFileRoot, L"%s\\", temp);
    //swprintf_s( Details::g_strApplicationDataPath, L"%s\\", temp );
#endif
}

#if defined(TREE3D12)

// Desc: Load a shader blob from file
//--------------------------------------------------------------------------------------
HRESULT XSF::LoadShader(const wchar_t* path, ID3DBlob** ppShader)
{
	VERBOSEATGPROFILETHIS;

	wchar_t tmp[1024];
	_snwprintf_s(tmp, _TRUNCATE, L"%s%s", Details::g_strCommonFileRoot, path);

	return D3DReadFileToBlob(tmp, ppShader);
}
#endif

//--------------------------------------------------------------------------------------
// Name: LoadBlob
// Desc: Reads a file from the read only data content path
//--------------------------------------------------------------------------------------
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

#if defined(TREE3D12)
void SetDebugName(ID3D12DeviceChild* child, const char* name)
{
	child->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(name), name);
}

// Helper function for acquiring the first available hardware adapter that supports Direct3D 12.
// If no such adapter can be found, *ppAdapter will be set to nullptr.
void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter)
{
	CComPtr<IDXGIAdapter1> adapter;
	*ppAdapter = nullptr;

	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Don't select the Basic Render Driver adapter.
			// If you want a software adapter, pass in "/warp" on the command line.
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the
		// actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}

	*ppAdapter = adapter.Detach();
}

#else
//
// Naming
//
#if defined(_DEBUG) && !defined(_XBOX_ONE) // NAMING
void SetDebugName(ID3D11DeviceChild* child, const char* name)
{
	child->SetPrivateData(WKPDID_D3DDebugObjectName, strlen(name), name);
}

#else
#endif // TREE3D12

#endif

