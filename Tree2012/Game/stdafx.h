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
#include "Win32_DirectXAppUtil.h"

#define XSF_USE_DX_11_1

using namespace DirectX;
using namespace std;

#ifdef _DEBUG
__inline void Report(char* msg, char* file, long line, char* exp) 
{
	std::cerr << msg << " " << file << " " << line << " " << exp << "\n"; 
}

__inline void ReportError(char* msg, char* file, long line, char* exp) 
{
	Report(msg, file, line, exp);
	assert(false); 
}

__inline void ReportFailure(char* msg, char* file, long line, HRESULT hr) 
{
	std::cerr << msg << " " << file << " " << line << " " << hr << "\n"; 
	assert(SUCCEEDED(hr)); 
}


#else

void ReportError(char* msg, char* file, long line, char* exp) { }
void ReportFailure(char* msg, char* file, long line, HRESULT hr) { }

#endif 

#if defined(DEBUG) | defined(_DEBUG)

#ifndef HR
#define HR(x)                                              \
	{                                                          \
		HRESULT hr = (x);                                      \
		if (FAILED(hr))                                         \
		{                                                      \
		std::cerr << "ERROR: " << __FILE__ << ": " << (DWORD)__LINE__ << ", HR:" << hr << ", " << L#x << "\n"; \
		assert(SUCCEEDED(hr)); \
		}                                                      \
	}
#endif

#ifndef HRR
#define HRR(x)                                              \
	{                                                          \
		HRESULT hr = (x);                                      \
		if (FAILED(hr))                                         \
		{                                                      \
		std::cerr << "ERROR: " << __FILE__ << ": " << (DWORD)__LINE__ << ", HR:" << hr << ", " << L#x << "\n"; \
		assert(SUCCEEDED(hr)); \
		return hr; \
		}                                                      \
	}
#endif

#ifndef HRC
#define HRC(x)                                              \
		hr = (x);                                      \
		if (FAILED(hr))                                         \
		{                                                      \
			std::cerr << "ERROR: " << __FILE__ << ": " << (DWORD)__LINE__ << ", HR:" << hr << ", " << L#x << "\n"; \
			assert(SUCCEEDED(hr)); \
			goto Cleanup; \
		}                                                      
#endif

#ifndef LOG
#define LOG(x)	\
		{		\
            Util.Output("Log: %s \n", x);  \
		}        
#endif 

#define ASSERTSZ(x, str) \
		if (!(x)) { \
			assert(0 == L#x);   \
			Util.Output("Assert failed: %s \n", L#x); \
		} 
			//wstringstream str; \
			//str << L"LOG: " << __FILE__ << ": " << (DWORD)__LINE__ << ", " << L#x << L"\n"; \
			//OutputDebugString(str.str().c_str());  \

#else
#ifndef HRR
#define HR(x) (x)
#define HRC(x) (x)
#define HRR(x) (x)
#define LOG(x)
#endif
#endif 

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

#else   // NDEBUG

#define XSF_ASSERT( exp )   if( !(exp) ) { ReportError( "assertion failed: %s\n", __FILE__, __LINE__, #exp ); }

#define XSF_RETURN_IF_FAILED( exp ) { HRESULT _hr_ = (exp); if( FAILED( _hr_ ) ) { ReportFailure( "Failure with HRESULT of %x", __FILE__, __LINE__, ( _hr_ ) ); return _hr_; } }

#define XSF_ERROR_IF_FAILED( exp ) { HRESULT _hr_ = (exp); if( FAILED( _hr_ ) ) ReportFailure( "Failure with HRESULT of %x", __FILE__, __LINE__, ( _hr_ ) ); }

#endif  // NDEBUG

// safe release for com pointers
#define XSF_SAFE_ADDREF( o ) if( o ) { (o)->AddRef(); }
#define XSF_SAFE_RELEASE( o ) if( o ) { (o)->Release(); o = nullptr; }
#define XSF_SAFE_RELEASE_C( o ) if( o ) { if( (o)->Release() == 0 ) (o) = nullptr; }


#ifdef XSF_USE_PIX_EVENTS
#ifdef _XBOX_ONE
#include <pix.h>
#pragma comment(lib, "pixEvt")
#else
// PC doesn't support CPU timing
#define PIXBeginEvent( ctx, color, text, ... )
#define PIXEndEvent( ctx )
#define PIXSetMarker( ctx, color, text, ... )
#endif

// Pass the d3d device context as a first parameter, and NULL if there is no relevant context (say, it's a CPU function)
// The "No-op" versions of those are to enforce calling convention in release
// Begin and EndNamedEvents should always come in pairs. SetMarker needs a single call.
// The xxxF versions allow to pass printf style formatted string to the macro. The non-F versions will pass the string
// directly to the device. This is to ensure as little overhead as possible and to enable outputting strings like 0%
// These macros all pre-append L to strings so that calling code can exclude, e.g. XSFBeginNamedEvent( ctx, color, "stuff")
#if defined(ATG_PROFILE) || defined(ATG_PROFILE_VERBOSE)

#define XSFBeginNamedEventF( ctx, color, text, ... )   PIXBeginEvent(color, text, __VA_ARGS__ ); ::XboxSampleFramework::XSFBeginNamedEventFImpl( ctx, text, __VA_ARGS__ )
#define XSFBeginNamedEvent( ctx, color, text )         PIXBeginEvent(color, text); ::XboxSampleFramework::XSFBeginNamedEventImpl( ctx, text )
#define XSFEndNamedEvent( ctx )                        PIXEndEvent(); ::XboxSampleFramework::XSFEndNamedEventImpl( ctx );

// Scoped versions. XSFScopedNamedEvent will open an event in constructor and close it in destructor, so it will wrap a C++ scope
// XSFScopedNamedEventFunc is commonly used to wrap the entire function body in Begin/End named event

#define XSFScopedNamedEvent( ctx, color, text, ... )   ATGPROFILELABEL( text ); ::XboxSampleFramework::XsfScopedNamedEvent   XSF_PASTE( pixEvent, __LINE__ ) ( ctx, color, text, __VA_ARGS__ );
#define XSFScopedNamedEventFunc( ctx, color )          ATGPROFILETHIS; ::XboxSampleFramework::XsfScopedNamedEvent   XSF_PASTE( pixEvent, __LINE__ ) ( ctx, color, XSF_PASTE( L, __FUNCTION__ ) );

#else

#define XSFBeginNamedEventF( ctx, color, text, ... )   PIXBeginEvent(color, text, __VA_ARGS__ ); ::XboxSampleFramework::XSFBeginNamedEventFImpl( ctx, text, __VA_ARGS__ )
#define XSFBeginNamedEvent( ctx, color, text )         PIXBeginEvent(color, text); ::XboxSampleFramework::XSFBeginNamedEventImpl( ctx, text )
#define XSFEndNamedEvent( ctx )                        PIXEndEvent(); ::XboxSampleFramework::XSFEndNamedEventImpl( ctx )

// Scoped versions. XSFScopedNamedEvent will open an event in constructor and close it in destructor, so it will wrap a C++ scope
// XSFScopedNamedEventFunc is commonly used to wrap the entire function body in Begin/End named event
#define XSFScopedNamedEvent( ctx, color, text, ... )   ::XboxSampleFramework::XsfScopedNamedEvent   XSF_PASTE( pixEvent, __LINE__ ) ( ctx, color, text, __VA_ARGS__ );
#define XSFScopedNamedEventFunc( ctx, color )          ::XboxSampleFramework::XsfScopedNamedEvent   XSF_PASTE( pixEvent, __LINE__ ) ( ctx, color,  XSF_PASTE( L, __FUNCTION__ ) );
#endif

#else

#define XSFBeginNamedEventF( ctx, color, text, ... )   
#define XSFBeginNamedEvent( ctx, color, text )         
#define XSFEndNamedEvent( ctx )                        

#if defined(ATG_PROFILE) || defined(ATG_PROFILE_VERBOSE)
#define XSFScopedNamedEvent( ctx, color, text, ... )   ATGPROFILELABEL(text);
#define XSFScopedNamedEventFunc( ctx, color )          ATGPROFILETHIS;
#else
#define XSFSetMarkerF( ctx, color, text, ... )         
#define XSFSetMarker( ctx, color, text )               
#define XSFScopedNamedEvent( ctx, color, text, ... )   
#define XSFScopedNamedEventFunc( ctx, color )          
#endif

#endif


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

#else

// Null versions
#define ATGPROFILETHIS
#define STARTATGPROFILETHIS
#define ENDATGPROFILETHIS
#define STARTATGPROFILELABEL( a )
#define ATGPROFILELABEL( a )
#define ENDATGPROFILELABEL
#define VERBOSEATGPROFILETHIS
#define VERBOSESTARTATGPROFILETHIS
#define VERBOSEENDATGPROFILETHIS
#define VERBOSESTARTATGPROFILELABEL( a )
#define VERBOSEATGPROFILELABEL( a )
#define VERBOSEENDATGPROFILELABEL

#endif


namespace XboxSampleFramework
{
    // Auto-releasing D3D resources
    template< typename t_Resource >
    struct D3DTypePtr  //: public Microsoft::WRL::ComPtr< t_Resource >
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

    typedef D3DTypePtr< ID3D11Buffer >              D3DBufferPtr;



#if defined(_XBOX_ONE) && defined(_TITLE)
    typedef ID3D11DeviceX           D3DDevice;
    typedef ID3D11DeviceContextX    D3DDeviceContext;
    typedef ID3D11ComputeContextX   D3DComputeContext;
    typedef ID3D11RasterizerState1  D3DRasterizerState;
    typedef D3D11_RASTERIZER_DESC1  D3DRasterizerDesc;
    typedef IDXGISwapChain1         DXGISwapChain;
#elif defined( XSF_USE_DX_11_1 )
    typedef ID3D11Device1           D3DDevice;
    typedef ID3D11DeviceContext1    D3DDeviceContext;
    typedef ID3D11DeviceContext1    D3DComputeContext;
    typedef ID3D11RasterizerState1  D3DRasterizerState;
    typedef D3D11_RASTERIZER_DESC1  D3DRasterizerDesc;
    typedef IDXGISwapChain1         DXGISwapChain;
#else
    typedef ID3D11Device            D3DDevice;
    typedef ID3D11DeviceContext     D3DDeviceContext;
    typedef ID3D11DeviceContext     D3DComputeContext;
    typedef ID3D11RasterizerState   D3DRasterizerState;
    typedef D3D11_RASTERIZER_DESC   D3DRasterizerDesc;
    typedef IDXGISwapChain          DXGISwapChain;
#endif

    void DebugPrint( _In_z_ const char* msg, ... );
    void DebugPrint( _In_z_ const wchar_t* msg, ... );
    void PrintNoVarargs( _In_z_ const wchar_t* msg );

	void SetContentFileRoot();

    _Check_return_
    HRESULT LoadBlob( _In_z_ const wchar_t* pFilename, std::vector< BYTE >& data );
    _Check_return_
    HRESULT LoadPixelShader( _In_ ID3D11Device* pDevice, _In_z_ const wchar_t* fileName, _COM_Outptr_ ID3D11PixelShader** ppPS, _In_opt_ std::vector< BYTE >* pData = nullptr );
    _Check_return_
    HRESULT LoadVertexShader( _In_ ID3D11Device* pDevice, _In_z_ const wchar_t* fileName, _COM_Outptr_ ID3D11VertexShader** ppVS,
                              _In_opt_ const D3D11_INPUT_ELEMENT_DESC* pInputElementDesc = NULL, _In_opt_ UINT numElements = 0, _COM_Outptr_ ID3D11InputLayout** ppInputLayout = NULL, _In_opt_ std::vector< BYTE >* pData = nullptr );

    //--------------------------------------------------------------------------------------
    // Name: DynamicBuffer
    // Desc: Implements regular dynamic vertex/index/constant buffer. Uses placement memory on ERA
    //-------------------------------------------------------------------------------------
    class DynamicBuffer
    {
        D3DBufferPtr        m_spBuffer;
        D3DDeviceContext*   m_mappedOnContext;
        UINT                m_bufferTailOffset;
        UINT                m_numBytesMapped;
        UINT                m_bufferSize;
        BOOL                m_alwaysDiscard;

    public:
        DynamicBuffer();
        ~DynamicBuffer();

        _Check_return_
        HRESULT Create( _In_ D3DDevice* pDev, D3D11_BIND_FLAG bindFlags, UINT size );
        void    Destroy();

        _Check_return_
        HRESULT Map( _In_ D3DDeviceContext* pCtx, UINT numBytesToMap, _Outptr_ void** ppData, _Out_opt_ UINT* pOffset = nullptr );
        void    Unmap( UINT numBytesUsed = 0 );

        ID3D11Buffer* const&   GetBuffer() const;       // * const& is to be able to write &GetBuffer() to get a ** for passing to D3D functions
        UINT            GetNumBytesLastMapped() const;
        UINT            GetTailOffset() const;

    private:
        DynamicBuffer( const DynamicBuffer& );// = delete;
        DynamicBuffer& operator = ( const DynamicBuffer& );// = delete;
    };

}

namespace XSF = XboxSampleFramework;

#endif