#pragma once 
#ifndef TREEUTILS_H
#define TREEUTILS_H

 
//#define NOMINMAX 
#include <assert.h>
#include <iostream>
#include <vector>
#include <CComPtr.h>
#include <fstream>
#include <sstream>
#include <functional>
#include <algorithm>
#include <map>
#include <list>

#define XSF_USE_DX_11_1

using namespace DirectX;
using namespace std;

///////////////////////////////////////////////
//
// Error reporting and logging
//
#ifdef _DEBUG
__inline void Report(const char* msg, const char* file, long line, const char* exp)
{
    std::cerr << msg << " " << file << " " << line << " " << exp << "\n"; 
}

__inline void ReportError(const char* msg, const char* file, long line, const char* exp)
{
    Report(msg, file, line, exp);
    assert(false); 
}

__inline void ReportFailure(const char* msg, const char* file, long line, HRESULT hr)
{
    std::cerr << msg << " " << file << " " << line << " " << hr << "\n"; 
    assert(SUCCEEDED(hr)); 
}


#else

__inline void ReportError(const char* msg, const char* file, long line, const char* exp) { }
__inline void ReportFailure(const char* msg, const char* file, long line, HRESULT hr) { }

#endif 

#if defined(DEBUG) | defined(_DEBUG)

#ifndef HR
#define HR(x)                                              \
    {                                                          \
        HRESULT hr2 = (x);                                      \
        if (FAILED(hr2))                                         \
        {                                                      \
            Util.Output("ERROR: %s(%d), HR:%x - " #x "\n", __FILE__, (DWORD)__LINE__, hr2);  \
        assert(SUCCEEDED(hr2)); \
        }                                                      \
    }
#endif

#ifndef HRR
#define HRR(x)                                              \
    {                                                          \
        HRESULT hr2 = (x);                                      \
        if (FAILED(hr2))                                         \
        {                                                      \
            Util.Output("ERROR: %s(%d), HR:%x - " #x "\n", __FILE__, (DWORD)__LINE__, hr2);  \
            assert(SUCCEEDED(hr2)); \
            return hr2; \
        }                                                      \
    }
#endif

#ifndef HRC
#define HRC(x)                                              \
        hr = (x);                                      \
        if (FAILED(hr))                                         \
        {                                                      \
            Util.Output("ERROR: %s(%d), HR:%x - " #x "\n", __FILE__, (DWORD)__LINE__, hr);  \
            assert(SUCCEEDED(hr)); \
            goto Cleanup; \
        }                                                      
#endif

#ifndef LOG
#define LOG(x)    \
        {        \
            Util.Output("Log: %s \n", x);  \
        }        
#endif 

#define ASSERTSZ(x, str) \
        if (!(x)) { \
            assert(0);   \
            Util.Output("Assert failed: %s \n", L#str); \
        } 
            //wstringstream str; \
            //str << L"LOG: " << __FILE__ << ": " << (DWORD)__LINE__ << ", " << L#x << L"\n"; \
            //OutputDebugString(str.str().c_str());  \

#else
#ifndef HRR
#define HR(x) (x)
#define HRC(x) if (FAILED(x)) goto Cleanup;
#define HRR(x) (x)
#define LOG(x)
#define ASSERTSZ(x, str)
#endif
#endif 

struct Utility
{
    void Output(const char * fnt, ...)
    {
        static char string_text[256 * 256];
        va_list args; va_start(args, fnt);
        vsprintf_s(string_text, fnt, args);
        va_end(args);
        OutputDebugStringA(string_text);
    }
} static Util;

///////////////////////////////////////////////
//
// Safe macros
//

template <class T>
void SafeRelease(T* obj)
{
    if (*obj)
    {
        (*obj)->Release();
        (*obj) = nullptr;
    }
}

template <class T>
void SafeDelete(T* obj)
{
    if (*obj)
    {
        delete (*obj);
        (*obj) = nullptr;
    }
}

// XSF macros

// XSF_ASSERT
#ifdef NDEBUG

#define XSF_ASSERT( exp )  exp 
#define XSF_RETURN_IF_FAILED( exp ) exp   
#define XSF_ERROR_IF_FAILED( exp ) exp  

#if defined(TREE3D12)
__inline void SetDebugName(ID3D12DeviceChild* /*child*/, const wchar_t* /*name*/) { }
//void GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter);
#elif defined(TREE3D11)
__inline void SetDebugName(ID3D11DeviceChild* /*child*/, const char* /*name*/) { }
#endif // DX12

#else   // NDEBUG -> DEBUG

#define XSF_ASSERT( exp )   if( !(exp) ) { ReportError( "assertion failed: %s\n", __FILE__, __LINE__, #exp ); }

#define XSF_RETURN_IF_FAILED( exp ) { HRESULT _hr_ = (exp); if( FAILED( _hr_ ) ) { ReportFailure( "Failure with HRESULT of %x", __FILE__, __LINE__, ( _hr_ ) ); return _hr_; } }

#define XSF_ERROR_IF_FAILED( exp ) { HRESULT _hr_ = (exp); if( FAILED( _hr_ ) ) ReportFailure( "Failure with HRESULT of %x", __FILE__, __LINE__, ( _hr_ ) ); }

//
// Naming of objects
//
#if defined(TREE3D12)
    void SetDebugName(ID3D12DeviceChild* child, const wchar_t* name);

#elif defined(TREE3D11)
    void SetDebugName(ID3D11DeviceChild* child, const char* name);
#endif

#endif  // NDEBUG

// safe release for com pointers
#define XSF_SAFE_ADDREF( o ) if( o ) { (o)->AddRef(); }
#define XSF_SAFE_RELEASE( o ) if( o ) { (o)->Release(); o = nullptr; }
#define XSF_SAFE_RELEASE_C( o ) if( o ) { if( (o)->Release() == 0 ) (o) = nullptr; }


///////////////////////////////////////////////
//
// PIX markers and events
//


#if defined(TREE_XBOX)
#include <pix.h>
#pragma comment(lib, "pixEvt")
#else
#include "pix3.h"
#endif

#if defined(ATG_INSTRUMENTATION)

#if defined(ATG_PROFILE_VERBOSE)

#define ATGPROFILETHIS XboxSampleFramework::ATGProfiler::Timer __perf_timer( __FUNCSIG__ )
#define STARTATGPROFILETHIS { XboxSampleFramework::ATGProfiler::Timer __perf_timer( __FUNCSIG__ )
#define ENDATGPROFILETHIS }
#define STARTATGPROFILELABEL( a ) { XboxSampleFramework::ATGProfiler::Timer __perf_timer( a )
#define ENDATGPROFILELABEL }
#define ATGPROFILELABEL( a ) XboxSampleFramework::ATGProfiler::Timer __perf_timer( a )
#define VERBOSEATGPROFILETHIS XboxSampleFramework::ATGProfiler::Timer __perf_timer( __FUNCSIG__ )
#define VERBOSESTARTATGPROFILETHIS { XboxSampleFramework::ATGProfiler::Timer __perf_timer( __FUNCSIG__ )
#define VERBOSEENDATGPROFILETHIS }
#define VERBOSESTARTATGPROFILELABEL( a ) { XboxSampleFramework::ATGProfiler::Timer __perf_timer( a )
#define VERBOSEATGPROFILELABEL XboxSampleFramework::ATGProfiler::Timer __perf_timer( a )
#define VERBOSEENDATGPROFILELABEL }

#elif defined(ATG_PROFILE)

#define ATGPROFILETHIS XboxSampleFramework::ATGProfiler::Timer __perf_timer( __FUNCSIG__ )
#define STARTATGPROFILETHIS { XboxSampleFramework::ATGProfiler::Timer __perf_timer( __FUNCSIG__ )
#define ENDATGPROFILETHIS }
#define STARTATGPROFILELABEL( a ) { XboxSampleFramework::ATGProfiler::Timer __perf_timer( a )
#define ATGPROFILELABEL( a ) XboxSampleFramework::ATGProfiler::Timer __perf_timer( a )
#define ENDATGPROFILELABEL }
#define VERBOSEATGPROFILETHIS
#define VERBOSESTARTATGPROFILETHIS
#define VERBOSEENDATGPROFILETHIS
#define VERBOSESTARTATGPROFILELABEL( a )
#define VERBOSEATGPROFILELABEL( a )
#define VERBOSEENDATGPROFILELABEL

#endif 

#else // No ATG_INSTRUMENTATION

// Null versions
#define ATGPROFILETHIS
#define STARTATGPROFILETHIS
#define ENDATGPROFILETHIS
#define STARTATGPROFILELABEL( a )
#define ATGPROFILELABEL( a )
#define ENDATGPROFILELABEL
#define VERBOSEATGPROFILETHIS ;
#define VERBOSESTARTATGPROFILETHIS
#define VERBOSEENDATGPROFILETHIS
#define VERBOSESTARTATGPROFILELABEL( a )
#define VERBOSEATGPROFILELABEL( a )
#define VERBOSEENDATGPROFILELABEL

#endif 

const UINT64 XTF_COLOR_DRAW_TEXT = 0xFF0000FF;
const UINT64 TREE_COLOR_DRAW_TEXT = 0x0000FFFF;

///////////////////////////////////////////////
//
// Utilities
//

#if defined( _XBOX_ONE ) || defined( TREE_XBOX )
#define XSF_TEXTURE_DATA_PITCH_ALIGNMENT D3D12XBOX_TEXTURE_DATA_PITCH_ALIGNMENT
#else
#define XSF_TEXTURE_DATA_PITCH_ALIGNMENT D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
#endif

//--------------------------------------------------------------------------------------
// Name: IsPowerOfTwo
// Desc: Is n a power of two?
//--------------------------------------------------------------------------------------
inline bool IsPowerOfTwo(UINT64 n)
{
    return ((n & (n - 1)) == 0 && (n) != 0);
}

//--------------------------------------------------------------------------------------
// Name: NextMultiple
// Desc: Next multiple after value
//--------------------------------------------------------------------------------------
inline UINT64 NextMultiple(UINT64 value, UINT64 multiple)
{
    XSF_ASSERT(IsPowerOfTwo(multiple));

    return (value + multiple - 1) & ~(multiple - 1);
}

namespace XboxSampleFramework
{
    // Auto-releasing D3D resources
    template< typename t_Resource >
    struct D3DTypePtr
    {
        t_Resource* ptr;

        operator t_Resource* () { return ptr; }
        operator const t_Resource* () const { return ptr; }

        t_Resource** operator &() { return &ptr; }

        void Release()
        {
            if (ptr)
            {
                ptr->Release();
                ptr = nullptr;
            }
        }

        //// This type traits infrastructure allows you to cast D3DTypePtr< t_Resource > to D3DTypePtr< t_Other > 
        //// whenever you can cast t_Resource to t_Other.
        ////
        //// The cast operator is declared for all t_Other, but only implemented when 
        //// std::is_convertible< t_Resource, t_Other >::value == true
        //// 
        //// Note this facility already exists for ComPtr, but we need to lift it to D3DTypePtr.
        //template< typename t_Other, bool t_bAllowed > struct Typecast;

        //template< typename t_Other > 
        //struct Typecast< t_Other, true >
        //{
        //    static D3DTypePtr< t_Other >& allowed_cast( D3DTypePtr< t_Resource >& from ) 
        //    { 
        //        return reinterpret_cast< D3DTypePtr< t_Other >& >( from ); 
        //    }
        //};

        //template< typename t_Other > 
        //operator D3DTypePtr< t_Other >& () { return Typecast< t_Other, std::is_convertible< t_Resource, t_Other >::value >::allowed_cast( *this ); }
    };

#if defined(TREE3D12)
    typedef D3DTypePtr< ID3D12Resource >        D3DBufferPtr;
    typedef D3DTypePtr<ID3D12Device>            D3DDevicePtr;
    typedef CComPtr<ID3D12RootSignature>        D3DRootSignaturePtr;
    typedef CComPtr<ID3D12PipelineState>        D3DPipelineStatePtr;
    typedef CComPtr<ID3DBlob>                    D3DBlobPtr;
    
#elif defined(TREE3D11)

    typedef D3DTypePtr< ID3D11Buffer >              D3DBufferPtr;
#endif


#if defined(_XBOX_ONE) && defined(_TITLE)
    typedef ID3D11DeviceX           D3DDevice;
    typedef ID3D11DeviceContextX    D3DDeviceContext;
    typedef ID3D11ComputeContextX   D3DComputeContext;
    typedef ID3D11RasterizerState1  D3DRasterizerState;
    typedef D3D11_RASTERIZER_DESC1  D3DRasterizerDesc;
    typedef IDXGISwapChain1         DXGISwapChain;
#elif defined( TREE3D12 )
    typedef ID3D12Device              D3DDevice;
    typedef ID3D12GraphicsCommandList D3DDeviceContext;
    typedef ID3D12GraphicsCommandList D3DComputeContext;
    typedef ID3D12CommandQueue      D3DCommandQueue;
    typedef ID3D12CommandAllocator  D3DCommandAllocator;
    typedef ID3D12GraphicsCommandList D3DCommandList;
    typedef IDXGISwapChain          DXGISwapChain;
#elif defined( TREE3D11 )
    typedef ID3D11Device1           D3DDevice;
    typedef ID3D11DeviceContext1    D3DDeviceContext;
    typedef ID3D11DeviceContext1    D3DComputeContext;
    typedef ID3D11RasterizerState1  D3DRasterizerState;
    typedef D3D11_RASTERIZER_DESC1  D3DRasterizerDesc;
    typedef IDXGISwapChain1         DXGISwapChain;
#endif

    void DebugPrint( _In_z_ const char* msg, ... );
    void DebugPrint( _In_z_ const wchar_t* msg, ... );
    void PrintNoVarargs( _In_z_ const wchar_t* msg );

    HRESULT LoadBlob(_In_z_ const wchar_t* pFilename, std::vector< BYTE >& data);

#if defined(TREE3D12)
    HRESULT LoadShader(const wchar_t* path, ID3DBlob** ppShader);
#endif
}

namespace GameCommon
{
    void SetContentFileRoot();
    const wchar_t* GetContentFileRoot();
}

namespace XSF = XboxSampleFramework;
using namespace XSF;

#endif

#if defined(TREE3D12)

#ifndef IID_GRAPHICS_PPV_ARGS
#define IID_GRAPHICS_PPV_ARGS IID_PPV_ARGS
#endif

#include "D3D12Util.h"
#endif
