// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the RENDERPLATFORM12_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// RENDERPLATFORM12_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef RENDERPLATFORM12_EXPORTS
#define RENDERPLATFORM12_API __declspec(dllexport)
#else
#define RENDERPLATFORM12_API __declspec(dllimport)
#endif

// This class is exported from the RenderPlatform12.dll
class RENDERPLATFORM12_API CRenderPlatform12 {
public:
	CRenderPlatform12(void);
	// TODO: add your methods here.
};

extern RENDERPLATFORM12_API int nRenderPlatform12;

RENDERPLATFORM12_API int fnRenderPlatform12(void);
